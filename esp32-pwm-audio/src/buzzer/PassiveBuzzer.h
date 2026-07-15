#pragma once

#include <Arduino.h>

class PassiveBuzzer
{
public:
  PassiveBuzzer(uint8_t pin,
                uint8_t pwmChannel = 0,
                bool activeLow = true,
                uint8_t resolutionBits = 10);

  // Initializes LEDC and leaves the buzzer in its inactive state.
  bool begin();

  // Starts a continuous square-wave tone. Call stop() to silence it.
  bool playTone(uint32_t frequencyHz, uint8_t volumePercent = 100);

  // Changes the low-pulse width without changing the current frequency.
  bool setVolume(uint8_t volumePercent);

  // Stops PWM output and holds the signal at the inactive level.
  void stop();

  bool isPlaying() const;
  uint32_t frequency() const;
  uint8_t volume() const;

private:
  uint32_t inactiveDuty() const;
  uint32_t dutyForVolume(uint8_t volumePercent) const;

  const uint8_t pin_;
  const uint8_t pwmChannel_;
  const bool activeLow_;
  const uint8_t resolutionBits_;

  bool initialized_;
  bool playing_;
  uint32_t frequencyHz_;
  uint8_t volumePercent_;
};
