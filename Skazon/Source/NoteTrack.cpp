#include "NoteTrack.h"
#include <algorithm>
#include <numeric>

// Monophonic audio -> notes. Ported from MIDI GOD's AudioToMidi (melody / bass modes): YIN per frame,
// split into notes on silence, pitch jumps and spectral-flux onsets; times are in seconds.
namespace skazon
{
namespace
{
using Note = TrackedNote;

std::vector<float> resample (const std::vector<float>& in, double srIn, double srOut)
{
    if (std::abs (srIn - srOut) < 1.0) return in;
    std::vector<float> x (in);
    if (srOut < srIn)
        for (int pass = 0; pass < 2; ++pass) // anti-alias before dropping the rate
        {
            juce::IIRFilter f;
            f.setCoefficients (juce::IIRCoefficients::makeLowPass (srIn, 0.42 * srOut));
            f.processSamples (x.data(), (int) x.size());
        }
    const double ratio = srIn / srOut;
    const int outLen = juce::jmax (0, (int) ((double) x.size() / ratio) - 8);
    std::vector<float> out ((size_t) outLen);
    juce::LagrangeInterpolator li;
    if (outLen > 0) li.process (ratio, x.data(), out.data(), outLen);
    return out;
}

void lowPass (std::vector<float>& x, double sr, double hz)
{
    juce::IIRFilter f;
    f.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, hz));
    f.processSamples (x.data(), (int) x.size());
}

// Magnitude spectrum of a Hann window centred on sample `offset + n * hop`, for every frame n
template <typename Fn>
void forEachSpectrum (const std::vector<float>& x, int order, int hop, int offset, int numFrames, Fn&& fn)
{
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N), buf ((size_t) N * 2);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
    for (int n = 0; n < numFrames; ++n)
    {
        const int start = offset + n * hop - N / 2;
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int k = start + i;
            if (k >= 0 && k < (int) x.size()) buf[(size_t) i] = x[(size_t) k] * win[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        fn (n, buf.data(), N / 2 + 1);
    }
}

float mean (const std::vector<float>& v) { return v.empty() ? 0.0f : std::accumulate (v.begin(), v.end(), 0.0f) / (float) v.size(); }

