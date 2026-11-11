#pragma once

#include "engine/Chord.h"
#include "engine/Session.h"

namespace phrasearp {

struct NoteSpan {
    int64_t onTick = 0;
    int64_t offTick = 0;
    int note = 60;
    int velocity = 100;
    int channel = 0;

    bool operator==(const NoteSpan& o) const = default;
};

struct RenderOutput {
    std::vector<ScheduledEvent> events;
    std::vector<NoteSpan> onsets;
};

RenderOutput renderMusic(const AudioState& state,
                         const RenderConfig& config,
                         int64_t startTick,
                         int64_t endTick,
                         double bpm,
                         int sigNum = 4,
                         int sigDen = 4);

// Melody realisation used by the voice-leading tests. One chord per step.
std::vector<int> realizeLine(const std::vector<int>& indices,
                             const std::vector<Chord>& chords,
                             const Scale& scale,
                             PitchMode mode,
                             float color,
                             int maxJump,
                             int low,
                             int high);

} // namespace phrasearp
