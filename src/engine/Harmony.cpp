#include "engine/Harmony.h"

#include <algorithm>
#include <cmath>

namespace phrasearp {
namespace {

int walkScaleFrom(const Scale& scale, int rootPc, int steps) {
    int midi = 48 + pcOf(rootPc);
    while (pcOf(midi) != pcOf(rootPc))
        ++midi;
    if (steps <= 0)
        return pcOf(midi);
    int left = steps;
    for (int guard = 0; left > 0 && guard < 64; ++guard) {
        ++midi;
        if (scale.contains(midi))
            --left;
    }
    return pcOf(midi);
}

std::vector<int> allowedPitches(const Chord& chord, const Scale& scale, float color, int low, int high, bool strongOnly) {
    std::vector<int> out;
    const int lo = std::max(0, low - 12);
    const int hi = std::min(127, high + 12);
    for (int n = lo; n <= hi; ++n) {
        const ToneRole role = roleOf(n, chord, scale);
        if (strongOnly) {
            if (role == ToneRole::Chord)
                out.push_back(n);
        } else if (roleAllowed(role, color, false)) {
            out.push_back(n);
        }
    }
    if (out.empty()) {
        for (int n = lo; n <= hi; ++n)
            if (isChordTone(n, chord) || n == std::clamp(chord.root, lo, hi))
                out.push_back(n);
    }
    if (out.empty())
        out.push_back(std::clamp(60, 0, 127));
    return out;
}

int nearestAllowed(int target, int prev, int dir, int maxJump, const std::vector<int>& allowed, bool preferIdeal) {
    int best = allowed.front();
    int bestRank = 1000000;
    for (int n : allowed) {
        const int distPrev = prev >= 0 ? std::abs(n - prev) : 0;
        if (prev >= 0 && maxJump > 0 && distPrev > maxJump)
            continue;
        if (prev >= 0 && dir > 0 && n < prev)
            continue;
        if (prev >= 0 && dir < 0 && n > prev)
            continue;
        const int distIdeal = std::abs(n - target);
        const int rank = distPrev * 10 + distIdeal;
        if (rank < bestRank || (rank == bestRank && n > best)) {
            bestRank = rank;
            best = n;
        }
        (void) preferIdeal;
    }
    if (bestRank == 1000000) {
        // Nothing inside the jump window: take the closest allowed tone.
        bestRank = 1000000;
        for (int n : allowed) {
            const int d = std::abs(n - (prev >= 0 ? prev : target));
            if (d < bestRank) {
                bestRank = d;
                best = n;
            }
        }
    }
    return best;
}

} // namespace

int nearestPitch(int pitchClass, int target, int low, int high) {
    low = std::clamp(low, 0, 127);
    high = std::clamp(high, low, 127);
    int best = std::clamp(target, low, high);
    int bestD = 99;
    for (int n = low; n <= high; ++n) {
        if (pcOf(n) != pcOf(pitchClass))
            continue;
        const int d = std::abs(n - target);
        if (d < bestD) {
            bestD = d;
            best = n;
        }
    }
    return best;
}

int fitRange(int midi, int low, int high, BoundaryMode boundary) {
    low = std::clamp(low, 0, 127);
    high = std::clamp(high, 0, 127);
    if (high < low)
        std::swap(low, high);
    if (midi >= low && midi <= high)
        return midi;
    if (boundary == BoundaryMode::Wrap) {
        const int span = high - low + 1;
        if (span <= 0)
            return low;
        return low + (int) floorMod((int64_t) midi - low, span);
    }
    const int span = std::max(1, high - low);
    const int period = span * 2;
    int m = (int) floorMod((int64_t) midi - low, period);
    if (m > span)
        m = period - m;
    return std::clamp(low + m, low, high);
}

Voicing voiceLead(const Voicing* previous, const Chord& chord, int low, int high, int maxJump) {
    low = std::clamp(low, 0, 127);
    high = std::clamp(high, low, 127);
    const int center = previous != nullptr ? previous->rootPitch : 60;
    Voicing out;
    out.rootPitch = nearestPitch(chord.root, center, low, high);

    std::vector<int> pcs;
    for (int pc = 0; pc < 12; ++pc)
        if ((chord.tones & (1u << pc)) && pc != pcOf(chord.root))
            pcs.push_back(pc);
    if ((chord.tones & (1u << pcOf(chord.root))) == 0)
        pcs.insert(pcs.begin(), pcOf(chord.root));

    if (previous == nullptr || previous->count <= 0) {
        std::vector<int> stacked;
        stacked.push_back(out.rootPitch);
        for (int pc : pcs) {
            int best = -1;
            for (int n = out.rootPitch; n <= std::min(127, out.rootPitch + 12); ++n) {
                if (pcOf(n) == pc && n >= low && n <= high) {
                    best = n;
                    break;
                }
            }
            if (best < 0)
                best = nearestPitch(pc, out.rootPitch + 4, low, high);
            stacked.push_back(best);
        }
        std::sort(stacked.begin(), stacked.end());
        stacked.erase(std::unique(stacked.begin(), stacked.end()), stacked.end());
        out.count = std::min(8, (int) stacked.size());
        for (int i = 0; i < out.count; ++i)
            out.notes[(size_t) i] = stacked[(size_t) i];
        return out;
    }

    std::vector<int> prevNotes(previous->notes.begin(), previous->notes.begin() + previous->count);
    std::sort(prevNotes.begin(), prevNotes.end());

    std::vector<std::vector<int>> options(pcs.size());
    for (size_t i = 0; i < pcs.size(); ++i) {
        for (int n = low; n <= high; ++n)
            if (pcOf(n) == pcs[i])
                options[i].push_back(n);
        if (options[i].empty())
            options[i].push_back(nearestPitch(pcs[i], out.rootPitch, low, high));
    }

    int bestCost = 1000000;
    std::vector<int> best;
    std::vector<int> cur(pcs.size());
    const int jump = std::max(1, maxJump);

    auto consider = [&]() {
        std::vector<int> chosen = cur;
        chosen.push_back(out.rootPitch);
        std::sort(chosen.begin(), chosen.end());
        chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
        int cost = 0;
        const size_t n = std::min(chosen.size(), prevNotes.size());
        for (size_t i = 0; i < n; ++i) {
            const int d = std::abs(chosen[i] - prevNotes[i]);
            cost += d;
            if (d > jump)
                cost += (d - jump) * 4;
        }
        cost += (int) std::abs((int) chosen.size() - (int) prevNotes.size()) * 3;
        if (!chosen.empty())
            cost += (chosen.back() - chosen.front()) / 12;
        if (cost < bestCost) {
            bestCost = cost;
            best = chosen;
        }
    };

    if (pcs.empty()) {
        consider();
    } else {
        std::vector<size_t> idx(pcs.size(), 0);
        while (true) {
            for (size_t i = 0; i < pcs.size(); ++i)
                cur[i] = options[i][idx[i]];
            consider();
            size_t k = 0;
            for (; k < pcs.size(); ++k) {
                ++idx[k];
                if (idx[k] < options[k].size())
                    break;
                idx[k] = 0;
            }
            if (k == pcs.size())
                break;
        }
    }

    if (best.empty())
        best.push_back(out.rootPitch);
    out.count = std::min(8, (int) best.size());
    for (int i = 0; i < out.count; ++i)
        out.notes[(size_t) i] = best[(size_t) i];
    bool foundRoot = false;
    for (int i = 0; i < out.count; ++i) {
        if (pcOf(out.notes[(size_t) i]) == pcOf(chord.root)) {
            out.rootPitch = out.notes[(size_t) i];
            foundRoot = true;
            break;
        }
    }
    if (!foundRoot)
        out.rootPitch = nearestPitch(chord.root, center, low, high);
    return out;
}

std::vector<int> chordTonePitches(const Voicing& voicing) {
    std::vector<int> v;
    for (int i = 0; i < voicing.count; ++i)
        v.push_back(voicing.notes[(size_t) i]);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

int indexIdeal(const Chord& chord, const Scale& scale, float color, int index, int anchorRoot, int low, int high, int octaves) {
    int lo = low;
    int hi = high;
    const int span = std::max(12, octaves * 12);
    const int mid = anchorRoot;
    lo = std::max(low, mid - span / 2);
    hi = std::min(high, lo + span);
    if (hi <= lo)
        hi = std::min(127, lo + 12);
    auto list = allowedPitches(chord, scale, color, lo, hi, false);
    if (list.empty())
        return std::clamp(anchorRoot, 0, 127);
    int anchorPos = 0;
    int bestD = 1000;
    for (int i = 0; i < (int) list.size(); ++i) {
        if (!isChordTone(list[(size_t) i], chord) && pcOf(list[(size_t) i]) != pcOf(chord.root))
            continue;
        if (pcOf(list[(size_t) i]) != pcOf(anchorRoot) && pcOf(anchorRoot) == pcOf(chord.root))
            continue;
        const int d = std::abs(list[(size_t) i] - anchorRoot);
        if (d < bestD) {
            bestD = d;
            anchorPos = i;
        }
    }
    // Prefer the anchor pitch itself when it is in the list.
    for (int i = 0; i < (int) list.size(); ++i) {
        if (list[(size_t) i] == anchorRoot) {
            anchorPos = i;
            break;
        }
    }
    int i = anchorPos + index;
    if (i < 0)
        i = 0;
    if (i >= (int) list.size())
        i = (int) list.size() - 1;
    return list[(size_t) i];
}

int realizePitch(PitchMode mode,
                 int stored,
                 const Chord& chord,
                 const Scale& scale,
                 float color,
                 int prevMidi,
                 int prevStored,
                 bool strong,
                 int maxJump,
                 int low,
                 int high,
                 int octaves,
                 BoundaryMode boundary,
                 int octaveShift,
                 int transpose,
                 int contour,
                 uint32_t randomSalt) {
    Voicing anchor = voiceLead(nullptr, chord, low, high, maxJump);
    // The caller passes prevMidi; anchor root stays near the previous melody
    // register when we have one, otherwise near middle C.
    const int center = prevMidi >= 0 ? prevMidi : 60;
    anchor.rootPitch = nearestPitch(chord.root, prevMidi >= 0 ? (center + 60) / 2 : 60, low, high);
    // Stable anchor: nearest root to C4, then the melody follower limits the jump.
    // Recomputing the anchor from the previous melody note makes the index
    // meaning depend on history in a way that fights composed lines.
    anchor.rootPitch = nearestPitch(chord.root, 60, low, high);
    if (prevMidi >= 0) {
        // Voice-lead the root toward the previous root register implied by prevMidi's octave.
        const int prevRoot = nearestPitch(chord.root, prevMidi, low, high);
        anchor.rootPitch = prevRoot;
    }

    auto toneAllowed = [&](int midi, bool strongBeat) {
        return roleAllowed(roleOf(midi, chord, scale), color, strongBeat);
    };

    int ideal = 60;
    if (mode == PitchMode::ScaleDegree) {
        ideal = scaleDegreeToMidi(scale, stored + contour, 60);
    } else if (mode == PitchMode::Index) {
        ideal = indexIdeal(chord, scale, color, stored + contour, anchor.rootPitch, low, high, octaves);
    } else {
        auto tones = allowedPitches(chord, scale, 0.f, low, high, true);
        if (tones.empty())
            tones.push_back(anchor.rootPitch);
        const int n = (int) tones.size();
        int pick = 0;
        switch (mode) {
            case PitchMode::Up: pick = positiveMod(stored, n); break;
            case PitchMode::Down: pick = n - 1 - positiveMod(stored, n); break;
            case PitchMode::UpDown: {
                if (n == 1) {
                    pick = 0;
                    break;
                }
                const int cycle = n * 2 - 2;
                const int m = positiveMod(stored, cycle);
                pick = m < n ? m : cycle - m;
                break;
            }
            case PitchMode::Random: pick = (int) (stepHash(randomSalt, (uint32_t) stored, 3, 9, 1) % (uint32_t) n); break;
            case PitchMode::AsPlayed: pick = positiveMod(stored, n); break;
            case PitchMode::Root: {
                pick = 0;
                int best = 100;
                for (int i = 0; i < n; ++i) {
                    const int d = std::abs(tones[(size_t) i] - anchor.rootPitch);
                    if (pcOf(tones[(size_t) i]) == pcOf(chord.root) && d < best) {
                        best = d;
                        pick = i;
                    }
                }
                break;
            }
            case PitchMode::Top: pick = n - 1; break;
            default: pick = 0; break;
        }
        ideal = tones[(size_t) std::clamp(pick, 0, n - 1)];
        (void) prevStored;
    }

    ideal += octaveShift * 12;
    if (transpose != 0)
        ideal = transposeScale(scale, ideal, transpose);

    const int dir = prevStored == stored ? 0 : (stored > prevStored ? 1 : -1);
    int midi = ideal;
    const bool jumpOk = prevMidi < 0 || std::abs(ideal - prevMidi) <= std::max(0, maxJump);
    if (!(jumpOk && toneAllowed(ideal, strong))) {
        auto list = allowedPitches(chord, scale, color, low, high, strong);
        int useDir = dir;
        if (mode == PitchMode::ScaleDegree || mode == PitchMode::Index) {
            if (prevMidi >= 0 && ideal != prevMidi)
                useDir = ideal > prevMidi ? 1 : -1;
        }
        midi = nearestAllowed(ideal, prevMidi, useDir, std::max(0, maxJump), list, true);
        if (strong && !isChordTone(midi, chord)) {
            auto chordList = allowedPitches(chord, scale, color, low, high, true);
            midi = nearestAllowed(ideal, prevMidi, useDir, std::max(0, maxJump), chordList, true);
        }
    }
    return fitRange(midi, low, high, boundary);
}

} // namespace phrasearp
