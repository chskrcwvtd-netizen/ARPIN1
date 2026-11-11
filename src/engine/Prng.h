#pragma once

#include "engine/Util.h"

namespace phrasearp {

// xoshiro256**. Audio and generation never call std::rand / rand().
class Prng {
public:
    uint64_t s[4] {};

    static uint64_t splitmix64(uint64_t& x) {
        uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    static Prng stream(uint32_t seed, uint32_t lane, uint32_t salt = 0) {
        uint64_t x = 0xA1B2C3D4E5F60718ULL;
        x ^= (uint64_t) seed + 0x9e3779b97f4a7c15ULL;
        x = splitmix64(x);
        x ^= (uint64_t) lane * 0xD1B54A32D192ED03ULL;
        x ^= (uint64_t) salt << 17;
        Prng p;
        p.s[0] = splitmix64(x);
        p.s[1] = splitmix64(x);
        p.s[2] = splitmix64(x);
        p.s[3] = splitmix64(x);
        if ((p.s[0] | p.s[1] | p.s[2] | p.s[3]) == 0)
            p.s[0] = 0x9E3779B97F4A7C15ULL;
        return p;
    }

    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t next() {
        const uint64_t result = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    uint32_t nextU32() { return (uint32_t) next(); }

    int nextInt(int n) {
        if (n <= 1)
            return 0;
        return (int) (nextU32() % (uint32_t) n);
    }

    float unit() { return (float) (nextU32() >> 8) * (1.0f / 16777216.0f); }
};

} // namespace phrasearp
