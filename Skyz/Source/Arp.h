#pragma once

#include "Util.h"
#include <vector>

// CONSTELLATION ARP: held keys -> a tempo-locked line shaped by 9 lanes of 32 steps
// (gate, velocity, pitch, octave, length, chance, ratchet, slide, nudge).
// Follows the host beat while it plays, else a free clock. SLIDE steps are sent as CC65 so the synth glides into them.
namespace skyz
{
    struct Lanes
    {
        enum { gate, vel, pitch, oct, len, prob, rat, slide, nudge, numLanes };
        float v[numLanes][32] {};
        Lanes()
        {
            for (int s = 0; s < 32; ++s) { v[gate][s] = 1; v[vel][s] = 0.75f; v[pitch][s] = 0.5f; v[oct][s] = 2; v[len][s] = 1; v[prob][s] = 1; v[rat][s] = 0; v[slide][s] = 0; v[nudge][s] = 0.5f; }
        }
        juce::String toString() const
        {
            juce::StringArray rows;
            for (auto& lane : v) { juce::StringArray c; for (float x : lane) c.add (juce::String (x, 3)); rows.add (c.joinIntoString (",")); }
            return rows.joinIntoString (";");
        }
        void fromString (const juce::String& s)
        {
            auto rows = juce::StringArray::fromTokens (s, ";", "");
            for (int i = 0; i < juce::jmin ((int) numLanes, rows.size()); ++i)
            {
                auto c = juce::StringArray::fromTokens (rows[i], ",", "");
                for (int k = 0; k < juce::jmin (32, c.size()); ++k) v[i][k] = c[k].getFloatValue();
            }
        }
    };

    class Arp
    {
    public:
        // Called from prepareToPlay: reserves every buffer here, so process() never allocates on the audio thread.
        void reset()
        {
            rng.setSeed (12345); noteCounter = 0; orderCounter = 0; held.clear(); offs.clear(); down.clearQuick(); counter = 0; pos = 0; dirSign = 1; beat = 0;
            anyDown = false; wasOn = false; waitUntil = -1; step = -1; lastBarReset = -1; gridShift = 0; anchor = 0;
            held.reserve (128); offs.reserve (1024); keys.reserve (512); baseBuf.reserve (128); seqBuf.reserve (2048); tmpBuf.reserve (2048); noteBuf.reserve (512);
            down.ensureStorageAllocated (128); outBuf.ensureSize (16384);
        }
        int currentStep() const { return step; }

