#include "PdmAudioPlayer.h"

#include <driver/i2s.h>

namespace
{
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr uint32_t kTickRateNumerator = 138U * 4U;
constexpr uint32_t kTickRateDenominator = PdmAudioPlayer::kSampleRate * 60U;
constexpr uint16_t kEnvelopeMaximum = 32767;
constexpr uint16_t kAttackStep = 342;  // About 4 ms at 24 kHz.
constexpr uint16_t kReleaseStep = 114; // About 12 ms at 24 kHz.
constexpr int32_t kMaximumAmplitude = 14500;
constexpr int32_t kPositiveHeadroom = 256;

// MIDI 47 (B2) through 79 (G5), calculated for a 24 kHz, 32-bit DDS.
constexpr uint32_t kPhaseIncrements[] = {
    22095965, 23409859, 24801882, 26276679, 27839171, 29494575,
    31248413, 33106541, 35075158, 37160835, 39370534, 41711627,
    44191930, 46819719, 49603764, 52553357, 55678342, 58989149,
    62496826, 66213081, 70150316, 74321671, 78741067, 83423255,
    88383859, 93639437, 99207528, 105106715, 111356685, 117978298,
    124993653, 132426162, 140300631,
};

// One sine period plus the wrap sample. Linear interpolation removes table steps.
constexpr int16_t kSineTable[] = {
    0, 3212, 6393, 9512, 12539, 15446, 18204, 20787,
    23170, 25329, 27245, 28898, 30273, 31356, 32137, 32609,
    32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329,
    23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212,
    0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
    -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329,
    -23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212, 0,
};

int16_t sineFromPhase(uint32_t phase)
{
  const uint8_t index = phase >> 26;
  const uint16_t fraction = (phase >> 10) & 0xffff;
  const int32_t first = kSineTable[index];
  const int32_t difference = kSineTable[index + 1] - first;
  return first + (static_cast<int64_t>(difference) * fraction) / 65536;
}

int32_t clampSample(int32_t value)
{
  if (value > 32767)
  {
    return 32767;
  }
  if (value < -32767)
  {
    return -32767;
  }
  return value;
}
}

PdmAudioPlayer::PdmAudioPlayer(uint8_t outputPin)
    : outputPin_(outputPin),
      volumePercent_(70),
      idleBias_(22361),
      outputAmplitude_(10150),
      melody_(),
      taskHandle_(nullptr),
      tickAccumulator_(0),
      noiseLfsr_(0x6d2b79f5),
      noiseSamplesRemaining_(0),
      noiseEnvelope_(0),
      noiseDecay_(1),
      stopRequested_(false),
      running_(false),
      finished_(false),
      i2sInstalled_(false)
{
  for (VoiceState &voice : voices_)
  {
    voice = {0, 0, 0, 0, false};
  }
}

bool PdmAudioPlayer::begin(uint8_t volumePercent)
{
  if (running_)
  {
    return true;
  }

  volumePercent_ = volumePercent > 100 ? 100 : volumePercent;
  outputAmplitude_ = (kMaximumAmplitude * volumePercent_) / 100;
  idleBias_ = outputAmplitude_ == 0
                  ? 32767
                  : 32767 - outputAmplitude_ - kPositiveHeadroom;
  tickAccumulator_ = 0;
  noiseLfsr_ = 0x6d2b79f5;
  noiseSamplesRemaining_ = 0;
  noiseEnvelope_ = 0;
  noiseDecay_ = 1;
  stopRequested_ = false;
  finished_ = false;
  for (VoiceState &voice : voices_)
  {
    voice = {0, 0, 0, 0, false};
  }

  pinMode(outputPin_, OUTPUT);
  digitalWrite(outputPin_, HIGH);
  if (!installI2s())
  {
    digitalWrite(outputPin_, HIGH);
    return false;
  }

  melody_.beginSequencer();
  running_ = true;
  if (xTaskCreatePinnedToCore(taskEntry, "pdm_audio", 4096, this, 3,
                              &taskHandle_, 0) != pdPASS)
  {
    running_ = false;
    taskHandle_ = nullptr;
    uninstallI2s();
    digitalWrite(outputPin_, HIGH);
    return false;
  }
  return true;
}

void PdmAudioPlayer::end()
{
  stopRequested_ = true;
}

bool PdmAudioPlayer::isRunning() const
{
  return running_;
}

bool PdmAudioPlayer::isFinished() const
{
  return finished_;
}

bool PdmAudioPlayer::installI2s()
{
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_PDM);
  config.sample_rate = kSampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 6;
  config.dma_buf_len = kBlockSamples;
  config.use_apll = true;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;
  config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

  esp_err_t result = i2s_driver_install(kI2sPort, &config, 0, nullptr);
  if (result != ESP_OK)
  {
    Serial.printf("PDM: i2s_driver_install failed (%d)\n", result);
    return false;
  }
  i2sInstalled_ = true;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_PIN_NO_CHANGE;
  pins.ws_io_num = I2S_PIN_NO_CHANGE;
  pins.data_out_num = outputPin_;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  result = i2s_set_pin(kI2sPort, &pins);
  if (result != ESP_OK)
  {
    Serial.printf("PDM: output pin configuration failed (%d)\n", result);
    uninstallI2s();
    return false;
  }
  Serial.println("PDM: I2S initialized");
  return true;
}

void PdmAudioPlayer::uninstallI2s()
{
  if (i2sInstalled_)
  {
    i2s_stop(kI2sPort);
    i2s_driver_uninstall(kI2sPort);
    i2sInstalled_ = false;
  }
}

void PdmAudioPlayer::taskEntry(void *context)
{
  static_cast<PdmAudioPlayer *>(context)->audioTask();
}

