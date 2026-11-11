#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phrasearp {

struct SmfNote {
    int tick = 0;
    int duration = 0;
    int note = 60;
    int velocity = 100;
    int channel = 0;
};

std::vector<uint8_t> writeSmf(const std::vector<SmfNote>& notes, double bpm, int numerator, int denominator,
                              const std::string& trackName, const std::vector<SmfNote>* chordNotes = nullptr);

bool writeSmfFile(const std::string& path, const std::vector<uint8_t>& bytes);

struct ParsedNote {
    int onTick = 0;
    int offTick = 0;
    int note = 60;
    int velocity = 100;
    int channel = 0;
};

struct ParsedMidi {
    bool ok = false;
    std::string error;
    int ppq = 960;
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
    std::vector<ParsedNote> notes;
};

ParsedMidi parseSmf(const std::vector<uint8_t>& bytes);

} // namespace phrasearp
