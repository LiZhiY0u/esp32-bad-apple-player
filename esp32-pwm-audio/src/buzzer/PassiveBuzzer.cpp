#include "PassiveBuzzer.h"

#include <driver/ledc.h>
#include <math.h>

namespace
{
constexpr uint32_t kInitialFrequencyHz = 1000;
constexpr uint32_t kMinimumFrequencyHz = 20;
constexpr uint32_t kMaximumFrequencyHz = 20000;
constexpr uint32_t kAttackTimeMs = 4;
constexpr uint32_t kReleaseTimeMs = 4;
constexpr double kMidiA4FrequencyHz = 440.0;
constexpr uint8_t kMidiA4Note = 69;
constexpr uint32_t kLedcDividerFraction = 256;
constexpr uint32_t kMinimumLedcDivider = 256;
constexpr uint32_t kMaximumLedcDivider = 0x3ffff;
}

PassiveBuzzer::PassiveBuzzer(uint8_t pin,
                             uint8_t pwmChannel,
                             bool activeLow,
                             uint8_t resolutionBits)
    : pin_(pin),
      pwmChannel_(pwmChannel),
      activeLow_(activeLow),
      resolutionBits_(resolutionBits),
      initialized_(false),
      fadeEnabled_(false),
      playing_(false),
      frequencyHz_(0),
      volumePercent_(0)
{
}

bool PassiveBuzzer::begin()
{
  if (resolutionBits_ == 0 || resolutionBits_ > 16)
  {
    return false;
  }

  // Set the safe level before connecting the LEDC peripheral so an
  // active-low module does not chirp during startup.
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, activeLow_ ? HIGH : LOW);

  if (ledcSetup(pwmChannel_, kInitialFrequencyHz, resolutionBits_) == 0)
  {
    return false;
  }

  ledcWrite(pwmChannel_, inactiveDuty());
  ledcAttachPin(pin_, pwmChannel_);
  ledcWrite(pwmChannel_, inactiveDuty());

  const esp_err_t fadeResult = ledc_fade_func_install(0);
  fadeEnabled_ = fadeResult == ESP_OK || fadeResult == ESP_ERR_INVALID_STATE;
  initialized_ = true;
  return true;
}

bool PassiveBuzzer::playTone(uint32_t frequencyHz, uint8_t volumePercent)
{
  return playFrequency(static_cast<double>(frequencyHz), volumePercent);
}

bool PassiveBuzzer::playMidiNote(uint8_t midiNote, uint8_t volumePercent)
{
  if (midiNote > 127)
  {
    return false;
  }

  const double semitonesFromA4 =
      static_cast<int16_t>(midiNote) - static_cast<int16_t>(kMidiA4Note);
  const double frequencyHz =
      kMidiA4FrequencyHz * pow(2.0, semitonesFromA4 / 12.0);
  return playFrequency(frequencyHz, volumePercent);
}

bool PassiveBuzzer::playFrequency(double frequencyHz, uint8_t volumePercent)
{
  if (!initialized_ || frequencyHz < kMinimumFrequencyHz ||
      frequencyHz > kMaximumFrequencyHz)
  {
    return false;
  }

  if (volumePercent == 0)
  {
    stop();
    return true;
  }

  if (volumePercent > 100)
  {
    volumePercent = 100;
  }

  if (playing_)
  {
    fadeToDuty(inactiveDuty(), kReleaseTimeMs, true);
  }

  if (!configureExactFrequency(frequencyHz))
  {
    stop();
    return false;
  }

  ledcWrite(pwmChannel_, inactiveDuty());
  fadeToDuty(dutyForVolume(volumePercent), kAttackTimeMs, false);
  frequencyHz_ = lround(frequencyHz);
  volumePercent_ = volumePercent;
  playing_ = true;
  return true;
}

bool PassiveBuzzer::setVolume(uint8_t volumePercent)
{
  if (!initialized_)
  {
    return false;
  }

  if (volumePercent == 0)
  {
    stop();
    return true;
  }

  if (!playing_)
  {
    return false;
  }

  if (volumePercent > 100)
  {
    volumePercent = 100;
  }

  fadeToDuty(dutyForVolume(volumePercent), kAttackTimeMs, false);
  volumePercent_ = volumePercent;
  return true;
}

void PassiveBuzzer::stop()
{
  if (initialized_)
  {
    if (playing_)
    {
      fadeToDuty(inactiveDuty(), kReleaseTimeMs, true);
    }
    ledcWrite(pwmChannel_, inactiveDuty());
  }

  playing_ = false;
  frequencyHz_ = 0;
  volumePercent_ = 0;
}

bool PassiveBuzzer::configureExactFrequency(double frequencyHz)
{
  const uint32_t dutySteps = 1UL << resolutionBits_;
  const double dividerValue =
      (static_cast<double>(LEDC_APB_CLK_HZ) * kLedcDividerFraction) /
      (frequencyHz * dutySteps);
  const uint32_t divider = lround(dividerValue);
  if (divider < kMinimumLedcDivider || divider > kMaximumLedcDivider)
  {
    return false;
  }

  const ledc_mode_t speedMode = static_cast<ledc_mode_t>(pwmChannel_ / 8);
  const ledc_timer_t timer =
      static_cast<ledc_timer_t>((pwmChannel_ / 2) % LEDC_TIMER_MAX);
  if (ledc_timer_set(speedMode, timer, divider, resolutionBits_, LEDC_APB_CLK) != ESP_OK)
  {
    return false;
  }
  return ledc_timer_rst(speedMode, timer) == ESP_OK;
}

bool PassiveBuzzer::fadeToDuty(uint32_t targetDuty,
                               uint32_t durationMs,
                               bool waitUntilDone)
{
  if (fadeEnabled_)
  {
    const ledc_mode_t speedMode = static_cast<ledc_mode_t>(pwmChannel_ / 8);
    const ledc_channel_t channel =
        static_cast<ledc_channel_t>(pwmChannel_ % 8);
    const ledc_fade_mode_t mode =
        waitUntilDone ? LEDC_FADE_WAIT_DONE : LEDC_FADE_NO_WAIT;
    if (ledc_set_fade_time_and_start(speedMode, channel, targetDuty,
                                     durationMs, mode) == ESP_OK)
    {
      return true;
    }
  }

  ledcWrite(pwmChannel_, targetDuty);
  return false;
}

bool PassiveBuzzer::isPlaying() const
{
  return playing_;
}

uint32_t PassiveBuzzer::frequency() const
{
  return frequencyHz_;
}

uint8_t PassiveBuzzer::volume() const
{
  return volumePercent_;
}

uint32_t PassiveBuzzer::inactiveDuty() const
{
  const uint32_t maximumDuty = (1UL << resolutionBits_) - 1UL;
  return activeLow_ ? maximumDuty : 0;
}

uint32_t PassiveBuzzer::dutyForVolume(uint8_t volumePercent) const
{
  const uint32_t maximumDuty = (1UL << resolutionBits_) - 1UL;
  const uint32_t halfDuty = 1UL << (resolutionBits_ - 1U);

  // A passive buzzer is loudest around a 50% square wave. For an
  // active-low module, volume therefore grows from 100% high (silent)
  // toward a 50% high / 50% low waveform.
  if (activeLow_)
  {
    const uint32_t activeRange = maximumDuty - halfDuty;
    return maximumDuty - (activeRange * volumePercent / 100U);
  }

  return halfDuty * volumePercent / 100U;
}
