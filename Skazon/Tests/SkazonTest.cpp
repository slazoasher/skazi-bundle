// SkazonTest            -> offline checks: tables, pitch, every preset renders sane audio, voices free up
// SkazonTest shot a.png -> renders the editor (preset 10 playing) to a PNG
#include "../Source/PluginProcessor.h"
#include "../Source/Theme.h"
#include "../Source/PluginEditor.h"

namespace
{
    constexpr double rate = 48000.0;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    // Plays the notes for holdSeconds, then renders until totalSeconds. Returns the left channel.
    // Drops a mod source on the knob for paramID through the real drag-and-drop calls. False if the knob refuses it.
    bool dropOnKnob (juce::Component& root, const juce::String& paramID, int source)
    {
        std::function<TargetKnob* (juce::Component&)> find = [&] (juce::Component& c) -> TargetKnob*
        {
            if (auto* k = dynamic_cast<TargetKnob*> (&c); k != nullptr && k->paramID == paramID) return k;
            for (auto* child : c.getChildren()) if (auto* k = find (*child)) return k;
            return nullptr;
        };
        auto* k = find (root);
        const juce::DragAndDropTarget::SourceDetails d ("mod:" + juce::String (source), nullptr, {});
        if (k == nullptr || ! k->isInterestedInDragSource (d)) return false;
        k->itemDropped (d);
        return true;
    }

