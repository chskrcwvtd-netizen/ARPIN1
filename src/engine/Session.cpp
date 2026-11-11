#include "engine/Session.h"

#include "engine/Scale.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace phrasearp {

void History::push(const AudioState& state) {
    if (index >= 0 && index < (int) states.size() && states[(size_t) index] == state)
        return;
    if (index + 1 < (int) states.size())
        states.erase(states.begin() + index + 1, states.end());
    states.push_back(state);
    if ((int) states.size() > kHistory) {
        states.erase(states.begin());
    }
    index = (int) states.size() - 1;
}

const AudioState* History::undo() {
    if (!canUndo())
        return nullptr;
    --index;
    return &states[(size_t) index];
}

const AudioState* History::redo() {
    if (!canRedo())
        return nullptr;
    ++index;
    return &states[(size_t) index];
}

const AudioState* History::current() const {
    if (index < 0 || index >= (int) states.size())
        return nullptr;
    return &states[(size_t) index];
}

const Pattern& activePattern(const AudioState& state) {
    const int slot = std::clamp(state.activeSlot, 0, kPatternSlots - 1);
    return state.patterns[(size_t) slot];
}

RenderConfig defaultRenderConfig() {
    RenderConfig cfg;
    cfg.color = 28.f;
    cfg.variation = 0.f;
    cfg.phraseBars = 4;
    cfg.phraseTemplate = PhraseTemplate::AAAA;
    cfg.twoBarUnit = false;
    cfg.questionAnswer = true;
    cfg.maxJump = 4;
    cfg.accent = 16;
    cfg.swing = 50.f;
    cfg.swingEighth = true;
    cfg.groove = 0;
    cfg.humanizeMs = 0.f;
    cfg.humanizeVel = 0.f;
    cfg.humanizeGate = 0.f;
    cfg.humanizeLive = false;
    cfg.strumMs = 0;
    cfg.strumDir = StrumDirection::Down;
    cfg.strumTilt = 10;
    cfg.noteLength = NoteLengthMode::Gate;
    cfg.fixedMs = 250;
    cfg.outputChannel = 1;
    cfg.cc1Number = 74;
    cfg.cc2Number = 1;
    cfg.ccSmooth = false;
    cfg.keyRoot = 0;
    cfg.scaleType = ScaleType::Major;
    cfg.autoKey = false;
    cfg.lowNote = 48;
    cfg.highNote = 84;
    cfg.octaves = 3;
    cfg.boundary = BoundaryMode::Fold;
    cfg.retrigger = RetriggerMode::Off;
    cfg.contour = ContourShape::Flat;
    cfg.dynamics = DynamicsShape::Arch;
    cfg.chordTrackEnabled = true;
    cfg.rate = StepRate::Eighth;
    cfg.ratchetCurve = RatchetCurve::Linear;
    cfg.bakeFeel = true;
    return cfg;
}

GenerateSettings defaultGenerateSettings(const RenderConfig& cfg) {
    GenerateSettings g;
    g.density = 58.f;
    g.complexity = 32.f;
    g.motion = 38.f;
    g.color = cfg.color;
    g.repetition = 74.f;
    g.range = 48.f;
    g.variation = cfg.variation;
    g.euclidean = false;
    g.stepsPerBar = stepsPerBar(cfg.rate, 4, 4);
    g.phraseBars = cfg.phraseBars;
    g.seed = kInitSeed;
    g.maxJump = cfg.maxJump;
    g.lowNote = cfg.lowNote;
    g.highNote = cfg.highNote;
    g.keyRoot = cfg.keyRoot;
    g.scaleType = cfg.scaleType;
    return g;
}

