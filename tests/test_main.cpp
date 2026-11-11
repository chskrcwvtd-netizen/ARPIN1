#include "engine/Chord.h"
#include "engine/Generator.h"
#include "engine/Harmony.h"
#include "engine/Importer.h"
#include "engine/LockFreeSwap.h"
#include "engine/MidiFile.h"
#include "engine/PreviewSynth.h"
#include "engine/Render.h"
#include "engine/Scale.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int gFails = 0;

void check(bool ok, const char* expr, int line) {
    if (!ok) {
        std::cerr << "FAIL line " << line << ": " << expr << '\n';
        ++gFails;
    }
}

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __LINE__)

phrasearp::Scale cMajor() {
    return phrasearp::makeScale(0, phrasearp::ScaleType::Major, 0);
}

std::vector<int> onsets(const phrasearp::RenderOutput& out) {
    std::vector<int> notes;
    notes.reserve(out.onsets.size());
    for (const auto& n : out.onsets)
        notes.push_back(n.note);
    return notes;
}

void dumpNotes(const std::vector<int>& notes) {
    for (size_t i = 0; i < notes.size(); ++i)
        std::cerr << (i ? "," : "") << notes[i];
    std::cerr << '\n';
}

void testScaleDegree() {
    using namespace phrasearp;
    const Scale scale = cMajor();
    CHECK(scaleDegreeToMidi(scale, 0, 60) == 60);
    CHECK(scaleDegreeToMidi(scale, 1, 60) == 62);
    CHECK(scaleDegreeToMidi(scale, 2, 60) == 64);
    CHECK(scaleDegreeToMidi(scale, 3, 60) == 65);
    CHECK(scaleDegreeToMidi(scale, 4, 60) == 67);
    CHECK(scaleDegreeToMidi(scale, 5, 60) == 69);
    CHECK(scaleDegreeToMidi(scale, 7, 60) == 72);
    CHECK(scaleDegreeToMidi(scale, -1, 60) == 59);
    CHECK(scaleDegreeToMidi(scale, -2, 60) == 57);
}

void testChords() {
    using namespace phrasearp;
    const Scale scale = cMajor();
    auto named = [&](std::vector<int> notes) { return recognizeChord(notes, &scale); };

    CHECK(named({60, 64, 67}).name == "C");
    CHECK(named({64, 67, 72}).name == "C/E");
    CHECK(named({60, 63, 67}).name == "Cm");
    CHECK(named({60, 64, 67, 70}).name == "C7");
    CHECK(named({60, 64, 67, 71}).name == "Cmaj7");
    CHECK(named({60, 63, 67, 70}).name == "Cm7");
    CHECK(named({60, 63, 66, 70}).name == "Cm7b5");
    CHECK(named({60, 63, 66, 69}).name == "Cdim7");
    CHECK(named({60, 64, 68}).name == "Caug");
    CHECK(named({60, 62, 67}).name == "Csus2");
    CHECK(named({60, 65, 67}).name == "Csus4");
    CHECK(named({60, 64, 67, 69}).name == "C6");
    CHECK(named({60, 63, 67, 69}).name == "Cm6");
    CHECK(named({60, 64, 67, 62}).name == "Cadd9");
    CHECK(named({60, 64, 67, 70, 62}).name == "C9");
    CHECK(named({60, 63, 67, 70, 62}).name == "Cm9");
    CHECK(named({60, 64, 67, 71, 62}).name == "Cmaj9");
    CHECK(named({60, 63, 66}).name == "Cdim");

    const Chord cluster = named({60, 61, 62});
    CHECK(!cluster.ok);
    CHECK(cluster.name == "C C# D");

    const Chord single = recognizeChord({60}, &scale);
    CHECK(single.quality == ChordQuality::Single);
    CHECK(single.name == "C");
    CHECK((single.tones & (1u << 0)) != 0);
    CHECK((single.tones & (1u << 4)) != 0);
    CHECK((single.tones & (1u << 7)) != 0);

    const Chord dyad = named({60, 64});
    CHECK(dyad.ok);
    CHECK(dyad.quality == ChordQuality::Major);
    CHECK(dyad.name == "C");
}

