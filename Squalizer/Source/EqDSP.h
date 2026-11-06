#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

// S-QUALIZER filter maths. Every filter starts as an analogue prototype H(s); the phase mode decides
// how it becomes digital:
//   ZERO LATENCY  bilinear transform (classic digital EQ, cramps near Nyquist)
//   NATURAL       magnitude-matched biquads (Vicanek 2016): analogue curve and phase all the way up, no pre-ring
//   LINEAR        the analogue magnitude goes into one FIR (built in the processor), zero phase shift
namespace sq
{
    enum Type    { bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tiltShelf, numTypes };
    enum Channel { stereo, mid, side, left, right, numChannels };
    enum Phase   { zeroLatency, natural, linear };

    // Like a console channel EQ: LOW CUT, four EQ bands, HIGH CUT.
    constexpr int numBands = 6, lowCutBand = 0, highCutBand = 5;
    constexpr double pi = 3.14159265358979323846;

    inline bool hasGain (int type) { return type == bell || type == lowShelf || type == highShelf || type == tiltShelf; }
    inline bool isCutType (int type) { return type == lowCut || type == highCut; }

    // Cut slopes: 6, 12, 24, 36, 48, 72, 96 dB/oct and BRICK (ideal in LINEAR, 96 dB otherwise).
    constexpr int numSlopes = 8, brickSlope = 7, maxSections = 8;
    inline int cutOrder (int slope) { static constexpr int orders[numSlopes] { 1, 2, 4, 6, 8, 12, 16, 16 }; return orders[std::clamp (slope, 0, numSlopes - 1)]; }
    inline int sectionsFor (int type, int slope) { return isCutType (type) ? (cutOrder (slope) + 1) / 2 : 1; }

    struct BandSettings
    {
        bool on = false;
        int type = bell;
        double freq = 1000.0, gain = 0.0, q = 0.71;
        int channel = stereo, slope = 2;
        double dyn = 0.0, threshold = -24.0;
        bool external = false;   // dynamics follow the sidechain input instead of the band's own signal
    };

    // H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0), s normalised to the corner frequency.
    struct Analog { double n0, n1, n2, d0, d1, d2; };

