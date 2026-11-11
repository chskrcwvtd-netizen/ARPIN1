#pragma once

#include "engine/Importer.h"
#include "engine/LockFreeSwap.h"
#include "engine/PreviewSynth.h"
#include "engine/Render.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class PhraseArpAudioProcessor : public juce::AudioProcessor {
public:
    PhraseArpAudioProcessor();
    ~PhraseArpAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return JucePlugin_WantsMidiInput; }
    bool producesMidi() const override { return JucePlugin_ProducesMidiOutput; }
    bool isMidiEffect() const override { return JucePlugin_IsMidiEffect; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState apvts;

    void requestGenerate();
    void requestEvolve();
    void requestNewSeed();
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    void editState(const std::function<void(phrasearp::AudioState&)>& edit, bool recordHistory);
    phrasearp::AudioState copyState() const;
    uint32_t stateEpoch() const { return epoch.load(std::memory_order_acquire); }

    void selectSlot(int slot);
    void storeActiveIntoSlot(int slot);
    int heardSlot() const { return heardSlotAtomic.load(std::memory_order_acquire); }
    int64_t heardTick() const { return heardTickAtomic.load(std::memory_order_acquire); }

    phrasearp::RenderConfig makeConfig(const phrasearp::AudioState& state, const std::vector<int>& held, double bpm) const;
    phrasearp::GenerateSettings makeGenerateSettings(const phrasearp::AudioState& state) const;

    juce::File renderMidiFile(const juce::File& dest, int phrases, bool bakeFeel, bool includeChords) const;
    void armCapture();
    bool captureReady() const { return captureState.load(std::memory_order_acquire) == 2; }
    juce::File writeArmedCapture(const juce::File& dest);

    void importMidi(const juce::File& file, phrasearp::ImportMode mode);
    void injectMidi(int note, int velocity, bool on);

    void setMessage(juce::String text);
    juce::String message() const;

    int exportPhrases = 1;
    bool exportBakeFeel = true;
    bool exportChords = false;

    void laneOp(phrasearp::LaneId lane, int op);
    void copyLaneToClipboard(phrasearp::LaneId lane);
    void pasteClipboard();

private:
    struct LiveNote {
        int note = 60;
        int channel = 0;
        int64_t offTick = 0;
    };
    struct CapEv {
        int64_t tick = 0;
        uint8_t kind = 0;
        uint8_t channel = 0;
        uint8_t note = 0;
        uint8_t value = 0;
    };
    struct Inj {
        int note = 60;
        int velocity = 0;
        int on = 0;
    };

    void workerLoop();
    void runGenerate();
    void runEvolve();
    void publish(const phrasearp::AudioState& state);
    float raw(const char* id) const;
    int choice(const char* id) const;
    bool flag(const char* id) const;
    float shaped(float base, float lo, float hi, phrasearp::MacroTarget target, const phrasearp::AudioState& state) const;
    void panic(juce::MidiBuffer& midi, int sample);
    void capture(const CapEv& event);
    std::vector<int> chordFromKeys() const;
    void loadHeld(std::vector<int>& dest) const;

    mutable std::mutex stateMutex;
    phrasearp::AudioState uiState;
    phrasearp::History history;
    phrasearp::LaneClipboard clipboard;
    phrasearp::LockFreeSwap<phrasearp::AudioState> swap;
    std::atomic<uint32_t> epoch { 1 };
    std::atomic<bool> workerStop { false };
    std::atomic<bool> generateRequest { false };
    std::atomic<bool> evolveRequest { false };
    std::thread worker;

    phrasearp::AudioState playing;
    phrasearp::PreviewSynth synth;
    std::vector<LiveNote> liveNotes;
    std::array<uint8_t, 128> keyDown {};
    std::array<uint8_t, 128> sustained {};
    bool pedal = false;
    std::vector<int> liveHeld;
    std::vector<int> pendingHeld;
    std::vector<int> latchedChord;
    int64_t pendingChordTick = -1;
    int64_t internalTick = 0;
    double tickFraction = 0.0;
    bool internalClock = false;
    bool haveHostTick = false;
    int64_t expectedTick = 0;
    int lastPhraseIndex = -1;

    double sampleRate = 44100.0;
    std::atomic<float> lastBpm { 120.f };
    std::atomic<int> lastSigNum { 4 };
    std::atomic<int> lastSigDen { 4 };
    std::atomic<int> heardSlotAtomic { 0 };
    std::atomic<int64_t> heardTickAtomic { 0 };
    std::atomic<int> keySwitchSlot { -1 };

    std::atomic<uint32_t> heldSeq { 0 };
    int heldSnap[16] {};
    int heldSnapCount = 0;

    static constexpr int kCap = 8192;
    CapEv capRing[kCap] {};
    std::atomic<uint32_t> capWrite { 0 };
    CapEv capExport[kCap] {};
    int capExportCount = 0;
    int64_t capExportBegin = 0;
    double capExportBpm = 120.0;
    int capExportNum = 4;
    int capExportDen = 4;
    std::atomic<int> captureState { 0 };

    Inj injectQueue[128] {};
    std::atomic<int> injectHead { 0 };
    std::atomic<int> injectTail { 0 };
    std::atomic<uint32_t> seedCounter { 1 };

    mutable std::mutex messageMutex;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseArpAudioProcessor)
};