        void process (juce::MidiBuffer& midi, int numSamples, double sampleRate, double bpm, double ppq, const Values& p, const Lanes& lanes)
        {
            auto& out = outBuf;
            out.clear();
            const bool on = p.on (P_arpOn);
            if (! on || sampleRate <= 0 || numSamples <= 0) // off: the played notes pass through untouched
            {
                if (wasOn) { for (auto& o : offs) midi.addEvent (juce::MidiMessage::noteOff (1, o.note), 0); offs.clear(); held.clear(); down.clearQuick(); anyDown = false; step = -1; }
                wasOn = false;
                return;
            }
            wasOn = true;
            const double bps = bpm / 60.0 / sampleRate;
            srNow = sampleRate;
            if (ppq >= 0) beat = ppq;
            const double start = beat, end = beat + numSamples * bps;
            const double stepB = divBeats (p.list (P_aRate, numDivs));
            const int length = 1 + (int) std::lround (p[P_aSteps] * 31.0f);

            keys.clear();
            for (const auto m : midi)
            {
                const auto msg = m.getMessage();
                if (msg.isAllNotesOff() || msg.isAllSoundOff()) // host stop / panic: forget the held keys, release what sounds
                {
                    held.clear(); down.clearQuick(); anyDown = false; step = -1;
                    for (auto& o : offs) out.addEvent (juce::MidiMessage::noteOff (1, o.note), m.samplePosition);
                    offs.clear();
                    out.addEvent (msg, m.samplePosition);
                    continue;
                }
                if (msg.isNoteOnOrOff()) { keys.push_back ({ m.samplePosition, msg }); if (p[P_aThru] > 0.001f) { auto t = msg; if (t.isNoteOn()) t.setVelocity (t.getFloatVelocity() * p[P_aThru]); out.addEvent (t, m.samplePosition); } }
                else out.addEvent (msg, m.samplePosition);
            }
            // DAW stopped (or standalone): there is no song grid to lock to, so the arp's grid starts on the key you press and
            // the first note plays at once. With the DAW playing, steps stay on the song's grid (and START QUANTIZE applies).
            const bool freeMode = ppq < 0;
            if (! freeMode) gridShift = anchor = 0;
            else if (! anyDown)
                for (auto& [pos0, msg] : keys)
                    if (msg.isNoteOn()) { anchor = start + pos0 * bps; gridShift = std::fmod (anchor, stepB); break; } // swing counts from the key too

            size_t nextKey = 0;
            auto applyKeys = [&] (int sample, double atBeat) { for (; nextKey < keys.size() && keys[nextKey].first <= sample; ++nextKey) key (keys[nextKey].second, p, atBeat, freeMode); };

            // swing pushes steps that start in an odd swing-grid cell; nudge lanes push single steps
            static const double grids[] { 0.5, 0.25, 0.125 };
            const double grid = grids[p.idx (P_aSwGrid)];
            const float swing = p[P_aSwing];

            for (juce::int64 k = (juce::int64) std::floor ((start - gridShift) / stepB) - 2;; ++k)
            {
                const double t0 = (double) k * stepB + gridShift;
                if (t0 - stepB > end) break;
                const int lanePos = laneIndex (k, length, p);
                double t = t0;
                if (((juce::int64) std::floor ((t0 - anchor) / grid + 1e-6) & 1) != 0) t += swing * 0.5 * grid;
                t += (lanes.v[Lanes::nudge][lanePos] - 0.5f) * stepB; // +-50 % of a step
                if (t < start || t >= end) continue;
                const int sample = juce::jlimit (0, numSamples - 1, (int) std::floor ((t - start) / bps + 1e-6)); // a step on the key's sample sees the key
                emitOffs (out, t, start, bps, numSamples);
                applyKeys (sample, t);
                if (held.empty()) { step = -1; continue; }
                if (waitUntil >= 0 && t + 1e-9 < waitUntil) continue; // quantized start
                waitUntil = -1;
                // retrigger by bar
                const int rt = p.idx (P_aRetrig);
                if (rt == 1 || rt == 2) { const juce::int64 bar = (juce::int64) std::floor (t / (rt == 1 ? 4.0 : 8.0) + 1e-9); if (bar != lastBarReset) { lastBarReset = bar; pos = 0; counter = 0; } }
                fire (out, sample, t, stepB, bps, numSamples, p, lanes, length);
            }
            applyKeys (numSamples, end);
            emitOffs (out, end, start, bps, numSamples);
            beat = end;
            midi.clear(); // copy back (not swap): the arp keeps its own reserved buffer, the host keeps its own
            midi.addEvents (out, 0, -1, 0);
        }

    private:
        struct Held { int note, vel, order; };
        struct Off { double beat; int note; };

        int laneIndex (juce::int64, int length, const Values& p) const
        {
            const int offset = (int) std::lround (p[P_aOffset] * 31.0f);
            return (offset + ((pos % length) + length) % length) % 32;
        }

        void key (const juce::MidiMessage& m, const Values& p, double atBeat, bool freeMode)
        {
            const int n = m.getNoteNumber();
            const bool latch = p.on (P_arpLatch);
            if (m.isNoteOn())
            {
                if (! anyDown) // first key after all were up
                {
                    if (latch || held.empty()) held.clear();
                    if (p.on (P_aKeySync) || p.idx (P_aRetrig) == 0) { pos = 0; counter = 0; dirSign = 1; }
                    static const double q[] { 0, 0.25, 0.5, 1.0, 4.0 };
                    const int qi = p.idx (P_aQuant);
                    waitUntil = (qi > 0 && ! freeMode) ? std::ceil (atBeat / q[qi] - 1e-6) * q[qi] : -1;
                }
                else if (p.idx (P_aRetrig) == 0 && ! p.on (P_aKeySync)) {} // keep running
                anyDown = true;
                down.addIfNotAlreadyThere (n);
                for (auto& h : held) if (h.note == n) return;
                held.push_back ({ n, m.getVelocity(), orderCounter++ });
            }
            else
            {
                down.removeFirstMatchingValue (n);
                anyDown = ! down.isEmpty();
                if (! latch) held.erase (std::remove_if (held.begin(), held.end(), [n] (const Held& h) { return h.note == n; }), held.end());
            }
        }

