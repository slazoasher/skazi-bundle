#pragma once

#include "Util.h"
#include "ReverbDSP.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>
#include <vector>

// GALAXY FX: 10 reorderable units + master. Everything is preallocated in prepare(); process() never allocates.
namespace skyz
{
    struct FxContext { double bpm = 120, beat = 0; float sr = 48000; }; // beat = host ppq at the block start (or a free clock)

    // RBJ biquad, coefficients computed in place (no allocation).
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
        void shelf (bool high, float hz, float db, float sr) noexcept
        {
            const float A = std::pow (10.0f, db / 40.0f), w = twoPi * hz / sr, cs = std::cos (w), sn = std::sin (w), al = sn / 2.0f * std::sqrt (2.0f), sq = 2.0f * std::sqrt (A) * al;
            float B0, B1, B2, A0, A1, A2;
            if (! high) { B0 = A * ((A + 1) - (A - 1) * cs + sq); B1 = 2 * A * ((A - 1) - (A + 1) * cs); B2 = A * ((A + 1) - (A - 1) * cs - sq); A0 = (A + 1) + (A - 1) * cs + sq; A1 = -2 * ((A - 1) + (A + 1) * cs); A2 = (A + 1) + (A - 1) * cs - sq; }
            else { B0 = A * ((A + 1) + (A - 1) * cs + sq); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - sq); A0 = (A + 1) - (A - 1) * cs + sq; A1 = 2 * ((A - 1) - (A + 1) * cs); A2 = (A + 1) - (A - 1) * cs - sq; }
            set (B0, B1, B2, A0, A1, A2);
        }
        void peak (float hz, float q, float db, float sr) noexcept
        {
            const float A = std::pow (10.0f, db / 40.0f), w = twoPi * hz / sr, cs = std::cos (w), al = std::sin (w) / (2.0f * q);
            set (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
        }
        void bandpass (float hz, float q, float sr) noexcept
        {
            const float w = twoPi * hz / sr, cs = std::cos (w), al = std::sin (w) / (2.0f * q);
            set (al, 0, -al, 1 + al, -2 * cs, 1 - al);
        }
        void set (float B0, float B1, float B2, float A0, float A1, float A2) noexcept { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
        float run (int c, float x) noexcept { const float y = b0 * x + z1[c]; z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y; return y; }
        void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    };

    struct Ring // power-of-two delay line with fractional read
    {
        std::vector<float> d; int mask = 0, w = 0;
        void prepare (int minSize) { int n = 1; while (n < minSize) n <<= 1; d.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
        void push (float x) noexcept { d[(size_t) w] = x; w = (w + 1) & mask; }
        float read (float delaySamples) const noexcept
        {
            float rp = (float) w - 1.0f - delaySamples; while (rp < 0) rp += (float) (mask + 1);
            const int i = (int) rp; const float f = rp - (float) i;
            return d[(size_t) (i & mask)] + f * (d[(size_t) ((i + 1) & mask)] - d[(size_t) (i & mask)]);
        }
        void clear() { std::fill (d.begin(), d.end(), 0.0f); }
    };

    class FxChain
    {
    public:
        enum Unit { uDrive, uChorus, uPhaser, uDelay, uReverb, uGate, uDynamics, uEq, uStereo, uLofi, numUnits };
        std::atomic<float> peakL { 0 }, peakR { 0 };
        std::atomic<float> trimDb { 0 }; // preset level match, applied at the master gain (after the FX, before the limiters)

        void prepare (double sampleRate, int blockSize)
        {
            sr = (float) sampleRate;
            for (auto& r : chorusLine) r.prepare ((int) (sr * 0.1f));
            for (auto& r : flangeLine) r.prepare ((int) (sr * 0.02f));
            for (auto& r : delayLine) r.prepare ((int) (sr * 8.5f));
            for (auto& r : haasLine) r.prepare ((int) (sr * 0.05f));
            for (auto& r : wowLine) r.prepare ((int) (sr * 0.05f));
            if (! reverb) reverb = std::make_unique<verb::Engine>();
            reverb->prepare (sampleRate);
            wetL.assign ((size_t) juce::jmax (blockSize, 64), 0.0f); wetR = wetL;
            const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (blockSize, 64), 2 };
            for (auto* f : { &xo1, &xo2 }) { f->prepare (spec); f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass); }
            xo1.setCutoffFrequency (120.0f); xo2.setCutoffFrequency (2500.0f);
            limiter.prepare (spec); safety.prepare (spec);
            safety.setThreshold (-0.3f); safety.setRelease (60.0f);
            masterGain.reset (sampleRate, 0.05);
            snapGain = true;
            reset();
        }
        void reset()
        {
            for (auto& r : chorusLine) r.clear(); for (auto& r : flangeLine) r.clear(); for (auto& r : delayLine) r.clear(); for (auto& r : haasLine) r.clear(); for (auto& r : wowLine) r.clear();
            std::fill (&phAp[0][0], &phAp[0][0] + 48, 0.0f); std::fill (&phAp2[0][0], &phAp2[0][0] + 48, 0.0f);
            for (auto* b : { &eqLo, &eqMid, &eqHi, &tiltLo, &tiltHi, &radioBp, &fbLo, &fbHi }) b->reset();
            std::fill (std::begin (ottEnv), std::end (ottEnv), 0.0f);
            gateEnv = 1; compEnv = 0; xo1.reset(); xo2.reset(); limiter.reset(); safety.reset();
        }
        void setOrder (const std::array<int, numUnits>& o) { for (int i = 0; i < numUnits; ++i) order[(size_t) i].store (o[(size_t) i]); }

        void process (juce::AudioBuffer<float>& buf, const Values& p, const FxContext& ctx)
        {
            const int n = buf.getNumSamples();
            if (buf.getNumChannels() < 2 || n == 0) return;
            float* l = buf.getWritePointer (0); float* r = buf.getWritePointer (1);
            bps = (float) (ctx.bpm / 60.0 / sr);
            for (int k = 0; k < numUnits; ++k)
            {
                const int u = order[(size_t) k].load();
                switch (u)
                {
                    case uDrive:    if (p.on (P_onDRIVE)) drive (l, r, n, p); break;
                    case uChorus:   if (p.on (P_onCHORUS) && p[P_cMix] > 0.001f) chorus (l, r, n, p); break;
                    case uPhaser:   if (p.on (P_onPHASER) && p[P_pMix] > 0.001f) phaser (l, r, n, p); break;
                    case uDelay:    if (p.on (P_onDELAY)) delay (l, r, n, p, ctx); break;
                    case uReverb:   if (p.on (P_onREVERB)) reverbUnit (l, r, n, p); break;
                    case uGate:     if (p.on (P_onGATE)) gate (l, r, n, p, ctx); break;
                    case uDynamics: if (p.on (P_onDYNAMICS)) dynamics (l, r, n, p, ctx, buf); break;
                    case uEq:       if (p.on (P_onEQ)) eq (l, r, n, p); break;
                    case uStereo:   if (p.on (P_onSTEREO)) stereo (l, r, n, p, ctx); break;
                    case uLofi:     if (p.on (P_onLOFI)) lofi (l, r, n, p); break;
                    default: break;
                }
            }
            master (buf, p);
            float pl = 0, pr = 0;
            for (int i = 0; i < n; ++i) { pl = juce::jmax (pl, std::abs (l[i])); pr = juce::jmax (pr, std::abs (r[i])); }
            const float fall = std::exp (-(float) n / (0.25f * sr)); // meter falls over ~0.25 s whatever the block size
            peakL = juce::jmax (pl, peakL.load() * fall); peakR = juce::jmax (pr, peakR.load() * fall);
        }

    private:
        //-------------------------------------------------------------------- DRIVE
        static float shape (int type, float d, float x) noexcept
        {
            const float k = 1.0f + d * 24.0f;
            switch (type)
            {
                case 0:  { const float v = x * k; return v >= 0.0f ? 1.0f - std::exp (-v) : -(1.0f - std::exp (v * 1.4f)) * 0.75f; }  // tube
                case 1:  { const float v = x * (1.0f + d * 6.0f); return v / std::pow (1.0f + std::pow (std::abs (v), 2.5f), 0.4f); }  // tape
                case 2:  { const float v = x * k; return v >= 0.0f ? 1.0f - std::exp (-v * 1.5f) : -(1.0f - std::exp (v * 0.5f)) * 0.6f; } // diode
                case 3:  { const float v = x * k * 2.0f; return (v >= 0.0f ? 1.0f : -1.0f) * (1.0f - std::exp (-std::abs (v) * 4.0f)); } // fuzz
                case 4:  return std::sin (x * k * pi * 0.5f);                                                                              // fold
                case 5:  { const float bits = std::exp2 (12.0f - d * 9.0f); return std::round (juce::jlimit (-1.0f, 1.0f, x * (1.0f + d * 3.0f)) * bits) / bits; }
                case 6:  return std::tanh (std::abs (x) * k) * 1.4f - 0.2f;                                                                // rectify
                case 7:  { const float v = x * k; return std::tanh (v + 0.8f * std::sin (v * 2.0f)); }                                     // scream
                case 8:  return 0.6366f * std::atan (x * k);                                                                               // overdrive
                case 9:  return juce::jlimit (-1.0f, 1.0f, x * k);                                                                         // hard clip
                default: return std::tanh (x * (1.0f + d * 8.0f));                                                                         // soft
            }
        }
        void drive (float* l, float* r, int n, const Values& p) noexcept
        {
            // bias shifts the operating point inside the curve (scaled by the drive gain), so it colours the sound but can never pin it flat
            const int type = p.idx (P_xType); const float d = p[P_xDrive], bias = p.bi (P_xBias) * 0.35f / (1.0f + d * 8.0f), mix = p[P_xMix];
            const float comp = 1.0f / (1.0f + d * 3.0f) * juce::Decibels::decibelsToGain (p.lin (P_xOut, -12, 12));
            preHp[0].setHz (p.ex (P_xPre, 20, 2000), sr); preHp[1].a = preHp[0].a;
            toneLp[0].setHz (p.ex (P_xTone, 500, 20000), sr); toneLp[1].a = toneLp[0].a;
            dcB[0].setHz (12.0f, sr); dcB[1].a = dcB[0].a;
            const float offset = shape (type, d, bias); // what the bias alone makes: removed so silence stays silent (no DC thump)
            float* ch[2] { l, r };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float x = ch[c][i];
                    float y = shape (type, d, preHp[c].hp (x) + bias) - offset;
                    y = toneLp[c].lp (dcB[c].hp (y)) * comp;
                    ch[c][i] = x + (y - x) * mix;
                }
        }
        //-------------------------------------------------------------------- CHORUS
        void chorus (float* l, float* r, int n, const Values& p) noexcept
        {
            const int type = p.idx (P_cType);
            const float rate = p.ex (P_cRate, 0.05f, 10), base = p.lin (P_cDelay, 1, 40) * 0.001f * sr, depth = p[P_cDepth] * base * 0.8f, fb = p[P_cFb] * 0.85f, width = p[P_cWidth], mix = p[P_cMix];
            const float inc = rate / sr;
            for (int i = 0; i < n; ++i)
            {
                chPh += inc; chPh -= std::floor (chPh);
                const float dl = l[i], dr = r[i];
                float wl = 0, wr = 0;
                switch (type)
                {
                    case 1: // dimension: four short fixed-ish taps cross-mixed
                        for (int t = 0; t < 4; ++t) { const float d = sr * (0.002f + 0.0015f * (float) t) + depth * 0.15f * std::sin (twoPi * (chPh + 0.25f * (float) t));
                            wl += chorusLine[t & 1].read (d) * (t & 1 ? 0.35f : 0.65f); wr += chorusLine[(t + 1) & 1].read (d) * (t & 1 ? 0.65f : 0.35f); }
                        wl *= 0.5f; wr *= 0.5f; break;
                    case 2: // ensemble: three phases plus a fast shimmer
                        for (int t = 0; t < 3; ++t) { const float ph = chPh + (float) t / 3.0f; const float d = base + depth * (0.7f * std::sin (twoPi * ph) + 0.3f * std::sin (twoPi * ph * 7.3f));
                            wl += chorusLine[0].read (d); wr += chorusLine[1].read (base + depth * (0.7f * std::sin (twoPi * (ph + 0.5f)) + 0.3f * std::sin (twoPi * ph * 6.1f))); }
                        wl /= 3.0f; wr /= 3.0f; break;
                    case 4: // rotary: doppler + amplitude, the two horns opposite
                    {   const float s = std::sin (twoPi * chPh), c = std::cos (twoPi * chPh);
                        wl = chorusLine[0].read (base * 0.3f + depth * 0.5f * (1 + s)) * (0.75f + 0.25f * c);
                        wr = chorusLine[1].read (base * 0.3f + depth * 0.5f * (1 - s)) * (0.75f - 0.25f * c); break; }
                    default: // classic + vibrato
                        wl = chorusLine[0].read (base + depth * std::sin (twoPi * chPh));
                        wr = chorusLine[1].read (base + depth * std::sin (twoPi * (chPh + 0.25f)));
                }
                chorusLine[0].push (dl + wl * fb); chorusLine[1].push (dr + wr * fb);
                const float m = 0.5f * (wl + wr); wl = m + (wl - m) * width; wr = m + (wr - m) * width;
                if (type == 3) { l[i] = dl + (wl - dl) * mix; r[i] = dr + (wr - dr) * mix; }
                else { l[i] = dl * (1.0f - mix * 0.4f) + wl * mix; r[i] = dr * (1.0f - mix * 0.4f) + wr * mix; }
            }
        }
        //-------------------------------------------------------------------- PHASER / FLANGER
        void phaser (float* l, float* r, int n, const Values& p) noexcept
        {
            const int type = p.idx (P_pType);
            const float rate = p.ex (P_pRate, 0.03f, 10), depth = p[P_pDepth], fb = p.bi (P_pFb) * 0.9f, centre = p.ex (P_pFreq, 100, 8000), mix = p[P_pMix];
            const float inc = rate / sr;
            const int stages = type == 0 ? 4 : type == 1 ? 8 : 12;
            for (int i = 0; i < n; ++i)
            {
                phPh += inc; phPh -= std::floor (phPh);
                const float x[2] { l[i], r[i] };
                float y[2];
                if (type == 3) // flanger
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        const float d = sr * (0.0004f + 0.0055f * depth * (0.5f + 0.5f * std::sin (twoPi * (phPh + 0.25f * (float) c))));
                        const float w = flangeLine[c].read (d);
                        flangeLine[c].push (x[c] + w * fb);
                        y[c] = w;
                    }
                }
                else
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        auto sweep = [&] (float ph) { return juce::jlimit (30.0f, sr * 0.45f, centre * std::exp2 (depth * 3.0f * std::sin (twoPi * ph))); };
                        auto run = [&] (float* st, float hz, float in, float& last)
                        {
                            const float t = std::tan (pi * hz / sr), a = (t - 1.0f) / (t + 1.0f);
                            float v = in + last * fb;
                            for (int s = 0; s < stages; ++s) { const float o = a * v + st[s]; st[s] = v - a * o; v = o; }
                            last = v; return v;
                        };
                        if (type == 4) // barberpole: two saw sweeps half a cycle apart, crossfaded so it seems to rise forever
                        {
                            const float p1 = std::fmod (phPh + 0.5f * (float) c * 0.1f, 1.0f), p2 = std::fmod (p1 + 0.5f, 1.0f);
                            const float hz1 = juce::jlimit (30.0f, sr * 0.45f, centre * std::exp2 (depth * 6.0f * (p1 - 0.5f))), hz2 = juce::jlimit (30.0f, sr * 0.45f, centre * std::exp2 (depth * 6.0f * (p2 - 0.5f)));
                            const float w1 = std::sin (pi * p1), w2 = std::sin (pi * p2);
                            y[c] = run (phAp[c], hz1, x[c], phLast[c]) * w1 + run (phAp2[c], hz2, x[c], phLast2[c]) * w2;
                        }
                        else y[c] = run (phAp[c], sweep (phPh + 0.25f * (float) c), x[c], phLast[c]);
                    }
                }
                l[i] = x[0] + (y[0] - x[0]) * mix * 0.5f + y[0] * mix * 0.5f;
                r[i] = x[1] + (y[1] - x[1]) * mix * 0.5f + y[1] * mix * 0.5f;
            }
        }
        //-------------------------------------------------------------------- DELAY
        void delay (float* l, float* r, int n, const Values& p, const FxContext& ctx) noexcept
        {
            const int type = p.idx (P_yType);
            const float mix = p[P_yMix];
            const bool freeze = p.on (P_yFreeze);
            if (mix <= 0.001f && ! freeze) return;
            auto secs = [&] (int id) { return (float) (divBeats (p.list (id, numDivs)) * 60.0 / ctx.bpm); };
            float tl = juce::jlimit (0.002f, 8.0f, secs (P_yTimeL)) * sr, tr = juce::jlimit (0.002f, 8.0f, secs (P_yTimeR)) * sr;
            if (type == 4) { tl = (0.06f + p[P_yTimeL] * 0.1f) * sr; tr = tl * 1.07f; }
            float fb = freeze ? 1.0f : p[P_yFb] * (type == 3 ? 1.05f : type == 4 ? 0.5f : 1.0f);
            const float width = p[P_yWidth], wow = p[P_yMod], duck = p[P_yDuck], sat = p[P_ySat];
            fbLoHz = p.ex (P_yLo, 20, 2000); fbHiHz = p.ex (P_yHi, 1000, 20000);
            if (type == 3) { fbLoHz = juce::jmax (fbLoHz, 250.0f); fbHiHz = juce::jmin (fbHiHz, 2500.0f); }
            for (int c = 0; c < 2; ++c) { dLp[c].setHz (fbHiHz, sr); dHp[c].setHz (fbLoHz, sr); }
            duckF.setHz (6.0f, sr);
            for (int i = 0; i < n; ++i)
            {
                dlWow += (0.4f + 0.9f * wow) / sr; dlWow -= std::floor (dlWow);
                const float mod = (type == 2 || wow > 0.001f) ? sr * 0.0025f * wow * std::sin (twoPi * dlWow) : 0.0f;
                const float xl = freeze ? 0.0f : l[i], xr = freeze ? 0.0f : r[i];
                float yl, yr;
                if (type == 5) // reverse: play the last window backwards, with a smooth window
                {
                    for (int c = 0; c < 2; ++c) { revPos[c] += 1.0f; if (revPos[c] >= (c ? tr : tl)) revPos[c] = 0; }
                    const float wl = std::sin (pi * revPos[0] / tl), wr = std::sin (pi * revPos[1] / tr);
                    yl = delayLine[0].read (2.0f * revPos[0]) * wl; yr = delayLine[1].read (2.0f * revPos[1]) * wr;
                }
                else if (type == 6) // grain: four Hann grains per side around the delay time, slightly detuned
                {
                    yl = yr = 0;
                    for (int g = 0; g < 4; ++g)
                    {
                        auto& gr = grains[g];
                        if (gr.age >= gr.len) { gr.len = (int) (sr * (0.05f + 0.1f * rng.uni())); gr.age = 0; gr.off = rng.uni() * 0.5f; gr.speed = 1.0f + rng.bip() * (0.003f + wow * 0.03f); gr.pos = 0; }
                        const float w = 0.5f - 0.5f * std::cos (twoPi * (float) gr.age / (float) gr.len);
                        gr.pos += 1.0f - gr.speed; ++gr.age;
                        yl += delayLine[0].read (juce::jlimit (2.0f, delayLine[0].d.size() - 4.0f, tl * (1.0f + gr.off) + gr.pos)) * w;
                        yr += delayLine[1].read (juce::jlimit (2.0f, delayLine[1].d.size() - 4.0f, tr * (1.0f + gr.off) + gr.pos)) * w;
                    }
                    yl *= 0.5f; yr *= 0.5f;
                }
                else { yl = delayLine[0].read (tl + mod); yr = delayLine[1].read (tr + mod); }

                float fl = yl, fr = yr;
                if (! freeze)
                {
                    fl = dLp[0].lp (dHp[0].hp (fl)); fr = dLp[1].lp (dHp[1].hp (fr));
                    if (sat > 0.001f || type == 2 || type == 3) { const float s = 1.0f + sat * 3.0f + (type >= 2 ? 0.5f : 0.0f); fl = std::tanh (fl * s) / s * (1.0f + sat * 0.5f); fr = std::tanh (fr * s) / s * (1.0f + sat * 0.5f); }
                }
                if (type == 0) { delayLine[0].push ((xl + xr) * 0.5f + fr * fb); delayLine[1].push (fl * fb); }
                else { delayLine[0].push (xl + fl * fb); delayLine[1].push (xr + fr * fb); }

                const float env = duckF.lp (std::abs (l[i]) + std::abs (r[i]));
                const float dk = 1.0f - juce::jlimit (0.0f, 0.9f, duck * env * 3.0f);
                const float m = 0.5f * (yl + yr); yl = m + (yl - m) * width; yr = m + (yr - m) * width;
                l[i] += yl * mix * dk; r[i] += yr * mix * dk;
            }
        }
        //-------------------------------------------------------------------- REVERB
        void reverbUnit (float* l, float* r, int n, const Values& p) noexcept
        {
            const float mix = p[P_rMix];
            if (mix <= 0.001f) return;
            const int type = p.idx (P_rType);
            verb::Settings s;
            s.mode = type == 10 ? verb::hall : type;
            s.size = type == 10 ? 1.0f : p[P_rSize];
            s.decay = p.pw (P_rDecay, 0.2f, 30.0f) * (type == 10 ? 1.6f : 1.0f);
            s.preDelayMs = p.lin (P_rPre, 0, 500);
            s.damping = p[P_rDamp]; s.diffusion = p[P_rDiff]; s.modDepth = type == 10 ? 0.8f : p[P_rMod]; s.early = p[P_rEarly];
            s.width = p.lin (P_rWidth, 0, 2); s.lowCut = p.ex (P_rLo, 20, 2000); s.highCut = p.ex (P_rHi, 1000, 20000);
            s.shimmerAmount = type == 10 ? juce::jmax (0.45f, p[P_rShim]) : (type == 6 ? juce::jmax (0.5f, p[P_rShim]) : p[P_rShim]);
            s.freeze = p.on (P_rFreeze);
            const float duck = p[P_rDuck], dry = 1.0f - mix * 0.3f;
            rvDuck.setHz (4.0f, sr);
            for (int start = 0; start < n; start += (int) wetL.size())
            {
                const int len = juce::jmin ((int) wetL.size(), n - start);
                reverb->process (l + start, r + start, wetL.data(), wetR.data(), len, s);
                for (int i = 0; i < len; ++i)
                {
                    const float env = rvDuck.lp (std::abs (l[start + i]) + std::abs (r[start + i]));
                    const float dk = 1.0f - juce::jlimit (0.0f, 0.9f, duck * env * 3.0f);
                    l[start + i] = l[start + i] * dry + wetL[(size_t) i] * mix * dk;
                    r[start + i] = r[start + i] * dry + wetR[(size_t) i] * mix * dk;
                }
            }
        }
        //-------------------------------------------------------------------- TRANCE GATE
        void gate (float* l, float* r, int n, const Values& p, const FxContext& ctx) noexcept
        {
            static const float lens[] { 0.125f, 0.25f, 0.5f, 1.0f };
            const float len = lens[p.list (P_gRate, 4)], depth = p[P_gDepth], mix = p[P_gMix];
            const float atk = 1.0f - std::exp (-1.0f / (expo (p[P_gAtk], 0.0003f, 0.03f) * sr)), rel = 1.0f - std::exp (-1.0f / (expo (p[P_gRel], 0.001f, 0.2f) * sr));
            for (int i = 0; i < n; ++i)
            {
                const double beat = ctx.beat + (double) i * bps;
                const int step = (int) std::floor (beat / len) & 15;
                const float tgt = p.on (P_g0 + step) ? 1.0f : 1.0f - depth;
                gateEnv += (tgt - gateEnv) * (tgt > gateEnv ? atk : rel);
                const float g = 1.0f + (gateEnv - 1.0f) * mix;
                l[i] *= g; r[i] *= g;
            }
        }
        //-------------------------------------------------------------------- DYNAMICS
        void dynamics (float* l, float* r, int n, const Values& p, const FxContext& ctx, juce::AudioBuffer<float>& buf) noexcept
        {
            const int type = p.idx (P_dType);
            const float depth = p[P_dDepth], out = juce::Decibels::decibelsToGain (p.lin (P_dGain, -12, 12));
            const float tconst = expo (p[P_dTime], 0.002f, 0.3f);
            const float att = 1.0f - std::exp (-1.0f / (tconst * 0.2f * sr)), relc = 1.0f - std::exp (-1.0f / (tconst * sr));
            if (type == 0 && depth > 0.001f) // OTT: 3 bands, upward + downward
            {
                const float up = p[P_dUp] * depth, down = p[P_dDown] * depth;
                for (int i = 0; i < n; ++i)
                {
                    float o[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        const float x = c ? r[i] : l[i];
                        float lo, rest, mid, hi;
                        xo1.processSample (c, x, lo, rest);
                        xo2.processSample (c, rest, mid, hi);
                        const float bands[3] { lo, mid, hi };
                        float sum = 0;
                        for (int b = 0; b < 3; ++b)
                        {
                            float& e = ottEnv[b * 2 + c];
                            const float a = std::abs (bands[b]);
                            e += (a - e) * (a > e ? att : relc);
                            const float db = juce::Decibels::gainToDecibels (e + 1e-6f, -100.0f);
                            float gdb = 0;
                            if (db > -18.0f) gdb -= (db + 18.0f) * 0.75f * down;
                            else if (db < -36.0f && db > -80.0f) gdb += juce::jmin (18.0f, (-36.0f - db) * 0.6f) * up;
                            sum += bands[b] * juce::Decibels::decibelsToGain (gdb + depth * 4.0f);
                        }
                        o[c] = sum;
                    }
                    l[i] = o[0] * out; r[i] = o[1] * out;
                }
                return;
            }
            if (type == 1 && depth > 0.001f) // glue: RMS-ish bus compressor
            {
                const float ratio = 2.0f + depth * 6.0f, thr = -24.0f + (1.0f - depth) * 12.0f, makeup = juce::Decibels::decibelsToGain (-thr * (1.0f - 1.0f / ratio) * 0.2f);
                for (int i = 0; i < n; ++i)
                {
                    const float a = juce::jmax (std::abs (l[i]), std::abs (r[i]));
                    compEnv += (a - compEnv) * (a > compEnv ? att : relc);
                    const float db = juce::Decibels::gainToDecibels (compEnv + 1e-6f, -100.0f);
                    const float g = db > thr ? juce::Decibels::decibelsToGain (-(db - thr) * (1.0f - 1.0f / ratio)) : 1.0f;
                    l[i] *= g * makeup * out; r[i] *= g * makeup * out;
                }
                return;
            }
            if (type == 2 && depth > 0.001f) // limiter
            {
                limiter.setThreshold (-depth * 12.0f); limiter.setRelease (20.0f + p[P_dTime] * 300.0f);
                juce::dsp::AudioBlock<float> block (buf);
                limiter.process (juce::dsp::ProcessContextReplacing<float> (block));
                juce::FloatVectorOperations::multiply (l, out * juce::Decibels::decibelsToGain (depth * 6.0f), n);
                juce::FloatVectorOperations::multiply (r, out * juce::Decibels::decibelsToGain (depth * 6.0f), n);
                return;
            }
            if (type >= 3 && depth > 0.001f) // pump: tempo-locked sidechain duck
            {
                static const float lens[] { 1.0f, 0.5f, 0.25f };
                const float len = lens[type - 3], curve = 1.0f + p[P_dPump] * 5.0f;
                for (int i = 0; i < n; ++i)
                {
                    const double beat = ctx.beat + (double) i * bps;
                    const float ph = (float) (beat / len - std::floor (beat / len));
                    const float g = 1.0f - depth * std::pow (1.0f - ph, curve);
                    pumpG += (g - pumpG) * att * 4.0f;
                    l[i] *= pumpG * out; r[i] *= pumpG * out;
                }
                return;
            }
            if (std::abs (out - 1.0f) > 1e-4f) { juce::FloatVectorOperations::multiply (l, out, n); juce::FloatVectorOperations::multiply (r, out, n); }
        }
        //-------------------------------------------------------------------- EQ
        void eq (float* l, float* r, int n, const Values& p) noexcept
        {
            const float key[8] { p[P_qLo], p[P_qLoF], p[P_qMid], p[P_qMidF], p[P_qQ], p[P_qHi], p[P_qHiF], p[P_qTilt] };
            if (! std::equal (std::begin (key), std::end (key), std::begin (eqKey)))
            {
                std::copy (std::begin (key), std::end (key), std::begin (eqKey));
                eqLo.shelf (false, p.ex (P_qLoF, 20, 500), p.lin (P_qLo, -15, 15), sr);
                eqMid.peak (p.ex (P_qMidF, 200, 8000), p.ex (P_qQ, 0.3f, 10), p.lin (P_qMid, -15, 15), sr);
                eqHi.shelf (true, p.ex (P_qHiF, 2000, 18000), p.lin (P_qHi, -15, 15), sr);
                const float t = p.bi (P_qTilt) * 6.0f;
                tiltLo.shelf (false, 1000.0f, -t, sr); tiltHi.shelf (true, 1000.0f, t, sr);
            }
            for (int i = 0; i < n; ++i)
            {
                l[i] = tiltHi.run (0, tiltLo.run (0, eqHi.run (0, eqMid.run (0, eqLo.run (0, l[i])))));
                r[i] = tiltHi.run (1, tiltLo.run (1, eqHi.run (1, eqMid.run (1, eqLo.run (1, r[i])))));
            }
        }
        //-------------------------------------------------------------------- STEREO
        void stereo (float* l, float* r, int n, const Values& p, const FxContext& ctx) noexcept
        {
            static const float lens[] { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
            const float width = p[P_sWidth] * 2.0f, haas = p.lin (P_sHaas, 0, 30) * 0.001f * sr, monoF = p.ex (P_sMonoF, 20, 500), pan = p[P_sPan], len = lens[p.list (P_sRate, 5)];
            sideLp.setHz (monoF, sr);
            for (int i = 0; i < n; ++i)
            {
                float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]);
                if (p[P_sMonoF] > 0.01f) s -= sideLp.lp (s);
                s *= width;
                float L = m + s, R = m - s;
                haasLine[0].push (R);
                if (haas > 1.0f) R = haasLine[0].read (haas);
                if (pan > 0.001f)
                {
                    const double beat = ctx.beat + (double) i * bps;
                    const float a = pan * std::sin (twoPi * (float) (beat / len - std::floor (beat / len)));
                    L *= 1.0f - juce::jmax (0.0f, a); R *= 1.0f + juce::jmin (0.0f, a);
                }
                l[i] = L; r[i] = R;
            }
        }
        //-------------------------------------------------------------------- LO-FI
        void lofi (float* l, float* r, int n, const Values& p) noexcept
        {
            const int type = p.idx (P_lType);
            const float bits = std::exp2 (std::round (p.lin (P_lBits, 2, 16)) - 1.0f), hold = juce::jmax (1.0f, sr / p.ex (P_lRate, 1000, 48000)), noise = p[P_lNoise], wow = p[P_lWow], mix = p[P_lMix];
            if (type == 4) radioBp.bandpass (1200.0f, 0.7f, sr);
            lfLp[0].setHz (type == 2 ? 9000.0f : 11000.0f, sr); lfLp[1].a = lfLp[0].a;
            lfHp[0].setHz (type == 3 ? 60.0f : 25.0f, sr); lfHp[1].a = lfHp[0].a;
            for (int i = 0; i < n; ++i)
            {
                float x[2] { l[i], r[i] };
                if (type >= 2 && wow > 0.001f)
                {
                    wowPh += (0.5f + (type == 3 ? 6.0f * rng.uni() * 0.05f : 0.0f)) / sr; wowPh -= std::floor (wowPh);
                    const float d = sr * (0.004f + 0.003f * wow * (std::sin (twoPi * wowPh) + (type == 3 ? 0.3f * std::sin (twoPi * wowPh * 13.0f) : 0.0f)));
                    for (int c = 0; c < 2; ++c) { wowLine[c].push (x[c]); x[c] = wowLine[c].read (d); }
                }
                if (++holdCount >= hold) { holdCount -= hold; heldS[0] = x[0]; heldS[1] = x[1]; }
                for (int c = 0; c < 2; ++c)
                {
                    float y = std::round (heldS[c] * bits) / bits;
                    switch (type)
                    {
                        case 2: y = lfLp[c].lp (y) + (rng.uni() < 0.0006f * (0.3f + noise) ? rng.bip() * 0.6f * noise : 0.0f); break;
                        case 3: y = lfLp[c].lp (lfHp[c].hp (std::tanh (y * 1.6f) / 1.3f)) + rng.bip() * noise * 0.02f; break;
                        case 4: y = std::tanh (radioBp.run (c, y) * 3.0f) * 0.5f + rng.bip() * noise * 0.03f; break;
                        default: y += rng.bip() * noise * 0.01f;
                    }
                    x[c] = (c ? r[i] : l[i]) + (y - (c ? r[i] : l[i])) * mix;
                }
                l[i] = x[0]; r[i] = x[1];
            }
        }
        //-------------------------------------------------------------------- MASTER
        void master (juce::AudioBuffer<float>& buf, const Values& p) noexcept
        {
            const int n = buf.getNumSamples();
            float* l = buf.getWritePointer (0); float* r = buf.getWritePointer (1);
            const float target = juce::Decibels::decibelsToGain (p.lin (P_master, -24, 6) + trimDb.load());
            if (snapGain) { masterGain.setCurrentAndTargetValue (target); snapGain = false; } // after a restart: start at the level, no fade-in
            else masterGain.setTargetValue (target);
            const bool on = p.on (P_onMASTER);
            const float clip = on ? p[P_mClip] : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float g = masterGain.getNextValue();
                float a = l[i] * g, b = r[i] * g;
                if (clip > 0.001f) { const float k = 1.0f + clip * 4.0f; a = a + (std::tanh (a * k) / k * (1.0f + clip) - a) * clip; b = b + (std::tanh (b * k) / k * (1.0f + clip) - b) * clip; }
                l[i] = a; r[i] = b;
            }
            juce::dsp::AudioBlock<float> block (buf);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            if (on) { limiter.setThreshold (p.lin (P_mLim, -12, 0)); limiter.setRelease (60.0f); limiter.process (ctx); }
            safety.process (ctx); // never louder than -0.3 dBFS
        }

        float sr = 48000, bps = 0;
        std::array<std::atomic<int>, numUnits> order { { {0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9} } };
        Rng rng { 777 };
        OnePole preHp[2], toneLp[2], dcB[2], dLp[2], dHp[2], duckF, rvDuck, sideLp, lfLp[2], lfHp[2];
        Ring chorusLine[2], flangeLine[2], delayLine[2], haasLine[1], wowLine[2];
        float chPh = 0, phPh = 0, phAp[2][24] {}, phAp2[2][24] {}, phLast[2] {}, phLast2[2] {};
        float dlWow = 0, revPos[2] {}, fbLoHz = 20, fbHiHz = 20000;
        struct Grain { int age = 1, len = 0; float off = 0, speed = 1, pos = 0; } grains[4];
        std::unique_ptr<verb::Engine> reverb; std::vector<float> wetL, wetR;
        float gateEnv = 1, compEnv = 0, pumpG = 1, ottEnv[6] {};
        juce::dsp::LinkwitzRileyFilter<float> xo1, xo2;
        juce::dsp::Limiter<float> limiter, safety;
        Biquad eqLo, eqMid, eqHi, tiltLo, tiltHi, radioBp, fbLo, fbHi;
        float eqKey[8] { -1, -1, -1, -1, -1, -1, -1, -1 };
        float wowPh = 0, holdCount = 0, heldS[2] {};
        juce::SmoothedValue<float> masterGain;
        bool snapGain = true;
    };
}
