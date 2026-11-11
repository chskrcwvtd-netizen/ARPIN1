#pragma once

#include "engine/Chord.h"

#include <array>
#include <vector>

namespace phrasearp {

struct Voicing {
    std::array<int, 8> notes {};
    int count = 0;
    int rootPitch = 60;

    bool operator==(const Voicing& o) const {
        if (count != o.count || rootPitch != o.rootPitch)
            return false;
        for (int i = 0; i < count; ++i)
            if (notes[(size_t) i] != o.notes[(size_t) i])
                return false;
        return true;
    }
};

int nearestPitch(int pitchClass, int target, int low, int high);
Voicing voiceLead(const Voicing* previous, const Chord& chord, int low, int high, int maxJump);

int fitRange(int midi, int low, int high, BoundaryMode boundary);

// Ideal pitch for an index into the color-filtered, voice-led tone set.
int indexIdeal(const Chord& chord, const Scale& scale, float color, int index, int anchorRoot, int low, int high, int octaves);

// Realise one step. prevMidi < 0 means there is no previous melody note.
int realizePitch(PitchMode mode,
                 int stored,
                 const Chord& chord,
                 const Scale& scale,
                 float color,
                 int prevMidi,
                 int prevStored,
                 bool strong,
                 int maxJump,
                 int low,
                 int high,
                 int octaves,
                 BoundaryMode boundary,
                 int octaveShift,
                 int transpose,
                 int contour,
                 uint32_t randomSalt);

std::vector<int> chordTonePitches(const Voicing& voicing);

} // namespace phrasearp
