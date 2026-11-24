#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>

// ITU-R BS.1770-4 / EBU R128 loudness: K-weighting, 400 ms momentary, 3 s short-term,
// gated integrated (abs -70, rel -10) and LRA (EBU 3342: abs -70, rel -20, 10th..95th percentile).
// Runs on the audio thread with no allocation; the gated values come from 0.1 dB histograms.
class Loudness
{
public:
    static constexpr int NB = 850; // 0.1 dB bins, -70 .. +15 LUFS

    void prepare (double sr)
    {
        // K-weighting coefficients for any sample rate (same derivation as libebur128)
        double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        Biquad pre { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                     2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        f0 = 38.13547087602444; Q = 0.5003270373238773;
        K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        a0 = 1.0 + K / Q + K * K;
        Biquad rlb { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        for (int c = 0; c < 2; ++c) { stage1[c] = pre; stage2[c] = rlb; }
        segLen = juce::jmax (1, (int) std::lround (sr * 0.1));
        resetNow();
    }

    // audio thread; numCh 1 or 2 (channel weights are 1.0 for L/R)
    void process (const float* const* ch, int numCh, int n)
    {
        if (resetRequested.exchange (false)) resetNow();
        for (int i = 0; i < n; ++i)
        {
            double s = 0;
            for (int c = 0; c < numCh; ++c)
            {
                const double y = stage2[c].run (stage1[c].run ((double) ch[c][i]));
                s += y * y;
            }
            segSum += s;
            if (++segN == segLen) endSegment();
        }
    }

    void requestReset() { resetRequested = true; }

    static double lufs (double p) { return p > 0 ? -0.691 + 10.0 * std::log10 (p) : -150.0; }

    std::atomic<float> M { -150 }, S { -150 }, I { -150 }, LRA { 0 };

    // (momentary, short-term) per 100 ms block, for the history graph; drained by the editor
    juce::AbstractFifo fifo { 4096 };
    std::array<float, 4096 * 2> fifoData {};

private:
    struct Biquad
    {
        double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };

    void resetNow()
    {
        for (int c = 0; c < 2; ++c) { stage1[c].z1 = stage1[c].z2 = stage2[c].z1 = stage2[c].z2 = 0; }
        segSum = 0; segN = 0; segCount = 0; segPos = 0; seg.fill (0);
        iPow.fill (0); iCnt.fill (0); sPow.fill (0); sCnt.fill (0);
        M = -150; S = -150; I = -150; LRA = 0;
    }

    static int bin (double l) { return juce::jlimit (0, NB - 1, (int) std::floor ((l + 70.0) * 10.0)); }
    static double centre (int b) { return -70.0 + (b + 0.5) / 10.0; }

    void endSegment()
    {
        seg[(size_t) segPos] = segSum / segLen;
        segPos = (segPos + 1) % 30; segCount = juce::jmin (segCount + 1, 30);
        segSum = 0; segN = 0;

        double m = -150, s = -150;
        if (segCount >= 4)
        {
            double p = 0; for (int k = 1; k <= 4; ++k) p += seg[(size_t) ((segPos - k + 30) % 30)];
            m = lufs (p / 4);
            if (m > -70) { iPow[(size_t) bin (m)] += p / 4; iCnt[(size_t) bin (m)] += 1; }
        }
        if (segCount >= 30)
        {
            double p = 0; for (auto v : seg) p += v;
            s = lufs (p / 30);
            if (s > -70) { sPow[(size_t) bin (s)] += p / 30; sCnt[(size_t) bin (s)] += 1; }
        }
        M = (float) m; S = (float) s;
        I = (float) integrated();
        LRA = (float) range();

        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) { fifoData[(size_t) s1 * 2] = (float) m; fifoData[(size_t) s1 * 2 + 1] = (float) s; fifo.finishedWrite (1); }
    }

    double integrated() const
    {
        double p = 0, c = 0;
        for (int b = 0; b < NB; ++b) { p += iPow[(size_t) b]; c += iCnt[(size_t) b]; }
        if (c == 0) return -150;
        const double thr = lufs (p / c) - 10.0;
        p = 0; c = 0;
        for (int b = 0; b < NB; ++b) if (centre (b) > thr) { p += iPow[(size_t) b]; c += iCnt[(size_t) b]; }
        return c > 0 ? lufs (p / c) : -150;
    }

    double range() const
    {
        double p = 0, c = 0;
        for (int b = 0; b < NB; ++b) { p += sPow[(size_t) b]; c += sCnt[(size_t) b]; }
        if (c == 0) return 0;
        const double thr = lufs (p / c) - 20.0;
        double total = 0;
        for (int b = 0; b < NB; ++b) if (centre (b) > thr) total += sCnt[(size_t) b];
        if (total < 2) return 0;
        double run = 0, lo = 0, hi = 0; bool haveLo = false;
        for (int b = 0; b < NB; ++b)
        {
            if (centre (b) <= thr || sCnt[(size_t) b] == 0) continue;
            run += sCnt[(size_t) b];
            if (! haveLo && run > 0.10 * total) { lo = centre (b); haveLo = true; }
            if (run >= 0.95 * total) { hi = centre (b); break; }
        }
        return hi - lo;
    }

    Biquad stage1[2] {}, stage2[2] {};
    double segSum = 0;
    int segN = 0, segLen = 4800, segCount = 0, segPos = 0;
    std::array<double, 30> seg {};
    std::array<double, NB> iPow {}, iCnt {}, sPow {}, sCnt {};
    std::atomic<bool> resetRequested { false };
};
