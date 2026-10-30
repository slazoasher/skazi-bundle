#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

// SKVERB reverb engine. Everything here runs on the audio thread and never allocates after prepare().
//
//   in -> PRE-DELAY (+ feedback echoes) -> DRIVE -> EARLY REFLECTIONS ----------------------+
//                                              \-> DIFFUSION -> [SPRING TANK] -> 8-LINE FDN (allpass in each line) -+-> REVERSE / GATE -> WIDTH -> LOW/HIGH CUT
//                                                                     ^-- SHIMMER pitch loop -+
//   ... -> REVERSE LAYER (reversed copy blended in) -> WIDTH -> TAIL FILTER (LP/BP/HP, envelope-swept) -> LOW/HIGH CUT
namespace verb
{
    constexpr float pi = 3.14159265358979f;
    constexpr int numLines = 8;

    enum Mode { hall, plate, room, chamber, cathedral, spring, shimmer, reverse, gated, blackhole, numModes };

    // What each algorithm does with the shared engine.
    struct ModeSpec
    {
        const char* name;
        float scale;      // FDN delay-length multiplier
        float diffusion;  // input-diffusion multiplier
        float early;      // early-reflection level
        int erPattern;    // 0 small room, 1 hall, 2 huge space
        float mod;        // modulation-depth multiplier
        float decayMul;   // RT60 multiplier
        float bright;     // damping-cutoff multiplier
    };

    inline const ModeSpec& spec (int mode)
    {
        static const ModeSpec specs[numModes] {
            { "HALL",      1.00f, 1.0f, 0.60f, 1, 1.0f, 1.0f, 1.0f },
            { "PLATE",     0.50f, 1.2f, 0.00f, 0, 1.3f, 1.0f, 1.5f },
            { "ROOM",      0.38f, 0.9f, 1.00f, 0, 0.6f, 1.0f, 0.9f },
            { "CHAMBER",   0.65f, 1.0f, 0.80f, 0, 0.8f, 1.0f, 1.0f },
            { "CATHEDRAL", 1.70f, 1.0f, 0.50f, 2, 1.0f, 1.4f, 0.8f },
            { "SPRING",    0.30f, 0.6f, 0.00f, 0, 0.5f, 1.0f, 0.7f },
            { "SHIMMER",   1.20f, 1.0f, 0.30f, 1, 1.2f, 1.3f, 1.1f },
            { "REVERSE",   0.90f, 1.0f, 0.30f, 1, 0.8f, 1.0f, 1.0f },
            { "GATED",     0.55f, 1.2f, 0.70f, 0, 0.3f, 1.0f, 1.2f },
            { "BLACKHOLE", 2.40f, 1.2f, 0.00f, 2, 3.0f, 3.0f, 0.6f },
        };
        return specs[juce::jlimit (0, numModes - 1, mode)];
    }

    // Size knob 0..1 -> delay-length factor.
    inline float sizeFactor (float size) { return 0.35f * std::pow (1.6f / 0.35f, juce::jlimit (0.0f, 1.0f, size)); }

    // Soft limiter that is linear for normal levels; it only keeps runaway feedback (freeze + shimmer) bounded.
    inline float softLimit (float x)
    {
        const float y = juce::jlimit (-3.0f, 3.0f, x * 0.25f);
        return 4.0f * y * (27.0f + y * y) / (27.0f + 9.0f * y * y);
    }

