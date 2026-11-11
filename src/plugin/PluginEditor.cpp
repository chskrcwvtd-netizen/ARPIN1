#include "plugin/PluginProcessor.h"

#include <cmath>

namespace {

juce::String noteToken(int midi) {
    midi = std::clamp(midi, 0, 127);
    const int oct = midi / 12 - 1;
    return juce::String(phrasearp::pitchClassName(phrasearp::pcOf(midi))) + juce::String(oct);
}

bool parseChordText(const juce::String& text, std::vector<int>& notes) {
    notes.clear();
    auto tokens = juce::StringArray::fromTokens(text, " ,/", "");
    tokens.removeEmptyStrings();
    if (tokens.isEmpty())
        return true;
    int previous = 48;
    bool anyName = false;
    for (auto token : tokens) {
        token = token.trim();
        if (token.containsOnly("0123456789")) {
            notes.push_back(std::clamp(token.getIntValue(), 0, 127));
            previous = notes.back();
            continue;
        }
        anyName = true;
        juce::String letters = token.toUpperCase();
        int octave = -1;
        if (letters.length() >= 2 && juce::CharacterFunctions::isDigit(letters[letters.length() - 1])) {
            octave = letters.getTrailingIntValue();
            letters = letters.dropLastCharacters(1);
        }
        int pc = -1;
        if (letters == "C") pc = 0;
        else if (letters == "C#" || letters == "DB") pc = 1;
        else if (letters == "D") pc = 2;
        else if (letters == "D#" || letters == "EB") pc = 3;
        else if (letters == "E") pc = 4;
        else if (letters == "F") pc = 5;
        else if (letters == "F#" || letters == "GB") pc = 6;
        else if (letters == "G") pc = 7;
        else if (letters == "G#" || letters == "AB") pc = 8;
        else if (letters == "A") pc = 9;
        else if (letters == "A#" || letters == "BB") pc = 10;
        else if (letters == "B") pc = 11;
        else
            return false;
        int midi = octave >= 0 ? octave * 12 + 12 + pc : 60 + pc;
        if (octave < 0 && !notes.empty()) {
            while (midi < previous)
                midi += 12;
        }
        midi = std::clamp(midi, 0, 127);
        notes.push_back(midi);
        previous = midi;
    }
    (void) anyName;
    return true;
}

juce::String chordText(const std::vector<int>& notes) {
    juce::String out;
    for (int n : notes) {
        if (out.isNotEmpty())
            out << " ";
        out << noteToken(n);
    }
    return out;
}

int readStep(const phrasearp::Pattern& pattern, phrasearp::LaneId lane, int index) {
    auto at = [&](const auto& src) {
        if (index < 0 || index >= (int) src.steps.size())
            return 0;
        return (int) src.steps[(size_t) index];
    };
    switch (lane) {
        case phrasearp::LaneId::Pitch: return at(pattern.pitch);
        case phrasearp::LaneId::Octave: return at(pattern.octave);
        case phrasearp::LaneId::Transpose: return at(pattern.transpose);
        case phrasearp::LaneId::Type: return at(pattern.type);
        case phrasearp::LaneId::Velocity: return at(pattern.velocity);
        case phrasearp::LaneId::Gate: return at(pattern.gate);
        case phrasearp::LaneId::Ratchet: return at(pattern.ratchet);
        case phrasearp::LaneId::Probability: return at(pattern.probability);
        case phrasearp::LaneId::Timing: return at(pattern.timing);
        case phrasearp::LaneId::CC1: return at(pattern.cc1);
        case phrasearp::LaneId::CC2: return at(pattern.cc2);
        case phrasearp::LaneId::Count: break;
    }
    return 0;
}

bool stepLocked(const phrasearp::Pattern& pattern, phrasearp::LaneId lane, int index) {
    auto at = [&](const auto& src) {
        return index >= 0 && index < (int) src.locks.size() && src.locks[(size_t) index] != 0;
    };
    switch (lane) {
        case phrasearp::LaneId::Pitch: return at(pattern.pitch);
        case phrasearp::LaneId::Octave: return at(pattern.octave);
        case phrasearp::LaneId::Transpose: return at(pattern.transpose);
        case phrasearp::LaneId::Type: return at(pattern.type);
        case phrasearp::LaneId::Velocity: return at(pattern.velocity);
        case phrasearp::LaneId::Gate: return at(pattern.gate);
        case phrasearp::LaneId::Ratchet: return at(pattern.ratchet);
        case phrasearp::LaneId::Probability: return at(pattern.probability);
        case phrasearp::LaneId::Timing: return at(pattern.timing);
        case phrasearp::LaneId::CC1: return at(pattern.cc1);
        case phrasearp::LaneId::CC2: return at(pattern.cc2);
        case phrasearp::LaneId::Count: break;
    }
    return false;
}

void writeStep(phrasearp::Pattern& pattern, phrasearp::LaneId lane, int index, int value, bool lockFlip) {
    auto put = [&](auto& dst, int lo, int hi) {
        if (index < 0 || index >= (int) dst.steps.size())
            return;
        if (lockFlip) {
            if (index < (int) dst.locks.size())
                dst.locks[(size_t) index] = dst.locks[(size_t) index] ? 0 : 1;
            return;
        }
        if (index < (int) dst.locks.size() && dst.locks[(size_t) index])
            return;
        using Value = typename std::decay_t<decltype(dst.steps)>::value_type;
        dst.steps[(size_t) index] = static_cast<Value>(std::clamp(value, lo, hi));
    };
    switch (lane) {
        case phrasearp::LaneId::Pitch: put(pattern.pitch, -12, 12); break;
        case phrasearp::LaneId::Octave: put(pattern.octave, -2, 2); break;
        case phrasearp::LaneId::Transpose: put(pattern.transpose, -7, 7); break;
        case phrasearp::LaneId::Type: put(pattern.type, 0, 4); break;
        case phrasearp::LaneId::Velocity: put(pattern.velocity, 1, 127); break;
        case phrasearp::LaneId::Gate: put(pattern.gate, 5, 400); break;
        case phrasearp::LaneId::Ratchet: put(pattern.ratchet, 1, 8); break;
        case phrasearp::LaneId::Probability: put(pattern.probability, 0, 100); break;
        case phrasearp::LaneId::Timing: put(pattern.timing, -50, 50); break;
        case phrasearp::LaneId::CC1: put(pattern.cc1, 0, 127); break;
        case phrasearp::LaneId::CC2: put(pattern.cc2, 0, 127); break;
        case phrasearp::LaneId::Count: break;
    }
}

struct LaneUi {
    const char* name;
    phrasearp::LaneId id;
    int minV;
    int maxV;
};

const LaneUi kLanes[] = {
    {"Pitch", phrasearp::LaneId::Pitch, -12, 12},
    {"Octave", phrasearp::LaneId::Octave, -2, 2},
    {"Transpose", phrasearp::LaneId::Transpose, -7, 7},
    {"Type", phrasearp::LaneId::Type, 0, 4},
    {"Velocity", phrasearp::LaneId::Velocity, 1, 127},
    {"Gate", phrasearp::LaneId::Gate, 5, 400},
    {"Ratchet", phrasearp::LaneId::Ratchet, 1, 8},
    {"Probability", phrasearp::LaneId::Probability, 0, 100},
    {"Timing", phrasearp::LaneId::Timing, -50, 50},
    {"CC 1", phrasearp::LaneId::CC1, 0, 127},
    {"CC 2", phrasearp::LaneId::CC2, 0, 127},
};

class PianoRoll : public juce::Component {
public:
    void setNotes(std::vector<phrasearp::NoteSpan> next, int64_t phraseTicks, int64_t playTick) {
        notes = std::move(next);
        phrase = std::max<int64_t>(1, phraseTicks);
        play = playTick;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff14181f));
        g.setColour(juce::Colour(0xff2a3140));
        for (int i = 0; i <= 4; ++i)
            g.drawVerticalLine(getWidth() * i / 4, 0.f, (float) getHeight());
        if (notes.empty()) {
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.drawText("Init phrase", getLocalBounds(), juce::Justification::centred);
            return;
        }
        int low = 127, high = 0;
        for (const auto& n : notes) {
            low = std::min(low, n.note);
            high = std::max(high, n.note);
        }
        low = std::max(0, low - 2);
        high = std::min(127, high + 2);
        const float span = (float) std::max(1, high - low);
        for (const auto& n : notes) {
            const float x = (float) getWidth() * (float) n.onTick / (float) phrase;
            const float w = std::max(2.f, (float) getWidth() * (float) (n.offTick - n.onTick) / (float) phrase);
            const float y = (float) getHeight() * (1.f - (float) (n.note - low) / span);
            const float h = std::max(4.f, (float) getHeight() / span);
            g.setColour(n.note == notes.back().note ? juce::Colour(0xfff0c674) : juce::Colour(0xff8fd0ff));
            g.fillRoundedRectangle(x, y - h, w, h, 2.f);
        }
        if (play >= 0) {
            const float x = (float) getWidth() * (float) (play % phrase) / (float) phrase;
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawVerticalLine((int) x, 0.f, (float) getHeight());
        }
    }