AudioState makeInitState() {
    AudioState state;
    state.seed = kInitSeed;
    state.presetName = "Init";
    state.customScale = defaultMask(ScaleType::Major);
    state.macroDepth.fill(1.f);
    state.contourCustom = {0.f, 0.3f, 0.7f, 1.f, 1.f, 0.7f, 0.3f, 0.f};
    state.dynamicsCustom = {0.7f, 0.85f, 1.f, 0.95f, 0.9f, 0.8f, 0.75f, 0.7f};

    // Through-composed 4-bar line in C major, scale degrees from C4.
    // Bar 1 C, bar 2 Am, bar 3 F, bar 4 C. Leaps stay inside the default 4-semitone jump.
    const int16_t degrees[32] = {
        0, 2, 4, 2, 0, 1, 2, 4,
        2, 0, 1, 2, 0, -2, 0, 2,
        3, 5, 4, 5, 3, 4, 3, 5,
        4, 2, 4, 5, 4, 2, 1, 0
    };
    const int vel[32] = {
        100, 78, 88, 76, 96, 80, 84, 74,
        100, 78, 86, 76, 96, 82, 80, 74,
        104, 82, 90, 78, 100, 84, 80, 76,
        108, 86, 96, 90, 100, 84, 78, 96
    };

    Pattern pattern;
    pattern.pitchMode = PitchMode::ScaleDegree;
    preparePattern(pattern, 32);
    for (int i = 0; i < 32; ++i) {
        pattern.pitch.steps[(size_t) i] = degrees[i];
        pattern.type.steps[(size_t) i] = (uint8_t) StepType::Note;
        pattern.velocity.steps[(size_t) i] = (uint8_t) vel[i];
        pattern.gate.steps[(size_t) i] = (i == 31) ? 140 : 85;
        pattern.ratchet.steps[(size_t) i] = 1;
        pattern.probability.steps[(size_t) i] = 100;
        pattern.timing.steps[(size_t) i] = 0;
        pattern.octave.steps[(size_t) i] = 0;
        pattern.transpose.steps[(size_t) i] = 0;
        pattern.cc1.steps[(size_t) i] = (uint8_t) std::clamp(40 + (i * 50) / 31, 0, 127);
        pattern.cc2.steps[(size_t) i] = (uint8_t) (64 + (i % 8) * 2);
        const bool strong = (i % 8) == 0 || (i % 8) == 4;
        pattern.accent[(size_t) i] = (strong || i == 31) ? 1 : 0;
    }
    for (int s = 0; s < kPatternSlots; ++s)
        state.patterns[(size_t) s] = pattern;

    state.chords.enabled = true;
    state.chords.bars.assign(16, {});
    state.chords.bars[0] = {true, {60, 64, 67}};
    state.chords.bars[1] = {true, {57, 60, 64}};
    state.chords.bars[2] = {true, {53, 57, 60}};
    state.chords.bars[3] = {true, {60, 64, 67}};
    return state;
}

namespace {

template <typename T>
void emitLane(std::ostream& out, const char* name, const Lane<T>& lane) {
    out << "lane " << name << ' ' << lane.length << ' ' << (int) lane.speed << ' ' << (int) lane.direction << ' '
        << (int) lane.reset << ' ' << (lane.locked ? 1 : 0) << '\n';
    out << "v";
    for (auto v : lane.steps)
        out << ' ' << (int) v;
    out << '\n';
    out << "k";
    for (auto v : lane.locks)
        out << ' ' << (int) v;
    out << '\n';
}

bool readNumbers(const std::string& line, std::vector<long>& nums) {
    std::istringstream in(line);
    long v = 0;
    nums.clear();
    while (in >> v)
        nums.push_back(v);
    return true;
}

template <typename T>
bool applyValues(Lane<T>& lane, const std::vector<long>& nums, int lo, int hi) {
    lane.steps.resize(nums.size());
    lane.length = std::max(1, (int) nums.size());
    for (size_t i = 0; i < nums.size(); ++i)
        lane.steps[i] = (T) std::clamp((int) nums[i], lo, hi);
    if (lane.locks.size() != lane.steps.size())
        lane.locks.resize(lane.steps.size(), 0);
    return true;
}

} // namespace

