#include "plugin/PluginProcessor.h"

#include "engine/Generator.h"
#include "engine/Prng.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

int phraseBarCount(int index) {
    static const int bars[] = {1, 2, 4, 8, 16};
    return bars[std::clamp(index, 0, 4)];
}

int autoEvolveEvery(int index) {
    if (index <= 0)
        return 0;
    return 1 << std::clamp(index - 1, 0, 3);
}

} // namespace

PhraseArpAudioProcessor::PhraseArpAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", phrasearp::createParameterLayout()) {
    uiState = phrasearp::makeInitState();
    playing = uiState;
    history.push(uiState);
    liveNotes.reserve(128);
    worker = std::thread([this] { workerLoop(); });
}

PhraseArpAudioProcessor::~PhraseArpAudioProcessor() {
    workerStop.store(true);
    if (worker.joinable())
        worker.join();
    swap.drain();
}

void PhraseArpAudioProcessor::prepareToPlay(double rate, int) {
    sampleRate = rate > 0.0 ? rate : 44100.0;
    synth.setSampleRate(sampleRate);
    synth.allOff();
    liveNotes.clear();
    internalClock = false;
    haveHostTick = false;
    tickFraction = 0.0;
}

void PhraseArpAudioProcessor::releaseResources() {
    synth.allOff();
    liveNotes.clear();
}

bool PhraseArpAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled()
           && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

float PhraseArpAudioProcessor::raw(const char* id) const {
    if (auto* v = apvts.getRawParameterValue(id))
        return v->load();
    return 0.f;
}

int PhraseArpAudioProcessor::choice(const char* id) const {
    return (int) std::lround(raw(id));
}

bool PhraseArpAudioProcessor::flag(const char* id) const {
    return raw(id) >= 0.5f;
}

float PhraseArpAudioProcessor::shaped(float base, float lo, float hi, phrasearp::MacroTarget target,
                                      const phrasearp::AudioState& state) const {
    float value = base;
    const float span = hi - lo;
    for (int i = 0; i < 8; ++i) {
        if ((phrasearp::MacroTarget) state.macroTarget[(size_t) i] != target)
            continue;
        const float depth = std::clamp(state.macroDepth[(size_t) i], 0.f, 1.f);
        const float knob = raw(phrasearp::ids::macro(i));
        value += (knob - 0.5f) * 2.f * depth * span;
    }
    return std::clamp(value, lo, hi);
}

phrasearp::RenderConfig PhraseArpAudioProcessor::makeConfig(const phrasearp::AudioState& state,
                                                            const std::vector<int>& held, double bpm) const {
    phrasearp::RenderConfig cfg = phrasearp::defaultRenderConfig();
    cfg.color = shaped(raw(phrasearp::ids::color), 0.f, 100.f, phrasearp::MacroTarget::Color, state);
    cfg.variation = shaped(raw(phrasearp::ids::variation), 0.f, 100.f, phrasearp::MacroTarget::Variation, state);
    cfg.phraseBars = phraseBarCount(choice(phrasearp::ids::phraseBars));
    cfg.phraseTemplate = (phrasearp::PhraseTemplate) std::clamp(choice(phrasearp::ids::phraseTemplate), 0, 4);
    cfg.twoBarUnit = flag(phrasearp::ids::twoBar);
    cfg.questionAnswer = flag(phrasearp::ids::questionAnswer);
    cfg.maxJump = (int) std::lround(raw(phrasearp::ids::maxJump));
    cfg.accent = (int) std::lround(raw(phrasearp::ids::accent));
    cfg.swing = shaped(raw(phrasearp::ids::swing), 50.f, 75.f, phrasearp::MacroTarget::Swing, state);
    cfg.swingEighth = flag(phrasearp::ids::swingEighth);
    cfg.groove = std::clamp(choice(phrasearp::ids::groove), 0, phrasearp::grooveCount() - 1);
    cfg.humanizeMs = raw(phrasearp::ids::humanizeMs);
    cfg.humanizeVel = raw(phrasearp::ids::humanizeVel);
    cfg.humanizeGate = raw(phrasearp::ids::humanizeGate);
    cfg.humanizeLive = flag(phrasearp::ids::humanizeLive);
    cfg.strumMs = (int) std::lround(shaped(raw(phrasearp::ids::strumMs), 0.f, 100.f, phrasearp::MacroTarget::Strum, state));
    cfg.strumDir = (phrasearp::StrumDirection) std::clamp(choice(phrasearp::ids::strumDir), 0, 2);
    cfg.strumTilt = (int) std::lround(raw(phrasearp::ids::strumTilt));
    cfg.noteLength = (phrasearp::NoteLengthMode) std::clamp(choice(phrasearp::ids::noteLength), 0, 2);
    cfg.fixedMs = (int) std::lround(raw(phrasearp::ids::fixedMs));
    cfg.outputChannel = std::clamp((int) std::lround(raw(phrasearp::ids::outputChannel)), 1, 16);
    cfg.cc1Number = std::clamp((int) std::lround(raw(phrasearp::ids::cc1)), 0, 127);
    cfg.cc2Number = std::clamp((int) std::lround(raw(phrasearp::ids::cc2)), 0, 127);
    cfg.ccSmooth = flag(phrasearp::ids::ccSmooth);
    cfg.keyRoot = std::clamp(choice(phrasearp::ids::keyRoot), 0, 11);
    cfg.scaleType = (phrasearp::ScaleType) std::clamp(choice(phrasearp::ids::scale), 0, (int) phrasearp::ScaleType::Count - 1);
    cfg.autoKey = flag(phrasearp::ids::autoKey);
    cfg.lowNote = std::clamp((int) std::lround(raw(phrasearp::ids::lowNote)), 0, 127);
    cfg.highNote = std::clamp((int) std::lround(raw(phrasearp::ids::highNote)), 0, 127);
    cfg.octaves = std::clamp((int) std::lround(raw(phrasearp::ids::octaves)), 1, 4);
    cfg.boundary = (phrasearp::BoundaryMode) std::clamp(choice(phrasearp::ids::boundary), 0, 1);
    cfg.retrigger = (phrasearp::RetriggerMode) std::clamp(choice(phrasearp::ids::retrigger), 0, 3);
    cfg.contour = (phrasearp::ContourShape) std::clamp(choice(phrasearp::ids::contour), 0, 4);
    cfg.dynamics = (phrasearp::DynamicsShape) std::clamp(choice(phrasearp::ids::dynamics), 0, 4);
    cfg.chordTrackEnabled = flag(phrasearp::ids::chordTrack);
    cfg.rate = (phrasearp::StepRate) std::clamp(choice(phrasearp::ids::rate), 0, (int) phrasearp::StepRate::Count - 1);
    cfg.ratchetCurve = (phrasearp::RatchetCurve) std::clamp(choice(phrasearp::ids::ratchetCurve), 0, 2);
    cfg.bakeFeel = true;
    cfg.heldNotes = held;
    (void) bpm;
    return cfg;
}