private:
    std::vector<phrasearp::NoteSpan> notes;
    int64_t phrase = 1;
    int64_t play = 0;
};

class StepStrip : public juce::Component {
public:
    explicit StepStrip(PhraseArpAudioProcessor& proc) : processor(proc) {}

    void setLane(int index) {
        lane = std::clamp(index, 0, (int) std::size(kLanes) - 1);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override { edit(e, true); }
    void mouseDrag(const juce::MouseEvent& e) override { edit(e, false); }
    void mouseUp(const juce::MouseEvent&) override {
        if (changed)
            processor.editState([](phrasearp::AudioState&) {}, true);
        changed = false;
        dragging = false;
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff10141b));
        const auto state = processor.copyState();
        const auto& pattern = phrasearp::activePattern(state);
        const auto& info = kLanes[lane];
        const int n = std::clamp(pattern.type.length, 1, phrasearp::kMaxSteps);
        for (int i = 0; i < n; ++i) {
            const int x0 = getWidth() * i / n;
            const int x1 = getWidth() * (i + 1) / n;
            const int value = readStep(pattern, info.id, i);
            const float u = (float) (value - info.minV) / (float) std::max(1, info.maxV - info.minV);
            const int h = (int) (u * (float) (getHeight() - 16));
            g.setColour(stepLocked(pattern, info.id, i) ? juce::Colour(0xffe0a050) : juce::Colour(0xff3d8bfd));
            g.fillRect(x0 + 1, getHeight() - 14 - h, std::max(1, x1 - x0 - 2), h);
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.setFont(10.f);
            g.drawText(juce::String(value), x0, getHeight() - 14, std::max(1, x1 - x0), 14, juce::Justification::centred);
        }
    }

private:
    void edit(const juce::MouseEvent& e, bool down) {
        const auto state = processor.copyState();
        const int n = std::clamp(phrasearp::activePattern(state).type.length, 1, phrasearp::kMaxSteps);
        const int index = std::clamp(e.x * n / std::max(1, getWidth()), 0, n - 1);
        const auto& info = kLanes[lane];
        const bool lockFlip = down && e.mods.isRightButtonDown();
        int value = info.minV;
        if (!lockFlip) {
            const float u = 1.f - juce::jlimit(0.f, 1.f, (float) e.y / (float) std::max(1, getHeight() - 16));
            value = info.minV + (int) std::lround(u * (float) (info.maxV - info.minV));
        }
        dragging = true;
        changed = true;
        processor.editState([&](phrasearp::AudioState& audio) {
            auto& pattern = audio.patterns[(size_t) std::clamp(audio.activeSlot, 0, phrasearp::kPatternSlots - 1)];
            writeStep(pattern, info.id, index, value, lockFlip);
        }, false);
        repaint();
    }

    PhraseArpAudioProcessor& processor;
    int lane = 0;
    bool dragging = false;
    bool changed = false;
};

class DragHandle : public juce::Component {
public:
    explicit DragHandle(PhraseArpAudioProcessor& proc) : processor(proc) {}

    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff2f6fed));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 4.f);
        g.setColour(juce::Colours::white);
        g.drawText("Drag MIDI", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6)
            return;
        started = true;
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("PhraseArp");
        dir.createDirectory();
        const auto state = processor.copyState();
        auto file = dir.getChildFile(safeFile(state.presetName) + "_" + juce::String(phrasearp::toHex8(state.seed)) + ".mid");
        file = processor.renderMidiFile(file, processor.exportPhrases, processor.exportBakeFeel, processor.exportChords);
        if (file.existsAsFile()) {
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
                container->performExternalDragDropOfFiles({file.getFullPathName()}, false, this);
        }
    }

    void mouseUp(const juce::MouseEvent&) override { started = false; }

private:
    static juce::String safeFile(const juce::String& text) {
        juce::String out;
        for (auto c : text)
            if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_')
                out += c;
        return out.isEmpty() ? "PhraseArp" : out;
    }

    PhraseArpAudioProcessor& processor;
    bool started = false;
};

