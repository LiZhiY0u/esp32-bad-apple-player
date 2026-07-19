#include "HaruhikageMelodyPlayer.h"

#include <esp_timer.h>

#include "music/HaruhikageSongData.h"

HaruhikageMelodyPlayer::HaruhikageMelodyPlayer(PassiveBuzzer &buzzer)
    : buzzer_(buzzer),
      taskHandle_(nullptr),
      volumePercent_(40),
      running_(false),
      stopRequested_(false)
{
}

bool HaruhikageMelodyPlayer::begin(uint8_t volumePercent)
{
  if (running_)
  {
    return true;
  }

  volumePercent_ = volumePercent > 100 ? 100 : volumePercent;
  stopRequested_ = false;
  running_ = true;
  if (xTaskCreatePinnedToCore(taskEntry, "haruhikage", 3072, this, 2,
                              &taskHandle_, 0) != pdPASS)
  {
    running_ = false;
    taskHandle_ = nullptr;
    return false;
  }
  return true;
}

void HaruhikageMelodyPlayer::end()
{
  stopRequested_ = true;
}

bool HaruhikageMelodyPlayer::isRunning() const
{
  return running_;
}

void HaruhikageMelodyPlayer::taskEntry(void *context)
{
  static_cast<HaruhikageMelodyPlayer *>(context)->playbackTask();
}

void HaruhikageMelodyPlayer::playbackTask()
{
  const int64_t songStartUs = esp_timer_get_time();
  uint32_t noteStartMs = 0;

  for (uint16_t index = 0;
       index < HaruhikageSongData::kNoteCount && !stopRequested_;
       ++index)
  {
    const HaruhikageSongData::NoteEvent &note =
        HaruhikageSongData::kNotes[index];
    noteStartMs += note.startDeltaMs;

    if (!waitUntil(songStartUs + static_cast<int64_t>(noteStartMs) * 1000LL))
    {
      break;
    }

    buzzer_.playMidiNote(note.midiNote, volumePercent_);
    if (!waitUntil(songStartUs +
                   static_cast<int64_t>(noteStartMs + note.durationMs) * 1000LL))
    {
      break;
    }
    buzzer_.stop();
  }

  buzzer_.stop();
  running_ = false;
  taskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

bool HaruhikageMelodyPlayer::waitUntil(int64_t targetTimeUs)
{
  while (!stopRequested_)
  {
    const int64_t remainingUs = targetTimeUs - esp_timer_get_time();
    if (remainingUs <= 0)
    {
      return true;
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
  return false;
}
