#include "BadAppleMelody.h"

namespace
{
// MIDI notes 47 (B2) through 79 (G5), rounded to the nearest hertz.
constexpr uint16_t kNoteFrequencies[] = {
    123, 131, 139, 147, 156, 165, 175, 185, 196, 208, 220,
    233, 247, 262, 277, 294, 311, 330, 349, 370, 392, 415,
    440, 466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};
}

BadAppleMelody::BadAppleMelody(PassiveBuzzer &buzzer)
    : buzzer_(buzzer),
      stackDepth_(0),
      currentTick_(0),
      nextEventTick_(0),
      outputNote_(0),
      volumePercent_(70),
      running_(false),
      eventStreamEnded_(false)
{
  for (uint8_t i = 0; i < BadAppleSongData::kMaximumBackReferenceDepth; ++i)
  {
    decoderStack_[i] = {0, 0};
  }
  for (uint8_t i = 0; i < kVoiceCount; ++i)
  {
    activeNotes_[i] = 0;
    noteStopTicks_[i] = 0;
  }
}

void BadAppleMelody::begin(uint8_t volumePercent)
{
  buzzer_.stop();
  stackDepth_ = 0;
  currentTick_ = 0;
  nextEventTick_ = 0;
  outputNote_ = 0;
  volumePercent_ = volumePercent > 100 ? 100 : volumePercent;
  eventStreamEnded_ = false;
  running_ = true;

  for (uint8_t i = 0; i < BadAppleSongData::kMaximumBackReferenceDepth; ++i)
  {
    decoderStack_[i] = {0, 0};
  }
  decoderStack_[0].remainingEvents = BadAppleSongData::kEventCount + 1;

  for (uint8_t i = 0; i < kVoiceCount; ++i)
  {
    activeNotes_[i] = 0;
    noteStopTicks_[i] = 0;
  }
}

void BadAppleMelody::updateFrame(uint32_t frameIndex)
{
  if (!running_ || frameIndex < kAudioStartFrame)
  {
    return;
  }

  const uint32_t framesSinceAudioStart = frameIndex - kAudioStartFrame;
  const uint32_t targetTick = 1U +
      (framesSinceAudioStart * kBeatsPerMinute * kTicksPerBeat) /
          (kVideoFrameRate * 60U);

  advanceToTick(targetTick);
}

void BadAppleMelody::updateTime(uint32_t elapsedMilliseconds)
{
  if (!running_)
  {
    return;
  }

  const uint32_t targetTick = 1U +
      (elapsedMilliseconds * kBeatsPerMinute * kTicksPerBeat) / 60000U;
  advanceToTick(targetTick);
}

void BadAppleMelody::advanceToTick(uint32_t targetTick)
{
  while (currentTick_ < targetTick && running_)
  {
    ++currentTick_;
    performTick();
  }
}

void BadAppleMelody::stop()
{
  buzzer_.stop();
  outputNote_ = 0;
  running_ = false;
}

bool BadAppleMelody::isFinished() const
{
  return !running_ && eventStreamEnded_;
}

uint8_t BadAppleMelody::pullBit(uint16_t &bitOffset) const
{
  const uint16_t wordIndex = bitOffset >> 5;
  const uint8_t bitIndex = bitOffset & 0x1f;
  const uint8_t value = (BadAppleSongData::kBitstream[wordIndex] >> bitIndex) & 1U;
  ++bitOffset;
  return value;
}

uint16_t BadAppleMelody::pullExponentialGolomb(uint16_t &bitOffset) const
{
  uint8_t exponent = 0;
  while (pullBit(bitOffset) == 0)
  {
    ++exponent;
  }

  uint16_t value = 1;
  for (uint8_t i = 0; i < exponent; ++i)
  {
    value = (value << 1) | pullBit(bitOffset);
  }
  return value - 1;
}

uint8_t BadAppleMelody::pullHuffman(const uint16_t *tree, uint16_t &bitOffset) const
{
  uint8_t treeOffset = 0;
  while (true)
  {
    uint16_t entry = tree[treeOffset];
    entry >>= pullBit(bitOffset) * 8;
    if (entry & 0x80)
    {
      return entry & 0x7f;
    }
    treeOffset += (entry & 0xff) + 1;
  }
}