    std::vector<float> render (SkazonAudioProcessor& p, std::initializer_list<int> notes, double holdSeconds, double totalSeconds)
    {
        p.prepareToPlay (rate, 512);
        std::vector<float> out;
        const int total = (int) (totalSeconds * rate), off = (int) (holdSeconds * rate);
        juce::AudioBuffer<float> buf (2, 512);
        for (int done = 0; done < total; done += 512)
        {
            juce::MidiBuffer midi;
            for (int n : notes)
            {
                if (done == 0) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.9f), 0);
                if (off >= done && off < done + 512) midi.addEvent (juce::MidiMessage::noteOff (1, n), off - done);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < 512; ++i)
                if (! std::isfinite (buf.getSample (0, i)) || ! std::isfinite (buf.getSample (1, i))) { out.assign (1, 1e9f); return out; }
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
        }
        return out;
    }

    float peak (const std::vector<float>& x, double from = 0, double to = 1e9)
    {
        float m = 0;
        for (size_t i = (size_t) (from * rate); i < x.size() && i < (size_t) (to * rate); ++i)
            m = juce::jmax (m, std::abs (x[i]));
        return m;
    }

    double freq (const std::vector<float>& x, double from, double to)
    {
        int crossings = 0;
        for (int i = (int) (from * rate) + 1; i < juce::jmin ((int) x.size(), (int) (to * rate)); ++i)
            crossings += (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f) ? 1 : 0;
        return crossings / (to - from);
    }

    // Loudest 300 ms window, RMS in dBFS.
    float loudness (const std::vector<float>& x)
    {
        const size_t w = (size_t) (0.3 * rate);
        double best = 1.0e-12;
        for (size_t a = 0; a + w <= x.size(); a += w / 2)
        {
            double sum = 0;
            for (size_t i = a; i < a + w; ++i)
                sum += (double) x[i] * x[i];
            best = juce::jmax (best, sum / (double) w);
        }
        return (float) (10.0 * std::log10 (best));
    }

    // A fake "recorded lead": additive saw at hz whose brightness falls over the note, with optional
    // vibrato and a detuned right channel for width. 40 ms attack, 0.3 s release tail.
    juce::File writeLead (const juce::String& fileName, double hz, double vibHz, double vibCents, bool wide)
    {
        const double sr = 44100.0, len = 2.0;
        juce::AudioBuffer<float> b (2, (int) (sr * len));
        double ph[2] {};
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double cents = t > 0.3 ? vibCents * std::sin (juce::MathConstants<double>::twoPi * vibHz * t) : 0.0;
            const double env = juce::jmin (1.0, t / 0.04) * (t < 1.7 ? 1.0 - 0.4 * juce::jmin (1.0, t / 0.5) : 0.6 * juce::jmax (0.0, 1.0 - (t - 1.7) / 0.3));
            const double bright = 1.0 - 0.7 * t / len;
            for (int c = 0; c < 2; ++c)
            {
                ph[c] += hz * std::exp2 ((cents + (wide && c == 1 ? 12.0 : 0.0)) / 1200.0) / sr;
                double v = 0;
                for (int k = 1; k <= 24; ++k)
                    v += std::sin (juce::MathConstants<double>::twoPi * k * ph[c]) / k * std::pow (bright, k - 1);
                b.setSample (c, i, (float) (0.3 * env * v));
            }
        }
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (fileName);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }

    // A fake "lead line": notes { midi, seconds } played back to back on a 3-voice detuned saw (supersaw-ish),
    // 10 ms attack, short gaps between notes.
    juce::File writePhrase (const juce::String& fileName, const std::vector<std::pair<int, double>>& line, double detuneCents)
    {
        const double sr = 44100.0;
        double total = 0.3;
        for (auto& n : line) total += n.second;
        juce::AudioBuffer<float> b (2, (int) (sr * total));
        b.clear();
        double t0 = 0.1;
        for (auto [note, dur] : line)
        {
            const double hz = 440.0 * std::exp2 ((note - 69) / 12.0);
            double ph[3] { 0.0, 0.31, 0.67 };
            const int a = (int) (t0 * sr), n = (int) ((dur - 0.03) * sr);
            for (int i = 0; i < n && a + i < b.getNumSamples(); ++i)
            {
                const double env = juce::jmin (1.0, i / (0.01 * sr)) * juce::jmin (1.0, (n - i) / (0.01 * sr));
                double v[2] {};
                for (int k = 0; k < 3; ++k)
                {
                    ph[k] += hz * std::exp2 ((k - 1) * detuneCents / 1200.0) / sr;
                    ph[k] -= std::floor (ph[k]);
                    const double saw = 2.0 * ph[k] - 1.0;
                    v[0] += saw * (k == 2 ? 0.6 : 1.0);
                    v[1] += saw * (k == 0 ? 0.6 : 1.0);
                }
                for (int c = 0; c < 2; ++c)
                    b.setSample (c, a + i, (float) (0.15 * env * v[c]));
            }
            t0 += dur;
        }
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (fileName);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }

    // Harmonic levels (dB, relative to the fundamental) of x around time t.
    std::vector<float> harmonics (const std::vector<float>& x, double sr, double hz, double t, int count)
    {
        std::vector<float> out;
        const int a = (int) (t * sr), n = (int) (0.1 * sr);
        float h1 = 0;
        for (int k = 1; k <= count; ++k)
        {
            double re = 0, im = 0;
            for (int i = 0; i < n && a + i < (int) x.size(); ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / n);
                re += x[(size_t) (a + i)] * w * std::cos (juce::MathConstants<double>::twoPi * k * hz * i / sr);
                im += x[(size_t) (a + i)] * w * std::sin (juce::MathConstants<double>::twoPi * k * hz * i / sr);
            }
            const float db = juce::Decibels::gainToDecibels ((float) std::sqrt (re * re + im * im), -200.0f);
            if (k == 1) h1 = db;
            out.push_back (db - h1);
        }
        return out;
    }

    void set (SkazonAudioProcessor& p, const char* id, float v)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    SkazonAudioProcessor p;

    if (argc > 2 && juce::String (argv[1]) == "shot")
    {
        p.setCurrentProgram (argc > 3 ? juce::String (argv[3]).getIntValue() : 10);
        p.uiPage = argc > 4 ? juce::String (argv[4]).getIntValue() : 0;
        if (argc > 5) // demo routings so the depth arcs show
        {
            set (p, "m15Src", skazon::sLfo1); set (p, "m15Dst", skazon::tCutoff); set (p, "m15Amt", 0.4f);
            set (p, "m16Src", skazon::sMacro1); set (p, "m16Dst", skazon::tAPos); set (p, "m16Amt", 0.5f);
            set (p, "aWarp", skazon::wBendUp); set (p, "aWarpAmt", 0.6f);
            set (p, "fxDrive", 0.6f); set (p, "distType", 6); set (p, "ottDepth", 0.5f);
        }
        render (p, { 60, 67 }, 2.0, 0.5);
        if (const char* skinArg = std::getenv ("SKAZON_SKIN"))
            Theme::applySkin (juce::String (skinArg).getIntValue());
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (1240, 880);
        if (argc > 5) // demo macro links: MACRO 2 on the amp attack and the reverb (knobs the matrix cannot reach)
        {
            dropOnKnob (*ed, "e1A", skazon::sMacro2);
            dropOnKnob (*ed, "fxReverb", skazon::sMacro2);
            set (p, "macro2", 0.6f);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << "wrote " << out.getFullPathName() << "\n";
        return 0;
    }

    // trim out.h -> level-match every preset to -16 dBFS (3 passes, distortion makes it nonlinear)
    if (argc > 2 && juce::String (argv[1]) == "trim")
    {
        juce::String body;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            float trim = 0, level = 0;
            for (int pass = 0; pass < 3; ++pass)
            {
                set (p, "master", trim);
                level = loudness (render (p, { 48, 55, 60, 64 }, 2.5, 3.0));
                trim = juce::jlimit (-18.0f, 6.0f, trim + (-16.0f - level));
            }
            body << juce::String (trim, 1) << "f," << (i % 12 == 11 ? "\n        " : " ");
            std::cout << i << " " << p.getProgramName (i) << "  " << juce::String (level, 1) << " dB -> trim " << juce::String (trim, 1) << "\n";
        }
        juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])).replaceWithText (
            "#pragma once\n#include <vector>\n\n// Per-preset master trim in dB so presets play at a similar loudness.\n"
            "// Generated by: SkazonTest trim Source/PresetTrims.h   (ignored when the count doesn't match the preset list)\n"
            "namespace skazon\n{\n    inline const std::vector<float> presetTrimDb {\n        " + body.trimEnd() + "\n    };\n}\n");
        return 0;
    }

    // Band-limiting: the top mip of a square is a pure sine, mip 0 keeps the edges.
    {
        const auto& t = skazon::Wavetables::get();
        const float* top = t.frame (1, 0, skazon::numMips - 1);
        float topPeak = 1.0e-6f, err = 0;
        for (int i = 0; i < skazon::tableSize; ++i)
            topPeak = juce::jmax (topPeak, std::abs (top[i]));
        for (int i = 0; i < skazon::tableSize; ++i)
            err = juce::jmax (err, std::abs (top[i] / topPeak - std::sin (juce::MathConstants<float>::twoPi * (float) i / skazon::tableSize)));
        check (err < 0.05f, "top mip is a single sine (err " + juce::String (err, 3) + ")");
        check (skazon::Wavetables::mipFor (20000.0, 48000.0) == skazon::numMips - 1 && skazon::Wavetables::mipFor (20.0, 48000.0) == 0, "mip choice spans low to high notes");
        check (skazon::applyOp (skazon::opQuantize, 0.3f) == 0.25f && skazon::applyOp (skazon::opInvert, 0.5f) == -0.5f, "math ops");
    }

    p.setCurrentProgram (0);
    set (p, "fxReverb", 0);
    set (p, "aPos", 0);  // pure sine for a clean pitch count
    auto x = render (p, { 69 }, 1.0, 1.0);
    check (std::abs (freq (x, 0.2, 0.9) - 440.0) < 4.0, "A4 plays 440 Hz (" + juce::String (freq (x, 0.2, 0.9), 1) + ")");

    set (p, "aOct", 1);
    x = render (p, { 69 }, 1.0, 1.0);
    check (std::abs (freq (x, 0.2, 0.9) - 880.0) < 8.0, "octave knob +1 (" + juce::String (freq (x, 0.2, 0.9), 1) + ")");

    // MACRO LINKS: macro 1 on the octave knob (not a matrix target) moves it a full octave, survives save/reload, presets clear it.
    {
        p.setCurrentProgram (0);
        set (p, "fxReverb", 0);
        set (p, "aPos", 0);
        auto* oct = p.apvts.getParameter ("aOct");
        p.setMacroLink (0, "aOct", oct->convertTo0to1 (1.0f) - oct->convertTo0to1 (0.0f));
        set (p, "macro1", 1);
        x = render (p, { 69 }, 1.0, 1.0);
        check (std::abs (freq (x, 0.2, 0.9) - 880.0) < 8.0, "MACRO 1 linked to OCT moves it +1 (" + juce::String (freq (x, 0.2, 0.9), 1) + " Hz)");
        juce::MemoryBlock state;
        p.getStateInformation (state);
        p.clearMacroLinks();
        p.setStateInformation (state.getData(), (int) state.getSize());
        x = render (p, { 69 }, 1.0, 1.0);
        check (p.macroLinks().size() == 1 && std::abs (freq (x, 0.2, 0.9) - 880.0) < 8.0, "macro link comes back with the project");
        p.setCurrentProgram (0);
        check (p.macroLinks().empty(), "a factory preset clears macro links");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        check (dropOnKnob (*ed, "e1A", skazon::sMacro3) && dropOnKnob (*ed, "ottDepth", skazon::sMacro1) && dropOnKnob (*ed, "glide", skazon::sMacro4)
                   && p.macroLinks().size() == 3,
               "MACRO chips drop onto envelope, FX and glide knobs");
        check (! dropOnKnob (*ed, "e1A", skazon::sLfo1) && ! dropOnKnob (*ed, "macro1", skazon::sMacro2), "LFO refused on a non-target knob, macro refused on a macro");
        ed.reset();
        p.clearMacroLinks();
    }

    p.setCurrentProgram (0);
    set (p, "fxReverb", 0);
    x = render (p, { 60 }, 0.5, 2.0);
    check (peak (x, 0.1, 0.5) > 0.05f && peak (x, 1.5) < 1.0e-4f, "note sounds, then releases to silence");

    // Mod matrix: LFO 1 on pitch must move the frequency away from the dry pitch.
    set (p, "aPos", 0);
    set (p, "m1Src", 3);
    set (p, "m1Dst", 4);
    set (p, "m1Amt", 0.5f);
    set (p, "l1Shape", 3); // square: +12 / -12 semitones
    set (p, "l1Rate", 1);
    x = render (p, { 69 }, 1.0, 1.0);
    check (std::abs (freq (x, 0.05, 0.45) - 880.0) < 20.0, "LFO -> pitch through the matrix (" + juce::String (freq (x, 0.05, 0.45), 1) + " Hz, expect 880)");

    for (int i = 0; i < p.getNumPrograms(); ++i)
    {
        p.setCurrentProgram (i);
        x = render (p, { 48, 55, 60, 64 }, 1.5, 2.0);
        const float pk = peak (x);
        check (x.size() > 1 && pk > 0.02f && pk < 1.6f, "preset " + juce::String (i).paddedLeft ('0', 2) + " " + p.getProgramName (i) + " (peak " + juce::String (pk, 2) + ")");
    }

    // MAGIC dice: random presets in every category play sane audio.
    for (int c = 0; c < skazon::generatedCategories().size(); ++c)
        for (int k = 0; k < 3; ++k)
        {
            p.randomize (c);
            x = render (p, { 48, 55, 60, 64 }, 1.5, 2.0);
            const float pk = peak (x);
            check (x.size() > 1 && pk > 0.005f && pk < 1.05f, "random " + p.randomName + " (peak " + juce::String (pk, 2) + ")");
        }

    // Audio -> USER wavetable: a 220 Hz sine file becomes a table that plays at the note pitch.
    {
        juce::AudioBuffer<float> sine (1, 48000);
        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample (0, i, std::sin (juce::MathConstants<float>::twoPi * (float) i / 2048.0f)); // one cycle per frame
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("skazon_table.wav");
        file.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), 48000.0, 1, 24, {}, 0));
            w->writeFromAudioSampleBuffer (sine, 0, sine.getNumSamples());
        }
        p.setCurrentProgram (0);
        set (p, "fxReverb", 0);
        check (p.loadUserTable (file, "a") && (int) p.param ("aTable") == skazon::userTableIndex, "WAV dropped on OSC A becomes the USER table");
        x = render (p, { 69 }, 1.0, 1.0);
        check (std::abs (freq (x, 0.2, 0.9) - 440.0) < 5.0, "USER table plays in tune (" + juce::String (freq (x, 0.2, 0.9), 1) + " Hz)");
    }

    // Mono legato: the second key glides the same voice, no new attack.
    {
        p.setCurrentProgram (0);
        set (p, "fxReverb", 0);
        set (p, "aPos", 0);
        set (p, "mono", 1);
        p.prepareToPlay (rate, 512);
        juce::AudioBuffer<float> buf (2, 512);
        std::vector<float> out;
        for (int b = 0; b < 200; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)  midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);
            if (b == 50) midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.9f), 0);
            p.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
        }
        check (std::abs (freq (out, 0.1, 0.5) - 220.0) < 4.0 && std::abs (freq (out, 1.0, 2.0) - 440.0) < 5.0, "mono legato moves to the new key");
        check (p.activeVoices.load() == 1, "mono uses one voice (" + juce::String (p.activeVoices.load()) + ")");
        set (p, "mono", 0);
    }

    // Every distortion type, OTT, hyper, comb + notch filters and warps stay finite and bounded.
    for (int t = 0; t < skazon::distNames().size(); ++t)
    {
        p.setCurrentProgram (5);
        set (p, "fxDrive", 0.8f);
        set (p, "distType", (float) t);
        set (p, "ottDepth", 1.0f);
        set (p, "hyMix", 0.7f);
        set (p, "hyDim", 0.7f);
        set (p, "phMix", 0.5f);
        set (p, "eqLow", 6);
        set (p, "fType", (float) (t % 2 == 0 ? skazon::fComb : skazon::fNotch));
        set (p, "fRes", 0.9f);
        set (p, "aWarp", (float) (1 + t % (skazon::numWarps - 1)));
        set (p, "aWarpAmt", 0.8f);
        set (p, "bOn", 1);
        x = render (p, { 48, 55, 60, 64 }, 1.0, 1.5);
        const float pk = peak (x);
        check (x.size() > 1 && pk > 0.01f && pk <= 1.0001f, "dist " + skazon::distNames()[t] + " + OTT + hyper + " + skazon::warpNames()[1 + t % (skazon::numWarps - 1)] + " (peak " + juce::String (pk, 2) + ")");
    }

    // SOUND MATCH: a recorded lead comes back as a patch with the same pitch, timbre, envelope and vibrato.
    {
        const auto plain = writeLead ("skazon_match_plain.wav", 330.0, 0, 0, false);
        auto m = p.soundMatch (plain);
        check (m.ok, "sound match analyses a lead note" + (m.ok ? juce::String() : ": " + m.error));
        check (std::abs (m.f0 - 330.0f) < 3.0f && m.midiNote == 64, "detects the pitch (" + juce::String (m.f0, 1) + " Hz, note " + juce::String (m.midiNote) + ")");
        check (m.attack > 0.015f && m.attack < 0.09f, "measures the attack (" + juce::String (m.attack * 1000.0f, 0) + " ms, real 40)");
        check (m.vibratoHz == 0.0f && m.width < 0.12f, "no vibrato / width invented on a plain mono note");
        x = render (p, { 64 }, 1.5, 1.6);
        double played = 0;
        {
            juce::AudioBuffer<float> rb (2, (int) x.size());
            for (size_t i = 0; i < x.size(); ++i) { rb.setSample (0, (int) i, x[i]); rb.setSample (1, (int) i, x[i]); }
            played = skazon::matchSound (rb, rate, "render").f0; // YIN; zero crossings miscount bright waves
        }
        check (std::abs (played - 329.63) < 4.0, "matched patch plays at the original pitch (" + juce::String (played, 1) + " Hz)");

        juce::AudioBuffer<float> orig;
        double origRate = 0;
        SkazonAudioProcessor::readAudio (plain, orig, origRate);
        std::vector<float> o (orig.getReadPointer (0), orig.getReadPointer (0) + orig.getNumSamples());
        for (double t : { 0.2, 1.0 })
        {
            const auto ho = harmonics (o, origRate, 330.0, t, 8);
            const auto hm = harmonics (x, rate, played, t, 8);
            float diff = 0;
            for (size_t k = 1; k < ho.size(); ++k)
                diff += std::abs (ho[k] - hm[k]) / (float) (ho.size() - 1);
            check (diff < 3.0f, "timbre at " + juce::String (t, 1) + " s matches (harmonics 2-8 off by " + juce::String (diff, 1) + " dB on average)");
        }

        const auto lively = writeLead ("skazon_match_vibrato.wav", 220.0, 5.5, 35.0, true);
        m = p.soundMatch (lively);
        check (m.ok && std::abs (m.vibratoHz - 5.5f) < 1.0f && m.vibratoCents > 15.0f && m.vibratoCents < 60.0f,
               "finds the vibrato (" + juce::String (m.vibratoHz, 1) + " Hz, " + juce::String (m.vibratoCents, 0) + " cents; real 5.5 Hz, 35)");
        check (m.width > 0.12f && (int) p.param ("aUni") > 1, "wide stereo -> unison (" + juce::String ((int) p.param ("aUni")) + " voices)");
        check (std::abs (m.f0 - 220.0f) < 3.0f, "pitch through vibrato (" + juce::String (m.f0, 1) + " Hz)");

        juce::AudioBuffer<float> noiseOnly (1, 44100);
        juce::Random rnd (3);
        for (int i = 0; i < noiseOnly.getNumSamples(); ++i)
            noiseOnly.setSample (0, i, rnd.nextFloat() * 0.6f - 0.3f);
        check (! skazon::matchSound (noiseOnly, 44100.0, "noise").ok, "refuses pure noise with a clear message");

        // A whole lead LINE on a detuned supersaw -> the right MIDI notes, a patch, a .mid and a preview.
        const std::vector<std::pair<int, double>> line { { 64, 0.4 }, { 67, 0.4 }, { 71, 0.8 }, { 69, 0.4 }, { 67, 0.4 }, { 64, 0.6 }, { 62, 0.3 }, { 64, 1.2 } };
        for (double detune : { 0.0, 18.0 })
        {
            const auto phrase = writePhrase ("skazon_match_line.wav", line, detune);
            m = p.soundMatch (phrase);
            juce::String got;
            for (auto& n : m.notes) got << n.pitch << " ";
            bool same = m.ok && m.notes.size() == line.size();
            for (size_t i = 0; same && i < line.size(); ++i)
                same = m.notes[i].pitch == line[i].first;
            check (same, "line -> MIDI (detune " + juce::String (detune, 0) + " c): got " + got + (m.ok ? juce::String() : "/ " + m.error));
            check (m.ok && m.attack < 0.05f && m.values.size() > 0, "envelope survives unison beating (attack " + juce::String (m.attack * 1000.0f, 0) + " ms, real 10)");
            check (m.ok && m.midiNote == 64, "timbre from the longest note (E4 = 64, got " + juce::String (m.midiNote) + ", at " + juce::String (m.heroStart, 2) + " s)");
            x = render (p, { 64 }, 1.0, 1.1);
            juce::AudioBuffer<float> out (1, (int) x.size());
            out.copyFrom (0, 0, x.data(), (int) x.size());
            const double played = skazon::matchSound (out, rate, "render").f0; // YIN: zero crossings can't read unison saws
            check (std::abs (played - 329.63) < 6.0, "line patch plays E4 in tune (" + juce::String (played, 1) + " Hz)");
        }
        const auto mid = p.writeMatchMidi();
        juce::MidiFile mf;
        juce::FileInputStream in (mid);
        check (mid.existsAsFile() && in.openedOk() && mf.readFrom (in) && mf.getNumTracks() == 1, "writes the .mid for drag-out");
        int ons = 0;
        if (mf.getNumTracks() == 1)
            for (auto* e : *mf.getTrack (0))
                ons += e->message.isNoteOn() ? 1 : 0;
        check (ons == (int) line.size(), "the .mid has every note (" + juce::String (ons) + ")");
        p.setPreview (true);
        x = render (p, {}, 2.0, 2.0);
        p.setPreview (false);
        check (peak (x) > 0.01f, "MIDI button plays the line through the patch");

        check (skazon::divNames()[13] == "16 BAR" && skazon::divBeats (13) == 64.0 && skazon::divBeats (11) == 16.0
               && p.apvts.getParameter ("l1Div")->getNumSteps() == 14 && p.apvts.getParameter ("fxDelayDiv")->getNumSteps() == 12,
               "LFO sync reaches 16 bars (old indices kept, delay stays at 4)");

        // On-screen wheels: BEND 24 + full pitch wheel up = two octaves up; mod wheel reaches the WHEEL source.
        p.setCurrentProgram (0);
        set (p, "bendRange", 24.0f);
        p.uiBend = 16383;
        p.uiModWheel = 127;
        x = render (p, { 57 }, 1.0, 1.1);
        {
            juce::AudioBuffer<float> out (1, (int) x.size());
            out.copyFrom (0, 0, x.data(), (int) x.size());
            const float hz = skazon::matchSound (out, rate, "bend").f0;
            check (std::abs (hz - 880.0f) < 12.0f, "pitch wheel +24 st (A3 220 Hz -> " + juce::String (hz, 1) + " Hz, expect 880)");
        }
        check (p.shownModWheel.load() == 127 && p.shownBend.load() == 16383, "wheels report back for the display");
        {
            // Mod wheel out of the box = vibrato: LFO 1 x wheel on PITCH.
            p.setCurrentProgram (0);
            p.uiModWheel = 127;
            float most = 0;
            for (int k = 0; k < 6; ++k)
            {
                render (p, { 60 }, 0.2, 0.2);
                most = juce::jmax (most, std::abs (p.liveMods[skazon::tPitch].load()));
            }
            check (most > 0.005f && most < 0.05f, "mod wheel -> LFO 1 -> pitch by default (depth " + juce::String (most * 24.0f, 2) + " st)");
        }
        p.uiBend = 8192;
        p.uiModWheel = 0;
        render (p, {}, 0.1, 0.1);

        p.setCurrentProgram (5);
        p.mutate (0.35f, SkazonAudioProcessor::lockFx);
        x = render (p, { 48, 55, 60, 64 }, 1.0, 1.5);
        check (x.size() > 1 && peak (x) > 0.01f && p.param ("fxReverb") == 0.3f, "MUTATE changes the patch and keeps locked FX");
    }

    // CPU: 8 voices of the heaviest preset in real time.
    p.setCurrentProgram (5);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    render (p, { 48, 52, 55, 59, 60, 64, 67, 71 }, 4.0, 4.0);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    check (ms < 4000.0, "8 voices of Supersaw Gold: " + juce::String (ms / 40.0, 1) + "% of one core");

    std::cout << (failures == 0 ? "ALL PASS\n" : juce::String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}