class PhrasePage : public juce::Component {
public:
    explicit PhrasePage(PhraseArpAudioProcessor& proc) : processor(proc), strip(proc), drag(proc) {
        addAndMakeVisible(seed);
        seed.setFont(juce::FontOptions(16.f, juce::Font::bold));
        seed.setInputRestrictions(8, "0123456789abcdefABCDEF");
        seed.setText(phrasearp::toHex8(processor.copyState().seed), false);
        seed.onReturnKey = [this] { commitSeed(); };
        seed.onFocusLost = [this] { commitSeed(); };

        auto button = [this](juce::TextButton& b, const juce::String& name, auto fn) {
            b.setButtonText(name);
            b.onClick = fn;
            addAndMakeVisible(b);
        };
        button(generate, "Generate", [this] { processor.requestGenerate(); });
        button(evolve, "Evolve", [this] { processor.requestEvolve(); });
        button(undo, "Undo", [this] { processor.undo(); });
        button(redo, "Redo", [this] { processor.redo(); });
        button(star, "Star", [this] { processor.storeActiveIntoSlot(processor.copyState().activeSlot); });
        button(newSeed, "New Seed", [this] { processor.requestNewSeed(); });
        button(exp, "Export", [this] { exportMidi(); });
        button(capture, "Capture", [this] { processor.armCapture(); captureArmed = true; });
        button(import, "Import", [this] { importMidi(); });

        for (int i = 0; i < 8; ++i) {
            slots[(size_t) i].setButtonText(juce::String::charToString((juce::juce_wchar) ('A' + i)));
            slots[(size_t) i].addMouseListener(this, false);
            addAndMakeVisible(slots[(size_t) i]);
        }

        addAndMakeVisible(roll);
        addAndMakeVisible(strip);
        addAndMakeVisible(drag);
        addAndMakeVisible(status);
        status.setJustificationType(juce::Justification::centredLeft);

        laneBox.addItemList({"Pitch", "Octave", "Transpose", "Type", "Velocity", "Gate", "Ratchet", "Probability", "Timing", "CC 1", "CC 2"}, 1);
        laneBox.setSelectedId(1, juce::dontSendNotification);
        laneBox.onChange = [this] { strip.setLane(laneBox.getSelectedId() - 1); refreshLaneMeta(); };
        addAndMakeVisible(laneBox);

        pitchMode.addItemList({"Index", "Scale Degree", "Up", "Down", "Up Down", "Random", "As Played", "Root", "Top"}, 1);
        pitchMode.onChange = [this] {
            if (refreshing)
                return;
            processor.editState([this](phrasearp::AudioState& state) {
                const int slot = std::clamp(state.activeSlot, 0, phrasearp::kPatternSlots - 1);
                state.patterns[(size_t) slot].pitchMode = (phrasearp::PitchMode) (pitchMode.getSelectedId() - 1);
            }, true);
        };
        addAndMakeVisible(pitchMode);

        auto meta = [this](juce::ComboBox& box, const juce::StringArray& items) {
            box.addItemList(items, 1);
            addAndMakeVisible(box);
        };
        meta(speed, {"x1/4", "x1/2", "x1", "x2", "x4"});
        meta(direction, {"Forward", "Back", "Ping Pong", "Random", "Random Walk"});
        meta(reset, {"Never", "Every N Bars", "Phrase", "Chord"});
        speed.onChange = [this] { commitLaneMeta(); };
        direction.onChange = [this] { commitLaneMeta(); };
        reset.onChange = [this] { commitLaneMeta(); };

        lockLane.setButtonText("Lock Lane");
        lockLane.onClick = [this] {
            if (refreshing)
                return;
            const int lane = laneBox.getSelectedId() - 1;
            processor.editState([this, lane](phrasearp::AudioState& state) {
                setLaneLocked(state.patterns[(size_t) state.activeSlot], kLanes[lane].id, lockLane.getToggleState());
            }, true);
        };
        addAndMakeVisible(lockLane);

        length.setRange(1, 64, 1);
        length.setSliderStyle(juce::Slider::LinearHorizontal);
        length.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
        length.onDragEnd = [this] { commitLength(true); };
        length.onValueChange = [this] {
            if (!refreshing && length.isMouseButtonDown())
                commitLength(false);
        };
        addAndMakeVisible(length);

        auto op = [this](juce::TextButton& b, const juce::String& name, int code) {
            b.setButtonText(name);
            b.onClick = [this, code] { processor.laneOp(kLanes[laneBox.getSelectedId() - 1].id, code); };
            addAndMakeVisible(b);
        };
        op(shiftL, "Shift L", phrasearp::kOpShiftLeft);
        op(shiftR, "Shift R", phrasearp::kOpShiftRight);
        op(invert, "Invert", phrasearp::kOpInvert);
        op(reverse, "Reverse", phrasearp::kOpReverse);
        op(randomize, "Randomize", phrasearp::kOpRandomize);
        op(doubled, "Double", phrasearp::kOpDouble);
        copy.setButtonText("Copy");
        copy.onClick = [this] { processor.copyLaneToClipboard(kLanes[laneBox.getSelectedId() - 1].id); };
        paste.setButtonText("Paste");
        paste.onClick = [this] { processor.pasteClipboard(); };
        addAndMakeVisible(copy);
        addAndMakeVisible(paste);

        addKnob(phrasearp::ids::density, "Density");
        addKnob(phrasearp::ids::complexity, "Complexity");
        addKnob(phrasearp::ids::motion, "Motion");
        addKnob(phrasearp::ids::color, "Color");
        addKnob(phrasearp::ids::repetition, "Repetition");
        addKnob(phrasearp::ids::range, "Range");
        addKnob(phrasearp::ids::variation, "Variation");
        addKnob(phrasearp::ids::evolveAmount, "Evolve");

        addChoice(phrasearp::ids::rate, "Rate");
        addChoice(phrasearp::ids::phraseBars, "Phrase Bars");
        addChoice(phrasearp::ids::phraseTemplate, "Template");
        addToggle(phrasearp::ids::audition, "Audition");
        addToggle(phrasearp::ids::preview, "Preview");
        addToggle(phrasearp::ids::latch, "Latch");
        addToggle(phrasearp::ids::thru, "Thru");

        importMode.addItemList({"Rhythm", "Rhythm + Contour", "All"}, 1);
        importMode.setSelectedId(3, juce::dontSendNotification);
        addAndMakeVisible(importMode);
        for (auto& editor : chords) {
            addAndMakeVisible(editor);
            editor.onReturnKey = [this, &editor] { commitChord(editor); };
            editor.onFocusLost = [this, &editor] { commitChord(editor); };
        }
        refreshFromState();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        for (int i = 0; i < 8; ++i) {
            if (e.eventComponent != &slots[(size_t) i])
                continue;
            if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
                processor.storeActiveIntoSlot(i);
            else
                processor.selectSlot(i);
            return;
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto top = area.removeFromTop(28);
        seed.setBounds(top.removeFromLeft(110));
        top.removeFromLeft(6);
        newSeed.setBounds(top.removeFromLeft(90));
        top.removeFromLeft(6);
        generate.setBounds(top.removeFromLeft(90));
        evolve.setBounds(top.removeFromLeft(80));
        undo.setBounds(top.removeFromLeft(64));
        redo.setBounds(top.removeFromLeft(64));
        star.setBounds(top.removeFromLeft(64));
        auto row2 = area.removeFromTop(28);
        for (auto& slot : slots)
            slot.setBounds(row2.removeFromLeft(36).reduced(2));
        row2.removeFromLeft(8);
        for (size_t i = 0; i < toggles.size() && i < 4; ++i)
            toggles[i]->button.setBounds(row2.removeFromLeft(100).reduced(2));
        auto chordsRow = area.removeFromTop(24);
        for (auto& editor : chords)
            editor.setBounds(chordsRow.removeFromLeft(chordsRow.getWidth() / 8).reduced(1, 0));
        roll.setBounds(area.removeFromTop(190).reduced(0, 4));
        auto dragRow = area.removeFromTop(28);
        drag.setBounds(dragRow.removeFromLeft(110).reduced(1));
        exp.setBounds(dragRow.removeFromLeft(80).reduced(1));
        capture.setBounds(dragRow.removeFromLeft(80).reduced(1));
        import.setBounds(dragRow.removeFromLeft(80).reduced(1));
        importMode.setBounds(dragRow.removeFromLeft(150).reduced(1));
        status.setBounds(dragRow);
        auto knobRow = area.removeFromTop(96);
        const int kw = knobRow.getWidth() / std::max(1, (int) knobs.size());
        for (auto& knob : knobs) {
            auto cell = knobRow.removeFromLeft(kw);
            knob->label.setBounds(cell.removeFromTop(16));
            knob->slider.setBounds(cell);
        }
        auto choiceRow = area.removeFromTop(26);
        for (auto& choice : choices)
            choice->box.setBounds(choiceRow.removeFromLeft(160).reduced(2));
        auto laneRow = area.removeFromTop(26);
        laneBox.setBounds(laneRow.removeFromLeft(120).reduced(1));
        pitchMode.setBounds(laneRow.removeFromLeft(130).reduced(1));
        speed.setBounds(laneRow.removeFromLeft(80).reduced(1));
        direction.setBounds(laneRow.removeFromLeft(120).reduced(1));
        reset.setBounds(laneRow.removeFromLeft(120).reduced(1));
        lockLane.setBounds(laneRow.removeFromLeft(100).reduced(1));
        length.setBounds(laneRow);
        auto opRow = area.removeFromTop(26);
        shiftL.setBounds(opRow.removeFromLeft(70).reduced(1));
        shiftR.setBounds(opRow.removeFromLeft(70).reduced(1));
        invert.setBounds(opRow.removeFromLeft(70).reduced(1));
        reverse.setBounds(opRow.removeFromLeft(70).reduced(1));
        randomize.setBounds(opRow.removeFromLeft(90).reduced(1));
        doubled.setBounds(opRow.removeFromLeft(70).reduced(1));
        copy.setBounds(opRow.removeFromLeft(60).reduced(1));
        paste.setBounds(opRow.removeFromLeft(60).reduced(1));
        strip.setBounds(area.reduced(0, 4));
    }

    void refresh() {
        undo.setEnabled(processor.canUndo());
        redo.setEnabled(processor.canRedo());
        const int heard = processor.heardSlot();
        const auto state = processor.copyState();
        for (int i = 0; i < 8; ++i)
            slots[(size_t) i].setToggleState(i == state.activeSlot || i == heard, juce::dontSendNotification);
        status.setText("Playing " + juce::String::charToString((juce::juce_wchar) ('A' + std::clamp(heard, 0, 7)))
                           + "   " + processor.message(),
                       juce::dontSendNotification);
        if (!seed.hasKeyboardFocus(true))
            seed.setText(phrasearp::toHex8(state.seed), juce::dontSendNotification);
        if (processor.stateEpoch() != seenEpoch) {
            seenEpoch = processor.stateEpoch();
            refreshFromState();
        }
        auto cfg = processor.makeConfig(state, {}, 120.0);
        const int64_t phrase = phrasearp::ticksPerBar(4, 4) * std::max(1, cfg.phraseBars);
        roll.setNotes(phrasearp::renderMusic(state, cfg, 0, phrase, 120.0, 4, 4).onsets, phrase, processor.heardTick());
        if (!strip.isMouseButtonDown())
            strip.repaint();
        if (captureArmed && (processor.captureReady() || ++captureWait > 15)) {
            captureArmed = false;
            captureWait = 0;
            saveCapture();
        }
    }

private:
    struct Knob {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Choice {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Toggle {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };

    void addKnob(const char* id, const char* name) {
        auto knob = std::make_unique<Knob>();
        knob->label.setText(name, juce::dontSendNotification);
        knob->label.setJustificationType(juce::Justification::centred);
        knob->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        knob->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, id, knob->slider);
        addAndMakeVisible(knob->label);
        addAndMakeVisible(knob->slider);
        knobs.push_back(std::move(knob));
    }

    void addChoice(const char* id, const char*) {
        auto choice = std::make_unique<Choice>();
        choice->att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.apvts, id, choice->box);
        addAndMakeVisible(choice->box);
        choices.push_back(std::move(choice));
    }

    void addToggle(const char* id, const char* name) {
        auto toggle = std::make_unique<Toggle>();
        toggle->button.setButtonText(name);
        toggle->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, id, toggle->button);
        addAndMakeVisible(toggle->button);
        toggles.push_back(std::move(toggle));
    }