phrasearp::GenerateSettings PhraseArpAudioProcessor::makeGenerateSettings(const phrasearp::AudioState& state) const {
    auto cfg = makeConfig(state, {}, lastBpm.load());
    phrasearp::GenerateSettings g = phrasearp::defaultGenerateSettings(cfg);
    g.density = shaped(raw(phrasearp::ids::density), 0.f, 100.f, phrasearp::MacroTarget::Density, state);
    g.complexity = shaped(raw(phrasearp::ids::complexity), 0.f, 100.f, phrasearp::MacroTarget::Complexity, state);
    g.motion = shaped(raw(phrasearp::ids::motion), 0.f, 100.f, phrasearp::MacroTarget::Motion, state);
    g.color = cfg.color;
    g.repetition = shaped(raw(phrasearp::ids::repetition), 0.f, 100.f, phrasearp::MacroTarget::Repetition, state);
    g.range = shaped(raw(phrasearp::ids::range), 0.f, 100.f, phrasearp::MacroTarget::Range, state);
    g.variation = cfg.variation;
    g.euclidean = flag(phrasearp::ids::euclidean);
    g.stepsPerBar = phrasearp::stepsPerBar(cfg.rate, lastSigNum.load(), lastSigDen.load());
    g.phraseBars = cfg.phraseBars;
    g.seed = state.seed;
    g.maxJump = cfg.maxJump;
    g.lowNote = cfg.lowNote;
    g.highNote = cfg.highNote;
    g.keyRoot = cfg.keyRoot;
    g.scaleType = cfg.scaleType;
    return g;
}

void PhraseArpAudioProcessor::publish(const phrasearp::AudioState& state) {
    swap.publish(state);
    epoch.fetch_add(1, std::memory_order_release);
}

void PhraseArpAudioProcessor::editState(const std::function<void(phrasearp::AudioState&)>& edit, bool recordHistory) {
    std::lock_guard<std::mutex> lock(stateMutex);
    edit(uiState);
    uiState.activeSlot = std::clamp(uiState.activeSlot, 0, phrasearp::kPatternSlots - 1);
    if (recordHistory)
        history.push(uiState);
    publish(uiState);
}

phrasearp::AudioState PhraseArpAudioProcessor::copyState() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return uiState;
}

void PhraseArpAudioProcessor::setMessage(juce::String text) {
    std::lock_guard<std::mutex> lock(messageMutex);
    status = std::move(text);
}

juce::String PhraseArpAudioProcessor::message() const {
    std::lock_guard<std::mutex> lock(messageMutex);
    return status;
}

void PhraseArpAudioProcessor::requestGenerate() { generateRequest.store(true); }
void PhraseArpAudioProcessor::requestEvolve() { evolveRequest.store(true); }

void PhraseArpAudioProcessor::requestNewSeed() {
    editState([this](phrasearp::AudioState& state) {
        const uint32_t n = seedCounter.fetch_add(1);
        state.seed = (uint32_t) phrasearp::Prng::stream(state.seed ^ 0x51A7u, n, 0x5EEDu).nextU32();
    }, true);
}

void PhraseArpAudioProcessor::undo() {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (const phrasearp::AudioState* prev = history.undo()) {
        uiState = *prev;
        publish(uiState);
    }
}

void PhraseArpAudioProcessor::redo() {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (const phrasearp::AudioState* next = history.redo()) {
        uiState = *next;
        publish(uiState);
    }
}

bool PhraseArpAudioProcessor::canUndo() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return history.canUndo();
}

bool PhraseArpAudioProcessor::canRedo() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return history.canRedo();
}

