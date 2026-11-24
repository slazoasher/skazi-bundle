#pragma once
// SKATOM DSP building blocks. GlitchSpectral is a line-for-line port of the web AudioWorklet
// (web/audio.js) so the plugin sounds like the browser preview.
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace skatom
{
constexpr double kTau = 6.283185307179586;

/* RBJ cookbook biquad (same formulas as Web Audio's BiquadFilterNode), TDF-II, stereo state. */
struct Biquad
{
    enum Kind { LP, HP, BP, LowShelf, Peak };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};

    void set (Kind k, double f, double q, double gainDb, double sr)
    {
        f = juce::jlimit (10.0, sr * 0.49, f);
        const double w = kTau * f / sr, c = std::cos (w), s = std::sin (w), A = std::pow (10.0, gainDb / 40.0);
        double al = s / (2.0 * q), n0, n1, n2, d0, d1, d2;
        switch (k)
        {
            case LP: n0 = (1 - c) / 2; n1 = 1 - c; n2 = n0; d0 = 1 + al; d1 = -2 * c; d2 = 1 - al; break;
            case HP: n0 = (1 + c) / 2; n1 = -(1 + c); n2 = n0; d0 = 1 + al; d1 = -2 * c; d2 = 1 - al; break;
            case BP: n0 = al; n1 = 0; n2 = -al; d0 = 1 + al; d1 = -2 * c; d2 = 1 - al; break;
            case LowShelf:
            {
                al = s / 2 * std::sqrt (2.0); // S = 1
                const double sa = 2 * std::sqrt (A) * al;
                n0 = A * ((A + 1) - (A - 1) * c + sa); n1 = 2 * A * ((A - 1) - (A + 1) * c); n2 = A * ((A + 1) - (A - 1) * c - sa);
                d0 = (A + 1) + (A - 1) * c + sa; d1 = -2 * ((A - 1) + (A + 1) * c); d2 = (A + 1) + (A - 1) * c - sa;
                break;
            }
            default: n0 = 1 + al * A; n1 = -2 * c; n2 = 1 - al * A; d0 = 1 + al / A; d1 = -2 * c; d2 = 1 - al / A; break;
        }
        b0 = n0 / d0; b1 = n1 / d0; b2 = n2 / d0; a1 = d1 / d0; a2 = d2 / d0;
    }
    float process (float x, int ch)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

/* Distortion curves — same math as makeCurve() in audio.js, evaluated directly. */
inline float tube (float x, float d)
{
    const float k = 1 + d * 30;
    x = juce::jlimit (-1.0f, 1.0f, x);
    const float y = (x >= 0 ? std::tanh (k * x) : std::tanh (k * 0.7f * x) / 0.9f) / std::tanh (k);
    return juce::jlimit (-1.0f, 1.0f, y);
}
inline float distShape (int type, float x, float d)
{
    if (type == 0) return tube (x, d);
    x = juce::jlimit (-1.0f, 1.0f, x);
    float y;
    if (type == 1) y = std::sin (x * (1 + d * 7) * juce::MathConstants<float>::halfPi);                 // mech: wavefolder
    else if (type == 2) { const float st = 2.0f / std::pow (2.0f, 12 - d * 10.5f); y = std::round (x / st) * st; } // crush
    else y = 0.55f * x + 0.45f * std::tanh ((1 + d * 30) * 2 * x);                                      // xcite
    return juce::jlimit (-1.0f, 1.0f, y);
}

/* Pitch shifter for builds up to +48 st (also the SHIMMER tail): 4 Hann-windowed read heads over a 70 ms
   delay window (the windows sum to 2), input band-limited to Nyquist/ratio so a 4-octave lift doesn't alias,
   semitones glided over ~25 ms so automation and the macro sweep smoothly instead of stepping. */
struct PitchShifter
{
    void prepare (double sampleRate)
    {
        sr = sampleRate; PW = sr * 0.07; PN = (int) PW + 8;
        for (auto& b : buf) b.assign ((size_t) PN, 0);
        w = 0; ph = 0; semis = target = 0; ratio = 1; cnt = 0; aa1.reset(); aa2.reset();
        glideK = 1 - std::exp (-1.0 / (0.025 * sr));
    }
    void process (double& l, double& r)
    {
        semis += (target - semis) * glideK;
        if (--cnt <= 0)
        {
            cnt = 32; ratio = std::pow (2.0, semis / 12);
            const double fc = 0.45 * sr / std::max (1.0, ratio); // 4th-order Butterworth anti-alias
            aa1.set (Biquad::LP, fc, 0.5412, 0, sr); aa2.set (Biquad::LP, fc, 1.3066, 0, sr);
        }
        buf[0][(size_t) w] = aa2.process (aa1.process ((float) l, 0), 0);
        buf[1][(size_t) w] = aa2.process (aa1.process ((float) r, 1), 1);
        if (std::abs (semis) > 0.01)
        {
            ph += (1 - ratio) / PW; ph -= std::floor (ph);
            double a = 0, b = 0;
            for (int k = 0; k < 4; ++k)
            {
                double p2 = ph + k * 0.25; p2 -= std::floor (p2);
                double rp = w - (p2 * PW + 2); if (rp < 0) rp += PN;
                const int q0 = (int) rp, q1 = (q0 + 1) % PN; const double fq = rp - q0, g = 0.5 - 0.5 * std::cos (kTau * p2);
                a += (buf[0][(size_t) q0] + (buf[0][(size_t) q1] - buf[0][(size_t) q0]) * fq) * g;
                b += (buf[1][(size_t) q0] + (buf[1][(size_t) q1] - buf[1][(size_t) q0]) * fq) * g;
            }
            const double mix = std::min (1.0, std::abs (semis) * 2);
            l += (a * 0.5 - l) * mix; r += (b * 0.5 - r) * mix;
        }
        w = (w + 1) % PN;
    }
    double target = 0;
private:
    std::vector<float> buf[2];
    Biquad aa1, aa2;
    double sr = 48000, PW = 3360, ph = 0, semis = 0, ratio = 1, glideK = 0.001;
    int PN = 3368, w = 0, cnt = 0;
};

/* Stages 01–02: glitch buffer (stutter / reverse / tape stop / freeze), freq shifter, contrast, reson combs. */
struct GlitchSpectral
{
    struct Params { double bpm = 128; int div = 4; bool stutter = false, reverse = false, tape = false, freeze = false;
                    float tapeTime = 0.6f, glitch = 0, shift = 0, contrast = 0, resonFreq = 220, tension = 0, pitch = 0; } p;

    // read by the UI telemetry
    int mode = 0; float pos = 0, rate = 1; int len = 1; bool glActive = false;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        N = (int) (sr * 4);
        buf[0].assign ((size_t) N, 0); buf[1].assign ((size_t) N, 0);
        CM = (int) std::ceil (sr / 35) + 4;
        for (auto& ch : cb) for (auto& v : ch) v.assign ((size_t) CM, 0);
        w = 0; cw = 0; mode = 0; t = 0; wet = 0; fin = 1; clock = 0; gl = {};
        cd[0] = sr / 220; cd[1] = sr / 330;
        shifter.prepare (sr);
        std::fill (std::begin (hs), std::end (hs), 0.0);
        hd[0] = hd[1] = 0; ph = 0; ef = es = 0; clp[0][0] = clp[0][1] = clp[1][0] = clp[1][1] = 0;
    }
    void resetClock() { clock = 0; gl = {}; }

    void process (float* L, float* R, int n)
    {
        const double spStep = 60.0 / p.bpm / 4 * sr;
        const double F = 96, fb = p.tension * 0.96, dT0 = sr / p.resonFreq, dT1 = dT0 / 1.5, dph = p.shift / sr;
        const double sm = std::min (1.0, std::abs ((double) p.shift) / 4);
        float* b0 = buf[0].data(); float* b1 = buf[1].data();

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            b0[w] = xl; b1[w] = xr; w = (w + 1) % N;
            if (++clock >= spStep) { clock -= spStep; onStep(); }

            const int want = p.freeze ? 4 : p.tape ? 3 : (p.stutter || gl.kind == 1) ? 1 : (p.reverse || gl.kind == 2) ? 2 : 0;
            if (want != mode) enter (want);
            double yl = xl, yr = xr;

            if (mode)
            {
                double a = 0, b = 0;
                if (mode == 1 || mode == 2)
                {
                    if (mode == 1 && gl.kind == 0) len = divLen (p.div); // live division changes re-slice the held loop
                    const long L_ = len, phs = t % L_;
                    const long idx = mode == 1 ? start + phs : start - 1 - phs;
                    const long j = ((idx % N) + N) % N;
                    const double f = std::min ({ 1.0, phs / F, (L_ - phs) / F });
                    a = b0[j] * f; b = b1[j] * f;
                    if (mode == 2 && phs == L_ - 1) start = w;
                    ++t; pos = (float) phs / (float) L_;
                }
                else if (mode == 3)
                {
                    rate = (float) std::max (0.0, rate - 1.0 / (p.tapeTime * sr));
                    rp += rate; if (rp >= N) rp -= N;
                    const long k = (long) std::floor (rp); const double fr = rp - k;
                    const long j0 = ((k % N) + N) % N, j1 = (j0 + 1) % N;
                    const double f = std::min (1.0, rate * 3.0);
                    a = (b0[j0] + (b0[j1] - b0[j0]) * fr) * f; b = (b1[j0] + (b1[j1] - b1[j0]) * fr) * f;
                    pos = 1 - rate;
                }
                else // freeze: two Hann grains at 50% overlap
                {
                    for (int k = 0; k < 2; ++k)
                    {
                        const long g = (t + k * (len >> 1)) % len;
                        const double h = 0.5 - 0.5 * std::cos (kTau * g / len);
                        const long j = (start + g) % N;
                        a += b0[j] * h; b += b1[j] * h;
                    }
                    ++t; pos = (float) (t % len) / (float) len;
                }
                if (wet < 1) wet = std::min (1.0, wet + 1.0 / 128);
                yl = xl + (a - xl) * wet; yr = xr + (b - xr) * wet;
            }
            else if (fin < 1) { fin = std::min (1.0, fin + 1.0 / 128); yl *= fin; yr *= fin; }

            // 01b pitch shifter (up to +48 st)
            shifter.target = p.pitch;
            shifter.process (yl, yr);

            // 02a frequency shifter
            ph += dph; ph -= std::floor (ph);
            const double cs = std::cos (kTau * ph), sn = std::sin (kTau * ph);
            const double sl = hilb (0, yl, cs, sn), sr_ = hilb (1, yr, cs, sn);
            yl += (sl - yl) * sm; yr += (sr_ - yr) * sm;

            // 02b contrast (fast/slow envelope ratio)
            // shortcut: dynamic contrast, not per-bin spectral contrast — upgrade with an STFT stage if it needs to be "true" GRM
            const double ax = std::max (std::abs (yl), std::abs (yr));
            ef += (ax - ef) * (ax > ef ? 0.05 : 0.0015); es += (ax - es) * 0.00015;
            if (p.contrast > 0.001f)
            {
                const double gc = std::min (4.0, std::pow ((ef + 1e-5) / (es + 1e-5), p.contrast * 1.5));
                yl *= gc; yr *= gc;
            }

            // 02c reson bank: root + fifth damped combs
            cd[0] += (dT0 - cd[0]) * 0.002; cd[1] += (dT1 - cd[1]) * 0.002;
            double acc[2] = { 0, 0 };
            for (int k = 0; k < 2; ++k)
            {
                double r = cw - cd[k]; if (r < 0) r += CM;
                const int r0 = (int) r, r1 = (r0 + 1) % CM; const double fr = r - r0;
                for (int c = 0; c < 2; ++c)
                {
                    auto& bb = cb[c][k];
                    const double v = bb[(size_t) r0] + (bb[(size_t) r1] - bb[(size_t) r0]) * fr;
                    clp[c][k] += 0.5 * (v - clp[c][k]);
                    const double yk = (c ? yr : yl) + fb * clp[c][k];
                    bb[(size_t) cw] = (float) yk; acc[c] += yk;
                }
            }
            cw = (cw + 1) % CM;
            if (p.tension > 0.001f)
            {
                const double T = p.tension, wg = 0.5 * (1 - fb * 0.85) * T;
                yl = yl * (1 - 0.6 * T) + acc[0] * wg; yr = yr * (1 - 0.6 * T) + acc[1] * wg;
            }
            L[i] = (float) yl; R[i] = (float) yr;
        }
        glActive = gl.kind != 0;
    }