    void commitSeed() {
        uint32_t value = 0;
        if (!phrasearp::fromHex8(seed.getText().toStdString(), value))
            return;
        processor.editState([value](phrasearp::AudioState& state) { state.seed = value; }, true);
    }

    void commitChord(juce::TextEditor& editor) {
        const int index = (int) (&editor - chords.data());
        if (index < 0 || index >= (int) chords.size())
            return;
        std::vector<int> notes;
        if (!parseChordText(editor.getText(), notes)) {
            processor.setMessage("Could not read chord " + juce::String(index + 1));
            return;
        }
        processor.editState([index, notes](phrasearp::AudioState& state) {
            if ((int) state.chords.bars.size() <= index)
                state.chords.bars.resize((size_t) index + 1);
            state.chords.bars[(size_t) index].set = !notes.empty();
            state.chords.bars[(size_t) index].notes = notes;
        }, true);
    }

    void refreshFromState() {
        refreshing = true;
        const auto state = processor.copyState();
        for (int i = 0; i < (int) chords.size(); ++i) {
            if (chords[(size_t) i].hasKeyboardFocus(true))
                continue;
            if (i < (int) state.chords.bars.size() && state.chords.bars[(size_t) i].set)
                chords[(size_t) i].setText(chordText(state.chords.bars[(size_t) i].notes), false);
            else
                chords[(size_t) i].setText({}, false);
        }
        const auto& pattern = phrasearp::activePattern(state);
        pitchMode.setSelectedId((int) pattern.pitchMode + 1, juce::dontSendNotification);
        refreshLaneMeta();
        refreshing = false;
    }