void PhraseArpAudioProcessor::selectSlot(int slot) {
    slot = std::clamp(slot, 0, phrasearp::kPatternSlots - 1);
    keySwitchSlot.store(-1, std::memory_order_release);
    if (auto* param = apvts.getParameter(phrasearp::ids::slot))
        param->setValueNotifyingHost(param->convertTo0to1((float) slot));
    editState([slot](phrasearp::AudioState& state) { state.activeSlot = slot; }, true);
}

void PhraseArpAudioProcessor::storeActiveIntoSlot(int slot) {
    slot = std::clamp(slot, 0, phrasearp::kPatternSlots - 1);
    editState([slot](phrasearp::AudioState& state) {
        const int from = std::clamp(state.activeSlot, 0, phrasearp::kPatternSlots - 1);
        state.patterns[(size_t) slot] = state.patterns[(size_t) from];
        if (state.presetName.find(" *") == std::string::npos)
            state.presetName += " *";
    }, true);
}

void PhraseArpAudioProcessor::laneOp(phrasearp::LaneId lane, int op) {
    editState([lane, op](phrasearp::AudioState& state) {
        phrasearp::applyLaneOp(state.patterns[(size_t) std::clamp(state.activeSlot, 0, phrasearp::kPatternSlots - 1)], lane, op, state.seed);
    }, true);
}

void PhraseArpAudioProcessor::copyLaneToClipboard(phrasearp::LaneId lane) {
    std::lock_guard<std::mutex> lock(stateMutex);
    phrasearp::copyLane(phrasearp::activePattern(uiState), lane, clipboard);
}

void PhraseArpAudioProcessor::pasteClipboard() {
    editState([this](phrasearp::AudioState& state) {
        phrasearp::pasteLane(state.patterns[(size_t) std::clamp(state.activeSlot, 0, phrasearp::kPatternSlots - 1)], clipboard);
    }, true);
}

void PhraseArpAudioProcessor::workerLoop() {
    while (!workerStop.load()) {
        if (generateRequest.exchange(false))
            runGenerate();
        if (evolveRequest.exchange(false))
            runEvolve();
        swap.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void PhraseArpAudioProcessor::loadHeld(std::vector<int>& dest) const {
    dest.clear();
    for (;;) {
        const uint32_t a = heldSeq.load(std::memory_order_acquire);
        if (a & 1u)
            continue;
        const int n = heldSnapCount;
        int temp[16];
        for (int i = 0; i < n && i < 16; ++i)
            temp[i] = heldSnap[i];
        const uint32_t b = heldSeq.load(std::memory_order_acquire);
        if (a == b) {
            dest.assign(temp, temp + std::clamp(n, 0, 16));
            return;
        }
    }
}

void PhraseArpAudioProcessor::runGenerate() {
    phrasearp::AudioState state;
    phrasearp::GenerateSettings settings;
    std::vector<int> held;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state = uiState;
        settings = makeGenerateSettings(state);
    }
    loadHeld(held);
    const phrasearp::Scale scale = phrasearp::makeScale(settings.keyRoot, settings.scaleType, state.customScale);
    phrasearp::Chord chord;
    if (!held.empty())
        chord = phrasearp::recognizeChord(held, &scale);
    else if (!state.chords.bars.empty() && state.chords.bars.front().set)
        chord = phrasearp::recognizeChord(state.chords.bars.front().notes, &scale);
    else
        chord = phrasearp::recognizeChord({60, 64, 67}, &scale);
    const int slot = std::clamp(state.activeSlot, 0, phrasearp::kPatternSlots - 1);
    phrasearp::Pattern generated = phrasearp::generatePattern(state.patterns[(size_t) slot], settings, chord);
    std::lock_guard<std::mutex> lock(stateMutex);
    uiState.patterns[(size_t) slot] = generated;
    history.push(uiState);
    publish(uiState);
}

void PhraseArpAudioProcessor::runEvolve() {
    phrasearp::AudioState state;
    phrasearp::GenerateSettings settings;
    int amount = 20;
    std::vector<int> held;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state = uiState;
        settings = makeGenerateSettings(state);
        amount = std::clamp((int) std::lround(raw(phrasearp::ids::evolveAmount)), 1, 100);
    }
    loadHeld(held);
    const phrasearp::Scale scale = phrasearp::makeScale(settings.keyRoot, settings.scaleType, state.customScale);
    phrasearp::Chord chord;
    if (!held.empty())
        chord = phrasearp::recognizeChord(held, &scale);
    else if (!state.chords.bars.empty() && state.chords.bars.front().set)
        chord = phrasearp::recognizeChord(state.chords.bars.front().notes, &scale);
    else
        chord = phrasearp::recognizeChord({60, 64, 67}, &scale);
    const int slot = std::clamp(state.activeSlot, 0, phrasearp::kPatternSlots - 1);
    phrasearp::Pattern evolved = phrasearp::evolvePattern(state.patterns[(size_t) slot], settings, amount, chord);
    std::lock_guard<std::mutex> lock(stateMutex);
    uiState.patterns[(size_t) slot] = evolved;
    history.push(uiState);
    publish(uiState);
}

void PhraseArpAudioProcessor::panic(juce::MidiBuffer& midi, int sample) {
    for (const auto& note : liveNotes)
        midi.addEvent(juce::MidiMessage::noteOff(note.channel + 1, note.note), sample);
    midi.addEvent(juce::MidiMessage::allNotesOff(1), sample);
    liveNotes.clear();
    synth.allOff();
}

void PhraseArpAudioProcessor::capture(const CapEv& event) {
    const uint32_t w = capWrite.load(std::memory_order_relaxed);
    capRing[w % kCap] = event;
    capWrite.store(w + 1, std::memory_order_release);
}