    //==============================================================================
    struct DelayLine
    {
        void prepare (int maxSamples)
        {
            int n = 1;
            while (n < maxSamples + 8)
                n <<= 1;
            buffer.assign ((size_t) n, 0.0f);
            mask = n - 1;
            pos = 0;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        // Delay in samples, >= 1: the sample written that many pushes ago.
        float read (int d) const noexcept { return buffer[(size_t) ((pos - d) & mask)]; }

        // Fractional read with Hermite interpolation; d >= 2.
        float readFrac (float d) const noexcept
        {
            d = juce::jmax (2.0f, d);
            const int i = (int) d;
            const float t = d - (float) i;
            const float x0 = read (i - 1), x1 = read (i), x2 = read (i + 1), x3 = read (i + 2);
            const float c1 = 0.5f * (x2 - x0);
            const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
            const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            return ((c3 * t + c2) * t + c1) * t + x1;
        }

        void push (float v) noexcept
        {
            buffer[(size_t) pos] = v;
            pos = (pos + 1) & mask;
        }

        int capacity() const noexcept { return mask - 4; }

        std::vector<float> buffer;
        int mask = 0, pos = 0;
    };

    // Schroeder allpass for input diffusion.
    struct Allpass
    {
        void prepare (int maxSamples) { line.prepare (maxSamples); }
        void clear() { line.clear(); }

        float process (float x, int delay, float g) noexcept
        {
            const float z = line.read (delay);
            const float w = x - g * z;
            line.push (w);
            return z + g * w;
        }

        // Same, with a fractional delay (>= 2) that can follow the SIZE knob smoothly.
        float processFrac (float x, float delay, float g) noexcept
        {
            const float z = line.readFrac (delay);
            const float w = x - g * z;
            line.push (w);
            return z + g * w;
        }

        DelayLine line;
    };

    struct OnePole
    {
        float process (float x, float a) noexcept { return state += a * (x - state); }
        float state = 0.0f;
    };

    inline float onePoleCoeff (float freq, double sampleRate)
    {
        return 1.0f - std::exp (-2.0f * pi * freq / (float) sampleRate);
    }

    // 12 dB/oct TPT state-variable filter.
    struct SVF
    {
        void set (float freq, double sampleRate, float q = 0.7071f)
        {
            const float g = std::tan (pi * juce::jlimit (10.0f, 0.45f * (float) sampleRate, freq) / (float) sampleRate);
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void clear() { ic1 = ic2 = 0.0f; }

        // Low, band and high outputs at once.
        void processAll (float x, float& lp, float& bp, float& hp) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2;
            bp = v1;
            hp = x - k * v1 - v2;
        }

        // Returns low; high goes to hp.
        float process (float x, float& hp) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            hp = x - k * v1 - v2;
            return v2;
        }

        float k = 1.414f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
    };

    //==============================================================================
    // Two-grain delay-line pitch shifter (feeds the shimmer loop).
    struct PitchShifter
    {
        void prepare (double sr)
        {
            sampleRate = sr;
            grain = (float) (0.09 * sr);
            line.prepare ((int) grain + 8);
            phase = 0.0f;
        }

        void clear() { line.clear(); }

        float process (float x, float ratio) noexcept
        {
            line.push (x);
            phase += (1.0f - ratio) / grain;
            phase -= std::floor (phase);
            float out = 0.0f;
            for (int h = 0; h < 2; ++h)
            {
                float p = phase + 0.5f * (float) h;
                p -= std::floor (p);
                const float w = std::sin (pi * p);
                out += w * w * line.readFrac (2.0f + p * grain);
            }
            return out;
        }

        DelayLine line;
        double sampleRate = 44100.0;
        float grain = 4000.0f, phase = 0.0f;
    };

    // Plays the incoming signal back in reversed, crossfaded windows.
    struct Reverser
    {
        void prepare (double sr, float maxWindowSeconds)
        {
            line.prepare ((int) (2.0 * maxWindowSeconds * sr) + 8);
            maxWindow = (int) (maxWindowSeconds * sr);
            reset (maxWindow / 2);
        }

        void reset (int window)
        {
            line.clear();
            for (int h = 0; h < 2; ++h)
            {
                heads[h].window = juce::jmax (64, window);
                heads[h].pos = h * heads[h].window / 2;
            }
        }

