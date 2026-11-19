// SkyzTest                 -> offline checks: every factory preset plays, stays finite and under 0 dBFS, the arp runs, CPU
// SkyzTest trim <out.h>    -> measures every preset and writes level-matching trims (PresetTrims.h)
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <set>

// Counts heap allocations while g_countAllocs is set (for the "no allocation on the audio thread" check).
static std::atomic<bool> g_countAllocs { false };
static std::atomic<long> g_allocs { 0 };
void* operator new (std::size_t n) { if (g_countAllocs.load (std::memory_order_relaxed)) g_allocs.fetch_add (1, std::memory_order_relaxed); if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    const double rate = std::getenv ("SKYZ_RATE") ? std::atof (std::getenv ("SKYZ_RATE")) : 48000.0; // SKYZ_RATE=44100 SkyzTest ... to check another rate
    int failures = 0;
    void check (bool ok, const juce::String& what) { std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n"; failures += ok ? 0 : 1; }

    struct Result { bool finite = true; float peak = 0, rmsDb = -200; double clipped = 0; }; // clipped = share of samples at the limiter ceiling

    // plays notes for hold seconds, renders total seconds, measures from 'from' to hold
    const int blockSize = std::getenv ("SKYZ_BLOCK") ? std::atoi (std::getenv ("SKYZ_BLOCK")) : 512; // SKYZ_BLOCK=64 to check small host blocks

    Result play (SkyzProcessor& p, std::initializer_list<int> notes, double hold, double total, double from)
    {
        p.prepareToPlay (rate, blockSize);
        juce::AudioBuffer<float> buf (2, blockSize);
        Result r; double win = 0, best = 0; std::vector<double> ring (2400, 0.0); size_t ri = 0; // loudest 50 ms window
        juce::ignoreUnused (from);
        const int off = (int) (hold * rate), end = (int) (total * rate), start = (int) (from * rate);
        for (int done = 0; done < end; done += blockSize)
        {
            juce::MidiBuffer midi;
            for (int n : notes)
            {
                if (done == 0) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
                if (off >= done && off < done + blockSize) midi.addEvent (juce::MidiMessage::noteOff (1, n), off - done);
            }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float x = buf.getSample (c, i);
                    if (! std::isfinite (x)) { r.finite = false; return r; }
                    r.peak = juce::jmax (r.peak, std::abs (x));
                    if (std::abs (x) > 0.985f) r.clipped += 1.0 / (2.0 * end);
                }
            for (int i = 0; i < blockSize; ++i)
            {
                const double e = 0.5 * ((double) buf.getSample (0, i) * buf.getSample (0, i) + (double) buf.getSample (1, i) * buf.getSample (1, i));
                win += e - ring[ri]; ring[ri] = e; ri = (ri + 1) % ring.size();
                if (done + i >= start) best = juce::jmax (best, win / (double) ring.size());
            }
        }
        r.rmsDb = (float) juce::Decibels::gainToDecibels (std::sqrt (best), -200.0);
        return r;
    }

    std::initializer_list<int> notesFor (const juce::String& name)
    {
        static const std::initializer_list<int> bass { 40 }, chord { 52, 59, 64, 67 }, lead { 64 };
        if (name.contains ("BASS") || name.startsWith ("ACID") || name.contains ("RUMBLE")) return bass;
        if (name.startsWith ("PSY LEAD")) return lead;
        return chord;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const bool trim = argc > 2 && juce::String (argv[1]) == "trim";
    if (argc > 2 && juce::String (argv[1]) == "probe") // probe <index>: loudness with parts switched off, to find what makes a preset quiet
    {
        SkyzProcessor q; const int idx = juce::String (argv[2]).getIntValue();
        auto run = [&] (const char* what, std::vector<std::pair<const char*, float>> sets)
        {
            q.setCurrentProgram (idx);
            for (auto& [id, x] : sets) q.apvts.getParameter (id)->setValueNotifyingHost (x);
            const auto r = play (q, notesFor (q.currentPresetName()), 1.6, 2.0, 0.02);
            std::cout << "  " << what << ": " << r.rmsDb << " dB" << std::endl;
        };
        std::cout << q.getProgramName (idx) << std::endl;
        run ("as is", {});
        run ("arp off", { { "arpOn", 0 } });
        run ("arp off, fx off", { { "arpOn", 0 }, { "onDRIVE", 0 }, { "onCHORUS", 0 }, { "onPHASER", 0 }, { "onDELAY", 0 }, { "onREVERB", 0 }, { "onGATE", 0 }, { "onDYNAMICS", 0 }, { "onEQ", 0 }, { "onSTEREO", 0 }, { "onLOFI", 0 } });
        std::vector<std::pair<const char*, float>> noMod { { "arpOn", 0 } }; for (int k = 1; k <= 16; ++k) noMod.push_back ({ strdup (("m" + juce::String (k) + "Src").toRawUTF8()), 0 });
        run ("arp off, no matrix", noMod);
        run ("arp off, filter open", { { "arpOn", 0 }, { "fCut", 1 }, { "f2Type", 0 }, { "f1Type", 3.0f / 11.0f } });
        return 0;
    }
    if (argc > 3 && juce::String (argv[1]) == "leak") // leak <a> <b> [id=value ...]: play a (with overrides), then b; print b's level
    {
        SkyzProcessor q;
        q.setCurrentProgram (juce::String (argv[2]).getIntValue());
        auto apply = [&] (bool second) { for (int k = 4; k < argc; ++k) { juce::String kv (argv[k]); const bool two = kv.startsWith ("2:"); if (two != second) continue; if (two) kv = kv.substring (2);
            q.apvts.getParameter (kv.upToFirstOccurrenceOf ("=", false, false))->setValueNotifyingHost (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()); } };
        apply (false);
        play (q, notesFor (q.currentPresetName()), 1.6, 2.4, 0.02);
        q.setCurrentProgram (juce::String (argv[3]).getIntValue());
        apply (true);
        {   SkyzProcessor fresh; fresh.setCurrentProgram (juce::String (argv[3]).getIntValue());
            for (int i = 0; i < skyz::NUM_PARAMS; ++i) { const float x = q.apvts.getParameter (skyz::paramDefs[i].id)->getValue(), y = fresh.apvts.getParameter (skyz::paramDefs[i].id)->getValue();
                if (std::abs (x - y) > 1e-5f) std::cout << "  differs " << skyz::paramDefs[i].id << ": reused " << x << " fresh " << y << std::endl; }
            if (q.lanesString() != fresh.lanesString()) std::cout << "  lanes differ" << std::endl; }
        std::cout << play (q, notesFor (q.currentPresetName()), 1.6, 2.4, 0.02).rmsDb << " dB" << std::endl;
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "latency") // latency: reported latency, note->sound offset, worst block time, arp start delay
    {
        SkyzProcessor q;
        q.prepareToPlay (rate, 64);
        std::cout << "reported latency: " << q.getLatencySamples() << " samples" << std::endl;

        // 1. note -> first sound, arp off, note placed mid-block at varying offsets
        std::vector<double> onsets; juce::String worstName; double worst = 0;
        for (int k = 0; k < 60; ++k)
        {
            const int idx = (k * 97) % 555;
            q.setCurrentProgram (idx); q.apvts.getParameter ("arpOn")->setValueNotifyingHost (0.0f);
            q.prepareToPlay (rate, 64);
            juce::AudioBuffer<float> b (2, 64);
            const int noteAt = 640 + (k * 13) % 64; // somewhere inside the 11th block
            int first = -1;
            for (int done = 0; done < 64 * 400 && first < 0; done += 64)
            {
                juce::MidiBuffer m;
                if (noteAt >= done && noteAt < done + 64) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), noteAt - done);
                q.processBlock (b, m);
                for (int i = 0; i < 64 && first < 0; ++i) if (std::abs (b.getSample (0, i)) > 1e-6f || std::abs (b.getSample (1, i)) > 1e-6f) first = done + i;
            }
            const double ms = first < 0 ? 999.0 : (first - noteAt) * 1000.0 / rate;
            onsets.push_back (ms);
            if (ms < 0) std::cout << "  sound with no note: " << q.currentPresetName() << "  (lofi " << q.apvts.getParameter ("onLOFI")->getValue() << ", lofi noise " << q.apvts.getParameter ("lNoise")->getValue()
                                  << ", reverb freeze " << q.apvts.getParameter ("rFreeze")->getValue() << ", delay freeze " << q.apvts.getParameter ("yFreeze")->getValue() << ", first at " << first << ")" << std::endl;
            if (ms > worst) { worst = ms; worstName = q.currentPresetName(); }
        }
        std::sort (onsets.begin(), onsets.end());
        std::cout << "note -> first sound (60 presets, arp off): median " << onsets[30] << " ms, 90% under " << onsets[54] << " ms, worst " << worst << " ms (" << worstName << ")" << std::endl;
        int early = 0; for (double o : onsets) if (o < 0) ++early;
        std::cout << "  sound before the note (should be 0): " << early << std::endl;

        // 2. worst block time at 64 samples (1.33 ms budget at 48 kHz): heavy patches, 8-note chords, preset switches, RANDOM, arp
        for (int bs : { 64, 128, 256 })
        {
            q.prepareToPlay (rate, bs);
            juce::AudioBuffer<float> b (2, bs);
            std::vector<double> times; juce::Random rr (5);
            const double budget = bs * 1000.0 / rate;
            for (int blk = 0; blk < (int) (rate * 40 / bs); ++blk)
            {
                juce::MidiBuffer m;
                const int t = blk * bs;
                if (t % (int) (rate * 2) < bs) // every 2 s: new preset (some with arp), 8-note chord
                {
                    q.setCurrentProgram (rr.nextInt (555));
                    for (int n : { 36, 48, 52, 55, 59, 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.9f), 0);
                }
                if (t % (int) (rate * 2) >= (int) (rate * 1.5) && t % (int) (rate * 2) < (int) (rate * 1.5) + bs) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                q.processBlock (b, m);
                times.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
            }
            std::sort (times.begin(), times.end());
            const double p999 = times[(size_t) (times.size() * 0.999)], mx = times.back();
            int over = 0; for (double x : times) if (x > budget * 0.7) ++over;
            std::cout << "block " << bs << " (" << budget << " ms budget): median " << times[times.size() / 2] << " ms, 99.9% " << p999 << " ms, worst " << mx << " ms, blocks over 70% of budget: " << over << " of " << times.size() << std::endl;
        }

        // 3. arp: how long after the key the first arp note sounds, with and without START QUANTIZE
        for (float quant : { 0.25f, 0.0f })
        {
            double worstArp = 0, sum = 0; int cnt = 0;
            for (int k = 0; k < 20; ++k)
            {
                skyz::Arp arp; skyz::Values v; for (int i = 0; i < skyz::NUM_PARAMS; ++i) v.v[i] = skyz::paramDefs[i].def;
                v.v[skyz::P_arpOn] = 1; v.v[skyz::P_aQuant] = quant; v.v[skyz::P_aRate] = 3.0f / 16.0f;
                const auto lanes = skyz::lanesFromPattern (3); // a note on every step
                const int keyAt = 1000 + k * 977; int firstOut = -1;
                for (int done = 0; done < 96000 && firstOut < 0; done += 64)
                {
                    juce::MidiBuffer m; if (keyAt >= done && keyAt < done + 64) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), keyAt - done);
                    arp.process (m, 64, rate, 120.0, -1.0, v, lanes);
                    for (const auto e : m) if (e.getMessage().isNoteOn() && firstOut < 0) firstOut = done + e.samplePosition;
                }
                const double ms = (firstOut - keyAt) * 1000.0 / rate; worstArp = juce::jmax (worstArp, ms); sum += ms; ++cnt;
            }
            std::cout << "arp first note after the key, START QUANTIZE " << (quant > 0 ? "1/16 (default)" : "OFF") << ": average " << sum / cnt << " ms, worst " << worstArp << " ms (120 BPM)" << std::endl;
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "live") // live <a> <b> ...: like a user, switch presets without restarting and play each
    {
        SkyzProcessor q; q.prepareToPlay (rate, blockSize);
        juce::AudioBuffer<float> buf (2, blockSize);
        std::vector<juce::String> sets; for (int k = 2; k < argc; ++k) if (juce::String (argv[k]).contains ("=")) sets.push_back (argv[k]); // id=value after every preset
        for (int k = 2; k < argc; ++k)
        {
            if (juce::String (argv[k]).contains ("=")) continue;
            q.setCurrentProgram (juce::String (argv[k]).getIntValue());
            for (auto& kv : sets) q.apvts.getParameter (kv.upToFirstOccurrenceOf ("=", false, false))->setValueNotifyingHost (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
            const auto name = q.currentPresetName();
            std::vector<double> ring (2400, 0.0); size_t ri = 0; double win = 0, best = 0;
            const int hold = (int) (1.4 * rate), total = (int) (1.8 * rate);
            for (int done = 0; done < total; done += blockSize)
            {
                juce::MidiBuffer m;
                if (done == 0) for (int n : notesFor (name)) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
                if (hold >= done && hold < done + blockSize) for (int n : notesFor (name)) m.addEvent (juce::MidiMessage::noteOff (1, n), hold - done);
                q.processBlock (buf, m);
                for (int i = 0; i < blockSize; ++i)
                {
                    const double e = 0.5 * ((double) buf.getSample (0, i) * buf.getSample (0, i) + (double) buf.getSample (1, i) * buf.getSample (1, i));
                    win += e - ring[ri]; ring[ri] = e; ri = (ri + 1) % ring.size(); best = juce::jmax (best, win / (double) ring.size());
                }
            }
            std::cout << argv[k] << " " << name << ": " << juce::Decibels::gainToDecibels (std::sqrt (best), -200.0) << " dB" << std::endl;
        }
        return 0;
    }
    if (argc > 3 && juce::String (argv[1]) == "seq") // seq <from> <to>: measure presets one after another in one instance
    {
        SkyzProcessor q;
        for (int i = juce::String (argv[2]).getIntValue(); i <= juce::String (argv[3]).getIntValue(); ++i)
        {
            q.setCurrentProgram (i);
            const auto r = play (q, notesFor (q.currentPresetName()), 1.6, 2.4, 0.02);
            std::cout << i << " " << q.currentPresetName() << ": " << r.rmsDb << " dB" << std::endl;
        }
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "randcheck") // the same RANDOM sound measured 3 times in fresh instances
    {
        for (int k = 0; k < 6; ++k)
        {
            auto pr = skyz::generatePreset (11, 1000u + (uint32_t) k);
            std::cout << pr.name;
            for (int t = 0; t < 3; ++t)
            {
                SkyzProcessor q; pr.trimDb = 0; q.applyPreset (pr);
                const auto r = play (q, notesFor (pr.name), 1.4, 1.4, 0.02);
                std::cout << "  " << r.rmsDb;
            }
            pr.trimDb = SkyzProcessor::measureTrimDb (pr, 48000.0);
            SkyzProcessor q; q.applyPreset (pr);
            std::cout << "   -> trim " << pr.trimDb << " gives " << play (q, notesFor (pr.name), 1.4, 1.4, 0.02).rmsDb;
            std::cout << std::endl;
        }
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "arpdbg")
    {
        skyz::Arp arp; skyz::Values v; for (int i = 0; i < skyz::NUM_PARAMS; ++i) v.v[i] = skyz::paramDefs[i].def;
        v.v[skyz::P_arpOn] = 1; v.v[skyz::P_aMode] = 5.0f / 11.0f; v.v[skyz::P_aDir] = 3.0f / 4.0f; v.v[skyz::P_aQuant] = 0; v.v[skyz::P_aOct] = 1.0f / 3.0f;
        const auto lanes = skyz::lanesFromPattern (5);
        for (int done = 0; done < 96000; done += 512)
        {
            juce::MidiBuffer m;
            if (done == 0) for (int n : { 48, 52, 55 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
            arp.process (m, 512, 48000, 120.0, -1.0, v, lanes);
            if (arp.currentStep() >= 0 && done < 24000) std::cout << arp.currentStep() << " ";
            for (const auto e : m) if (e.getMessage().isNoteOn()) std::cout << "[on " << e.getMessage().getNoteNumber() << "] ";
        }
        std::cout << std::endl; return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "dump") // dump <index>: the preset's values that differ from the defaults
    {
        const auto& pr = skyz::factoryPresets()[(size_t) juce::String (argv[2]).getIntValue()];
        std::cout << pr.name << std::endl;
        for (auto& [id, x] : pr.values) if (std::abs (x - skyz::paramDefs[id].def) > 1e-4f) std::cout << "  " << skyz::paramDefs[id].id << " = " << x << std::endl;
        std::cout << "lanes " << pr.lanes.toString().substring (0, 200) << std::endl;
        return 0;
    }
    SkyzProcessor p;
    const auto& presets = skyz::factoryPresets();
    check (presets.size() == 555, "555 factory presets (" + juce::String ((int) presets.size()) + ")");
    juce::StringArray names; for (auto& pr : presets) names.addIfNotAlreadyThere (pr.name);
    check (names.size() == (int) presets.size(), "every preset name is unique");

    std::vector<float> trims;
    int silent = 0, loud = 0, bad = 0;
    float minDb = 0, maxDb = -200;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        p.setCurrentProgram (i);
        if (trim) p.apvts.getParameter ("master")->setValueNotifyingHost (0.75f);
        const auto& name = presets[(size_t) i].name;
        const bool slow = name.contains ("PAD") || name.contains ("ATMOS") || name.contains ("RISER") || name.contains ("RUMBLE");
        const auto r = play (p, notesFor (name), slow ? 2.5 : 1.4, slow ? 2.5 : 1.4, 0.02); // the leveller's listening window
        // refine the trim that is in effect (run `trim` twice: the second pass corrects what the limiter squashed)
        if (trim) { auto pr = presets[(size_t) i]; pr.trimDb = 0; trims.push_back (SkyzProcessor::measureTrimDb (pr, rate)); } // same leveller as RANDOM // loudest 50 ms at -15 dBFS RMS
        if (! r.finite) { ++bad; check (false, "preset " + juce::String (i) + " " + name + " is not finite"); }
        else if (r.rmsDb < -60.0f) { ++silent; check (false, "preset " + juce::String (i) + " " + name + " is silent (" + juce::String (r.rmsDb, 1) + " dB)"); }
        else if (r.clipped > 0.005) { ++loud; check (false, "preset " + juce::String (i) + " " + name + " sits on the limiter " + juce::String (r.clipped * 100.0, 2) + "% of the time"); }
        minDb = juce::jmin (minDb, r.rmsDb); maxDb = juce::jmax (maxDb, r.rmsDb);
        if (! trim && std::abs (r.rmsDb + 15.0f) > 5.0f) std::cout << "       level " << i << " " << name << ": " << r.rmsDb << " dB (trim " << presets[(size_t) i].trimDb << ")" << std::endl;
    }
    check (bad == 0 && silent == 0 && loud == 0 && maxDb - minDb < 12.0f, "all presets play, finite, not limiter-crushed, level-matched (silent " + juce::String (silent) + ", loud " + juce::String (loud) + ", RMS " + juce::String (minDb, 1) + " .. " + juce::String (maxDb, 1) + " dB)");

    if (trim)
    {
        juce::String s = "// GENERATED by `SkyzTest trim` (level-matches the factory presets). Regenerate after changing a recipe.\n#pragma once\nnamespace skyz { inline const float presetTrims[] {\n";
        for (size_t i = 0; i < trims.size(); ++i) s << juce::String (trims[i], 2) << "f" << (i + 1 < trims.size() ? ((i + 1) % 16 ? ", " : ",\n") : "\n");
        s << "}; }\n";
        juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])).replaceWithText (s);
        std::cout << "wrote " << argv[2] << "\n";
        return 0;
    }

    // Arp: 1 s at 120 BPM, 1/16, PSY GALLOP = 3 notes of every 4 sixteenths -> 6 note-ons
    {
        skyz::Arp arp; skyz::Values v; for (int i = 0; i < skyz::NUM_PARAMS; ++i) v.v[i] = skyz::paramDefs[i].def;
        v.v[skyz::P_arpOn] = 1; v.v[skyz::P_aRate] = 3.0f / 16.0f; v.v[skyz::P_aQuant] = 0; v.v[skyz::P_aSteps] = 15.0f / 31.0f;
        const auto lanes = skyz::lanesFromPattern (0);
        int ons = 0;
        for (int done = 0; done < 48000; done += 480)
        {
            juce::MidiBuffer m;
            if (done == 0) m.addEvent (juce::MidiMessage::noteOn (1, 45, 0.8f), 0);
            arp.process (m, 480, rate, 120.0, -1.0, v, lanes);
            for (const auto e : m) ons += e.getMessage().isNoteOn() ? 1 : 0;
        }
        check (ons == 6, "arp PSY GALLOP plays 6 of 8 sixteenths in 1 s (" + juce::String (ons) + ")");
    }

    using namespace skyz;
    auto vec = [&] { std::vector<float> v; for (int i = 0; i < NUM_PARAMS; ++i) if (i != P_uiEnv && i != P_uiLfo) v.push_back (p.apvts.getParameter (paramDefs[i].id)->getValue()); return v; };
    auto dist = [] (const std::vector<float>& a, const std::vector<float>& b) { float d = 0; for (size_t i = 0; i < a.size(); ++i) d += std::abs (a[i] - b[i]); return d / (float) a.size(); };
    auto setP = [&] (int id, float x) { p.apvts.getParameter (paramDefs[id].id)->setValueNotifyingHost (x); };

    // ---- 1. diversity: no two factory presets close to each other ----
    {
        std::vector<std::vector<float>> all;
        for (int i = 0; i < (int) presets.size(); ++i) { p.setCurrentProgram (i); all.push_back (vec()); }
        float worst = 1, sum = 0; int worstA = 0, worstB = 0; long pairs = 0; double avg = 0;
        for (size_t a = 0; a < all.size(); ++a)
        {
            float nearest = 1;
            for (size_t b = 0; b < all.size(); ++b) if (a != b) { const float d = dist (all[a], all[b]); nearest = juce::jmin (nearest, d); if (b > a) { avg += d; ++pairs; } if (d < worst) { worst = d; worstA = (int) a; worstB = (int) b; } }
            sum += nearest;
        }
        check (worst > 0.06f, "closest two presets still differ by " + juce::String (worst * 100.0f, 1) + "% of all knobs (" + presets[(size_t) worstA].name + " / " + presets[(size_t) worstB].name + ")");
        check (sum / (float) all.size() > 0.12f, "average nearest-neighbour difference " + juce::String (sum / (float) all.size() * 100.0f, 1) + "%, average pair " + juce::String (avg / (double) pairs * 100.0, 1) + "%");
        int differentFilter = 0, differentOsc = 0; std::set<juce::String> combos;
        for (auto& v : all) combos.insert (juce::String (v[P_f1Type], 2) + juce::String (v[P_bWave], 2) + juce::String (v[P_rType], 2) + juce::String (v[P_yType], 2) + juce::String (v[P_cons], 2));
        differentFilter = (int) combos.size();
        juce::ignoreUnused (differentOsc);
        check (differentFilter > 400, "filter x wave x reverb x delay x constellation combinations used: " + juce::String (differentFilter) + " of 555 presets");
    }

    // ---- 2. RANDOM: 200 presses, every one plays, lands near the target level, and changes nearly everything ----
    {
        int bad = 0, silentR = 0, crushed = 0; float minDb = 0, maxDb = -200, minChange = 1; double avgChange = 0;
        std::vector<float> prev;
        for (int i = 0; i < 200; ++i)
        {
            p.randomPreset();
            const auto v = vec();
            if (! prev.empty()) { const float d = dist (v, prev); minChange = juce::jmin (minChange, d); avgChange += d; }
            prev = v;
            const auto nm = p.currentPresetName();
            const bool slow = nm.contains ("PAD") || nm.contains ("ATMOS") || nm.contains ("RISER") || nm.contains ("RUMBLE");
            const auto r = play (p, notesFor (nm), slow ? 2.5 : 1.4, slow ? 2.5 : 1.4, 0.02); // same listening window as the leveller
            if (! r.finite) ++bad; else if (r.rmsDb < -60.0f) ++silentR; else if (r.clipped > 0.005) ++crushed;
            if (r.finite && r.rmsDb > -60.0f) { minDb = juce::jmin (minDb, r.rmsDb); maxDb = juce::jmax (maxDb, r.rmsDb); }
            if (std::abs (r.rmsDb + 15.0f) > 6.0f) std::cout << "       random " << nm << ": " << r.rmsDb << " dB" << std::endl;
        }
        check (bad == 0 && silentR == 0 && crushed == 0, "200 RANDOM sounds: finite " + juce::String (200 - bad) + ", silent " + juce::String (silentR) + ", crushed " + juce::String (crushed));
        check (maxDb - minDb < 12.0f, "RANDOM levels stay matched: " + juce::String (minDb, 1) + " .. " + juce::String (maxDb, 1) + " dB");
        check (avgChange / 199.0 > 0.15 && minChange > 0.08f, "each RANDOM press changes " + juce::String (avgChange / 199.0 * 100.0, 1) + "% of all knobs on average (least " + juce::String (minChange * 100.0f, 1) + "%)");
    }

    // ---- 3. every filter type and every FX type at extreme settings stays finite and bounded ----
    {
        int bad = 0; juce::String which;
        for (int f = 0; f < 12; ++f)
            for (float res : { 0.0f, 1.0f })
            {
                p.setCurrentProgram (0); setP (P_arpOn, 0); setP (P_f1Type, (float) f / 11.0f); setP (P_fRes, res); setP (P_fDrive, 1); setP (P_fCut, 0.9f);
                const auto r = play (p, { 36, 60, 96 }, 0.8, 1.0, 0);
                if (! r.finite || r.peak > 1.01f) { ++bad; which << " F" << f; }
            }
        struct T { int type; int n; std::vector<std::pair<int, float>> extra; };
        const T units[] { { P_xType, 11, { { P_onDRIVE, 1 }, { P_xDrive, 1 }, { P_xBias, 1 } } }, { P_cType, 5, { { P_onCHORUS, 1 }, { P_cMix, 1 }, { P_cFb, 1 } } },
                          { P_pType, 5, { { P_onPHASER, 1 }, { P_pMix, 1 }, { P_pFb, 1 } } }, { P_yType, 7, { { P_onDELAY, 1 }, { P_yMix, 1 }, { P_yFb, 1 }, { P_ySat, 1 } } },
                          { P_rType, 11, { { P_onREVERB, 1 }, { P_rMix, 1 }, { P_rDecay, 1 }, { P_rShim, 1 } } }, { P_dType, 6, { { P_onDYNAMICS, 1 }, { P_dDepth, 1 }, { P_dGain, 1 } } },
                          { P_lType, 5, { { P_onLOFI, 1 }, { P_lMix, 1 }, { P_lBits, 0 }, { P_lNoise, 1 } } } };
        for (auto& u : units)
            for (int t = 0; t < u.n; ++t)
            {
                p.setCurrentProgram (100); setP (P_arpOn, 0); setP (u.type, (float) t / (float) (u.n - 1));
                for (auto& [id, x] : u.extra) setP (id, x);
                setP (P_onGATE, 1); setP (P_onSTEREO, 1); setP (P_sWidth, 1); setP (P_sHaas, 1); setP (P_onEQ, 1); setP (P_qLo, 1); setP (P_qHi, 1);
                const auto r = play (p, { 48, 55, 64 }, 1.0, 2.5, 0);
                if (! r.finite || r.peak > 1.01f) { ++bad; which << " " << paramDefs[u.type].id << t; }
            }
        check (bad == 0, "12 filters x 2 resonances and 50 FX types at maximum stay finite and under 0 dBFS" + (bad ? " (bad:" + which + ")" : juce::String()));
    }

    // ---- 4. other sample rates, tiny / huge / changing blocks, mono output ----
    {
        bool ok = true; juce::String note;
        for (double sr : { 44100.0, 96000.0, 192000.0 })
            for (int block : { 1, 33, 2048 })
            {
                p.setCurrentProgram ((int) (sr / 1000.0) + block % 50);
                p.prepareToPlay (sr, block);
                juce::AudioBuffer<float> b (2, block);
                bool any = false;
                for (int done = 0; done < (int) sr; done += block)
                {
                    juce::MidiBuffer m; if (done == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
                    p.processBlock (b, m);
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const float x = b.getSample (c, i); if (! std::isfinite (x) || std::abs (x) > 1.01f) ok = false; any |= std::abs (x) > 1e-4f; }
                }
                if (! any) { ok = false; note << " silent@" << (int) sr << "/" << block; }
            }
        {   // varying block sizes in one run, like some hosts
            p.setCurrentProgram (7); p.prepareToPlay (48000, 1024); juce::Random rr (3);
            for (int k = 0; k < 400; ++k) { const int bs = 1 + rr.nextInt (1024); juce::AudioBuffer<float> b (2, bs); juce::MidiBuffer m; if (k % 40 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50 + k % 20, 0.8f), 0); if (k % 40 == 30) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                p.processBlock (b, m); for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) if (! std::isfinite (b.getSample (c, i))) ok = false; }
        }
        {   // mono output bus
            p.setCurrentProgram (20); p.prepareToPlay (48000, 256); juce::AudioBuffer<float> b (1, 256); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            for (int k = 0; k < 100; ++k) { p.processBlock (b, m); m.clear(); for (int i = 0; i < 256; ++i) if (! std::isfinite (b.getSample (0, i))) ok = false; }
        }
        check (ok, "44.1 / 96 / 192 kHz, blocks of 1, 33, 2048, random block sizes and mono output all play clean" + note);
    }

    // ---- 5. automation stress: every block a random knob jumps anywhere ----
    {
        p.setCurrentProgram (300); p.prepareToPlay (48000, 256);
        juce::AudioBuffer<float> b (2, 256); juce::Random rr (11); bool ok = true; float pk = 0;
        for (int k = 0; k < 2000; ++k)
        {
            for (int j = 0; j < 4; ++j) { const int id = rr.nextInt (NUM_PARAMS); if (id != P_master) setP (id, rr.nextFloat()); }
            juce::MidiBuffer m; if (k % 25 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 40 + rr.nextInt (40), 0.9f), 0); if (k % 25 == 20) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            p.processBlock (b, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float x = b.getSample (c, i); if (! std::isfinite (x)) ok = false; pk = juce::jmax (pk, std::abs (x)); }
        }
        check (ok && pk <= 1.01f, "10 s of random automation on all 290 knobs: finite, peak " + juce::String (pk, 2));
    }

    // ---- 6. save / load round trip and preset switching while notes play ----
    {
        p.setCurrentProgram (123); p.randomPreset();
        const auto before = vec(); const auto lanesBefore = p.lanesString();
        juce::MemoryBlock mb; p.getStateInformation (mb);
        p.setCurrentProgram (5);
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        check (dist (before, vec()) < 1e-6f && p.lanesString() == lanesBefore, "save -> load restores every knob and the arp lanes");
        p.prepareToPlay (48000, 512); juce::AudioBuffer<float> b (2, 512); bool ok = true;
        for (int k = 0; k < 300; ++k)
        {
            if (k % 10 == 0) p.setCurrentProgram ((k * 37) % 555);
            juce::MidiBuffer m; if (k % 10 == 1) m.addEvent (juce::MidiMessage::noteOn (1, 48 + k % 24, 0.8f), 0);
            p.processBlock (b, m); for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) if (! std::isfinite (b.getSample (c, i))) ok = false;
        }
        check (ok, "switching presets every 100 ms while playing stays clean");
    }

    // ---- 7. arp: every mode x direction plays, and nothing hangs after the keys and the arp stop ----
    {
        int silentCombos = 0;
        for (int mode = 0; mode < 12; ++mode)
            for (int dir = 0; dir < 5; ++dir)
            {
                Arp arp; Values v; for (int i = 0; i < NUM_PARAMS; ++i) v.v[i] = paramDefs[i].def;
                v.v[P_arpOn] = 1; v.v[P_aMode] = (float) mode / 11.0f; v.v[P_aDir] = (float) dir / 4.0f; v.v[P_aQuant] = 0; v.v[P_aOct] = 1.0f / 3.0f;
                const auto lanes = lanesFromPattern (3); // a note on every step: directions and modes are tested, not luck with rests
                std::set<int> on; int ons = 0;
                for (int done = 0; done < 96000; done += 512)
                {
                    juce::MidiBuffer m;
                    if (done == 0) for (int n : { 48, 52, 55 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
                    if (done >= 48000 && done < 48512) for (int n : { 48, 52, 55 }) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
                    if (done >= 72000 && done < 72512) v.v[P_arpOn] = 0;
                    arp.process (m, 512, 48000, 120.0, -1.0, v, lanes);
                    for (const auto e : m) { const auto msg = e.getMessage(); if (msg.isNoteOn()) { on.insert (msg.getNoteNumber()); ++ons; } else if (msg.isNoteOff()) on.erase (msg.getNoteNumber()); }
                }
                if (ons == 0 || ! on.empty()) { ++silentCombos; std::cout << "       mode " << mode << " dir " << dir << ": " << ons << " notes, " << on.size() << " left on" << std::endl; }
            }
        check (silentCombos == 0, "arp: 12 modes x 5 directions all play and leave no hanging notes (" + juce::String (silentCombos) + " bad)");
        // the whole plugin: arp on, keys up -> voices end
        p.setCurrentProgram (60); setP (P_arpOn, 1); p.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> b (2, 512);
        for (int done = 0; done < 48000 * 5; done += 512)
        {
            juce::MidiBuffer m; if (done == 0) m.addEvent (juce::MidiMessage::noteOn (1, 45, 0.8f), 0); if (done == 48000) m.addEvent (juce::MidiMessage::noteOff (1, 45), 0);
            p.processBlock (b, m);
        }
        check (p.voices.load() == 0, "4 s after the key is released no voice is left sounding (" + juce::String (p.voices.load()) + " voices)");
    }

    // ---- 8. every header action works: MUTATE moves knobs, A/B swaps and comes back, SAVE writes a user preset ----
    {
        p.setCurrentProgram (42); const auto a = vec();
        p.mutate(); const auto m = vec();
        p.abSwap(); const auto back = vec(); p.abSwap();
        check (dist (a, m) > 0.001f, "MUTATE changes the sound (" + juce::String (dist (a, m) * 100.0f, 2) + "% of all knobs)");
        check (dist (back, m) < 1e-6f || dist (back, a) < 1e-6f, "A/B swaps between two sounds");
        const bool saved = p.saveUserPreset ("SkyzTest Temp");
        auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SKY Z/Presets/SkyzTest Temp.skyz");
        check (saved && f.existsAsFile() && p.presetNames().contains ("USER // SkyzTest Temp"), "SAVE writes a user preset and lists it");
        f.deleteFile();
        check (p.writeArpMidi().existsAsFile(), "DRAG MIDI writes the arp line as a .mid");
    }

    // ---- 9. live use: switching presets without a restart plays each one as loud as loading it fresh ----
    {
        auto loudest = [&] (SkyzProcessor& q, const juce::String& name)
        {
            juce::AudioBuffer<float> buf (2, 256); std::vector<double> ring (2400, 0.0); size_t ri = 0; double win = 0, best = 0;
            for (int done = 0; done < (int) (1.8 * rate); done += 256)
            {
                juce::MidiBuffer m;
                if (done == 0) for (int n : notesFor (name)) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
                if (done == (int) (1.4 * rate) / 256 * 256) for (int n : notesFor (name)) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
                q.processBlock (buf, m);
                for (int i = 0; i < 256; ++i) { const double e = 0.5 * ((double) buf.getSample (0, i) * buf.getSample (0, i) + (double) buf.getSample (1, i) * buf.getSample (1, i));
                    win += e - ring[ri]; ring[ri] = e; ri = (ri + 1) % ring.size(); best = juce::jmax (best, win / (double) ring.size()); }
            }
            return (float) juce::Decibels::gainToDecibels (std::sqrt (best), -200.0);
        };
        SkyzProcessor liveP; liveP.prepareToPlay (rate, 256);
        float worst = 0; juce::String worstName;
        for (int k = 0; k < 40; ++k)
        {
            const int idx = (k * 137) % 555;
            liveP.setCurrentProgram (idx);
            const auto name = liveP.currentPresetName();
            const float a = loudest (liveP, name), b2 = loudest (liveP, name); // played twice in a row, no restart
            SkyzProcessor fresh; fresh.setCurrentProgram (idx); fresh.prepareToPlay (rate, 256);
            const float f = loudest (fresh, name);
            // a tail left by the previous play may add level; a state leak shows up as a sound getting QUIETER
            const float d = juce::jmax (juce::jmax (f - a, f - b2), juce::jmax (a, b2) - f - 3.0f);
            if (d > worst) { worst = d; worstName = name; }
        }
        check (worst < 4.0f, "40 presets switched live (no restart) are never quieter than a fresh load (worst gap " + juce::String (worst, 1) + " dB: " + worstName + ")");
    }

    // ---- 10. arp timing: with the DAW stopped the first note follows the key at once; with it playing, notes sit on the grid ----
    {
        double worstFree = 0, worstFreeQ = 0; bool onGrid = true;
        for (int k = 0; k < 30; ++k)
            for (float quant : { 0.0f, 0.25f })
            {
                Arp arp; arp.reset(); Values v; for (int i = 0; i < NUM_PARAMS; ++i) v.v[i] = paramDefs[i].def;
                v.v[P_arpOn] = 1; v.v[P_aQuant] = quant; v.v[P_aRate] = (float) (3 + (k % 4) * 2) / 16.0f; v.v[P_aSwing] = (k % 3) * 0.2f;
                const auto lanes = lanesFromPattern (3);
                const int keyAt = 777 + k * 1231; int firstOut = -1;
                juce::MidiBuffer m; m.ensureSize (8192);
                for (int done = 0; done < 96000 && firstOut < 0; done += 64)
                {
                    m.clear(); if (keyAt >= done && keyAt < done + 64) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), keyAt - done);
                    arp.process (m, 64, rate, 120.0, -1.0, v, lanes);
                    for (const auto e : m) if (e.getMessage().isNoteOn() && firstOut < 0) firstOut = done + e.samplePosition;
                }
                const double ms = (firstOut - keyAt) * 1000.0 / rate;
                (quant > 0 ? worstFreeQ : worstFree) = juce::jmax (quant > 0 ? worstFreeQ : worstFree, firstOut < 0 ? 999.0 : ms);
                // DAW playing: the first note lands on a 1/16 of the song (START QUANTIZE 1/16)
                Arp a2; a2.reset(); v.v[P_aSwing] = 0; v.v[P_aRate] = 3.0f / 16.0f; int firstHost = -1;
                for (int done = 0; done < 96000 && firstHost < 0; done += 64)
                {
                    m.clear(); if (keyAt >= done && keyAt < done + 64) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), keyAt - done);
                    a2.process (m, 64, rate, 120.0, done * 2.0 / rate, v, lanes);
                    for (const auto e : m) if (e.getMessage().isNoteOn() && firstHost < 0) firstHost = done + e.samplePosition;
                }
                const double beatPos = firstHost * 2.0 / rate / 0.25; // in sixteenths
                if (firstHost < keyAt || std::abs (beatPos - std::round (beatPos)) * 0.25 * rate / 2.0 > 1.5) onGrid = false;
            }
        check (worstFree < 1.0 && worstFreeQ < 1.0, "arp, DAW stopped: first note " + juce::String (worstFree, 2) + " ms after the key (with START QUANTIZE on: " + juce::String (worstFreeQ, 2) + " ms)");
        check (onGrid, "arp, DAW playing: the first note lands on the song's 1/16 grid");

        // no heap allocation in the arp while it plays (allocations on the audio thread cause crackles)
        Arp arp; arp.reset(); Values v; for (int i = 0; i < NUM_PARAMS; ++i) v.v[i] = paramDefs[i].def;
        v.v[P_arpOn] = 1; v.v[P_aQuant] = 0; v.v[P_aOct] = 1.0f; v.v[P_aRatchet] = 1.0f / 3.0f;
        const auto lanes = lanesFromPattern (13);
        juce::MidiBuffer m; m.ensureSize (16384);
        long allocs = 0;
        for (int mode = 0; mode < 12; ++mode)
        {
            v.v[P_aMode] = (float) mode / 11.0f;
            for (int done = 0; done < 48000 * 2; done += 64)
            {
                m.clear();
                if (done == 0) for (int n : { 48, 52, 55, 59, 62 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
                if (done == 48000) for (int n : { 52, 59 }) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
                g_allocs = 0; g_countAllocs = done > 4096; // after the first blocks
                arp.process (m, 64, rate, 120.0, -1.0, v, lanes);
                g_countAllocs = false; allocs += g_allocs.load();
            }
            juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allNotesOff (1), 0); arp.process (off, 64, rate, 120.0, -1.0, v, lanes);
        }
        {   // host stop sends All Notes Off: the arp must stop, even with keys it still thinks are held
            Arp a3; a3.reset(); v.v[P_aMode] = 0; int after = 0;
            for (int done = 0; done < 48000; done += 64)
            {
                juce::MidiBuffer b2;
                if (done == 0) b2.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
                if (done == 24000) b2.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                a3.process (b2, 64, rate, 120.0, -1.0, v, lanes);
                if (done > 24000) for (const auto e : b2) after += e.getMessage().isNoteOn() ? 1 : 0;
            }
            check (after == 0, "arp stops on All Notes Off (host stop): " + juce::String (after) + " notes after it (want 0)");
        }
        check (allocs == 0, "arp, all 12 modes with ratchets: " + juce::String (allocs) + " memory allocations while playing (want 0)");
    }

    // CPU: 8 voices of the heaviest kind (64 stars + 16 unison) for 10 s
    {
        p.setCurrentProgram (0);
        for (auto [id, x] : std::initializer_list<std::pair<const char*, float>> { { "aPart", 1.0f }, { "bUni", 1.0f }, { "bWave", 0.0f }, { "arpOn", 0.0f } })
            p.apvts.getParameter (id)->setValueNotifyingHost (x);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        play (p, { 48, 52, 55, 59, 60, 64, 67, 71 }, 10.0, 10.0, 0.0);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 6000.0, "8 voices x 64 stars x 16 unison, 10 s: " + juce::String (ms / 100.0, 1) + "% of one core");
    }

    std::cout << (failures == 0 ? "ALL PASS\n" : juce::String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}
