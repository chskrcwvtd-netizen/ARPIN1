#pragma once

#include "engine/Types.h"

namespace phrasearp {

struct Scale {
    int root = 0;
    ScaleType type = ScaleType::Major;
    uint16_t customMask = 0;

    uint16_t mask() const;
    bool contains(int pitchClass) const;
    int degreeIndex(int pitchClass) const; // 0-based scale degree, or -1
    int size() const;
};

uint16_t intervalsToMask(std::initializer_list<int> intervals);
uint16_t defaultMask(ScaleType type);
Scale makeScale(int root, ScaleType type, uint16_t customMask);

int scaleDegreeToMidi(const Scale& scale, int degree, int center = 60);
int transposeScale(const Scale& scale, int midi, int degrees);

struct KeyEstimate {
    int root = 0;
    ScaleType mode = ScaleType::Major; // Major or NaturalMinor
    float score = -1.f;
    bool valid = false;
};

KeyEstimate estimateKey(const float hist[12], const KeyEstimate* previous = nullptr);
void addChordToHistogram(float hist[12], int root, int bass, uint16_t toneMask);

} // namespace phrasearp
