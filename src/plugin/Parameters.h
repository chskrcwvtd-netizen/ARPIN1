#pragma once

#include "engine/Feel.h"
#include "engine/Types.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace phrasearp::ids {

inline constexpr const char* color = "color";
inline constexpr const char* variation = "variation";
inline constexpr const char* swing = "swing";
inline constexpr const char* humanizeMs = "humanizeMs";
inline constexpr const char* humanizeVel = "humanizeVel";
inline constexpr const char* humanizeGate = "humanizeGate";
inline constexpr const char* accent = "accent";
inline constexpr const char* maxJump = "maxJump";
inline constexpr const char* density = "density";
inline constexpr const char* complexity = "complexity";
inline constexpr const char* motion = "motion";
inline constexpr const char* repetition = "repetition";
inline constexpr const char* range = "range";
inline constexpr const char* evolveAmount = "evolveAmount";
inline constexpr const char* strumMs = "strumMs";
inline constexpr const char* strumTilt = "strumTilt";
inline constexpr const char* fixedMs = "fixedMs";
inline constexpr const char* previewVolume = "previewVolume";
inline constexpr const char* previewDecay = "previewDecay";
inline constexpr const char* lowNote = "lowNote";
inline constexpr const char* highNote = "highNote";
inline constexpr const char* octaves = "octaves";
inline constexpr const char* outputChannel = "outputChannel";
inline constexpr const char* cc1 = "cc1";
inline constexpr const char* cc2 = "cc2";
inline constexpr const char* phraseBars = "phraseBars";
inline constexpr const char* phraseTemplate = "phraseTemplate";
inline constexpr const char* rate = "rate";
inline constexpr const char* scale = "scale";
inline constexpr const char* keyRoot = "keyRoot";
inline constexpr const char* groove = "groove";
inline constexpr const char* contour = "contour";
inline constexpr const char* dynamics = "dynamics";
inline constexpr const char* retrigger = "retrigger";
inline constexpr const char* boundary = "boundary";
inline constexpr const char* noteLength = "noteLength";
inline constexpr const char* strumDir = "strumDir";
inline constexpr const char* ratchetCurve = "ratchetCurve";
inline constexpr const char* timbre = "timbre";
inline constexpr const char* slot = "slot";
inline constexpr const char* slotQuantize = "slotQuantize";
inline constexpr const char* autoEvolve = "autoEvolve";
inline constexpr const char* questionAnswer = "questionAnswer";
inline constexpr const char* twoBar = "twoBar";
inline constexpr const char* humanizeLive = "humanizeLive";
inline constexpr const char* swingEighth = "swingEighth";
inline constexpr const char* euclidean = "euclidean";
inline constexpr const char* thru = "thru";
inline constexpr const char* preview = "preview";
inline constexpr const char* audition = "audition";
inline constexpr const char* latch = "latch";
inline constexpr const char* autoKey = "autoKey";
inline constexpr const char* ccSmooth = "ccSmooth";
inline constexpr const char* chordTrack = "chordTrack";

inline const char* macro(int index) {
    static const char* names[] = {"macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7", "macro8"};
    return names[index & 7];
}

} // namespace phrasearp::ids

