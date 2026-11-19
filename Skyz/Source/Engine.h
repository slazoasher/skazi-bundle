#pragma once

#include "Util.h"
#include <array>
#include <vector>

// SKY Z voice engine.
//   OSC A  STAR FIELD : additive, up to 64 partials ("stars"), each a recursive phasor; tilt / odd-even / inharmonic stretch /
//                       constellation morph / blur / twinkle shape the partial amplitudes at control rate.
//   OSC B  NEBULA     : band-limited analog shapes (polyBLEP) with up to 16 unison voices, FM from A, hard sync.
//   MOONS             : sub, 5 noise colours, ring A x B.
//   EVENT HORIZON     : 12 filter types (ladder 24/12, diode, SVF, notch, comb +/-, formant, vowel, phaser) + filter 2,
//                       serial / parallel / split routing.
//   ECLIPSE           : 3 AHDSR envelopes with 4 curves and loop.  ORBITS: 4 LFOs.  16-slot matrix in knob space.
namespace skyz
{
    constexpr int maxPartials = 64, maxUnison = 16, maxVoices = 32, chunk = 32, numSources = 17, numTargets = 23;

    enum Source { sOff, sLfo1, sLfo2, sLfo3, sLfo4, sModEnv, sFltEnv, sAmpEnv, sVel, sNote, sWheel, sAfter, sRandom, sMac1, sMac2, sMac3, sMac4 };
    enum Target { tOff, tPitch, tMorph, tTilt, tSpread, tOdd, tBlur, tALvl, tBPw, tBFm, tBDet, tBLvl, tCut, tRes, tDrive, tF2Cut, tSub, tNoise, tRing, tWidth, tVolume, tShimmer, tPartials };
    // the knob each target moves (matches TARGETS in web/index.html)
    inline const int targetParam[numTargets] { -1, -1, P_aMorph, P_aTilt, P_aSpread, P_aOdd, P_aBlur, P_aLvl, P_bPw, P_bFm, P_bDet, P_bLvl, P_fCut, P_fRes, P_fDrive, P_f2Cut,
                                               P_subLvl, P_noise, P_ring, P_bPan, -1, P_aShimmer, P_aPart };

    // constellations: the partials a constellation lights up (1-based), FLAT = none
    inline const std::vector<int>& constellation (int i)
    {
        static const std::vector<int> c[] { { 1, 3, 4, 7, 9, 12, 16 }, { 2, 3, 5, 6, 8 }, { 1, 2, 4, 8, 16, 32 }, { 3, 5, 7, 11, 13, 17, 19 }, { 1, 5, 9, 10, 14, 20 }, {} };
        return c[juce::jlimit (0, 5, i)];
    }

    struct Shared // per-block values every voice reads
    {
        const Values* p = nullptr;
        float sr = 48000;
        double bpm = 120;
        float bend = 0;          // semitones, already scaled by the bend range
        float wheel = 0, after = 0;
        float macro[4] {};
        float lfoGlobal[4] {};   // free-running phases for non-retrig LFOs
        struct Slot { int src, dst; float amt; };
        Slot slots[16]; int numSlots = 0;
        float lfoHz[4] {};
        float time = 0;          // seconds, drives twinkle
    };

    //==============================================================================
    struct Env
    {
        enum Stage { idle, attack, hold, decay, sustain, release };
        int stage = idle; float value = 0, x = 0, from = 0;
        float a = 1, h = 0, d = 1, s = 1, r = 1; int curve = 0; bool loop = false;

        void set (float sr, float aS, float hS, float dS, float sus, float rS, int c, bool lp) noexcept
        {
            a = 1.0f / (juce::jmax (0.0005f, aS) * sr); h = hS > 0.0001f ? 1.0f / (hS * sr) : 10.0f;
            d = 1.0f / (juce::jmax (0.002f, dS) * sr); s = sus; r = 1.0f / (juce::jmax (0.003f, rS) * sr); curve = c; loop = lp;
        }
        void on (bool fromZero) noexcept { from = fromZero ? 0.0f : value; if (fromZero) value = 0; x = 0; stage = attack; }
        void off() noexcept { if (stage != idle) { from = value; x = 0; stage = release; } }
        void kill() noexcept { stage = idle; value = 0; }
        static float shapeA (int c, float t) noexcept
        {
            switch (c) { case 1: return t; case 2: return std::sqrt (t); case 3: return t * t; default: return (1.0f - std::exp (-4.0f * t)) * 1.01866f; }
        }
        static float shapeD (int c, float t) noexcept
        {
            switch (c) { case 1: case 3: return t; case 2: return (1.0f - std::exp (-9.0f * t)) * 1.000123f; default: return (1.0f - std::exp (-5.0f * t)) * 1.00678f; }
        }
        float next() noexcept
        {
            switch (stage)
            {
                case attack:  x += a; value = from + (1.0f - from) * shapeA (curve, juce::jmin (1.0f, x)); if (x >= 1.0f) { value = 1; x = 0; stage = hold; } break;
                case hold:    x += h; if (x >= 1.0f) { x = 0; stage = decay; } break;
                case decay:   x += d; value = 1.0f + (s - 1.0f) * shapeD (curve, juce::jmin (1.0f, x)); if (x >= 1.0f) { value = s; x = 0; stage = sustain; } break;
                case sustain: value = s; if (loop) { from = value; x = 0; stage = attack; } break;
                case release: x += r; value = from * (1.0f - shapeD (curve, juce::jmin (1.0f, x))); if (x >= 1.0f) { value = 0; stage = idle; } break;
                default: value = 0; break;
            }
            return value;
        }
        bool active() const noexcept { return stage != idle; }
    };

