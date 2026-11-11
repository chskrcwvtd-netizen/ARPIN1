#include "engine/Generator.h"

#include "engine/Harmony.h"
#include "engine/Prng.h"

#include <algorithm>
#include <cmath>

namespace phrasearp {
namespace {

float metricWeight(int step, int stepsPerBar) {
    if (stepsPerBar < 1)
        stepsPerBar = 1;
    const int s = positiveMod(step, stepsPerBar);
    if (s == 0)
        return 1.f;
    if (stepsPerBar % 2 == 0 && s == stepsPerBar / 2)
        return 0.9f;
    const int quarter = stepsPerBar / 4;
    if (quarter > 0 && s % quarter == 0)
        return 0.7f;
    const int eighth = stepsPerBar / 8;
    if (eighth > 0 && s % eighth == 0)
        return 0.42f;
    return 0.2f;
}

bool strongIndex(int step, int stepsPerBar) {
    if (stepsPerBar < 1)
        return step == 0;
    const int s = positiveMod(step, stepsPerBar);
    if (s == 0)
        return true;
    return stepsPerBar % 2 == 0 && s == stepsPerBar / 2;
}

std::vector<bool> euclid(int pulses, int steps) {
    std::vector<bool> out((size_t) std::max(1, steps), false);
    if (steps <= 0 || pulses <= 0)
        return out;
    pulses = std::clamp(pulses, 1, steps);
    int bucket = 0;
    for (int i = 0; i < steps; ++i) {
        bucket += pulses;
        if (bucket >= steps) {
            bucket -= steps;
            out[(size_t) i] = true;
        }
    }
    int rotate = 0;
    for (int i = 0; i < steps; ++i) {
        if (out[(size_t) i]) {
            rotate = i;
            break;
        }
    }
    if (rotate != 0) {
        std::rotate(out.begin(), out.begin() + rotate, out.end());
    }
    return out;
}

int nearestChordIndex(int index, const std::vector<int>& chordIdx) {
    if (chordIdx.empty())
        return 0;
    int best = chordIdx.front();
    int bestD = std::abs(index - best);
    for (int c : chordIdx) {
        const int d = std::abs(index - c);
        if (d < bestD) {
            bestD = d;
            best = c;
        }
    }
    return best;
}

template <typename T>
void overlay(Lane<T>& fresh, const Lane<T>& src) {
    if (src.locked) {
        fresh = src;
        return;
    }
    const int n = (int) std::min(fresh.steps.size(), src.steps.size());
    for (int i = 0; i < n; ++i) {
        if (i < (int) src.locks.size() && src.locks[(size_t) i])
            fresh.steps[(size_t) i] = src.steps[(size_t) i];
    }
    fresh.locks = src.locks;
    if (fresh.locks.size() != fresh.steps.size())
        fresh.locks.resize(fresh.steps.size(), 0);
    fresh.locked = false;
    fresh.speed = src.speed;
    fresh.direction = src.direction;
    fresh.reset = src.reset;
}

Pattern generateRaw(const Pattern& shape, const GenerateSettings& settings, const Chord& reference) {
    Pattern out = shape;
    const Scale scale = makeScale(settings.keyRoot, settings.scaleType, defaultMask(settings.scaleType));
    const int stepsPerBar = std::max(1, settings.stepsPerBar);
    const float density = std::clamp(settings.density, 0.f, 100.f);
    const float complexity = std::clamp(settings.complexity, 0.f, 100.f);
    const float motion = std::clamp(settings.motion, 0.f, 100.f);
    const float repetition = std::clamp(settings.repetition, 0.f, 100.f);
    const float rangeAmt = std::clamp(settings.range, 0.f, 100.f);
    const int span = 2 + (int) (rangeAmt / 100.f * 10.f);

    auto lengthOf = [](const auto& lane) { return std::clamp(lane.length > 0 ? lane.length : (int) lane.steps.size(), 1, kMaxSteps); };

    const int typeLen = lengthOf(shape.type);
    preparePattern(out, typeLen);
    // preparePattern resets metadata; put the shape's lane transport back.
    auto keepMeta = [](auto& dst, const auto& src) {
        dst.speed = src.speed;
        dst.direction = src.direction;
        dst.reset = src.reset;
        dst.locked = src.locked;
        dst.length = std::clamp(src.length > 0 ? src.length : dst.length, 1, kMaxSteps);
    };
    keepMeta(out.pitch, shape.pitch);
    keepMeta(out.octave, shape.octave);
    keepMeta(out.transpose, shape.transpose);
    keepMeta(out.type, shape.type);
    keepMeta(out.velocity, shape.velocity);
    keepMeta(out.gate, shape.gate);
    keepMeta(out.ratchet, shape.ratchet);
    keepMeta(out.probability, shape.probability);
    keepMeta(out.timing, shape.timing);
    keepMeta(out.cc1, shape.cc1);
    keepMeta(out.cc2, shape.cc2);

    auto ensure = [](auto& lane, auto fill) {
        const int n = std::clamp(lane.length, 1, kMaxSteps);
        lane.length = n;
        lane.steps.assign((size_t) n, fill);
        lane.locks.assign((size_t) n, 0);
    };
    ensure(out.pitch, (int16_t) 0);
    ensure(out.octave, (int8_t) 0);
    ensure(out.transpose, (int8_t) 0);
    ensure(out.type, (uint8_t) StepType::Rest);
    ensure(out.velocity, (uint8_t) 90);
    ensure(out.gate, (uint16_t) 90);
    ensure(out.ratchet, (uint8_t) 1);
    ensure(out.probability, (uint8_t) 100);
    ensure(out.timing, (int8_t) 0);
    ensure(out.cc1, (uint8_t) 70);
    ensure(out.cc2, (uint8_t) 20);
    out.accent.assign((size_t) out.velocity.length, 0);
    out.pitchMode = PitchMode::Index;

    // Chord-tone indices on the reference chord at this color, relative to anchor 0.
    std::vector<int> chordIdx;
    {
        Voicing v = voiceLead(nullptr, reference, settings.lowNote, settings.highNote, settings.maxJump);
        for (int i = -span; i <= span; ++i) {
            const int midi = indexIdeal(reference, scale, settings.color, i, v.rootPitch, settings.lowNote, settings.highNote, 3);
            if (isChordTone(midi, reference))
                chordIdx.push_back(i);
        }
        if (chordIdx.empty())
            chordIdx.push_back(0);
    }

    // Type lane — its own stream.
    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Type);
        const auto pulses = euclid(std::max(1, (int) std::lround(density / 100.0 * typeLen)), typeLen);
        for (int i = 0; i < typeLen; ++i) {
            const float u = rng.unit();
            const bool strong = strongIndex(i, stepsPerBar);
            bool on = false;
            if (settings.euclidean)
                on = pulses[(size_t) (i % (int) pulses.size())];
            else {
                const float w = metricWeight(i, stepsPerBar);
                const float c = complexity / 100.f;
                const float mixed = w * (1.f - c) + (1.f - w) * c;
                const float thresh = 1.f - density / 100.f;
                on = (mixed + (u - 0.5f) * 0.12f * c) >= thresh;
            }
            if (strong && density >= 25.f)
                on = true;
            if (i == 0 && density > 0.f)
                on = true;
            out.type.steps[(size_t) i] = (uint8_t) (on ? StepType::Note : StepType::Rest);
        }
        if (density > 0.f)
            out.type.steps.back() = (uint8_t) StepType::Note;
    }