std::vector<int> PhraseArpAudioProcessor::chordFromKeys() const {
    std::vector<int> notes;
    for (int n = 0; n < 128; ++n)
        if (keyDown[(size_t) n] || sustained[(size_t) n])
            notes.push_back(n);
    if (notes.empty() && flag(phrasearp::ids::latch))
        return latchedChord;
    return notes;
}

void PhraseArpAudioProcessor::injectMidi(int note, int velocity, bool on) {
    note = std::clamp(note, 0, 127);
    const int tail = injectTail.load(std::memory_order_relaxed);
    const int next = (tail + 1) % 128;
    if (next == injectHead.load(std::memory_order_acquire))
        return;
    injectQueue[tail] = {note, std::clamp(velocity, 0, 127), on ? 1 : 0};
    injectTail.store(next, std::memory_order_release);
}

void PhraseArpAudioProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) {
    juce::AudioBuffer<float> temp(buffer.getNumChannels(), buffer.getNumSamples());
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            temp.setSample(channel, i, (float) buffer.getSample(channel, i));
    processBlock(temp, midi);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(channel, i, (double) temp.getSample(channel, i));
}

void PhraseArpAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    juce::MidiBuffer input;
    input.swapWith(midi);

    if (captureState.load(std::memory_order_acquire) == 1) {
        const uint32_t w = capWrite.load(std::memory_order_acquire);
        const uint32_t n = std::min<uint32_t>((uint32_t) kCap, w);
        capExportCount = (int) n;
        for (uint32_t i = 0; i < n; ++i)
            capExport[i] = capRing[(w - n + i) % kCap];
        capExportBegin = capExportCount > 0 ? capExport[0].tick : 0;
        capExportBpm = lastBpm.load();
        capExportNum = lastSigNum.load();
        capExportDen = lastSigDen.load();
        captureState.store(2, std::memory_order_release);
    }

    double bpm = 120.0;
    int sigNum = 4;
    int sigDen = 4;
    bool hostPlaying = false;
    bool havePpq = false;
    double ppq = 0.0;
    if (auto* head = getPlayHead()) {
        if (auto pos = head->getPosition()) {
            if (auto b = pos->getBpm())
                bpm = std::clamp(*b, 20.0, 400.0);
            if (auto sig = pos->getTimeSignature()) {
                sigNum = std::max(1, sig->numerator);
                sigDen = std::max(1, sig->denominator);
            }
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) {
                ppq = *p;
                havePpq = true;
            }
        }
    }
    lastBpm.store((float) bpm);
    lastSigNum.store(sigNum);
    lastSigDen.store(sigDen);

    const bool audition = flag(phrasearp::ids::audition);
    const double ticksPerSample = (bpm / 60.0) * (double) phrasearp::kPpq / sampleRate;
    int64_t t0 = 0;
    int64_t t1 = 0;
    bool reset = false;
    if (hostPlaying && havePpq) {
        if (internalClock || !haveHostTick)
            reset = true;
        internalClock = false;
        t0 = (int64_t) std::floor(std::max(0.0, ppq) * (double) phrasearp::kPpq);
        t1 = (int64_t) std::floor(std::max(0.0, ppq + ticksPerSample * numSamples) * (double) phrasearp::kPpq);
        if (t1 < t0)
            t1 = t0;
        if (haveHostTick && std::llabs(t0 - expectedTick) > 2)
            reset = true;
        haveHostTick = true;
        expectedTick = t1;
    } else if (audition) {
        if (!internalClock)
            reset = true;
        internalClock = true;
        haveHostTick = false;
        t0 = internalTick;
        tickFraction += ticksPerSample * numSamples;
        const int64_t adv = (int64_t) tickFraction;
        tickFraction -= (double) adv;
        t1 = t0 + std::max<int64_t>(0, adv);
        internalTick = t1;
    } else {
        if (internalClock || haveHostTick)
            reset = true;
        internalClock = false;
        haveHostTick = false;
        t0 = internalTick;
        t1 = t0;
    }
    if (reset)
        panic(midi, 0);

    auto cfgPreview = makeConfig(playing, liveHeld, bpm);
    const int64_t stepTicks = std::max<int64_t>(1, phrasearp::masterStepTicks(cfgPreview.rate));
    const int64_t barTicks = std::max<int64_t>(1, phrasearp::ticksPerBar(sigNum, sigDen));
    const int64_t phraseTicks = barTicks * std::max(1, cfgPreview.phraseBars);

    struct Slice {
        int sample = 0;
        int64_t tick = 0;
    };
    std::vector<Slice> cuts;
    cuts.push_back({0, t0});
    cuts.push_back({numSamples, t1});
    auto addCut = [&](int64_t tick) {
        if (t1 <= t0 || tick <= t0 || tick >= t1)
            return;
        const double u = (double) (tick - t0) / (double) (t1 - t0);
        const int sample = std::clamp((int) std::llround(u * numSamples), 0, numSamples);
        cuts.push_back({sample, tick});
    };
    if (swap.hasPending() && t1 > t0) {
        const int64_t boundary = (t0 % stepTicks == 0) ? t0 : (t0 / stepTicks + 1) * stepTicks;
        if (boundary > t0 && boundary < t1)
            addCut(boundary);
    }
    if (pendingChordTick > t0 && pendingChordTick < t1)
        addCut(pendingChordTick);

    const bool latch = flag(phrasearp::ids::latch);
    const bool thru = flag(phrasearp::ids::thru);
    const bool keyOn = playing.keySwitchEnabled;
    auto handleNote = [&](int sample, int64_t tick, int note, int velocity, bool on, int channel) {
        if (note < 0 || note > 127)
            return;
        if (keyOn && note >= playing.keySwitchLow && note <= playing.keySwitchHigh) {
            if (on)
                keySwitchSlot.store(std::clamp(note - playing.keySwitchLow, 0, phrasearp::kPatternSlots - 1), std::memory_order_release);
            return;
        }
        if (on)
            keyDown[(size_t) note] = 1;
        else if (pedal)
            sustained[(size_t) note] = 1;
        else
            keyDown[(size_t) note] = 0;
        if (thru && channel >= 0)
            midi.addEvent(on ? juce::MidiMessage::noteOn(channel, note, (juce::uint8) std::clamp(velocity, 1, 127))
                             : juce::MidiMessage::noteOff(channel, note),
                          sample);
        auto notes = chordFromKeys();
        if (latch && !notes.empty())
            latchedChord = notes;
        const int64_t grid = (tick / stepTicks + 1) * stepTicks;
        const int64_t dist = grid - tick;
        if (dist > 0 && dist <= phrasearp::kPpq / 8 && notes != liveHeld) {
            pendingHeld = std::move(notes);
            pendingChordTick = grid;
            addCut(grid);
        } else {
            liveHeld = std::move(notes);
            pendingChordTick = -1;
            addCut(tick);
        }
    };

    for (const auto meta : input) {
        const auto msg = meta.getMessage();
        const int sample = std::clamp(meta.samplePosition, 0, std::max(0, numSamples - 1));
        int64_t tick = t0;
        if (t1 > t0)
            tick = t0 + (t1 - t0) * sample / numSamples;
        if (msg.isNoteOn())
            handleNote(sample, tick, msg.getNoteNumber(), msg.getVelocity(), true, msg.getChannel());
        else if (msg.isNoteOff())
            handleNote(sample, tick, msg.getNoteNumber(), 0, false, msg.getChannel());
        else if (msg.isController() && msg.getControllerNumber() == 64) {
            const bool down = msg.getControllerValue() >= 64;
            if (pedal && !down) {
                for (int n = 0; n < 128; ++n)
                    sustained[(size_t) n] = 0;
            }
            pedal = down;
            auto notes = chordFromKeys();
            if (latch && !notes.empty())
                latchedChord = notes;
            if (pendingChordTick < 0)
                liveHeld = std::move(notes);
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            keyDown.fill(0);
            sustained.fill(0);
            liveHeld.clear();
            panic(midi, sample);
        } else if (thru && msg.isController()) {
            midi.addEvent(msg, sample);
        }
    }
    // handleNote with note -1 still runs the chord update via the controller branch's call.
    // A note of -1 returns immediately, so pedal-up must refresh the chord here.
    if (!pedal) {
        auto notes = chordFromKeys();
        if (latch && !notes.empty())
            latchedChord = notes;
        if (notes != liveHeld && pendingChordTick < 0)
            liveHeld = notes;
    }

    for (;;) {
        const int head = injectHead.load(std::memory_order_relaxed);
        if (head == injectTail.load(std::memory_order_acquire))
            break;
        const Inj ev = injectQueue[head];
        injectHead.store((head + 1) % 128, std::memory_order_release);
        handleNote(0, t0, ev.note, ev.velocity, ev.on != 0, 1);
    }

    if (pendingChordTick >= 0 && pendingChordTick <= t0) {
        liveHeld = pendingHeld;
        pendingChordTick = -1;
    }
    const int quant = choice(phrasearp::ids::slotQuantize);
    if (quant == 1)
        addCut((t0 / barTicks + 1) * barTicks);
    else if (quant == 2)
        addCut((t0 / phraseTicks + 1) * phraseTicks);

    heldSeq.fetch_add(1, std::memory_order_release);
    heldSnapCount = std::min(16, (int) liveHeld.size());
    for (int i = 0; i < heldSnapCount; ++i)
        heldSnap[i] = liveHeld[(size_t) i];
    heldSeq.fetch_add(1, std::memory_order_release);

    std::sort(cuts.begin(), cuts.end(), [](const Slice& a, const Slice& b) {
        if (a.sample != b.sample)
            return a.sample < b.sample;
        return a.tick < b.tick;
    });
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](const Slice& a, const Slice& b) {
                   return a.sample == b.sample;
               }),
               cuts.end());

    const int paramSlot = std::clamp(choice(phrasearp::ids::slot), 0, phrasearp::kPatternSlots - 1);
    const int keyed = keySwitchSlot.load(std::memory_order_acquire);
    const int targetSlot = keyed >= 0 ? keyed : paramSlot;
    const bool previewOn = flag(phrasearp::ids::preview);
    const auto timbre = (phrasearp::Timbre) std::clamp(choice(phrasearp::ids::timbre), 0, 2);
    const float decay = raw(phrasearp::ids::previewDecay);
    const float volume = raw(phrasearp::ids::previewVolume);
    std::vector<phrasearp::SynthEvent> previewEvents;

    auto finishOffs = [&](int64_t from, int64_t to, int sampleBase, int sampleSpan, int64_t tickBase, int64_t tickSpan) {
        for (size_t i = 0; i < liveNotes.size();) {
            if (liveNotes[i].offTick >= from && (tickSpan <= 0 || liveNotes[i].offTick < to)) {
                int sample = sampleBase;
                if (tickSpan > 0)
                    sample = sampleBase + (int) ((liveNotes[i].offTick - tickBase) * sampleSpan / tickSpan);
                sample = std::clamp(sample, 0, std::max(0, numSamples - 1));
                midi.addEvent(juce::MidiMessage::noteOff(liveNotes[i].channel + 1, liveNotes[i].note), sample);
                previewEvents.push_back({sample, liveNotes[i].note, 0, false});
                CapEv ev;
                ev.tick = liveNotes[i].offTick;
                ev.kind = 0;
                ev.channel = (uint8_t) liveNotes[i].channel;
                ev.note = (uint8_t) liveNotes[i].note;
                capture(ev);
                liveNotes[i] = liveNotes.back();
                liveNotes.pop_back();
            } else {
                ++i;
            }
        }
    };

    for (size_t c = 0; c + 1 < cuts.size(); ++c) {
        const int sampleA = cuts[c].sample;
        const int sampleB = cuts[c + 1].sample;
        const int64_t tickA = cuts[c].tick;
        const int64_t tickB = cuts[c + 1].tick;
        if (pendingChordTick >= 0 && tickA >= pendingChordTick) {
            liveHeld = pendingHeld;
            pendingChordTick = -1;
        }
        const bool onBoundary = stepTicks > 0 && tickA % stepTicks == 0;
        if (onBoundary && swap.hasPending()) {
            const int keep = playing.activeSlot;
            swap.takeInto(playing);
            if (quant != 0)
                playing.activeSlot = keep;
        }
        const int64_t barA = barTicks > 0 ? tickA / barTicks : 0;
        const int64_t barB = barTicks > 0 && tickB > 0 ? (tickB - 1) / barTicks : barA;
        const int phraseB = phraseTicks > 0 && tickB > 0 ? (int) ((tickB - 1) / phraseTicks) : (phraseTicks > 0 ? (int) (tickA / phraseTicks) : 0);
        (void) barA;
        (void) barB;
        (void) phraseB;
        if (quant == 0 || (quant == 1 && tickA % barTicks == 0) || (quant == 2 && tickA % phraseTicks == 0))
            playing.activeSlot = targetSlot;
        heardSlotAtomic.store(playing.activeSlot, std::memory_order_release);

        if (tickB > tickA) {
            auto cfg = makeConfig(playing, liveHeld, bpm);
            if (flag(phrasearp::ids::autoKey) && !liveHeld.empty()) {
                cfg.useLiveHist = true;
                for (int n : liveHeld)
                    cfg.liveHist[phrasearp::pcOf(n)] += 1.f;
            }
            const auto rendered = phrasearp::renderMusic(playing, cfg, tickA, tickB, bpm, sigNum, sigDen);
            const int spanSamples = std::max(1, sampleB - sampleA);
            const int64_t spanTicks = tickB - tickA;
            for (const auto& onset : rendered.onsets) {
                int sample = sampleA;
                if (spanTicks > 0)
                    sample = sampleA + (int) ((onset.onTick - tickA) * spanSamples / spanTicks);
                sample = std::clamp(sample, 0, std::max(0, numSamples - 1));
                for (size_t i = 0; i < liveNotes.size(); ++i) {
                    if (liveNotes[i].note == onset.note && liveNotes[i].channel == onset.channel) {
                        midi.addEvent(juce::MidiMessage::noteOff(onset.channel + 1, onset.note), sample);
                        previewEvents.push_back({sample, onset.note, 0, false});
                        liveNotes[i] = liveNotes.back();
                        liveNotes.pop_back();
                        break;
                    }
                }
                midi.addEvent(juce::MidiMessage::noteOn(onset.channel + 1, onset.note, (juce::uint8) std::clamp(onset.velocity, 1, 127)), sample);
                previewEvents.push_back({sample, onset.note, onset.velocity, true});
                liveNotes.push_back({onset.note, onset.channel, onset.offTick});
                CapEv ev;
                ev.tick = onset.onTick;
                ev.kind = 1;
                ev.channel = (uint8_t) onset.channel;
                ev.note = (uint8_t) onset.note;
                ev.value = (uint8_t) std::clamp(onset.velocity, 1, 127);
                capture(ev);
            }
            for (const auto& event : rendered.events) {
                if (event.type != phrasearp::ScheduledEvent::Type::CC)
                    continue;
                int sample = sampleA;
                if (spanTicks > 0)
                    sample = sampleA + (int) ((event.tick - tickA) * spanSamples / spanTicks);
                sample = std::clamp(sample, 0, std::max(0, numSamples - 1));
                midi.addEvent(juce::MidiMessage::controllerEvent(event.channel + 1, event.note, std::clamp(event.value, 0, 127)), sample);
                CapEv ev;
                ev.tick = event.tick;
                ev.kind = 2;
                ev.channel = (uint8_t) event.channel;
                ev.note = (uint8_t) event.note;
                ev.value = (uint8_t) std::clamp(event.value, 0, 127);
                capture(ev);
            }
            finishOffs(tickA, tickB, sampleA, spanSamples, tickA, spanTicks);
        }
        const int phraseIndex = phraseTicks > 0 ? (int) (tickA / phraseTicks) : 0;
        const int every = autoEvolveEvery(choice(phrasearp::ids::autoEvolve));
        if (every > 0 && lastPhraseIndex >= 0 && phraseIndex > lastPhraseIndex && phraseIndex % every == 0)
            evolveRequest.store(true);
        if (tickA != t0 || c == 0)
            lastPhraseIndex = phraseIndex;
    }
    if (!cuts.empty())
        heardTickAtomic.store(cuts.back().tick, std::memory_order_release);

    std::sort(previewEvents.begin(), previewEvents.end(), [](const phrasearp::SynthEvent& a, const phrasearp::SynthEvent& b) {
        if (a.sample != b.sample)
            return a.sample < b.sample;
        return a.on < b.on;
    });
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
    if (previewOn)
        synth.renderEvents(left, right, numSamples, previewEvents, timbre, decay, volume, true);
    else
        synth.allOff();
}