    //==============================================================================
    inline float lfoShape (int shape, float p, float& held, float& prevHeld, Rng& rng, bool wrapped) noexcept
    {
        if (wrapped) { prevHeld = held; held = rng.bip(); }
        switch (shape)
        {
            case 1:  return 1.0f - 4.0f * std::abs (p - 0.5f);
            case 2:  return 2.0f * p - 1.0f;
            case 3:  return 1.0f - 2.0f * p;
            case 4:  return p < 0.5f ? 1.0f : -1.0f;
            case 5:  return held;
            case 6:  { const float t = 0.5f - 0.5f * std::cos (pi * p); return prevHeld + (held - prevHeld) * t; }
            case 7:  return std::sin (twoPi * p + 0.6f * std::sin (twoPi * p)); // ellipse: fast swing, slow return
            case 8:  return 2.0f * std::exp (-5.0f * p) - 1.0f;                // comet: bright head, long tail
            case 9:  return p < 0.25f ? 1.0f : -1.0f;
            default: return std::sin (twoPi * p);
        }
    }

    //==============================================================================
    class Voice
    {
    public:
        void prepare (float sampleRate)
        {
            sr = sampleRate;
            for (auto& c : comb) c.assign (4096, 0.0f);
            reset();
        }
        void reset()
        {
            amp.kill(); flt.kill(); modE.kill();
            for (auto& c : comb) std::fill (c.begin(), c.end(), 0.0f);
            std::fill (&lad[0][0], &lad[0][0] + 8, 0.0f);
            for (auto& s : svf1) s.reset(); for (auto& s : svf2) s.reset();
            for (auto& f : form) for (auto& s : f) s.reset();
            std::fill (&ap[0][0], &ap[0][0] + 12, 0.0f);
            note = -1;
        }

        bool isActive() const noexcept { return amp.active(); }
        int note = -1; float velocity = 1; juce::uint32 age = 0; bool held = false, sustained = false;
        float livePitch = 60;

        void start (int n, float vel, float fromPitch, float glideSeconds, bool retrigEnv, const Shared& sh, uint32_t seed)
        {
            const auto& p = *sh.p;
            const bool wasActive = isActive();
            note = n; velocity = vel; held = true; sustained = false;
            target = (float) n;
            pitch = (glideSeconds > 0.0005f && fromPitch >= 0.0f) ? fromPitch : (float) n;
            glideRate = glideSeconds > 0.0005f ? std::abs (target - pitch) / (glideSeconds * sr) : 1e9f;
            rng = Rng (seed);
            randomValue = rng.bip();
            if (! wasActive)
            {
                for (int k = 0; k < maxPartials; ++k) { const float ph = (float) (k + 1) * (float) (k + 1) * 0.37f; re[k] = std::cos (ph); im[k] = std::sin (ph); pa[k] = 0; blurV[k] = rng.uni(); blurT[k] = rng.uni(); }
                for (int u = 0; u < maxUnison; ++u) bph[u] = rng.uni();
                subPh = 0; masterPh = 0; driftV = 0; driftT = 0;
                std::fill (&lad[0][0], &lad[0][0] + 8, 0.0f);
                for (auto& s : svf1) s.reset(); for (auto& s : svf2) s.reset();
            }
            for (int i = 0; i < 4; ++i)
            {
                const auto L = P_lfo1Shape + i * 6; // lfoNShape, Rate, Depth, Phase, Sync, Retrig are consecutive
                lfoPh[i] = p.on (L + 5) ? p[L + 3] : sh.lfoGlobal[i];
                lfoHeld[i] = rng.bip(); lfoPrev[i] = rng.bip();
            }
            const bool fromZero = retrigEnv || ! wasActive;
            amp.on (fromZero); flt.on (fromZero); modE.on (fromZero);
            sinceTick = chunk; // run control on the first sample
        }
        void glideTo (int n, float glideSeconds) // legato / mono note change without retrigger
        {
            note = n; target = (float) n; held = true;
            glideRate = glideSeconds > 0.0005f ? std::abs (target - pitch) / (glideSeconds * sr) : 1e9f;
        }
        void release() noexcept { held = false; if (! sustained) { amp.off(); flt.off(); modE.off(); } }
        void kill() noexcept { amp.kill(); flt.kill(); modE.kill(); note = -1; }

