#pragma once

#include "engine/Chord.h"
#include "engine/Session.h"

namespace phrasearp {

Pattern generatePattern(const Pattern& current, const GenerateSettings& settings, const Chord& reference);
Pattern evolvePattern(const Pattern& current, const GenerateSettings& settings, int amount, const Chord& reference);

} // namespace phrasearp