juce::File PhraseArpAudioProcessor::renderMidiFile(const juce::File& dest, int phrases, bool bakeFeel, bool includeChords) const {
    phrasearp::AudioState state = copyState();
    auto cfg = makeConfig(state, {}, lastBpm.load());
    cfg.bakeFeel = bakeFeel;
    if (!bakeFeel) {
        cfg.swing = 50.f;
        cfg.groove = 0;
        cfg.humanizeMs = 0.f;
        cfg.humanizeVel = 0.f;
        cfg.humanizeGate = 0.f;
        for (auto& pattern : state.patterns)
            for (auto& step : pattern.timing.steps)
                step = 0;
    }
    const int sigN = lastSigNum.load();
    const int sigD = lastSigDen.load();
    const int64_t bars = (int64_t) std::max(1, cfg.phraseBars) * std::max(1, phrases);
    const int64_t end = phrasearp::ticksPerBar(sigN, sigD) * bars;
    const auto rendered = phrasearp::renderMusic(state, cfg, 0, end, lastBpm.load(), sigN, sigD);
    std::vector<phrasearp::SmfNote> notes;
    for (const auto& onset : rendered.onsets) {
        phrasearp::SmfNote note;
        note.tick = (int) onset.onTick;
        note.duration = (int) std::max<int64_t>(1, onset.offTick - onset.onTick);
        note.note = onset.note;
        note.velocity = onset.velocity;
        note.channel = onset.channel;
        notes.push_back(note);
    }
    std::vector<phrasearp::SmfNote> chords;
    if (includeChords) {
        const int64_t barTicks = phrasearp::ticksPerBar(sigN, sigD);
        const int loop = std::max(1, cfg.phraseBars);
        for (int64_t bar = 0; bar < bars; ++bar) {
            const int idx = (int) (bar % loop);
            if (idx < 0 || idx >= (int) state.chords.bars.size() || !state.chords.bars[(size_t) idx].set)
                continue;
            for (int pitch : state.chords.bars[(size_t) idx].notes) {
                phrasearp::SmfNote note;
                note.tick = (int) (bar * barTicks);
                note.duration = (int) barTicks;
                note.note = pitch;
                note.velocity = 80;
                note.channel = 1;
                chords.push_back(note);
            }
        }
    }
    const auto bytes = phrasearp::writeSmf(notes, lastBpm.load(), sigN, sigD,
                                           state.presetName + " " + phrasearp::toHex8(state.seed),
                                           includeChords ? &chords : nullptr);
    dest.deleteFile();
    if (!phrasearp::writeSmfFile(dest.getFullPathName().toStdString(), bytes))
        return {};
    return dest;
}

