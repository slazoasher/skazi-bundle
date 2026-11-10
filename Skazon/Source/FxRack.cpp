#include "FxRack.h"

namespace skazon
{
    const juce::StringArray& distNames()
    {
        static const juce::StringArray n { "SOFT", "HARD CLIP", "TUBE", "FOLD", "BIT CRUSH", "RECTIFY", "SCREAM" };
        return n;
    }

    void FxRack::prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        const juce::dsp::ProcessSpec stereo { sampleRate, (juce::uint32) blockSize, 2 }, mono { sampleRate, (juce::uint32) blockSize, 1 };

        for (int c = 0; c < 2; ++c)
        {
            lowShelf[c].prepare (mono);
            highShelf[c].prepare (mono);
            crushHold[c] = dcX[c] = dcY[c] = toneZ[c] = 0;
        }
        std::fill (std::begin (eqState), std::end (eqState), 1e9f);

        phaser.prepare (stereo);
        phaser.setCentreFrequency (900.0f);
        chorus.prepare (stereo);
        chorus.setCentreDelay (9.0f);
        chorus.setFeedback (0.0f);
        for (auto& b : hyperBuf)
            b.assign (hyperSize, 0.0f);
        hyperWrite = 0;
        delayBuffer.setSize (2, (int) (sampleRate * 4.5) + 1);
        delayBuffer.clear();
        delayWrite = 0;
        reverb.setSampleRate (sampleRate);
        reverb.reset();

        xLow.prepare (stereo);
        xLow.setCutoffFrequency (120.0f);
        xHigh.prepare (stereo);
        xHigh.setCutoffFrequency (2500.0f);
        std::fill (std::begin (ottEnv), std::end (ottEnv), 0.0f);