    // Pitch: motif cell, then repetition / sequence / mirror. Own stream.
    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Pitch);
        int cellLen = 2;
        if (repetition < 45.f)
            cellLen = 4;
        else if (repetition < 75.f)
            cellLen = 3;
        cellLen = std::clamp(cellLen, 2, 4);
        std::vector<int> cell((size_t) cellLen, 0);
        int cursor = 0;
        int prevDelta = 0;
        const int maxStep = 1 + (int) (motion / 100.f * 4.f);
        for (int k = 0; k < cellLen; ++k) {
            int delta = rng.nextInt(maxStep * 2 + 1) - maxStep;
            if (motion < 55.f && rng.unit() > motion / 140.f)
                delta = std::clamp(delta, -1, 1);
            if (std::abs(prevDelta) > 3)
                delta = prevDelta > 0 ? -1 : 1;
            if (k == 0)
                delta = 0;
            cursor = std::clamp(cursor + delta, -span, span);
            prevDelta = delta;
            cell[(size_t) k] = cursor;
        }
        const int seqDir = rng.nextInt(2) == 0 ? 1 : -1;
        const int n = (int) out.pitch.steps.size();
        for (int i = 0; i < n; ++i) {
            const int rep = cellLen > 0 ? i / cellLen : 0;
            const int pos = cellLen > 0 ? i % cellLen : 0;
            int idx = cell[(size_t) pos];
            if (repetition >= 70.f) {
                // literal repeat
            } else if (repetition >= 40.f) {
                idx += (rep % 2 == 0 ? 0 : seqDir);
            } else if (rep % 2 == 1) {
                idx = -idx;
            }
            if (strongIndex(i, stepsPerBar) || (StepType) out.type.steps[(size_t) std::min(i, typeLen - 1)] == StepType::Rest)
                idx = nearestChordIndex(idx, chordIdx);
            // Weak-beat color: occasionally step off the chord tone toward a neighbour.
            if (!strongIndex(i, stepsPerBar) && settings.color >= 1.f && rng.unit() * 100.f < settings.color * 0.35f)
                idx += (rng.nextInt(2) == 0 ? 1 : -1);
            out.pitch.steps[(size_t) i] = (int16_t) std::clamp(idx, -12, 12);
        }
        // Cadence: last sounding step lands on a chord tone, the one before approaches it.
        out.pitch.steps.back() = (int16_t) nearestChordIndex(0, chordIdx);
        if (n >= 2 && !strongIndex(n - 2, stepsPerBar))
            out.pitch.steps[(size_t) (n - 2)] = (int16_t) std::clamp((int) out.pitch.steps.back() + (seqDir > 0 ? 1 : -1), -12, 12);
    }

    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Velocity);
        for (int i = 0; i < (int) out.velocity.steps.size(); ++i) {
            const bool strong = strongIndex(i, stepsPerBar);
            int vel = strong ? 96 : 78;
            vel += (int) std::lround((rng.unit() - 0.5f) * (8.f + complexity * 0.15f));
            out.velocity.steps[(size_t) i] = (uint8_t) std::clamp(vel, 1, 127);
            if (i < (int) out.accent.size())
                out.accent[(size_t) i] = strong ? 1 : 0;
        }
    }

    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Gate);
        for (int i = 0; i < (int) out.gate.steps.size(); ++i) {
            const bool strong = strongIndex(i, stepsPerBar);
            int gate = strong ? 100 : 80;
            if (!strong && complexity > 20.f)
                gate += (int) std::lround((rng.unit() - 0.4f) * complexity * 0.6f);
            else
                (void) rng.unit();
            out.gate.steps[(size_t) i] = (uint16_t) std::clamp(gate, 5, 400);
        }
    }

    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Ratchet);
        for (int i = 0; i < (int) out.ratchet.steps.size(); ++i) {
            const bool strong = strongIndex(i, stepsPerBar);
            int r = 1;
            if (!strong && complexity > 45.f && rng.unit() < (complexity - 40.f) / 200.f)
                r = 2 + (complexity > 75.f && rng.unit() < 0.4f ? 1 : 0);
            else if (!strong)
                (void) rng.unit();
            out.ratchet.steps[(size_t) i] = (uint8_t) std::clamp(r, 1, 8);
        }
    }

    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Probability);
        for (int i = 0; i < (int) out.probability.steps.size(); ++i) {
            int p = 100;
            if (!strongIndex(i, stepsPerBar) && complexity > 30.f && rng.unit() < complexity / 400.f)
                p = 70;
            out.probability.steps[(size_t) i] = (uint8_t) p;
        }
    }

    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Timing);
        for (int i = 0; i < (int) out.timing.steps.size(); ++i) {
            int t = 0;
            if (!strongIndex(i, stepsPerBar) && complexity > 25.f)
                t = (int) std::lround((rng.unit() - 0.5f) * complexity * 0.2f);
            else
                (void) rng.unit();
            out.timing.steps[(size_t) i] = (int8_t) std::clamp(t, -50, 50);
        }
    }

    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Octave);
        for (int i = 0; i < (int) out.octave.steps.size(); ++i) {
            int o = 0;
            if (rng.unit() < rangeAmt / 250.f)
                o = rng.nextInt(2) == 0 ? -1 : 1;
            out.octave.steps[(size_t) i] = (int8_t) std::clamp(o, -2, 2);
        }
    }

    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Transpose);
        for (size_t i = 0; i < out.transpose.steps.size(); ++i) {
            (void) rng.unit();
            out.transpose.steps[i] = 0;
        }
    }

    {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::CC1);
        Prng rng2 = Prng::stream(settings.seed, (uint32_t) LaneId::CC2);
        const int n = std::max(1, (int) out.cc1.steps.size());
        for (int i = 0; i < n; ++i) {
            const int base = 48 + (i * 40) / n;
            out.cc1.steps[(size_t) i] = (uint8_t) std::clamp(base + rng.nextInt(7) - 3, 0, 127);
            out.cc2.steps[(size_t) std::min(i, (int) out.cc2.steps.size() - 1)] =
                (uint8_t) std::clamp(70 + (int) std::lround(std::sin(i * 0.7) * 20) + rng2.nextInt(5) - 2, 0, 127);
        }
    }

    // Ties on a few weak steps when complexity asks for them. Consumes no extra cross-lane state.
    if (complexity > 55.f) {
        Prng rng = Prng::stream(settings.seed, (uint32_t) LaneId::Type, 2);
        for (int i = 1; i < typeLen; ++i) {
            if (strongIndex(i, stepsPerBar))
                continue;
            if ((StepType) out.type.steps[(size_t) i] != StepType::Note)
                continue;
            if (rng.unit() < (complexity - 55.f) / 300.f)
                out.type.steps[(size_t) i] = (uint8_t) StepType::Tie;
        }
    }

    (void) reference;
    return out;
}

} // namespace

