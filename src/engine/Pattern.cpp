#include "engine/Pattern.h"

#include "engine/Prng.h"

#include <algorithm>
#include <type_traits>

namespace phrasearp {
namespace {

template <typename T>
void resizeLane(Lane<T>& lane, int length, T fill) {
    length = std::clamp(length, 1, kMaxSteps);
    lane.length = length;
    lane.steps.resize((size_t) length, fill);
    lane.locks.resize((size_t) length, 0);
    if ((int) lane.steps.size() > length)
        lane.steps.resize((size_t) length);
}

template <typename T>
void shiftLane(Lane<T>& lane, int dir) {
    if (lane.steps.empty())
        return;
    if (dir < 0)
        std::rotate(lane.steps.begin(), lane.steps.begin() + 1, lane.steps.end());
    else
        std::rotate(lane.steps.rbegin(), lane.steps.rbegin() + 1, lane.steps.rend());
    if (lane.locks.size() == lane.steps.size()) {
        if (dir < 0)
            std::rotate(lane.locks.begin(), lane.locks.begin() + 1, lane.locks.end());
        else
            std::rotate(lane.locks.rbegin(), lane.locks.rbegin() + 1, lane.locks.rend());
    }
}

template <typename T>
void reverseLane(Lane<T>& lane) {
    std::reverse(lane.steps.begin(), lane.steps.end());
    if (lane.locks.size() == lane.steps.size())
        std::reverse(lane.locks.begin(), lane.locks.end());
}

template <typename T>
void doubleLane(Lane<T>& lane) {
    if (lane.length >= kMaxSteps || lane.steps.empty())
        return;
    const int n = lane.length;
    const int m = std::min(kMaxSteps, n * 2);
    lane.steps.resize((size_t) m);
    lane.locks.resize((size_t) m, 0);
    for (int i = 0; i < m - n; ++i) {
        lane.steps[(size_t) (n + i)] = lane.steps[(size_t) (i % n)];
        lane.locks[(size_t) (n + i)] = lane.locks[(size_t) (i % n)];
    }
    lane.length = m;
}

} // namespace

int64_t scaledStepTicks(int64_t masterTicks, LaneSpeed speed) {
    if (masterTicks < 1)
        masterTicks = 1;
    switch (speed) {
        case LaneSpeed::X1_4: return masterTicks * 4;
        case LaneSpeed::X1_2: return masterTicks * 2;
        case LaneSpeed::X1: return masterTicks;
        case LaneSpeed::X2: return std::max<int64_t>(1, masterTicks / 2);
        case LaneSpeed::X4: return std::max<int64_t>(1, masterTicks / 4);
    }
    return masterTicks;
}

int mapLaneIndex(int64_t position, int length, Direction direction, uint32_t seed, uint32_t laneId) {
    if (length <= 1)
        return 0;
    if (position < 0)
        position = 0;
    switch (direction) {
        case Direction::Forward: return (int) (position % length);
        case Direction::Backward: return (int) ((length - 1 - (position % length) + length) % length);
        case Direction::PingPong: {
            const int cycle = length * 2 - 2;
            const int m = (int) (position % cycle);
            return m < length ? m : cycle - m;
        }
        case Direction::Random: {
            std::vector<int> perm((size_t) length);
            for (int i = 0; i < length; ++i)
                perm[(size_t) i] = i;
            Prng rng = Prng::stream(seed, laneId, 0x51A7u);
            for (int k = length - 1; k > 0; --k) {
                const int j = rng.nextInt(k + 1);
                std::swap(perm[(size_t) k], perm[(size_t) j]);
            }
            return perm[(size_t) (position % length)];
        }
        case Direction::RandomWalk: {
            const int period = std::max(1, length * 2);
            const int steps = (int) (position % period);
            int idx = 0;
            Prng rng = Prng::stream(seed, laneId, 0x5A11u);
            for (int s = 0; s < steps; ++s) {
                const int d = rng.nextInt(3) - 1;
                idx = (idx + d + length) % length;
            }
            return idx;
        }
    }
    return (int) (position % length);
}

void preparePattern(Pattern& pattern, int length) {
    length = std::clamp(length, 1, kMaxSteps);
    resizeLane(pattern.pitch, length, (int16_t) 0);
    resizeLane(pattern.octave, length, (int8_t) 0);
    resizeLane(pattern.transpose, length, (int8_t) 0);
    resizeLane(pattern.type, length, (uint8_t) StepType::Note);
    resizeLane(pattern.velocity, length, (uint8_t) 96);
    resizeLane(pattern.gate, length, (uint16_t) 90);
    resizeLane(pattern.ratchet, length, (uint8_t) 1);
    resizeLane(pattern.probability, length, (uint8_t) 100);
    resizeLane(pattern.timing, length, (int8_t) 0);
    resizeLane(pattern.cc1, length, (uint8_t) 64);
    resizeLane(pattern.cc2, length, (uint8_t) 0);
    pattern.accent.assign((size_t) length, 0);
}

void copyLane(const Pattern& pattern, LaneId lane, LaneClipboard& clip) {
    clip.lane = lane;
    clip.valid = true;
    clip.values.clear();
    clip.locks.clear();
    auto take = [&](const auto& src) {
        clip.values.reserve(src.steps.size());
        for (auto v : src.steps)
            clip.values.push_back((int) v);
        clip.locks = src.locks;
    };
    switch (lane) {
        case LaneId::Pitch: take(pattern.pitch); break;
        case LaneId::Octave: take(pattern.octave); break;
        case LaneId::Transpose: take(pattern.transpose); break;
        case LaneId::Type: take(pattern.type); break;
        case LaneId::Velocity: take(pattern.velocity); break;
        case LaneId::Gate: take(pattern.gate); break;
        case LaneId::Ratchet: take(pattern.ratchet); break;
        case LaneId::Probability: take(pattern.probability); break;
        case LaneId::Timing: take(pattern.timing); break;
        case LaneId::CC1: take(pattern.cc1); break;
        case LaneId::CC2: take(pattern.cc2); break;
        case LaneId::Count: clip.valid = false; break;
    }
}

void pasteLane(Pattern& pattern, const LaneClipboard& clip) {
    if (!clip.valid)
        return;
    auto put = [&](auto& dst, auto lo, auto hi) {
        const int n = std::min((int) dst.steps.size(), (int) clip.values.size());
        for (int i = 0; i < n; ++i) {
            if (i < (int) dst.locks.size() && dst.locks[(size_t) i])
                continue;
            int v = clip.values[(size_t) i];
            v = std::clamp(v, (int) lo, (int) hi);
            using Value = std::decay_t<decltype(dst.steps[0])>;
            dst.steps[(size_t) i] = static_cast<Value>(v);
        }
    };
    switch (clip.lane) {
        case LaneId::Pitch: put(pattern.pitch, -12, 12); break;
        case LaneId::Octave: put(pattern.octave, -2, 2); break;
        case LaneId::Transpose: put(pattern.transpose, -7, 7); break;
        case LaneId::Type: put(pattern.type, 0, 4); break;
        case LaneId::Velocity: put(pattern.velocity, 1, 127); break;
        case LaneId::Gate: put(pattern.gate, 5, 400); break;
        case LaneId::Ratchet: put(pattern.ratchet, 1, 8); break;
        case LaneId::Probability: put(pattern.probability, 0, 100); break;
        case LaneId::Timing: put(pattern.timing, -50, 50); break;
        case LaneId::CC1: put(pattern.cc1, 0, 127); break;
        case LaneId::CC2: put(pattern.cc2, 0, 127); break;
        case LaneId::Count: break;
    }
}

void applyLaneOp(Pattern& pattern, LaneId lane, int op, uint32_t seed) {
    auto with = [&](auto& dst, auto invertFn, auto randomFn) {
        if (dst.locked)
            return;
        if (op == kOpShiftLeft)
            shiftLane(dst, -1);
        else if (op == kOpShiftRight)
            shiftLane(dst, +1);
        else if (op == kOpReverse)
            reverseLane(dst);
        else if (op == kOpDouble)
            doubleLane(dst);
        else if (op == kOpInvert) {
            for (size_t i = 0; i < dst.steps.size(); ++i) {
                if (i < dst.locks.size() && dst.locks[i])
                    continue;
                dst.steps[i] = invertFn(dst.steps[i]);
            }
        } else if (op == kOpRandomize) {
            Prng rng = Prng::stream(seed, (uint32_t) lane, 0xA11Du);
            for (size_t i = 0; i < dst.steps.size(); ++i) {
                if (i < dst.locks.size() && dst.locks[i])
                    continue;
                dst.steps[i] = randomFn(rng);
            }
        }
    };

    switch (lane) {
        case LaneId::Pitch:
            with(pattern.pitch,
                 [](int16_t v) { return (int16_t) std::clamp(-v, -12, 12); },
                 [](Prng& r) { return (int16_t) (r.nextInt(25) - 12); });
            break;
        case LaneId::Octave:
            with(pattern.octave,
                 [](int8_t v) { return (int8_t) std::clamp(-v, -2, 2); },
                 [](Prng& r) { return (int8_t) (r.nextInt(5) - 2); });
            break;
        case LaneId::Transpose:
            with(pattern.transpose,
                 [](int8_t v) { return (int8_t) std::clamp(-v, -7, 7); },
                 [](Prng& r) { return (int8_t) (r.nextInt(15) - 7); });
            break;
        case LaneId::Type:
            with(pattern.type,
                 [](uint8_t v) {
                     if ((StepType) v == StepType::Note)
                         return (uint8_t) StepType::Rest;
                     if ((StepType) v == StepType::Rest)
                         return (uint8_t) StepType::Note;
                     return v;
                 },
                 [](Prng& r) { return (uint8_t) r.nextInt(5); });
            break;
        case LaneId::Velocity:
            with(pattern.velocity,
                 [](uint8_t v) { return (uint8_t) std::clamp(128 - (int) v, 1, 127); },
                 [](Prng& r) { return (uint8_t) (1 + r.nextInt(127)); });
            if (op == kOpShiftLeft && !pattern.accent.empty())
                std::rotate(pattern.accent.begin(), pattern.accent.begin() + 1, pattern.accent.end());
            else if (op == kOpShiftRight && !pattern.accent.empty())
                std::rotate(pattern.accent.rbegin(), pattern.accent.rbegin() + 1, pattern.accent.rend());
            else if (op == kOpReverse)
                std::reverse(pattern.accent.begin(), pattern.accent.end());
            break;
        case LaneId::Gate:
            with(pattern.gate,
                 [](uint16_t v) { return (uint16_t) std::clamp(200 - (int) v, 5, 400); },
                 [](Prng& r) { return (uint16_t) (40 + r.nextInt(120)); });
            break;
        case LaneId::Ratchet:
            with(pattern.ratchet,
                 [](uint8_t v) { return (uint8_t) std::clamp(9 - (int) v, 1, 8); },
                 [](Prng& r) { return (uint8_t) (1 + r.nextInt(4)); });
            break;
        case LaneId::Probability:
            with(pattern.probability,
                 [](uint8_t v) { return (uint8_t) std::clamp(100 - (int) v, 0, 100); },
                 [](Prng& r) { return (uint8_t) (50 + r.nextInt(51)); });
            break;
        case LaneId::Timing:
            with(pattern.timing,
                 [](int8_t v) { return (int8_t) std::clamp(-v, -50, 50); },
                 [](Prng& r) { return (int8_t) (r.nextInt(41) - 20); });
            break;
        case LaneId::CC1:
            with(pattern.cc1,
                 [](uint8_t v) { return (uint8_t) (127 - v); },
                 [](Prng& r) { return (uint8_t) r.nextInt(128); });
            break;
        case LaneId::CC2:
            with(pattern.cc2,
                 [](uint8_t v) { return (uint8_t) (127 - v); },
                 [](Prng& r) { return (uint8_t) r.nextInt(128); });
            break;
        case LaneId::Count: break;
    }
    if (pattern.accent.size() != pattern.velocity.steps.size())
        pattern.accent.resize(pattern.velocity.steps.size(), 0);
}

} // namespace phrasearp
