// Loads the built SKAZI 303 VST3 like a DAW would and checks it actually makes
// sound: from MIDI with the sequencer off, from MIDI while the sequencer is on but
// the transport is stopped, and from the internal-clock sequencer with no MIDI.
//
// Usage: Skazi303HostTest "<path to SKAZI 303.vst3>"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <cstdio>
#include <thread>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name)
            return prm;
    return nullptr;
}

void setParam (juce::AudioPluginInstance& p, const juce::String& name, float normalised)
{
    if (auto* prm = findParam (p, name))
        prm->setValueNotifyingHost (normalised);
    else
        check (false, "parameter not found: " + name);
}

double lastPeak = 0.0;
bool lastFinite = true;

/** Renders `seconds` of audio, sending a note-on at the start (and note-off half way) if note >= 0. */
double render (juce::AudioPluginInstance& p, double seconds, int note, int velocity = 100)
{
    const int block = 512;
    const int total = (int) (seconds * 44100.0);
    juce::AudioBuffer<float> buf (2, block);
    double sum = 0.0;
    long count = 0;

    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        if (note >= 0 && pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), 0);
        if (note >= 0 && pos <= total / 2 && pos + block > total / 2)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);

        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) lastFinite = false;
                lastPeak = juce::jmax (lastPeak, (double) std::abs (v));
            }

        // Measure only while the note is held.
        if (note < 0 || pos < total / 2)
            for (int i = 0; i < block; ++i)
            {
                sum += (double) buf.getSample (0, i) * buf.getSample (0, i);
                ++count;
            }
    }
    return std::sqrt (sum / (double) juce::jmax (1L, count));
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 2)
    {
        std::printf ("usage: Skazi303HostTest <plugin.vst3>\n");
        return 2;
    }

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());

    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, juce::String::fromUTF8 (argv[1]));
    check (found.size() == 1, "found plugin in " + juce::String::fromUTF8 (argv[1]));
    if (found.isEmpty())
        return 1;

    std::printf ("loaded: %s by %s, instrument=%d\n", found[0]->name.toRawUTF8(),
                 found[0]->manufacturerName.toRawUTF8(), (int) found[0]->isInstrument);

    juce::String error;
    auto plugin = formats.createPluginInstance (*found[0], 44100.0, 512, error);
    check (plugin != nullptr, "instantiated " + error);
    if (plugin == nullptr)
        return 1;

    // How long a preset change and a new instance take: both run on threads the DAW waits on
    {
        for (int i = 0; i < 20; ++i)
            plugin->setCurrentProgram (i * 17);   // first visit measures each preset's level once
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i)
            plugin->setCurrentProgram (i * 17);   // second visit: remembered, so it must be quick
        const double perPreset = (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0;

        // A host sending a program change from its audio thread must get control back at once;
        // the preset then loads on the message thread.
        double audioThreadMs = 0.0;
        std::thread audio ([&]
        {
            const auto ta = juce::Time::getMillisecondCounterHiRes();
            plugin->setCurrentProgram (321);
            audioThreadMs = juce::Time::getMillisecondCounterHiRes() - ta;
        });
        audio.join();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (audioThreadMs < 2.0 && plugin->getCurrentProgram() == 321,
               juce::String::formatted ("program change from the audio thread returns in %.2f ms, preset %d loads after",
                                        audioThreadMs, plugin->getCurrentProgram() + 1));
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        auto another = formats.createPluginInstance (*found[0], 44100.0, 512, error);
        const double create = juce::Time::getMillisecondCounterHiRes() - t1;
        another.reset();
        check (perPreset < 5.0, juce::String::formatted ("preset change (already measured) takes %.2f ms", perPreset));

        // A host may ask for every program name at once (program lists, name-to-value lookups)
        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
            plugin->getProgramName (i);
        const double allNames = juce::Time::getMillisecondCounterHiRes() - t2;
        check (allNames < 50.0, juce::String::formatted ("all %d program names take %.1f ms", plugin->getNumPrograms(), allNames));
        check (create < 1000.0, juce::String::formatted ("new instance takes %.0f ms", create));
    }

    plugin->enableAllBuses();
    plugin->setPlayConfigDetails (0, 2, 44100.0, 512);
    plugin->prepareToPlay (44100.0, 512);

    const auto* seqOn = findParam (*plugin, "Sequencer On");
    check (seqOn != nullptr && seqOn->getValue() < 0.5f, "sequencer is off by default");

    // Presets carry echoes and reverb; the gate/stop checks below need a dry sound.
    for (auto name : { "Delay Mix", "Reverb Mix", "Mod Mix" })
        setParam (*plugin, name, 0.0f);

    // 1. Plain synth: MIDI note plays.
    const double silent = render (*plugin, 0.5, -1);
    check (silent < 1.0e-4, juce::String::formatted ("silent with no input (rms %.6f)", silent));

    const double midiRms = render (*plugin, 1.0, 48);
    check (midiRms > 0.01, juce::String::formatted ("MIDI note plays with sequencer off (rms %.4f)", midiRms));

    const double after = render (*plugin, 0.5, -1);
    check (after < 0.01, juce::String::formatted ("note stops after note-off (rms %.5f)", after));

    // 2. Sequencer on, but no transport running: MIDI still plays the synth.
    setParam (*plugin, "Sequencer On", 1.0f);
    render (*plugin, 0.05, -1);
    const double seqStoppedRms = render (*plugin, 1.0, 50);
    check (seqStoppedRms > 0.01, juce::String::formatted ("MIDI plays with sequencer on and transport stopped (rms %.4f)", seqStoppedRms));

    // 3. Internal clock: the pattern plays by itself.
    setParam (*plugin, "Seq Sync", 1.0f); // Internal
    const double seqRms = render (*plugin, 2.0, -1);
    check (seqRms > 0.01, juce::String::formatted ("internal sequencer plays the pattern (rms %.4f)", seqRms));

    // 4. Turning it off again stops the pattern.
    setParam (*plugin, "Sequencer On", 0.0f);
    render (*plugin, 0.2, -1);
    const double offRms = render (*plugin, 0.5, -1);
    check (offRms < 0.005, juce::String::formatted ("sequencer off -> pattern stops (rms %.5f)", offRms));

    // 5. Every factory preset loads, plays a held note, and stays sane.
    {
        const int n = plugin->getNumPrograms();
        check (n >= 500, "factory bank has " + juce::String (n) + " presets");

        int silentCount = 0, loudCount = 0;
        double minRms = 1.0e9, maxRms = 0.0;
        juce::String quietest, loudest;
        juce::StringArray names;
        for (int i = 0; i < n; ++i)
        {
            plugin->setCurrentProgram (i);
            names.addIfNotAlreadyThere (plugin->getProgramName (i));
            const double rms = render (*plugin, 0.6, 45, 90); // normal (non-accent) velocity
            render (*plugin, 0.3, -1); // let tails die between presets
            if (! std::isfinite (rms) || rms < 0.005) ++silentCount;
            if (rms > 1.0) ++loudCount;
            const auto volText = findParam (*plugin, "Volume")->getCurrentValueAsText();
            if (rms < minRms) { minRms = rms; quietest = plugin->getProgramName (i) + " vol " + volText; }
            if (rms > maxRms) { maxRms = rms; loudest = plugin->getProgramName (i) + " vol " + volText; }
        }
        check (silentCount == 0, juce::String (silentCount) + " presets silent; quietest " + quietest
                                   + juce::String::formatted (" (rms %.4f)", minRms));
        check (loudCount == 0, juce::String (loudCount) + " presets over 0 dBFS rms; loudest " + loudest
                                 + juce::String::formatted (" (rms %.3f)", maxRms));
        const double spreadDb = 20.0 * std::log10 (maxRms / minRms);
        check (spreadDb < 12.0, juce::String::formatted ("preset loudness spread %.1f dB (held note)", spreadDb));
        check (names.size() == n, juce::String (names.size()) + " unique preset names");
        std::printf ("  e.g. %s | %s | %s\n", plugin->getProgramName (0).toRawUTF8(),
                     plugin->getProgramName (200).toRawUTF8(), plugin->getProgramName (n - 1).toRawUTF8());
    }

    // 6. POWER: TURBO and DRIVE 2 + OTT keep the level (within 3 dB), never clip, never NaN.
    {
        double worstDb = 0.0, worstPeak = 0.0;
        juce::String worstName;
        bool allFinite = true;
        int tested = 0;
        for (int i = 0; i < plugin->getNumPrograms(); i += 7)
        {
            plugin->setCurrentProgram (i);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            for (auto name : { "Delay Mix", "Reverb Mix", "Mod Mix" })
                setParam (*plugin, name, 0.0f);

            // settings: off, TURBO, max DRIVE 2 + OTT, TURBO + max everything (incl. Super Dist type)
            struct Set { float turbo, d2, ott, type; } sets[] = { { 0, 0, 0, 5 }, { 1, 0, 0, 5 }, { 0, 1, 1, 5 }, { 1, 1, 1, 6 } };
            double ref = 0.0;
            for (auto& st : sets)
            {
                setParam (*plugin, "Turbo", st.turbo);
                setParam (*plugin, "Drive 2", st.d2);
                setParam (*plugin, "OTT Depth", st.ott);
                setParam (*plugin, "Drive 2 Type", st.type / 13.0f);
                render (*plugin, 0.3, -1);
                lastPeak = 0.0; lastFinite = true;
                render (*plugin, 0.5, 45, 90);                  // settle the level match
                const double rms = render (*plugin, 1.2, 45, 120); // accented held note
                allFinite = allFinite && lastFinite && std::isfinite (rms);
                if (&st == &sets[0]) { ref = rms; continue; }
                const double db = 20.0 * std::log10 (juce::jmax (1.0e-9, rms) / juce::jmax (1.0e-9, ref));
                if (std::abs (db) > std::abs (worstDb)) { worstDb = db; worstName = plugin->getProgramName (i); }
                worstPeak = juce::jmax (worstPeak, lastPeak);
            }
            setParam (*plugin, "Turbo", 0.0f); setParam (*plugin, "Drive 2", 0.0f); setParam (*plugin, "OTT Depth", 0.0f);
            ++tested;
        }
        check (allFinite, "POWER: no NaN/inf over " + juce::String (tested) + " presets x 3 settings");
        check (std::abs (worstDb) < 3.0, juce::String::formatted ("POWER level change vs off, worst %+.1f dB (", worstDb) + worstName + ")");
        check (worstPeak < 1.0, juce::String::formatted ("POWER peak %.3f, under 0 dBFS", worstPeak));
    }

    // 7. MOD page: OSC B, unison, ladder filter, LFOs. Every combination stays finite and under 0 dBFS,
    //    and the ladder sits near the diode's level.
    {
        bool allFinite = true;
        double worstPeak = 0.0, worstLadderDb = 0.0;
        for (int i = 0; i < plugin->getNumPrograms(); i += 17)
        {
            plugin->setCurrentProgram (i);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            for (auto name : { "Delay Mix", "Reverb Mix", "Mod Mix" })
                setParam (*plugin, name, 0.0f);

            render (*plugin, 0.3, -1);
            const double diodeRms = render (*plugin, 1.0, 45, 90);
            setParam (*plugin, "Filter Model", 1.0f);
            render (*plugin, 0.3, -1);
            lastPeak = 0.0; lastFinite = true;
            const double ladderRms = render (*plugin, 1.0, 45, 90);
            const double ladderDb = 20.0 * std::log10 (juce::jmax (1.0e-9, ladderRms) / juce::jmax (1.0e-9, diodeRms));
            if (std::abs (ladderDb) > std::abs (worstLadderDb)) worstLadderDb = ladderDb;

            setParam (*plugin, "Osc B Level", 1.0f);
            setParam (*plugin, "Unison Voices", 1.0f);     // 7 voices
            setParam (*plugin, "Unison Detune", 1.0f);
            setParam (*plugin, "LFO 1 Destination", 1.0f / 9.0f);  // cutoff
            setParam (*plugin, "LFO 1 Shape", 5.0f / 7.0f);        // S&H
            setParam (*plugin, "LFO 1 Amount", 1.0f);
            setParam (*plugin, "LFO 2 Destination", 5.0f / 9.0f);  // volume
            setParam (*plugin, "LFO 2 Shape", 6.0f / 7.0f);        // pump
            setParam (*plugin, "Turbo", 1.0f);
            render (*plugin, 1.0, 45, 120);
            allFinite = allFinite && lastFinite;
            worstPeak = juce::jmax (worstPeak, lastPeak);

            for (auto name : { "Filter Model", "Osc B Level", "Unison Voices", "LFO 1 Destination", "LFO 2 Destination", "Turbo" })
                setParam (*plugin, name, 0.0f);
            setParam (*plugin, "Unison Detune", 0.3f);
            setParam (*plugin, "LFO 1 Amount", 0.75f);
        }
        check (allFinite, "MOD: no NaN/inf with OSC B + 7 voices + ladder + 2 LFOs + TURBO");
        check (worstPeak < 1.0, juce::String::formatted ("MOD peak %.3f, under 0 dBFS", worstPeak));
        check (std::abs (worstLadderDb) < 6.0, juce::String::formatted ("ladder vs diode level, worst %+.1f dB", worstLadderDb));
    }

    // 8. Editor opens.
    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
    check (editor != nullptr && editor->getWidth() > 400, "editor opens");
    editor.reset();

    plugin->releaseResources();
    plugin.reset();

    std::printf ("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
