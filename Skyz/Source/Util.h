#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <algorithm>
#include "Params.h"

// Shared helpers. Every parameter arrives normalised 0..1; these map it the same way the UI's fmt()/real() does.
namespace skyz
{
    constexpr float pi = 3.14159265358979f, twoPi = 6.28318530717959f;

    struct Values // one block's parameter snapshot
    {
        float v[NUM_PARAMS] {};
        float operator[] (int i) const noexcept { return v[i]; }
        bool on (int i) const noexcept { return v[i] > 0.5f; }
        int idx (int i) const noexcept { const int n = paramDefs[i].steps; return n > 1 ? (int) std::lround (v[i] * (float) (n - 1)) : 0; }
        int list (int i, int n) const noexcept { return juce::jlimit (0, n - 1, (int) std::lround (v[i] * (float) (n - 1))); } // knobs that show a list
        float lin (int i, float a, float b) const noexcept { return a + v[i] * (b - a); }
        float ex (int i, float a, float b) const noexcept { return a * std::pow (b / a, v[i]); }
        float pw (int i, float a, float b) const noexcept { return a + v[i] * v[i] * v[i] * (b - a); }
        float bi (int i) const noexcept { return (v[i] - 0.5f) * 2.0f; } // -1..1
    };

    inline float lin (float x, float a, float b) noexcept { return a + x * (b - a); }
    inline float expo (float x, float a, float b) noexcept { return a * std::pow (b / a, x); }

    // UI list DIVS = 1/64 .. 8 BAR, in beats
    inline double divBeats (int i)
    {
        static const double b[] { 1.0 / 16, 1.0 / 8, 1.0 / 12, 0.25, 1.0 / 6, 0.375, 0.5, 1.0 / 3, 0.75, 1.0, 2.0 / 3, 1.5, 2.0, 4.0, 8.0, 16.0, 32.0 };
        return b[juce::jlimit (0, 16, i)];
    }
    constexpr int numDivs = 17;

    inline float fastTanh (float x) noexcept { x = juce::jlimit (-3.0f, 3.0f, x); return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }
    inline float noteHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

    // Topology-preserving state variable filter (Zavalishin). One channel.
    struct Svf
    {
        float g = 0.1f, k = 1.4f, a1 = 0, a2 = 0, a3 = 0, s1 = 0, s2 = 0;
        void set (float hz, float q, float sr) noexcept
        {
            g = std::tan (pi * juce::jlimit (10.0f, sr * 0.45f, hz) / sr);
            k = 1.0f / juce::jmax (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() noexcept { s1 = s2 = 0; }
        // returns lp, writes bp/hp
        float run (float x, float& bp, float& hp) noexcept
        {
            const float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1; s2 = 2.0f * v2 - s2;
            bp = v1; hp = x - k * v1 - v2;
            return v2;
        }
        float lp (float x) noexcept { float b, h; return run (x, b, h); }
        float bpf (float x) noexcept { float b, h; run (x, b, h); return b; }
        float hpf (float x) noexcept { float b, h; run (x, b, h); return h; }
    };

    struct OnePole
    {
        float a = 0.5f, z = 0;
        void setHz (float hz, float sr) noexcept { a = 1.0f - std::exp (-twoPi * juce::jlimit (1.0f, sr * 0.49f, hz) / sr); }
        float lp (float x) noexcept { z += a * (x - z); return z; }
        float hp (float x) noexcept { z += a * (x - z); return x - z; }
    };

    struct Rng // tiny fast PRNG, audio-thread safe
    {
        uint32_t s = 0x12345678u;
        explicit Rng (uint32_t seed = 0x12345678u) : s (seed ? seed : 1u) {}
        uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
        float bip() noexcept { return uni() * 2.0f - 1.0f; }                             // -1..1
    };

    // PolyBLEP residual for band-limited steps.
    inline float blep (float t, float dt) noexcept
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
}
