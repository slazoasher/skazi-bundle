// Renders the editor to a PNG after playing audio through it, for checking the panel and making page pictures.
//   FilterFishShot <out.png> [program]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("usage: FilterFishShot <out.png> [program]\n"); return 2; }

    GlitchMagmaProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (1);
    auto play = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            proc.processBlock (buffer, midi);
        }
    };

    auto set = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };

    // FilterFishShot check: every reverb type rings out and stays bounded, OFF passes audio untouched,
    // and flipping type or power while sound plays never blows up.
    if (juce::String (argv[1]) == "check")
    {
        int failures = 0;
        set (ids::reverbOn, 1.0f);
        set (ids::reverbMix, 0.5f);
        for (int type = 0; type < reverbTypeNames().size(); ++type)
        {
            set (ids::reverbType, (float) type);
            proc.prepareToPlay (48000.0, 512);
            float peak = 0.0f, tail = 0.0f;
            bool finite = true;
            for (int b = 0; b < 200; ++b)
            {
                buffer.clear();
                if (b == 0) buffer.setSample (0, 0, 1.0f), buffer.setSample (1, 0, 1.0f);
                proc.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = buffer.getSample (c, i);
                        finite = finite && std::isfinite (v);
                        peak = juce::jmax (peak, std::abs (v));
                        if (b > 20 && b < 60) tail = juce::jmax (tail, std::abs (v));   // 0.2 - 0.6 s after the hit
                    }
            }
            const bool ok = finite && peak < 4.0f && tail > 1.0e-5f;
            failures += ok ? 0 : 1;
            std::printf ("reverb %-10s peak %.3f tail %.5f %s\n", reverbTypeNames()[type].toRawUTF8(), peak, tail, ok ? "ok" : "FAIL");
        }

        // OFF: after the fade the output is the input, sample for sample
        set (ids::bypass, 1.0f);
        play (4);
        float worst = 0.0f;
        for (int b = 0; b < 4; ++b)
        {
            juce::AudioBuffer<float> in (2, 512);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    in.setSample (c, i, std::sin ((float) (b * 512 + i) * 0.03f + (float) c));
            buffer.makeCopyOf (in);
            proc.processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    worst = juce::jmax (worst, std::abs (buffer.getSample (c, i) - in.getSample (c, i)));
        }
        const bool bypassOk = worst == 0.0f;
        failures += bypassOk ? 0 : 1;
        std::printf ("OFF passes input untouched: max diff %g %s\n", worst, bypassOk ? "ok" : "FAIL");

        // Flip type and power every few blocks while noise plays
        float flipPeak = 0.0f;
        bool flipFinite = true;
        for (int k = 0; k < 120; ++k)
        {
            if (k % 5 == 0) set (ids::reverbType, (float) ((k / 5) % reverbTypeNames().size()));
            if (k % 13 == 0) set (ids::bypass, (k / 13) % 2 == 0 ? 0.0f : 1.0f);
            play (1);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                {
                    flipFinite = flipFinite && std::isfinite (buffer.getSample (c, i));
                    flipPeak = juce::jmax (flipPeak, std::abs (buffer.getSample (c, i)));
                }
        }
        const bool flipOk = flipFinite && flipPeak < 4.0f;
        failures += flipOk ? 0 : 1;
        std::printf ("switching type + power while playing: peak %.3f %s\n", flipPeak, flipOk ? "ok" : "FAIL");
        std::printf ("%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
        return failures == 0 ? 0 : 1;
    }

    if (argc > 2)
    {
        proc.setCurrentProgram (juce::String (argv[2]).getIntValue());
        for (int i = 0; i < 8; ++i)   // let the preset fade finish
        {
            play (4);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
    }

    if (argc > 3)   // FilterFishShot <out.png> <program> <reverb type>: show a reverb type on the panel
    {
        set (ids::reverbOn, 1.0f);
        set (ids::reverbType, juce::String (argv[3]).getFloatValue());
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (1280, 800);
    for (int frame = 0; frame < 40; ++frame)
    {
        play (2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
        return 1;
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    editor.reset();
    return 0;
}