std::string serializeState(const AudioState& state) {
    std::ostringstream out;
    out << "PHRASEARP 1\n";
    out << "seed " << state.seed << '\n';
    out << "name " << state.presetName << '\n';
    out << "slot " << state.activeSlot << '\n';
    out << "scaleMask " << state.customScale << '\n';
    out << "keySwitch " << (state.keySwitchEnabled ? 1 : 0) << ' ' << state.keySwitchLow << ' ' << state.keySwitchHigh
        << '\n';
    out << "resetN " << state.resetEveryNBars << '\n';
    out << "chain " << state.chainLength;
    for (int i = 0; i < state.chainLength; ++i)
        out << ' ' << state.chain[(size_t) i];
    out << '\n';
    out << "grooveT";
    for (auto v : state.grooveTiming)
        out << ' ' << (int) v;
    out << '\n';
    out << "grooveV";
    for (auto v : state.grooveVelocity)
        out << ' ' << (int) v;
    out << '\n';
    out << "macroT";
    for (auto v : state.macroTarget)
        out << ' ' << v;
    out << '\n';
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "macroD";
    for (auto v : state.macroDepth)
        out << ' ' << v;
    out << '\n';
    out << "contourC";
    for (auto v : state.contourCustom)
        out << ' ' << v;
    out << '\n';
    out << "dynamicsC";
    for (auto v : state.dynamicsCustom)
        out << ' ' << v;
    out << '\n';
    for (int p = 0; p < kPatternSlots; ++p) {
        const Pattern& pat = state.patterns[(size_t) p];
        out << "pattern " << p << ' ' << (int) pat.pitchMode << '\n';
        emitLane(out, "pitch", pat.pitch);
        emitLane(out, "octave", pat.octave);
        emitLane(out, "transpose", pat.transpose);
        emitLane(out, "type", pat.type);
        emitLane(out, "velocity", pat.velocity);
        out << "accent";
        for (auto v : pat.accent)
            out << ' ' << (int) v;
        out << '\n';
        emitLane(out, "gate", pat.gate);
        emitLane(out, "ratchet", pat.ratchet);
        emitLane(out, "probability", pat.probability);
        emitLane(out, "timing", pat.timing);
        emitLane(out, "cc1", pat.cc1);
        emitLane(out, "cc2", pat.cc2);
        out << "endpattern\n";
    }
    out << "chords " << (state.chords.enabled ? 1 : 0) << '\n';
    for (size_t i = 0; i < state.chords.bars.size(); ++i) {
        if (!state.chords.bars[i].set)
            continue;
        out << "bar " << i;
        for (int n : state.chords.bars[i].notes)
            out << ' ' << n;
        out << '\n';
    }
    out << "end\n";
    return out.str();
}

