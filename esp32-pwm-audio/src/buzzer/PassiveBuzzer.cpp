#include "PassiveBuzzer.h"

namespace
{
constexpr uint32_t kInitialFrequencyHz = 1000;
constexpr uint32_t kMinimumFrequencyHz = 20;
constexpr uint32_t kMaximumFrequencyHz = 20000;
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

  initialized_ = true;
  return true;
}

bool PassiveBuzzer::playTone(uint32_t frequencyHz, uint8_t volumePercent)
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

  if (ledcSetup(pwmChannel_, frequencyHz, resolutionBits_) == 0)
  {
    stop();
    return false;
  }

  ledcWrite(pwmChannel_, dutyForVolume(volumePercent));
  frequencyHz_ = frequencyHz;
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

  ledcWrite(pwmChannel_, dutyForVolume(volumePercent));
  volumePercent_ = volumePercent;
  return true;
}

void PassiveBuzzer::stop()
{
  if (initialized_)
  {
    ledcWrite(pwmChannel_, inactiveDuty());
  }

  playing_ = false;
  frequencyHz_ = 0;
  volumePercent_ = 0;
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
