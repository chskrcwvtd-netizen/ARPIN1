#pragma once

#include "engine/MidiFile.h"
#include "engine/Scale.h"
#include "engine/Session.h"

namespace phrasearp {

struct ImportResult {
    bool ok = false;
    std::string message;
    Pattern pattern;
    StepRate rate = StepRate::Sixteenth;
    ChordTrack chords;
};

ImportResult importMidiBytes(const std::vector<uint8_t>& bytes, ImportMode mode, const Scale& scale);
ImportResult importMidiFile(const std::string& path, ImportMode mode, const Scale& scale);

} // namespace phrasearp