    void refreshLaneMeta() {
        refreshing = true;
        const auto state = processor.copyState();
        const auto& pattern = phrasearp::activePattern(state);
        const int lane = std::clamp(laneBox.getSelectedId() - 1, 0, (int) std::size(kLanes) - 1);
        int len = 16, spd = 0, dir = 0, res = 0;
        bool locked = false;
        readLaneMeta(pattern, kLanes[lane].id, len, spd, dir, res, locked);
        speed.setSelectedId(spd + 1, juce::dontSendNotification);
        direction.setSelectedId(dir + 1, juce::dontSendNotification);
        reset.setSelectedId(res + 1, juce::dontSendNotification);
        lockLane.setToggleState(locked, juce::dontSendNotification);
        length.setValue(len, juce::dontSendNotification);
        refreshing = false;
        strip.setLane(lane);
    }

    void commitLaneMeta() {
        if (refreshing)
            return;
        const int lane = std::clamp(laneBox.getSelectedId() - 1, 0, (int) std::size(kLanes) - 1);
        const int spd = speed.getSelectedId() - 1;
        const int dir = direction.getSelectedId() - 1;
        const int res = reset.getSelectedId() - 1;
        processor.editState([lane, spd, dir, res](phrasearp::AudioState& state) {
            writeLaneMeta(state.patterns[(size_t) state.activeSlot], kLanes[lane].id, spd, dir, res);
        }, true);
    }

    void commitLength(bool history) {
        if (refreshing)
            return;
        const int lane = std::clamp(laneBox.getSelectedId() - 1, 0, (int) std::size(kLanes) - 1);
        const int n = (int) length.getValue();
        processor.editState([lane, n](phrasearp::AudioState& state) {
            resizeOne(state.patterns[(size_t) state.activeSlot], kLanes[lane].id, n);
        }, history);
    }

    static void readLaneMeta(const phrasearp::Pattern& pattern, phrasearp::LaneId id, int& len, int& spd, int& dir, int& res, bool& locked) {
        auto take = [&](const auto& lane) {
            len = lane.length;
            spd = (int) lane.speed;
            dir = (int) lane.direction;
            res = (int) lane.reset;
            locked = lane.locked;
        };
        switch (id) {
            case phrasearp::LaneId::Pitch: take(pattern.pitch); break;
            case phrasearp::LaneId::Octave: take(pattern.octave); break;
            case phrasearp::LaneId::Transpose: take(pattern.transpose); break;
            case phrasearp::LaneId::Type: take(pattern.type); break;
            case phrasearp::LaneId::Velocity: take(pattern.velocity); break;
            case phrasearp::LaneId::Gate: take(pattern.gate); break;
            case phrasearp::LaneId::Ratchet: take(pattern.ratchet); break;
            case phrasearp::LaneId::Probability: take(pattern.probability); break;
            case phrasearp::LaneId::Timing: take(pattern.timing); break;
            case phrasearp::LaneId::CC1: take(pattern.cc1); break;
            case phrasearp::LaneId::CC2: take(pattern.cc2); break;
            case phrasearp::LaneId::Count: break;
        }
    }

    static void writeLaneMeta(phrasearp::Pattern& pattern, phrasearp::LaneId id, int spd, int dir, int res) {
        auto put = [&](auto& lane) {
            lane.speed = (phrasearp::LaneSpeed) std::clamp(spd, 0, 4);
            lane.direction = (phrasearp::Direction) std::clamp(dir, 0, 4);
            lane.reset = (phrasearp::ResetMode) std::clamp(res, 0, 3);
        };
        switch (id) {
            case phrasearp::LaneId::Pitch: put(pattern.pitch); break;
            case phrasearp::LaneId::Octave: put(pattern.octave); break;
            case phrasearp::LaneId::Transpose: put(pattern.transpose); break;
            case phrasearp::LaneId::Type: put(pattern.type); break;
            case phrasearp::LaneId::Velocity: put(pattern.velocity); break;
            case phrasearp::LaneId::Gate: put(pattern.gate); break;
            case phrasearp::LaneId::Ratchet: put(pattern.ratchet); break;
            case phrasearp::LaneId::Probability: put(pattern.probability); break;
            case phrasearp::LaneId::Timing: put(pattern.timing); break;
            case phrasearp::LaneId::CC1: put(pattern.cc1); break;
            case phrasearp::LaneId::CC2: put(pattern.cc2); break;
            case phrasearp::LaneId::Count: break;
        }
    }

    static void setLaneLocked(phrasearp::Pattern& pattern, phrasearp::LaneId id, bool locked) {
        auto put = [&](auto& lane) { lane.locked = locked; };
        switch (id) {
            case phrasearp::LaneId::Pitch: put(pattern.pitch); break;
            case phrasearp::LaneId::Octave: put(pattern.octave); break;
            case phrasearp::LaneId::Transpose: put(pattern.transpose); break;
            case phrasearp::LaneId::Type: put(pattern.type); break;
            case phrasearp::LaneId::Velocity: put(pattern.velocity); break;
            case phrasearp::LaneId::Gate: put(pattern.gate); break;
            case phrasearp::LaneId::Ratchet: put(pattern.ratchet); break;
            case phrasearp::LaneId::Probability: put(pattern.probability); break;
            case phrasearp::LaneId::Timing: put(pattern.timing); break;
            case phrasearp::LaneId::CC1: put(pattern.cc1); break;
            case phrasearp::LaneId::CC2: put(pattern.cc2); break;
            case phrasearp::LaneId::Count: break;
        }
    }