Pattern generatePattern(const Pattern& current, const GenerateSettings& settings, const Chord& reference) {
    Pattern fresh = generateRaw(current, settings, reference);
    const bool pitchLocked = current.pitch.locked;
    const PitchMode keepMode = current.pitchMode;
    overlay(fresh.pitch, current.pitch);
    overlay(fresh.octave, current.octave);
    overlay(fresh.transpose, current.transpose);
    overlay(fresh.type, current.type);
    overlay(fresh.velocity, current.velocity);
    overlay(fresh.gate, current.gate);
    overlay(fresh.ratchet, current.ratchet);
    overlay(fresh.probability, current.probability);
    overlay(fresh.timing, current.timing);
    overlay(fresh.cc1, current.cc1);
    overlay(fresh.cc2, current.cc2);
    if (pitchLocked)
        fresh.pitchMode = keepMode;
    if (current.velocity.locked)
        fresh.accent = current.accent;
    else if (fresh.accent.size() == current.accent.size()) {
        for (size_t i = 0; i < current.velocity.locks.size() && i < fresh.accent.size(); ++i) {
            if (current.velocity.locks[i] && i < current.accent.size())
                fresh.accent[i] = current.accent[i];
        }
    }
    if (fresh.accent.size() != fresh.velocity.steps.size())
        fresh.accent.resize(fresh.velocity.steps.size(), 0);
    return fresh;
}