void testVoiceLeading() {
    using namespace phrasearp;
    const Scale scale = cMajor();
    const Chord c = recognizeChord({60, 64, 67}, &scale);
    const Chord f = recognizeChord({65, 69, 72}, &scale);
    Voicing first = voiceLead(nullptr, c, 36, 96, 4);
    CHECK(first.count == 3);
    CHECK(first.notes[0] == 60);
    CHECK(first.notes[1] == 64);
    CHECK(first.notes[2] == 67);
    CHECK(first.rootPitch == 60);

    Voicing next = voiceLead(&first, f, 36, 96, 4);
    CHECK(next.count == 3);
    CHECK(next.notes[0] == 60);
    CHECK(next.notes[1] == 65);
    CHECK(next.notes[2] == 69);
    CHECK(next.rootPitch == 65);
    int cost = 0;
    for (int i = 0; i < 3; ++i)
        cost += std::abs(next.notes[(size_t) i] - first.notes[(size_t) i]);
    CHECK(cost == 3);

    const auto line = realizeLine({0, 1, 2, 2}, {c, c, c, f}, scale, PitchMode::Index, 0.f, 4, 36, 96);
    const std::vector<int> expect = {60, 64, 67, 69};
    if (line != expect) {
        std::cerr << "realizeLine got ";
        dumpNotes(line);
    }
    CHECK(line == expect);
    CHECK(line.back() > line[2]);
    CHECK(std::abs(line.back() - line[2]) <= 4);
    CHECK(isChordTone(line.back(), f));
    CHECK(line.back() != 72);
}

void testInitPhrase() {
    using namespace phrasearp;
    const AudioState state = makeInitState();
    const RenderConfig cfg = defaultRenderConfig();
    const int64_t phrase = ticksPerBar(4, 4) * cfg.phraseBars;
    const RenderOutput a = renderMusic(state, cfg, 0, phrase, 120.0, 4, 4);
    const RenderOutput b = renderMusic(state, cfg, 0, phrase, 120.0, 4, 4);
    CHECK(a.events == b.events);
    CHECK(a.onsets == b.onsets);

    const std::vector<int> expect = {
        60, 64, 67, 64, 60, 62, 64, 67,
        64, 60, 62, 64, 60, 57, 60, 64,
        65, 69, 67, 69, 65, 67, 65, 69,
        67, 64, 67, 69, 67, 64, 62, 60
    };
    const auto got = onsets(a);
    if (got != expect) {
        std::cerr << "init onsets (" << got.size() << ") ";
        dumpNotes(got);
    }
    CHECK(got == expect);

    const Scale scale = cMajor();
    const Chord chords[4] = {
        recognizeChord({60, 64, 67}, &scale),
        recognizeChord({57, 60, 64}, &scale),
        recognizeChord({53, 57, 60}, &scale),
        recognizeChord({60, 64, 67}, &scale),
    };
    CHECK(a.onsets.size() == expect.size());
    for (size_t i = 0; i < a.onsets.size(); ++i) {
        const int64_t barTicks = ticksPerBar(4, 4);
        const int bar = (int) (a.onsets[i].onTick / barTicks);
        const int64_t pos = a.onsets[i].onTick % barTicks;
        const int64_t beat = barTicks / 4;
        const bool strong = pos <= 1 || std::llabs(pos - 2 * beat) <= 1;
        if (strong)
            CHECK(isChordTone(a.onsets[i].note, chords[std::clamp(bar, 0, 3)]));
        if (i > 0)
            CHECK(std::abs(a.onsets[i].note - a.onsets[i - 1].note) <= 4);
        if (!isChordTone(a.onsets[i].note, chords[std::clamp(bar, 0, 3)]) && i + 1 < a.onsets.size()) {
            CHECK(std::abs(a.onsets[i + 1].note - a.onsets[i].note) <= 2);
            CHECK(isChordTone(a.onsets[i + 1].note, chords[std::clamp((int) (a.onsets[i + 1].onTick / barTicks), 0, 3)]));
        }
    }
    CHECK(pcOf(a.onsets.back().note) == 0);
    CHECK(a.onsets.back().offTick - a.onsets.back().onTick == 1344);
    CHECK(pcOf(a.onsets[15].note) != 0);

    const int64_t step = masterStepTicks(StepRate::Eighth);
    int ordinary = 0;
    for (size_t i = 0; i + 1 < a.onsets.size(); ++i) {
        if (a.onsets[i].offTick - a.onsets[i].onTick == step * 85 / 100)
            ++ordinary;
    }
    CHECK(ordinary == 31);
}

