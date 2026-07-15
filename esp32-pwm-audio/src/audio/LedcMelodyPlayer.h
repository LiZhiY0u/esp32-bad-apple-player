#pragma once

#include <Arduino.h>

#include "music/BadAppleMelody.h"

class LedcMelodyPlayer
{
public:
  explicit LedcMelodyPlayer(BadAppleMelody &melody);

  bool begin(uint8_t volumePercent = 70);
  void end();
  bool isRunning() const;

private:
  static void taskEntry(void *context);
  void playbackTask();

  BadAppleMelody &melody_;
  TaskHandle_t taskHandle_;
  uint8_t volumePercent_;
  volatile bool running_;
  volatile bool stopRequested_;
};