Pattern evolvePattern(const Pattern& current, const GenerateSettings& settings, int amount, const Chord& reference) {
    Pattern out = current;
    amount = std::clamp(amount, 1, 100);
    const int stepsPerBar = std::max(1, settings.stepsPerBar);
    auto mutate = [&](auto& lane, LaneId id, auto apply) {
        if (lane.locked)
            return;
        for (int i = 0; i < (int) lane.steps.size(); ++i) {
            if (i < (int) lane.locks.size() && lane.locks[(size_t) i])
                continue;
            const uint32_t h = stepHash(settings.seed, (uint32_t) id, (uint32_t) i, 0xE501u, (uint32_t) amount);
            if ((int) (h % 100u) >= amount)
                continue;
            apply(lane.steps[(size_t) i], h, i);
        }
    };
    mutate(out.pitch, LaneId::Pitch, [&](int16_t& v, uint32_t h, int) {
        const int d = (h & 1u) ? 1 : -1;
        v = (int16_t) std::clamp((int) v + d, -12, 12);
    });
    mutate(out.velocity, LaneId::Velocity, [&](uint8_t& v, uint32_t h, int) {
        const int d = (h & 1u) ? 10 : -10;
        v = (uint8_t) std::clamp((int) v + d, 1, 127);
    });
    mutate(out.gate, LaneId::Gate, [&](uint16_t& v, uint32_t h, int) {
        const int d = (h & 1u) ? 15 : -15;
        v = (uint16_t) std::clamp((int) v + d, 5, 400);
    });
    mutate(out.type, LaneId::Type, [&](uint8_t& v, uint32_t, int i) {
        if (strongIndex(i, stepsPerBar))
            return;
        if ((StepType) v == StepType::Note)
            v = (uint8_t) StepType::Rest;
        else if ((StepType) v == StepType::Rest)
            v = (uint8_t) StepType::Note;
    });
    mutate(out.ratchet, LaneId::Ratchet, [&](uint8_t& v, uint32_t h, int i) {
        if (strongIndex(i, stepsPerBar))
            return;
        const int d = (h & 1u) ? 1 : -1;
        v = (uint8_t) std::clamp((int) v + d, 1, 8);
    });
    (void) reference;
    return out;
}

} // namespace phrasearp