void testQuestionAnswerAndClosure() {
    using namespace phrasearp;
    AudioState state = makeInitState();
    Pattern& pat = state.patterns[0];
    for (int s = 0; s < kPatternSlots; ++s)
        state.patterns[(size_t) s] = pat;
    for (auto& step : pat.pitch.steps)
        step = 0;
    for (int s = 0; s < kPatternSlots; ++s)
        state.patterns[(size_t) s] = pat;
    for (auto& bar : state.chords.bars)
        bar = {true, {60, 64, 67}};

    RenderConfig on = defaultRenderConfig();
    RenderConfig off = on;
    off.questionAnswer = false;
    const int64_t phrase = ticksPerBar(4, 4) * 4;
    const auto withQa = onsets(renderMusic(state, on, 0, phrase, 120.0, 4, 4));
    const auto noQa = onsets(renderMusic(state, off, 0, phrase, 120.0, 4, 4));
    CHECK(withQa.size() == 32);
    CHECK(noQa.size() == 32);
    CHECK(noQa[15] == 60);
    CHECK(withQa[15] == 64);
    CHECK(withQa.back() == 60);
    CHECK(noQa.back() == 60);

    for (auto& step : pat.pitch.steps)
        step = 5;
    pat.pitch.steps.back() = 5;
    for (int s = 0; s < kPatternSlots; ++s)
        state.patterns[(size_t) s] = pat;
    off.questionAnswer = false;
    const auto closed = onsets(renderMusic(state, off, 0, phrase, 120.0, 4, 4));
    CHECK(!closed.empty());
    CHECK(pcOf(closed.back()) == 0);
}

void testDeterminismAndChunks() {
    using namespace phrasearp;
    const AudioState state = makeInitState();
    RenderConfig cfg = defaultRenderConfig();
    cfg.swing = 62.f;
    cfg.humanizeMs = 8.f;
    cfg.humanizeVel = 6.f;
    cfg.humanizeGate = 5.f;
    cfg.humanizeLive = false;
    const int64_t end = ticksPerBar(4, 4) * 8;
    const RenderOutput full = renderMusic(state, cfg, 0, end, 120.0, 4, 4);
    const RenderOutput again = renderMusic(state, cfg, 0, end, 100.0, 4, 4);
    // Tempo changes humanize tick offsets, so the identical call is the determinism check.
    const RenderOutput same = renderMusic(state, cfg, 0, end, 120.0, 4, 4);
    CHECK(full.events == same.events);
    CHECK(!(full.events == again.events));

    std::vector<ScheduledEvent> chunks;
    int64_t cursor = 0;
    const int64_t sizes[] = {100, 480, 333, 960, 5000, 1, 8000};
    size_t si = 0;
    while (cursor < end) {
        const int64_t n = sizes[si % 7];
        ++si;
        const int64_t to = std::min(end, cursor + n);
        const RenderOutput part = renderMusic(state, cfg, cursor, to, 120.0, 4, 4);
        for (const auto& e : part.events) {
            CHECK(e.tick >= cursor);
            CHECK(e.tick < to);
            chunks.push_back(e);
        }
        cursor = to;
    }
    CHECK(chunks == full.events);

    cfg.liveCycle = 9;
    const RenderOutput cycleIgnored = renderMusic(state, cfg, 0, end, 120.0, 4, 4);
    CHECK(cycleIgnored.events == full.events);
    cfg.humanizeLive = true;
    const RenderOutput cycleUsed = renderMusic(state, cfg, 0, end, 120.0, 4, 4);
    CHECK(cycleUsed.events != full.events);
}

