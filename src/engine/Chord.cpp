#include "engine/Chord.h"

#include <algorithm>

namespace phrasearp {
namespace {

struct Template {
    ChordQuality quality;
    uint16_t rel = 0;
    int size = 0;
};

const Template& templates() {
    static const Template kTpls[] = {
        {ChordQuality::Major, intervalsToMask({0, 4, 7}), 3},
        {ChordQuality::Minor, intervalsToMask({0, 3, 7}), 3},
        {ChordQuality::Dim, intervalsToMask({0, 3, 6}), 3},
        {ChordQuality::Aug, intervalsToMask({0, 4, 8}), 3},
        {ChordQuality::Sus2, intervalsToMask({0, 2, 7}), 3},
        {ChordQuality::Sus4, intervalsToMask({0, 5, 7}), 3},
        {ChordQuality::Six, intervalsToMask({0, 4, 7, 9}), 4},
        {ChordQuality::Minor6, intervalsToMask({0, 3, 7, 9}), 4},
        {ChordQuality::Dom7, intervalsToMask({0, 4, 7, 10}), 4},
        {ChordQuality::Maj7, intervalsToMask({0, 4, 7, 11}), 4},
        {ChordQuality::Min7, intervalsToMask({0, 3, 7, 10}), 4},
        {ChordQuality::Min7b5, intervalsToMask({0, 3, 6, 10}), 4},
        {ChordQuality::Dim7, intervalsToMask({0, 3, 6, 9}), 4},
        {ChordQuality::Add9, intervalsToMask({0, 2, 4, 7}), 4},
        {ChordQuality::Dom9, intervalsToMask({0, 2, 4, 7, 10}), 5},
        {ChordQuality::Min9, intervalsToMask({0, 2, 3, 7, 10}), 5},
        {ChordQuality::Maj9, intervalsToMask({0, 2, 4, 7, 11}), 5},
    };
    return kTpls[0];
}

constexpr int kTemplateCount = 17;

int popcount(uint16_t m) {
    int n = 0;
    for (int i = 0; i < 12; ++i)
        if (m & (1u << i))
            ++n;
    return n;
}

uint16_t rotateMask(uint16_t rel, int root) {
    uint16_t out = 0;
    for (int i = 0; i < 12; ++i)
        if (rel & (1u << i))
            out = (uint16_t) (out | (uint16_t) (1u << pcOf(root + i)));
    return out;
}

std::string joinPitchNames(uint16_t mask, int bass) {
    std::string name;
    // Start at the bass so the lowest heard tone leads the label.
    for (int i = 0; i < 12; ++i) {
        const int pc = pcOf(bass + i);
        if (mask & (1u << pc)) {
            if (!name.empty())
                name += ' ';
            name += pitchClassName(pc);
        }
    }
    if (name.empty())
        name = "—";
    return name;
}

int walkScaleFrom(const Scale& scale, int rootPc, int steps) {
    int midi = 48 + pcOf(rootPc);
    while (pcOf(midi) != pcOf(rootPc))
        ++midi;
    int left = std::max(0, steps);
    for (int guard = 0; left > 0 && guard < 48; ++guard) {
        ++midi;
        if (scale.contains(midi))
            --left;
    }
    return pcOf(midi);
}

} // namespace

const char* qualitySuffix(ChordQuality quality) {
    switch (quality) {
        case ChordQuality::Major: return "";
        case ChordQuality::Minor: return "m";
        case ChordQuality::Dim: return "dim";
        case ChordQuality::Aug: return "aug";
        case ChordQuality::Sus2: return "sus2";
        case ChordQuality::Sus4: return "sus4";
        case ChordQuality::Six: return "6";
        case ChordQuality::Minor6: return "m6";
        case ChordQuality::Dom7: return "7";
        case ChordQuality::Maj7: return "maj7";
        case ChordQuality::Min7: return "m7";
        case ChordQuality::Min7b5: return "m7b5";
        case ChordQuality::Dim7: return "dim7";
        case ChordQuality::Add9: return "add9";
        case ChordQuality::Dom9: return "9";
        case ChordQuality::Min9: return "m9";
        case ChordQuality::Maj9: return "maj9";
        case ChordQuality::Single: return "";
        case ChordQuality::Unknown: return "";
    }
    return "";
}

bool isChordTone(int midi, const Chord& chord) {
    if (midi < 0 || midi > 127)
        return false;
    return (chord.tones & (1u << pcOf(midi))) != 0;
}

ToneRole roleOf(int midi, const Chord& chord, const Scale& scale) {
    const int pc = pcOf(midi);
    if (chord.tones & (1u << pc))
        return ToneRole::Chord;
    if (!scale.contains(pc) || !chord.ok)
        return scale.contains(pc) ? ToneRole::Passing : ToneRole::Chromatic;
    const int rel = pcOf(pc - chord.root);
    // 9 / 11 / 13, including the flat and sharp neighbours that belong to the key scale.
    if (rel == 1 || rel == 2 || rel == 3 || rel == 5 || rel == 6 || rel == 8 || rel == 9)
        return ToneRole::Tension;
    return ToneRole::Passing;
}

bool roleAllowed(ToneRole role, float color, bool strongBeat) {
    if (strongBeat)
        return role == ToneRole::Chord;
    if (role == ToneRole::Chord)
        return true;
    if (role == ToneRole::Tension)
        return color >= 1.f;
    if (role == ToneRole::Passing)
        return color >= 51.f;
    return color >= 81.f;
}

Chord recognizeChord(const std::vector<int>& midiNotes, const Scale* scale) {
    Chord chord;
    if (midiNotes.empty()) {
        chord.name = "—";
        return chord;
    }

    int lowest = 127;
    for (int n : midiNotes) {
        if (n < 0 || n > 127)
            continue;
        chord.input = (uint16_t) (chord.input | (uint16_t) (1u << pcOf(n)));
        if (n < lowest)
            lowest = n;
    }
    if (chord.input == 0 || lowest > 127) {
        chord.name = "—";
        return chord;
    }
    chord.bass = pcOf(lowest);
    const int inputCount = popcount(chord.input);

    if (inputCount == 1) {
        chord.root = chord.bass;
        chord.quality = ChordQuality::Single;
        chord.ok = true;
        chord.tones = (uint16_t) (1u << chord.root);
        if (scale != nullptr) {
            // Triad built by walking the key scale up from this root.
            const int third = walkScaleFrom(*scale, chord.root, 2);
            const int fifth = walkScaleFrom(*scale, chord.root, 4);
            chord.tones = (uint16_t) (chord.tones | (uint16_t) (1u << third) | (uint16_t) (1u << fifth));
        } else {
            chord.tones = (uint16_t) (chord.tones | (uint16_t) (1u << pcOf(chord.root + 4))
                                       | (uint16_t) (1u << pcOf(chord.root + 7)));
        }
        chord.name = pitchClassName(chord.root);
        return chord;
    }

    const Template* tpls = &templates();
    int bestScore = -100000;
    int bestRoot = chord.bass;
    ChordQuality bestQ = ChordQuality::Unknown;
    uint16_t bestTones = 0;
    int bestBelow = 99;
    bool found = false;

    for (int root = 0; root < 12; ++root) {
        for (int ti = 0; ti < kTemplateCount; ++ti) {
            const uint16_t absMask = rotateMask(tpls[ti].rel, root);
            const uint16_t missingMask = (uint16_t) (absMask & (uint16_t) ~chord.input);
            const uint16_t extraMask = (uint16_t) (chord.input & (uint16_t) ~absMask);
            const int missing = popcount(missingMask);
            const int extra = popcount(extraMask);
            if (missing > 1)
                continue;
            if (missing > 0 && inputCount >= 3)
                continue;
            if (missing == 1 && inputCount >= tpls[ti].size)
                continue;
            if (extra > tpls[ti].size)
                continue;
            if (missing == 1 && scale != nullptr) {
                int missingPc = 0;
                for (int p = 0; p < 12; ++p)
                    if (missingMask & (1u << p))
                        missingPc = p;
                if (!scale->contains(missingPc))
                    continue;
            }

            int score = 50 * (tpls[ti].size - missing) - 25 * extra;
            if (chord.bass == root)
                score += 30;
            else if (absMask & (1u << chord.bass))
                score += 10;
            else
                score -= 8;
            if (extra == 1 && (extraMask & (1u << chord.bass)) && missing == 0)
                score += 18; // foreign bass: slash chord, not a failed match

            const int below = pcOf(chord.bass - root); // 0 if the bass is the root
            const bool better = !found || score > bestScore || (score == bestScore && below < bestBelow)
                                || (score == bestScore && below == bestBelow && (int) tpls[ti].quality < (int) bestQ);
            if (better) {
                found = true;
                bestScore = score;
                bestRoot = root;
                bestQ = tpls[ti].quality;
                bestTones = absMask;
                bestBelow = below;
            }
        }
    }

    if (!found) {
        chord.ok = false;
        chord.quality = ChordQuality::Unknown;
        chord.root = chord.bass;
        chord.tones = chord.input; // the held notes are the chord
        chord.name = joinPitchNames(chord.input, chord.bass);
        return chord;
    }

    chord.ok = true;
    chord.root = bestRoot;
    chord.quality = bestQ;
    chord.tones = bestTones;
    chord.name = std::string(pitchClassName(bestRoot)) + qualitySuffix(bestQ);
    if (chord.bass != chord.root)
        chord.name += std::string("/") + pitchClassName(chord.bass);
    return chord;
}

} // namespace phrasearp
