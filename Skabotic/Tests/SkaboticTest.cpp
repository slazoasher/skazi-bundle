// SKABOTIC offline checks.
//   SkaboticTest test [voice.wav]      run every preset, check output, write renders/NN_NAME.wav
//   SkaboticTest shot out.png [preset] render the editor after playing the test voice
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    // A fake singer: glottal pulses gliding 110-220 Hz through moving vowel formants, with gaps and an "s".
    juce::AudioBuffer<float> makeVoice (double seconds)
    {
        const int n = (int) (seconds * sr);
        juce::AudioBuffer<float> b (2, n);
        const float vowels[4][3] { { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
        bot::Biquad f[3];
        juce::Random rnd (7);
        double phase = 0.0;
        int lastVowel = -1;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const int syl = (int) (t / 0.5);
            const double inSyl = std::fmod (t, 0.5);
            const int v = syl % 4;
            if (v != lastVowel)
            {
                for (int k = 0; k < 3; ++k)
                    f[k].bandpass (sr, vowels[v][k], 8.0);
                lastVowel = v;
            }
            const double hz = 110.0 * std::pow (2.0, (syl % 5) / 4.0) * (1.0 + 0.01 * std::sin (t * 2 * juce::MathConstants<double>::pi * 5.5));
            phase += hz / sr;
            float pulse = 0.0f;
            if (phase >= 1.0) { phase -= 1.0; pulse = 1.0f; }
            float s = 0.0f;
            for (int k = 0; k < 3; ++k)
                s += f[k].process (pulse * 6.0f) * (k == 0 ? 1.0f : 0.5f);
            const float env = inSyl < 0.38 ? (float) std::sin (juce::MathConstants<double>::pi * inSyl / 0.38) : 0.0f;
            float y = s * env;
            if (syl % 3 == 2 && inSyl > 0.38 && inSyl < 0.48)
                y += (rnd.nextFloat() * 2 - 1) * 0.06f; // "s"
            b.setSample (0, i, y);
            b.setSample (1, i, y);
        }
        b.applyGain (0.3f / juce::jmax (1e-6f, b.getMagnitude (0, n)));
        return b;
    }

    juce::AudioBuffer<float> loadWav (const juce::File& file)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
        if (r == nullptr)
            return {};
        juce::AudioBuffer<float> b (2, (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (sr * 12)));
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        return b;
    }

    float rmsDb (const juce::AudioBuffer<float>& b)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (0, 0, b.getNumSamples()), -120.0f);
    }

    bool run (SkaboticAudioProcessor& p, juce::AudioBuffer<float>& io, bool withMidi = false)
    {
        juce::MidiBuffer none, chord;
        chord.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), 0);
        chord.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        chord.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);
        for (int pos = 0; pos < io.getNumSamples(); pos += block)
        {
            const int len = juce::jmin (block, io.getNumSamples() - pos);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, len);
            auto& midi = (withMidi && pos == 0) ? chord : none;
            p.processBlock (view, midi);
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < io.getNumSamples(); ++i)
                if (! std::isfinite (io.getSample (c, i)))
                    return false;
        return true;
    }

    void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        if (w != nullptr)
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "test";
    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (mode == "shot")
    {
        SkaboticAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (sr, block);
        proc.prepareToPlay (sr, block);
        if (argc > 3) proc.setCurrentProgram (juce::String (argv[3]).getIntValue());
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto voice = makeVoice (1.2);
        for (int pos = 0; pos + block <= voice.getNumSamples(); pos += block)
        {
            juce::AudioBuffer<float> view (voice.getArrayOfWritePointers(), 2, pos, block);
            juce::MidiBuffer m;
            proc.processBlock (view, m);
            if (pos % (block * 4) == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto out = cwd.getChildFile (argc > 2 ? argv[2] : "skabotic.png");
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    };

    juce::AudioBuffer<float> voice = argc > 2 ? loadWav (juce::File (argv[2])) : makeVoice (4.0);
    if (voice.getNumSamples() == 0) { std::printf ("could not read input\n"); return 1; }
    const float inDb = rmsDb (voice);
    const auto renders = cwd.getChildFile ("renders");
    renders.createDirectory();
    writeWav (renders.getChildFile ("00_INPUT.wav"), voice);

    // Pitch tracker on a steady 150 Hz pulse.
    {
        SkaboticAudioProcessor p;
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
        juce::AudioBuffer<float> tone (2, (int) sr);
        for (int i = 0; i < tone.getNumSamples(); ++i)
        {
            const float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 150.0 * i / sr)
                          + 0.2f * (float) std::sin (2 * juce::MathConstants<double>::pi * 300.0 * i / sr);
            tone.setSample (0, i, v); tone.setSample (1, i, v);
        }
        run (p, tone);
        check (std::abs (p.voiceHz.load() - 150.0f) < 3.0f, "pitch tracker reads 150 Hz: " + juce::String (p.voiceHz.load(), 1));
    }

    // Silence stays silent (gate + no carrier leak).
    {
        SkaboticAudioProcessor p;
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
        juce::AudioBuffer<float> silent (2, (int) sr);
        silent.clear();
        run (p, silent);
        check (silent.getMagnitude (0, silent.getNumSamples()) < 1e-4f, "silence in -> silence out");
    }

    auto setParam = [] (SkaboticAudioProcessor& p, const char* id, float v)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    };
    auto sine = [] (double hz, double seconds)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * sr));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * hz * i / sr)
                          + 0.15f * (float) std::sin (2 * juce::MathConstants<double>::pi * hz * 2 * i / sr);
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        return b;
    };
    auto fresh = [] (SkaboticAudioProcessor& p) { p.setRateAndBufferSizeDetails (sr, block); p.prepareToPlay (sr, block); };
    auto midiNote = [] (float hz) { return 12.0f * std::log2 (hz / 440.0f) + 69.0f; };

    // MIDI MONO plays the newest key: hold A3 then press C4 -> carrier on C4.
    {
        SkaboticAudioProcessor p;
        setParam (p, ParamIDs::source, 4);
        setParam (p, ParamIDs::glide, 0);
        fresh (p);
        auto io = sine (150.0, 0.2);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 10);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, 0, block);
        p.processBlock (view, m);
        for (int pos = block; pos + block <= io.getNumSamples(); pos += block)
        {
            juce::AudioBuffer<float> v2 (io.getArrayOfWritePointers(), 2, pos, block);
            juce::MidiBuffer none;
            p.processBlock (v2, none);
        }
        check (std::abs (midiNote (p.carrierHz.load()) - 60.0f) < 0.1f, "MIDI MONO plays newest key (C4): " + juce::String (midiNote (p.carrierHz.load()), 2));
    }

    // MIDI POLY with no keys follows the voice.
    {
        SkaboticAudioProcessor p;
        setParam (p, ParamIDs::source, 3);
        fresh (p);
        auto io = sine (200.0, 1.0);
        run (p, io);
        check (std::abs (p.carrierHz.load() - 200.0f) < 4.0f, "MIDI POLY with no keys follows the voice: " + juce::String (p.carrierHz.load(), 1));
    }

    // AUTO-TUNE in C MAJOR: 158 Hz (between D#3 and E3) snaps to E3 = 164.8 Hz; chromatic would give D#3.
    {
        SkaboticAudioProcessor p;
        setParam (p, ParamIDs::source, 2);
        setParam (p, ParamIDs::scale, 1);
        fresh (p);
        auto io = sine (158.0, 1.0);
        run (p, io);
        check (std::abs (p.carrierHz.load() - 164.81f) < 1.0f, "AUTO-TUNE C major snaps 158 Hz to E3: " + juce::String (p.carrierHz.load(), 1));
    }

    // NOTE mode holds one note whatever the voice does.
    {
        SkaboticAudioProcessor p;
        setParam (p, ParamIDs::note, 50);
        fresh (p);
        auto io = sine (230.0, 0.5);
        run (p, io);
        check (std::abs (midiNote (p.carrierHz.load()) - 50.0f) < 0.05f, "NOTE mode holds D3");
    }

    // FREQ SHIFT +100 Hz moves a 1 kHz tone up to 1.1 kHz, not down.
    {
        SkaboticAudioProcessor p;
        setParam (p, ParamIDs::vocode, 0);
        setParam (p, ParamIDs::shift, 100);
        fresh (p);
        juce::AudioBuffer<float> io (2, (int) sr);
        for (int i = 0; i < io.getNumSamples(); ++i)
        {
            const float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * i / sr);
            io.setSample (0, i, v); io.setSample (1, i, v);
        }
        run (p, io);
        auto power = [&io] (double hz)
        {
            double re = 0, im = 0;
            for (int i = io.getNumSamples() / 2; i < io.getNumSamples(); ++i)
            {
                re += io.getSample (0, i) * std::cos (2 * juce::MathConstants<double>::pi * hz * i / sr);
                im += io.getSample (0, i) * std::sin (2 * juce::MathConstants<double>::pi * hz * i / sr);
            }
            return re * re + im * im;
        };
        const double up = power (1100.0), down = power (900.0), orig = power (1000.0);
        check (up > 100.0 * down && up > 10.0 * orig, "FREQ SHIFT +100 Hz goes up (1100 vs 900: " + juce::String (10.0 * std::log10 (up / juce::jmax (1e-12, down)), 1) + " dB)");
    }

    // WIDTH: 200% makes it wider, keeps the mono sum identical; 0% is pure mono.
    {
        auto render = [&] (float widthPct)
        {
            SkaboticAudioProcessor p;
            setParam (p, ParamIDs::army, 50);
            setParam (p, ParamIDs::width, widthPct);
            fresh (p);
            auto io = voice;
            run (p, io);
            return io;
        };
        auto sideRms = [] (const juce::AudioBuffer<float>& b)
        {
            double s = 0;
            for (int i = 0; i < b.getNumSamples(); ++i) { const double d = 0.5 * (b.getSample (0, i) - b.getSample (1, i)); s += d * d; }
            return std::sqrt (s / b.getNumSamples());
        };
        const auto normal = render (100), wide = render (200), mono = render (0);
        double monoDiff = 0;
        for (int i = 0; i < normal.getNumSamples(); ++i)
            monoDiff = juce::jmax (monoDiff, (double) std::abs ((normal.getSample (0, i) + normal.getSample (1, i)) - (wide.getSample (0, i) + wide.getSample (1, i))));
        check (sideRms (wide) > 2.0 * sideRms (normal), "WIDTH 200% widens (side x" + juce::String (sideRms (wide) / juce::jmax (1e-9, sideRms (normal)), 1) + ")");
        check (monoDiff < 1e-4, "WIDTH 200% keeps the mono sum (max diff " + juce::String (monoDiff, 7) + ")");
        check (sideRms (mono) < 1e-6, "WIDTH 0% is mono");
    }

    for (int i = 0; i < 200 && failures < 50; ++i)
    {
        SkaboticAudioProcessor p;
        if (i >= p.getNumPrograms()) break;
        p.setCurrentProgram (i);
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
        auto io = voice;
        const bool midi = p.getProgramName (i).contains ("MIDI");
        const bool finite = run (p, io, midi);
        const float outDb = rmsDb (io), peak = io.getMagnitude (0, io.getNumSamples());
        check (finite && peak < 4.0f && outDb > inDb - 12.0f && outDb < inDb + 12.0f,
               juce::String (i).paddedLeft ('0', 2) + " " + p.getProgramName (i).paddedRight (' ', 20)
                   + " rms " + juce::String (outDb, 1) + " dB (in " + juce::String (inDb, 1) + ")  peak " + juce::String (peak, 2));
        writeWav (renders.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + "_" + p.getProgramName (i).replaceCharacter (' ', '_') + ".wav"), io);
    }

    // 20 random rolls must stay finite and bounded.
    for (int i = 0; i < 20; ++i)
    {
        SkaboticAudioProcessor p;
        p.randomise();
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
        auto io = voice;
        const bool finite = run (p, io);
        check (finite && io.getMagnitude (0, io.getNumSamples()) < 4.0f, "random roll " + juce::String (i));
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