void testProbability() {
    using namespace phrasearp;
    AudioState state = makeInitState();
    auto setProb = [&](int p) {
        for (auto& pat : state.patterns)
            for (auto& step : pat.probability.steps)
                step = (uint8_t) p;
    };
    RenderConfig cfg = defaultRenderConfig();
    const int64_t phrase = ticksPerBar(4, 4) * 4;
    setProb(0);
    CHECK(onsets(renderMusic(state, cfg, 0, phrase, 120.0, 4, 4)).empty());
    setProb(100);
    CHECK(onsets(renderMusic(state, cfg, 0, phrase, 120.0, 4, 4)).size() == 32);
    setProb(50);
    const auto full = renderMusic(state, cfg, 0, phrase, 120.0, 4, 4);
    const auto other = renderMusic(state, cfg, 0, phrase, 120.0, 4, 4);
    CHECK(full.events == other.events);
    std::vector<ScheduledEvent> chunks;
    for (int64_t t = 0; t < phrase; t += 700) {
        const auto part = renderMusic(state, cfg, t, std::min(phrase, t + 700), 120.0, 4, 4);
        chunks.insert(chunks.end(), part.events.begin(), part.events.end());
    }
    CHECK(chunks == full.events);
    CHECK(!full.onsets.empty());
    CHECK(full.onsets.size() < 32);

    const uint32_t h = stepHash(kInitSeed, 0, (uint32_t) LaneId::Probability, 0, 0xA11CEu);
    CHECK((h % 1000u) < 1000u);
    CHECK(stepHash(1, 2, 3, 4, 5) == stepHash(1, 2, 3, 4, 5));
    CHECK(stepHash(1, 2, 3, 4, 5) != stepHash(1, 2, 3, 4, 6));
}

void testLock() {
    using namespace phrasearp;
    const AudioState state = makeInitState();
    const GenerateSettings settings = defaultGenerateSettings(defaultRenderConfig());
    const Scale scale = cMajor();
    const Chord chord = recognizeChord({60, 64, 67}, &scale);
    const Pattern base = activePattern(state);
    const Pattern a = generatePattern(base, settings, chord);
    const Pattern b = generatePattern(base, settings, chord);
    CHECK(a == b);
    CHECK(a.pitchMode == PitchMode::Index);

    Pattern locked = base;
    locked.velocity.locked = true;
    const Pattern kept = generatePattern(locked, settings, chord);
    CHECK(kept.velocity.steps == base.velocity.steps);
    CHECK(kept.velocity.locks == base.velocity.locks);
    CHECK(kept.pitch.steps == a.pitch.steps);
    CHECK(kept.type.steps == a.type.steps);
    CHECK(kept.gate.steps == a.gate.steps);
    CHECK(kept.octave.steps == a.octave.steps);
    CHECK(kept.probability.steps == a.probability.steps);
    CHECK(kept.cc1.steps == a.cc1.steps);

    Pattern step = base;
    if (!step.pitch.locks.empty())
        step.pitch.locks[3] = 1;
    const int16_t held = step.pitch.steps[3];
    const Pattern partial = generatePattern(step, settings, chord);
    CHECK(partial.pitch.steps[3] == held);
    CHECK(partial.pitch.steps[0] == a.pitch.steps[0]);
    CHECK(partial.pitch.steps[4] == a.pitch.steps[4]);
    CHECK(partial.type.steps == a.type.steps);
    CHECK(partial.velocity.steps == a.velocity.steps);

    GenerateSettings other = settings;
    other.seed = settings.seed ^ 0x00FFu;
    const Pattern c = generatePattern(base, other, chord);
    CHECK(c.pitch.steps != a.pitch.steps);
}

void testSerializeAndMidi() {
    using namespace phrasearp;
    AudioState state = makeInitState();
    state.presetName = "Kept";
    state.seed = 0xA1B2C3D4u;
    state.activeSlot = 2;
    state.patterns[2].pitch.steps[0] = 3;
    const std::string text = serializeState(state);
    AudioState back;
    CHECK(deserializeState(text, back));
    CHECK(back == state);

    const auto rendered = renderMusic(state, defaultRenderConfig(), 0, ticksPerBar(4, 4) * 4, 120.0, 4, 4);
    std::vector<SmfNote> notes;
    for (const auto& n : rendered.onsets) {
        SmfNote s;
        s.tick = (int) n.onTick;
        s.duration = (int) (n.offTick - n.onTick);
        s.note = n.note;
        s.velocity = n.velocity;
        notes.push_back(s);
    }
    const auto bytes = writeSmf(notes, 120.0, 4, 4, "PhraseArp");
    const ParsedMidi parsed = parseSmf(bytes);
    CHECK(parsed.ok);
    CHECK(parsed.notes.size() == notes.size());
    if (parsed.notes.size() == notes.size()) {
        for (size_t i = 0; i < notes.size(); ++i) {
            CHECK(parsed.notes[i].note == notes[i].note);
            CHECK(parsed.notes[i].onTick == notes[i].tick);
            CHECK(parsed.notes[i].velocity == notes[i].velocity);
        }
    }

    ImportResult garbage = importMidiBytes({1, 2, 3, 4, 5}, ImportMode::All, cMajor());
    CHECK(!garbage.ok);
    ImportResult empty = importMidiBytes(writeSmf({}, 120.0, 4, 4, "Empty"), ImportMode::All, cMajor());
    CHECK(!empty.ok);
    SmfNote late;
    late.tick = 16 * (int) ticksPerBar(4, 4);
    late.duration = 480;
    late.note = 60;
    late.velocity = 100;
    ImportResult tooLong = importMidiBytes(writeSmf({late}, 120.0, 4, 4, "Long"), ImportMode::All, cMajor());
    CHECK(!tooLong.ok);
}

