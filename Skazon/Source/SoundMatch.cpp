#include "SoundMatch.h"

namespace skazon
{
    namespace
    {
        constexpr int fftOrder = 13, fftSize = 1 << fftOrder, window = 2048, cyclesPerFrame = 4;

        // YIN pitch detector; the lag correlation comes from one FFT per hop.
        class Yin
        {
        public:
            explicit Yin (double rate)
                : sr (rate), fft (fftOrder), a ((size_t) fftSize * 2), b ((size_t) fftSize * 2), d ((size_t) window + 1)
            {
                maxLag = juce::jmin (window, (int) (sr / 45.0));
                minLag = juce::jmax (2, (int) (sr / 2500.0));
            }

            int span() const { return window + maxLag; }

            // x[start .. start + span()) must exist. Returns { Hz, clarity } (clarity: 0 = perfectly periodic).
            std::pair<float, float> detect (const std::vector<float>& x, int start)
            {
                std::fill (a.begin(), a.end(), 0.0f);
                std::fill (b.begin(), b.end(), 0.0f);
                double e0 = 0;
                for (int j = 0; j < window; ++j)
                {
                    a[(size_t) j] = x[(size_t) (start + j)];
                    e0 += (double) a[(size_t) j] * a[(size_t) j];
                }
                for (int j = 0; j < span(); ++j)
                    b[(size_t) j] = x[(size_t) (start + j)];
                if (e0 < 1.0e-9)
                    return { 0.0f, 1.0f };

                fft.performRealOnlyForwardTransform (a.data());
                fft.performRealOnlyForwardTransform (b.data());
                for (int k = 0; k < fftSize; ++k) // conj(A) * B -> cross-correlation
                {
                    const float ar = a[(size_t) k * 2], ai = a[(size_t) k * 2 + 1], br = b[(size_t) k * 2], bi = b[(size_t) k * 2 + 1];
                    b[(size_t) k * 2] = ar * br + ai * bi;
                    b[(size_t) k * 2 + 1] = ar * bi - ai * br;
                }
                fft.performRealOnlyInverseTransform (b.data());
                const double scale = e0 / juce::jmax (1.0e-12, (double) b[0]); // whatever scaling the FFT backend uses

                // Difference function and its cumulative-mean normalisation.
                double eTau = e0, running = 0;
                d[0] = 1.0f;
                int best = -1;
                for (int tau = 1; tau <= maxLag; ++tau)
                {
                    const double out = x[(size_t) (start + tau - 1)], in = x[(size_t) (start + tau - 1 + window)];
                    eTau += in * in - out * out;
                    const double diff = juce::jmax (0.0, e0 + eTau - 2.0 * b[(size_t) tau] * scale);
                    running += diff;
                    d[(size_t) tau] = (float) (running > 0 ? diff * tau / running : 1.0);
                }
                for (int tau = minLag; tau < maxLag; ++tau)
                    if (d[(size_t) tau] < 0.15f)
                    {
                        while (tau + 1 < maxLag && d[(size_t) tau + 1] < d[(size_t) tau])
                            ++tau;
                        best = tau;
                        break;
                    }
                if (best < 0)
                {
                    best = minLag;
                    for (int tau = minLag; tau < maxLag; ++tau)
                        if (d[(size_t) tau] < d[(size_t) best])
                            best = tau;
                }

                // Parabolic interpolation around the dip.
                float lag = (float) best;
                if (best > 1 && best < maxLag - 1)
                {
                    const float l = d[(size_t) best - 1], c = d[(size_t) best], r = d[(size_t) best + 1];
                    const float den = l - 2.0f * c + r;
                    if (std::abs (den) > 1.0e-9f)
                        lag += 0.5f * (l - r) / den;
                }
                return { (float) (sr / lag), d[(size_t) best] };
            }

        private:
            double sr;
            juce::dsp::FFT fft;
            std::vector<float> a, b, d;
            int maxLag = 1000, minLag = 2;
        };

        float sampleAt (const std::vector<float>& x, double pos)
        {
            const int i = juce::jlimit (0, (int) x.size() - 2, (int) pos);
            const float f = (float) juce::jlimit (0.0, 1.0, pos - i);
            return x[(size_t) i] + f * (x[(size_t) i + 1] - x[(size_t) i]);
        }

