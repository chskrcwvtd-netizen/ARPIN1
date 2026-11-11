#include "engine/Feel.h"

#include <algorithm>
#include <cmath>

namespace phrasearp {
namespace {

constexpr int kGrooveCount = 13; // 12 factory + custom

const int kTiming[12][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 8, 0},
    {0, 0, 18, 0, 0, 0, 18, 0, 0, 0, 18, 0, 0, 0, 18, 0},
    {0, 6, 0, 6, 0, 6, 0, 6, 0, 6, 0, 6, 0, 6, 0, 6},
    {0, 14, 0, 14, 0, 14, 0, 14, 0, 14, 0, 14, 0, 14, 0, 14},
    {6, 6, 8, 6, 12, 8, 8, 6, 6, 6, 8, 6, 12, 8, 8, 6},
    {-6, -2, -4, -2, -8, -2, -4, -2, -6, -2, -4, -2, -8, -2, -4, -2},
    {0, 4, 10, 0, 4, 10, 0, 4, 10, 0, 4, 10, 0, 4, 10, 0},
    {4, 6, 8, 6, 4, 8, 10, 6, 4, 6, 8, 6, 4, 8, 10, 6},
    {0, -12, 0, -14, 0, -10, 0, -12, 0, -12, 0, -14, 0, -10, 0, -12},
    {0, 4, 2, 8, 0, 2, 10, 4, 0, 6, 2, 4, 0, 8, 4, 2},
    {0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16},
};

const int kVelocity[12][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {4, 0, 2, 0, 4, 0, 2, 0, 4, 0, 2, 0, 4, 0, 2, 0},
    {6, -2, 2, -2, 6, -2, 2, -2, 6, -2, 2, -2, 6, -2, 2, -2},
    {2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0},
    {4, -4, 2, -4, 4, -4, 2, -4, 4, -4, 2, -4, 4, -4, 2, -4},
    {-2, -4, -2, -4, 2, -2, -2, -4, -2, -4, -2, -4, 4, -2, -2, -4},
    {2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0},
    {8, -6, -2, 6, -4, -2, 8, -6, -2, 6, -4, -2, 8, -6, 4, -2},
    {6, -8, 0, -6, 2, -8, 8, -6, 0, -8, 0, -6, 6, -8, 2, -4},
    {10, -16, -4, -14, 4, -12, -2, -14, 2, -16, -4, -12, 8, -14, -2, -12},
    {12, -6, -8, 8, -4, -6, 10, -8, -2, 6, -6, -4, 12, -8, 4, -6},
    {6, -6, 2, -4, 6, -6, 2, -4, 6, -6, 2, -4, 6, -6, 2, -4},
};

const char* kNames[] = {
    "Straight", "Light Swing 8", "Heavy Swing 8", "Light Swing 16", "Heavy Swing 16", "Laid Back",
    "Pushed", "Triplet Pulse", "Half Time", "Funk Ghosts", "Clave 3-2", "Shuffle 16", "Custom"
};

float customAt(const float* points, double position) {
    if (points == nullptr)
        return 0.f;
    const double x = std::clamp(position, 0.0, 1.0) * 7.0;
    const int i = std::min(6, (int) x);
    const float f = (float) (x - i);
    return points[i] * (1.f - f) + points[i + 1] * f;
}

} // namespace

const char* grooveName(int index) {
    if (index < 0 || index >= kGrooveCount)
        return kNames[0];
    return kNames[index];
}

int grooveCount() { return kGrooveCount; }

GrooveMap grooveMap(int index, const int8_t* customTiming, const int8_t* customVelocity) {
    GrooveMap map {};
    if (index >= 12 && customTiming != nullptr && customVelocity != nullptr) {
        for (int i = 0; i < 16; ++i) {
            map.timing[i] = customTiming[i];
            map.velocity[i] = customVelocity[i];
        }
        return map;
    }
    if (index < 0 || index >= 12)
        index = 0;
    for (int i = 0; i < 16; ++i) {
        map.timing[i] = kTiming[index][i];
        map.velocity[i] = kVelocity[index][i];
    }
    return map;
}

int contourSteps(ContourShape shape, double position, const float custom[8]) {
    const double p = std::clamp(position, 0.0, 1.0);
    switch (shape) {
        case ContourShape::Flat: return 0;
        case ContourShape::Rising: return (int) std::lround(-2.0 + 4.0 * p);
        case ContourShape::Arch: return (int) std::lround(2.0 * std::sin(p * 3.141592653589793));
        case ContourShape::Wave: return (int) std::lround(std::sin(p * 2.0 * 3.141592653589793));
        case ContourShape::Custom: return (int) std::lround(customAt(custom, p) * 4.f - 2.f);
    }
    return 0;
}

float dynamicsGain(DynamicsShape shape, double position, const float custom[8]) {
    const double p = std::clamp(position, 0.0, 1.0);
    switch (shape) {
        case DynamicsShape::Flat: return 1.f;
        case DynamicsShape::Arch: return 0.82f + 0.28f * (float) std::sin(p * 3.141592653589793);
        case DynamicsShape::Rising: return 0.75f + 0.35f * (float) p;
        case DynamicsShape::Wave: return 0.9f + 0.15f * (float) std::sin(p * 2.0 * 3.141592653589793);
        case DynamicsShape::Custom: return std::clamp(0.5f + customAt(custom, p), 0.2f, 1.5f);
    }
    return 1.f;
}

int64_t swingDelayTicks(int64_t tick, float swingPercent, bool eighthGrid, int64_t ticksPerBeat) {
    if (swingPercent <= 50.01f || ticksPerBeat <= 0)
        return 0;
    const int64_t grid = eighthGrid ? ticksPerBeat / 2 : ticksPerBeat / 4;
    if (grid <= 0)
        return 0;
    if (floorMod(tick, grid * 2) != grid)
        return 0;
    const double swung = (double) (grid * 2) * (double) swingPercent / 100.0;
    return (int64_t) std::llround(swung - (double) grid);
}

} // namespace phrasearp
