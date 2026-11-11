#pragma once

#include "engine/Util.h"

#include <vector>

namespace phrasearp {

enum class ScaleType : uint8_t {
    Major = 0,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    Custom,
    Count
};

enum class ChordQuality : uint8_t {
    Major = 0,
    Minor,
    Dim,
    Aug,
    Sus2,
    Sus4,
    Six,
    Minor6,
    Dom7,
    Maj7,
    Min7,
    Min7b5,
    Dim7,
    Add9,
    Dom9,
    Min9,
    Maj9,
    Single,
    Unknown
};

enum class ToneRole : uint8_t { Chord, Tension, Passing, Chromatic };

enum class StepRate : uint8_t {
    Quarter = 0,
    QuarterTriplet,
    QuarterDotted,
    Eighth,
    EighthTriplet,
    EighthDotted,
    Sixteenth,
    SixteenthTriplet,
    SixteenthDotted,
    ThirtySecond,
    ThirtySecondTriplet,
    ThirtySecondDotted,
    SixtyFourth,
    SixtyFourthTriplet,
    SixtyFourthDotted,
    Count
};

enum class LaneSpeed : uint8_t { X1_4 = 0, X1_2, X1, X2, X4 };

enum class Direction : uint8_t { Forward = 0, Backward, PingPong, Random, RandomWalk };

enum class ResetMode : uint8_t { Never = 0, EveryNBars, Phrase, ChordChange };

enum class StepType : uint8_t { Note = 0, Rest, Tie, Chord, Strum };

enum class PitchMode : uint8_t {
    Index = 0,
    ScaleDegree,
    Up,
    Down,
    UpDown,
    Random,
    AsPlayed,
    Root,
    Top
};

enum class LaneId : uint8_t {
    Pitch = 0,
    Octave,
    Transpose,
    Type,
    Velocity,
    Gate,
    Ratchet,
    Probability,
    Timing,
    CC1,
    CC2,
    Count
};

enum class PhraseTemplate : uint8_t { AAAA = 0, AAAB, AABA, ABAC, AABB };

enum class ContourShape : uint8_t { Flat = 0, Rising, Arch, Wave, Custom };

enum class DynamicsShape : uint8_t { Flat = 0, Arch, Rising, Wave, Custom };

enum class NoteLengthMode : uint8_t { Gate = 0, FixedMs, Legato };

enum class BoundaryMode : uint8_t { Fold = 0, Wrap };

enum class RetriggerMode : uint8_t { Off = 0, Chord, Bar, Key };

enum class RatchetCurve : uint8_t { Linear = 0, Exponential, Rising };

enum class StrumDirection : uint8_t { Up = 0, Down, Alternate };

enum class MacroTarget : uint8_t {
    None = 0,
    Density,
    Complexity,
    Motion,
    Color,
    Repetition,
    Range,
    Variation,
    Swing,
    Strum
};

enum class ImportMode : uint8_t { Rhythm = 0, RhythmContour, All };

struct ScheduledEvent {
    int64_t tick = 0;
    enum class Type : uint8_t { Off = 0, CC = 1, On = 2 } type = Type::On;
    int channel = 0;
    int note = 0;
    int value = 0;

    bool operator==(const ScheduledEvent& o) const = default;
};

inline bool eventLess(const ScheduledEvent& a, const ScheduledEvent& b) {
    if (a.tick != b.tick)
        return a.tick < b.tick;
    if (a.type != b.type)
        return (int) a.type < (int) b.type;
    if (a.note != b.note)
        return a.note < b.note;
    return a.value < b.value;
}

struct RenderConfig {
    float color = 28.f;
    float variation = 0.f;
    int phraseBars = 4;
    PhraseTemplate phraseTemplate = PhraseTemplate::AAAA;
    bool twoBarUnit = false;
    bool questionAnswer = true;
    int maxJump = 4;
    int accent = 16;
    float swing = 50.f;
    bool swingEighth = true;
    int groove = 0;
    float humanizeMs = 0.f;
    float humanizeVel = 0.f;
    float humanizeGate = 0.f;
    bool humanizeLive = false;
    uint32_t liveCycle = 0;
    int strumMs = 0;
    StrumDirection strumDir = StrumDirection::Down;
    int strumTilt = 10;
    NoteLengthMode noteLength = NoteLengthMode::Gate;
    int fixedMs = 250;
    int outputChannel = 1;
    int cc1Number = 74;
    int cc2Number = 1;
    bool ccSmooth = false;
    int keyRoot = 0;
    ScaleType scaleType = ScaleType::Major;
    bool autoKey = false;
    bool useLiveHist = false;
    float liveHist[12] {};
    int lowNote = 48;
    int highNote = 84;
    int octaves = 3;
    BoundaryMode boundary = BoundaryMode::Fold;
    RetriggerMode retrigger = RetriggerMode::Off;
    ContourShape contour = ContourShape::Flat;
    DynamicsShape dynamics = DynamicsShape::Arch;
    bool chordTrackEnabled = true;
    StepRate rate = StepRate::Eighth;
    std::vector<int> heldNotes;
    int64_t keyRetriggerTick = -1;
    RatchetCurve ratchetCurve = RatchetCurve::Linear;
    bool bakeFeel = true;
};

struct GenerateSettings {
    float density = 58.f;
    float complexity = 32.f;
    float motion = 38.f;
    float color = 28.f;
    float repetition = 74.f;
    float range = 48.f;
    float variation = 0.f;
    bool euclidean = false;
    int stepsPerBar = 8;
    int phraseBars = 4;
    uint32_t seed = kInitSeed;
    int maxJump = 4;
    int lowNote = 48;
    int highNote = 84;
    int keyRoot = 0;
    ScaleType scaleType = ScaleType::Major;
};

inline int channelIndex(int outputChannel) {
    if (outputChannel < 1)
        return 0;
    if (outputChannel > 16)
        return 15;
    return outputChannel - 1;
}

const char* scaleTypeName(ScaleType type);
const char* stepRateName(StepRate rate);
int64_t masterStepTicks(StepRate rate);
int64_t ticksPerBar(int sigNum, int sigDen);
int stepsPerBar(StepRate rate, int sigNum, int sigDen);

} // namespace phrasearp
