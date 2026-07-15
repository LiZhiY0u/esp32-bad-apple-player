#pragma once

#include <Arduino.h>

#include "music/BadAppleMelody.h"

class PdmAudioPlayer
{
public:
  static constexpr uint32_t kSampleRate = 24000;

  explicit PdmAudioPlayer(uint8_t outputPin);

  bool begin(uint8_t volumePercent = 70);
  void end();
  bool isRunning() const;
  bool isFinished() const;

private:
  struct VoiceState
  {
    uint32_t phase;
    uint32_t phaseIncrement;
    uint16_t envelope;
    uint8_t noteCode;
    bool gate;
  };

  static constexpr size_t kBlockSamples = 256;

  static void taskEntry(void *context);
  void audioTask();
  bool installI2s();
  void uninstallI2s();
  void syncVoices();
  void triggerNoise(uint8_t durationTicks);
  int16_t renderSample();
  bool envelopesAreSilent() const;

  uint8_t outputPin_;
  uint8_t volumePercent_;
  int16_t idleBias_;
  int32_t outputAmplitude_;
  BadAppleMelody melody_;
  VoiceState voices_[BadAppleMelody::kVoiceCount];
  TaskHandle_t taskHandle_;
  uint32_t tickAccumulator_;
  uint32_t noiseLfsr_;
  uint32_t noiseSamplesRemaining_;
  uint16_t noiseEnvelope_;
  uint16_t noiseDecay_;
  volatile bool stopRequested_;
  volatile bool running_;
  volatile bool finished_;
  bool i2sInstalled_;
};