        // Adds this voice into out (stereo).
        void render (float* outL, float* outR, int n, const Shared& sh) noexcept
        {
            const auto& p = *sh.p;
            for (int i = 0; i < n; ++i)
            {
                if (++sinceTick >= chunk) { control (sh); sinceTick = 0; }

                // ---- pitch glide happens at control rate; oscillators per sample ----
                float aL = 0, aR = 0, aMono = 0;
                if (aOn)
                {
                    for (int k = 0; k < nPart; ++k)
                    {
                        const float r0 = re[k];
                        re[k] = r0 * pc[k] - im[k] * ps[k];
                        im[k] = r0 * ps[k] + im[k] * pc[k];
                        pa[k] += pStep[k];
                        const float v = im[k] * pa[k];
                        aMono += v;
                        if (k & 1) { aL += v * panNear; aR += v * panFar; } else { aL += v * panFar; aR += v * panNear; }
                    }
                }

                float bL = 0, bR = 0, bMono = 0;
                if (bOn)
                {
                    masterPh += masterInc;
                    const bool syncNow = masterPh >= 1.0f;
                    if (syncNow) masterPh -= 1.0f;
                    for (int u = 0; u < nUni; ++u)
                    {
                        float ph = bph[u] + bInc[u];
                        if (ph >= 1.0f) ph -= 1.0f;
                        if (syncNow && syncOn) ph = masterPh * (bInc[u] / juce::jmax (1e-9f, masterInc));
                        bph[u] = ph;
                        float rp = ph;
                        if (fmIndex > 0.0f) { rp += fmIndex * aMono; rp -= std::floor (rp); }
                        const float v = waveB (rp, bInc[u], u);
                        bL += v * uniL[u]; bR += v * uniR[u]; bMono += v;
                    }
                    bL *= bGain; bR *= bGain; bMono *= bGain;
                }

                // sub, noise, ring
                subPh += subInc; if (subPh >= 1.0f) subPh -= 1.0f;
                float sub = 0;
                switch (subShape) { case 1: sub = 1.0f - 4.0f * std::abs (subPh - 0.5f); break; case 2: sub = (subPh < 0.5f ? 1.0f : -1.0f) + blep (subPh, subInc) - blep (std::fmod (subPh + 0.5f, 1.0f), subInc); break; default: sub = std::sin (twoPi * subPh); }
                sub *= subLvl;
                float nz = 0;
                if (noiseLvl > 0.0f) nz = noiseSample() * noiseLvl;
                const float ring = ringLvl > 0.0f ? aMono * bMono * ringLvl * 2.0f : 0.0f;

                float inAL = aL * aLvl + sub + nz + ring, inAR = aR * aLvl + sub + nz + ring;
                float inBL = bL * bLvl, inBR = bR * bLvl;
                float l, r;
                if (route == 2) // split: A side -> filter 1, B -> filter 2
                {
                    l = filter1 (0, inAL) + filter2 (0, inBL);
                    r = filter1 (1, inAR) + filter2 (1, inBR);
                }
                else if (route == 1) // parallel
                {
                    const float xl = inAL + inBL, xr = inAR + inBR;
                    l = filter1 (0, xl) + (f2Type ? filter2 (0, xl) : 0.0f);
                    r = filter1 (1, xr) + (f2Type ? filter2 (1, xr) : 0.0f);
                }
                else
                {
                    l = filter1 (0, inAL + inBL); r = filter1 (1, inAR + inBR);
                    if (f2Type) { l = filter2 (0, l); r = filter2 (1, r); }
                }

                const float e = amp.next();
                flt.next(); modE.next();
                const float g = e * gain;
                outL[i] += l * g;
                outR[i] += r * g;
                if (! amp.active()) { note = -1; break; }
            }
            age++;
        }