float percentile (std::vector<float> v, float p)
{
    if (v.empty()) return 0.0f;
    const size_t k = std::min (v.size() - 1, (size_t) (p * (float) (v.size() - 1)));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

// Local maxima above a moving median + delta, at least minGap frames apart
std::vector<int> pickPeaks (const std::vector<float>& f, float delta, int minGap, int radius)
{
    std::vector<int> peaks;
    std::vector<float> win;
    int last = -minGap;
    const int n = (int) f.size();
    for (int i = 0; i < n; ++i)
    {
        bool isMax = true;
        for (int k = juce::jmax (0, i - 2); k <= juce::jmin (n - 1, i + 2) && isMax; ++k)
            if (f[(size_t) k] > f[(size_t) i]) isMax = false;
        if (! isMax || i - last < minGap) continue;
        win.assign (f.begin() + juce::jmax (0, i - radius), f.begin() + juce::jmin (n, i + radius + 1));
        std::nth_element (win.begin(), win.begin() + (long) (win.size() / 2), win.end());
        if (f[(size_t) i] > win[win.size() / 2] + delta)
        {
            peaks.push_back (i);
            last = i;
        }
    }
    return peaks;
}


//==============================================================================
// Melody and bass: YIN pitch per frame, split into notes on silence, pitch jumps and onsets
std::vector<Note> trackMonophonic (const std::vector<float>& mono, double srIn, bool bass, float sens, float threshold)
{
    const double sr = bass ? 5512.5 : 11025.0;
    auto x = resample (mono, srIn, sr);
    if (bass) { lowPass (x, sr, 700.0); lowPass (x, sr, 700.0); }

    const int W = bass ? 384 : 512, hop = bass ? 64 : 128;
    const int tauMin = (int) (sr / (bass ? 400.0 : 1500.0)), tauMax = (int) (sr / (bass ? 30.0 : 55.0));
    const int numFrames = ((int) x.size() - W - tauMax) / hop + 1;
    if (numFrames < 8) return {};

    std::vector<float> pitch ((size_t) numFrames, -1.0f), rms ((size_t) numFrames, 0.0f), d ((size_t) tauMax + 1);
    for (int n = 0; n < numFrames; ++n)
    {
        const float* s = x.data() + (size_t) n * (size_t) hop;
        double e = 0.0;
        for (int j = 0; j < W; ++j) e += s[j] * s[j];
        rms[(size_t) n] = (float) std::sqrt (e / W);
        if (e < 1.0e-9) continue;

        // cumulative-mean-normalised difference
        double running = 0.0;
        d[0] = 1.0f;
        int found = -1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < W; ++j) { const float diff = s[j] - s[j + tau]; sum += diff * diff; }
            running += sum;
            d[(size_t) tau] = running > 0.0 ? (float) (sum * tau / running) : 1.0f;
        }
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                found = tau;
                break;
            }
        if (found < 0) continue;
        const float a = d[(size_t) found - 1], b = d[(size_t) found], c = d[(size_t) found + 1];
        const float denom = a - 2.0f * b + c;
        const float tau = (float) found + (std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f);
        pitch[(size_t) n] = 69.0f + 12.0f * std::log2 ((float) sr / tau / 440.0f);
    }

    // quiet frames are not notes, however periodic they look
    const float maxRms = *std::max_element (rms.begin(), rms.end());
    if (maxRms <= 0.0f) return {};
    const float gate = maxRms * juce::Decibels::decibelsToGain (-(28.0f + 24.0f * sens));
    std::vector<bool> voiced ((size_t) numFrames);
    for (int n = 0; n < numFrames; ++n) voiced[(size_t) n] = pitch[(size_t) n] > 0.0f && rms[(size_t) n] > gate;

    // 5-frame median over voiced neighbours removes single-frame octave slips
    std::vector<float> smooth (pitch);
    for (int n = 0; n < numFrames; ++n)
    {
        if (! voiced[(size_t) n]) continue;
        float v[5]; int k = 0;
        for (int m = juce::jmax (0, n - 2); m <= juce::jmin (numFrames - 1, n + 2); ++m)
            if (voiced[(size_t) m]) v[k++] = pitch[(size_t) m];
        std::nth_element (v, v + k / 2, v + k);
        smooth[(size_t) n] = v[k / 2];
    }

    // onsets split repeated notes of the same pitch (rolling basses)
    std::vector<float> flux ((size_t) numFrames, 0.0f), prev;
    forEachSpectrum (x, bass ? 8 : 9, hop, W / 2, numFrames, [&] (int n, const float* mag, int bins)
    {
        std::vector<float> cur ((size_t) bins);
        float f = 0.0f;
        for (int k = 1; k < bins; ++k)
        {
            cur[(size_t) k] = std::log1p (10.0f * mag[k]);
            if (! prev.empty()) f += juce::jmax (0.0f, cur[(size_t) k] - prev[(size_t) k]);
        }
        flux[(size_t) n] = f;
        prev = std::move (cur);
    });
    // an onset has to bring more energy: the splatter when a note stops is not a new note
    std::vector<bool> onset ((size_t) numFrames, false);
    for (int p : pickPeaks (flux, (1.2f - 0.8f * sens) * mean (flux), 4, 8))
    {
        float before = 1.0e9f, after = 0.0f;
        for (int k = juce::jmax (0, p - 3); k < p; ++k) before = juce::jmin (before, rms[(size_t) k]);
        for (int k = p; k < juce::jmin (numFrames, p + 3); ++k) after = juce::jmax (after, rms[(size_t) k]);
        if (p < 3 || after > before * 1.25f) onset[(size_t) p] = true;
    }

    const double secPerFrame = hop / sr, beatsPerSec = 1.0; // notes in seconds
    const double frameTime0 = (W / 2.0) / sr;
    std::vector<Note> notes;
    auto close = [&] (int a, int b)
    {
        if (b - a < 3) return;
        std::vector<float> vals;
        float peak = 0.0f;
        for (int m = a; m < b; ++m)
            if (voiced[(size_t) m]) { vals.push_back (smooth[(size_t) m]); peak = juce::jmax (peak, rms[(size_t) m]); }
        if (vals.size() < 3) return;
        std::nth_element (vals.begin(), vals.begin() + (long) (vals.size() / 2), vals.end());
        int p = juce::roundToInt (vals[vals.size() / 2]);
        while (bass && p < 24) p += 12;
        for (int m = juce::jmax (0, a - 2); m <= juce::jmin (numFrames - 1, a + 1); ++m)
            if (onset[(size_t) m]) { a = m; break; }
        const double t0 = frameTime0 + (a - 0.5) * secPerFrame, t1 = frameTime0 + (b - 0.5) * secPerFrame;
        const float db = juce::Decibels::gainToDecibels (peak / maxRms);
        notes.push_back ({ juce::jlimit (0, 127, p), juce::jmax (0.0, t0) * beatsPerSec, (t1 - juce::jmax (0.0, t0)) * beatsPerSec,
                           juce::jlimit (20, 127, juce::roundToInt (127.0f + db * 2.2f)) });
    };

    int start = -1, silent = 0, change = 0;
    std::vector<float> vals;
    for (int n = 0; n < numFrames; ++n)
    {
        if (! voiced[(size_t) n])
        {
            if (start >= 0 && ++silent >= 2) { close (start, n - silent + 1); start = -1; }
            continue;
        }
        silent = 0;
        const float p = smooth[(size_t) n];
        if (start < 0) { start = n; vals.assign (1, p); change = 0; continue; }
        std::vector<float> recent (vals.end() - (long) std::min (vals.size(), (size_t) 7), vals.end());
        std::nth_element (recent.begin(), recent.begin() + (long) (recent.size() / 2), recent.end());
        const float ref = recent[recent.size() / 2];
        if (onset[(size_t) n] && n - start >= 3) { close (start, n); start = n; vals.assign (1, p); change = 0; continue; }
        if (std::abs (p - ref) > 0.8f)
        {
            if (++change >= 3) { close (start, n - 2); start = n - 2; vals = { smooth[(size_t) n - 2], smooth[(size_t) n - 1], p }; change = 0; }
            continue;
        }
        change = 0;
        vals.push_back (p);
    }
    if (start >= 0) close (start, numFrames);

    // a quiet same-pitch scrap straight after a note is its release tail: give it back to the note
    std::vector<Note> merged;
    for (auto& n : notes)
    {
        if (! merged.empty())
        {
            auto& p = merged.back();
            const double gapSec = (n.start - (p.start + p.length)) / beatsPerSec;
            if (n.pitch == p.pitch && gapSec < 0.02 && n.length / beatsPerSec < 0.12 && n.velocity < p.velocity - 6)
            {
                p.length = n.start + n.length - p.start;
                continue;
            }
        }
        merged.push_back (n);
    }
    return merged;
}

} // namespace

std::vector<TrackedNote> trackNotes (const std::vector<float>& mono, double sampleRate)
{
    // Clean leads first; then a looser YIN threshold for detuned / distorted leads; then the bass tracker.
    for (auto [bass, thr] : { std::pair<bool, float> { false, 0.22f }, { false, 0.45f }, { true, 0.45f } })
    {
        auto notes = trackMonophonic (mono, sampleRate, bass, 0.7f, thr);
        if (! notes.empty())
            return notes;
    }
    return {};
}
} // namespace skazon