        float process (float x, int targetWindow) noexcept
        {
            line.push (x);
            float out = 0.0f;
            for (auto& h : heads)
            {
                const float s = std::sin (pi * (float) h.pos / (float) h.window);
                out += s * s * line.read (2 * h.pos + 1);
                if (++h.pos >= h.window)
                {
                    h.pos = 0;
                    h.window = juce::jlimit (64, maxWindow, targetWindow);
                }
            }
            return out;
        }

        struct Head { int pos = 0, window = 1000; };
        DelayLine line;
        std::array<Head, 2> heads;
        int maxWindow = 1000;
    };

    // Dispersive spring tank: a chain of first-order allpasses inside a feedback delay gives the chirp.
    struct SpringTank
    {
        static constexpr int stages = 24;

        void prepare (double sr, float delayMs)
        {
            sampleRate = sr;
            delay = juce::jmax (4, (int) (delayMs * 0.001 * sr));
            line.prepare (delay + 4);
            clear();
        }

        void clear()
        {
            line.clear();
            states.fill (0.0f);
            tone.state = 0.0f;
        }

        float process (float x, float feedback, float toneCoeff) noexcept
        {
            float v = x + feedback * line.read (delay);
            const float a = 0.62f;
            for (auto& s : states)
            {
                const float y = -a * v + s;
                s = v + a * y;
                v = y;
            }
            v = tone.process (v, toneCoeff);
            line.push (softLimit (v));
            return v;
        }

        DelayLine line;
        std::array<float, stages> states {};
        OnePole tone;
        double sampleRate = 44100.0;
        int delay = 1000;
    };

    //==============================================================================
    struct Settings
    {
        int mode = hall;
        float decay = 2.5f;        // seconds (gate time in GATED)
        float preDelayMs = 20.0f;
        float feedback = 0.0f;     // pre-delay echo feedback 0..0.95
        float size = 0.6f;         // 0..1
        float lowCut = 80.0f, highCut = 12000.0f;
        float damping = 0.4f;      // 0..1
        float bassMult = 1.0f;     // low-frequency decay multiplier
        float diffusion = 0.8f;    // 0..1
        float early = 0.5f;        // 0..1
        float width = 1.0f;        // 0..2
        float modRate = 0.6f, modDepth = 0.3f;
        float shimmerPitch = 12.0f, shimmerAmount = 0.0f;
        float drive = 0.0f;        // 0..1
        bool freeze = false;
        float reverseAmount = 0.0f;   // 0..1 reversed copy of the tail blended into the wet
        float reverseTime = 0.8f;     // seconds per reversed window
        int filterType = 0;           // 0 off, 1 low pass, 2 band pass, 3 high pass
        float filterCutoff = 2000.0f;
        float filterReso = 0.2f;      // 0..1
        float filterEnv = 0.0f;       // -1..1: cutoff follows the tail level (+ closes as it fades, - opens)
    };

    class Engine
    {
    public:
        void prepare (double sr)
        {
            sampleRate = sr;
            const int maxLine = (int) (0.42 * sr);
            for (auto& l : lines)
                l.prepare (maxLine);
            for (auto& a : loopDiffusers)
                a.prepare ((int) (0.06 * sr));
            for (auto& c : diffusers)
                for (auto& a : c)
                    a.prepare ((int) (0.05 * sr));
            for (auto& e : erLines)
                e.prepare ((int) (0.4 * sr));
            for (auto& p : preDelay)
                p.prepare ((int) (2.1 * sr));
            springs[0].prepare (sr, 38.0f);
            springs[1].prepare (sr, 44.5f);
            shifter.prepare (sr);
            for (auto& r : reversers)
                r.prepare (sr, 2.5f);
            for (auto& r : reverseLayer)
                r.prepare (sr, 2.5f);
            for (auto& f : tailFilter)
                f.clear();
            tailEnv = 0.0f;
            filterCountdown = 0;
            for (auto* s : { &reverseSmooth, &filterOff, &filterLp, &filterBp, &filterHp })
                s->reset (sr, 0.03);
            filterOff.setCurrentAndTargetValue (1.0f);
            for (auto* s : { &filterLp, &filterBp, &filterHp })
                s->setCurrentAndTargetValue (0.0f);

            for (auto* s : { &sizeSmooth, &earlySmooth, &widthSmooth, &driveSmooth, &shimmerSmooth, &feedbackSmooth, &fadeSmooth })
                s->reset (sr, 0.05);
            sizeSmooth.reset (sr, 0.25);
            preDelaySmooth.reset (sr, 0.12);
            fadeSmooth.reset (sr, 0.015);
            fadeSmooth.setCurrentAndTargetValue (1.0f);
            activeMode = -1;
            gateGain = 0.0f;
            gateHold = 0;
        }