void PdmAudioPlayer::audioTask()
{
  int16_t samples[kBlockSamples];

  // Tick 1 contains the first events; process it before the first sample.
  melody_.advanceOneTick();
  syncVoices();
  triggerNoise(melody_.takeNoiseTrigger());

  while (!stopRequested_)
  {
    bool songEnded = false;
    for (size_t i = 0; i < kBlockSamples; ++i)
    {
      tickAccumulator_ += kTickRateNumerator;
      if (tickAccumulator_ >= kTickRateDenominator)
      {
        tickAccumulator_ -= kTickRateDenominator;
        melody_.advanceOneTick();
        syncVoices();
        triggerNoise(melody_.takeNoiseTrigger());
      }

      samples[i] = renderSample();
      if (melody_.isFinished() && envelopesAreSilent())
      {
        songEnded = true;
        for (++i; i < kBlockSamples; ++i)
        {
          samples[i] = idleBias_;
        }
        break;
      }
    }

    // ESP-IDF 4.4's 16-bit mono TX path emits adjacent DMA samples in reverse
    // order. Pre-swap each pair, matching the workaround in Arduino's I2S
    // library, so the waveform reaches the PDM converter in time order.
    for (size_t i = 0; i < kBlockSamples; i += 2)
    {
      const int16_t first = samples[i];
      samples[i] = samples[i + 1];
      samples[i + 1] = first;
    }

    size_t bytesWritten = 0;
    const esp_err_t result = i2s_write(kI2sPort, samples, sizeof(samples),
                                       &bytesWritten, portMAX_DELAY);
    if (result != ESP_OK || bytesWritten != sizeof(samples))
    {
      Serial.printf("PDM: audio write failed (%d, %u/%u bytes)\n", result,
                    static_cast<unsigned>(bytesWritten),
                    static_cast<unsigned>(sizeof(samples)));
      break;
    }
    if (songEnded)
    {
      finished_ = true;
      break;
    }
  }

  // Give the final idle samples time to leave DMA before restoring inactive HIGH.
  vTaskDelay(pdMS_TO_TICKS(25));
  uninstallI2s();
  pinMode(outputPin_, OUTPUT);
  digitalWrite(outputPin_, HIGH);
  melody_.stop();
  running_ = false;
  taskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void PdmAudioPlayer::syncVoices()
{
  for (uint8_t i = 0; i < BadAppleMelody::kVoiceCount; ++i)
  {
    VoiceState &voice = voices_[i];
    const uint8_t noteCode = melody_.voiceNoteCode(i);
    if (noteCode == voice.noteCode)
    {
      continue;
    }

    if (noteCode == 0)
    {
      voice.noteCode = 0;
      voice.gate = false;
      continue;
    }

    voice.noteCode = noteCode;
    voice.phase = 0;
    voice.phaseIncrement = kPhaseIncrements[noteCode - 1];
    voice.envelope = 0;
    voice.gate = true;
  }
}

void PdmAudioPlayer::triggerNoise(uint8_t durationTicks)
{
  if (durationTicks == 0)
  {
    return;
  }

  // Keep percussion short: 12 ms plus about 3 ms per encoded duration tick.
  noiseSamplesRemaining_ = 288U + static_cast<uint32_t>(durationTicks) * 72U;
  noiseEnvelope_ = kEnvelopeMaximum;
  noiseDecay_ = kEnvelopeMaximum / noiseSamplesRemaining_;
  if (noiseDecay_ == 0)
  {
    noiseDecay_ = 1;
  }
}

int16_t PdmAudioPlayer::renderSample()
{
  int32_t mix = 0;
  for (VoiceState &voice : voices_)
  {
    if (voice.gate)
    {
      const uint32_t next = voice.envelope + kAttackStep;
      voice.envelope = next > kEnvelopeMaximum ? kEnvelopeMaximum : next;
    }
    else if (voice.envelope > kReleaseStep)
    {
      voice.envelope -= kReleaseStep;
    }
    else
    {
      voice.envelope = 0;
    }

    if (voice.envelope != 0)
    {
      const int16_t oscillator = sineFromPhase(voice.phase);
      mix += (static_cast<int32_t>(oscillator) * voice.envelope) / kEnvelopeMaximum;
      voice.phase += voice.phaseIncrement;
    }
  }

  // Fixed /4 normalization prevents chords from clipping and keeps loudness stable.
  mix /= BadAppleMelody::kVoiceCount;

  if (noiseSamplesRemaining_ != 0)
  {
    noiseLfsr_ ^= noiseLfsr_ << 13;
    noiseLfsr_ ^= noiseLfsr_ >> 17;
    noiseLfsr_ ^= noiseLfsr_ << 5;
    const int16_t randomValue = noiseLfsr_ >> 16;
    mix += (static_cast<int32_t>(randomValue) * noiseEnvelope_) /
           (kEnvelopeMaximum * 4L);

    --noiseSamplesRemaining_;
    if (noiseSamplesRemaining_ == 0 || noiseEnvelope_ <= noiseDecay_)
    {
      noiseEnvelope_ = 0;
    }
    else
    {
      noiseEnvelope_ -= noiseDecay_;
    }
  }

  mix = clampSample(mix);
  const int32_t audio =
      (static_cast<int64_t>(mix) * outputAmplitude_) / 32767L;
  return static_cast<int16_t>(idleBias_ + audio);
}

bool PdmAudioPlayer::envelopesAreSilent() const
{
  if (noiseSamplesRemaining_ != 0 || noiseEnvelope_ != 0)
  {
    return false;
  }
  for (const VoiceState &voice : voices_)
  {
    if (voice.envelope != 0)
    {
      return false;
    }
  }
  return true;
}
