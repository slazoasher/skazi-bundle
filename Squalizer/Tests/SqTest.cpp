// Offline checks for S-QUALIZER, run against the processor directly.
//   SqTest                     responses per phase mode, M/S routing, dynamics, tube EQ, presets
//   SqTest shot <out.png> [p] [solo band]  editor screenshot after playing drums through it
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    void set (SqualizerAudioProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    void band (SqualizerAudioProcessor& p, int b, int type, float freq, float gain, float q, int channel = sq::stereo, int slope = 2, float dyn = 0, float thr = -24)
    {
        set (p, ParamIDs::band (b, "ON"), 1);
        set (p, ParamIDs::band (b, "TYPE"), (float) type);
        set (p, ParamIDs::band (b, "FREQ"), freq);
        set (p, ParamIDs::band (b, "GAIN"), gain);
        set (p, ParamIDs::band (b, "Q"), q);
        set (p, ParamIDs::band (b, "CH"), (float) channel);
        set (p, ParamIDs::band (b, "SLOPE"), (float) slope);
        set (p, ParamIDs::band (b, "DYN"), dyn);
        set (p, ParamIDs::band (b, "THR"), thr);
    }

    void fresh (SqualizerAudioProcessor& p, int phase)
    {
        p.setCurrentProgram (0);
        set (p, ParamIDs::phase, (float) phase);
    }

    void prepare (SqualizerAudioProcessor& p)
    {
        p.prepareToPlay (sr, block);
        if (p.phaseMode() == sq::linear)
        {
            // The convolution loads its new IR on a background thread; give it time, then flush.
            juce::AudioBuffer<float> silence (2, block);
            juce::MidiBuffer midi;
            for (int i = 0; i < 40; ++i)
            {
                juce::Thread::sleep (10);
                silence.clear();
                p.processBlock (silence, midi);
            }
        }
    }

    // Gain in dB of each output channel for a steady sine fed as (inL, inR) amplitudes.
    std::pair<double, double> sineGain (SqualizerAudioProcessor& p, double freq, float amp = 0.25f, float inL = 1, float inR = 1)
    {
        prepare (p);
        const int total = (int) sr * 2 + p.getLatencySamples();
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double inSum = 0, outL = 0, outR = 0;
        for (int pos = 0; pos < total; pos += block)
        {
            for (int i = 0; i < block; ++i)
            {
                const float s = amp * (float) std::sin (2 * sq::pi * freq * (pos + i) / sr);
                buf.setSample (0, i, s * inL);
                buf.setSample (1, i, s * inR);
            }
            p.processBlock (buf, midi);
            if (pos > total - (int) sr)
                for (int i = 0; i < block; ++i)
                {
                    const double s = amp * std::sin (2 * sq::pi * freq * (pos + i) / sr);
                    inSum += s * s;
                    outL += buf.getSample (0, i) * buf.getSample (0, i);
                    outR += buf.getSample (1, i) * buf.getSample (1, i);
                }
        }
        return { 10 * std::log10 ((outL + 1e-20) / inSum), 10 * std::log10 ((outR + 1e-20) / inSum) };
    }

    float drums (int n)
    {
        const int beat = (int) (sr * 60.0 / 140.0);
        const int inBeat = n % beat, beatIndex = n / beat;
        const float t = (float) inBeat / (float) sr;
        float v = std::sin (2.0f * 3.14159f * (50.0f * t + 120.0f * (1.0f - std::exp (-t * 30.0f)) / 30.0f)) * std::exp (-t * 9.0f) * 0.8f;
        juce::Random r (n);
        if (beatIndex % 2 == 1)
            v += (r.nextFloat() * 2.0f - 1.0f) * std::exp (-t * 18.0f) * 0.5f;
        v += (r.nextFloat() * 2.0f - 1.0f) * 0.04f;
        return v;
    }

    int shot (const juce::File& out, int preset, int solo)
    {
        juce::ScopedJuceInitialiser_GUI gui;
        SqualizerAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (sr, block);
        proc.prepareToPlay (sr, block);
        if (preset >= 0)
            proc.setCurrentProgram (preset);
        proc.soloBand = solo;

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setSize (1200, 1125);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        int pos = 0;
        for (int frame = 0; frame < 90; ++frame)
        {
            for (int b = 0; b < 3; ++b, pos += block)
            {
                for (int i = 0; i < block; ++i)
                    buf.setSample (0, i, drums (pos + i)), buf.setSample (1, i, buf.getSample (0, i) * 0.9f);
                proc.processBlock (buf, midi);
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        }

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            return 1;
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        editor.reset();
        return 0;
    }
}

int main (int argc, char* argv[])
{
    if (argc > 2 && juce::String (argv[1]) == "shot")
        return shot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), argc > 3 ? juce::String (argv[3]).getIntValue() : -1,
                     argc > 4 ? juce::String (argv[4]).getIntValue() : -1);

    juce::ScopedJuceInitialiser_GUI gui;
    SqualizerAudioProcessor p;
    p.setRateAndBufferSizeDetails (sr, block);

    // Flat is transparent in every mode.
    for (int phase : { sq::zeroLatency, sq::natural, sq::linear })
    {
        fresh (p, phase);
        const auto [l, r] = sineGain (p, 997);
        check (std::abs (l) < 0.05 && std::abs (r) < 0.05, "flat " + Names::phases[phase] + ": " + juce::String (l, 3) + " dB");
    }

    // Bell responses match what the display draws, and NATURAL keeps the analogue shape near Nyquist.
    for (int phase : { sq::zeroLatency, sq::natural, sq::linear })
        for (double f : { 100.0, 1000.0, 9000.0, 16000.0 })
        {
            fresh (p, phase);
            band (p, 4, sq::bell, 12000, 9, 1.0f);
            band (p, 1, sq::lowShelf, 120, -6, 0.71f);
            const auto measured = sineGain (p, f).first;
            const double predicted = 20 * std::log10 (sq::bandMagnitude (p.bandSettings (4), f, sr, phase) * sq::bandMagnitude (p.bandSettings (1), f, sr, phase));
            const double analogue = 20 * std::log10 (sq::bandMagnitude (p.bandSettings (4), f, sr, sq::linear) * sq::bandMagnitude (p.bandSettings (1), f, sr, sq::linear));
            check (std::abs (measured - predicted) < 0.25, Names::phases[phase] + " bell12k+shelf @" + juce::String (f) + " Hz: " + juce::String (measured, 2)
                                                             + " dB (drawn " + juce::String (predicted, 2) + ", analogue " + juce::String (analogue, 2) + ")");
        }

    // Cuts: 48 dB/oct low cut at 200 Hz.
    fresh (p, sq::natural);
    band (p, 0, sq::lowCut, 200, 0, 0.71f, sq::stereo, 4);
    check (sineGain (p, 50).first < -45, "48 dB low cut kills 50 Hz: " + juce::String (sineGain (p, 50).first, 1));
    check (std::abs (sineGain (p, 2000).first) < 0.1, "48 dB low cut passes 2 kHz");

    // M/S: a side low cut leaves mono lows alone and makes a hard-left low mono.
    for (int phase : { sq::natural, sq::linear })
    {
        fresh (p, phase);
        band (p, 0, sq::lowCut, 300, 0, 0.71f, sq::side, 4);
        const auto mono = sineGain (p, 50);
        const auto leftOnly = sineGain (p, 50, 0.25f, 1, 0);
        check (std::abs (mono.first) < 0.1 && std::abs (mono.second) < 0.1, Names::phases[phase] + " side cut keeps mono 50 Hz");
        check (std::abs (leftOnly.first + 6.02) < 0.3 && std::abs (leftOnly.second + 6.02) < 0.3,
               Names::phases[phase] + " side cut: hard-left 50 Hz -> centre (" + juce::String (leftOnly.first, 2) + " / " + juce::String (leftOnly.second, 2) + ")");
    }
    fresh (p, sq::zeroLatency);
    band (p, 2, sq::bell, 1000, -12, 1.0f, sq::right);
    const auto rOnly = sineGain (p, 1000);
    check (std::abs (rOnly.first) < 0.05 && std::abs (rOnly.second + 12) < 0.1, "RIGHT band touches only R");

    // Dynamics: a -6 dB dynamic cut acts on loud 1 kHz only.
    fresh (p, sq::natural);
    band (p, 2, sq::bell, 1000, 0, 2.0f, sq::stereo, 1, -6, -30);
    const double loud = sineGain (p, 1000, 0.5f).first, quiet = sineGain (p, 1000, 0.002f).first;
    check (loud < -5 && loud > -6.5, "dynamic cut on loud 1 kHz: " + juce::String (loud, 2) + " dB");
    check (std::abs (quiet) < 0.1, "dynamic cut leaves quiet 1 kHz: " + juce::String (quiet, 2) + " dB");

    // Tube EQ: the boost + atten trick at 60 Hz gives a sub lift and a dip above it.
    fresh (p, sq::natural);
    set (p, ParamIDs::tubeOn, 1);
    set (p, ParamIDs::drive, 0);
    set (p, ParamIDs::lowBoost, 10);
    set (p, ParamIDs::lowAtten, 10);
    juce::String curve;
    double sub = -99, dip = 99;
    for (double f : { 20.0, 30.0, 45.0, 60.0, 90.0, 130.0, 200.0, 300.0, 500.0, 1000.0, 3000.0 })
    {
        const double g = 20 * std::log10 (sq::tubeMagnitude (p.tubeSettings(), f, sr));
        curve << juce::String (f, 0) << ":" << juce::String (g, 1) << "  ";
        if (f <= 60) sub = std::max (sub, g);
        if (f >= 90 && f <= 500) dip = std::min (dip, g);
    }
    std::printf ("      tube trick 60 Hz  %s\n", curve.toRawUTF8());
    check (sub > 2 && dip < -1, "tube trick: sub peak " + juce::String (sub, 1) + " dB, dip " + juce::String (dip, 1) + " dB");
    check (std::abs (sineGain (p, 60).first - 20 * std::log10 (sq::tubeMagnitude (p.tubeSettings(), 60, sr))) < 0.3, "tube EQ audio matches its drawn curve");

    // HIGH CUT band really is a low-pass, whatever its TYPE parameter says.
    fresh (p, sq::natural);
    band (p, 5, sq::bell, 2000, 0, 0.71f, sq::stereo, 4);
    check (sineGain (p, 10000).first < -45, "HIGH CUT band cuts 10 kHz: " + juce::String (sineGain (p, 10000).first, 1));

    // Band solo: holding band 2 at 1 kHz passes 1 kHz and drops 100 Hz and 10 kHz.
    fresh (p, sq::natural);
    band (p, 2, sq::bell, 1000, 0, 1.0f);
    p.soloBand = 2;
    const double soloIn = sineGain (p, 1000).first, soloLow = sineGain (p, 100).first, soloHigh = sineGain (p, 10000).first;
    p.soloBand = -1;
    check (std::abs (soloIn) < 0.5 && soloLow < -30 && soloHigh < -30,
           "solo band 2: 1 kHz " + juce::String (soloIn, 2) + " dB, 100 Hz " + juce::String (soloLow, 1) + " dB, 10 kHz " + juce::String (soloHigh, 1) + " dB");

    // Tilt shelf: -g/2 well below the corner, +g/2 well above.
    fresh (p, sq::natural);
    band (p, 2, sq::tiltShelf, 1000, 6, 0.71f);
    const double tiltLow = sineGain (p, 60).first, tiltHigh = sineGain (p, 15000).first;
    check (std::abs (tiltLow + 3) < 0.4 && std::abs (tiltHigh - 3) < 0.4, "tilt +6: " + juce::String (tiltLow, 2) + " / " + juce::String (tiltHigh, 2) + " dB");

    // Slopes: one octave below a 1 kHz low cut, 6 dB/oct gives about -7 dB, 96 dB/oct more than -80 dB.
    for (auto [slope, lo, hi] : { std::tuple<int, double, double> { 0, -8.0, -6.0 }, { 6, -200.0, -80.0 } })
    {
        fresh (p, sq::natural);
        band (p, 0, sq::lowCut, 1000, 0, 0.71f, sq::stereo, slope);
        const double g = sineGain (p, 500).first;
        check (g > lo && g < hi, Names::slopes[slope] + " low cut, 1 octave down: " + juce::String (g, 1) + " dB");
    }
    // BRICK in LINEAR: flat just above the corner, gone just below.
    fresh (p, sq::linear);
    band (p, 0, sq::lowCut, 1000, 0, 0.71f, sq::stereo, sq::brickSlope);
    const double brickPass = sineGain (p, 1200).first, brickStop = sineGain (p, 800).first;
    check (std::abs (brickPass) < 0.3 && brickStop < -40, "BRICK linear: 1.2k " + juce::String (brickPass, 2) + " dB, 800 Hz " + juce::String (brickStop, 1) + " dB");

    // Monitor: MONO sums, SIDE keeps only the difference, polarity flips a channel.
    fresh (p, sq::natural);
    set (p, ParamIDs::monMode, 1);
    const auto monoLeft = sineGain (p, 1000, 0.25f, 1, 0);
    check (std::abs (monoLeft.first + 6.02) < 0.1 && std::abs (monoLeft.second + 6.02) < 0.1, "MONO: hard-left tone lands at -6 dB in both");
    set (p, ParamIDs::monMode, 2);
    check (sineGain (p, 1000).first < -100, "SIDE: a centred tone disappears");
    set (p, ParamIDs::monMode, 0);
    set (p, ParamIDs::monPolL, 1);
    set (p, ParamIDs::monMode, 1);
    check (sineGain (p, 1000).first < -100, "polarity L + MONO: a centred tone cancels");
    set (p, ParamIDs::monPolL, 0);
    set (p, ParamIDs::monMode, 0);

    // AUTO GAIN pulls a broad +6 dB boost back down; GAIN SCALE 50% halves a band.
    fresh (p, sq::natural);
    band (p, 2, sq::bell, 1000, 6, 0.3f);
    p.updateAutoGain();
    check (p.autoGainDb.load() < -2.0f && p.autoGainDb.load() > -6.5f, "auto gain for a wide +6 dB bell: " + juce::String (p.autoGainDb.load(), 2) + " dB");
    band (p, 2, sq::bell, 1000, 12, 1.0f);
    set (p, ParamIDs::gainScale, 50);
    check (std::abs (sineGain (p, 1000).first - 6.0) < 0.2, "gain scale 50%: +12 dB band plays +6");
    set (p, ParamIDs::gainScale, 100);

    // Sidechain: an EXTERNAL dynamic band ducks when the sidechain is loud, not when the main input is.
    {
        fresh (p, sq::natural);
        band (p, 2, sq::bell, 1000, 0, 2.0f, sq::stereo, 2, -6, -30);
        set (p, ParamIDs::band (2, "SC"), 1);
        p.getBus (true, 1)->enable (true);
        p.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (4, block);
        juce::MidiBuffer midi;
        auto run = [&] (float mainAmp, float scAmp)
        {
            double in = 0, out = 0;
            for (int pos = 0; pos < (int) sr * 2; pos += block)
            {
                for (int i = 0; i < block; ++i)
                {
                    const float v = (float) std::sin (2 * sq::pi * 1000.0 * (pos + i) / sr);
                    buf.setSample (0, i, mainAmp * v);
                    buf.setSample (1, i, mainAmp * v);
                    buf.setSample (2, i, scAmp * v);
                    buf.setSample (3, i, scAmp * v);
                }
                p.processBlock (buf, midi);
                if (pos > (int) sr)
                    for (int i = 0; i < block; ++i)
                    {
                        in += mainAmp * mainAmp * 0.5;
                        out += buf.getSample (0, i) * buf.getSample (0, i);
                    }
            }
            return 10 * std::log10 (out / (in * 1.0 / 1.0) + 1e-20);
        };
        const double keyed = run (0.01f, 0.5f), quietKey = run (0.5f, 0.0f);
        check (keyed < -5 && std::abs (quietKey) < 0.2, "sidechain: loud key ducks quiet input " + juce::String (keyed, 2) + " dB, silent key leaves loud input " + juce::String (quietKey, 2) + " dB");
        p.getBus (true, 1)->enable (false);
    }

    // A/B and undo.
    fresh (p, sq::natural);
    set (p, ParamIDs::output, -3);
    p.switchSlot (1);
    set (p, ParamIDs::output, 5);
    p.switchSlot (0);
    const float aOut = p.apvts.getRawParameterValue (ParamIDs::output)->load();
    p.switchSlot (1);
    const float bOut = p.apvts.getRawParameterValue (ParamIDs::output)->load();
    p.switchSlot (0);
    check (std::abs (aOut + 3) < 0.01f && std::abs (bOut - 5) < 0.01f, "A/B keeps two settings: A " + juce::String (aOut) + " / B " + juce::String (bOut));
    // The parameter state reaches the undo history on JUCE's state timer (up to 500 ms when idle), so let it run between steps.
    auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (700); };
    settle();
    p.undoManager.beginNewTransaction();
    set (p, ParamIDs::output, 7);
    settle();
    p.undoManager.beginNewTransaction();
    p.undoManager.undo();
    settle();
    check (std::abs (p.apvts.getRawParameterValue (ParamIDs::output)->load() + 3) < 0.01f, "undo brings OUTPUT back: " + juce::String (p.apvts.getRawParameterValue (ParamIDs::output)->load()) + (p.undoManager.canUndo() ? " (more to undo)" : ""));

    // Stress: random settings at every common rate and odd block sizes, phase-mode flips and state reloads mid-stream.
    // Output must stay finite and bounded (+36 dB of total boost is the most the controls can add).
    {
        juce::Random rnd (1234);
        bool finite = true;
        float worst = 0.0f;
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            const int maxBlock = 4096;
            p.setRateAndBufferSizeDetails (rate, maxBlock);
            p.prepareToPlay (rate, maxBlock);
            juce::AudioBuffer<float> buf (2, maxBlock);
            juce::MidiBuffer midi;
            int pos = 0;
            for (int round = 0; round < 24; ++round)
            {
                for (auto* prm : p.getParameters())
                    if (rnd.nextFloat() < 0.5f && ! dynamic_cast<juce::RangedAudioParameter*> (prm)->getParameterID().startsWith ("MON_"))
                        prm->setValueNotifyingHost (rnd.nextFloat());
                if (round % 6 == 5)
                {
                    juce::MemoryBlock state;
                    p.getStateInformation (state);
                    p.setStateInformation (state.getData(), (int) state.getSize());
                }
                if (p.phaseMode() == sq::linear)
                    p.rebuildLinearPhase();
                for (int b = 0; b < 8; ++b)
                {
                    const int n = std::array<int, 6> { 1, 7, 64, 333, 512, 4096 }[(size_t) rnd.nextInt (6)];
                    for (int i = 0; i < n; ++i)
                    {
                        buf.setSample (0, i, drums ((pos + i) % 200000) * 0.7f);
                        buf.setSample (1, i, drums ((pos + i + 99) % 200000) * 0.7f);
                    }
                    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
                    p.processBlock (view, midi);
                    pos += n;
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < n; ++i)
                        {
                            const float v = buf.getSample (c, i);
                            finite &= std::isfinite (v);
                            worst = std::max (worst, std::abs (v));
                        }
                }
            }
        }
        check (finite && worst < 80.0f, "stress: 5 rates x random settings x odd blocks, peak " + juce::String (worst, 1));
        p.setRateAndBufferSizeDetails (sr, block);
    }

    // Every preset on drums: finite, no runaway level.
    for (int preset = 0; preset < p.getNumPrograms(); ++preset)
    {
        p.setCurrentProgram (preset);
        prepare (p);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        float peak = 0;
        bool finite = true;
        for (int pos = 0; pos < (int) sr * 3; pos += block)
        {
            for (int i = 0; i < block; ++i)
                buf.setSample (0, i, drums (pos + i)), buf.setSample (1, i, drums (pos + i + 300));
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i)
                {
                    finite &= std::isfinite (buf.getSample (c, i));
                    peak = std::max (peak, std::abs (buf.getSample (c, i)));
                }
        }
        check (finite && peak < 4.0f, "preset " + p.getProgramName (preset) + " peak " + juce::String (peak, 2));
    }

    std::printf ("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
