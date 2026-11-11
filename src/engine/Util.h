#pragma once

#include <cstdint>
#include <string>

namespace phrasearp {

inline int pcOf(int midi) {
    int m = midi % 12;
    return m < 0 ? m + 12 : m;
}

inline int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    const int64_t r = a % b;
    if (r != 0 && ((a < 0) != (b < 0)))
        --q;
    return q;
}

inline int64_t floorMod(int64_t a, int64_t b) {
    int64_t r = a % b;
    if ((r < 0 && b > 0) || (r > 0 && b < 0))
        r += b;
    return r;
}

inline int positiveMod(int a, int b) {
    if (b <= 0)
        return 0;
    int r = a % b;
    if (r < 0)
        r += b;
    return r;
}

inline std::string toHex8(uint32_t value) {
    static const char* kDigits = "0123456789ABCDEF";
    std::string out(8, '0');
    for (int i = 7; i >= 0; --i) {
        out[(size_t) i] = kDigits[value & 0xF];
        value >>= 4;
    }
    return out;
}

inline bool fromHex8(const std::string& text, uint32_t& out) {
    if (text.size() != 8)
        return false;
    uint32_t value = 0;
    for (char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= (uint32_t) (c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= (uint32_t) (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= (uint32_t) (c - 'A' + 10);
        else
            return false;
    }
    out = value;
    return true;
}

inline const char* pitchClassName(int pc) {
    static const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[pcOf(pc)];
}

// Stateless mix used for probability, evolve and humanize.
// Position-addressed so block size cannot change a decision.
inline uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline uint32_t stepHash(uint32_t seed, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t x = seed ^ 0xA5A5A5A5u;
    x = mix32(x ^ (a + 0x9e3779b9u));
    x = mix32(x ^ (b + 0x85ebca6bu));
    x = mix32(x ^ (c + 0xc2b2ae35u));
    x = mix32(x ^ (d + 0x27d4eb2fu));
    return x;
}

constexpr int kPpq = 960;
constexpr int kMaxSteps = 64;
constexpr int kPatternSlots = 8;
constexpr int kMaxChain = 16;
constexpr int kHistory = 32;
constexpr uint32_t kInitSeed = 0xC0FFEE01u;

} // namespace phrasearp
