#include "engine/PreviewSynth.h"

#include <algorithm>
#include <cmath>

namespace phrasearp {
namespace {

double midiFreq(int note) {
    return 440.0 * std::pow(2.0, (std::clamp(note, 0, 127) - 69) / 12.0);
}

} // namespace

void PreviewSynth::setSampleRate(double sampleRate) {
    if (sampleRate < 1000.0)
        sampleRate = 44100.0;
    sampleRate_ = sampleRate;
}

void PreviewSynth::noteOn(int note, int velocity, Timbre timbre, float decay) {
    Voice* slot = nullptr;
    for (auto& v : voices_) {
        if (!v.active) {
            slot = &v;
            break;
        }
    }
    if (slot == nullptr) {
        slot = &voices_[0];
        double quiet = 1e9;
        for (auto& v : voices_) {
            if (v.amp < quiet) {
                quiet = v.amp;
                slot = &v;
            }
        }
    }
    const float d = std::clamp(decay, 0.f, 1.f);
    const double seconds = 0.12 + (double) d * 2.4;
    slot->active = true;
    slot->note = note;
    slot->timbre = timbre;
    slot->phase = 0;
    slot->modPhase = 0;
    slot->amp = 1;
    slot->decay = 1.0 / (seconds * sampleRate_);
    slot->attack = timbre == Timbre::Keys ? 0 : 1;
    slot->velocity = std::clamp(velocity, 1, 127) / 127.0;
    slot->pan = std::clamp((note - 60) * 0.015, -0.35, 0.35);
    slot->releasing = false;
}

void PreviewSynth::noteOff(int note) {
    for (auto& v : voices_) {
        if (v.active && v.note == note && !v.releasing)
            v.releasing = true;
    }
}

void PreviewSynth::allOff() {
    for (auto& v : voices_)
        v.active = false;
    gain_ = 0;
}

void PreviewSynth::render(float* left, float* right, int samples, float volume, bool enabled) {
    if (left == nullptr || right == nullptr || samples <= 0)
        return;
    const float target = enabled ? std::clamp(volume, 0.f, 1.f) : 0.f;
    for (int i = 0; i < samples; ++i) {
        gain_ += ((double) target - gain_) * 0.002;
        double mixL = 0;
        double mixR = 0;
        for (auto& v : voices_) {
            if (!v.active)
                continue;
            const double freq = midiFreq(v.note);
            const double inc = freq / sampleRate_;
            v.phase += inc;
            if (v.phase >= 1.0)
                v.phase -= std::floor(v.phase);
            double sample = 0;
            if (v.timbre == Timbre::Bell) {
                v.modPhase += inc * 3.5;
                if (v.modPhase >= 1.0)
                    v.modPhase -= std::floor(v.modPhase);
                const double modEnv = std::pow(std::max(0.0, v.amp), 1.6);
                sample = std::sin(2.0 * 3.141592653589793 * v.phase + 1.8 * modEnv * std::sin(2.0 * 3.141592653589793 * v.modPhase));
            } else if (v.timbre == Timbre::Keys) {
                v.attack += (1.0 - v.attack) * 0.004;
                sample = v.attack * (std::sin(2.0 * 3.141592653589793 * v.phase)
                                     + 0.25 * std::sin(4.0 * 3.141592653589793 * v.phase));
            } else {
                const double harm = std::exp(-v.amp * -6.0); // brighter at the attack
                const double bright = std::min(1.0, (1.0 - v.amp) * 4.0);
                sample = std::sin(2.0 * 3.141592653589793 * v.phase)
                         + 0.35 * (1.0 - bright) * std::sin(4.0 * 3.141592653589793 * v.phase);
                (void) harm;
            }
            if (v.timbre == Timbre::Keys)
                v.amp *= (1.0 - v.decay * 0.35);
            else
                v.amp *= (1.0 - v.decay);
            if (v.releasing)
                v.amp *= 0.996;
            if (v.amp < 1.0e-4) {
                v.active = false;
                continue;
            }
            const double shaped = sample * v.amp * v.velocity;
            const double l = shaped * (1.0 - v.pan);
            const double r = shaped * (1.0 + v.pan);
            mixL += l;
            mixR += r;
        }
        const double outL = std::tanh(mixL * 0.35) * gain_;
        const double outR = std::tanh(mixR * 0.35) * gain_;
        left[i] += (float) outL;
        right[i] += (float) outR;
    }
}

void PreviewSynth::renderEvents(float* left, float* right, int samples, const std::vector<SynthEvent>& events, Timbre timbre,
                                float decay, float volume, bool enabled) {
    if (samples <= 0)
        return;
    int cursor = 0;
    for (const auto& event : events) {
        const int at = std::clamp(event.sample, 0, samples);
        if (at > cursor)
            render(left + cursor, right + cursor, at - cursor, volume, enabled);
        if (event.on)
            noteOn(event.note, event.velocity, timbre, decay);
        else
            noteOff(event.note);
        cursor = at;
    }
    if (cursor < samples)
        render(left + cursor, right + cursor, samples - cursor, volume, enabled);
}

} // namespace phrasearp
