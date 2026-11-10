#include "Engine.h"

namespace skazon
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        constexpr float pi = juce::MathConstants<float>::pi;

        double noteHz (float semis) { return 440.0 * std::exp2 ((semis - 69.0) / 12.0); }

        // One single-cycle frame: table t, morph f (0..1), frame index fi, phase x (0..1).
        float generate (int t, float f, int fi, float x)
        {
            switch (t)
            {
                case 0: // BASIC: sine -> triangle -> saw -> square
                {
                    const float sine = std::sin (twoPi * x), tri = std::asin (sine) * 2.0f / pi; // phase-aligned shapes
                    const float saw = 2.0f * std::fmod (x + 0.5f, 1.0f) - 1.0f, sq = x < 0.5f ? 1.0f : -1.0f;
                    const float m = f * 3.0f;
                    if (m < 1.0f) return juce::jmap (m, sine, tri);
                    if (m < 2.0f) return juce::jmap (m - 1.0f, tri, saw);
                    return juce::jmap (m - 2.0f, saw, sq);
                }
                case 1: return x < 0.5f - 0.47f * f ? 1.0f : -1.0f;                              // PWM
                case 2: return 2.0f * std::fmod (x * (1.0f + 7.0f * f), 1.0f) - 1.0f;               // SYNC
                case 3: return std::sin (std::sin (twoPi * x) * (1.0f + 9.0f * f) * pi * 0.5f);     // FOLD
                case 4: return std::sin (twoPi * (1.0f + 15.0f * f) * x) * (0.5f - 0.5f * std::cos (twoPi * x)); // FORMANT
                case 5: return std::tanh (3.0f * std::sin (twoPi * x + (0.5f + 5.0f * f) * std::sin (twoPi * 2.0f * x + 3.0f * f))); // GROWL
                case 6: return std::sin (twoPi * x + 10.0f * f * std::sin (twoPi * 2.0f * x));    // FM
                case 7: // HARMONIC: saw built up one harmonic at a time
                {
                    const float n = 1.0f + 63.0f * f;
                    float v = 0;
                    for (int k = 1; k <= (int) std::ceil (n); ++k)
                        v += std::sin (twoPi * (float) k * x) / (float) k * juce::jmin (1.0f, n - (float) k + 1.0f);
                    return v;
                }
                case 8: // BITS
                {
                    const float steps = std::exp2 (6.0f - 5.0f * f);
                    return std::round (std::sin (twoPi * x) * steps) / steps;
                }
                case 9: // SPECTRAL: odd partials with per-frame random weights
                {
                    juce::Random r (1000 + fi * 77);
                    float v = 0;
                    for (int k = 1; k < 48; k += 2)
                        v += std::sin (twoPi * (float) k * x) * juce::square (r.nextFloat()) / std::sqrt ((float) k);
                    return v;
                }
                case 10: // PHASE DIST
                {
                    const float d = 0.5f - 0.48f * f;
                    return std::sin (twoPi * (x < d ? 0.5f * x / d : 0.5f + 0.5f * (x - d) / (1.0f - d)));
                }
                default: // VOX: two moving formants over a harmonic comb
                {
                    const float f1 = 2.5f + 6.0f * f, f2 = 7.0f + 12.0f * std::sin (pi * f);
                    float v = 0;
                    for (int k = 1; k <= 40; ++k)
                        v += std::sin (twoPi * (float) k * x)
                             * (std::exp (-juce::square ((float) k - f1) / 3.0f) + 0.7f * std::exp (-juce::square ((float) k - f2) / 6.0f) + 0.04f / (float) k);
                    return v;
                }
            }
        }
    }

    const juce::StringArray& tableNames()
    {
        static const juce::StringArray n { "BASIC", "PWM", "SYNC", "FOLD", "FORMANT", "GROWL", "FM", "HARMONIC", "BITS", "SPECTRAL", "PHASE DIST", "VOX", "USER" };
        return n;
    }
    const juce::StringArray& sourceNames()
    {
        static const juce::StringArray n { "-", "AMP ENV", "MOD ENV", "LFO 1", "LFO 2", "VELOCITY", "NOTE", "MOD WHEEL", "AFTERTOUCH", "RANDOM",
                                           "LFO 3", "LFO 4", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4" };
        return n;
    }
    const juce::StringArray& targetNames()
    {
        static const juce::StringArray n { "-", "A POS", "B POS", "GRAIN POS", "PITCH", "A PITCH", "B PITCH", "A LEVEL", "B LEVEL", "GRAIN LEVEL",
                                           "GRAIN SIZE", "GRAIN DENS", "CUTOFF", "RESO", "DRIVE", "PAN", "LFO1 RATE", "LFO2 RATE",
                                           "A WARP", "B WARP", "A DETUNE", "B DETUNE", "GRAIN SPRAY", "SUB", "NOISE", "LFO3 RATE", "LFO4 RATE", "FILTER ENV" };
        return n;
    }
    const juce::StringArray& opNames()
    {
        static const juce::StringArray n { "LINEAR", "INVERT", "ABS", "CURVE", "QUANTIZE", "SINE FOLD" };
        return n;
    }
    const juce::StringArray& lfoShapeNames()
    {
        static const juce::StringArray n { "SINE", "TRI", "SAW", "SQUARE", "S&H", "RAMP DOWN", "SMOOTH RND", "PULSE 25%", "STAIRS", "BOUNCE" };
        return n;
    }
    const juce::StringArray& warpNames()
    {
        static const juce::StringArray n { "NO WARP", "SYNC", "BEND +", "BEND -", "PWM", "FM < OTHER", "RM < OTHER", "QUANTIZE" };
        return n;
    }
    const juce::StringArray& filterNames()
    {
        static const juce::StringArray n { "LOW PASS", "BAND PASS", "HIGH PASS", "NOTCH", "COMB" };
        return n;
    }
    const juce::StringArray& divNames()
    {
        static const juce::StringArray n { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1 BAR", "2 BAR", "4 BAR", "8 BAR", "16 BAR" }; // new ones only at the end: presets store indices
        return n;
    }
    double divBeats (int i)
    {
        static const double b[] { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
        return b[juce::jlimit (0, 13, i)];
    }

    float applyOp (int op, float x)
    {
        switch (op)
        {
            case opInvert:   return -x;
            case opAbs:      return std::abs (x);
            case opSquare:   return x * std::abs (x);
            case opQuantize: return std::round (x * 4.0f) / 4.0f;
            case opFold:     return std::sin (x * pi);
            default:         return x;
        }
    }

    float lfoShape (int shape, float p, float hold, float prevHold)
    {
        switch (shape)
        {
            case 1:  return 1.0f - 4.0f * std::abs (p - 0.5f);
            case 2:  return 2.0f * p - 1.0f;
            case 3:  return p < 0.5f ? 1.0f : -1.0f;
            case 4:  return hold;
            case 5:  return 1.0f - 2.0f * p;
            case 6:  return prevHold + (hold - prevHold) * (0.5f - 0.5f * std::cos (pi * p));
            case 7:  return p < 0.25f ? 1.0f : -1.0f;
            case 8:  return std::floor (p * 4.0f) / 1.5f - 1.0f;
            case 9:  return 1.0f - 2.0f * std::abs (std::sin (twoPi * p));
            default: return std::sin (twoPi * p);
        }
    }

    //==============================================================================
    const Wavetables& Wavetables::get()
    {
        static const Wavetables tables;
        return tables;
    }

    // Band-limits one single-cycle frame (tableSize samples) into numMips octave mips at dst.
    static void bandlimitFrame (juce::dsp::FFT& fft, const float* in, float* dst)
    {
        std::vector<float> spectrum ((size_t) tableSize * 2, 0.0f), work;
        std::copy (in, in + tableSize, spectrum.begin());
        fft.performRealOnlyForwardTransform (spectrum.data());

        float scale = 1.0f;
        for (int m = 0; m < numMips; ++m)
        {
            work = spectrum;
            const int keep = (tableSize / 2) >> m;
            work[0] = work[1] = 0.0f; // no DC
            for (int k = keep + 1; k <= tableSize / 2; ++k)
            {
                work[(size_t) k * 2] = work[(size_t) k * 2 + 1] = 0.0f;
                work[(size_t) (tableSize - k) * 2] = work[(size_t) (tableSize - k) * 2 + 1] = 0.0f;
            }
            fft.performRealOnlyInverseTransform (work.data());

            if (m == 0) // every mip of a frame shares mip 0's peak normalisation
            {
                float peak = 1.0e-6f;
                for (int i = 0; i < tableSize; ++i)
                    peak = juce::jmax (peak, std::abs (work[(size_t) i]));
                scale = 1.0f / peak;
            }

            auto* out = dst + (size_t) m * (tableSize + 1);
            for (int i = 0; i < tableSize; ++i)
                out[i] = work[(size_t) i] * scale;
            out[tableSize] = out[0];
        }
    }

    Wavetables::Wavetables()
    {
        const int stride = tableSize + 1;
        data.resize ((size_t) (numBuiltInTables * numFrames * numMips * stride));
        juce::dsp::FFT fft (11);
        std::vector<float> frameData ((size_t) tableSize);
        for (int t = 0; t < numBuiltInTables; ++t)
            for (int fi = 0; fi < numFrames; ++fi)
            {
                for (int i = 0; i < tableSize; ++i)
                    frameData[(size_t) i] = generate (t, (float) fi / (float) (numFrames - 1), fi, (float) i / (float) tableSize);
                bandlimitFrame (fft, frameData.data(), data.data() + ((size_t) (t * numFrames + fi) * numMips) * (size_t) stride);
            }
    }

    std::shared_ptr<const UserTable> makeUserTable (const juce::AudioBuffer<float>& audio, const juce::String& name)
    {
        // Mono mix; 16 frames spread evenly over the file (like Serum's import of a long sample).
        const int len = audio.getNumSamples();
        if (len < 64)
            return nullptr;
        std::vector<float> mono ((size_t) len);
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int i = 0; i < len; ++i)
                mono[(size_t) i] += audio.getSample (c, i) / (float) audio.getNumChannels();

        auto table = std::make_shared<UserTable>();
        table->name = name;
        table->data.resize ((size_t) numFrames * numMips * (tableSize + 1));
        juce::dsp::FFT fft (11);
        std::vector<float> frameData ((size_t) tableSize);
        const bool stretch = len < tableSize * 2; // short sounds: one frame = the whole file, resampled
        for (int fi = 0; fi < numFrames; ++fi)
        {
            const double start = stretch ? 0.0 : (double) (len - tableSize) * fi / (numFrames - 1);
            const double step = stretch ? (double) (len - 1) / tableSize : 1.0;
            for (int i = 0; i < tableSize; ++i)
            {
                const double x = start + i * step;
                const int k = juce::jmin ((int) x, len - 2);
                frameData[(size_t) i] = mono[(size_t) k] + (float) (x - k) * (mono[(size_t) k + 1] - mono[(size_t) k]);
            }
            // Crossfade the last 128 samples into the first so the loop point doesn't click.
            for (int i = 0; i < 128; ++i)
            {
                const float w = (float) i / 128.0f;
                auto& v = frameData[(size_t) (tableSize - 128 + i)];
                v = v * (1.0f - w) + frameData[(size_t) i] * w;
            }
            bandlimitFrame (fft, frameData.data(), table->data.data() + (size_t) fi * numMips * (tableSize + 1));
        }
        return table;
    }

    std::shared_ptr<const UserTable> makeUserTableFromFrames (const std::vector<float>& frames, const juce::String& name)
    {
        if (frames.size() != (size_t) numFrames * tableSize)
            return nullptr;
        auto table = std::make_shared<UserTable>();
        table->name = name;
        table->data.resize ((size_t) numFrames * numMips * (tableSize + 1));
        juce::dsp::FFT fft (11);
        for (int fi = 0; fi < numFrames; ++fi)
            bandlimitFrame (fft, frames.data() + (size_t) fi * tableSize, table->data.data() + (size_t) fi * numMips * (tableSize + 1));
        return table;
    }

    const float* Wavetables::frame (int t, int f, int m) const
    {
        return data.data() + ((size_t) ((t * numFrames + f) * numMips + m)) * (size_t) (tableSize + 1);
    }

    int Wavetables::mipFor (double hz, double sampleRate)
    {
        // mip m keeps harmonics up to 1024 >> m; pick the first whose top partial stays under Nyquist.
        return juce::jlimit (0, numMips - 1, (int) std::ceil (std::log2 (juce::jmax (1.0e-3, hz * tableSize / sampleRate))));
    }

    float readFrames (const float* base, float pos, int m, float phase)
    {
        const float fp = juce::jlimit (0.0f, 1.0f, pos) * (float) (numFrames - 1);
        const int f0 = juce::jmin ((int) fp, numFrames - 2);
        const float ff = fp - (float) f0;
        const float idx = phase * (float) tableSize;
        const int i = juce::jlimit (0, tableSize - 1, (int) idx);
        const float fr = idx - (float) i;
        const float* a = base + ((size_t) f0 * numMips + (size_t) m) * (tableSize + 1);
        const float* b = a + (size_t) numMips * (tableSize + 1);
        const float va = a[i] + fr * (a[i + 1] - a[i]);
        const float vb = b[i] + fr * (b[i + 1] - b[i]);
        return va + ff * (vb - va);
    }

    float Wavetables::read (int t, float pos, int m, float phase) const
    {
        return readFrames (frame (juce::jlimit (0, numBuiltInTables - 1, t), 0, 0), pos, m, phase);
    }

    const float* tableBase (int table, const UserTable* user)
    {
        if (table >= numBuiltInTables)
            return user != nullptr ? user->data.data() : Wavetables::get().frame (0, 0, 0);
        return Wavetables::get().frame (juce::jmax (0, table), 0, 0);
    }

    //==============================================================================
        float warpPhase (int mode, float amt, float ph, float other)
        {
            switch (mode)
            {
                case wSync:     { const float x = ph * (1.0f + amt * 7.0f); return x - std::floor (x); }
                case wBendUp:   return std::pow (ph, std::exp2 (-amt * 2.5f));
                case wBendDown: return std::pow (ph, std::exp2 (amt * 2.5f));
                case wPwm:      { const float w = 1.0f - amt * 0.95f; return ph < w ? ph / w : 0.0f; }
                case wFm:       { const float x = ph + amt * other * 1.5f; return x - std::floor (x); }
                default:        return ph;
            }
        }

        float warpOutput (int mode, float amt, float v, float other)
        {
            if (mode == wRing)
                return v * (1.0f - amt + amt * other);
            if (mode == wQuantize && amt > 0.0f)
            {
                const float steps = std::exp2 (6.0f - amt * 5.0f);
                return std::round (v * steps) / steps;
            }
            return v;
        }

    Voice::Voice (const Snapshot& snapshot, int& last) : s (snapshot), lastNote (last)
    {
        for (auto& c : comb)
            c.assign (combSize, 0.0f);
    }

    void Voice::setCurrentPlaybackSampleRate (double sr)
    {
        SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
        if (sr <= 0)
            return;
        amp.setSampleRate (sr);
        modEnv.setSampleRate (sr / chunk); // advanced once per control tick
        filter.prepare ({ sr, 512, 2 });
    }

    void Voice::startNote (int n, float vel, juce::SynthesiserSound*, int)
    {
        note = n;
        velocity = vel;
        randomValue = rng.nextFloat() * 2.0f - 1.0f;
        polyPressure = 0;
        semi = (s.glide > 0.0f && lastNote >= 0) ? (float) lastNote : (float) n;
        bendNow = s.bend;
        lastNote = n;

        for (auto& osc : phase)
            for (auto& p : osc)
                p = rng.nextFloat();
        subPhase = 0;
        oscOut[0] = oscOut[1] = 0;
        for (int i = 0; i < numLfos; ++i)
        {
            lfoPhase[i] = 0;
            lfoHold[i] = rng.nextFloat() * 2.0f - 1.0f;
            lfoPrev[i] = 0;
            rateMod[i] = 0;
        }
        for (auto& g : grains)
            g.on = false;
        for (auto& c : comb)
            std::fill (c.begin(), c.end(), 0.0f);
        grainCountdown = 0;
        sinceTick = 0;
        std::fill (std::begin (levelNow), std::end (levelNow), 0.0f);
        env1Value = env2Value = 0;

        amp.setParameters (s.env[0]);
        modEnv.setParameters (s.env[1]);
        amp.noteOn();
        modEnv.noteOn();
        filter.reset();
    }

    void Voice::legato (int newNote, float newVelocity)
    {
        note = newNote;
        velocity = newVelocity;
        lastNote = newNote;
        if (s.glide <= 0.0f)
            semi = (float) newNote;
    }

    void Voice::stopNote (float, bool allowTailOff)
    {
        if (allowTailOff)
        {
            amp.noteOff();
            modEnv.noteOff();
        }
        else
        {
            amp.reset();
            modEnv.reset();
            clearCurrentNote();
        }
    }

    float Voice::source (int src) const
    {
        switch (src)
        {
            case sEnv1:       return env1Value;
            case sEnv2:       return env2Value;
            case sLfo1:       return lfoShape (s.lfo[0].shape, lfoPhase[0], lfoHold[0], lfoPrev[0]);
            case sLfo2:       return lfoShape (s.lfo[1].shape, lfoPhase[1], lfoHold[1], lfoPrev[1]);
            case sLfo3:       return lfoShape (s.lfo[2].shape, lfoPhase[2], lfoHold[2], lfoPrev[2]);
            case sLfo4:       return lfoShape (s.lfo[3].shape, lfoPhase[3], lfoHold[3], lfoPrev[3]);
            case sVelocity:   return velocity;
            case sNote:       return (float) (note - 60) / 48.0f;
            case sWheel:      return s.wheel;
            case sAftertouch: return juce::jmax (s.aftertouch, polyPressure);
            case sRandom:     return randomValue;
            case sMacro1: case sMacro2: case sMacro3: case sMacro4: return s.macro[src - sMacro1];
            default:          return 0.0f;
        }
    }

    void Voice::controlTick()
    {
        const double sr = getSampleRate();

        amp.setParameters (s.env[0]);
        modEnv.setParameters (s.env[1]);
        env2Value = modEnv.getNextSample();

        for (int i = 0; i < numLfos; ++i)
        {
            lfoPhase[i] += (float) (s.lfo[i].hz * std::exp2 (rateMod[i] * 4.0f) * chunk / sr);
            if (lfoPhase[i] >= 1.0f)
            {
                lfoPhase[i] -= std::floor (lfoPhase[i]);
                lfoPrev[i] = lfoHold[i];
                lfoHold[i] = rng.nextFloat() * 2.0f - 1.0f;
            }
            liveLfo[i] = lfoPhase[i];
        }

        float mods[numTargets] {};
        for (const auto& slot : s.slots)
            if (slot.src != sNone && slot.dst != tNone && slot.amount != 0.0f)
                mods[slot.dst] += applyOp (slot.op, source (slot.src)) * slot.amount;
        if (s.mwDst != tNone && s.wheel > 0.0f)
        {
            static constexpr int lfoSources[] { sNone, sLfo1, sLfo2, sLfo3, sLfo4 };
            mods[s.mwDst] += (s.mwLfo == 0 ? 1.0f : source (lfoSources[juce::jlimit (0, 4, s.mwLfo)])) * s.wheel * s.mwAmt;
        }
        std::copy (std::begin (mods), std::end (mods), std::begin (liveMods));
        rateMod[0] = mods[tLfo1Rate];
        rateMod[1] = mods[tLfo2Rate];
        rateMod[2] = mods[tLfo3Rate];
        rateMod[3] = mods[tLfo4Rate];

        semi = s.glide > 0.0f ? (float) note + (semi - (float) note) * (float) std::exp (-chunk / (s.glide * sr)) : (float) note;
        bendNow += (s.bend - bendNow) * (1.0f - std::exp (-(float) chunk / (0.006f * (float) sr))); // glides over block steps
        const float pitch = semi + bendNow + mods[tPitch] * 24.0f;

        for (int i = 0; i < 2; ++i)
        {
            const auto& o = s.osc[i];
            const double hz = noteHz (pitch + o.tune + mods[tAPitch + i] * 24.0f);
            const int n = juce::jlimit (1, maxUnison, o.unison);
            const float detune = juce::jlimit (0.0f, 1.0f, o.detune + mods[tADetune + i]);
            ctl.warp[i] = juce::jlimit (0.0f, 1.0f, o.warpAmt + mods[tAWarp + i]);
            // Sync and FM push partials up, so read a darker mip to keep them under Nyquist.
            const double brighten = o.warp == wSync ? 1.0 + ctl.warp[i] * 7.0 : o.warp == wFm ? 1.0 + ctl.warp[i] * 3.0 : 1.0;
            ctl.n[i] = n;
            ctl.mip[i] = Wavetables::mipFor (hz * brighten * std::exp2 (detune / 12.0), sr);
            ctl.pos[i] = juce::jlimit (0.0f, 1.0f, o.pos + mods[tAPos + i]);
            ctl.levelTarget[i] = o.on ? juce::jlimit (0.0f, 1.0f, o.level + mods[tALevel + i]) : 0.0f;
            for (int u = 0; u < n; ++u)
            {
                const float spread = n > 1 ? 2.0f * (float) u / (float) (n - 1) - 1.0f : 0.0f;
                ctl.inc[i][u] = (float) (hz * std::exp2 (spread * detune / 12.0) / sr);
                const float angle = (spread * 0.9f + 1.0f) * pi * 0.25f;
                ctl.panL[i][u] = std::cos (angle) / std::sqrt ((float) n);
                ctl.panR[i][u] = std::sin (angle) / std::sqrt ((float) n);
            }
        }

        const float sizeMs = juce::jlimit (5.0f, 2000.0f, s.gSizeMs * std::exp2 (mods[tGSize] * 3.0f));
        const float density = juce::jlimit (0.5f, 400.0f, s.gDensity * std::exp2 (mods[tGDensity] * 3.0f));
        ctl.gHz = (float) noteHz (pitch + s.gPitch);
        ctl.gPos = juce::jlimit (0.0f, 1.0f, s.gPos + mods[tGPos]);
        ctl.gSpray = juce::jlimit (0.0f, 1.0f, s.gSpray + mods[tGSpray]);
        ctl.gLength = juce::jmax (32, (int) (sizeMs * 0.001 * sr));
        ctl.gInterval = (float) (sr / density);
        ctl.levelTarget[2] = s.gOn ? juce::jlimit (0.0f, 1.0f, s.gLevel + mods[tGLevel]) / std::sqrt (juce::jmax (1.0f, density * sizeMs * 0.001f * 0.5f)) : 0.0f;

        ctl.subInc = (float) (noteHz (pitch - 12.0f) / sr);
        ctl.sub = juce::jlimit (0.0f, 1.0f, s.sub + mods[tSub]);
        ctl.noise = juce::jlimit (0.0f, 1.0f, s.noise + mods[tNoise]);

        const float envAmt = juce::jlimit (-1.0f, 1.0f, s.fEnv + mods[tFEnv]);
        const float cutSemis = envAmt * env2Value * 72.0f + s.fKey * (pitch - 60.0f) + mods[tCutoff] * 96.0f;
        const float cutoff = juce::jlimit (20.0f, (float) sr * 0.45f, s.cutoff * std::exp2 (cutSemis / 12.0f));
        const float res = juce::jlimit (0.0f, 1.0f, s.res + mods[tRes]);
        filter.setCutoffFrequency (cutoff);
        ctl.resonance = 0.5f * std::pow (36.0f, res);
        filter.setResonance (ctl.resonance);
        filter.setType (s.fType == fBand || s.fType == fNotch ? juce::dsp::StateVariableTPTFilterType::bandpass
                      : s.fType == fHigh ? juce::dsp::StateVariableTPTFilterType::highpass
                                         : juce::dsp::StateVariableTPTFilterType::lowpass);
        ctl.combDelay = juce::jlimit (2, combSize - 1, (int) (sr / cutoff));
        ctl.combFb = 0.3f + res * 0.67f;

        const float d = juce::jlimit (0.0f, 1.0f, s.drive + mods[tDrive]);
        ctl.preGain = 1.0f + d * d * 20.0f;
        ctl.postGain = 1.0f / (1.0f + d * 1.5f);

        const float pan = juce::jlimit (-1.0f, 1.0f, mods[tPan]);
        ctl.outL = std::cos ((pan + 1.0f) * pi * 0.25f) * juce::MathConstants<float>::sqrt2;
        ctl.outR = std::sin ((pan + 1.0f) * pi * 0.25f) * juce::MathConstants<float>::sqrt2;

        livePos[0] = ctl.pos[0];
        livePos[1] = ctl.pos[1];
        livePos[2] = ctl.gPos;
    }

    void Voice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int num)
    {
        if (! isVoiceActive())
            return;

        const double sr = getSampleRate();
        const float* oscBase[2] { tableBase (s.osc[0].table, s.user), tableBase (s.osc[1].table, s.user) };
        const float* grainBase = tableBase (s.gTable, s.user);
        auto* outL = out.getWritePointer (0);
        auto* outR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        const float velGain = 0.3f + 0.7f * velocity;

        for (int i = start; i < start + num; ++i)
        {
            if (sinceTick == 0)
            {
                controlTick();
                for (int k = 0; k < 3; ++k)
                    levelStep[k] = (ctl.levelTarget[k] - levelNow[k]) / (float) chunk;
            }
            sinceTick = (sinceTick + 1) % chunk;

            float l = 0, r = 0;
            float newOut[2] {};
            for (int o = 0; o < 2; ++o)
            {
                levelNow[o] += levelStep[o];
                const auto& osc = s.osc[o];
                const int otherWarp = s.osc[1 - o].warp;
                const bool feedsOther = osc.on && (otherWarp == wFm || otherWarp == wRing); // runs even at level 0
                if (! feedsOther && levelNow[o] <= 1.0e-5f && ctl.levelTarget[o] <= 0.0f)
                    continue;
                float ol = 0, orr = 0;
                const float other = oscOut[1 - o];
                for (int u = 0; u < ctl.n[o]; ++u)
                {
                    auto& ph = phase[o][u];
                    float v = readFrames (oscBase[o], ctl.pos[o], ctl.mip[o], warpPhase (osc.warp, ctl.warp[o], ph, other));
                    v = warpOutput (osc.warp, ctl.warp[o], v, other);
                    ol += v * ctl.panL[o][u];
                    orr += v * ctl.panR[o][u];
                    ph += ctl.inc[o][u];
                    ph -= std::floor (ph);
                }
                newOut[o] = juce::jlimit (-1.5f, 1.5f, (ol + orr) * 0.7f);
                l += ol * levelNow[o];
                r += orr * levelNow[o];
            }
            oscOut[0] = newOut[0];
            oscOut[1] = newOut[1];

            // Grain cloud: Hann-windowed bursts of the grain table, scattered in morph position, pitch and pan.
            levelNow[2] += levelStep[2];
            if (s.gOn || levelNow[2] > 1.0e-5f)
            {
                if (--grainCountdown <= 0 && s.gOn)
                {
                    grainCountdown = juce::jmax (1, (int) (ctl.gInterval * (0.7f + 0.6f * rng.nextFloat())));
                    for (auto& g : grains)
                        if (! g.on)
                        {
                            const float detune = (rng.nextFloat() * 2.0f - 1.0f) * s.gSpread;
                            const double hz = ctl.gHz * std::exp2 (detune / 12.0);
                            const float angle = (1.0f + (rng.nextFloat() * 2.0f - 1.0f) * juce::jmin (1.0f, ctl.gSpray + 0.3f)) * pi * 0.25f;
                            g = { true, rng.nextFloat(), (float) (hz / sr),
                                  juce::jlimit (0.0f, 1.0f, ctl.gPos + (rng.nextFloat() - 0.5f) * ctl.gSpray),
                                  0, ctl.gLength, Wavetables::mipFor (hz, sr), std::cos (angle), std::sin (angle) };
                            break;
                        }
                }

                float gl = 0, gr = 0;
                for (auto& g : grains)
                {
                    if (! g.on)
                        continue;
                    const float w = juce::square (std::sin (pi * (float) g.age / (float) g.length));
                    const float v = readFrames (grainBase, g.pos, g.mip, g.phase) * w;
                    gl += v * g.gl;
                    gr += v * g.gr;
                    g.phase += g.inc;
                    g.phase -= std::floor (g.phase);
                    g.on = ++g.age < g.length;
                }
                l += gl * levelNow[2];
                r += gr * levelNow[2];
            }

            const float extra = (std::sin (twoPi * subPhase) * ctl.sub + (rng.nextFloat() * 2.0f - 1.0f) * ctl.noise * 0.5f) * 0.7f;
            subPhase += ctl.subInc;
            subPhase -= std::floor (subPhase);
            l = std::tanh ((l + extra) * ctl.preGain) * ctl.postGain;
            r = std::tanh ((r + extra) * ctl.preGain) * ctl.postGain;

            if (s.fType == fComb)
            {
                const int read = (combWrite - ctl.combDelay) & (combSize - 1);
                const float yl = l + ctl.combFb * comb[0][(size_t) read];
                const float yr = r + ctl.combFb * comb[1][(size_t) read];
                comb[0][(size_t) combWrite] = std::tanh (yl);
                comb[1][(size_t) combWrite] = std::tanh (yr);
                combWrite = (combWrite + 1) & (combSize - 1);
                l = yl * (1.0f - ctl.combFb * 0.6f);
                r = yr * (1.0f - ctl.combFb * 0.6f);
            }
            else if (s.fType == fNotch)
            {
                // TPT SVF: notch = input - bandpass / Q
                l -= filter.processSample (0, l) / ctl.resonance;
                r -= filter.processSample (1, r) / ctl.resonance;
            }
            else
            {
                l = filter.processSample (0, l);
                r = filter.processSample (1, r);
            }

            env1Value = amp.getNextSample();
            const float g = env1Value * velGain * 0.17f; // 4-note chords peak under 0 dBFS
            outL[i] += l * g * ctl.outL;
            if (outR != nullptr)
                outR[i] += r * g * ctl.outR;

            if (! amp.isActive())
            {
                for (auto& gr : grains)
                    gr.on = false;
                clearCurrentNote();
                break;
            }
        }
    }
}
