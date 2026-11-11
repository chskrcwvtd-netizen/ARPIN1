#include "engine/Render.h"

#include "engine/Feel.h"
#include "engine/Harmony.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace phrasearp {
namespace {

int64_t msToTicks(double ms, double bpm) {
    if (bpm < 1.0)
        bpm = 120.0;
    return (int64_t) std::llround(ms * (double) kPpq * bpm / 60000.0);
}

char sectionLetter(int barInPhrase, PhraseTemplate templ, bool twoBar) {
    int idx = twoBar ? barInPhrase / 2 : barInPhrase;
    if (idx < 0)
        idx = 0;
    const char* letters = "AAAA";
    switch (templ) {
        case PhraseTemplate::AAAA: letters = "AAAA"; break;
        case PhraseTemplate::AAAB: letters = "AAAB"; break;
        case PhraseTemplate::AABA: letters = "AABA"; break;
        case PhraseTemplate::ABAC: letters = "ABAC"; break;
        case PhraseTemplate::AABB: letters = "AABB"; break;
    }
    return letters[idx % 4];
}

int variationLevel(char section, float variation) {
    if (section == 'A' || variation <= 0.f)
        return 0;
    int level = (int) std::lround(variation);
    if (section == 'C')
        level = std::min(100, level + 25);
    return level;
}

struct Intent {
    int64_t baseTick = 0;
    int stored = 0;
    int midi = 60;
    int velocity = 100;
    int gate = 90;
    int ratchet = 1;
    int probability = 100;
    int timingPct = 0;
    int cc1 = 0;
    int cc2 = 0;
    int octave = 0;
    int transpose = 0;
    StepType type = StepType::Note;
    bool strong = false;
    bool accent = false;
    int barInPhrase = 0;
    int phraseStep = 0;
    Chord chord;
};

template <typename T>
T readAt(const Lane<T>& lane, int64_t tick, int64_t origin, int64_t stepTicks, uint32_t seed, LaneId id) {
    if (lane.steps.empty() || stepTicks <= 0)
        return T{};
    int64_t pos = floorDiv(tick - origin, stepTicks);
    if (pos < 0)
        pos = 0;
    const int length = std::max(1, std::min(lane.length, (int) lane.steps.size()));
    int idx = mapLaneIndex(pos, length, lane.direction, seed, (uint32_t) id);
    idx = std::clamp(idx, 0, (int) lane.steps.size() - 1);
    return lane.steps[(size_t) idx];
}

int64_t laneOrigin(const AudioState& state, ResetMode reset, int64_t tick, int64_t phraseStart, int64_t barStart,
                   int64_t chordStart, int64_t ticksBar, const RenderConfig& cfg) {
    int64_t origin = 0;
    switch (reset) {
        case ResetMode::Never: origin = 0; break;
        case ResetMode::EveryNBars: {
            const int64_t period = ticksBar * std::max(1, state.resetEveryNBars);
            origin = floorDiv(tick, period) * period;
            break;
        }
        case ResetMode::Phrase: origin = phraseStart; break;
        case ResetMode::ChordChange: origin = chordStart; break;
    }
    if (cfg.retrigger == RetriggerMode::Bar)
        origin = barStart;
    else if (cfg.retrigger == RetriggerMode::Chord)
        origin = chordStart;
    else if (cfg.retrigger == RetriggerMode::Key && cfg.keyRetriggerTick >= 0)
        origin = cfg.keyRetriggerTick;
    return origin;
}

bool tickStrong(int64_t tick, int64_t ticksBar, int sigNum) {
    const int64_t pos = floorMod(tick, ticksBar);
    auto near = [&](int64_t t) { return std::llabs(pos - t) <= 1; };
    if (near(0))
        return true;
    if (sigNum >= 3) {
        const int64_t beat = ticksBar / std::max(1, sigNum);
        if (near(2 * beat))
            return true;
    }
    return false;
}

int cadencePitch(int current, const Chord& chord, int keyRoot, int low, int high) {
    auto nearestPc = [&](int pc) -> int {
        if ((chord.tones & (1u << pcOf(pc))) == 0)
            return -1;
        return nearestPitch(pc, current, low, high);
    };
    const int root = nearestPc(keyRoot);
    if (root >= 0 && std::abs(root - current) <= 7)
        return root;
    const int fifth = nearestPc(pcOf(keyRoot + 7));
    if (fifth >= 0 && std::abs(fifth - current) <= 7)
        return fifth;
    int best = std::clamp(current, low, high);
    int bestD = 1000;
    for (int n = low; n <= high; ++n) {
        if (!isChordTone(n, chord))
            continue;
        const int d = std::abs(n - current);
        if (d < bestD) {
            bestD = d;
            best = n;
        }
    }
    return best;
}

int nearestChordWithin(int around, int prefer, const Chord& chord, int semitones, int low, int high) {
    int best = -1;
    int bestD = 1000;
    for (int n = low; n <= high; ++n) {
        if (!isChordTone(n, chord))
            continue;
        if (std::abs(n - around) > semitones)
            continue;
        const int d = std::abs(n - prefer);
        if (d < bestD) {
            bestD = d;
            best = n;
        }
    }
    return best;
}

void resolveLine(std::vector<Intent>& notes, int low, int high) {
    auto sounding = [&](const Intent& n) {
        return n.type != StepType::Rest && n.type != StepType::Tie && n.probability > 0;
    };
    for (auto& n : notes) {
        if (!sounding(n) || !n.strong)
            continue;
        if (!isChordTone(n.midi, n.chord)) {
            int best = n.midi;
            int bestD = 1000;
            for (int m = low; m <= high; ++m) {
                if (!isChordTone(m, n.chord))
                    continue;
                const int d = std::abs(m - n.midi);
                if (d < bestD) {
                    bestD = d;
                    best = m;
                }
            }
            n.midi = best;
        }
    }
    for (size_t i = 0; i < notes.size(); ++i) {
        if (!sounding(notes[i]) || notes[i].strong)
            continue;
        if (isChordTone(notes[i].midi, notes[i].chord))
            continue;
        size_t j = i + 1;
        while (j < notes.size() && !sounding(notes[j]))
            ++j;
        if (j >= notes.size())
            continue;
        if (isChordTone(notes[j].midi, notes[j].chord) && std::abs(notes[j].midi - notes[i].midi) <= 2)
            continue;
        const int fixed = nearestChordWithin(notes[i].midi, notes[j].midi, notes[j].chord, 2, low, high);
        if (fixed >= 0)
            notes[j].midi = fixed;
        else {
            int best = notes[i].midi;
            int bestD = 1000;
            for (int m = low; m <= high; ++m) {
                if (!isChordTone(m, notes[i].chord))
                    continue;
                const int d = std::abs(m - notes[i].midi);
                if (d < bestD) {
                    bestD = d;
                    best = m;
                }
            }
            notes[i].midi = best;
        }
    }
}

float ratchetGain(int index, int count, RatchetCurve curve) {
    if (count <= 1)
        return 1.f;
    const float t = (float) index / (float) (count - 1);
    if (curve == RatchetCurve::Exponential)
        return std::pow(1.f - t, 1.35f);
    if (curve == RatchetCurve::Rising)
        return 0.55f + 0.45f * t;
    return 1.f - 0.5f * t;
}

struct Span {
    int64_t on = 0;
    int64_t off = 0;
    int note = 60;
    int velocity = 100;
    int channel = 0;
    int cc1 = -1;
    int cc2 = -1;
    int64_t ccTick = 0;
    bool hasCc = false;
};

} // namespace

