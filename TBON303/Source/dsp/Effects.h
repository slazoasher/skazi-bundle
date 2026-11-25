#pragma once

// Spatial FX for the mono voice: stereo chorus/flanger, a bucket-brigade style
// ping-pong delay, and a width control that keeps the low end mono.
// (The room reverb uses juce::Reverb in the processor.)

#include "Oscillator.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace s303
{
/**
    Shared FX mix law. 0..50%: dry stays at full level while the effect fades in.
    50..100%: the effect stays full while the dry fades out, so 100% is fully wet.
*/
inline void mixGains (float mix, float& dry, float& wet)
{
    mix = std::clamp (mix, 0.0f, 1.0f);
    dry = mix <= 0.5f ? 1.0f : 2.0f * (1.0f - mix);
    wet = mix >= 0.5f ? 1.0f : 2.0f * mix;
}

class DelayBuffer
{
public:
    void resize (int samples)
    {
        data.assign ((size_t) samples, 0.0f);
        pos = 0;
    }

    void clear() { std::fill (data.begin(), data.end(), 0.0f); }

    void write (float x)
    {
        if (data.empty())
            return;
        data[(size_t) pos] = x;
        if (++pos >= (int) data.size())
            pos = 0;
    }

    /** Linear-interpolated read, delay measured from the most recent write. */
    float read (double delaySamples) const
    {
        const int size = (int) data.size();
        if (size < 4 || ! std::isfinite (delaySamples))
            return 0.0f;
        delaySamples = std::clamp (delaySamples, 1.0, (double) size - 2.0);
        double rp = pos - 1 - delaySamples;
        if (rp < 0.0)
            rp += size; // delaySamples < size, so one wrap is always enough
        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % size;
        const float t = (float) (rp - i0);
        return data[(size_t) i0] + (data[(size_t) i1] - data[(size_t) i0]) * t;
    }

private:
    std::vector<float> data;
    int pos = 0;
};

/** Stereo chorus (mode 1) or flanger (mode 2). Mode 0 bypasses. */
class ModFX
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        buf.resize ((int) (sr * 0.05) + 4);
        phase = 0.0;
        fbState = 0.0f;
    }

    void reset()
    {
        buf.clear();
        fbState = 0.0f;
    }

    void process (float in, int mode, float rateHz, float depth, float mix, float& outL, float& outR)
    {
        if (mode == 0 || mix <= 0.0001f)
        {
            buf.write (in);
            outL = outR = in;
            return;
        }

        const bool flanger = mode == 2;
        const double centre = (flanger ? 0.0025 : 0.008) * sr;
        const double swing = (flanger ? 0.0020 : 0.005) * sr * depth;

        phase += rateHz / sr;
        if (phase >= 1.0)
            phase -= 1.0;

        // L and R LFOs run in quadrature for chorus, inverted for flanger.
        const double offset = flanger ? 0.5 : 0.25;
        const double dL = centre + swing * std::sin (kTwoPi * phase);
        const double dR = centre + swing * std::sin (kTwoPi * (phase + offset));

        const float fb = flanger ? 0.72f * depth : 0.0f;
        buf.write (in + fbState * fb);

        const float tapL = buf.read (dL);
        const float tapR = buf.read (dR);
        fbState = std::tanh (tapL);

        float dry, wet;
        mixGains (mix, dry, wet);
        outL = in * dry + tapL * wet;
        outR = in * dry + tapR * wet;
    }

private:
    double sr = 44100.0, phase = 0.0;
    DelayBuffer buf;
    float fbState = 0.0f;
};

/**
    Bucket-brigade flavoured ping-pong delay: darkened, saturating repeats, a little
    clock wobble, and delay-time changes that glide (pitch-bend) instead of jumping.
*/
class BBDDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        bufL.resize ((int) (sr * 2.5) + 4);
        bufR.resize ((int) (sr * 2.5) + 4);
        reset();
    }

    void reset()
    {
        bufL.clear();
        bufR.clear();
        lpL = lpR = hpL = hpR = 0.0;
        time = -1.0;
        wow = 0.0;
    }

    void process (float inL, float inR, double timeSec, float feedback, float toneHz, float mix,
                  float& outL, float& outR)
    {
        if (time < 0.0)
            time = timeSec;
        time += (timeSec - time) * (1.0 - std::exp (-1.0 / (0.08 * sr)));

        wow += 0.45 / sr;
        if (wow >= 1.0)
            wow -= 1.0;
        const double d = (time + 0.0003 * std::sin (kTwoPi * wow)) * sr;

        const float wetL = bufL.read (d);
        const float wetR = bufR.read (d);

        // Repeats lose top end and the lowest bass on each pass, like a BBD + the
        // usual low cut you'd want on a club bass delay.
        const double lpC = 1.0 - std::exp (-kTwoPi * std::min ((double) toneHz, sr * 0.45) / sr);
        const double hpC = 1.0 - std::exp (-kTwoPi * 140.0 / sr);
        lpL += (wetL - lpL) * lpC;
        lpR += (wetR - lpR) * lpC;
        hpL += (lpL - hpL) * hpC;
        hpR += (lpR - hpR) * hpC;
        const float fbL = std::tanh ((float) (lpL - hpL) * feedback * 1.1f);
        const float fbR = std::tanh ((float) (lpR - hpR) * feedback * 1.1f);

        // Ping-pong: input enters left, each side feeds the other.
        bufL.write (0.5f * (inL + inR) + fbR);
        bufR.write (fbL);

        float dry, wet;
        mixGains (mix, dry, wet);
        outL = inL * dry + wetL * wet;
        outR = inR * dry + wetR * wet;
    }