    private:
        // ---------------- control rate ----------------
        void control (const Shared& sh) noexcept
        {
            const auto& p = *sh.p;
            const float dtc = (float) chunk / sr;

            // sources
            float src[numSources] {};
            for (int i = 0; i < 4; ++i)
            {
                const int L = P_lfo1Shape + i * 6;
                float ph = lfoPh[i] + sh.lfoHz[i] * dtc;
                const bool wrap = ph >= 1.0f; ph -= std::floor (ph); lfoPh[i] = ph;
                lfoRaw[i] = lfoShape (p.idx (L), ph, lfoHeld[i], lfoPrev[i], rng, wrap);
                src[sLfo1 + i] = lfoRaw[i] * p[L + 2];
            }
            src[sModEnv] = modE.value; src[sFltEnv] = flt.value; src[sAmpEnv] = amp.value;
            src[sVel] = velocity; src[sNote] = ((float) note - 60.0f) / 48.0f; src[sWheel] = sh.wheel; src[sAfter] = sh.after; src[sRandom] = randomValue;
            for (int m = 0; m < 4; ++m) src[sMac1 + m] = sh.macro[m];

            float mod[numTargets] {};
            for (int s = 0; s < sh.numSlots; ++s) mod[sh.slots[s].dst] += src[sh.slots[s].src] * sh.slots[s].amt;
            auto K = [&] (int t) { return juce::jlimit (0.0f, 1.0f, p[targetParam[t]] + mod[t]); };

            // pitch
            if (pitch != target) { const float step = glideRate * (float) chunk; pitch = std::abs (target - pitch) <= step ? target : pitch + (target > pitch ? step : -step); }
            static const float driftCents[] { 0.0f, 3.0f, 8.0f, 20.0f };
            if (rng.uni() < 0.01f) driftT = rng.bip();
            driftV += (driftT - driftV) * 0.004f;
            const float vib = sh.wheel * p[P_mwAmt] * lfoRaw[0] * 0.7f;
            const float semis = pitch + sh.bend + mod[tPitch] * 12.0f + driftV * driftCents[p.idx (P_drift)] * 0.01f + vib;
            livePitch = semis;
            const float f0 = noteHz (semis);

            // ---- OSC A: star field ----
            aOn = p.on (P_aOn);
            nPart = 8 + (int) std::lround (K (tPartials) * 56.0f);
            const float tilt = (K (tTilt) - 0.5f) * 2.0f, odd = (K (tOdd) - 0.5f) * 2.0f, morph = K (tMorph), inh = K (tSpread) * 0.02f, blur = K (tBlur), shim = K (tShimmer);
            const auto& cons = constellation (p.idx (P_cons));
            const float t = p.on (P_aFreeze) ? frozenTime : (frozenTime = sh.time);
            float sumSq = 0; float tgt[maxPartials];
            for (int k = 0; k < maxPartials; ++k)
            {
                const int h = k + 1;
                const float fk = (float) h * f0 * (1.0f + inh * (float) h);
                float a = 0;
                if (k < nPart && fk < sr * 0.45f)
                {
                    a = std::pow ((float) h, -1.1f + tilt * 0.9f) * ((h & 1) ? 1.0f + odd * 0.8f : 1.0f - odd * 0.8f);
                    a *= std::find (cons.begin(), cons.end(), h) != cons.end() ? 1.0f + morph * 3.0f : 1.0f - morph * 0.5f;
                    a *= 1.0f + 0.35f * std::sin (t * (1.3f + (float) h * 0.17f) + (float) h) * shim;
                    if (! p.on (P_aFreeze)) { if (rng.uni() < 0.02f) blurT[k] = rng.uni(); blurV[k] += (blurT[k] - blurV[k]) * 0.05f; }
                    a *= 1.0f - blur * blurV[k];
                    a = juce::jmax (0.0f, a);
                }
                tgt[k] = a; sumSq += a * a;
                const float w = twoPi * fk / sr;
                pc[k] = std::cos (w); ps[k] = std::sin (w);
                const float mag = re[k] * re[k] + im[k] * im[k]; // keep the phasors on the unit circle
                const float fix = 1.5f - 0.5f * mag; re[k] *= fix; im[k] *= fix;
            }
            const float norm = juce::jmin (3.0f, 0.55f / std::sqrt (sumSq + 1e-9f));
            int last = 0;
            for (int k = 0; k < maxPartials; ++k) { const float a = tgt[k] * norm; pStep[k] = (a - pa[k]) / (float) chunk; if (a > 0.0f || pa[k] > 1e-6f) last = k + 1; }
            nPart = last;
            const float width = K (tWidth);
            panNear = 1.0f; panFar = 1.0f - width * 0.5f;
            aLvl = K (tALvl) * 1.1f;

            // ---- OSC B: nebula ----
            bOn = p.on (P_bOn);
            bWave = p.idx (P_bWave);
            nUni = 1 + (int) std::lround (p[P_bUni] * 15.0f);
            if (bWave == 4) nUni = juce::jmax (nUni, 7);
            const float bSemis = semis + (float) (p.list (P_bOct, 7) - 3) * 12.0f + std::round (p.lin (P_bSemi, -12, 12)) + p.lin (P_bFine, -50, 50) * 0.01f;
            const float fb = noteHz (bSemis);
            const float det = K (tBDet);
            const float syncAmt = p[P_bSync];
            syncOn = syncAmt > 0.001f;
            const float ratio = syncOn ? 1.0f + syncAmt * 7.0f : 1.0f;
            masterInc = fb / sr;
            for (int u = 0; u < nUni; ++u)
            {
                const float spread = nUni > 1 ? ((float) u / (float) (nUni - 1)) * 2.0f - 1.0f : 0.0f;
                const float cents = spread * det * det * 60.0f + (bWave == 4 ? spread * 12.0f * det : 0.0f);
                bInc[u] = juce::jmin (0.45f, fb * ratio * std::exp2 (cents / 1200.0f) / sr);
                const float pan = spread * width;
                uniL[u] = std::sqrt (0.5f * (1.0f - pan)); uniR[u] = std::sqrt (0.5f * (1.0f + pan));
            }
            bGain = 1.4f / std::sqrt ((float) nUni);
            bShape = K (tBPw);
            fmIndex = K (tBFm) * 1.5f;
            bLvl = K (tBLvl);
            pwmPh += 0.7f * dtc; pwmPh -= std::floor (pwmPh);

            // ---- moons ----
            subInc = f0 / (p.list (P_subOct, 2) == 0 ? 2.0f : 4.0f) / sr;
            subShape = p.list (P_subShape, 3);
            subLvl = K (tSub);
            noiseLvl = K (tNoise) * 0.6f; noiseType = p.list (P_nType, 5);
            const float col = p.bi (P_nColor);
            noiseTilt.setHz (col < 0 ? expo (col + 1.0f, 300.0f, 20000.0f) : expo (col, 20.0f, 4000.0f), sr);
            noiseHp = col > 0;
            windPh += 0.13f * dtc; windPh -= std::floor (windPh);
            for (auto& s : windSvf) s.set (500.0f + 900.0f * (0.5f + 0.5f * std::sin (twoPi * windPh)), 3.0f, sr);
            ringLvl = K (tRing);

            // ---- filters ----
            f1Type = p.idx (P_f1Type); f2Type = p.idx (P_f2Type); route = p.idx (P_fRoute);
            const float envAmt = (p[P_fEnv] - 0.5f) * 2.0f;
            const float fltEnvV = flt.value * (1.0f + ((velocity - 1.0f) * p[P_fltVel]));
            const float cutSemis = envAmt * fltEnvV * 72.0f + p[P_fKey] * ((float) note - 60.0f) + sh.after * p[P_atAmt] * 48.0f;
            const float cutoff = juce::jlimit (20.0f, sr * 0.45f, expo (K (tCut), 20.0f, 20000.0f) * std::exp2 (cutSemis / 12.0f));
            const float res = K (tRes);
            const float drv = K (tDrive);
            preGain = 1.0f + drv * drv * 14.0f; postGain = 1.0f / (1.0f + drv * 1.6f);
            {
                const float tt = std::tan (pi * juce::jmin (cutoff, sr * 0.42f) / sr);
                ladG = tt / (1.0f + tt);
                ladK = res * (f1Type == 2 ? 5.0f : 4.1f);
            }
            if (f1Type >= 3 && f1Type <= 6)
                for (auto& s : svf1) s.set (cutoff, 0.5f + res * res * 18.0f, sr);
            combDelay = juce::jlimit (2.0f, 4000.0f, sr / cutoff);
            combFb = (0.3f + res * 0.67f) * (f1Type == 8 ? -1.0f : 1.0f);
            if (f1Type == 9 || f1Type == 10) setFormants (cutoff, res);
            if (f1Type == 11) { const float tt = std::tan (pi * juce::jlimit (40.0f, sr * 0.4f, cutoff) / sr); apC = (tt - 1.0f) / (tt + 1.0f); apFb = res * 0.85f; }
            if (f2Type) for (auto& s : svf2) s.set (juce::jlimit (20.0f, sr * 0.45f, expo (K (tF2Cut), 20.0f, 20000.0f)), 0.8f, sr);

            // ---- envelopes ----
            auto setEnv = [&] (Env& e, int base) // base = P_xxxLoop: Loop, Curve, A, H, D, S, R, Vel
            { e.set (sr, p.pw (base + 2, 0, 10), p.pw (base + 3, 0, 2), p.pw (base + 4, 0, 10), p[base + 5], p.pw (base + 6, 0, 10), p.idx (base + 1), p.on (base)); };
            setEnv (amp, P_ampLoop); setEnv (flt, P_fltLoop); setEnv (modE, P_modLoop);

            const float velAmp = (1.0f + (velocity - 1.0f) * p[P_velAmt]) * (1.0f + (velocity - 1.0f) * p[P_ampVel]);
            gain = 0.11f * velAmp * juce::jlimit (0.0f, 2.0f, 1.0f + mod[tVolume]);
        }

