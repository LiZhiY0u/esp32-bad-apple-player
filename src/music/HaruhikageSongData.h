#pragma once

#include <stdint.h>

namespace HaruhikageSongData
{
struct NoteEvent
{
  uint16_t startDeltaMs;
  uint16_t durationMs;
  uint8_t midiNote;
};

constexpr uint16_t kNoteCount = 649;
constexpr uint32_t kDurationMs = 250488;

extern const NoteEvent kNotes[kNoteCount];
} // namespace HaruhikageSongData