std::vector<int> realizeLine(const std::vector<int>& indices,
                             const std::vector<Chord>& chords,
                             const Scale& scale,
                             PitchMode mode,
                             float color,
                             int maxJump,
                             int low,
                             int high) {
    std::vector<int> out;
    out.reserve(indices.size());
    int prevMidi = -1;
    int prevStored = 0;
    for (size_t i = 0; i < indices.size(); ++i) {
        const Chord& chord = chords[std::min(i, chords.size() - 1)];
        const int stored = indices[i];
        const bool strong = (i % 4) == 0;
        const int midi = realizePitch(mode, stored, chord, scale, color, prevMidi, prevStored, strong, maxJump, low, high,
                                      3, BoundaryMode::Fold, 0, 0, 0, (uint32_t) i);
        out.push_back(midi);
        prevMidi = midi;
        prevStored = stored;
    }
    return out;
}

RenderOutput renderMusic(const AudioState& state, const RenderConfig& config, int64_t startTick, int64_t endTick,
                         double bpm, int sigNum, int sigDen) {
    RenderOutput output;
    if (endTick <= startTick)
        return output;
    if (sigNum < 1)
        sigNum = 4;
    if (sigDen < 1)
        sigDen = 4;
    if (bpm < 1.0)
        bpm = 120.0;

    const int64_t barTicks = ticksPerBar(sigNum, sigDen);
    const int64_t beatTicks = barTicks / std::max(1, sigNum);
    const int64_t master = std::max<int64_t>(1, masterStepTicks(config.rate));
    const int phraseBars = std::max(1, config.phraseBars);
    const int64_t phraseTicks = barTicks * phraseBars;
    const int low = std::clamp(std::min(config.lowNote, config.highNote), 0, 127);
    const int high = std::clamp(std::max(config.lowNote, config.highNote), low, 127);
    const uint32_t seed = state.seed;
    const GrooveMap groove = grooveMap(config.groove, state.grooveTiming.data(), state.grooveVelocity.data());
    const Scale manual = makeScale(config.keyRoot, config.scaleType, state.customScale);

    std::vector<Scale> scaleByBar;
    auto scaleAt = [&](int bar) -> const Scale& {
        if (!config.autoKey)
            return manual;
        if (config.useLiveHist) {
            static Scale live;
            const KeyEstimate est = estimateKey(config.liveHist, nullptr);
            live = est.valid ? makeScale(est.root, est.mode, state.customScale) : manual;
            return live;
        }
        // Grow through the requested bar. The first call is often bar 0; stopping
        // there would make every later bar fall back to the manual key.
        if (bar < 0)
            bar = 0;
        if ((int) scaleByBar.size() <= bar) {
            KeyEstimate prev;
            const int limit = bar + 1;
            scaleByBar.resize((size_t) limit);
            for (int b = 0; b < limit; ++b) {
                float hist[12] {};
                for (int k = 0; k < 8; ++k) {
                    const int src = b - 7 + k;
                    if (src < 0 || !config.chordTrackEnabled)
                        continue;
                    const int loop = phraseBars;
                    const int idx = (int) floorMod((int64_t) src, loop);
                    if (idx >= 0 && idx < (int) state.chords.bars.size() && state.chords.bars[(size_t) idx].set) {
                        const Chord c = recognizeChord(state.chords.bars[(size_t) idx].notes, &manual);
                        addChordToHistogram(hist, c.root, c.bass, c.tones);
                    }
                }
                const KeyEstimate est = estimateKey(hist, prev.valid ? &prev : nullptr);
                prev = est;
                scaleByBar[(size_t) b] = est.valid ? makeScale(est.root, est.mode, state.customScale) : manual;
            }
        }
        if (bar >= 0 && bar < (int) scaleByBar.size())
            return scaleByBar[(size_t) bar];
        return manual;
    };

    auto chordAt = [&](int64_t tick, const Scale& scale) -> Chord {
        if (!config.heldNotes.empty())
            return recognizeChord(config.heldNotes, &scale);
        if (!config.chordTrackEnabled || state.chords.bars.empty())
            return {};
        const int bar = (int) floorDiv(tick, barTicks);
        const int loop = std::max(1, phraseBars);
        int idx = (int) floorMod((int64_t) bar, loop);
        for (int k = 0; k < loop; ++k) {
            int j = idx - k;
            if (j < 0)
                j += loop;
            if (j < (int) state.chords.bars.size() && state.chords.bars[(size_t) j].set)
                return recognizeChord(state.chords.bars[(size_t) j].notes, &scale);
        }
        return {};
    };

    const int64_t lookbehind = std::max(master * 16, msToTicks(4000.0, bpm));
    const int64_t lookahead = master * 4;
    int64_t ph0 = floorDiv(startTick - lookbehind, phraseTicks);
    int64_t ph1 = floorDiv(endTick + lookahead - 1, phraseTicks);
    if (ph0 < 0)
        ph0 = 0;
    if (ph1 < ph0)
        ph1 = ph0;

    const int channel = channelIndex(config.outputChannel);
    std::vector<Span> spans;

    for (int64_t ph = ph0; ph <= ph1; ++ph) {
        const int64_t phraseStart = ph * phraseTicks;
        const int64_t phraseEnd = phraseStart + phraseTicks;
        const int bar0 = (int) (phraseStart / barTicks);
        const Pattern& pattern = state.patterns[(size_t) std::clamp(
            state.chainLength > 0 ? state.chain[(size_t) ((ph >= 0 ? ph : 0) % state.chainLength)] % kPatternSlots
                                  : state.activeSlot,
            0, kPatternSlots - 1)];

        const int64_t typeTicks = std::max<int64_t>(1, scaledStepTicks(master, pattern.type.speed));
        std::vector<Intent> notes;
        const int64_t firstPos = floorDiv(phraseStart - 1, typeTicks);
        for (int64_t pos = std::max<int64_t>(0, firstPos);; ++pos) {
            const int64_t tick = pos * typeTicks;
            // Phrase-reset lanes restart at phraseStart, but the trigger grid stays on the
            // global master grid so strong beats stay on the project bar.
            if (tick >= phraseEnd)
                break;
            if (tick < phraseStart)
                continue;
            const int bar = (int) floorDiv(tick, barTicks);
            const int64_t barStart = (int64_t) bar * barTicks;
            const Scale& scale = scaleAt(std::max(0, bar));
            Chord chord = chordAt(tick, scale);
            int64_t chordStart = phraseStart;
            if (bar > 0) {
                const Chord prev = chordAt(tick - barTicks, scale);
                if (prev.root != chord.root || prev.quality != chord.quality || prev.bass != chord.bass)
                    chordStart = barStart;
            }

            Intent intent;
            intent.baseTick = tick;
            intent.strong = tickStrong(tick, barTicks, sigNum);
            intent.barInPhrase = std::max(0, bar - bar0);
            intent.phraseStep = (int) notes.size();
            intent.chord = chord;
            const int64_t pitchOrigin = laneOrigin(state, pattern.pitch.reset, tick, phraseStart, barStart, chordStart, barTicks, config);
            const int64_t typeOrigin = laneOrigin(state, pattern.type.reset, tick, phraseStart, barStart, chordStart, barTicks, config);
            auto originFor = [&](ResetMode reset) {
                return laneOrigin(state, reset, tick, phraseStart, barStart, chordStart, barTicks, config);
            };

            const int typeIdxPos = (int) floorDiv(tick - typeOrigin, typeTicks);
            (void) typeIdxPos;
            intent.type = (StepType) readAt(pattern.type, tick, typeOrigin, typeTicks, seed, LaneId::Type);
            intent.stored = (int) readAt(pattern.pitch, tick, pitchOrigin, scaledStepTicks(master, pattern.pitch.speed), seed, LaneId::Pitch);
            intent.octave = (int) readAt(pattern.octave, tick, originFor(pattern.octave.reset), scaledStepTicks(master, pattern.octave.speed), seed, LaneId::Octave);
            intent.transpose = (int) readAt(pattern.transpose, tick, originFor(pattern.transpose.reset), scaledStepTicks(master, pattern.transpose.speed), seed, LaneId::Transpose);
            intent.velocity = (int) readAt(pattern.velocity, tick, originFor(pattern.velocity.reset), scaledStepTicks(master, pattern.velocity.speed), seed, LaneId::Velocity);
            intent.gate = (int) readAt(pattern.gate, tick, originFor(pattern.gate.reset), scaledStepTicks(master, pattern.gate.speed), seed, LaneId::Gate);
            intent.ratchet = (int) readAt(pattern.ratchet, tick, originFor(pattern.ratchet.reset), scaledStepTicks(master, pattern.ratchet.speed), seed, LaneId::Ratchet);
            intent.probability = (int) readAt(pattern.probability, tick, originFor(pattern.probability.reset), scaledStepTicks(master, pattern.probability.speed), seed, LaneId::Probability);
            intent.timingPct = (int) readAt(pattern.timing, tick, originFor(pattern.timing.reset), scaledStepTicks(master, pattern.timing.speed), seed, LaneId::Timing);
            intent.cc1 = (int) readAt(pattern.cc1, tick, originFor(pattern.cc1.reset), scaledStepTicks(master, pattern.cc1.speed), seed, LaneId::CC1);
            intent.cc2 = (int) readAt(pattern.cc2, tick, originFor(pattern.cc2.reset), scaledStepTicks(master, pattern.cc2.speed), seed, LaneId::CC2);

            const int64_t velOrigin = originFor(pattern.velocity.reset);
            const int64_t velTicks = scaledStepTicks(master, pattern.velocity.speed);
            int64_t velPos = floorDiv(tick - velOrigin, velTicks);
            if (velPos < 0)
                velPos = 0;
            const int velLen = std::max(1, std::min(pattern.velocity.length, (int) pattern.velocity.steps.size()));
            const int velIdx = mapLaneIndex(velPos, velLen, pattern.velocity.direction, seed, (uint32_t) LaneId::Velocity);
            if (velIdx >= 0 && velIdx < (int) pattern.accent.size())
                intent.accent = pattern.accent[(size_t) velIdx] != 0;

            const char section = sectionLetter(intent.barInPhrase, config.phraseTemplate, config.twoBarUnit);
            const int level = variationLevel(section, config.variation);
            if (level >= 1 && intent.type != StepType::Rest) {
                // Op 1 is applied after the bar is collected.
            }
            (void) level;
            (void) pitchOrigin;
            notes.push_back(intent);
        }

        // Variation ops, per bar, on a copy of the stored indexes before realisation.
        for (int bar = 0; bar < phraseBars; ++bar) {
            const char section = sectionLetter(bar, config.phraseTemplate, config.twoBarUnit);
            const int level = variationLevel(section, config.variation);
            if (level <= 0)
                continue;
            std::vector<int> idx;
            for (int i = 0; i < (int) notes.size(); ++i)
                if (notes[(size_t) i].barInPhrase == bar && notes[(size_t) i].type != StepType::Rest
                    && notes[(size_t) i].type != StepType::Tie)
                    idx.push_back(i);
            if (idx.empty())
                continue;
            const uint32_t salt = stepHash(seed, (uint32_t) ph, (uint32_t) bar, (uint32_t) section, 0x5A11u);
            if (level >= 1) {
                const int nChange = std::min((int) idx.size(), level >= 10 ? 2 : 1);
                for (int c = 0; c < nChange; ++c) {
                    Intent& n = notes[(size_t) idx[(size_t) (idx.size() - 1 - c)]];
                    const int delta = (salt & 1u) ? 2 : -2;
                    n.stored = std::clamp(n.stored + delta, -12, 12);
                }
            }
            if (level >= 20) {
                for (size_t k = 0; k < idx.size(); ++k) {
                    Intent& n = notes[(size_t) idx[k]];
                    if (n.strong)
                        continue;
                    const uint32_t h = stepHash(salt, (uint32_t) k, 2, 0, 0);
                    if ((h % 100u) < (uint32_t) std::clamp(level - 10, 0, 40))
                        n.type = StepType::Rest;
                }
            }
            if (level >= 40) {
                const int64_t lastQuarter = phraseStart + (int64_t) (bar + 1) * barTicks - beatTicks;
                for (int id : idx) {
                    if (notes[(size_t) id].baseTick >= lastQuarter && !notes[(size_t) id].strong)
                        notes[(size_t) id].ratchet = std::min(8, notes[(size_t) id].ratchet + 1);
                }
            }
            if (level >= 55) {
                const int dir = (salt & 2u) ? 1 : -1;
                for (size_t k = idx.size() / 2; k < idx.size(); ++k)
                    notes[(size_t) idx[k]].octave = std::clamp(notes[(size_t) idx[k]].octave + dir, -2, 2);
            }
            if (level >= 70) {
                for (int id : idx)
                    notes[(size_t) id].stored = std::clamp(-notes[(size_t) id].stored, -12, 12);
            }
            if (level >= 85) {
                const int dir = (salt & 4u) ? 1 : -1;
                for (int id : idx)
                    notes[(size_t) id].transpose = std::clamp(notes[(size_t) id].transpose + dir, -7, 7);
            }
        }

        int prevMidi = -1;
        int prevStored = 0;
        if (ph > 0) {
            // Connect from a canonical root so the first phrase and later phrases
            // do not depend on walking the whole song, except one anchor.
            prevMidi = -1;
        }
        for (size_t i = 0; i < notes.size(); ++i) {
            Intent& n = notes[i];
            if (n.type == StepType::Rest) {
                n.midi = prevMidi;
                continue;
            }
            if (n.type == StepType::Tie) {
                n.midi = prevMidi;
                continue;
            }
            const int bar = (int) floorDiv(n.baseTick, barTicks);
            const Scale& scale = scaleAt(std::max(0, bar));
            const double pos = phraseTicks > 0 ? (double) (n.baseTick - phraseStart) / (double) phraseTicks : 0.0;
            const int contour = contourSteps(config.contour, pos, state.contourCustom.data());
            uint32_t salt = seed;
            if (pattern.pitchMode == PitchMode::AsPlayed && !config.heldNotes.empty()) {
                const int pick = positiveMod(n.phraseStep, (int) config.heldNotes.size());
                n.midi = config.heldNotes[(size_t) pick];
                n.midi += n.octave * 12;
                if (n.transpose)
                    n.midi = transposeScale(scale, n.midi, n.transpose);
                n.midi = fitRange(n.midi, low, high, config.boundary);
            } else {
                int stored = n.stored;
                if (pattern.pitchMode != PitchMode::Index && pattern.pitchMode != PitchMode::ScaleDegree)
                    stored = n.phraseStep;
                n.midi = realizePitch(pattern.pitchMode, stored, n.chord, scale, config.color, prevMidi, prevStored, n.strong,
                                      config.maxJump, low, high, config.octaves, config.boundary, n.octave, n.transpose,
                                      contour, stepHash(salt, (uint32_t) ph, (uint32_t) i, 0x91u, config.liveCycle));
            }
            if (n.midi >= 0) {
                prevMidi = n.midi;
                prevStored = n.stored;
            }
        }

        resolveLine(notes, low, high);

        if (config.questionAnswer) {
            const int64_t half = phraseStart + phraseTicks / 2;
            int halfIdx = -1;
            for (int i = (int) notes.size() - 1; i >= 0; --i) {
                if (notes[(size_t) i].baseTick >= half)
                    continue;
                if (notes[(size_t) i].type == StepType::Rest || notes[(size_t) i].type == StepType::Tie)
                    continue;
                halfIdx = i;
                break;
            }
            if (halfIdx >= 0) {
                Intent& n = notes[(size_t) halfIdx];
                const Scale& scale = scaleAt(std::max(0, (int) floorDiv(n.baseTick, barTicks)));
                if (pcOf(n.midi) == pcOf(scale.root)) {
                    int thirdPc = -1;
                    switch (n.chord.quality) {
                        case ChordQuality::Minor:
                        case ChordQuality::Min7:
                        case ChordQuality::Min9:
                        case ChordQuality::Minor6:
                        case ChordQuality::Min7b5:
                        case ChordQuality::Dim:
                        case ChordQuality::Dim7:
                            thirdPc = pcOf(n.chord.root + 3);
                            break;
                        default:
                            thirdPc = pcOf(n.chord.root + 4);
                            break;
                    }
                    if ((n.chord.tones & (1u << thirdPc)) != 0) {
                        const int third = nearestPitch(thirdPc, n.midi, low, high);
                        if (std::abs(third - n.midi) <= std::max(4, config.maxJump))
                            n.midi = third;
                    }
                }
            }
        }

        int finalIdx = -1;
        for (int i = (int) notes.size() - 1; i >= 0; --i) {
            if (notes[(size_t) i].type == StepType::Rest || notes[(size_t) i].type == StepType::Tie)
                continue;
            finalIdx = i;
            break;
        }
        if (finalIdx < 0 && !notes.empty() && config.chordTrackEnabled) {
            notes.back().type = StepType::Note;
            finalIdx = (int) notes.size() - 1;
        }
        if (finalIdx >= 0) {
            Intent& n = notes[(size_t) finalIdx];
            const Scale& scale = scaleAt(std::max(0, (int) floorDiv(n.baseTick, barTicks)));
            n.midi = cadencePitch(n.midi, n.chord, scale.root, low, high);
            n.midi = fitRange(n.midi, low, high, config.boundary);
            n.gate = std::clamp(n.gate * 2, 5, 400);
            if (finalIdx > 0) {
                Intent& prev = notes[(size_t) (finalIdx - 1)];
                if (prev.type != StepType::Rest && prev.type != StepType::Tie && !isChordTone(prev.midi, prev.chord)
                    && std::abs(prev.midi - n.midi) > 2) {
                    const int approach = nearestChordWithin(n.midi, prev.midi, n.chord, 2, low, high);
                    if (approach >= 0)
                        prev.midi = approach;
                    else {
                        const int step = n.midi >= prev.midi ? -1 : 1;
                        int neighbor = n.midi + step;
                        if (!scale.contains(neighbor))
                            neighbor = n.midi + step * 2;
                        prev.midi = fitRange(neighbor, low, high, config.boundary);
                    }
                }
            }
        }

        // Probability, dynamics, feel, then spans.
        int previousCc1 = notes.empty() ? 0 : notes.front().cc1;
        int previousCc2 = notes.empty() ? 0 : notes.front().cc2;
        for (size_t i = 0; i < notes.size(); ++i) {
            Intent& n = notes[i];
            const uint32_t cycle = config.humanizeLive ? config.liveCycle + (uint32_t) ph : 0u;
            if (n.probability < 100) {
                const uint32_t h = stepHash(seed, (uint32_t) n.baseTick, (uint32_t) LaneId::Probability, cycle, 0xA11CEu);
                if (n.probability <= 0 || (int) (h % 1000u) >= n.probability * 10)
                    n.type = StepType::Rest;
            }
            const double pos = phraseTicks > 0 ? (double) (n.baseTick - phraseStart) / (double) phraseTicks : 0.0;
            int vel = n.velocity;
            if (n.accent)
                vel += config.accent;
            vel = (int) std::lround((double) vel * (double) dynamicsGain(config.dynamics, pos, state.dynamicsCustom.data()));
            if (config.bakeFeel)
                vel += groove.velocity[(int) std::min<int64_t>(15, floorMod(n.baseTick, barTicks) * 16 / std::max<int64_t>(1, barTicks))];
            if (config.bakeFeel && config.humanizeVel > 0.f) {
                const uint32_t h = stepHash(seed, (uint32_t) n.baseTick, 0x51u, cycle, 3u);
                const int span = std::max(0, (int) std::lround(config.humanizeVel));
                if (span > 0)
                    vel += (int) (h % (uint32_t) (span * 2 + 1)) - span;
            }
            n.velocity = std::clamp(vel, 1, 127);

            int64_t shift = 0;
            if (config.bakeFeel) {
                shift += typeTicks * std::clamp(n.timingPct, -50, 50) / 100;
                shift += swingDelayTicks(n.baseTick, config.swing, config.swingEighth, beatTicks);
                const int slot = (int) std::min<int64_t>(15, floorMod(n.baseTick, barTicks) * 16 / std::max<int64_t>(1, barTicks));
                const int64_t sixteenth = std::max<int64_t>(1, beatTicks / 4);
                shift += sixteenth * groove.timing[slot] / 100;
                if (config.humanizeMs > 0.f) {
                    const uint32_t h = stepHash(seed, (uint32_t) n.baseTick, 0x71u, cycle, 9u);
                    const int span = std::max(0, (int) std::lround(config.humanizeMs));
                    if (span > 0) {
                        const int ms = (int) (h % (uint32_t) (span * 2 + 1)) - span;
                        shift += msToTicks((double) ms, bpm);
                    }
                }
            }
            const int64_t limit = typeTicks / 2;
            if (shift < -limit)
                shift = -limit;
            if (shift > typeTicks)
                shift = typeTicks;
            const int64_t onTick = n.baseTick + shift;

            int64_t duration = typeTicks * std::clamp(n.gate, 5, 400) / 100;
            if (config.noteLength == NoteLengthMode::FixedMs)
                duration = std::max<int64_t>(1, msToTicks((double) config.fixedMs, bpm));
            if (config.bakeFeel && config.humanizeGate > 0.f && duration > 0) {
                const uint32_t h = stepHash(seed, (uint32_t) n.baseTick, 0x64u, cycle, 5u);
                const int span = std::max(0, (int) std::lround(config.humanizeGate));
                if (span > 0) {
                    const int pct = (int) (h % (uint32_t) (span * 2 + 1)) - span;
                    duration += duration * pct / 100;
                }
            }
            if (duration < 1)
                duration = 1;

            const bool sounding = n.type != StepType::Rest && n.type != StepType::Tie && n.midi >= 0 && n.chord.tones != 0;
            if (n.type == StepType::Tie && !spans.empty()) {
                spans.back().off += duration;
            } else if (sounding) {
                std::vector<int> pitches;
                if (n.type == StepType::Chord || n.type == StepType::Strum) {
                    Voicing v = voiceLead(nullptr, n.chord, low, high, config.maxJump);
                    for (int p = 0; p < v.count; ++p)
                        pitches.push_back(fitRange(v.notes[(size_t) p], low, high, config.boundary));
                    if (pitches.empty())
                        pitches.push_back(n.midi);
                } else {
                    pitches.push_back(std::clamp(n.midi, 0, 127));
                }
                const int ratchet = std::clamp(n.ratchet, 1, 8);
                const int64_t sub = std::max<int64_t>(1, typeTicks / ratchet);
                int dir = config.strumDir == StrumDirection::Up ? 1 : -1;
                if (config.strumDir == StrumDirection::Alternate)
                    dir = (n.phraseStep & 1) ? -1 : 1;
                if (dir < 0)
                    std::sort(pitches.begin(), pitches.end(), std::greater<int>());
                else
                    std::sort(pitches.begin(), pitches.end());
                const int64_t spread = (n.type == StepType::Chord || n.type == StepType::Strum)
                                           ? msToTicks((double) std::clamp(config.strumMs, 0, 100), bpm)
                                           : 0;
                for (int r = 0; r < ratchet; ++r) {
                    const float g = ratchetGain(r, ratchet, config.ratchetCurve);
                    for (size_t p = 0; p < pitches.size(); ++p) {
                        int64_t strum = 0;
                        if (pitches.size() > 1 && spread > 0)
                            strum = spread * (int64_t) p / (int64_t) (pitches.size() - 1);
                        int pv = (int) std::lround((double) n.velocity * (double) g);
                        pv += (int) ((int) p - (int) pitches.size() / 2) * config.strumTilt / 4;
                        Span span;
                        span.on = onTick + r * sub + strum;
                        int64_t dur = ratchet == 1 ? duration : std::min(duration, sub * 9 / 10);
                        if (dur < 1)
                            dur = 1;
                        span.off = span.on + dur;
                        if (span.off <= span.on)
                            span.off = span.on + 1;
                        span.note = pitches[p];
                        span.velocity = std::clamp(pv, 1, 127);
                        span.channel = channel;
                        if (r == 0 && p == 0) {
                            span.hasCc = true;
                            span.ccTick = onTick;
                            span.cc1 = n.cc1;
                            span.cc2 = n.cc2;
                        }
                        spans.push_back(span);
                    }
                }
            } else if (n.type == StepType::Rest) {
                Span cc;
                cc.hasCc = true;
                cc.ccTick = onTick;
                cc.cc1 = n.cc1;
                cc.cc2 = n.cc2;
                cc.on = onTick;
                cc.off = onTick;
                cc.note = -1;
                spans.push_back(cc);
            }
            previousCc1 = n.cc1;
            previousCc2 = n.cc2;
            (void) previousCc1;
            (void) previousCc2;
        }

        if (config.noteLength == NoteLengthMode::Legato) {
            std::vector<int> order;
            for (int i = 0; i < (int) spans.size(); ++i)
                if (spans[(size_t) i].note >= 0 && spans[(size_t) i].on >= phraseStart && spans[(size_t) i].on < phraseEnd)
                    order.push_back(i);
            std::sort(order.begin(), order.end(), [&](int a, int b) { return spans[(size_t) a].on < spans[(size_t) b].on; });
            for (size_t i = 0; i + 1 < order.size(); ++i) {
                Span& a = spans[(size_t) order[i]];
                const Span& b = spans[(size_t) order[i + 1]];
                if (b.on > a.on)
                    a.off = b.on;
            }
        }
    }

    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) {
        if (a.on != b.on)
            return a.on < b.on;
        return a.note < b.note;
    });

    for (const Span& span : spans) {
        if (span.note >= 0 && span.on >= startTick && span.on < endTick) {
            ScheduledEvent on;
            on.tick = span.on;
            on.type = ScheduledEvent::Type::On;
            on.channel = span.channel;
            on.note = span.note;
            on.value = span.velocity;
            output.events.push_back(on);
            NoteSpan ns;
            ns.onTick = span.on;
            ns.offTick = span.off;
            ns.note = span.note;
            ns.velocity = span.velocity;
            ns.channel = span.channel;
            output.onsets.push_back(ns);
        }
        if (span.note >= 0 && span.off >= startTick && span.off < endTick) {
            ScheduledEvent off;
            off.tick = span.off;
            off.type = ScheduledEvent::Type::Off;
            off.channel = span.channel;
            off.note = span.note;
            off.value = 0;
            output.events.push_back(off);
        }
        if (span.hasCc && span.ccTick >= startTick && span.ccTick < endTick) {
            if (config.cc1Number >= 0 && config.cc1Number <= 127) {
                ScheduledEvent cc;
                cc.tick = span.ccTick;
                cc.type = ScheduledEvent::Type::CC;
                cc.channel = channel;
                cc.note = config.cc1Number;
                cc.value = std::clamp(span.cc1, 0, 127);
                output.events.push_back(cc);
            }
            if (config.cc2Number >= 0 && config.cc2Number <= 127 && config.cc2Number != config.cc1Number) {
                ScheduledEvent cc;
                cc.tick = span.ccTick;
                cc.type = ScheduledEvent::Type::CC;
                cc.channel = channel;
                cc.note = config.cc2Number;
                cc.value = std::clamp(span.cc2, 0, 127);
                output.events.push_back(cc);
            }
        }
    }

    if (config.ccSmooth) {
        // A second, lighter pass is intentionally not recursive: smoothing inserts
        // intermediate CC events between the ones already emitted.
        std::vector<ScheduledEvent> extra;
        const ScheduledEvent* prev1 = nullptr;
        const ScheduledEvent* prev2 = nullptr;
        for (const auto& e : output.events) {
            if (e.type != ScheduledEvent::Type::CC)
                continue;
            const ScheduledEvent* prev = e.note == config.cc1Number ? prev1 : prev2;
            if (prev != nullptr && e.tick > prev->tick + 4) {
                for (int s = 1; s < 4; ++s) {
                    ScheduledEvent mid = e;
                    mid.tick = prev->tick + (e.tick - prev->tick) * s / 4;
                    mid.value = prev->value + (e.value - prev->value) * s / 4;
                    if (mid.tick >= startTick && mid.tick < endTick && mid.tick != e.tick)
                        extra.push_back(mid);
                }
            }
            if (e.note == config.cc1Number)
                prev1 = &e;
            else
                prev2 = &e;
        }
        output.events.insert(output.events.end(), extra.begin(), extra.end());
    }

    std::sort(output.events.begin(), output.events.end(), eventLess);
    return output;
}

} // namespace phrasearp