bool deserializeState(const std::string& text, AudioState& state) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line))
        return false;
    if (line.rfind("PHRASEARP ", 0) != 0)
        return false;
    AudioState next = makeInitState();
    Pattern* cur = nullptr;
    std::string pendingLane;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "seed") {
            ls >> next.seed;
        } else if (tag == "name") {
            std::string name;
            std::getline(ls, name);
            if (!name.empty() && name[0] == ' ')
                name.erase(name.begin());
            if (!name.empty())
                next.presetName = name;
        } else if (tag == "slot") {
            ls >> next.activeSlot;
        } else if (tag == "scaleMask") {
            int m = 0;
            ls >> m;
            next.customScale = (uint16_t) m;
        } else if (tag == "keySwitch") {
            int en = 0;
            ls >> en >> next.keySwitchLow >> next.keySwitchHigh;
            next.keySwitchEnabled = en != 0;
        } else if (tag == "resetN") {
            ls >> next.resetEveryNBars;
        } else if (tag == "chain") {
            ls >> next.chainLength;
            next.chainLength = std::clamp(next.chainLength, 0, kMaxChain);
            for (int i = 0; i < next.chainLength; ++i)
                ls >> next.chain[(size_t) i];
        } else if (tag == "grooveT" || tag == "grooveV" || tag == "macroT" || tag == "accent") {
            std::vector<long> nums;
            long v = 0;
            int i = 0;
            while (ls >> v) {
                if (tag == "grooveT" && i < 16)
                    next.grooveTiming[(size_t) i] = (int8_t) v;
                else if (tag == "grooveV" && i < 16)
                    next.grooveVelocity[(size_t) i] = (int8_t) v;
                else if (tag == "macroT" && i < 8)
                    next.macroTarget[(size_t) i] = (int) v;
                else if (tag == "accent" && cur != nullptr) {
                    if (i == 0)
                        cur->accent.clear();
                    cur->accent.push_back((uint8_t) std::clamp((int) v, 0, 1));
                }
                ++i;
            }
            (void) nums;
        } else if (tag == "macroD" || tag == "contourC" || tag == "dynamicsC") {
            int i = 0;
            double v = 0;
            while (ls >> v) {
                if (tag == "macroD" && i < 8)
                    next.macroDepth[(size_t) i] = (float) v;
                else if (tag == "contourC" && i < 8)
                    next.contourCustom[(size_t) i] = (float) v;
                else if (tag == "dynamicsC" && i < 8)
                    next.dynamicsCustom[(size_t) i] = (float) v;
                ++i;
            }
        } else if (tag == "pattern") {
            int idx = 0, mode = 0;
            ls >> idx >> mode;
            if (idx >= 0 && idx < kPatternSlots) {
                cur = &next.patterns[(size_t) idx];
                cur->pitchMode = (PitchMode) std::clamp(mode, 0, (int) PitchMode::Top);
            }
        } else if (tag == "lane" && cur != nullptr) {
            std::string name;
            int length = 16, speed = 0, dir = 0, reset = 0, locked = 0;
            ls >> name >> length >> speed >> dir >> reset >> locked;
            pendingLane = name;
            auto setup = [&](auto& lane) {
                lane.length = std::clamp(length, 1, kMaxSteps);
                lane.speed = (LaneSpeed) std::clamp(speed, 0, 4);
                lane.direction = (Direction) std::clamp(dir, 0, 4);
                lane.reset = (ResetMode) std::clamp(reset, 0, 3);
                lane.locked = locked != 0;
            };
            if (name == "pitch") setup(cur->pitch);
            else if (name == "octave") setup(cur->octave);
            else if (name == "transpose") setup(cur->transpose);
            else if (name == "type") setup(cur->type);
            else if (name == "velocity") setup(cur->velocity);
            else if (name == "gate") setup(cur->gate);
            else if (name == "ratchet") setup(cur->ratchet);
            else if (name == "probability") setup(cur->probability);
            else if (name == "timing") setup(cur->timing);
            else if (name == "cc1") setup(cur->cc1);
            else if (name == "cc2") setup(cur->cc2);
        } else if ((tag == "v" || tag == "k") && cur != nullptr) {
            std::vector<long> nums;
            long v = 0;
            while (ls >> v)
                nums.push_back(v);
            auto putV = [&](auto& lane, int lo, int hi) {
                if (tag == "v") {
                    lane.steps.resize(nums.size());
                    using Value = std::decay_t<decltype(lane.steps[0])>;
                    for (size_t i = 0; i < nums.size(); ++i)
                        lane.steps[i] = static_cast<Value>(std::clamp((int) nums[i], lo, hi));
                    lane.length = std::max(1, (int) nums.size());
                } else {
                    lane.locks.resize(nums.size());
                    for (size_t i = 0; i < nums.size(); ++i)
                        lane.locks[i] = nums[i] ? 1 : 0;
                }
            };
            if (pendingLane == "pitch") putV(cur->pitch, -24, 24);
            else if (pendingLane == "octave") putV(cur->octave, -2, 2);
            else if (pendingLane == "transpose") putV(cur->transpose, -7, 7);
            else if (pendingLane == "type") putV(cur->type, 0, 4);
            else if (pendingLane == "velocity") putV(cur->velocity, 1, 127);
            else if (pendingLane == "gate") putV(cur->gate, 5, 400);
            else if (pendingLane == "ratchet") putV(cur->ratchet, 1, 8);
            else if (pendingLane == "probability") putV(cur->probability, 0, 100);
            else if (pendingLane == "timing") putV(cur->timing, -50, 50);
            else if (pendingLane == "cc1") putV(cur->cc1, 0, 127);
            else if (pendingLane == "cc2") putV(cur->cc2, 0, 127);
        } else if (tag == "chords") {
            int en = 1;
            ls >> en;
            next.chords.enabled = en != 0;
            next.chords.bars.assign(16, {});
        } else if (tag == "bar") {
            int idx = 0;
            ls >> idx;
            if (idx >= 0 && idx < 64) {
                if ((int) next.chords.bars.size() <= idx)
                    next.chords.bars.resize((size_t) idx + 1);
                next.chords.bars[(size_t) idx].set = true;
                next.chords.bars[(size_t) idx].notes.clear();
                int n = 0;
                while (ls >> n)
                    next.chords.bars[(size_t) idx].notes.push_back(n);
            }
        } else if (tag == "end") {
            break;
        }
    }
    next.activeSlot = std::clamp(next.activeSlot, 0, kPatternSlots - 1);
    for (auto& pat : next.patterns) {
        if (pat.accent.size() != pat.velocity.steps.size())
            pat.accent.resize(pat.velocity.steps.size(), 0);
    }
    state = std::move(next);
    return true;
}

} // namespace phrasearp