void testSynthAndSwap() {
    using namespace phrasearp;
    PreviewSynth synth;
    synth.setSampleRate(44100.0);
    synth.noteOn(60, 100, Timbre::Pluck, 0.6f);
    std::vector<float> left(22050, 0.f), right(22050, 0.f);
    synth.render(left.data(), right.data(), (int) left.size(), 0.8f, true);
    float peak = 0.f;
    bool finite = true;
    for (size_t i = 0; i < left.size(); ++i) {
        peak = std::max(peak, std::fabs(left[i]));
        peak = std::max(peak, std::fabs(right[i]));
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
            finite = false;
    }
    CHECK(finite);
    CHECK(peak > 0.02f);

    std::vector<float> quiet(8000, 0.f), quietR(8000, 0.f);
    synth.render(quiet.data(), quietR.data(), (int) quiet.size(), 0.8f, false);
    float quietPeak = 0.f;
    for (size_t i = 0; i < quiet.size(); ++i)
        quietPeak = std::max(quietPeak, std::fabs(quiet[i]));
    CHECK(quietPeak < peak);

    LockFreeSwap<int> swap;
    int dest = -1;
    CHECK(!swap.takeInto(dest));
    swap.publish(4);
    swap.publish(9);
    CHECK(swap.takeInto(dest));
    CHECK(dest == 9);
    CHECK(!swap.takeInto(dest));
    swap.drain();
}

void testAutoKeyLaterBars() {
    using namespace phrasearp;
    AudioState state = makeInitState();
    for (auto& bar : state.chords.bars) {
        bar.set = true;
        bar.notes = {53, 56, 60};
    }
    RenderConfig cfg = defaultRenderConfig();
    cfg.autoKey = true;
    cfg.keyRoot = 0;
    cfg.scaleType = ScaleType::Major;
    cfg.color = 0.f;
    const int64_t phrase = ticksPerBar(4, 4) * cfg.phraseBars;
    const RenderOutput out = renderMusic(state, cfg, 0, phrase, 120.0, 4, 4);
    CHECK(!out.onsets.empty());
    if (!out.onsets.empty() && pcOf(out.onsets.back().note) != 5) {
        std::cerr << "auto-key close ";
        dumpNotes(onsets(out));
    }
    CHECK(pcOf(out.onsets.back().note) == 5);
}

void testKeyEstimate() {
    using namespace phrasearp;
    float hist[12] {};
    addChordToHistogram(hist, 0, 0, (uint16_t) ((1u << 0) | (1u << 4) | (1u << 7)));
    const KeyEstimate est = estimateKey(hist, nullptr);
    CHECK(est.valid);
    CHECK(est.root == 0);
    CHECK(est.mode == ScaleType::Major);

    KeyEstimate previous = est;
    float nudged[12] {};
    for (int i = 0; i < 12; ++i)
        nudged[i] = hist[i];
    nudged[2] += 0.2f;
    const KeyEstimate held = estimateKey(nudged, &previous);
    CHECK(held.root == 0);
    CHECK(held.mode == ScaleType::Major);
}

} // namespace

int main() {
    testScaleDegree();
    testChords();
    testVoiceLeading();
    testInitPhrase();
    testQuestionAnswerAndClosure();
    testDeterminismAndChunks();
    testProbability();
    testLock();
    testSerializeAndMidi();
    testSynthAndSwap();
    testAutoKeyLaterBars();
    testKeyEstimate();
    if (gFails == 0) {
        std::cout << "ok\n";
        return 0;
    }
    std::cerr << gFails << " failed\n";
    return 1;
}