        float median (std::vector<float> v)
        {
            if (v.empty())
                return 0.0f;
            std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
            return v[v.size() / 2];
        }
    }

    MatchResult matchSound (const juce::AudioBuffer<float>& audio, double sr, const juce::String& name)
    {
        MatchResult r;
        const int fullLen = juce::jmin (audio.getNumSamples(), (int) (sr * 60.0));
        const int channels = audio.getNumChannels();
        if (fullLen < (int) (sr * 0.05) || channels < 1)
        {
            r.error = "The file is too short.";
            return r;
        }

        std::vector<float> monoAll ((size_t) fullLen);
        for (int i = 0; i < fullLen; ++i)
            monoAll[(size_t) i] = 0.5f * (audio.getSample (0, i) + audio.getSample (channels > 1 ? 1 : 0, i));

        // Audio -> MIDI, then the hero note: long and loud wins (a 60 ms blip never does).
        r.notes = trackNotes (monoAll, sr);
        int from = 0, to = fullLen;
        float heroHz = 0;
        if (! r.notes.empty())
        {
            size_t hero = 0;
            double best = -1;
            for (size_t i = 0; i < r.notes.size(); ++i)
            {
                const auto& n = r.notes[i];
                const double score = juce::jmin (n.length, 3.0) * std::pow (n.velocity / 127.0, 2.0);
                if (score > best) { best = score; hero = i; }
            }
            const auto& h = r.notes[hero];
            heroHz = (float) (440.0 * std::exp2 ((h.pitch - 69) / 12.0));
            const double nextStart = hero + 1 < r.notes.size() ? r.notes[hero + 1].start : 1.0e9;
            const double prevEnd = hero > 0 ? r.notes[hero - 1].start + r.notes[hero - 1].length : -1.0e9;
            from = juce::jlimit (0, fullLen - 1, (int) (juce::jmax (h.start - 0.015, prevEnd) * sr));
            to = juce::jlimit (from + 1, fullLen, (int) (juce::jmin (h.start + h.length + 0.4, nextStart) * sr)); // keep the release tail
            r.heroStart = from / sr;
            r.heroEnd = to / sr;
        }
        const int len = to - from;
        if (len < (int) (sr * 0.05))
        {
            r.error = "The note is too short to analyse.";
            return r;
        }
        std::vector<float> mono ((size_t) len), side ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            const float l = audio.getSample (0, from + i), rr = audio.getSample (channels > 1 ? 1 : 0, from + i);
            mono[(size_t) i] = 0.5f * (l + rr);
            side[(size_t) i] = 0.5f * (l - rr);
        }

        // Level curve in 10 ms hops.
        const int hop = juce::jmax (1, (int) (sr * 0.01));
        const int hops = len / hop;
        std::vector<float> rms ((size_t) hops);
        float peak = 0;
        int peakHop = 0;
        for (int h = 0; h < hops; ++h)
        {
            double sum = 0;
            for (int i = h * hop; i < (h + 1) * hop; ++i)
                sum += (double) mono[(size_t) i] * mono[(size_t) i];
            rms[(size_t) h] = (float) std::sqrt (sum / hop);
            if (rms[(size_t) h] > peak) { peak = rms[(size_t) h]; peakHop = h; }
        }
        if (peak < 1.0e-4f)
        {
            r.error = "The file is silent.";
            return r;
        }
        int startHop = 0, endHop = hops - 1;
        while (startHop < peakHop && rms[(size_t) startHop] < peak * 0.06f) ++startHop;
        while (endHop > peakHop && rms[(size_t) endHop] < peak * 0.03f) --endHop;
        const int noteStart = startHop * hop, noteEnd = juce::jmin (len, (endHop + 1) * hop);