    inline double analogMagnitude (const Analog& a, double omega)   // omega = f / f0
    {
        const double w2 = omega * omega;
        const double nr = a.n0 - a.n2 * w2, ni = a.n1 * omega, dr = a.d0 - a.d2 * w2, di = a.d1 * omega;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di + 1e-300));
    }

    // Section k of a band. Cuts are Butterworth cascades of the slope's order (odd orders start with a first-order
    // section); Q sets the resonance of the sharpest section (0.71 = flat). Tilt is a high shelf re-centred so it
    // goes -gain/2 below the corner and +gain/2 above.
    inline Analog prototypeFor (int type, double gainDb, double q, int slope, int k)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        switch (type)
        {
            case bell:      return { 1, A / q, 1, 1, 1 / (A * q), 1 };
            case lowShelf:  { const double s = std::sqrt (A) / q; return { A * A, A * s, A, 1, s, A }; }
            case highShelf: { const double s = std::sqrt (A) / q; return { A, A * s, A * A, A, s, 1 }; }
            case tiltShelf: { const double s = std::sqrt (A) / q; return { 1, s, A, A, s, 1 }; }
            case lowCut:
            case highCut:
            {
                const int order = cutOrder (slope), sections = (order + 1) / 2;
                const bool odd = (order % 2) == 1;
                if (odd && k == 0)
                    return type == lowCut ? Analog { 0, 1, 0, 1, 1, 0 } : Analog { 1, 0, 0, 1, 1, 0 };
                const int j = k - (odd ? 1 : 0);
                double qk = 1.0 / (2.0 * std::cos ((2 * j + 1) * pi / (2.0 * order)));
                if (k == sections - 1)
                    qk *= q / 0.7071;
                return type == lowCut ? Analog { 0, 0, 1, 1, 1 / qk, 1 } : Analog { 1, 0, 0, 1, 1 / qk, 1 };
            }
            case notch:     return { 1, 0, 1, 1, 1 / q, 1 };
            default:        return { 0, 1 / q, 0, 1, 1 / q, 1 };   // band pass, 0 dB peak
        }
    }

    inline Analog prototype (int type, double gainDb, double q) { return prototypeFor (type, gainDb, q, 2, 0); }

    // BRICK in LINEAR mode: an ideal cut with a 1/12-octave raised-cosine edge.
    inline double brickMagnitude (int type, double f, double fc)
    {
        const double x = std::log2 (std::max (f, 1e-3) / fc) * 12.0;   // in 1/12 octaves
        const double pass = x <= -0.5 ? 0.0 : (x >= 0.5 ? 1.0 : 0.5 - 0.5 * std::cos (pi * (x + 0.5)));
        return type == lowCut ? pass : 1.0 - pass;
    }

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

        double magnitude (double w) const   // |H(e^jw)|
        {
            const auto z1 = std::polar (1.0, -w), z2 = z1 * z1;
            return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
        }
    };

    // Bilinear transform pre-warped at the corner: identical to the RBJ cookbook filters.
    inline Biquad bilinear (const Analog& a, double w0)
    {
        const double K = std::tan (w0 * 0.5), K2 = K * K;
        const double a0 = a.d2 + a.d1 * K + a.d0 * K2;
        return { (a.n2 + a.n1 * K + a.n0 * K2) / a0, 2 * (a.n0 * K2 - a.n2) / a0, (a.n2 - a.n1 * K + a.n0 * K2) / a0,
                 2 * (a.d0 * K2 - a.d2) / a0, (a.d2 - a.d1 * K + a.d0 * K2) / a0 };
    }

    // Poles by impulse invariance, zeros chosen so |H| equals the analogue curve at DC, Nyquist and the corner.
    inline Biquad matched (const Analog& a, double w0)
    {
        if (a.d2 == 0.0)
            return bilinear (a, w0);   // first-order section: the bilinear version is already close
        Biquad q;
        const double disc = a.d1 * a.d1 - 4 * a.d2 * a.d0;
        const double poleRadius = std::sqrt (a.d0 / a.d2) * w0;
        if (poleRadius > 0.9 * pi)
            return bilinear (a, w0);   // pole would fold past Nyquist (deep shelf cuts up high)
        if (disc < 0)
        {
            const double re = -a.d1 / (2 * a.d2), im = std::sqrt (-disc) / (2 * a.d2);
            q.a1 = -2 * std::exp (re * w0) * std::cos (im * w0);
            q.a2 = std::exp (2 * re * w0);
        }
        else
        {
            const double r1 = (-a.d1 + std::sqrt (disc)) / (2 * a.d2), r2 = (-a.d1 - std::sqrt (disc)) / (2 * a.d2);
            q.a1 = -(std::exp (r1 * w0) + std::exp (r2 * w0));
            q.a2 = std::exp ((r1 + r2) * w0);
        }

        // |B(e^jw)|^2 = B0 phi0 + B1 phi1 + B2 phi2 with phi0 = cos^2(w/2), phi1 = sin^2(w/2), phi2 = 4 phi0 phi1.
        auto sq2 = [] (double v) { return v * v; };
        const double A0 = sq2 (1 + q.a1 + q.a2), A1 = sq2 (1 - q.a1 + q.a2), A2 = -4 * q.a2;
        const double wm = std::min (w0, 0.75 * pi);
        const double p1 = sq2 (std::sin (wm * 0.5)), p0 = 1 - p1, p2 = 4 * p0 * p1;
        const double B0 = sq2 (analogMagnitude (a, 0.0)) * A0;
        const double B1 = sq2 (analogMagnitude (a, pi / w0)) * A1;
        const double B2 = (sq2 (analogMagnitude (a, wm / w0)) * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;

        const double s0 = std::sqrt (B0), s1 = std::sqrt (B1), W = 0.5 * (s0 + s1);
        q.b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2)));
        q.b1 = 0.5 * (s0 - s1);
        q.b2 = q.b0 > 1e-300 ? -B2 / (4 * q.b0) : 0.0;
        return q;
    }

    inline double cornerOmega (double freq, double sampleRate)
    {
        return 2 * pi * std::min (freq, 0.497 * sampleRate) / sampleRate;
    }

    inline Biquad design (const Analog& a, double freq, double sampleRate, int phase)
    {
        const double w0 = cornerOmega (freq, sampleRate);
        return phase == zeroLatency ? bilinear (a, w0) : matched (a, w0);
    }

    // What one band does to frequency f, as the phase mode really renders it.
    inline double bandMagnitude (const BandSettings& b, double f, double sampleRate, int phase)
    {
        if (phase == linear && isCutType (b.type) && b.slope == brickSlope)
            return brickMagnitude (b.type, f, b.freq);
        const int n = sectionsFor (b.type, b.slope);
        double m = 1.0;
        for (int k = 0; k < n; ++k)
        {
            const auto a = prototypeFor (b.type, b.gain, b.q, b.slope, k);
            m *= phase == linear ? analogMagnitude (a, f / b.freq)
                                 : design (a, b.freq, sampleRate, phase).magnitude (2 * pi * f / sampleRate);
        }
        return m;
    }

    // A band's response in dB at many frequencies, designing its filters once (for drawing).
    inline void bandCurveDb (const BandSettings& b, const double* freqs, int count, double sampleRate, int phase, float* out)
    {
        if (phase == linear && isCutType (b.type) && b.slope == brickSlope)
        {
            for (int i = 0; i < count; ++i)
                out[i] = (float) (20.0 * std::log10 (brickMagnitude (b.type, freqs[i], b.freq) + 1e-12));
            return;
        }
        const int n = sectionsFor (b.type, b.slope);
        std::array<Analog, maxSections> a {};
        std::array<Biquad, maxSections> c {};
        for (int k = 0; k < n; ++k)
        {
            a[(size_t) k] = prototypeFor (b.type, b.gain, b.q, b.slope, k);
            if (phase != linear)
                c[(size_t) k] = design (a[(size_t) k], b.freq, sampleRate, phase);
        }
        for (int i = 0; i < count; ++i)
        {
            double m = 1.0;
            for (int k = 0; k < n; ++k)
                m *= phase == linear ? analogMagnitude (a[(size_t) k], freqs[i] / b.freq) : c[(size_t) k].magnitude (2 * pi * freqs[i] / sampleRate);
            out[i] = (float) (20.0 * std::log10 (m + 1e-12));
        }
    }

    struct Filter
    {
        Biquad c;
        double s1 = 0, s2 = 0;

        double process (double x)   // transposed direct form II
        {
            const double y = c.b0 * x + s1;
            s1 = c.b1 * x - c.a1 * y + s2;
            s2 = c.b2 * x - c.a2 * y;
            return y;
        }
        void reset() { s1 = s2 = 0; }
    };

    // One EQ band: up to 4 cascaded sections per lane, a dynamic gain stage and its band-pass detector.
    // Mid/side/left/right routing happens per band, so bands with different routings can be mixed freely.
    class Band
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            attack = 1.0 - std::exp (-1.0 / (0.003 * sr));
            release = 1.0 - std::exp (-1.0 / (0.09 * sr));
            fadeStep = chunk / (0.02 * sr);
            reset();
        }

        void reset()
        {
            for (auto& lane : filters)
                for (auto& f : lane)
                    f.reset();
            for (auto& f : dynFilters) f.reset();
            for (auto& f : detectors) f.reset();
            env = 0;
            delta = 0;
            fade = 0;
            primed = false;
        }

        // In LINEAR mode the static curve lives in the FIR, so only the dynamic stage runs here.
        // scL/scR: the sidechain input (or nullptr); used by the detector when the band is set to EXTERNAL.
        void process (float* l, float* r, int n, const BandSettings& s, int phase, const float* scL = nullptr, const float* scR = nullptr)
        {
            const double fadeTarget = s.on ? 1.0 : 0.0;
            if (fade <= 0.0 && fadeTarget <= 0.0)
            {
                if (primed)
                    reset();
                liveDyn = 0.0f;
                return;
            }

            if (! primed || s.type != type || s.slope != slope || s.channel != channel || phase != lastPhase)
            {
                if (s.type != type || s.slope != slope || s.channel != channel)
                    for (auto& lane : filters)
                        for (auto& f : lane)
                            f.reset();
                type = s.type; slope = s.slope; channel = s.channel; lastPhase = phase;
                freq = s.freq; gain = s.gain; q = s.q;
                primed = true;
                updateStatic();
            }

            const bool staticHere = phase != linear;
            const bool dynamic = hasGain (type) && std::abs (s.dyn) > 0.05;
            const bool external = dynamic && s.external && scL != nullptr && scR != nullptr;
            if (! dynamic && delta != 0.0)
            {
                delta = 0.0;
                for (auto& f : dynFilters) f.reset();
            }
            const int sections = sectionsFor (type, slope);

            for (int start = 0; start < n; start += chunk)
            {
                const int len = std::min (chunk, n - start);

                // Glide towards the knob values; coefficients follow every chunk while moving.
                const double nf = freq * std::pow (s.freq / freq, glide), ng = gain + (s.gain - gain) * glide, nq = q * std::pow (s.q / q, glide);
                if (std::abs (nf - freq) > 1e-6 * freq || std::abs (ng - gain) > 1e-5 || std::abs (nq - q) > 1e-6 * q)
                {
                    freq = std::abs (s.freq - nf) < 1e-4 * s.freq ? s.freq : nf;
                    gain = std::abs (s.gain - ng) < 1e-4 ? s.gain : ng;
                    q = std::abs (s.q - nq) < 1e-4 * s.q ? s.q : nq;
                    updateStatic();
                }
                fade = fadeTarget > fade ? std::min (fadeTarget, fade + fadeStep) : std::max (fadeTarget, fade - fadeStep);

                double peak = 0.0, key = 0.0;
                auto lane = [&] (int c, double x)
                {
                    double y = x;
                    if (staticHere)
                        for (int k = 0; k < sections; ++k)
                            y = filters[(size_t) c][(size_t) k].process (y);
                    if (dynamic)
                    {
                        const double d = detectors[(size_t) c].process (external ? key : x);
                        peak = std::max (peak, d * d);
                        y = dynFilters[(size_t) c].process (y);
                    }
                    return x + fade * (y - x);
                };

                for (int i = start; i < start + len; ++i)
                {
                    peak = 0.0;
                    if (external)
                        key = 0.5 * (scL[i] + scR[i]);
                    double L = l[i], R = r[i];
                    switch (channel)
                    {
                        case stereo: L = lane (0, L); R = lane (1, R); break;
                        case mid:    { const double m = lane (0, 0.5 * (L + R)), sd = 0.5 * (L - R); L = m + sd; R = m - sd; break; }
                        case side:   { const double m = 0.5 * (L + R), sd = lane (0, 0.5 * (L - R)); L = m + sd; R = m - sd; break; }
                        case left:   L = lane (0, L); break;
                        default:     R = lane (0, R); break;
                    }
                    l[i] = (float) L;
                    r[i] = (float) R;
                    if (dynamic)
                        env += (peak > env ? attack : release) * (peak - env);
                }

                if (dynamic)
                {
                    // Above the threshold the band moves 0.6 dB per dB of overshoot (about 2.5:1), up to the DYN range.
                    const double levelDb = 10.0 * std::log10 (2.0 * env + 1e-12);
                    const double over = std::max (0.0, levelDb - s.threshold);
                    const double target = std::copysign (std::min (std::abs (s.dyn), over * 0.6), s.dyn);
                    if (std::abs (target - delta) > 0.01)
                    {
                        delta = target;
                        updateDynamic();
                    }
                }
            }
            liveDyn = (float) delta;
        }

        float liveDyn = 0.0f;   // current dynamic gain in dB, for the display

    private:
        void updateStatic()
        {
            const int sections = sectionsFor (type, slope);
            for (int k = 0; k < sections; ++k)
            {
                const auto c = design (prototypeFor (type, gain, q, slope, k), freq, sr, lastPhase == zeroLatency ? zeroLatency : natural);
                for (auto& lane : filters)
                    lane[(size_t) k].c = c;
            }
            const auto bp = design (prototype (bandPass, 0.0, q), freq, sr, zeroLatency);
            for (auto& d : detectors) d.c = bp;
            updateDynamic();
        }

        void updateDynamic()
        {
            const auto c = design (prototype (type, delta, q), freq, sr, lastPhase == zeroLatency ? zeroLatency : natural);
            for (auto& f : dynFilters) f.c = c;
        }

        static constexpr int chunk = 16;
        static constexpr double glide = 0.12;
        double sr = 48000.0, attack = 0.01, release = 0.001, fadeStep = 0.02;
        std::array<std::array<Filter, maxSections>, 2> filters;
        std::array<Filter, 2> dynFilters, detectors;
        double env = 0, delta = 0, fade = 0;
        double freq = 1000, gain = 0, q = 0.71;
        int type = bell, slope = 1, channel = stereo, lastPhase = zeroLatency;
        bool primed = false;
    };

    //==========================================================================
    // Passive program EQ in the classic tube style: separate low boost and low attenuation (use both at once
    // for the famous tight-sub trick), a resonant high boost with bandwidth, a high shelf cut, then a tube stage.
    struct TubeSettings
    {
        int lowFreq = 2;          // index into lowFreqs
        double lowBoost = 0, lowAtten = 0;
        int highFreq = 4;
        double highBoost = 0, bandwidth = 5;
        int attenFreq = 1;
        double highAtten = 0, drive = 20;
    };

    inline constexpr double lowFreqs[]   { 20, 30, 60, 100 };
    inline constexpr double highFreqs[]  { 3000, 4000, 5000, 8000, 10000, 12000, 16000 };
    inline constexpr double attenFreqs[] { 5000, 10000, 20000 };

    // The four tone stages as analogue prototypes plus their corner frequencies.
    inline std::array<std::pair<Analog, double>, 4> tubeStages (const TubeSettings& t)
    {
        const double lf = lowFreqs[t.lowFreq];
        const double bwQ = 2.5 * std::pow (0.5 / 2.5, t.bandwidth / 10.0);   // SHARP 2.5 .. BROAD 0.5
        // Corners fitted so boost 10 + atten 10 gives about +4.5 dB of sub and a -3 dB dip near 3.5x the frequency.
        return { { { prototype (lowShelf, t.lowBoost * 1.35, 0.7), lf * 1.6 },
                   { prototype (lowShelf, -t.lowAtten * 0.8, 0.5), lf * 3.2 },
                   { prototype (bell, t.highBoost * 1.8, bwQ), highFreqs[t.highFreq] },
                   { prototype (highShelf, -t.highAtten * 2.0, 0.5), attenFreqs[t.attenFreq] } } };
    }

    inline double tubeMagnitude (const TubeSettings& t, double f, double sampleRate)
    {
        double m = 1.0;
        for (const auto& [a, fc] : tubeStages (t))
            m *= design (a, fc, sampleRate, natural).magnitude (2 * pi * f / sampleRate);
        return m;
    }

    inline void tubeCurveDb (const TubeSettings& t, const double* freqs, int count, double sampleRate, float* out)
    {
        std::array<Biquad, 4> c {};
        const auto list = tubeStages (t);
        for (size_t k = 0; k < 4; ++k)
            c[k] = design (list[k].first, list[k].second, sampleRate, natural);
        for (int i = 0; i < count; ++i)
        {
            double m = 1.0;
            for (const auto& s : c)
                m *= s.magnitude (2 * pi * freqs[i] / sampleRate);
            out[i] = (float) (20.0 * std::log10 (m + 1e-12));
        }
    }

    class TubeEq
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            fadeStep = 1.0 / (0.02 * sr);
            for (auto& lane : stages)
                for (auto& f : lane)
                    f.reset();
            dcIn = dcOut = { 0, 0 };
            fade = 0;
            current = {};
            update();
        }

        void process (float* l, float* r, int n, const TubeSettings& t, bool on)
        {
            const double fadeTarget = on ? 1.0 : 0.0;
            if (fade <= 0.0 && ! on)
                return;

            current.lowFreq = t.lowFreq; current.highFreq = t.highFreq; current.attenFreq = t.attenFreq;
            auto glideTo = [] (double& v, double target) { const double nv = v + (target - v) * 0.2; const bool moved = std::abs (nv - v) > 1e-4; v = moved ? nv : target; return moved; };
            bool moved = false;
            moved |= glideTo (current.lowBoost, t.lowBoost);
            moved |= glideTo (current.lowAtten, t.lowAtten);
            moved |= glideTo (current.highBoost, t.highBoost);
            moved |= glideTo (current.bandwidth, t.bandwidth);
            moved |= glideTo (current.highAtten, t.highAtten);
            if (moved || t.lowFreq != shownLow || t.highFreq != shownHigh || t.attenFreq != shownAtten)
                update();

            // Small-signal gain of the tube stays at unity; drive only adds 2nd/3rd harmonics as levels rise.
            const double d = t.drive / 100.0, pre = 1.0 + 4.0 * d, bias = 0.25 * d;
            const double tb = std::tanh (bias), norm = 1.0 / (pre * (1.0 - tb * tb));
            float* chans[] { l, r };
            for (int c = 0; c < 2; ++c)
            {
                double f = fade;
                for (int i = 0; i < n; ++i)
                {
                    f = fadeTarget > f ? std::min (fadeTarget, f + fadeStep) : std::max (fadeTarget, f - fadeStep);
                    const double x = chans[c][i];
                    double y = x;
                    for (auto& s : stages[(size_t) c])
                        y = s.process (y);
                    if (d > 0.001)
                    {
                        y = (std::tanh (pre * y + bias) - tb) * norm;
                        const double dc = y - dcIn[(size_t) c] + 0.9995 * dcOut[(size_t) c];
                        dcIn[(size_t) c] = y;
                        dcOut[(size_t) c] = dc;
                        y = dc;
                    }
                    chans[c][i] = (float) (x + f * (y - x));
                }
                if (c == 1)
                    fade = f;
            }
        }

    private:
        void update()
        {
            shownLow = current.lowFreq; shownHigh = current.highFreq; shownAtten = current.attenFreq;
            const auto list = tubeStages (current);
            for (size_t k = 0; k < 4; ++k)
            {
                const auto c = design (list[k].first, list[k].second, sr, natural);
                for (auto& lane : stages)
                    lane[k].c = c;
            }
        }

        double sr = 48000.0, fade = 0, fadeStep = 0.001;
        TubeSettings current;
        int shownLow = -1, shownHigh = -1, shownAtten = -1;
        std::array<std::array<Filter, 4>, 2> stages;
        std::array<double, 2> dcIn {}, dcOut {};
    };
}