        // ---------------- oscillator B shapes ----------------
        float waveB (float ph, float dt, int u) noexcept
        {
            switch (bWave)
            {
                case 1: { const float w = 0.5f + (bShape - 0.5f) * 0.8f; return pulse (ph, dt, w); }
                case 2: { const float pk = 0.05f + bShape * 0.9f; return ph < pk ? -1.0f + 2.0f * ph / pk : 1.0f - 2.0f * (ph - pk) / (1.0f - pk); }
                case 3: { const float s = std::sin (twoPi * ph); return bShape > 0.01f ? std::sin (s * (1.0f + bShape * 4.0f) * pi * 0.5f) : s; }
                case 5: { const float w = 0.05f + 0.9f * juce::jlimit (0.0f, 1.0f, bShape + 0.15f * std::sin (twoPi * (pwmPh + (float) u * 0.13f))); return pulse (ph, dt, w); }
                case 6: return std::sin (twoPi * ph) * (1.0f - bShape * 0.5f) + std::sin (3.0f * twoPi * ph) * bShape * 0.5f + std::sin (5.0f * twoPi * ph) * bShape * 0.2f;
                case 4: { const float saw = 2.0f * ph - 1.0f - blep (ph, dt); return u == nUni / 2 ? saw : saw * (0.35f + bShape * 1.3f); } // SUPER: shape = side voices
                default:
                {
                    float w = ph;
                    if (std::abs (bShape - 0.5f) > 0.02f) { const float k = 0.05f + 0.9f * (1.0f - bShape); w = ph < k ? ph * 0.5f / k : 0.5f + (ph - k) * 0.5f / (1.0f - k); }
                    return 2.0f * w - 1.0f - blep (ph, dt);
                }
            }
        }
        static float pulse (float ph, float dt, float w) noexcept
        {
            float v = ph < w ? 1.0f : -1.0f;
            v += blep (ph, dt);
            float t = ph - w; if (t < 0.0f) t += 1.0f;
            v -= blep (t, dt);
            return v;
        }