        // Pitch track.
        Yin yin (sr);
        std::vector<float> times, track;
        // Strict first; detuned / distorted leads only pass the looser clarity. The tracked note (if any)
        // rejects octave slips.
        for (const float maxClarity : { 0.2f, 0.5f })
        {
            for (int s = noteStart; s + yin.span() < len && s < noteEnd; s += 512)
            {
                if (rms[(size_t) juce::jmin (hops - 1, s / hop)] < peak * 0.15f)
                    continue;
                const auto [hz, clarity] = yin.detect (mono, s);
                if (clarity < maxClarity && hz > 30.0f && (heroHz <= 0 || std::abs (std::log2 (hz / heroHz)) < 0.3f))
                {
                    times.push_back ((float) (s + window / 2) / (float) sr);
                    track.push_back (hz);
                }
            }
            if (track.size() >= 3)
                break;
            times.clear();
            track.clear();
        }
        if (track.size() < 3 && heroHz > 0)
        {
            times = { (float) noteStart / (float) sr };
            track = { heroHz };
        }
        if (track.empty())
        {
            r.error = "No clear pitch found.\nDrop a melodic sound: one note or a mono line (lead, bass, pluck, vocal).\nChords, drums and full mixes can't be matched.";
            return r;
        }
        r.f0 = median (track);
        r.midiNote = juce::jlimit (0, 127, juce::roundToInt (69.0 + 12.0 * std::log2 (r.f0 / 440.0)));
        auto localHz = [&] (double seconds)
        {
            float best = r.f0, dist = 1e9f;
            for (size_t i = 0; i < times.size(); ++i)
                if (std::abs (times[i] - (float) seconds) < dist && std::abs (std::log2 (track[i] / r.f0)) < 0.5f) // ignore octave slips
                {
                    dist = std::abs (times[i] - (float) seconds);
                    best = track[i];
                }
            return best;
        };

        // 16 single cycles across the note (max 6 s): each the average of 4 consecutive periods,
        // rotated so the fundamental starts at phase 0, so neighbouring frames morph without cancelling.
        const double scanEnd = juce::jmin ((double) noteEnd, noteStart + sr * 6.0);
        const double longest = sr / r.f0 * cyclesPerFrame * 1.5;
        const double span = juce::jmax (0.0, scanEnd - noteStart - longest);
        std::vector<float> frames ((size_t) numFrames * tableSize), cycle ((size_t) tableSize);
        double noiseSum = 0, totalSum = 0;
        for (int f = 0; f < numFrames; ++f)
        {
            const double t = juce::jmin ((double) len - longest - 1.0, noteStart + span * f / (numFrames - 1));
            const double period = sr / localHz (t / sr);
            for (int i = 0; i < tableSize; ++i)
            {
                float sum = 0, sq = 0;
                for (int c = 0; c < cyclesPerFrame; ++c)
                {
                    const float v = sampleAt (mono, juce::jmax (0.0, t) + (c + (double) i / tableSize) * period);
                    sum += v;
                    sq += v * v;
                }
                const float mean = sum / cyclesPerFrame;
                cycle[(size_t) i] = mean;
                noiseSum += sq / cyclesPerFrame - mean * mean; // what changes between periods = not periodic
                totalSum += sq / cyclesPerFrame;
            }

            double re = 0, im = 0;
            for (int i = 0; i < tableSize; ++i)
            {
                const double w = juce::MathConstants<double>::twoPi * i / tableSize;
                re += cycle[(size_t) i] * std::cos (w);
                im += cycle[(size_t) i] * std::sin (w);
            }
            const int shift = juce::roundToInt (std::atan2 (re, im) / juce::MathConstants<double>::twoPi * tableSize);
            for (int i = 0; i < tableSize; ++i)
                frames[(size_t) f * tableSize + (size_t) i] = cycle[(size_t) (((i - shift) % tableSize + tableSize) % tableSize)];
        }
        r.table = makeUserTableFromFrames (frames, "MATCH " + name);