        const std::vector<Held>& sequence (const Values& p)
        {
            auto& base = baseBuf; base.assign (held.begin(), held.end());
            const int mode = p.idx (P_aMode);
            if (mode != 5) std::sort (base.begin(), base.end(), [] (const Held& a, const Held& b) { return a.note < b.note; });
            else std::sort (base.begin(), base.end(), [] (const Held& a, const Held& b) { return a.order < b.order; });
            if (p.on (P_aSeq) || mode == 9) base.resize (1); // SEQ MODE / PSY ROLL: the lowest key, the lanes make the line
            const int range = (int) std::lround (p.lin (P_aRange, 1, 48));
            if (! base.empty()) { const int lo = base.front().note; for (auto& h : base) while (h.note - lo > range) h.note -= 12; }

            const int octs = 1 + p.list (P_aOct, 4), om = p.idx (P_aOctMode);
            int octOrder[8]; int numOct = 0;
            for (int o = 0; o < octs; ++o) octOrder[numOct++] = o;
            if (om == 1) std::reverse (octOrder, octOrder + numOct);
            if (om == 2) for (int o = octs - 2; o > 0; --o) octOrder[numOct++] = o;
            auto& seq = seqBuf; seq.clear();
            for (int k = 0; k < numOct; ++k) for (auto h : base) seq.push_back ({ h.note + 12 * octOrder[k], h.vel, h.order });
            auto& c = tmpBuf;

            switch (mode)
            {
                case 1: std::reverse (seq.begin(), seq.end()); break;
                case 2: case 3: if (mode == 3) std::reverse (seq.begin(), seq.end()); if (seq.size() > 2) for (int i = (int) seq.size() - 2; i > 0; --i) seq.push_back (seq[(size_t) i]); break;
                case 7: case 8: { c.clear(); int lo = 0, hi = (int) seq.size() - 1; while (lo <= hi) { c.push_back (seq[(size_t) (mode == 7 ? lo : hi)]); if (lo != hi) c.push_back (seq[(size_t) (mode == 7 ? hi : lo)]); ++lo; --hi; } if (mode == 8) std::reverse (c.begin(), c.end()); seq.swap (c); break; }
                case 10: { if (seq.size() > 1) { c.clear(); const auto top = seq.back(); for (size_t i = 0; i + 1 < seq.size(); ++i) { c.push_back (seq[i]); c.push_back (top); } seq.swap (c); } break; } // pinky up
                case 11: { if (seq.size() > 1) { c.clear(); const auto low = seq.front(); for (size_t i = 1; i < seq.size(); ++i) { c.push_back (low); c.push_back (seq[i]); } seq.swap (c); } break; } // thumb
                default: break;
            }
            return seq;
        }

        int scaleSnap (int note, const Values& p) const
        {
            static const int scales[9][12] {
                { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,1,0,1,0,1,0,1,1,0,1,0 }, { 1,0,1,1,0,1,0,1,1,0,0,1 },
                { 1,0,1,1,0,1,0,1,0,1,1,0 }, { 1,1,0,0,1,1,0,1,1,0,1,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 }, { 1,0,0,1,0,1,1,1,0,0,1,0 } };
            const int sc = p.idx (P_aScale); if (sc == 0) return note;
            const int root = p.idx (P_aRoot);
            for (int d = 0; d < 12; ++d) { const int up = note + d, dn = note - d; if (scales[sc][((up - root) % 12 + 12) % 12]) return up; if (scales[sc][((dn - root) % 12 + 12) % 12]) return dn; }
            return note;
        }