        float noiseSample() noexcept
        {
            const float w = rng.bip();
            float x;
            switch (noiseType)
            {
                case 1: { pink[0] = 0.99886f * pink[0] + w * 0.0555179f; pink[1] = 0.99332f * pink[1] + w * 0.0750759f; pink[2] = 0.96900f * pink[2] + w * 0.1538520f;
                          pink[3] = 0.86650f * pink[3] + w * 0.3104856f; pink[4] = 0.55000f * pink[4] + w * 0.5329522f; x = (pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + w * 0.5362f) * 0.25f; break; }
                case 2: x = rng.uni() < 0.002f ? rng.bip() * 3.0f : 0.0f; break;                                  // dust
                case 3: x = (rng.uni() < 0.0008f ? rng.bip() * 4.0f : 0.0f) + windSvf[1].lp (w) * 0.3f; break;   // vinyl crackle + rumble
                case 4: x = windSvf[0].bpf (w) * 2.0f; break;                                                     // wind
                default: x = w;
            }
            return noiseHp ? noiseTilt.hp (x) : noiseTilt.lp (x);
        }

        // ---------------- filters ----------------
        float filter1 (int ch, float x) noexcept
        {
            x = fastTanh (x * preGain) * postGain;
            switch (f1Type)
            {
                case 0: case 1: case 2: return ladder (ch, x);
                case 3: return svf1[ch].lp (x);
                case 4: return svf1[ch].bpf (x);
                case 5: return svf1[ch].hpf (x);
                case 6: { float b, h; const float l = svf1[ch].run (x, b, h); return l + h; }
                case 7: case 8:
                {
                    auto& buf = comb[ch];
                    float rp = (float) cw - combDelay; if (rp < 0) rp += 4096.0f;
                    const int i0 = (int) rp; const float fr = rp - (float) i0;
                    const float d = buf[(size_t) i0] + fr * (buf[(size_t) ((i0 + 1) & 4095)] - buf[(size_t) i0]);
                    const float y = x + combFb * d;
                    buf[(size_t) cw] = fastTanh (y);
                    if (ch == 1) cw = (cw + 1) & 4095;
                    return y * (1.0f - std::abs (combFb) * 0.55f);
                }
                case 9: case 10: return (form[ch][0].bpf (x) + form[ch][1].bpf (x) * 0.7f + form[ch][2].bpf (x) * 0.45f) * 1.6f;
                case 11:
                {
                    float y = x + apLast[ch] * apFb;
                    for (int s = 0; s < 6; ++s) { const float o = apC * y + ap[ch][s]; ap[ch][s] = y - apC * o; y = o; }
                    apLast[ch] = y;
                    return (x + y) * 0.5f;
                }
                default: return x;
            }
        }
        float filter2 (int ch, float x) noexcept
        {
            switch (f2Type) { case 1: return svf2[ch].hpf (x); case 2: return svf2[ch].bpf (x); case 3: return svf2[ch].lp (x); default: return x; }
        }
        float ladder (int ch, float x) noexcept
        {
            auto* s = lad[ch];
            const int poles = f1Type == 0 ? 4 : f1Type == 2 ? 3 : 2;
            float u = fastTanh (x * (1.0f + ladK * 0.45f) - ladK * s[poles - 1]);
            for (int i = 0; i < poles; ++i) { s[i] += ladG * (u - fastTanh (s[i])); u = s[i]; }
            return s[poles - 1];
        }
        void setFormants (float cutoff, float res)
        {
            static const float vowels[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            float f[3];
            if (f1Type == 10) // VOWEL: the cutoff walks A > E > I > O > U
            {
                const float pos = juce::jlimit (0.0f, 3.999f, std::log (cutoff / 20.0f) / std::log (1000.0f) * 4.0f);
                const int i = (int) pos; const float fr = pos - (float) i;
                for (int k = 0; k < 3; ++k) f[k] = vowels[i][k] + fr * (vowels[i + 1][k] - vowels[i][k]);
            }
            else for (int k = 0; k < 3; ++k) f[k] = vowels[0][k] * juce::jlimit (0.25f, 4.0f, cutoff / 1000.0f); // FORMANT: shift the A formants
            for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < 3; ++k) form[ch][k].set (juce::jmin (f[k], sr * 0.45f), 4.0f + res * 16.0f, sr);
        }