    static void resizeOne(phrasearp::Pattern& pattern, phrasearp::LaneId id, int n) {
        n = std::clamp(n, 1, phrasearp::kMaxSteps);
        auto put = [&](auto& lane, auto fill) {
            lane.length = n;
            if ((int) lane.steps.size() < n)
                lane.steps.resize((size_t) n, fill);
            else
                lane.steps.resize((size_t) n);
            lane.locks.resize((size_t) n, 0);
        };
        switch (id) {
            case phrasearp::LaneId::Pitch: put(pattern.pitch, (int16_t) 0); break;
            case phrasearp::LaneId::Octave: put(pattern.octave, (int8_t) 0); break;
            case phrasearp::LaneId::Transpose: put(pattern.transpose, (int8_t) 0); break;
            case phrasearp::LaneId::Type: put(pattern.type, (uint8_t) phrasearp::StepType::Rest); break;
            case phrasearp::LaneId::Velocity: put(pattern.velocity, (uint8_t) 96); break;
            case phrasearp::LaneId::Gate: put(pattern.gate, (uint16_t) 90); break;
            case phrasearp::LaneId::Ratchet: put(pattern.ratchet, (uint8_t) 1); break;
            case phrasearp::LaneId::Probability: put(pattern.probability, (uint8_t) 100); break;
            case phrasearp::LaneId::Timing: put(pattern.timing, (int8_t) 0); break;
            case phrasearp::LaneId::CC1: put(pattern.cc1, (uint8_t) 64); break;
            case phrasearp::LaneId::CC2: put(pattern.cc2, (uint8_t) 0); break;
            case phrasearp::LaneId::Count: break;
        }
        if (pattern.accent.size() != pattern.velocity.steps.size())
            pattern.accent.resize(pattern.velocity.steps.size(), 0);
    }

    void exportMidi() {
        const auto state = processor.copyState();
        chooser = std::make_unique<juce::FileChooser>("Export MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(state.presetName + ".mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& dialog) {
                                 auto file = dialog.getResult();
                                 if (file == juce::File())
                                     return;
                                 if (!file.hasFileExtension("mid"))
                                     file = file.withFileExtension("mid");
                                 processor.renderMidiFile(file, processor.exportPhrases, processor.exportBakeFeel, processor.exportChords);
                                 processor.setMessage("Exported " + file.getFileName());
                             });
    }

    void saveCapture() {
        chooser = std::make_unique<juce::FileChooser>("Capture MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("PhraseArp-capture.mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& dialog) {
                                 auto file = dialog.getResult();
                                 if (file == juce::File())
                                     return;
                                 if (!file.hasFileExtension("mid"))
                                     file = file.withFileExtension("mid");
                                 processor.writeArmedCapture(file);
                                 processor.setMessage("Captured " + file.getFileName());
                             });
    }

    void importMidi() {
        chooser = std::make_unique<juce::FileChooser>("Import MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& dialog) {
                                 auto file = dialog.getResult();
                                 if (file == juce::File())
                                     return;
                                 const auto mode = (phrasearp::ImportMode) std::clamp(importMode.getSelectedId() - 1, 0, 2);
                                 processor.importMidi(file, mode);
                             });
    }

    PhraseArpAudioProcessor& processor;
    juce::TextEditor seed;
    juce::TextButton generate, evolve, undo, redo, star, newSeed, exp, capture, import, shiftL, shiftR, invert, reverse, randomize, doubled, copy, paste;
    std::array<juce::TextButton, 8> slots;
    PianoRoll roll;
    StepStrip strip;
    DragHandle drag;
    juce::Label status;
    juce::ComboBox laneBox, pitchMode, speed, direction, reset, importMode;
    juce::ToggleButton lockLane;
    juce::Slider length;
    std::array<juce::TextEditor, 8> chords;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::unique_ptr<juce::FileChooser> chooser;
    uint32_t seenEpoch = 0;
    bool refreshing = false;
    bool captureArmed = false;
    int captureWait = 0;
};

