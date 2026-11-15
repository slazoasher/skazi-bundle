// Offline checks for the SKACID engine, optionally writing WAVs to listen to.
//   SkacidTest [render folder]
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include "../Source/AcidEngine.h"
#include "../Source/Presets.h"
#include "../Source/PresetTrims.h"

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

skacid::Pattern patternFrom (const std::map<juce::String, float>& v)
{
    skacid::Pattern p;
    for (int i = 0; i < skacid::NumSteps; ++i)
    {
        auto& st = p[(size_t) i];
        st.note = (int) v.at (skacid::stepId (i, "note"));
        st.oct = (int) v.at (skacid::stepId (i, "oct"));
        st.gate = (int) v.at (skacid::stepId (i, "gate"));
        st.acc = v.at (skacid::stepId (i, "acc")) > 0.5f;
        st.sld = v.at (skacid::stepId (i, "sld")) > 0.5f;
    }
    return p;
}

struct Result { float peak = 0, rms = 0; bool finite = true; int stepsSeen = 0; };

// Plays the preset's pattern with the host transport running for `seconds`
Result render (const std::map<juce::String, float>& values, double seconds, juce::AudioBuffer<float>* keep = nullptr,
               const juce::MidiBuffer* events = nullptr, bool hostPlaying = true)
{
    const double sr = 48000.0;
    const int block = 256;
    skacid::Engine e;
    e.prepare (sr, block);
    const auto s = skacid::Settings::fromValues ([&] (const char* id) { return values.at (id); });
    const auto pat = patternFrom (values);
    skazi::HostTime ht;
    ht.bpm = 132.0;
    ht.playing = hostPlaying;

    const int total = (int) (seconds * sr);
    if (keep) keep->setSize (2, total);

    // ARP presets get a held minor chord, ACID SOUNDS presets a played riff (some notes overlap, so they slide)
    juce::MidiBuffer played;
    if (events == nullptr && s.mode == skacid::ModeArp)
        for (int note : { 48, 51, 55 })
            played.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
    if (events == nullptr && s.mode == skacid::ModeMidi)
    {
        const int riff[] = { 36, 36, 48, 39, 36, 43, 46, 36 };
        const int eighth = (int) (sr * 60.0 / ht.bpm / 2.0);
        for (int k = 0; k * eighth < total; ++k)
        {
            const int note = riff[k % 8], at = k * eighth;
            const int len = k % 3 == 2 ? eighth + eighth / 4 : eighth / 2;
            played.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (k % 4 == 0 ? 112 : 80)), at);
            played.addEvent (juce::MidiMessage::noteOff (1, note), at + len);
        }
    }
    if (events == nullptr) events = &played;
    Result res;
    double sum = 0;
    int lastStep = -2;
    juce::AudioBuffer<float> buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        ht.ppq = pos / sr * ht.bpm / 60.0;
        juce::MidiBuffer slice;
        if (events != nullptr)
            for (const auto m : *events)
                if (m.samplePosition >= pos && m.samplePosition < pos + n)
                    slice.addEvent (m.getMessage(), m.samplePosition - pos);
        e.process (buf, slice, s, pat, ht);
        const int st = e.playStep.load();
        if (st != lastStep) { lastStep = st; ++res.stepsSeen; }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float y = buf.getSample (c, i);
                if (! std::isfinite (y)) res.finite = false;
                res.peak = juce::jmax (res.peak, std::abs (y));
                sum += (double) y * y;
            }
        if (keep)
            for (int c = 0; c < 2; ++c) keep->copyFrom (c, pos, buf, c, 0, n);
    }
    res.rms = (float) std::sqrt (sum / (2.0 * total));
    return res;
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 48000.0, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File()) outDir.createDirectory();

    const auto& list = skacid::presets();

    // --trim <header>: measure every preset and write the volume trims that bring them to -15 dB RMS
    if (argc > 2 && juce::String (argv[1]) == "--trim")
    {
        juce::String out = "#pragma once\n\n// Volume trims (dB) that bring every preset to about the same loudness. Regenerate with: SkacidTest --trim Source/PresetTrims.h\n"
                           "// (run it 3 times: each pass starts from the previous trims)\nnamespace skacid { inline constexpr float presetTrims[] = {\n";
        constexpr size_t numOld = sizeof (skacid::presetTrims) / sizeof (skacid::presetTrims[0]);
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto r = render (list[i].values, 6.0);
            const float db = juce::Decibels::gainToDecibels (r.rms, -100.0f);
            const float old = i < numOld ? skacid::presetTrims[i] : 0.0f;
            const float vol = list[i].values.at ("volume");
            const float target = juce::jlimit (-24.0f, 6.0f, vol + (-15.0f - db));
            const float trim = old + (target - vol);
            out << "    " << juce::String (trim, 2) << "f," << (i % 8 == 7 ? "\n" : "");
        }
        out << "\n}; }\n";
        juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).replaceWithText (out);
        std::printf ("wrote trims for %d presets\n", (int) list.size());
        return 0;
    }
    check (list.size() == 129, juce::String ((int) list.size()) + " presets");

    // Every preset plays, stays finite and under 0 dBFS, and is neither silent nor squashed
    {
        float lo = 1e9f, hi = -1e9f, worstPeak = 0;
        std::vector<float> peaks;
        juce::String loName, hiName;
        bool allOk = true;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto r = render (list[i].values, 4.0);
            const float db = juce::Decibels::gainToDecibels (r.rms, -100.0f);
            if (db < lo) { lo = db; loName = list[i].category + "/" + list[i].name; }
            if (db > hi) { hi = db; hiName = list[i].category + "/" + list[i].name; }
            worstPeak = juce::jmax (worstPeak, r.peak);
            peaks.push_back (r.peak);
            const bool stepsOk = list[i].values.at ("playMode") > 1.5f || r.stepsSeen > 16; // MIDI mode has no steps
            if (! r.finite || r.peak > 1.0f || ! stepsOk) std::printf ("       %s: finite %d peak %.3f steps %d\n", list[i].name.toRawUTF8(), (int) r.finite, r.peak, r.stepsSeen);
            allOk = allOk && r.finite && r.peak <= 1.0f && stepsOk;
        }
        check (allOk, "all presets finite, peak " + juce::String (worstPeak, 3) + ", sequencer advancing");
        std::printf ("       loudest %.1f dB RMS (%s), quietest %.1f dB RMS (%s)\n", hi, hiName.toRawUTF8(), lo, loName.toRawUTF8());
        std::sort (peaks.begin(), peaks.end());
        std::printf ("       peaks: median %.2f, 90%% %.2f\n", peaks[peaks.size() / 2], peaks[peaks.size() * 9 / 10]);
        check (lo > -36.0f && hi < -4.0f, "preset loudness within -36..-4 dB RMS");
    }

    if (std::getenv ("SKACID_DEBUG") != nullptr)
        for (int idx : { 0, 81, 82 })
        {
            auto v = list[(size_t) idx].values;
            auto show = [&] (const char* what) { const auto r = render (v, 3.0); std::printf ("  %3d %-12s rms %.3f peak %.3f\n", idx, what, r.rms, r.peak); };
            show ("as is");
            v["dlyMix"] = 0; v["revMix"] = 0; show ("no fx");
            v["distMix"] = 0; show ("no dist");
            v["accent"] = 0; show ("no accent");
            v["reso"] = 0; show ("no reso");
            v["fDrive"] = 0; show ("no fdrive");
        }

    // Host stopped, no keys: silence
    {
        const auto r = render (list[0].values, 1.0, nullptr, nullptr, false);
        check (r.peak < 1.0e-4f, "silent when the host is stopped and no key is held");
    }

    // Sequencer off: plays MIDI notes, legato slides
    {
        auto v = list[0].values;
        v["playMode"] = skacid::ModeMidi;
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 80), 6000);
        m.addEvent (juce::MidiMessage::noteOff (1, 36), 9000);
        m.addEvent (juce::MidiMessage::noteOff (1, 48), 12000);
        juce::AudioBuffer<float> b;
        const auto r = render (v, 0.6, &b, &m, false);
        check (r.finite && r.rms > 0.01f, "MIDI mode plays notes (rms " + juce::String (r.rms, 3) + ")");
        float tail = 0;
        for (int i = 20000; i < b.getNumSamples(); ++i) tail = juce::jmax (tail, std::abs (b.getSample (0, i)));
        check (tail < 0.02f, "MIDI mode note ends after note-off (tail " + juce::String (tail, 4) + ")");
    }

    // GLIDE on: a separate (not overlapping) note starts from the last pitch, so its start sounds different
    {
        auto v = list[0].values;
        v["playMode"] = skacid::ModeMidi;
        v["glide"] = 300;
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 90), 0);
        m.addEvent (juce::MidiMessage::noteOff (1, 36), 9000);
        m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90), 12000);
        juce::AudioBuffer<float> off, on;
        render (v, 0.4, &off, &m, false);
        v["glideOn"] = 1;
        render (v, 0.4, &on, &m, false);
        double diff = 0;
        for (int i = 12000; i < 15000; ++i) diff += std::abs (on.getSample (0, i) - off.getSample (0, i));
        check (diff / 3000.0 > 0.005, "GLIDE on changes the start of the next note (mean diff " + juce::String (diff / 3000.0, 4) + ")");
    }

    // Live mutation changes steps but keeps the pattern itself
    {
        auto v = list[0].values;
        v["mutate"] = 100;
        skacid::Engine e;
        e.prepare (48000.0, 256);
        const auto s = skacid::Settings::fromValues ([&] (const char* id) { return v.at (id); });
        skazi::HostTime ht; ht.playing = true; ht.bpm = 130;
        juce::AudioBuffer<float> b (2, 256);
        uint32_t seen = 0;
        for (int k = 0; k < 400; ++k) { ht.ppq = k * 256 / 48000.0 * 130 / 60.0; e.process (b, {}, s, patternFrom (v), ht); seen |= e.mutatedMask.load(); }
        check (seen != 0, "MUTATE 100% changes steps while playing");
    }

    // Pattern tools
    {
        juce::Random r (1);
        const auto p = skacid::patterns::generate (r, skacid::patterns::Classic);
        const auto s1 = skacid::patterns::shift (p, 1, 16);
        check (s1[1].note == p[0].note && s1[0].note == p[15].note && s1[0].gate == p[15].gate, "shift rotates the pattern");
        check (p[0].gate == skacid::Note, "generated patterns start on a note");
    }

    // Screaming resonance stays bounded, extreme everything stays finite
    {
        auto v = list[0].values;
        v["reso"] = 100; v["cutoff"] = 8000; v["envMod"] = 100; v["accent"] = 100; v["fDrive"] = 100;
        v["distDrive"] = 100; v["dlyFb"] = 90; v["dlyMix"] = 100; v["revMix"] = 100; v["volume"] = 6; v["ratchet"] = 100;
        bool ok = true; float pk = 0;
        for (int t = 0; t < 5; ++t)
        {
            v["distType"] = (float) t;
            const auto r = render (v, 3.0);
            ok = ok && r.finite && r.peak <= 1.0f;
            pk = juce::jmax (pk, r.peak);
        }
        check (ok, "everything at maximum, all 5 distortions: finite, peak " + juce::String (pk, 3));
    }

    if (outDir != juce::File())
    {
        for (int idx : { 0, 1, 17, 33, 49, 65, 69, 77, 85, 97, 101, 109, 117 })
        {
            juce::AudioBuffer<float> b;
            render (list[(size_t) idx].values, 8.0, &b);
            const auto f = outDir.getChildFile (juce::String (idx) + " " + list[(size_t) idx].category + " - " + list[(size_t) idx].name + ".wav");
            writeWav (f, b);
            std::printf ("       wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    }

    std::printf ("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
