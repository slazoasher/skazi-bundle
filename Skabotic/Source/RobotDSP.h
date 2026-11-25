#pragma once

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>

// SKABOTIC building blocks. Everything here is mono and allocation-free after prepare().
namespace bot
{
    constexpr double pi = 3.14159265358979323846;

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void bandpass (double sr, double f, double q)
        {
            const double w = 2.0 * pi * f / sr, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
            b0 = (float) (alpha / a0); b1 = 0; b2 = (float) (-alpha / a0);
            a1 = (float) (-2.0 * std::cos (w) / a0); a2 = (float) ((1.0 - alpha) / a0);
        }

        void lowpass (double sr, double f, double q = 0.7071)
        {
            const double w = 2.0 * pi * f / sr, alpha = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + alpha;
            b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
            a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - alpha) / a0);
        }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0; }
    };

    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // Carrier oscillator bank: up to 8 voices of one waveform.
    struct Carrier
    {
        enum Type { saw, square, eightBit, chord, buzz, noise };
        static constexpr int maxVoices = 8;

        std::array<float, maxVoices> phase {};
        uint32_t seed = 22222;

        float white() noexcept
        {
            seed = seed * 1664525u + 1013904223u;
            return (float) (int32_t) seed * (1.0f / 2147483648.0f);
        }

        float voice (int type, int v, float freq, float sr) noexcept
        {
            const float dt = std::min (0.45f, freq / sr);
            float& p = phase[(size_t) v];
            const float t = p;
            p += dt;
            if (p >= 1.0f) p -= 1.0f;

            switch (type)
            {
                case square:   return (t < 0.5f ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (std::fmod (t + 0.5f, 1.0f), dt);
                case eightBit: return std::floor ((t < 0.25f ? 1.0f : -0.33f) * 7.0f) / 7.0f; // aliasing on purpose
                case buzz:     return t < 0.08f ? 3.0f : -0.26f;                              // narrow pulse train
                default:       return 2.0f * t - 1.0f - polyBlep (t, dt);                     // saw (chord uses saws too)
            }
        }

        // freqs: the voice frequencies; chord adds a fifth and an octave above each.
        float process (int type, const float* freqs, int numFreqs, float sr) noexcept
        {
            if (type == noise)
                return white();

            float out = 0.0f;
            int v = 0;
            for (int i = 0; i < numFreqs && v < maxVoices; ++i)
            {
                out += voice (type, v++, freqs[i], sr);
                if (type == chord && v + 1 < maxVoices)
                {
                    out += 0.7f * voice (type, v++, freqs[i] * 1.4983f, sr);
                    out += 0.5f * voice (type, v++, freqs[i] * 2.0f, sr);
                }
            }
            return out / std::sqrt ((float) std::max (1, v));
        }
    };

    // Channel vocoder: the voice (modulator) shapes the carrier through numBands envelope-followed band-pass pairs.
    struct Vocoder
    {
        static constexpr int numBands = 24;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            for (int i = 0; i < numBands; ++i)
            {
                centre[(size_t) i] = std::min (80.0 * std::pow (11000.0 / 80.0, i / (double) (numBands - 1)), sr * 0.45);
                mod[(size_t) i][0].bandpass (sr, centre[(size_t) i], 6.0);
                mod[(size_t) i][1].bandpass (sr, centre[(size_t) i], 6.0);
                env[(size_t) i] = 0.0f;
                for (auto& f : mod[(size_t) i]) f.reset();
                for (auto& f : car[(size_t) i]) f.reset();
            }
            appliedFormant = -100.0f;
            setFormant (0.0f);
            setRelease (20.0f);
        }

        void setFormant (float semitones)
        {
            if (std::abs (semitones - appliedFormant) < 0.01f)
                return;
            appliedFormant = semitones;
            const double ratio = std::pow (2.0, semitones / 12.0);
            for (int i = 0; i < numBands; ++i)
            {
                const double f = std::clamp (centre[(size_t) i] * ratio, 30.0, sr * 0.45);
                car[(size_t) i][0].bandpass (sr, f, 6.0);
                car[(size_t) i][1].bandpass (sr, f, 6.0);
            }
        }

        void setRelease (float ms)
        {
            attack = 1.0f - (float) std::exp (-1.0 / (0.0015 * sr));
            release = 1.0f - (float) std::exp (-1.0 / (ms * 0.001 * sr));
        }

        // hissNoise feeds the upper bands so consonants (s, t, ch) stay readable.
        float process (float modulator, float carrier, float hissNoise) noexcept
        {
            float out = 0.0f;
            for (int i = 0; i < numBands; ++i)
            {
                auto& m = mod[(size_t) i];
                const float a = std::abs (m[1].process (m[0].process (modulator)));
                float& e = env[(size_t) i];
                e += (a > e ? attack : release) * (a - e);

                const float hi = (float) i / (float) (numBands - 1);
                const float in = carrier + hissNoise * hi * hi * hi;
                auto& c = car[(size_t) i];
                out += c[1].process (c[0].process (in)) * e;
            }
            return out;
        }

        double sr = 44100.0;
        std::array<double, numBands> centre {};
        std::array<std::array<Biquad, 2>, numBands> mod {}, car {};
        std::array<float, numBands> env {};
        float attack = 0.1f, release = 0.01f, appliedFormant = -100.0f;
    };

    // YIN pitch detector on a ~12 kHz decimated copy of the voice.
    struct PitchTracker
    {
        void prepare (double sampleRate)
        {
            decim = std::max (1, (int) std::lround (sampleRate / 12000.0));
            dsr = sampleRate / decim;
            lp1.lowpass (sampleRate, dsr * 0.4); lp2.lowpass (sampleRate, dsr * 0.4);
            lp1.reset(); lp2.reset();
            buf.fill (0.0f);
            pos = count = hop = 0;
            freq = 0.0f; voiced = false;
        }

        void push (float x) noexcept
        {
            const float y = lp2.process (lp1.process (x));
            if (++count < decim)
                return;
            count = 0;
            buf[(size_t) pos] = y;
            pos = (pos + 1) & (size - 1);
            if (++hop >= 128) { hop = 0; analyse(); }
        }

        float freq = 0.0f; // last voiced pitch in Hz, 0 before the first one
        bool voiced = false;

    private:
        static constexpr int size = 1024, window = 320;

        void analyse() noexcept
        {
            const int maxTau = std::min ((int) (dsr / 60.0), size - window - 2);
            const int minTau = std::max (2, (int) (dsr / 1000.0));
            const int n = window + maxTau + 1;
            float energy = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                frame[(size_t) i] = buf[(size_t) ((pos - n + i) & (size - 1))];
                energy += frame[(size_t) i] * frame[(size_t) i];
            }
            if (energy / (float) n < 1e-7f) { voiced = false; return; }

            float running = 0.0f;
            d[0] = 1.0f;
            for (int tau = 1; tau <= maxTau; ++tau)
            {
                float sum = 0.0f;
                for (int j = 0; j < window; ++j)
                {
                    const float diff = frame[(size_t) j] - frame[(size_t) (j + tau)];
                    sum += diff * diff;
                }
                running += sum;
                d[(size_t) tau] = running > 0.0f ? sum * (float) tau / running : 1.0f;
            }

            int best = -1;
            for (int tau = minTau; tau < maxTau; ++tau)
                if (d[(size_t) tau] < 0.15f)
                {
                    while (tau + 1 < maxTau && d[(size_t) (tau + 1)] < d[(size_t) tau]) ++tau;
                    best = tau;
                    break;
                }
            if (best < 0)
            {
                best = minTau;
                for (int tau = minTau; tau < maxTau; ++tau)
                    if (d[(size_t) tau] < d[(size_t) best]) best = tau;
                if (d[(size_t) best] > 0.35f) { voiced = false; return; }
            }

            const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const float denom = a - 2.0f * b + c;
            const float shift = std::abs (denom) > 1e-9f ? std::clamp (0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
            freq = (float) (dsr / ((float) best + shift));
            voiced = true;
        }

        int decim = 4, pos = 0, count = 0, hop = 0;
        double dsr = 12000.0;
        Biquad lp1, lp2;
        std::array<float, size> buf {};
        std::array<float, size> frame {};
        std::array<float, size> d {};
    };

    // Two-head delay pitch shifter (the classic "deep robot / chipmunk" sound).
    struct PitchShifter
    {
        void prepare (double sampleRate)
        {
            buf.assign (1 << 14, 0.0f);
            window = (float) (0.05 * sampleRate);
            pos = 0; phase = 0.0f;
        }

        float process (float x, float ratio) noexcept
        {
            const int mask = (int) buf.size() - 1;
            buf[(size_t) pos] = x;
            phase += (1.0f - ratio) / window;
            phase -= std::floor (phase);

            auto tap = [&] (float p)
            {
                const float delay = 1.0f + p * window;
                const float rp = (float) pos - delay;
                const int i0 = (int) std::floor (rp);
                const float frac = rp - (float) i0;
                const float s0 = buf[(size_t) (i0 & mask)], s1 = buf[(size_t) ((i0 + 1) & mask)];
                return (s0 + frac * (s1 - s0)) * (float) std::sin (pi * p);
            };
            const float p2 = phase + 0.5f >= 1.0f ? phase - 0.5f : phase + 0.5f;
            const float y = tap (phase) + tap (p2);
            pos = (pos + 1) & mask;
            return y;
        }

        std::vector<float> buf;
        float window = 2400.0f, phase = 0.0f;
        int pos = 0;
    };

    // 90-degree phase splitter (two allpass chains), used by the frequency shifter.
    struct Hilbert
    {
        struct Allpass
        {
            float a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            float process (float x) noexcept
            {
                const float y = a2 * (x + y2) - x2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                return y;
            }
        };

        Allpass p1[4] { { 0.47940086558884f }, { 0.87621849353931f }, { 0.97659758950819f }, { 0.99749925593555f } };
        Allpass p2[4] { { 0.16175849836770f }, { 0.73302893234149f }, { 0.94534970032911f }, { 0.99059915668453f } };
        float delayed = 0.0f;

        void process (float x, float& re, float& im) noexcept
        {
            float u = x, v = x;
            for (int k = 0; k < 4; ++k) { u = p1[k].process (u); v = p2[k].process (v); }
            re = delayed;
            delayed = u;
            im = v;
        }

        void reset() noexcept { *this = Hilbert(); }
    };

    // Four modulated delay voices, two per side: one robot becomes an army.
    struct Army
    {
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            buf.assign (1 << 14, 0.0f);
            pos = 0;
            lfo = { 0.0f, 0.25f, 0.5f, 0.75f };
        }

        void process (float x, float& left, float& right) noexcept
        {
            static constexpr float baseMs[4] { 9.0f, 13.0f, 17.0f, 23.0f };
            static constexpr float rateHz[4] { 0.31f, 0.47f, 0.73f, 0.89f };
            const int mask = (int) buf.size() - 1;
            buf[(size_t) pos] = x;
            float v[4];
            for (int k = 0; k < 4; ++k)
            {
                lfo[(size_t) k] += rateHz[k] / sr;
                lfo[(size_t) k] -= std::floor (lfo[(size_t) k]);
                const float d = (baseMs[k] + 3.0f * (float) std::sin (2.0 * pi * lfo[(size_t) k])) * 0.001f * sr;
                const float rp = (float) pos - d;
                const int i0 = (int) std::floor (rp);
                const float frac = rp - (float) i0;
                const float s0 = buf[(size_t) (i0 & mask)], s1 = buf[(size_t) ((i0 + 1) & mask)];
                v[k] = s0 + frac * (s1 - s0);
            }
            pos = (pos + 1) & mask;
            left = 0.5f * (v[0] + v[2]);
            right = 0.5f * (v[1] + v[3]);
        }

        std::vector<float> buf;
        std::array<float, 4> lfo {};
        float sr = 44100.0f;
        int pos = 0;
    };

    // Short feedback comb: the "metal tube / tin can" ring.
    struct Comb
    {
        void prepare (double sampleRate)
        {
            buf.assign ((size_t) (sampleRate / 30.0) + 4, 0.0f);
            pos = 0;
        }

        float process (float x, float delaySamples, float feedback) noexcept
        {
            const int n = (int) buf.size();
            const float rp = (float) pos - std::clamp (delaySamples, 1.0f, (float) (n - 2));
            int i0 = (int) std::floor (rp);
            const float frac = rp - (float) i0;
            i0 = (i0 % n + n) % n;
            const float s0 = buf[(size_t) i0], s1 = buf[(size_t) ((i0 + 1) % n)];
            const float y = x + feedback * (s0 + frac * (s1 - s0));
            buf[(size_t) pos] = std::tanh (y); // tanh keeps high feedback from blowing up
            pos = (pos + 1) % n;
            return y;
        }

        std::vector<float> buf;
        int pos = 0;
    };
}
