#pragma once

#include <Arduino.h>

#include "buzzer/PassiveBuzzer.h"

class HaruhikageMelodyPlayer
{
public:
  explicit HaruhikageMelodyPlayer(PassiveBuzzer &buzzer);

  bool begin(uint8_t volumePercent = 40);
  void end();
  bool isRunning() const;

private:
  static void taskEntry(void *context);
  void playbackTask();
  bool waitUntil(int64_t targetTimeUs);

  PassiveBuzzer &buzzer_;
  TaskHandle_t taskHandle_;
  uint8_t volumePercent_;
  volatile bool running_;
  volatile bool stopRequested_;
};