class OptionsPage : public juce::Component {
public:
    explicit OptionsPage(PhraseArpAudioProcessor& proc) : processor(proc) {
        auto addKnob = [this](const char* id, const char* name) {
            auto knob = std::make_unique<Knob>();
            knob->label.setText(name, juce::dontSendNotification);
            knob->slider.setSliderStyle(juce::Slider::LinearHorizontal);
            knob->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
            knob->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, id, knob->slider);
            addAndMakeVisible(knob->label);
            addAndMakeVisible(knob->slider);
            knobs.push_back(std::move(knob));
        };
        auto addChoice = [this](const char* id) {
            auto choice = std::make_unique<Choice>();
            choice->att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.apvts, id, choice->box);
            addAndMakeVisible(choice->box);
            choices.push_back(std::move(choice));
        };
        auto addToggle = [this](const char* id, const char* name) {
            auto toggle = std::make_unique<Toggle>();
            toggle->button.setButtonText(name);
            toggle->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, id, toggle->button);
            addAndMakeVisible(toggle->button);
            toggles.push_back(std::move(toggle));
        };
        addKnob(phrasearp::ids::swing, "Swing");
        addKnob(phrasearp::ids::humanizeMs, "Humanize ms");
        addKnob(phrasearp::ids::humanizeVel, "Humanize Velocity");
        addKnob(phrasearp::ids::humanizeGate, "Humanize Gate");
        addKnob(phrasearp::ids::accent, "Accent");
        addKnob(phrasearp::ids::maxJump, "Max Jump");
        addKnob(phrasearp::ids::strumMs, "Strum");
        addKnob(phrasearp::ids::strumTilt, "Strum Tilt");
        addKnob(phrasearp::ids::fixedMs, "Fixed Length");
        addKnob(phrasearp::ids::lowNote, "Low Note");
        addKnob(phrasearp::ids::highNote, "High Note");
        addKnob(phrasearp::ids::octaves, "Octaves");
        addChoice(phrasearp::ids::keyRoot);
        addChoice(phrasearp::ids::scale);
        addChoice(phrasearp::ids::groove);
        addChoice(phrasearp::ids::contour);
        addChoice(phrasearp::ids::dynamics);
        addChoice(phrasearp::ids::retrigger);
        addChoice(phrasearp::ids::boundary);
        addChoice(phrasearp::ids::noteLength);
        addChoice(phrasearp::ids::strumDir);
        addChoice(phrasearp::ids::ratchetCurve);
        addChoice(phrasearp::ids::slotQuantize);
        addChoice(phrasearp::ids::autoEvolve);
        addToggle(phrasearp::ids::questionAnswer, "Question Answer");
        addToggle(phrasearp::ids::twoBar, "Two Bar Unit");
        addToggle(phrasearp::ids::humanizeLive, "Humanize Live");
        addToggle(phrasearp::ids::swingEighth, "Swing 1/8");
        addToggle(phrasearp::ids::euclidean, "Euclidean");
        addToggle(phrasearp::ids::autoKey, "Auto Key");
        addToggle(phrasearp::ids::ccSmooth, "CC Smooth");
        addToggle(phrasearp::ids::chordTrack, "Chord Track");
        for (int i = 0; i < 8; ++i) {
            addKnob(phrasearp::ids::macro(i), (juce::String("Macro ") + juce::String(i + 1)).toRawUTF8());
            targets[(size_t) i].addItemList({"None", "Density", "Complexity", "Motion", "Color", "Repetition", "Range", "Variation", "Swing", "Strum"}, 1);
            addAndMakeVisible(targets[(size_t) i]);
            targets[(size_t) i].onChange = [this, i] {
                if (refreshing)
                    return;
                processor.editState([this, i](phrasearp::AudioState& state) {
                    state.macroTarget[(size_t) i] = targets[(size_t) i].getSelectedId() - 1;
                }, true);
            };
            depths[(size_t) i].setRange(0.0, 1.0, 0.01);
            depths[(size_t) i].setSliderStyle(juce::Slider::LinearHorizontal);
            depths[(size_t) i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
            depths[(size_t) i].onDragEnd = [this, i] {
                processor.editState([this, i](phrasearp::AudioState& state) {
                    state.macroDepth[(size_t) i] = (float) depths[(size_t) i].getValue();
                }, true);
            };
            addAndMakeVisible(depths[(size_t) i]);
        }
        keySwitch.setButtonText("Key Switch C1-G1");
        keySwitch.onClick = [this] {
            if (refreshing)
                return;
            processor.editState([this](phrasearp::AudioState& state) {
                state.keySwitchEnabled = keySwitch.getToggleState();
                state.keySwitchLow = 24;
                state.keySwitchHigh = 31;
            }, true);
        };
        addAndMakeVisible(keySwitch);
        phrases.setRange(1, 8, 1);
        phrases.setSliderStyle(juce::Slider::LinearHorizontal);
        phrases.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
        phrases.setValue(processor.exportPhrases, juce::dontSendNotification);
        phrases.onDragEnd = [this] { processor.exportPhrases = (int) phrases.getValue(); };
        addAndMakeVisible(phrases);
        bake.setButtonText("Export Bake Feel");
        bake.setToggleState(processor.exportBakeFeel, juce::dontSendNotification);
        bake.onClick = [this] { processor.exportBakeFeel = bake.getToggleState(); };
        chords.setButtonText("Export Chords");
        chords.setToggleState(processor.exportChords, juce::dontSendNotification);
        chords.onClick = [this] { processor.exportChords = chords.getToggleState(); };
        addAndMakeVisible(bake);
        addAndMakeVisible(chords);
        phraseLabel.setText("Export phrases", juce::dontSendNotification);
        addAndMakeVisible(phraseLabel);
        setSize(1040, 980);
        refresh();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        for (size_t i = 0; i < knobs.size(); ++i) {
            if (i == 8 || i == 18)
                area.removeFromTop(8);
            auto row = area.removeFromTop(26);
            knobs[i]->label.setBounds(row.removeFromLeft(150));
            knobs[i]->slider.setBounds(row);
        }
        area.removeFromTop(8);
        for (auto& choice : choices) {
            auto row = area.removeFromTop(26);
            choice->box.setBounds(row.removeFromLeft(280));
        }
        area.removeFromTop(8);
        auto togglesArea = area.removeFromTop(70);
        int x = 0, y = 0;
        for (auto& toggle : toggles) {
            toggle->button.setBounds(togglesArea.getX() + x, togglesArea.getY() + y, 180, 24);
            x += 180;
            if (x > 700) {
                x = 0;
                y += 26;
            }
        }
        area.removeFromTop(78);
        for (int i = 0; i < 8; ++i) {
            auto row = area.removeFromTop(26);
            targets[(size_t) i].setBounds(row.removeFromLeft(180).reduced(1));
            depths[(size_t) i].setBounds(row.removeFromLeft(220));
        }
        auto row = area.removeFromTop(28);
        keySwitch.setBounds(row.removeFromLeft(180));
        phraseLabel.setBounds(row.removeFromLeft(120));
        phrases.setBounds(row.removeFromLeft(220));
        bake.setBounds(row.removeFromLeft(160));
        chords.setBounds(row.removeFromLeft(140));
    }

    void refresh() {
        if (processor.stateEpoch() == seen)
            return;
        seen = processor.stateEpoch();
        refreshing = true;
        const auto state = processor.copyState();
        for (int i = 0; i < 8; ++i) {
            if (!targets[(size_t) i].hasKeyboardFocus(true))
                targets[(size_t) i].setSelectedId(state.macroTarget[(size_t) i] + 1, juce::dontSendNotification);
            if (!depths[(size_t) i].isMouseButtonDown())
                depths[(size_t) i].setValue(state.macroDepth[(size_t) i], juce::dontSendNotification);
        }
        keySwitch.setToggleState(state.keySwitchEnabled, juce::dontSendNotification);
        refreshing = false;
    }