        // Fills wetL/wetR (wet only) from the dry input.
        void process (const float* inL, const float* inR, float* wetL, float* wetR, int n, const Settings& s)
        {
            if (activeMode < 0)
                switchMode (s.mode, s.size);
            else if (s.mode != activeMode && fadeSmooth.getTargetValue() > 0.5f)
                fadeSmooth.setTargetValue (0.0f);

            const auto& m = spec (activeMode);
            sizeSmooth.setTargetValue (sizeFactor (s.size) * m.scale);
            preDelaySmooth.setTargetValue (juce::jlimit (0.0f, 2000.0f, s.preDelayMs) * 0.001f * (float) sampleRate);
            earlySmooth.setTargetValue (s.early * m.early);
            widthSmooth.setTargetValue (s.width);
            driveSmooth.setTargetValue (s.drive);
            shimmerSmooth.setTargetValue (s.freeze ? 0.0f : juce::jlimit (0.0f, 1.0f, activeMode == shimmer ? 0.35f + 0.65f * s.shimmerAmount : s.shimmerAmount));
            feedbackSmooth.setTargetValue (juce::jlimit (0.0f, 0.95f, s.feedback) * juce::jlimit (0.0f, 1.0f, s.preDelayMs / 5.0f));

            // Block-rate coefficients.
            const float rt = activeMode == gated ? 2.2f : juce::jlimit (0.1f, 60.0f, s.decay * m.decayMul);
            const float dampFreq = juce::jlimit (500.0f, 0.45f * (float) sampleRate, 20000.0f * std::pow (800.0f / 20000.0f, s.damping) * m.bright);
            const float dampA = s.freeze ? 1.0f : onePoleCoeff (dampFreq, sampleRate);
            const float crossA = onePoleCoeff (300.0f, sampleRate);
            const float diffG = juce::jlimit (0.0f, 0.78f, s.diffusion * m.diffusion * 0.7f);
            // Depth is a delay swing, but capped so the pitch wobble (2*pi*rate*swing) stays musical at fast rates.
            const float modRate = juce::jlimit (0.01f, 10.0f, s.modRate);
            const float maxSwing = 0.012f * m.mod * (float) sampleRate / (2.0f * pi * modRate * 1.3f);
            const float modSamples = juce::jmin (s.modDepth * m.mod * 0.003f * (float) sampleRate, s.modDepth * maxSwing);
            const float lfoInc = 2.0f * pi * modRate / (float) sampleRate;
            const float loopDiffG = juce::jlimit (0.0f, 0.62f, 0.2f + 0.45f * s.diffusion * m.diffusion);
            const float ratio = std::pow (2.0f, s.shimmerPitch / 12.0f);
            const float shimHpA = onePoleCoeff (400.0f, sampleRate);
            const float springTone = onePoleCoeff (4200.0f, sampleRate);
            const int reverseWindow = (int) (juce::jlimit (0.15f, 2.5f, s.decay * 0.5f) * sampleRate);
            const int layerWindow = (int) (juce::jlimit (0.1f, 2.5f, s.reverseTime) * sampleRate);
            reverseSmooth.setTargetValue (activeMode == reverse ? 0.0f : juce::jlimit (0.0f, 1.0f, s.reverseAmount));

            // Tail filter: the type crossfades, the cutoff is recomputed every few samples while the envelope sweeps it.
            filterOff.setTargetValue (s.filterType == 0 ? 1.0f : 0.0f);
            filterLp.setTargetValue (s.filterType == 1 ? 1.0f : 0.0f);
            filterBp.setTargetValue (s.filterType == 2 ? 1.0f : 0.0f);
            filterHp.setTargetValue (s.filterType == 3 ? 1.0f : 0.0f);
            const float reso = juce::jlimit (0.0f, 1.0f, s.filterReso);
            const float filterQ = 0.6f + 9.0f * reso * reso;
            const float resoComp = 1.0f - 0.4f * reso;
            const float envAmount = juce::jlimit (-1.0f, 1.0f, s.filterEnv);
            const float envAttack = onePoleCoeff (1.0f / 0.005f, sampleRate), envRelease = onePoleCoeff (1.0f / 0.25f, sampleRate);
            const int gateSamples = (int) (juce::jlimit (0.05f, 1.5f, s.decay) * sampleRate);
            const float gateAttack = onePoleCoeff (1.0f / 0.001f, sampleRate), gateRelease = onePoleCoeff (1.0f / 0.02f, sampleRate);
            lowCut.set (s.lowCut, sampleRate);
            lowCutR.set (s.lowCut, sampleRate);
            highCut.set (s.highCut, sampleRate);
            highCutR.set (s.highCut, sampleRate);

            const float currentSize = sizeSmooth.getCurrentValue();
            std::array<float, numLines> gHigh, gLow;
            for (int i = 0; i < numLines; ++i)
            {
                const float seconds = (baseMs[(size_t) i] + loopDiffMs[(size_t) i]) * 0.001f * currentSize;
                gHigh[(size_t) i] = s.freeze ? 1.0f : std::pow (10.0f, -3.0f * seconds / rt);
                gLow[(size_t) i]  = s.freeze ? 1.0f : std::pow (10.0f, -3.0f * seconds / (rt * juce::jlimit (0.25f, 4.0f, s.bassMult)));
            }
            const float springFb = s.freeze ? 0.999f : std::pow (10.0f, -3.0f * 0.05f / juce::jmax (0.1f, s.decay));
            const float inputGain = s.freeze ? 0.0f : 1.0f;

            for (int i = 0; i < n; ++i)
            {
                const float fade = fadeSmooth.getNextValue();
                if (fade <= 0.0f && fadeSmooth.getTargetValue() <= 0.0f)
                {
                    switchMode (s.mode, s.size);
                    fadeSmooth.setTargetValue (1.0f);
                }

                // Pre-delay with echo feedback (darkened a little on every repeat).
                const float pd = juce::jmax (1.0f, preDelaySmooth.getNextValue());
                const float fb = feedbackSmooth.getNextValue();
                float x[2] { inL[i], inR[i] };
                for (int c = 0; c < 2; ++c)
                {
                    const float delayed = pd < 2.0f ? preDelay[c].read (1) : preDelay[c].readFrac (pd);
                    preDelay[c].push (x[c] + fb * echoTone[c].process (delayed, 0.55f));
                    x[c] = delayed;
                }

                // Drive: soft saturation into the tank with level compensation.
                const float drive = driveSmooth.getNextValue();
                if (drive > 0.001f)
                {
                    const float pre = 1.0f + 9.0f * drive;
                    for (auto& v : x)
                        v = std::tanh (v * pre) / std::sqrt (pre);
                }

                // Early reflections.
                float er[2] {};
                for (int c = 0; c < 2; ++c)
                {
                    erLines[c].push (x[c]);
                    for (int t = 0; t < erTaps; ++t)
                        er[c] += erLines[c].read (erDelay[c][(size_t) t]) * erGain[c][(size_t) t];
                }

                // Input diffusion, then the spring tank in SPRING mode.
                float d[2] { x[0] * inputGain, x[1] * inputGain };
                for (int c = 0; c < 2; ++c)
                    for (int a = 0; a < numDiffusers; ++a)
                        d[c] = diffusers[c][(size_t) a].process (d[c], diffuserDelay[c][(size_t) a], diffG);

                float springOut[2] {};
                if (activeMode == spring)
                    for (int c = 0; c < 2; ++c)
                    {
                        springOut[c] = springs[c].process (d[c] * 0.6f, springFb, springTone);
                        d[c] = d[c] * 0.4f + springOut[c] * 0.5f;
                    }

                // Shimmer: pitch-shifted tail fed back into the tank.
                const float shim = shimmerSmooth.getNextValue();
                float shimIn = 0.0f;
                if (shim > 0.001f)
                {
                    float y = shifter.process (lastLate, ratio);
                    y -= shimHp.process (y, shimHpA);
                    shimIn = softLimit (y) * shim * 0.45f;
                }

                // 8-line FDN.
                const float size = sizeSmooth.getNextValue();
                lfoPhase += lfoInc;
                if (lfoPhase > 2.0f * pi)
                    lfoPhase -= 2.0f * pi;

                std::array<float, numLines> y;
                for (int l = 0; l < numLines; ++l)
                {
                    const float wobble = std::sin (lfoPhase * (0.8f + 0.07f * (float) l) + (float) l * 0.785f);
                    const float delay = baseMs[(size_t) l] * 0.001f * (float) sampleRate * size + modSamples * (1.0f + wobble);
                    float v = lines[l].readFrac (juce::jmin (delay, (float) lines[l].capacity()));
                    // An allpass inside every line multiplies the echo density on each trip round the loop.
                    const float apDelay = juce::jlimit (2.0f, (float) loopDiffusers[l].line.capacity(), loopDiffMs[(size_t) l] * 0.001f * (float) sampleRate * size);
                    v = loopDiffusers[l].processFrac (v, apDelay, loopDiffG);
                    const float low = crossover[l].process (v, crossA);
                    v = low * gLow[(size_t) l] + (v - low) * gHigh[(size_t) l];
                    y[(size_t) l] = damp[l].process (v, dampA);
                }

                const float lateL = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * 0.35f;
                const float lateR = (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]) * 0.35f;
                lastLate = 0.5f * (lateL + lateR);

                // Fast Hadamard mix, then inject the input.
                for (int h = 1; h < numLines; h <<= 1)
                    for (int a = 0; a < numLines; a += h << 1)
                        for (int b = a; b < a + h; ++b)
                        {
                            const float p = y[(size_t) b], q = y[(size_t) (b + h)];
                            y[(size_t) b] = p + q;
                            y[(size_t) (b + h)] = p - q;
                        }
                for (int l = 0; l < numLines; ++l)
                {
                    const float in = ((l & 1) == 0 ? d[0] : d[1]) * inSign[(size_t) l] * 0.5f + shimIn * inSign[(size_t) ((l + 3) & 7)];
                    lines[l].push (softLimit (y[(size_t) l] * 0.35355339f + in));
                }

                float outL = lateL + er[0] * earlySmooth.getNextValue() * 0.9f + springOut[0] * 0.3f;
                float outR = lateR + er[1] * earlySmooth.getCurrentValue() * 0.9f + springOut[1] * 0.3f;

                if (activeMode == reverse)
                {
                    outL = reversers[0].process (outL, reverseWindow) * 1.3f;
                    outR = reversers[1].process (outR, reverseWindow) * 1.3f;
                }
                else if (activeMode == gated)
                {
                    const float level = juce::jmax (std::abs (inL[i]), std::abs (inR[i]));
                    if (level > 0.01f)
                        gateHold = gateSamples;
                    const float target = gateHold > 0 ? 1.0f : 0.0f;
                    if (gateHold > 0)
                        --gateHold;
                    gateGain += (target - gateGain) * (target > gateGain ? gateAttack : gateRelease);
                    outL *= gateGain * 1.4f;
                    outR *= gateGain * 1.4f;
                }

                // Reverse layer: a reversed copy of the tail swells up into each hit.
                {
                    const float revL = reverseLayer[0].process (outL, layerWindow) * 1.2f;
                    const float revR = reverseLayer[1].process (outR, layerWindow) * 1.2f;
                    const float amount = reverseSmooth.getNextValue();
                    if (amount > 0.0001f)
                    {
                        const float keep = std::cos (amount * pi * 0.5f), add = std::sin (amount * pi * 0.5f);
                        outL = outL * keep + revL * add;
                        outR = outR * keep + revR * add;
                    }
                }

                // Width (M/S), then the tail filter and the wet EQ.
                const float w = widthSmooth.getNextValue();
                const float mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR) * w;
                outL = mid + side;
                outR = mid - side;

