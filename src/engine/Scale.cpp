#include "engine/Scale.h"

#include <algorithm>
#include <cmath>

namespace phrasearp {
uint16_t intervalsToMask(std::initializer_list<int> intervals) {
    uint16_t m = 0;
    for (int iv : intervals)
        m = (uint16_t) (m | (uint16_t) (1u << (iv % 12)));
    return m;
}

uint16_t defaultMask(ScaleType type) {
    switch (type) {
        case ScaleType::Major: return intervalsToMask({0, 2, 4, 5, 7, 9, 11});
        case ScaleType::NaturalMinor: return intervalsToMask({0, 2, 3, 5, 7, 8, 10});
        case ScaleType::HarmonicMinor: return intervalsToMask({0, 2, 3, 5, 7, 8, 11});
        case ScaleType::MelodicMinor: return intervalsToMask({0, 2, 3, 5, 7, 9, 11});
        case ScaleType::Dorian: return intervalsToMask({0, 2, 3, 5, 7, 9, 10});
        case ScaleType::Phrygian: return intervalsToMask({0, 1, 3, 5, 7, 8, 10});
        case ScaleType::Lydian: return intervalsToMask({0, 2, 4, 6, 7, 9, 11});
        case ScaleType::Mixolydian: return intervalsToMask({0, 2, 4, 5, 7, 9, 10});
        case ScaleType::MajorPentatonic: return intervalsToMask({0, 2, 4, 7, 9});
        case ScaleType::MinorPentatonic: return intervalsToMask({0, 3, 5, 7, 10});
        case ScaleType::Blues: return intervalsToMask({0, 3, 5, 6, 7, 10});
        case ScaleType::Custom: return intervalsToMask({0, 2, 4, 5, 7, 9, 11});
        case ScaleType::Count: break;
    }
    return intervalsToMask({0, 2, 4, 5, 7, 9, 11});
}

const char* scaleTypeName(ScaleType type) {
    switch (type) {
        case ScaleType::Major: return "Major";
        case ScaleType::NaturalMinor: return "Natural Minor";
        case ScaleType::HarmonicMinor: return "Harmonic Minor";
        case ScaleType::MelodicMinor: return "Melodic Minor";
        case ScaleType::Dorian: return "Dorian";
        case ScaleType::Phrygian: return "Phrygian";
        case ScaleType::Lydian: return "Lydian";
        case ScaleType::Mixolydian: return "Mixolydian";
        case ScaleType::MajorPentatonic: return "Major Pentatonic";
        case ScaleType::MinorPentatonic: return "Minor Pentatonic";
        case ScaleType::Blues: return "Blues";
        case ScaleType::Custom: return "Custom";
        case ScaleType::Count: break;
    }
    return "Major";
}

const char* stepRateName(StepRate rate) {
    switch (rate) {
        case StepRate::Quarter: return "1/4";
        case StepRate::QuarterTriplet: return "1/4 T";
        case StepRate::QuarterDotted: return "1/4 D";
        case StepRate::Eighth: return "1/8";
        case StepRate::EighthTriplet: return "1/8 T";
        case StepRate::EighthDotted: return "1/8 D";
        case StepRate::Sixteenth: return "1/16";
        case StepRate::SixteenthTriplet: return "1/16 T";
        case StepRate::SixteenthDotted: return "1/16 D";
        case StepRate::ThirtySecond: return "1/32";
        case StepRate::ThirtySecondTriplet: return "1/32 T";
        case StepRate::ThirtySecondDotted: return "1/32 D";
        case StepRate::SixtyFourth: return "1/64";
        case StepRate::SixtyFourthTriplet: return "1/64 T";
        case StepRate::SixtyFourthDotted: return "1/64 D";
        case StepRate::Count: break;
    }
    return "1/8";
}

int64_t masterStepTicks(StepRate rate) {
    int64_t straight = kPpq;
    bool triplet = false;
    bool dotted = false;
    switch (rate) {
        case StepRate::Quarter: straight = kPpq; break;
        case StepRate::QuarterTriplet: straight = kPpq; triplet = true; break;
        case StepRate::QuarterDotted: straight = kPpq; dotted = true; break;
        case StepRate::Eighth: straight = kPpq / 2; break;
        case StepRate::EighthTriplet: straight = kPpq / 2; triplet = true; break;
        case StepRate::EighthDotted: straight = kPpq / 2; dotted = true; break;
        case StepRate::Sixteenth: straight = kPpq / 4; break;
        case StepRate::SixteenthTriplet: straight = kPpq / 4; triplet = true; break;
        case StepRate::SixteenthDotted: straight = kPpq / 4; dotted = true; break;
        case StepRate::ThirtySecond: straight = kPpq / 8; break;
        case StepRate::ThirtySecondTriplet: straight = kPpq / 8; triplet = true; break;
        case StepRate::ThirtySecondDotted: straight = kPpq / 8; dotted = true; break;
        case StepRate::SixtyFourth: straight = kPpq / 16; break;
        case StepRate::SixtyFourthTriplet: straight = kPpq / 16; triplet = true; break;
        case StepRate::SixtyFourthDotted: straight = kPpq / 16; dotted = true; break;
        case StepRate::Count: break;
    }
    if (triplet)
        return straight * 2 / 3;
    if (dotted)
        return straight * 3 / 2;
    return straight;
}

int64_t ticksPerBar(int sigNum, int sigDen) {
    if (sigNum < 1)
        sigNum = 4;
    if (sigDen < 1)
        sigDen = 4;
    // PPQ is ticks per quarter note. A bar of num/den has num * (4/den) quarters.
    return (int64_t) sigNum * kPpq * 4 / sigDen;
}

int stepsPerBar(StepRate rate, int sigNum, int sigDen) {
    const int64_t step = masterStepTicks(rate);
    const int64_t bar = ticksPerBar(sigNum, sigDen);
    if (step <= 0)
        return 4;
    return (int) std::max<int64_t>(1, bar / step);
}

Scale makeScale(int root, ScaleType type, uint16_t customMask) {
    Scale s;
    s.root = pcOf(root);
    s.type = type;
    uint16_t mask = type == ScaleType::Custom ? customMask : defaultMask(type);
    mask = (uint16_t) (mask | 1u); // root is always in the scale
    s.customMask = mask;
    return s;
}

uint16_t Scale::mask() const {
    uint16_t m = type == ScaleType::Custom ? customMask : defaultMask(type);
    return (uint16_t) (m | 1u);
}

bool Scale::contains(int pitchClass) const {
    const int rel = pcOf(pitchClass - root);
    return ((mask() >> rel) & 1u) != 0;
}

int Scale::degreeIndex(int pitchClass) const {
    if (!contains(pitchClass))
        return -1;
    const int rel = pcOf(pitchClass - root);
    int degree = 0;
    const uint16_t m = mask();
    for (int i = 0; i < rel; ++i)
        if ((m >> i) & 1u)
            ++degree;
    return degree;
}

int Scale::size() const {
    int n = 0;
    const uint16_t m = mask();
    for (int i = 0; i < 12; ++i)
        if ((m >> i) & 1u)
            ++n;
    return std::max(1, n);
}

int scaleDegreeToMidi(const Scale& scale, int degree, int center) {
    const int rootPc = pcOf(scale.root);
    int best = center - pcOf(center) + rootPc;
    for (int oct = -2; oct <= 2; ++oct) {
        const int c = best + 12 * oct;
        if (std::abs(c - center) < std::abs(best - center))
            best = c;
    }
    // best may have been shifted; recompute the closest root, then walk.
    int rootMidi = center - pcOf(center) + rootPc;
    int closest = rootMidi;
    for (int oct = -2; oct <= 2; ++oct) {
        const int c = rootMidi + 12 * oct;
        if (std::abs(c - center) < std::abs(closest - center))
            closest = c;
    }
    if (degree == 0)
        return std::clamp(closest, 0, 127);
    const int dir = degree > 0 ? 1 : -1;
    int left = std::abs(degree);
    int midi = closest;
    for (int guard = 0; left > 0 && guard < 256; ++guard) {
        midi += dir;
        if (midi < 0 || midi > 127)
            break;
        if (scale.contains(midi))
            --left;
    }
    return std::clamp(midi, 0, 127);
}

int transposeScale(const Scale& scale, int midi, int degrees) {
    if (degrees == 0)
        return std::clamp(midi, 0, 127);
    const int dir = degrees > 0 ? 1 : -1;
    int left = std::abs(degrees);
    int n = midi;
    for (int guard = 0; left > 0 && guard < 256; ++guard) {
        n += dir;
        if (n < 0 || n > 127)
            break;
        if (scale.contains(n))
            --left;
    }
    return std::clamp(n, 0, 127);
}

namespace {

constexpr float kMajor[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
constexpr float kMinor[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};

float pearsonRotated(const float* hist, const float* profile, int root) {
    float meanA = 0.f, meanB = 0.f;
    for (int i = 0; i < 12; ++i) {
        meanA += hist[i];
        meanB += profile[i];
    }
    meanA /= 12.f;
    meanB /= 12.f;
    float dot = 0.f, na = 0.f, nb = 0.f;
    for (int i = 0; i < 12; ++i) {
        const float a = hist[i] - meanA;
        const float b = profile[pcOf(i - root)] - meanB;
        dot += a * b;
        na += a * a;
        nb += b * b;
    }
    if (na < 1.0e-8f || nb < 1.0e-8f)
        return 0.f;
    return dot / std::sqrt(na * nb);
}

float scoreKey(const float* hist, int root, ScaleType mode) {
    const float* profile = mode == ScaleType::NaturalMinor ? kMinor : kMajor;
    return pearsonRotated(hist, profile, root);
}

} // namespace

KeyEstimate estimateKey(const float hist[12], const KeyEstimate* previous) {
    KeyEstimate best;
    best.score = -2.f;
    for (int root = 0; root < 12; ++root) {
        for (ScaleType mode : {ScaleType::Major, ScaleType::NaturalMinor}) {
            const float s = scoreKey(hist, root, mode);
            if (s > best.score) {
                best.score = s;
                best.root = root;
                best.mode = mode;
                best.valid = true;
            }
        }
    }
    if (previous != nullptr && previous->valid) {
        const float prevScore = scoreKey(hist, previous->root, previous->mode);
        // Hysteresis: a new key must win by a clear margin or the current key stays.
        if (best.score < prevScore + 0.05f) {
            best.root = previous->root;
            best.mode = previous->mode;
            best.score = prevScore;
            best.valid = true;
        }
    }
    return best;
}

void addChordToHistogram(float hist[12], int root, int bass, uint16_t toneMask) {
    if (toneMask == 0)
        hist[pcOf(root)] += 1.f;
    for (int pc = 0; pc < 12; ++pc)
        if (toneMask & (1u << pc))
            hist[pc] += 1.f;
    hist[pcOf(bass)] += 0.5f;
}

} // namespace phrasearp
