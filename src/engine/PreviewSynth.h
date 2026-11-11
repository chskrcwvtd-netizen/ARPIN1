#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace phrasearp {

enum class Timbre : uint8_t { Pluck = 0, Bell, Keys };

struct SynthEvent {
    int sample = 0;
    int note = 60;
    int velocity = 100;
    bool on = true;
};

// 16-voice preview. Pluck is additive, bell is two-operator FM, keys are a soft attack.
class PreviewSynth {
public:
    void setSampleRate(double sampleRate);
    void noteOn(int note, int velocity, Timbre timbre, float decay);
    void noteOff(int note);
    void allOff();
    void render(float* left, float* right, int samples, float volume, bool enabled);
    void renderEvents(float* left, float* right, int samples, const std::vector<SynthEvent>& events, Timbre timbre,
                      float decay, float volume, bool enabled);

private:
    struct Voice {
        bool active = false;
        int note = 60;
        Timbre timbre = Timbre::Pluck;
        double phase = 0;
        double modPhase = 0;
        double amp = 0;
        double decay = 0.002;
        double attack = 1;
        double velocity = 0.8;
        double pan = 0;
        bool releasing = false;
    };

    double sampleRate_ = 44100.0;
    double gain_ = 0;
    std::array<Voice, 16> voices_ {};
};

} // namespace phrasearp