        outGain.reset (sampleRate, 0.05);
        limiter.prepare (stereo);
        limiter.setThreshold (-0.3f);
        limiter.setRelease (80.0f);
    }

    float FxRack::transfer (int type, float d, float x)
    {
        const float k = type == 0 ? 1.0f + d * 8.0f : 1.0f + d * 24.0f;
        const float comp = type == 0 ? 1.0f / (1.0f + d * 2.0f) : 1.0f / (1.0f + d * 3.0f);
        float y;
        switch (type)
        {
            case 1:  y = juce::jlimit (-1.0f, 1.0f, x * k); break;
            case 2:  { const float v = x * k; y = v >= 0.0f ? 1.0f - std::exp (-v) : -(1.0f - std::exp (v * 1.4f)) * 0.75f; break; }
            case 3:  y = std::sin (x * k * juce::MathConstants<float>::halfPi); break;
            case 4:  { const float bits = std::exp2 (12.0f - d * 9.0f); y = std::round (juce::jlimit (-1.0f, 1.0f, x * (1.0f + d * 3.0f)) * bits) / bits; break; }
            case 5:  y = std::tanh (std::abs (x) * k) * 1.4f - 0.2f; break;
            case 6:  { const float v = x * k; y = std::tanh (v + 0.8f * std::sin (v * 2.0f)); break; } // folded screech, odd so no DC
            default: y = std::tanh (x * k); break;
        }
        return y * comp;
    }

    void FxRack::distort (float* l, float* r, int n, const Params& p)
    {
        const float d = p.distDrive;
        const float comp = p.distType == 0 ? 1.0f / (1.0f + d * 2.0f) : 1.0f / (1.0f + d * 3.0f);
        const float toneCoef = std::exp (-juce::MathConstants<float>::twoPi * juce::jlimit (200.0f, 20000.0f, p.distTone) / (float) sr);
        const bool toneOn = p.distTone < 19000.0f;
        const float bits = std::exp2 (12.0f - d * 9.0f);
        const int hold = 1 + (int) (d * 24.0f);
        float* ch[] { l, r };

        for (int i = 0; i < n; ++i)
        {
            const bool newCrush = ++crushCount >= hold;
            if (newCrush)
                crushCount = 0;
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                float y;
                if (p.distType == 4)
                {
                    if (newCrush)
                        crushHold[c] = std::round (juce::jlimit (-1.0f, 1.0f, x * (1.0f + d * 3.0f)) * bits) / bits;
                    y = crushHold[c] * comp;
                }
                else
                    y = transfer (p.distType, d, x);
                if (p.distType >= 2) // asymmetric shapes make DC
                {
                    const float out = y - dcX[c] + 0.995f * dcY[c];
                    dcX[c] = y;
                    dcY[c] = out;
                    y = out;
                }
                if (toneOn)
                    y = toneZ[c] = y + (toneZ[c] - y) * toneCoef;
                ch[c][i] = x + (y - x) * p.distMix;
            }
        }
    }

    void FxRack::ott (float* l, float* r, int n, const Params& p)
    {
        const float attackMs = 2.0f * (0.25f + p.ottTime * 3.0f), releaseMs = 90.0f * (0.25f + p.ottTime * 3.0f);
        const float att = std::exp (-1.0f / (attackMs * 0.001f * (float) sr)), rel = std::exp (-1.0f / (releaseMs * 0.001f * (float) sr));
        constexpr float downThresh[] { -30.0f, -28.0f, -30.0f }, upThresh[] { -42.0f, -40.0f, -42.0f }, makeup[] { 2.0f, 1.0f, 3.0f };
        const float out = juce::Decibels::decibelsToGain (p.ottGain);
        float lastGain[3] {};

        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = c == 0 ? l[i] : r[i];
                float lo, rest, mid, hi;
                xLow.processSample (c, x, lo, rest);
                xHigh.processSample (c, rest, mid, hi);
                band[0][c] = lo; band[1][c] = mid; band[2][c] = hi;
            }

            float wet[2] {};
            for (int b = 0; b < 3; ++b)
            {
                const float level = juce::jmax (std::abs (band[b][0]), std::abs (band[b][1]));
                ottEnv[b] = level > ottEnv[b] ? level + (ottEnv[b] - level) * att : level + (ottEnv[b] - level) * rel;
                const float db = juce::Decibels::gainToDecibels (ottEnv[b], -90.0f);
                float gain = makeup[b];
                if (db > downThresh[b])
                    gain -= (db - downThresh[b]) * 0.95f * p.ottDown;            // ~20:1 down
                else if (db < upThresh[b] && db > -70.0f)
                    gain += juce::jmin (24.0f, (upThresh[b] - db) * 0.75f * p.ottUp); // ~4:1 up
                lastGain[b] = gain;
                const float g = juce::Decibels::decibelsToGain (gain);
                wet[0] += band[b][0] * g;
                wet[1] += band[b][1] * g;
            }
            l[i] += (wet[0] * out - l[i]) * p.ottDepth;
            r[i] += (wet[1] * out - r[i]) * p.ottDepth;
        }
        for (int b = 0; b < 3; ++b)
            ottGainDb[(size_t) b] = lastGain[b];
    }

    // HYPER: 4 detuned, slowly drifting delay taps spread across the field (a supersaw for any sound).
    // DIMENSION: a delayed copy of the middle pushed into the sides for width without phasey mono.
    void FxRack::hyper (float* l, float* r, int n, const Params& p)
    {
        constexpr float baseMs[] { 7.0f, 11.0f, 15.0f, 21.0f }, rates[] { 0.31f, 0.47f, 0.59f, 0.73f };
        const float msToSamples = (float) sr * 0.001f;
        auto tap = [this] (int c, float delay)
        {
            const float pos = (float) hyperWrite - delay + (float) hyperSize;
            const int i = (int) pos;
            const float f = pos - (float) i;
            const float a = hyperBuf[c][(size_t) (i & (hyperSize - 1))], b = hyperBuf[c][(size_t) ((i + 1) & (hyperSize - 1))];
            return a + f * (b - a);
        };

        for (int i = 0; i < n; ++i)
        {
            hyperBuf[0][(size_t) hyperWrite] = l[i];
            hyperBuf[1][(size_t) hyperWrite] = r[i];

            float wl = 0, wr = 0;
            if (p.hyMix > 0.0f)
                for (int t = 0; t < 4; ++t)
                {
                    hyperPhase[t] += rates[t] * (0.2f + p.hyDetune * 3.0f) / (float) sr;
                    hyperPhase[t] -= std::floor (hyperPhase[t]);
                    const float d = (baseMs[t] + std::sin (juce::MathConstants<float>::twoPi * hyperPhase[t]) * (1.0f + p.hyDetune * 5.0f)) * msToSamples;
                    const float v = tap (t & 1, d);
                    wl += (t & 1) ? -0.35f * v : v;
                    wr += (t & 1) ? v : -0.35f * v;
                }

            float side = 0;
            if (p.hyDim > 0.0f)
                side = (0.5f * (tap (0, 13.0f * msToSamples) + tap (1, 13.0f * msToSamples)) - 0.5f * (tap (0, 19.0f * msToSamples) + tap (1, 19.0f * msToSamples))) * p.hyDim;

            l[i] += wl * 0.5f * p.hyMix + side;
            r[i] += wr * 0.5f * p.hyMix - side;
            hyperWrite = (hyperWrite + 1) & (hyperSize - 1);
        }
    }

    void FxRack::process (juce::AudioBuffer<float>& buffer, const Params& p)
    {
        const int n = buffer.getNumSamples();
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);

        if (p.distOn && p.distDrive > 0.0f)
            distort (l, r, n, p);

        if (p.eqOn && (p.eqLow != 0.0f || p.eqHigh != 0.0f))
        {
            if (p.eqLow != eqState[0] || p.eqLowFreq != eqState[1] || p.eqHigh != eqState[2] || p.eqHighFreq != eqState[3])
            {
                // shortcut: coefficients are rebuilt (allocating) only when a knob moves; precompute if automation stutters
                auto lo = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, p.eqLowFreq, 0.7f, juce::Decibels::decibelsToGain (p.eqLow));
                auto hi = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, p.eqHighFreq, 0.7f, juce::Decibels::decibelsToGain (p.eqHigh));
                for (int c = 0; c < 2; ++c)
                {
                    *lowShelf[c].coefficients = *lo;
                    *highShelf[c].coefficients = *hi;
                }
                eqState[0] = p.eqLow; eqState[1] = p.eqLowFreq; eqState[2] = p.eqHigh; eqState[3] = p.eqHighFreq;
            }
            for (int i = 0; i < n; ++i)
            {
                l[i] = highShelf[0].processSample (lowShelf[0].processSample (l[i]));
                r[i] = highShelf[1].processSample (lowShelf[1].processSample (r[i]));
            }
        }

        if (p.phOn && p.phMix > 0.0f)
        {
            phaser.setRate (p.phRate);
            phaser.setDepth (p.phDepth);
            phaser.setFeedback (p.phFb);
            phaser.setMix (p.phMix);
            phaser.process (context);
        }

        if (p.chOn && p.chMix > 0.0f)
        {
            chorus.setRate (p.chRate);
            chorus.setDepth (p.chDepth);
            chorus.setMix (p.chMix * 0.6f);
            chorus.process (context);
        }

        if (p.hyOn && (p.hyMix > 0.0f || p.hyDim > 0.0f))
            hyper (l, r, n, p);

        // Ping-pong delay synced to the host tempo.
        {
            const float mix = p.dlOn ? p.dlMix : 0.0f;
            const int size = delayBuffer.getNumSamples();
            const int time = juce::jlimit (1, size - 1, (int) (p.dlBeats * 60.0 / p.bpm * sr));
            auto* dl = delayBuffer.getWritePointer (0);
            auto* dr = delayBuffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const int read = (delayWrite - time + size) % size;
                const float yl = dl[read], yr = dr[read];
                dl[delayWrite] = (l[i] + r[i]) * 0.5f * (mix > 0.0f ? 1.0f : 0.0f) + yr * p.dlFb;
                dr[delayWrite] = yl * p.dlFb;
                l[i] += yl * mix;
                r[i] += yr * mix;
                delayWrite = (delayWrite + 1) % size;
            }
        }

        {
            const float mix = p.rvOn ? p.rvMix : 0.0f;
            juce::Reverb::Parameters rp;
            rp.roomSize = p.rvSize;
            rp.damping = 0.45f;
            rp.wetLevel = mix * 0.6f;
            rp.dryLevel = 1.0f - mix * 0.35f;
            rp.width = 1.0f;
            reverb.setParameters (rp);
            reverb.processStereo (l, r, n);
        }

        if (p.ottOn && p.ottDepth > 0.0f)
            ott (l, r, n, p);
        else
            for (auto& g : ottGainDb)
                g = 0.0f;

        outGain.setTargetValue (juce::Decibels::decibelsToGain (p.masterDb));
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            l[i] *= g;
            r[i] *= g;
        }
        limiter.process (context);
    }
}