        // Amp envelope from the level curve.
        auto seconds = [] (int hopsCount) { return (float) hopsCount * 0.01f; };
        // Detuned unison beats: judge the levels against a 200 ms smoothed peak, not the loudest beat.
        float level = 0;
        for (int h = startHop; h <= endHop; ++h)
        {
            double sum = 0;
            int count = 0;
            for (int k = juce::jmax (startHop, h - 10); k <= juce::jmin (endHop, h + 10); ++k, ++count)
                sum += rms[(size_t) k];
            level = juce::jmax (level, (float) (sum / juce::jmax (1, count)));
        }
        level = juce::jlimit (peak * 0.3f, peak, level);
        // Attack = the steep rise. It ends at 90% of the level, or earlier once the rise slows to a quarter of its
        // steepest (unison voices drifting into phase keep the level creeping up long after the real attack).
        float maxSlope = 0;
        int steepest = startHop;
        for (int h = startHop; h < peakHop; ++h)
            if (rms[(size_t) h + 1] - rms[(size_t) h] > maxSlope) { maxSlope = rms[(size_t) h + 1] - rms[(size_t) h]; steepest = h; }
        int reach = startHop;
        while (reach < peakHop && rms[(size_t) reach] < level * 0.9f)
        {
            if (reach > steepest && rms[(size_t) reach] > level * 0.3f && reach + 2 <= peakHop
                && (rms[(size_t) reach + 2] - rms[(size_t) reach]) * 0.5f < maxSlope * 0.25f)
                break;
            ++reach;
        }
        r.attack = juce::jlimit (0.001f, 5.0f, seconds (reach - startHop));
        std::vector<float> body;
        for (int h = peakHop + (endHop - peakHop) * 3 / 10; h <= peakHop + (endHop - peakHop) * 7 / 10; ++h)
            body.push_back (rms[(size_t) h] / level);
        float sustain = juce::jlimit (0.0f, 1.0f, median (body));
        int fall = peakHop;
        const float decayTarget = level * (sustain + (1.0f - sustain) * 0.37f);
        while (fall < endHop && rms[(size_t) fall] > decayTarget) ++fall;
        const float decay = juce::jlimit (0.01f, 5.0f, seconds (fall - peakHop) * 1.6f);
        if (sustain < 0.06f) sustain = 0.0f; // plucks
        int tail = endHop;
        const float tailLevel = juce::jmax (peak * 0.05f, level * sustain * 0.7f);
        while (tail > peakHop && rms[(size_t) tail] < tailLevel) --tail;
        const float release = juce::jlimit (0.02f, 4.0f, seconds (endHop - tail));

        // Vibrato: detrended pitch wobble after the attack.
        std::vector<float> cents, when;
        for (size_t i = 0; i < track.size(); ++i)
            if (times[i] > (float) noteStart / (float) sr + r.attack && std::abs (std::log2 (track[i] / r.f0)) < 0.5f)
            {
                cents.push_back (1200.0f * std::log2 (track[i] / r.f0));
                when.push_back (times[i]);
            }
        if (cents.size() > 20)
        {
            const size_t n = cents.size();
            double mt = 0, mc = 0, stt = 0, stc = 0;
            for (size_t i = 0; i < n; ++i) { mt += when[i]; mc += cents[i]; }
            mt /= (double) n; mc /= (double) n;
            for (size_t i = 0; i < n; ++i) { stt += (when[i] - mt) * (when[i] - mt); stc += (when[i] - mt) * (cents[i] - mc); }
            const double slope = stt > 0 ? stc / stt : 0;
            std::vector<float> dev (n);
            double var = 0;
            for (size_t i = 0; i < n; ++i)
            {
                dev[i] = (float) (cents[i] - (mc + slope * (when[i] - mt)));
                var += dev[i] * dev[i];
            }
            const float stdCents = (float) std::sqrt (var / (double) n);

            // Rate from the autocorrelation peak of the wobble (2..10 Hz); crossing counts are fooled by jitter.
            const float hopSec = (when.back() - when.front()) / (float) (n - 1);
            float rate = 0, bestScore = 0.3f;
            for (int lag = juce::jmax (1, (int) (0.1f / hopSec)); lag <= (int) (0.5f / hopSec) && lag < (int) n / 2; ++lag)
            {
                double num = 0, den = 0;
                for (size_t i = 0; i + (size_t) lag < n; ++i)
                {
                    num += dev[i] * dev[i + (size_t) lag];
                    den += dev[i] * dev[i];
                }
                const float score = den > 0 ? (float) (num / den) : 0.0f;
                if (score > bestScore)
                {
                    bestScore = score;
                    rate = 1.0f / ((float) lag * hopSec);
                }
            }
            if (stdCents > 6.0f && rate > 2.0f && rate < 10.0f)
            {
                r.vibratoCents = stdCents * 1.414f;
                r.vibratoHz = rate;
            }
        }