private:
    double sr = 44100.0, time = -1.0, wow = 0.0;
    DelayBuffer bufL, bufR;
    double lpL = 0.0, lpR = 0.0, hpL = 0.0, hpR = 0.0;
};

/** Mid/side width that leaves everything below ~150 Hz mono. */
class StereoWidth
{
public:
    void prepare (double sampleRate)
    {
        coeff = 1.0 - std::exp (-kTwoPi * 150.0 / sampleRate);
        lowSide = 0.0;
    }

    void process (float& l, float& r, float width)
    {
        const double mid = 0.5 * (l + r);
        const double side = 0.5 * (l - r);
        lowSide += (side - lowSide) * coeff;
        const double s = (side - lowSide) * width;
        l = (float) (mid + s);
        r = (float) (mid - s);
    }

private:
    double coeff = 0.0, lowSide = 0.0;
};
/**
    OTT: 3-band upward + downward compressor. Each band is pulled up when quiet and
    pushed down when loud, so the line gets dense and in-your-face. The bands are split
    by subtraction, so with depth 0 (or no gain change) they sum back to the input exactly.
*/
class OTT
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lowC = 1.0 - std::exp (-kTwoPi * 160.0 / sr);
        highC = 1.0 - std::exp (-kTwoPi * 2500.0 / sr);
        gainC = 1.0 - std::exp (-1.0 / (0.003 * sr));
        reset();
    }

    void reset()
    {
        lp1 = lp2 = hp1 = hp2 = 0.0;
        for (int b = 0; b < 3; ++b) { env[b] = 0.0; gain[b] = 1.0; }
    }

    /** depth 0..1, time 0..1 (fast..slow). */
    float process (float x, float depth, float time)
    {
        if (depth <= 0.0001f)
            return x;

        const double rel = std::exp (-1.0 / ((0.03 + 0.4 * time) * sr));

        lp1 += (x - lp1) * lowC;
        lp2 += (lp1 - lp2) * lowC;
        const double rest = x - lp2;
        hp1 += (rest - hp1) * highC;
        hp2 += (hp1 - hp2) * highC;
        const double band[3] = { lp2, hp2, rest - hp2 };

        double wet = 0.0;
        for (int b = 0; b < 3; ++b)
        {
            const double lv = std::abs (band[b]);
            env[b] = lv > env[b] ? lv : rel * env[b] + (1.0 - rel) * lv; // peak attack: hits are caught at once
            const double db = 20.0 * std::log10 (std::max (env[b], 1.0e-5));
            double gdb = 0.0;
            if (db > -14.0) gdb -= (db + 14.0) * 0.75;                                       // loud: pushed down
            else if (db > -75.0 && db < -30.0) gdb += std::min (12.0, (-30.0 - db) * 0.6);  // quiet: pulled up
            // Gain drops at once (a hit never rides a boosted band), recovers smoothly.
            const double target = std::pow (10.0, gdb / 20.0);
            gain[b] = target < gain[b] ? target : gain[b] + (target - gain[b]) * gainC;
            wet += band[b] * gain[b];
        }
        return (float) (x + (wet - x) * depth);
    }

private:
    double sr = 44100.0, lowC = 0.0, highC = 0.0, gainC = 0.0;
    double lp1 = 0.0, lp2 = 0.0, hp1 = 0.0, hp2 = 0.0;
    double env[3] {}, gain[3] { 1.0, 1.0, 1.0 };
};

/**
    Keeps the DRIVE 2 + OTT section at the level of what goes into it, so TURBO and the
    extra drive add density and grit, not just volume. Fed once per block with the
    energy before and after the section; returns the gain to apply to the section output.
*/
class LevelMatch
{
public:
    void reset() { inPow = outPow = 0.0; gain = 1.0; primed = false; }
    bool isPrimed() const { return primed; }

    double update (double blockInPow, double blockOutPow, double blockSeconds)
    {
        // The first block with signal sets the level directly, so switching on never jumps up.
        const double a = primed ? std::exp (-blockSeconds / 0.25) : 0.0;
        primed = primed || blockInPow > 1.0e-8;
        inPow = a * inPow + (1.0 - a) * blockInPow;
        outPow = a * outPow + (1.0 - a) * blockOutPow;
        if (inPow > 1.0e-8 && outPow > 1.0e-10) // hold the last gain through silence
            gain = std::clamp (std::sqrt (inPow / outPow), 0.02, 4.0);
        return gain;
    }

private:
    double inPow = 0.0, outPow = 0.0, gain = 1.0;
    bool primed = false;
};
} // namespace s303