void PhraseArpAudioProcessor::armCapture() {
    if (captureState.load() == 2)
        return;
    captureState.store(1, std::memory_order_release);
}

juce::File PhraseArpAudioProcessor::writeArmedCapture(const juce::File& dest) {
    if (captureState.load() != 2) {
        const uint32_t w = capWrite.load(std::memory_order_acquire);
        const uint32_t n = std::min<uint32_t>((uint32_t) kCap, w);
        capExportCount = (int) n;
        for (uint32_t i = 0; i < n; ++i)
            capExport[i] = capRing[(w - n + i) % (uint32_t) kCap];
        capExportBegin = capExportCount > 0 ? capExport[0].tick : 0;
        capExportBpm = lastBpm.load();
        capExportNum = lastSigNum.load();
        capExportDen = lastSigDen.load();
        captureState.store(0);
    }
    std::vector<phrasearp::SmfNote> notes;
    struct Open { int tick; int vel; int channel; bool used; };
    std::vector<Open> open[128];
    const int count = capExportCount;
    int64_t begin = capExportBegin;
    for (int i = 0; i < count; ++i)
        begin = std::min(begin, capExport[i].tick);
    const int64_t window = phrasearp::ticksPerBar(capExportNum, capExportDen) * 64;
    for (int i = 0; i < count; ++i) {
        const CapEv& ev = capExport[i];
        if (ev.tick < capExport[count - 1].tick - window && count > 0)
            continue;
        if (ev.kind == 1)
            open[ev.note].push_back({(int) (ev.tick - begin), ev.value, ev.channel, false});
        else if (ev.kind == 0 && !open[ev.note].empty()) {
            Open started = open[ev.note].back();
            open[ev.note].pop_back();
            phrasearp::SmfNote note;
            note.tick = std::max(0, started.tick);
            note.duration = std::max(1, (int) (ev.tick - begin) - note.tick);
            note.note = ev.note;
            note.velocity = std::max(1, (int) started.vel);
            note.channel = started.channel;
            notes.push_back(note);
        }
    }
    const int endTick = count > 0 ? (int) (capExport[count - 1].tick - begin) : 0;
    for (int pitch = 0; pitch < 128; ++pitch) {
        for (const auto& started : open[pitch]) {
            phrasearp::SmfNote note;
            note.tick = std::max(0, started.tick);
            note.duration = std::max(1, endTick - note.tick);
            note.note = pitch;
            note.velocity = std::max(1, (int) started.vel);
            note.channel = started.channel;
            notes.push_back(note);
        }
    }
    std::sort(notes.begin(), notes.end(), [](const phrasearp::SmfNote& a, const phrasearp::SmfNote& b) { return a.tick < b.tick; });
    int shift = notes.empty() ? 0 : notes.front().tick;
    for (auto& note : notes)
        note.tick -= shift;
    const auto bytes = phrasearp::writeSmf(notes, capExportBpm, capExportNum, capExportDen, "Capture");
    captureState.store(0);
    dest.deleteFile();
    if (!phrasearp::writeSmfFile(dest.getFullPathName().toStdString(), bytes))
        return {};
    return dest;
}

