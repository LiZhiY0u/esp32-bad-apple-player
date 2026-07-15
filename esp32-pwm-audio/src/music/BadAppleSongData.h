#pragma once

#include <stdint.h>

namespace BadAppleSongData
{
constexpr uint16_t kMinimumBackReferenceLength = 3;
constexpr uint16_t kMinimumBackReferenceOffset = 3;
constexpr uint8_t kMaximumBackReferenceDepth = 16;
constexpr uint16_t kEventCount = 1910;
constexpr uint8_t kNoiseNote = 33;
constexpr uint8_t kLowestMidiNote = 47;

extern const uint16_t kNoteTree[30];
extern const uint16_t kLengthTree[20];
extern const uint32_t kBitstream[189];
} // namespace BadAppleSongData