int32_t BadAppleMelody::pullEvent()
{
  DecoderStackEntry *entry = &decoderStack_[stackDepth_];
  --entry->remainingEvents;

  while (entry->remainingEvents == 0)
  {
    if (stackDepth_ == 0)
    {
      return -1;
    }
    --stackDepth_;
    entry = &decoderStack_[stackDepth_];
  }

  uint16_t *bitOffset = nullptr;
  while (true)
  {
    bitOffset = &entry->bitOffset;
    const uint16_t referenceStart = *bitOffset;
    if (pullBit(*bitOffset) == 0)
    {
      break;
    }

    const uint16_t runLength = pullExponentialGolomb(*bitOffset);
    const uint16_t backOffset = pullExponentialGolomb(*bitOffset);
    uint16_t eventsToReplay = runLength +
        BadAppleSongData::kMinimumBackReferenceLength + 1;
    if (eventsToReplay > entry->remainingEvents)
    {
      eventsToReplay = entry->remainingEvents;
    }
    entry->remainingEvents -= eventsToReplay;

    if (stackDepth_ + 1 >= BadAppleSongData::kMaximumBackReferenceDepth)
    {
      return -1;
    }

    ++stackDepth_;
    entry = &decoderStack_[stackDepth_];
    entry->bitOffset = referenceStart -
        (backOffset + BadAppleSongData::kMinimumBackReferenceOffset +
         runLength + BadAppleSongData::kMinimumBackReferenceLength + 1);
    entry->remainingEvents = eventsToReplay;
  }

  const uint8_t note = pullHuffman(BadAppleSongData::kNoteTree, *bitOffset);
  const uint8_t lengthAndRun = pullHuffman(BadAppleSongData::kLengthTree, *bitOffset);
  return (static_cast<int32_t>(note) << 8) | lengthAndRun;
}

void BadAppleMelody::performTick()
{
  for (uint8_t i = 0; i < kVoiceCount; ++i)
  {
    if (activeNotes_[i] != 0 && noteStopTicks_[i] <= currentTick_)
    {
      activeNotes_[i] = 0;
    }
  }

  while (!eventStreamEnded_ && currentTick_ >= nextEventTick_)
  {
    const int32_t event = pullEvent();
    if (event < 0)
    {
      eventStreamEnded_ = true;
      break;
    }

    uint8_t ticksUntilNextEvent = event & 0x07;
    if (ticksUntilNextEvent == 7)
    {
      ticksUntilNextEvent = 8;
    }
    const uint8_t durationTicks = (event >> 3) & 0x1f;
    const uint8_t note = event >> 8;
    nextEventTick_ = currentTick_ + ticksUntilNextEvent;

    if (note == BadAppleSongData::kNoiseNote)
    {
      continue; // A single passive buzzer cannot reproduce the percussion track.
    }

    for (uint8_t i = 0; i < kVoiceCount; ++i)
    {
      if (activeNotes_[i] == 0)
      {
        activeNotes_[i] = note + 1;
        noteStopTicks_[i] = currentTick_ + durationTicks + 1;
        break;
      }
    }
  }

  updateOutput();
  if (eventStreamEnded_ && !hasActiveNotes())
  {
    stop();
  }
}

void BadAppleMelody::updateOutput()
{
  uint8_t highestNote = 0;
  for (uint8_t i = 0; i < kVoiceCount; ++i)
  {
    if (activeNotes_[i] > highestNote)
    {
      highestNote = activeNotes_[i];
    }
  }

  if (highestNote == outputNote_)
  {
    return;
  }
  outputNote_ = highestNote;

  if (highestNote == 0)
  {
    buzzer_.stop();
    return;
  }

  const uint8_t noteIndex = highestNote - 1;
  buzzer_.playTone(kNoteFrequencies[noteIndex], volumePercent_);
}

bool BadAppleMelody::hasActiveNotes() const
{
  for (uint8_t i = 0; i < kVoiceCount; ++i)
  {
    if (activeNotes_[i] != 0)
    {
      return true;
    }
  }
  return false;
}