void PhraseArpAudioProcessor::importMidi(const juce::File& file, phrasearp::ImportMode mode) {
    const int root = std::clamp(choice(phrasearp::ids::keyRoot), 0, 11);
    const auto scaleType = (phrasearp::ScaleType) std::clamp(choice(phrasearp::ids::scale), 0, (int) phrasearp::ScaleType::Count - 1);
    phrasearp::AudioState state = copyState();
    const phrasearp::Scale scale = phrasearp::makeScale(root, scaleType, state.customScale);
    const phrasearp::ImportResult result = phrasearp::importMidiFile(file.getFullPathName().toStdString(), mode, scale);
    if (!result.ok) {
        setMessage(result.message.empty() ? "Not a MIDI file" : juce::String(result.message));
        return;
    }
    editState([&](phrasearp::AudioState& dest) {
        const int slot = std::clamp(dest.activeSlot, 0, phrasearp::kPatternSlots - 1);
        dest.patterns[(size_t) slot] = result.pattern;
        bool any = false;
        for (const auto& bar : result.chords.bars)
            if (bar.set)
                any = true;
        if (any) {
            if (dest.chords.bars.size() < result.chords.bars.size())
                dest.chords.bars.resize(result.chords.bars.size());
            for (size_t i = 0; i < result.chords.bars.size(); ++i)
                if (result.chords.bars[i].set)
                    dest.chords.bars[i] = result.chords.bars[i];
        }
    }, true);
    if (auto* param = apvts.getParameter(phrasearp::ids::rate))
        param->setValueNotifyingHost(param->convertTo0to1((float) result.rate));
    setMessage("Imported " + file.getFileName());
}

void PhraseArpAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::XmlElement root("PhraseArp");
    if (auto params = apvts.copyState().createXml())
        root.addChildElement(params.release());
    root.setAttribute("exportPhrases", exportPhrases);
    root.setAttribute("bakeFeel", exportBakeFeel ? 1 : 0);
    root.setAttribute("exportChords", exportChords ? 1 : 0);
    phrasearp::AudioState state;
    phrasearp::History hist;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state = uiState;
        hist = history;
    }
    auto* engine = root.createNewChildElement("engine");
    engine->addTextElement(phrasearp::serializeState(state));
    auto* historyXml = root.createNewChildElement("history");
    historyXml->setAttribute("index", hist.index);
    for (const auto& snap : hist.states) {
        auto* node = historyXml->createNewChildElement("snap");
        node->addTextElement(phrasearp::serializeState(snap));
    }
    copyXmlToBinary(root, dest);
}

void PhraseArpAudioProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> root(getXmlFromBinary(data, size));
    if (root == nullptr || !root->hasTagName("PhraseArp"))
        return;
    if (auto* params = root->getChildByName("PARAMS"))
        apvts.replaceState(juce::ValueTree::fromXml(*params));
    exportPhrases = std::clamp(root->getIntAttribute("exportPhrases", 1), 1, 8);
    exportBakeFeel = root->getIntAttribute("bakeFeel", 1) != 0;
    exportChords = root->getIntAttribute("exportChords", 0) != 0;
    phrasearp::AudioState state = phrasearp::makeInitState();
    if (auto* engine = root->getChildByName("engine"))
        phrasearp::deserializeState(engine->getAllSubText().toStdString(), state);
    phrasearp::History hist;
    if (auto* historyXml = root->getChildByName("history")) {
        for (auto* snap = historyXml->getFirstChildElement(); snap != nullptr; snap = snap->getNextElement()) {
            phrasearp::AudioState item;
            if (phrasearp::deserializeState(snap->getAllSubText().toStdString(), item))
                hist.states.push_back(std::move(item));
        }
        hist.index = std::clamp(historyXml->getIntAttribute("index", (int) hist.states.size() - 1),
                                hist.states.empty() ? -1 : 0,
                                hist.states.empty() ? -1 : (int) hist.states.size() - 1);
        if (hist.index >= 0 && hist.index < (int) hist.states.size())
            state = hist.states[(size_t) hist.index];
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        uiState = state;
        history = std::move(hist);
        if (history.states.empty())
            history.push(uiState);
        publish(uiState);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PhraseArpAudioProcessor();
}
