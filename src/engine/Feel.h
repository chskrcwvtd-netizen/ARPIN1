#pragma once

#include "engine/Types.h"

namespace phrasearp {

struct GrooveMap {
    int timing[16];
    int velocity[16];
};

const char* grooveName(int index);
int grooveCount();
GrooveMap grooveMap(int index, const int8_t* customTiming, const int8_t* customVelocity);

int contourSteps(ContourShape shape, double position, const float custom[8]);
float dynamicsGain(DynamicsShape shape, double position, const float custom[8]);
int64_t swingDelayTicks(int64_t tick, float swingPercent, bool eighthGrid, int64_t ticksPerBeat);

} // namespace phrasearp