        // Width and noise.
        double es = 0, em = 0;
        for (int i = noteStart; i < noteEnd; ++i)
        {
            es += (double) side[(size_t) i] * side[(size_t) i];
            em += (double) mono[(size_t) i] * mono[(size_t) i];
        }
        r.width = (float) std::sqrt (es / juce::jmax (1.0e-12, em));
        const float noiseRatio = (float) (noiseSum / juce::jmax (1.0e-12, totalSum));

        auto& v = r.values;
        v = { { "aOn", 1 }, { "aTable", (float) userTableIndex }, { "aPos", 0 }, { "aLevel", 0.8f },
              { "e1A", r.attack }, { "e1D", decay }, { "e1S", sustain }, { "e1R", release },
              { "e2A", (float) juce::jlimit (0.01, 10.0, (scanEnd - noteStart) / sr) }, { "e2D", 0.01f }, { "e2S", 1 }, { "e2R", release },
              { "m1Src", (float) sEnv2 }, { "m1Dst", (float) tAPos }, { "m1Amt", 1 },
              { "fCut", 20000 }, { "fRes", 0 }, { "fxReverb", 0 } };
        if (r.width > 0.12f)
        {
            v.push_back ({ "aUni", (float) juce::jlimit (2, 7, juce::roundToInt (2.0f + r.width * 10.0f)) });
            v.push_back ({ "aDet", juce::jlimit (0.06f, 0.4f, r.width * 0.5f) });
        }
        if (noiseRatio > 0.08f)
            v.push_back ({ "noise", juce::jlimit (0.0f, 0.5f, std::sqrt (noiseRatio) * 0.6f) });
        if (r.vibratoHz > 0.0f)
        {
            v.push_back ({ "l1Shape", 0 });
            v.push_back ({ "l1Sync", 0 });
            v.push_back ({ "l1Rate", r.vibratoHz });
            v.push_back ({ "m2Src", (float) sLfo1 });
            v.push_back ({ "m2Dst", (float) tPitch });
            v.push_back ({ "m2Amt", r.vibratoCents / 100.0f / 24.0f });
        }

        juce::String midiLine;
        if (r.notes.size() > 1)
        {
            midiLine << "MIDI: " << (int) r.notes.size() << " notes found  (";
            for (size_t i = 0; i < juce::jmin<size_t> (12, r.notes.size()); ++i)
                midiLine << (i ? " " : "") << juce::MidiMessage::getMidiNoteName (r.notes[i].pitch, true, true, 3);
            midiLine << (r.notes.size() > 12 ? " ...)" : ")") << "\nTimbre taken from the note at " << juce::String (r.heroStart, 2) << " s\n"
                     << "Click MIDI to hear the line on this patch, drag it into your DAW\n\n";
        }
        r.summary = midiLine + "Pitch: " + juce::MidiMessage::getMidiNoteName (r.midiNote, true, true, 3) + "  (" + juce::String (r.f0, 1) + " Hz)  -  play "
                  + juce::MidiMessage::getMidiNoteName (r.midiNote, true, true, 3) + " to hear it at the original pitch\n"
                  + "Timbre: 16 cycles over " + juce::String ((scanEnd - noteStart) / sr, 2) + " s -> USER wavetable, swept by MOD ENV\n"
                  + "Amp env: A " + juce::String (r.attack * 1000.0f, 0) + " ms, D " + juce::String (decay * 1000.0f, 0) + " ms, S "
                  + juce::String (juce::roundToInt (sustain * 100.0f)) + "%, R " + juce::String (release * 1000.0f, 0) + " ms\n"
                  + (r.vibratoHz > 0 ? "Vibrato: " + juce::String (r.vibratoHz, 1) + " Hz, " + juce::String (r.vibratoCents, 0) + " cents -> LFO 1\n" : juce::String ("Vibrato: none\n"))
                  + "Width: " + (r.width > 0.12f ? "wide -> unison" : juce::String ("narrow")) + "   Noise: " + (noiseRatio > 0.08f ? "yes" : "no");
        r.ok = r.table != nullptr;
        return r;
    }
}