private:
    struct Gl { int kind = 0, div = 4, left = 0; } gl;
    double sr = 48000, clock = 0, rp = 0, wet = 0, fin = 1, ph = 0, ef = 0, es = 0, cd[2] {}, clp[2][2] {}, hd[2] {};
    double hs[2 * 2 * 4 * 4] {}; // [channel][chain][section][x1 x2 y1 y2]
    std::vector<float> buf[2], cb[2][2];
    PitchShifter shifter;
    int N = 1, CM = 1, w = 0, cw = 0;
    long start = 0, t = 0;
    juce::Random rng;

    static constexpr double H1[4] = { 0.6923878 * 0.6923878, 0.9360654322959 * 0.9360654322959, 0.9882295226860 * 0.9882295226860, 0.9987488452737 * 0.9987488452737 };
    static constexpr double H2[4] = { 0.4021921162426 * 0.4021921162426, 0.8561710882420 * 0.8561710882420, 0.9722909545651 * 0.9722909545651, 0.9952884791278 * 0.9952884791278 };

    int divLen (int d) const
    {
        static constexpr int DV[] = { 1, 2, 4, 8, 16, 32, 64, 128, 4, 8, 16, 32 };
        d = juce::jlimit (0, 11, d);
        return std::max (64, (int) std::lround (4.0 / DV[d] * (d >= 8 ? 2.0 / 3.0 : 1.0) * 60.0 / p.bpm * sr));
    }
    void enter (int m)
    {
        mode = m; t = 0; wet = 0;
        if (m == 1) { len = divLen (gl.kind == 1 ? gl.div : p.div); start = (w - len + N) % N; }
        else if (m == 2) { len = divLen (gl.kind == 2 ? 4 : 2); start = w; }
        else if (m == 3) { rp = w - 1; rate = 1; }
        else if (m == 4) { len = (int) (sr * 0.12); start = (w - len + N) % N; }
        else fin = 0;
    }
    void onStep() // every 16th: random glitch events
    {
        if (gl.kind) { if (--gl.left <= 0) gl = {}; return; }
        if (rng.nextFloat() < p.glitch * 0.5f)
        {
            static constexpr int divs[] = { 4, 5, 6, 7, 9, 10 };
            gl.kind = rng.nextFloat() < 0.25f ? 2 : 1;
            gl.div = divs[rng.nextInt (6)];
            gl.left = rng.nextFloat() < 0.4f ? 2 : 1;
        }
    }
    double hilb (int c, double x, double cs, double sn)
    {
        double a = x, b = x;
        for (int k = 0; k < 4; ++k)
        {
            double* q = hs + ((c * 2 + 0) * 4 + k) * 4;
            const double y = H1[k] * (a + q[3]) - q[1]; q[1] = q[0]; q[0] = a; q[3] = q[2]; q[2] = y; a = y;
        }
        for (int k = 0; k < 4; ++k)
        {
            double* q = hs + ((c * 2 + 1) * 4 + k) * 4;
            const double y = H2[k] * (b + q[3]) - q[1]; q[1] = q[0]; q[0] = b; q[3] = q[2]; q[2] = y; b = y;
        }
        const double I = hd[c]; hd[c] = a;
        return I * cs - b * sn;
    }
};

/* Feed-forward compressor with 6 dB soft knee; exposes gain reduction for the meter. */
struct Comp
{
    double grDb = 0, atk = 0, rel = 0;
    void prepare (double sr) { atk = std::exp (-1.0 / (0.005 * sr)); rel = std::exp (-1.0 / (0.12 * sr)); grDb = 0; }
    float gain (float level, float thrDb, float ratio)
    {
        const double over = 20 * std::log10 (level + 1e-9) - thrDb, slope = 1 - 1 / ratio;
        const double want = over <= -3 ? 0 : over < 3 ? slope * (over + 3) * (over + 3) / 12 : slope * over;
        const double k = want > grDb ? atk : rel;
        grDb = k * grDb + (1 - k) * want;
        return (float) std::pow (10.0, -grDb / 20);
    }
};

/* Linear ramp across one control sub-block — zipper-free gains at 32-sample control rate. */
struct Ramp
{
    float a = 0, b = 0;
    void next (float target) { a = b; b = target; }
    void snap (float v) { a = b = v; }
    float at (float f) const { return a + (b - a) * f; }
};
} // namespace skatom