private:
    struct Knob {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Choice {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Toggle {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };

    PhraseArpAudioProcessor& processor;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::array<juce::ComboBox, 8> targets;
    std::array<juce::Slider, 8> depths;
    juce::ToggleButton keySwitch, bake, chords;
    juce::Slider phrases;
    juce::Label phraseLabel;
    uint32_t seen = 0;
    bool refreshing = false;
};

class SetupPage : public juce::Component {
public:
    explicit SetupPage(PhraseArpAudioProcessor& proc) : processor(proc) {
        help.setMultiLine(true);
        help.setReadOnly(true);
        help.setCaretVisible(false);
        help.setText(
            "FL Studio 25 MIDI output\n\n"
            "Host MIDI output was not verified in this build. FL Studio 25 is not available on the machine that produced this plugin, "
            "so treat the steps below as the routing to try, not as a tested result.\n\n"
            "1. Load PhraseArp on an instrument channel. It is a VST3 instrument (IS_SYNTH), not a MIDI effect.\n"
            "2. Open the wrapper settings for that plugin (the channel's plugin options, or the VST wrapper menu).\n"
            "3. Set PhraseArp's MIDI Output port to a free number, for example 1.\n"
            "4. On the target instrument, set MIDI Input port to that same number.\n"
            "5. Leave Thru off unless the held chord itself should also reach the target. CC64 is the latch pedal and is not passed through.\n"
            "6. Press play. If the host keeps processing while stopped, Audition plays the chord track on an internal clock so the Init phrase is audible immediately. "
            "If the host suspends a stopped instrument, start the transport or play a note.\n"
            "7. Drag the Drag MIDI handle into the piano roll or the playlist. The file is the phrase from its beginning, with tempo and time signature, "
            "matching what PhraseArp plays for the same chord track. Export writes the same file. Capture writes the last 64 bars that were actually sent.\n\n"
            "The preview instrument does not depend on host MIDI routing. Windows and macOS binaries were not built here; this tree was configured and built on Linux.",
            juce::dontSendNotification);
        addAndMakeVisible(help);
        auto addKnob = [this](const char* id, const char* name) {
            auto knob = std::make_unique<Knob>();
            knob->label.setText(name, juce::dontSendNotification);
            knob->slider.setSliderStyle(juce::Slider::LinearHorizontal);
            knob->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
            knob->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, id, knob->slider);
            addAndMakeVisible(knob->label);
            addAndMakeVisible(knob->slider);
            knobs.push_back(std::move(knob));
        };
        addKnob(phrasearp::ids::previewVolume, "Preview Volume");
        addKnob(phrasearp::ids::previewDecay, "Preview Decay");
        addKnob(phrasearp::ids::outputChannel, "Output Channel");
        addKnob(phrasearp::ids::cc1, "CC 1");
        addKnob(phrasearp::ids::cc2, "CC 2");
        timbre.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.apvts, phrasearp::ids::timbre, timbre.box);
        addAndMakeVisible(timbre.box);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        help.setBounds(area.removeFromTop(360));
        timbre.box.setBounds(area.removeFromTop(28).removeFromLeft(220));
        for (auto& knob : knobs) {
            auto row = area.removeFromTop(28);
            knob->label.setBounds(row.removeFromLeft(150));
            knob->slider.setBounds(row.removeFromLeft(360));
        }
    }

private:
    struct Knob {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Choice {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };

    PhraseArpAudioProcessor& processor;
    juce::TextEditor help;
    std::vector<std::unique_ptr<Knob>> knobs;
    Choice timbre;
};

class PhraseArpAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit PhraseArpAudioProcessorEditor(PhraseArpAudioProcessor& proc)
        : AudioProcessorEditor(&proc), processor(proc), phrase(proc), options(proc), setup(proc), tabs(juce::TabbedButtonBar::TabsAtTop) {
        look.setColourScheme(juce::LookAndFeel_V4::getDarkColourScheme());
        setLookAndFeel(&look);
        addAndMakeVisible(tabs);
        tabs.addTab("Phrase", juce::Colour(0xff1b212c), &phrase, false);
        viewport.setViewedComponent(&options, false);
        viewport.setScrollBarsShown(true, false);
        tabs.addTab("Options", juce::Colour(0xff1b212c), &viewport, false);
        tabs.addTab("Setup", juce::Colour(0xff1b212c), &setup, false);
        setResizeLimits(860, 640, 1400, 1000);
        setSize(1100, 760);
        setWantsKeyboardFocus(true);
        startTimerHz(12);
    }

    ~PhraseArpAudioProcessorEditor() override { setLookAndFeel(nullptr); }

    void resized() override { tabs.setBounds(getLocalBounds()); }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff10141b)); }

    void timerCallback() override {
        phrase.refresh();
        options.refresh();
        if (keySwitchSeen != processor.copyState().keySwitchEnabled)
            keySwitchSeen = processor.copyState().keySwitchEnabled;
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent()) {
            if (dynamic_cast<juce::TextEditor*>(focused) != nullptr || dynamic_cast<juce::ComboBox*>(focused) != nullptr
                || dynamic_cast<juce::Slider*>(focused) != nullptr)
                return false;
        }
        const auto desc = key.getTextCharacter();
        if (desc == 'z' || desc == 'Z') {
            octave = std::max(0, octave - 1);
            return true;
        }
        if (desc == 'x' || desc == 'X') {
            octave = std::min(8, octave + 1);
            return true;
        }
        const int note = keyToNote(desc);
        if (note < 0)
            return false;
        if (!held[(size_t) note]) {
            held[(size_t) note] = true;
            processor.injectMidi(note, 100, true);
        }
        return true;
    }

    bool keyStateChanged(bool) override {
        for (int note = 0; note < 128; ++note) {
            if (!held[(size_t) note])
                continue;
            // Releases are observed by checking the physical keys that map here.
        }
        releaseIfUp();
        return false;
    }

private:
    int keyToNote(juce::juce_wchar c) const {
        int semi = -1;
        switch (c) {
            case 'a': case 'A': semi = 0; break;
            case 'w': case 'W': semi = 1; break;
            case 's': case 'S': semi = 2; break;
            case 'e': case 'E': semi = 3; break;
            case 'd': case 'D': semi = 4; break;
            case 'f': case 'F': semi = 5; break;
            case 't': case 'T': semi = 6; break;
            case 'g': case 'G': semi = 7; break;
            case 'y': case 'Y': semi = 8; break;
            case 'h': case 'H': semi = 9; break;
            case 'u': case 'U': semi = 10; break;
            case 'j': case 'J': semi = 11; break;
            case 'k': case 'K': semi = 12; break;
            default: break;
        }
        if (semi < 0)
            return -1;
        return std::clamp(octave * 12 + semi, 0, 127);
    }

    void releaseIfUp() {
        static const char* keys = "awsedftgyhujk";
        static const int semis[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        for (int i = 0; keys[i] != 0; ++i) {
            const int note = std::clamp(octave * 12 + semis[i], 0, 127);
            const bool down = juce::KeyPress::isKeyCurrentlyDown((int) keys[i])
                              || juce::KeyPress::isKeyCurrentlyDown((int) (keys[i] - 32));
            if (held[(size_t) note] && !down) {
                held[(size_t) note] = false;
                processor.injectMidi(note, 0, false);
            }
        }
    }

    PhraseArpAudioProcessor& processor;
    juce::LookAndFeel_V4 look;
    PhrasePage phrase;
    OptionsPage options;
    SetupPage setup;
    juce::Viewport viewport;
    juce::TabbedComponent tabs;
    int octave = 4;
    std::array<bool, 128> held {};
    bool keySwitchSeen = false;
};

} // namespace

juce::AudioProcessorEditor* PhraseArpAudioProcessor::createEditor() {
    return new PhraseArpAudioProcessorEditor(*this);
}