        float sr = 48000;
        Env amp, flt, modE;
        Rng rng;
        float randomValue = 0, pitch = 60, target = 60, glideRate = 1e9f;
        int sinceTick = chunk;
        // A
        bool aOn = true; int nPart = 0;
        float re[maxPartials] {}, im[maxPartials] {}, pc[maxPartials] {}, ps[maxPartials] {}, pa[maxPartials] {}, pStep[maxPartials] {}, blurV[maxPartials] {}, blurT[maxPartials] {};
        float panNear = 1, panFar = 1, aLvl = 1, frozenTime = 0;
        // B
        bool bOn = true, syncOn = false; int bWave = 0, nUni = 1;
        float bph[maxUnison] {}, bInc[maxUnison] {}, uniL[maxUnison] {}, uniR[maxUnison] {};
        float bGain = 1, bShape = 0.5f, fmIndex = 0, bLvl = 0, masterPh = 0, masterInc = 0, pwmPh = 0;
        // moons
        float subPh = 0, subInc = 0, subLvl = 0, noiseLvl = 0, ringLvl = 0, pink[5] {}, windPh = 0;
        int subShape = 0, noiseType = 0; bool noiseHp = false; OnePole noiseTilt; Svf windSvf[2];
        float driftV = 0, driftT = 0;
        // lfo
        float lfoPh[4] {}, lfoHeld[4] {}, lfoPrev[4] {}, lfoRaw[4] {};
        // filters
        int f1Type = 0, f2Type = 0, route = 0;
        float preGain = 1, postGain = 1, ladG = 0.5f, ladK = 0, lad[2][4] {};
        Svf svf1[2], svf2[2], form[2][3];
        std::vector<float> comb[2]; int cw = 0; float combDelay = 100, combFb = 0;
        float ap[2][6] {}, apLast[2] {}, apC = 0, apFb = 0;
        float gain = 0.3f;
    };

    //==============================================================================
    // Voice manager: poly / mono / legato, glide, sustain pedal, stealing.
    class Synth
    {
    public:
        void prepare (float sampleRate)
        {
            sr = sampleRate; for (auto& v : voices) v.prepare (sampleRate); heldKeys.clear(); heldKeys.ensureStorageAllocated (128);
            wheelBend = 0; wheelMod = 0; after = 0; sustain = false; slideNext = false; lastNote = -1; seed = 1; // a restart forgets bend, pedal and slides; same random sequence -> repeatable bounces
        }
        void allOff() { for (auto& v : voices) v.kill(); heldKeys.clearQuick(); }

