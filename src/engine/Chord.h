#pragma once

#include "engine/Scale.h"

#include <string>
#include <vector>

namespace phrasearp {

struct Chord {
    int root = 0;
    int bass = 0;
    ChordQuality quality = ChordQuality::Unknown;
    uint16_t tones = 0; // absolute pitch-class bitmask of chord members
    uint16_t input = 0;
    bool ok = false;
    std::string name;
};

Chord recognizeChord(const std::vector<int>& midiNotes, const Scale* scale);
bool isChordTone(int midi, const Chord& chord);
ToneRole roleOf(int midi, const Chord& chord, const Scale& scale);
bool roleAllowed(ToneRole role, float color, bool strongBeat);
const char* qualitySuffix(ChordQuality quality);

} // namespace phrasearp