namespace phrasearp {

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto addFloat = [&](const char* id, const char* name, float lo, float hi, float def, float step = 0.01f) {
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, NormalisableRange<float>(lo, hi, step), def));
    };
    auto addInt = [&](const char* id, const char* name, int lo, int hi, int def) {
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id, 1}, name, lo, hi, def));
    };
    auto addBool = [&](const char* id, const char* name, bool def) {
        layout.add(std::make_unique<AudioParameterBool>(ParameterID{id, 1}, name, def));
    };
    auto addChoice = [&](const char* id, const char* name, const StringArray& items, int def) {
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, items, def));
    };

    addFloat(ids::color, "Color", 0.f, 100.f, 28.f);
    addFloat(ids::variation, "Variation", 0.f, 100.f, 0.f);
    addFloat(ids::swing, "Swing", 50.f, 75.f, 50.f);
    addFloat(ids::humanizeMs, "Humanize", 0.f, 20.f, 0.f);
    addFloat(ids::humanizeVel, "Humanize Velocity", 0.f, 30.f, 0.f);
    addFloat(ids::humanizeGate, "Humanize Gate", 0.f, 20.f, 0.f);
    addFloat(ids::accent, "Accent", 0.f, 40.f, 20.f, 1.f);
    addFloat(ids::maxJump, "Max Jump", 1.f, 12.f, 4.f, 1.f);
    addFloat(ids::density, "Density", 0.f, 100.f, 58.f);
    addFloat(ids::complexity, "Complexity", 0.f, 100.f, 32.f);
    addFloat(ids::motion, "Motion", 0.f, 100.f, 38.f);
    addFloat(ids::repetition, "Repetition", 0.f, 100.f, 74.f);
    addFloat(ids::range, "Range", 0.f, 100.f, 48.f);
    addFloat(ids::evolveAmount, "Evolve", 1.f, 100.f, 20.f, 1.f);
    addFloat(ids::strumMs, "Strum", 0.f, 100.f, 0.f, 1.f);
    addFloat(ids::strumTilt, "Strum Tilt", -40.f, 40.f, 10.f, 1.f);
    addFloat(ids::fixedMs, "Fixed Length", 10.f, 2000.f, 250.f, 1.f);
    addFloat(ids::previewVolume, "Preview Volume", 0.f, 1.f, 0.7f);
    addFloat(ids::previewDecay, "Preview Decay", 0.f, 1.f, 0.45f);
    static const char* macroNames[] = {"Macro 1", "Macro 2", "Macro 3", "Macro 4",
                                       "Macro 5", "Macro 6", "Macro 7", "Macro 8"};
    for (int i = 0; i < 8; ++i)
        addFloat(ids::macro(i), macroNames[i], 0.f, 1.f, 0.5f);

    addInt(ids::lowNote, "Low Note", 0, 127, 48);
    addInt(ids::highNote, "High Note", 0, 127, 84);
    addInt(ids::octaves, "Octaves", 1, 4, 3);
    addInt(ids::outputChannel, "Output Channel", 1, 16, 1);
    addInt(ids::cc1, "CC 1", 0, 127, 74);
    addInt(ids::cc2, "CC 2", 0, 127, 1);

    addChoice(ids::phraseBars, "Phrase Bars", {"1", "2", "4", "8", "16"}, 2);
    addChoice(ids::phraseTemplate, "Phrase Template", {"AAAA", "AAAB", "AABA", "ABAC", "AABB"}, 0);
    StringArray rates;
    for (int i = 0; i < (int) phrasearp::StepRate::Count; ++i)
        rates.add(phrasearp::stepRateName((phrasearp::StepRate) i));
    addChoice(ids::rate, "Rate", rates, (int) phrasearp::StepRate::Eighth);
    StringArray scales;
    for (int i = 0; i < (int) phrasearp::ScaleType::Count; ++i)
        scales.add(phrasearp::scaleTypeName((phrasearp::ScaleType) i));
    addChoice(ids::scale, "Scale", scales, 0);
    addChoice(ids::keyRoot, "Key", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0);
    StringArray grooves;
    for (int i = 0; i < phrasearp::grooveCount(); ++i)
        grooves.add(phrasearp::grooveName(i));
    addChoice(ids::groove, "Groove", grooves, 0);
    addChoice(ids::contour, "Contour", {"Flat", "Rising", "Arch", "Wave", "Custom"}, 0);
    addChoice(ids::dynamics, "Dynamics", {"Flat", "Arch", "Rising", "Wave", "Custom"}, 1);
    addChoice(ids::retrigger, "Retrigger", {"Off", "Chord", "Bar", "Key"}, 0);
    addChoice(ids::boundary, "Boundary", {"Fold", "Wrap"}, 0);
    addChoice(ids::noteLength, "Note Length", {"Gate", "Fixed ms", "Legato"}, 0);
    addChoice(ids::strumDir, "Strum Direction", {"Up", "Down", "Alternate"}, 1);
    addChoice(ids::ratchetCurve, "Ratchet Curve", {"Linear", "Exponential", "Rising"}, 0);
    addChoice(ids::timbre, "Timbre", {"Pluck", "Bell", "Keys"}, 0);
    addChoice(ids::slot, "Slot", {"A", "B", "C", "D", "E", "F", "G", "H"}, 0);
    addChoice(ids::slotQuantize, "Slot Quantize", {"Immediate", "Bar", "Phrase"}, 0);
    addChoice(ids::autoEvolve, "Auto Evolve", {"Off", "1 phrase", "2 phrases", "4 phrases", "8 phrases"}, 0);

    addBool(ids::questionAnswer, "Question Answer", true);
    addBool(ids::twoBar, "Two Bar Unit", false);
    addBool(ids::humanizeLive, "Humanize Live", false);
    addBool(ids::swingEighth, "Swing 1/8", true);
    addBool(ids::euclidean, "Euclidean", false);
    addBool(ids::thru, "Thru", false);
    addBool(ids::preview, "Preview", true);
    addBool(ids::audition, "Audition", true);
    addBool(ids::latch, "Latch", false);
    addBool(ids::autoKey, "Auto Key", false);
    addBool(ids::ccSmooth, "CC Smooth", false);
    addBool(ids::chordTrack, "Chord Track", true);
    return layout;
}

} // namespace phrasearp
