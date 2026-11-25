#pragma once

// 303-style oscillator with an octave-down sub that is phase-locked to it.
// Plain C++ (no JUCE) so the offline render test can use it directly.

#include <cmath>
#include <cstdint>

namespace s303
{
constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double polyBlep (double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

/** Small, fast xorshift noise source, deterministic per instance. */
struct Noise
{
    uint32_t state = 0x9E3779B9u;

    void seed (uint32_t s) { state = s ? s : 0x9E3779B9u; }

    float next() // uniform -1..1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) ((double) state / 2147483648.0 - 1.0);
    }
};

/**
    Saw + 303-style square. The real 303 derives its square from the saw through a
    single transistor, so the square here is a soft-clipped, offset saw: rounded
    edges, slightly off 50% duty, and a sagging top from the AC coupling (DC blocker).
*/
class AcidOscillator
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // ~25 Hz AC coupling gives the square its characteristic tilt on low notes.
        sagCoeff = std::exp (-kTwoPi * 25.0 / sr);
        reset();
    }

    void setPhase (double p) { phase = p - std::floor (p); }

    void reset()
    {
        phase = 0.0;
        subCycle = 0;
        sagX = sagY = 0.0;
    }

    /**
        wave: 0 = saw, 1 = square (continuous morph). pulseWidth 0..1 narrows the square
        from ~56% to ~90% duty. subWave: 0 sine, 1 triangle, 2 square, 3 saw. subOctaves: 1 or 2.
    */
    void process (double freq, float wave, float pulseWidth, int subWave, int subOctaves,
                  float& mainOut, float& subOut)
    {
        const double dt = std::fmin (freq / sr, 0.45);

        // Falling ramp like the hardware, with a hint of capacitor-charge curvature.
        double s = 1.0 - 2.0 * phase;
        s += polyBlep (phase, dt);
        const double curved = s - 0.08 * (s * s - 1.0 / 3.0);

        double sq = 0.0;
        if (wave > 0.001f)
        {
            const double bias = 0.12 + 0.68 * pulseWidth;
            const double shaped = std::tanh (3.2 * (curved + bias)) * 0.92;
            const double y = shaped - sagX + sagCoeff * sagY;
            sagX = shaped;
            sagY = y;
            sq = y;
        }

        mainOut = (float) (curved * (1.0 - wave) + sq * wave);

        // Sub phase runs at exactly 1/2 or 1/4 rate, derived from the main phase so it
        // can never drift out of lock.
        const int div = subOctaves >= 2 ? 4 : 2;
        const double sp = (phase + (double) (subCycle % div)) / (double) div;
        switch (subWave)
        {
            case 1:  subOut = (float) triWave (sp); break;
            case 2:  subOut = sp < 0.5 ? 0.8f : -0.8f; break;
            case 3:  subOut = (float) (0.8 * (1.0 - 2.0 * sp)); break;
            default: subOut = (float) std::sin (kTwoPi * sp); break;
        }

        phase += dt;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            subCycle = (subCycle + 1) & 3;
        }
    }

private:
    static double triWave (double p) // p in 0..1, zero-crossing at 0 like the sine
    {
        double t = p + 0.25;
        t -= std::floor (t);
        return 1.0 - 4.0 * std::fabs (t - 0.5);
    }

    double sr = 44100.0;
    double phase = 0.0;
    int subCycle = 0;
    double sagCoeff = 0.0, sagX = 0.0, sagY = 0.0;
};

/** Slow analog-style pitch instability: filtered random walk, in cents. */
class Drift
{
public:
    void prepare (double sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        noise.seed (seed);
        coeff = 1.0 - std::exp (-kTwoPi * 0.7 / sr);
        value = 0.0;
        target = 0.0;
        counter = 0;
    }

    double next (float amount)
    {
        // Pick a new random target ~8 times a second, glide towards it smoothly.
        if (--counter <= 0)
        {
            counter = (int) (sr / 8.0);
            target = noise.next();
        }
        value += (target - value) * coeff;
        return value * amount * 12.0; // up to +/-12 cents
    }

    float randomCents (float amount) { return noise.next() * amount * 4.0f; }

private:
    Noise noise;
    double sr = 44100.0, coeff = 0.0, value = 0.0, target = 0.0;
    int counter = 0;
};
} // namespace s303
