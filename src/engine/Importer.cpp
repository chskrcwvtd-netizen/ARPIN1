#include "engine/Importer.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace phrasearp {
namespace {

int degreeFromMidi(const Scale& scale, int midi) {
    const int root = scaleDegreeToMidi(scale, 0, 60);
    if (midi == root)
        return 0;
    const int dir = midi > root ? 1 : -1;
    int degree = 0;
    int cursor = root;
    for (int guard = 0; guard < 128 && cursor != midi; ++guard) {
        cursor += dir;
        if (scale.contains(cursor))
            degree += dir;
        if ((dir > 0 && cursor >= midi) || (dir < 0 && cursor <= midi))
            break;
    }
    return std::clamp(degree, -12, 12);
}

} // namespace

ImportResult importMidiBytes(const std::vector<uint8_t>& bytes, ImportMode mode, const Scale& scale) {
    ImportResult result;
    result.pattern = activePattern(makeInitState());
    ParsedMidi parsed = parseSmf(bytes);
    if (!parsed.ok) {
        result.message = parsed.error.empty() ? "Not a MIDI file" : parsed.error;
        return result;
    }
    if (parsed.notes.empty()) {
        result.message = "Empty MIDI file";
        return result;
    }
    const int ppq = std::max(1, parsed.ppq);
    const int64_t barTicks = (int64_t) parsed.numerator * ppq * 4 / std::max(1, parsed.denominator);
    const int last = parsed.notes.back().onTick;
    if (barTicks > 0 && last >= barTicks * 16) {
        result.message = "Clip is longer than 16 bars";
        return result;
    }

    struct Grid { int ticks; StepRate rate; bool triplet; };
    const Grid grids[] = {
        {std::max(1, ppq / 4), StepRate::Sixteenth, false},
        {std::max(1, ppq / 8), StepRate::ThirtySecond, false},
        {std::max(1, ppq / 6), StepRate::SixteenthTriplet, true},
        {std::max(1, ppq / 3), StepRate::EighthTriplet, true},
    };
    int bestGrid = 0;
    double bestErr = 1.0e9;
    for (int g = 0; g < 4; ++g) {
        double err = 0;
        for (const auto& n : parsed.notes) {
            const int q = (int) std::llround((double) n.onTick / grids[g].ticks) * grids[g].ticks;
            err += std::abs(q - n.onTick);
        }
        err /= (double) parsed.notes.size();
        if (err < bestErr - 0.5) {
            bestErr = err;
            bestGrid = g;
        }
    }
    const int grid = grids[bestGrid].ticks;
    result.rate = grids[bestGrid].rate;

    int maxTick = 0;
    for (const auto& n : parsed.notes)
        maxTick = std::max(maxTick, n.onTick);
    const int steps = std::clamp(maxTick / grid + 1, 1, kMaxSteps);
    preparePattern(result.pattern, steps);
    result.pattern.pitchMode = PitchMode::ScaleDegree;
    for (int i = 0; i < steps; ++i)
        result.pattern.type.steps[(size_t) i] = (uint8_t) StepType::Rest;

    std::vector<std::vector<ParsedNote>> buckets((size_t) steps);
    for (const auto& n : parsed.notes) {
        int q = (int) std::llround((double) n.onTick / grid);
        q = std::clamp(q, 0, steps - 1);
        buckets[(size_t) q].push_back(n);
    }

    result.chords.bars.assign(16, {});
    for (int i = 0; i < steps; ++i) {
        auto& bucket = buckets[(size_t) i];
        if (bucket.empty())
            continue;
        std::sort(bucket.begin(), bucket.end(), [](const ParsedNote& a, const ParsedNote& b) { return a.note < b.note; });
        const ParsedNote& top = *std::max_element(bucket.begin(), bucket.end(), [](const ParsedNote& a, const ParsedNote& b) {
            return a.note < b.note;
        });
        int same = 0;
        for (const auto& n : bucket)
            if (n.note == top.note)
                ++same;
        result.pattern.type.steps[(size_t) i] = (uint8_t) StepType::Note;
        result.pattern.velocity.steps[(size_t) i] = (uint8_t) std::clamp(top.velocity, 1, 127);
        const int durTicks = std::max(1, top.offTick - top.onTick);
        int gate = (int) std::lround(100.0 * (double) durTicks / (double) grid);
        result.pattern.gate.steps[(size_t) i] = (uint16_t) std::clamp(gate, 5, 400);
        result.pattern.ratchet.steps[(size_t) i] = (uint8_t) std::clamp(same, 1, 8);
        const int qTick = i * grid;
        int timing = (int) std::lround(100.0 * (double) (top.onTick - qTick) / (double) grid);
        result.pattern.timing.steps[(size_t) i] = (int8_t) std::clamp(timing, -50, 50);
        if (mode != ImportMode::Rhythm) {
            result.pattern.pitch.steps[(size_t) i] = (int16_t) degreeFromMidi(scale, top.note);
            const int rootMidi = scaleDegreeToMidi(scale, 0, 60);
            int oct = (int) std::lround((top.note - rootMidi) / 12.0);
            // Degree already carries the octave, so the octave lane stays at 0.
            (void) oct;
            result.pattern.octave.steps[(size_t) i] = 0;
        }
        if (bucket.size() > 1) {
            const int bar = barTicks > 0 ? (top.onTick / (int) barTicks) : 0;
            if (bar >= 0 && bar < 16) {
                result.chords.bars[(size_t) bar].set = true;
                for (const auto& n : bucket) {
                    auto& notes = result.chords.bars[(size_t) bar].notes;
                    if (std::find(notes.begin(), notes.end(), n.note) == notes.end())
                        notes.push_back(n.note);
                }
            }
        }
    }
    if (mode == ImportMode::Rhythm) {
        result.pattern.pitchMode = PitchMode::Index;
        for (auto& v : result.pattern.pitch.steps)
            v = 0;
    }
    result.ok = true;
    return result;
}

ImportResult importMidiFile(const std::string& path, ImportMode mode, const Scale& scale) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ImportResult result;
        result.message = "Not a MIDI file";
        return result;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return importMidiBytes(bytes, mode, scale);
}

} // namespace phrasearp
