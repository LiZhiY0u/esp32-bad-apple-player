#pragma once

#include <Arduino.h>

#include "buzzer/PassiveBuzzer.h"
#include "BadAppleSongData.h"

class BadAppleMelody
{
public:
  explicit BadAppleMelody(PassiveBuzzer &buzzer);

  // Resets the decoder. Call updateFrame() once for every displayed frame.
  void begin(uint8_t volumePercent = 70);
  void updateFrame(uint32_t frameIndex);
  // Advances the melody from elapsed wall-clock time, without video.
  void updateTime(uint32_t elapsedMilliseconds);
  void stop();
  bool isFinished() const;

private:
  struct DecoderStackEntry
  {
    uint16_t bitOffset;
    uint16_t remainingEvents;
  };

  static constexpr uint8_t kVoiceCount = 4;
  static constexpr uint16_t kAudioStartFrame = 46;
  static constexpr uint16_t kVideoFrameRate = 30;
  static constexpr uint16_t kBeatsPerMinute = 138;
  static constexpr uint8_t kTicksPerBeat = 4;

  uint8_t pullBit(uint16_t &bitOffset) const;
  uint16_t pullExponentialGolomb(uint16_t &bitOffset) const;
  uint8_t pullHuffman(const uint16_t *tree, uint16_t &bitOffset) const;
  int32_t pullEvent();
  void advanceToTick(uint32_t targetTick);
  void performTick();
  void updateOutput();
  bool hasActiveNotes() const;

  PassiveBuzzer &buzzer_;
  DecoderStackEntry decoderStack_[BadAppleSongData::kMaximumBackReferenceDepth];
  uint8_t stackDepth_;
  uint8_t activeNotes_[kVoiceCount];
  uint16_t noteStopTicks_[kVoiceCount];
  uint16_t currentTick_;
  uint16_t nextEventTick_;
  uint8_t outputNote_;
  uint8_t volumePercent_;
  bool running_;
  bool eventStreamEnded_;
};
