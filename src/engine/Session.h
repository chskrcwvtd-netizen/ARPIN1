#pragma once

#include "engine/Pattern.h"

#include <array>
#include <string>

namespace phrasearp {

struct ChordBar {
    bool set = false;
    std::vector<int> notes;

    bool operator==(const ChordBar& o) const { return set == o.set && notes == o.notes; }
};

struct ChordTrack {
    bool enabled = true;
    std::vector<ChordBar> bars;

    bool operator==(const ChordTrack& o) const { return enabled == o.enabled && bars == o.bars; }
};

struct AudioState {
    uint32_t seed = kInitSeed;
    std::string presetName = "Init";
    std::array<Pattern, kPatternSlots> patterns {};
    int activeSlot = 0;
    ChordTrack chords;
    std::array<int, kMaxChain> chain {};
    int chainLength = 0;
    uint16_t customScale = 0;
    std::array<int8_t, 16> grooveTiming {};
    std::array<int8_t, 16> grooveVelocity {};
    std::array<int, 8> macroTarget {};
    std::array<float, 8> macroDepth {};
    bool keySwitchEnabled = false;
    int keySwitchLow = 24;
    int keySwitchHigh = 31;
    int resetEveryNBars = 4;
    std::array<float, 8> contourCustom {};
    std::array<float, 8> dynamicsCustom {};

    bool operator==(const AudioState& o) const {
        return seed == o.seed && presetName == o.presetName && patterns == o.patterns && activeSlot == o.activeSlot
               && chords == o.chords && chain == o.chain && chainLength == o.chainLength && customScale == o.customScale
               && grooveTiming == o.grooveTiming && grooveVelocity == o.grooveVelocity && macroTarget == o.macroTarget
               && macroDepth == o.macroDepth && keySwitchEnabled == o.keySwitchEnabled && keySwitchLow == o.keySwitchLow
               && keySwitchHigh == o.keySwitchHigh && resetEveryNBars == o.resetEveryNBars
               && contourCustom == o.contourCustom && dynamicsCustom == o.dynamicsCustom;
    }
};

struct History {
    std::vector<AudioState> states;
    int index = -1;

    void push(const AudioState& state);
    bool canUndo() const { return index > 0; }
    bool canRedo() const { return index >= 0 && index + 1 < (int) states.size(); }
    const AudioState* undo();
    const AudioState* redo();
    const AudioState* current() const;
};

AudioState makeInitState();
RenderConfig defaultRenderConfig();
GenerateSettings defaultGenerateSettings(const RenderConfig& cfg);

std::string serializeState(const AudioState& state);
bool deserializeState(const std::string& text, AudioState& state);

const Pattern& activePattern(const AudioState& state);

} // namespace phrasearp