        void fire (juce::MidiBuffer& out, int sample, double t, double stepB, double bps, int numSamples, const Values& p, const Lanes& lanes, int length)
        {
            // where are we on the lanes (direction)
            const int dir = p.idx (P_aDir);
            const int offset = (int) std::lround (p[P_aOffset] * 31.0f);
            int idx;
            switch (dir)
            {
                case 1: idx = length - 1 - (counter % length); break;
                case 2: { const int period = juce::jmax (1, 2 * length - 2); const int c = counter % period; idx = c < length ? c : period - c; break; }
                case 3: idx = rng.nextInt (length); break;
                case 4: { pos += rng.nextBool() ? 1 : -1; pos = ((pos % length) + length) % length; idx = pos; break; }
                default: idx = counter % length;
            }
            if (dir != 4) pos = idx;
            const int li = (offset + idx) % 32;
            step = li;
            ++counter;

            const int g = (int) std::lround (lanes.v[Lanes::gate][li]);
            if (g == 0) return;
            if (g == 3) // tie: stretch what is sounding over this step too
            {
                for (auto& o : offs) o.beat = juce::jmax (o.beat, t + stepB * juce::jlimit (0.02, 1.0, (double) p[P_aGate]));
                return;
            }
            if (rng.nextFloat() > lanes.v[Lanes::prob][li] * p[P_aChance]) return;

            const auto& seq = sequence (p);
            if (seq.empty()) return;
            auto& notes = noteBuf; notes.clear();
            if (p.idx (P_aMode) == 6) notes.assign (seq.begin(), seq.end()); // chord
            else
            {
                const int n = (int) seq.size();
                const int i = p.idx (P_aMode) == 4 ? rng.nextInt (n) : noteCounter++ % n;
                notes.push_back (seq[(size_t) i]);
            }

            const int transpose = (int) std::lround (p.lin (P_aTrans, -24, 24)) + (int) std::lround ((lanes.v[Lanes::pitch][li] - 0.5f) * 48.0f) + ((int) std::lround (lanes.v[Lanes::oct][li]) - 2) * 12;
            const bool slide = lanes.v[Lanes::slide][li] > 0.5f;
            const int ratchet = juce::jmax (1 + (int) std::lround (lanes.v[Lanes::rat][li]), 1 + p.list (P_aRatchet, 4));
            const double len = stepB * juce::jlimit (0.02, 1.0, (double) (p[P_aGate] * lanes.v[Lanes::len][li])) / ratchet;
            const double humT = p.lin (P_aHumT, 0, 30) * 0.001, strum = p.lin (P_aStrum, 0, 200) * 0.001, flam = p.lin (P_aFlam, 0, 40) * 0.001;
            const double sr = srNow; // samples per second
            // shortcut: humanize / strum / flam offsets past the block end are clamped into this block

            if (slide) out.addEvent (juce::MidiMessage::controllerEvent (1, 65, 127), sample);
            int k = 0;
            for (auto h : notes)
            {
                const int note = juce::jlimit (0, 127, scaleSnap (h.note + transpose, p));
                float vel = (float) h.vel + ((lanes.v[Lanes::vel][li] * 127.0f) - (float) h.vel) * p[P_aVel];
                if (g == 2) vel += (127.0f - vel) * p[P_aAcc];
                vel += (rng.nextFloat() * 2.0f - 1.0f) * p[P_aHumV] * 25.0f;
                const int v = juce::jlimit (1, 127, (int) vel);
                for (int r = 0; r < ratchet; ++r)
                {
                    const double secOff = juce::jmax (0.0, humT * rng.nextFloat() + strum * k);
                    const double tb = t + (double) r * stepB / ratchet;
                    const int at = juce::jlimit (0, numSamples - 1, sample + (int) ((double) r * stepB / ratchet / bps) + (int) (secOff * sr));
                    for (auto it = offs.begin(); it != offs.end();) { if (it->note == note && ! slide) { out.addEvent (juce::MidiMessage::noteOff (1, note), at); it = offs.erase (it); } else ++it; }
                    out.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) v), at);
                    if (flam > 0.0005 && r == 0) // flam: a second, softer hit just after
                    {
                        const int fs = juce::jlimit (0, numSamples - 1, at + (int) (flam * sr));
                        out.addEvent (juce::MidiMessage::noteOff (1, note), fs);
                        out.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jmax (1, v * 3 / 5)), fs);
                    }
                    offs.push_back ({ tb + len + (slide ? stepB * 0.6 : 0.0), note });
                }
                ++k;
            }
            if (slide) out.addEvent (juce::MidiMessage::controllerEvent (1, 65, 0), juce::jmin (numSamples - 1, sample + 1));
        }

        void emitOffs (juce::MidiBuffer& out, double until, double start, double bps, int numSamples)
        {
            for (auto it = offs.begin(); it != offs.end();)
            {
                if (it->beat <= until) { out.addEvent (juce::MidiMessage::noteOff (1, it->note), juce::jlimit (0, numSamples - 1, (int) ((it->beat - start) / bps))); it = offs.erase (it); }
                else ++it;
            }
        }

        std::vector<Held> held; std::vector<Off> offs; juce::Array<int> down;
        bool anyDown = false, wasOn = false;
        int counter = 0, noteCounter = 0, pos = 0, dirSign = 1, orderCounter = 0, step = -1;
        double beat = 0, waitUntil = -1, srNow = 48000, gridShift = 0, anchor = 0; juce::int64 lastBarReset = -1;
        std::vector<std::pair<int, juce::MidiMessage>> keys; std::vector<Held> baseBuf, seqBuf, tmpBuf, noteBuf; juce::MidiBuffer outBuf;
        juce::Random rng;
    };
}