        void handle (const juce::MidiMessage& m, Shared& sh)
        {
            const auto& p = *sh.p;
            if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity(), sh);
            else if (m.isNoteOff()) noteOff (m.getNoteNumber(), sh);
            else if (m.isPitchWheel()) wheelBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            else if (m.isControllerOfType (1)) sh.wheel = wheelMod = (float) m.getControllerValue() / 127.0f;
            else if (m.isControllerOfType (64)) { sustain = m.getControllerValue() >= 64; if (! sustain) for (auto& v : voices) if (v.sustained) { v.sustained = false; if (! v.held) v.release(); } }
            else if (m.isControllerOfType (65)) slideNext = m.getControllerValue() >= 64; // the arp's SLIDE steps
            else if (m.isChannelPressure()) after = (float) m.getChannelPressureValue() / 127.0f;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
            juce::ignoreUnused (p);
        }

        void render (float* l, float* r, int n, Shared& sh)
        {
            sh.bend = wheelBend * std::round (sh.p->lin (P_bend, 0, 24));
            sh.wheel = wheelMod; sh.after = after;
            for (auto& v : voices) if (v.isActive()) v.render (l, r, n, sh);
        }
        int activeCount() const { int c = 0; for (auto& v : voices) c += v.isActive() ? 1 : 0; return c; }
        float lastPitch() const { const Voice* nv = newest(); return nv ? nv->livePitch : -1.0f; }
        float wheelBend = 0, wheelMod = 0, after = 0;

    private:
        const Voice* newest() const { const Voice* b = nullptr; for (auto& v : voices) if (v.isActive() && (! b || v.age < b->age)) b = &v; return b; }

        float glideFor (const Shared& sh) const
        {
            if (slideNext) return sh.p->lin (P_aGlide, 0, 0.5f);
            return sh.p->pw (P_glide, 0, 2);
        }

        void noteOn (int note, float vel, Shared& sh)
        {
            const auto& p = *sh.p;
            const bool mono = p.on (P_mono) || slideNext;
            heldKeys.removeFirstMatchingValue (note);
            heldKeys.add (note);
            const float glide = glideFor (sh);
            if (mono)
            {
                Voice& v = voices[0];
                for (int i = 1; i < maxVoices; ++i) if (voices[i].isActive() && voices[i].note >= 0 && ! p.on (P_mono)) voices[i].release();
                const bool legato = v.isActive() && v.held && (p.on (P_legato) || slideNext);
                if (legato) v.glideTo (note, glide);
                else v.start (note, vel, v.isActive() ? v.livePitch : lastNote, glide, p.on (P_retrig), sh, seed++);
                v.age = 0;
            }
            else
            {
                const int limit = (int) std::lround (std::pow (2.0f, 2.0f + (float) p.idx (P_voices))); // 4, 8, 16, 32
                Voice* free = nullptr; Voice* oldest = nullptr; int active = 0;
                for (int i = 0; i < maxVoices; ++i)
                {
                    auto& v = voices[i];
                    if (v.isActive()) { ++active; if (v.note == note) { free = &v; break; } if (! oldest || v.age > oldest->age) oldest = &v; }
                }
                if (! free) for (int i = 0; i < limit; ++i) if (! voices[i].isActive()) { free = &voices[i]; break; }
                if (! free || active >= limit) free = free ? free : oldest;
                if (! free) free = &voices[0];
                free->start (note, vel, lastNote, glide, p.on (P_retrig), sh, seed++);
                free->age = 0;
            }
            lastNote = (float) note;
            slideNext = false;
        }

        void noteOff (int note, Shared& sh)
        {
            const auto& p = *sh.p;
            heldKeys.removeFirstMatchingValue (note);
            if (p.on (P_mono))
            {
                Voice& v = voices[0];
                if (v.note == note)
                {
                    if (! heldKeys.isEmpty()) v.glideTo (heldKeys.getLast(), p.pw (P_glide, 0, 2)); // fall back to the last key still held
                    else { if (sustain) v.sustained = true; v.release(); }
                }
                return;
            }
            for (auto& v : voices)
                if (v.isActive() && v.note == note && v.held)
                {
                    if (sustain) v.sustained = true;
                    v.release();
                }
        }

        float sr = 48000;
        Voice voices[maxVoices];
        juce::Array<int> heldKeys;
        float lastNote = -1;
        bool sustain = false, slideNext = false;
        uint32_t seed = 1;
    };
}