                {
                    const float level = juce::jmax (std::abs (outL), std::abs (outR));
                    tailEnv += (level - tailEnv) * (level > tailEnv ? envAttack : envRelease);
                    if (--filterCountdown <= 0)
                    {
                        filterCountdown = 8;
                        const float e = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (tailEnv, -100.0f) + 48.0f) / 42.0f);
                        const float cutoff = juce::jlimit (20.0f, 20000.0f, s.filterCutoff * std::pow (2.0f, 5.0f * envAmount * (e - 1.0f)));
                        for (auto& f : tailFilter)
                            f.set (cutoff, sampleRate, filterQ);
                    }
                    const float off = filterOff.getNextValue(), wl = filterLp.getNextValue(), wb = filterBp.getNextValue(), wh = filterHp.getNextValue();
                    // The filter always runs on the real signal, so switching it on never clicks.
                    float* outs[2] { &outL, &outR };
                    for (int c = 0; c < 2; ++c)
                    {
                        float lp, bp, hpOut;
                        tailFilter[c].processAll (*outs[c], lp, bp, hpOut);
                        if (off < 0.9999f)
                        {
                            const float filtered = (wl * lp + wh * hpOut) * resoComp + wb * bp * tailFilter[c].k;
                            *outs[c] = *outs[c] * off + softLimit (filtered);
                        }
                    }
                }

                float hp;
                lowCut.process (outL, hp);   outL = hp;
                lowCutR.process (outR, hp);  outR = hp;
                outL = highCut.process (outL, hp);
                outR = highCutR.process (outR, hp);

                wetL[i] = outL * fade;
                wetR[i] = outR * fade;
            }
        }

        // Silences every tail and line without reallocating; safe on the audio thread.
        void reset()
        {
            for (auto& p : preDelay) p.clear();
            for (auto& e : erLines) e.clear();
            for (auto& t : echoTone) t.state = 0.0f;
            activeMode = -1;   // the next process() rebuilds the mode and clears the tank
            gateGain = 0.0f;
            gateHold = 0;
        }

        int getActiveMode() const noexcept { return activeMode; }
        float getGate() const noexcept { return activeMode == gated ? gateGain : 1.0f; }

    private:
        static constexpr int numDiffusers = 4, erTaps = 16;

        void switchMode (int newMode, float sizeKnob)
        {
            activeMode = juce::jlimit (0, numModes - 1, newMode);
            const auto& m = spec (activeMode);

            for (auto& l : lines) l.clear();
            for (auto& a : loopDiffusers) a.clear();
            for (auto& c : diffusers) for (auto& a : c) a.clear();
            for (auto& f : crossover) f.state = 0.0f;
            for (auto& f : damp) f.state = 0.0f;
            for (auto& s : springs) s.clear();
            shifter.clear();
            shimHp.state = 0.0f;
            lastLate = 0.0f;
            for (auto& r : reversers) r.reset ((int) (0.5 * sampleRate));
            for (auto& r : reverseLayer) r.reset ((int) (0.5 * sampleRate));
            for (auto& f : tailFilter) f.clear();
            sizeSmooth.setCurrentAndTargetValue (sizeFactor (sizeKnob) * m.scale);

            // Diffuser lengths grow gently with the algorithm's size.
            const float diffMs[2][numDiffusers] { { 4.7f, 3.6f, 12.7f, 9.3f }, { 4.3f, 3.1f, 11.9f, 8.7f } };
            const float diffScale = std::sqrt (m.scale) * (activeMode == plate ? 1.6f : 1.0f);
            for (int c = 0; c < 2; ++c)
                for (int a = 0; a < numDiffusers; ++a)
                    diffuserDelay[c][(size_t) a] = juce::jlimit (1, diffusers[c][(size_t) a].line.capacity(),
                                                                 (int) (diffMs[c][a] * diffScale * 0.001f * (float) sampleRate));

            // Early reflections: a seeded tap pattern per space, spreading out and decaying over time.
            const float spanMs[3] { 45.0f, 110.0f, 240.0f };
            const float span = spanMs[m.erPattern];
            for (int c = 0; c < 2; ++c)
            {
                juce::Random r (1000 + m.erPattern * 17 + c * 5);
                float energy = 0.0f;
                for (int t = 0; t < erTaps; ++t)
                {
                    const float pos = std::pow (((float) t + 0.3f + 0.6f * r.nextFloat()) / (float) erTaps, 1.6f);
                    const float ms = 1.5f + pos * span;
                    erDelay[c][(size_t) t] = juce::jlimit (1, erLines[c].capacity(), (int) (ms * 0.001f * (float) sampleRate));
                    const float g = std::exp (-2.5f * pos) * (r.nextBool() ? 1.0f : -1.0f) * (0.6f + 0.4f * r.nextFloat());
                    erGain[c][(size_t) t] = g;
                    energy += g * g;
                }
                const float norm = 0.8f / std::sqrt (energy);
                for (auto& g : erGain[c])
                    g *= norm;
            }
        }

        double sampleRate = 44100.0;
        int activeMode = -1;

        // Mutually prime-ish lengths (ms at factor 1); the mode and SIZE scale them.
        static constexpr std::array<float, numLines> baseMs { 31.3f, 37.9f, 43.1f, 49.7f, 56.3f, 62.9f, 71.3f, 79.7f };
        static constexpr std::array<float, numLines> loopDiffMs { 7.3f, 4.1f, 8.9f, 5.3f, 3.1f, 6.7f, 3.9f, 6.1f };
        static constexpr std::array<float, numLines> inSign { 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f };

        std::array<DelayLine, numLines> lines;
        std::array<OnePole, numLines> crossover, damp;
        std::array<Allpass, numLines> loopDiffusers;
        std::array<std::array<Allpass, numDiffusers>, 2> diffusers;
        std::array<std::array<int, numDiffusers>, 2> diffuserDelay {};
        std::array<DelayLine, 2> erLines, preDelay;
        std::array<std::array<int, erTaps>, 2> erDelay {};
        std::array<std::array<float, erTaps>, 2> erGain {};
        std::array<OnePole, 2> echoTone;
        std::array<SpringTank, 2> springs;
        std::array<Reverser, 2> reversers, reverseLayer;
        std::array<SVF, 2> tailFilter;
        juce::SmoothedValue<float> reverseSmooth, filterOff, filterLp, filterBp, filterHp;
        float tailEnv = 0.0f;
        int filterCountdown = 0;
        PitchShifter shifter;
        OnePole shimHp;
        SVF lowCut, lowCutR, highCut, highCutR;

        juce::SmoothedValue<float> sizeSmooth, preDelaySmooth, earlySmooth, widthSmooth, driveSmooth, shimmerSmooth, feedbackSmooth, fadeSmooth;
        float lfoPhase = 0.0f, lastLate = 0.0f;
        float gateGain = 0.0f;
        int gateHold = 0;
    };
}
