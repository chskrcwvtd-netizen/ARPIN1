#pragma once

#include "engine/Types.h"

#include <vector>

namespace phrasearp {

template <typename T>
struct Lane {
    std::vector<T> steps;
    std::vector<uint8_t> locks;
    int length = 16;
    LaneSpeed speed = LaneSpeed::X1;
    Direction direction = Direction::Forward;
    ResetMode reset = ResetMode::Phrase;
    bool locked = false;

    bool operator==(const Lane& o) const {
        return steps == o.steps && locks == o.locks && length == o.length && speed == o.speed && direction == o.direction
               && reset == o.reset && locked == o.locked;
    }
};

struct Pattern {
    Lane<int16_t> pitch;
    PitchMode pitchMode = PitchMode::Index;
    Lane<int8_t> octave;
    Lane<int8_t> transpose;
    Lane<uint8_t> type;
    Lane<uint8_t> velocity;
    std::vector<uint8_t> accent;
    Lane<uint16_t> gate;
    Lane<uint8_t> ratchet;
    Lane<uint8_t> probability;
    Lane<int8_t> timing;
    Lane<uint8_t> cc1;
    Lane<uint8_t> cc2;

    bool operator==(const Pattern& o) const {
        return pitch == o.pitch && pitchMode == o.pitchMode && octave == o.octave && transpose == o.transpose
               && type == o.type && velocity == o.velocity && accent == o.accent && gate == o.gate
               && ratchet == o.ratchet && probability == o.probability && timing == o.timing && cc1 == o.cc1
               && cc2 == o.cc2;
    }
};

struct LaneClipboard {
    LaneId lane = LaneId::Pitch;
    std::vector<int> values;
    std::vector<uint8_t> locks;
    bool valid = false;
};

int64_t scaledStepTicks(int64_t masterTicks, LaneSpeed speed);
int mapLaneIndex(int64_t position, int length, Direction direction, uint32_t seed, uint32_t laneId);

void preparePattern(Pattern& pattern, int length);
void applyLaneOp(Pattern& pattern, LaneId lane, int op, uint32_t seed);
void copyLane(const Pattern& pattern, LaneId lane, LaneClipboard& clip);
void pasteLane(Pattern& pattern, const LaneClipboard& clip);

// op codes used by applyLaneOp
constexpr int kOpShiftLeft = 0;
constexpr int kOpShiftRight = 1;
constexpr int kOpInvert = 2;
constexpr int kOpReverse = 3;
constexpr int kOpRandomize = 4;
constexpr int kOpDouble = 5;

} // namespace phrasearp
