#include "LedcMelodyPlayer.h"

#include <esp_timer.h>

namespace
{
constexpr uint32_t kBeatsPerMinute = 138;
constexpr uint32_t kTicksPerBeat = 4;
constexpr uint32_t kTicksPerMinute = kBeatsPerMinute * kTicksPerBeat;
constexpr uint64_t kMicrosecondsPerMinute = 60000000ULL;
}

LedcMelodyPlayer::LedcMelodyPlayer(BadAppleMelody &melody)
    : melody_(melody),
      taskHandle_(nullptr),
      volumePercent_(70),
      running_(false),
      stopRequested_(false)
{
}

bool LedcMelodyPlayer::begin(uint8_t volumePercent)
{
  if (running_)
  {
    return true;
  }

  volumePercent_ = volumePercent > 100 ? 100 : volumePercent;
  stopRequested_ = false;
  melody_.begin(volumePercent_);
  running_ = true;
  if (xTaskCreatePinnedToCore(taskEntry, "ledc_melody", 3072, this, 2,
                              &taskHandle_, 0) != pdPASS)
  {
    melody_.stop();
    running_ = false;
    taskHandle_ = nullptr;
    return false;
  }
  return true;
}

void LedcMelodyPlayer::end()
{
  stopRequested_ = true;
}

bool LedcMelodyPlayer::isRunning() const
{
  return running_;
}

void LedcMelodyPlayer::taskEntry(void *context)
{
  static_cast<LedcMelodyPlayer *>(context)->playbackTask();
}

void LedcMelodyPlayer::playbackTask()
{
  const int64_t startTimeUs = esp_timer_get_time();
  uint32_t tickIndex = 1;
  melody_.advanceOneTick();

  while (!stopRequested_ && !melody_.isFinished())
  {
    ++tickIndex;
    const int64_t targetTimeUs = startTimeUs +
        static_cast<int64_t>((static_cast<uint64_t>(tickIndex - 1) *
                              kMicrosecondsPerMinute) /
                             kTicksPerMinute);

    while (!stopRequested_)
    {
      const int64_t remainingUs = targetTimeUs - esp_timer_get_time();
      if (remainingUs <= 0)
      {
        break;
      }
      if (remainingUs > 2000)
      {
        vTaskDelay(pdMS_TO_TICKS((remainingUs - 1000) / 1000));
      }
      else
      {
        taskYIELD();
      }
    }

    if (!stopRequested_)
    {
      melody_.advanceOneTick();
    }
  }

  if (stopRequested_)
  {
    melody_.stop();
  }
  running_ = false;
  taskHandle_ = nullptr;
  vTaskDelete(nullptr);
}
