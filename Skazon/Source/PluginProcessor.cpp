#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include "PresetTrims.h"
#include "../../Common/UpdateNotice.h"

using namespace skazon;

namespace
{
    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
}

juce::StringArray SkazonAudioProcessor::presetNames()
{
    juce::StringArray names;
    for (const auto& p : allPresets())
        names.add (p.name);
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout SkazonAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&] (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float def, const juce::String& unit = {})
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addInt = [&] (const juce::String& id, const juce::String& name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto addChoice = [&] (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
    };
    auto addBool = [&] (const juce::String& id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };

    for (auto [x, name, on, table, pos] : { std::tuple { "a", "Osc A", true, 0, 0.66f }, std::tuple { "b", "Osc B", false, 0, 0.66f } })
    {
        const juce::String p (x), n (name);
        addBool (p + "On", n + " On", on);
        addChoice (p + "Table", n + " Table", tableNames(), table);
        add (p + "Pos", n + " WT Pos", { 0, 1 }, pos);
        addInt (p + "Oct", n + " Octave", -3, 3, 0);
        addInt (p + "Semi", n + " Semi", -12, 12, 0);
        add (p + "Fine", n + " Fine", { -100, 100 }, 0, "ct");
        addInt (p + "Uni", n + " Unison", 1, maxUnison, 1);
        add (p + "Det", n + " Detune", { 0, 1 }, 0.25f);
        add (p + "Level", n + " Level", { 0, 1 }, 0.75f);
        addChoice (p + "Warp", n + " Warp Mode", warpNames(), 0);
        add (p + "WarpAmt", n + " Warp", { 0, 1 }, 0);
    }

    addBool ("gOn", "Grain On", false);
    addChoice ("gTable", "Grain Table", tableNames(), 11);
    add ("gPos", "Grain Pos", { 0, 1 }, 0.5f);
    add ("gSize", "Grain Size", skewed (5, 1000, 80), 80, "ms");
    add ("gDens", "Grain Density", skewed (1, 200, 25), 25, "/s");
    add ("gSpray", "Grain Spray", { 0, 1 }, 0.3f);
    add ("gSpread", "Grain Pitch Spread", { 0, 12 }, 0, "st");
    addInt ("gPitch", "Grain Pitch", -24, 24, 0);
    add ("gLevel", "Grain Level", { 0, 1 }, 0.6f);

    add ("sub", "Sub Level", { 0, 1 }, 0);
    add ("noise", "Noise Level", { 0, 1 }, 0);

    addChoice ("fType", "Filter Type", filterNames(), 0);
    add ("fCut", "Cutoff", skewed (20, 20000, 1200), 12000, "Hz");
    add ("fRes", "Resonance", { 0, 1 }, 0.1f);
    add ("fDrive", "Filter Drive", { 0, 1 }, 0);
    add ("fEnv", "Filter Env", { -1, 1 }, 0);
    add ("fKey", "Key Track", { 0, 1 }, 0);

    for (auto [e, name, a, d, s, r] : { std::tuple { "e1", "Amp", 0.005f, 0.3f, 0.8f, 0.25f }, std::tuple { "e2", "Mod Env", 0.005f, 0.5f, 0.0f, 0.3f } })
    {
        const juce::String p (e), n (name);
        add (p + "A", n + " Attack", skewed (0.001f, 10, 0.4f), a, "s");
        add (p + "D", n + " Decay", skewed (0.001f, 10, 0.6f), d, "s");
        add (p + "S", n + " Sustain", { 0, 1 }, s);
        add (p + "R", n + " Release", skewed (0.001f, 10, 0.6f), r, "s");
    }

    for (auto [l, name, rate, div] : { std::tuple { "l1", "LFO 1", 2.0f, 6 }, std::tuple { "l2", "LFO 2", 0.3f, 9 },
                                       std::tuple { "l3", "LFO 3", 1.0f, 4 }, std::tuple { "l4", "LFO 4", 4.0f, 8 } })
    {
        const juce::String p (l), n (name);
        addChoice (p + "Shape", n + " Shape", lfoShapeNames(), 0);
        add (p + "Rate", n + " Rate", skewed (0.02f, 30, 2), rate, "Hz");
        addBool (p + "Sync", n + " Sync", false);
        addChoice (p + "Div", n + " Division", divNames(), div);
    }

    for (int i = 1; i <= numSlots; ++i)
    {
        const juce::String p ("m" + juce::String (i)), n ("Mod " + juce::String (i));
        addChoice (p + "Src", n + " Source", sourceNames(), 0);
        addChoice (p + "Op", n + " Math", opNames(), 0);
        addChoice (p + "Dst", n + " Target", targetNames(), 0);
        add (p + "Amt", n + " Amount", { -1, 1 }, 0);
    }

    add ("glide", "Glide", skewed (0, 2, 0.2f), 0, "s");
    addInt ("bendRange", "Bend Range", 0, 24, 2);
    // Mod wheel works out of the box: classic vibrato (LFO 1 on pitch, ~0.7 st at full wheel).
    addChoice ("mwLfo", "Mod Wheel LFO", { "DIRECT", "LFO 1", "LFO 2", "LFO 3", "LFO 4" }, 1);
    addChoice ("mwDst", "Mod Wheel Target", targetNames(), tPitch);
    add ("mwAmt", "Mod Wheel Amount", { -1, 1 }, 0.03f);
    for (int i = 1; i <= numMacros; ++i)
        add ("macro" + juce::String (i), "Macro " + juce::String (i), { 0, 1 }, 0);
    addBool ("mono", "Mono", false);
    addBool ("legato", "Legato", true);

    add ("fxDrive", "FX Drive", { 0, 1 }, 0);
    add ("fxChorus", "FX Chorus", { 0, 1 }, 0);
    add ("fxDelay", "FX Delay Mix", { 0, 1 }, 0);
    addChoice ("fxDelayDiv", "FX Delay Time", juce::StringArray (divNames().begin(), 12), 5); // up to 4 bars: the delay line is not longer
    add ("fxDelayFb", "FX Delay Feedback", { 0, 0.95f }, 0.4f);
    add ("fxReverb", "FX Reverb Mix", { 0, 1 }, 0.15f);
    add ("fxSize", "FX Reverb Size", { 0, 1 }, 0.6f);
    add ("master", "Master", { -24, 6 }, 0, "dB");

    addBool ("distOn", "Dist On", true);
    addChoice ("distType", "Dist Type", distNames(), 0);
    add ("distTone", "Dist Tone", skewed (200, 20000, 3000), 20000, "Hz");
    add ("distMix", "Dist Mix", { 0, 1 }, 1);
    addBool ("eqOn", "EQ On", true);
    add ("eqLow", "EQ Low Gain", { -15, 15 }, 0, "dB");
    add ("eqLowFreq", "EQ Low Freq", skewed (30, 800, 150), 120, "Hz");
    add ("eqHigh", "EQ High Gain", { -15, 15 }, 0, "dB");
    add ("eqHighFreq", "EQ High Freq", skewed (1500, 16000, 6000), 6000, "Hz");
    addBool ("phOn", "Phaser On", true);
    add ("phMix", "Phaser Mix", { 0, 1 }, 0);
    add ("phRate", "Phaser Rate", skewed (0.05f, 10, 1), 0.5f, "Hz");
    add ("phDepth", "Phaser Depth", { 0, 1 }, 0.6f);
    add ("phFb", "Phaser Feedback", { -0.95f, 0.95f }, 0.4f);
    addBool ("chOn", "Chorus On", true);
    add ("chRate", "Chorus Rate", skewed (0.05f, 8, 1), 0.7f, "Hz");
    add ("chDepth", "Chorus Depth", { 0, 1 }, 0.35f);
    addBool ("hyOn", "Hyper On", true);
    add ("hyMix", "Hyper Mix", { 0, 1 }, 0);
    add ("hyDetune", "Hyper Detune", { 0, 1 }, 0.4f);
    add ("hyDim", "Dimension", { 0, 1 }, 0);
    addBool ("dlOn", "Delay On", true);
    addBool ("rvOn", "Reverb On", true);
    addBool ("ottOn", "OTT On", true);
    add ("ottDepth", "OTT Depth", { 0, 1 }, 0);
    add ("ottTime", "OTT Time", { 0, 1 }, 0.5f);
    add ("ottUp", "OTT Upward", { 0, 1 }, 1);
    add ("ottDown", "OTT Downward", { 0, 1 }, 1);
    add ("ottGain", "OTT Out Gain", { -18, 12 }, 0, "dB");
    return layout;
}

//==============================================================================
SkazonAudioProcessor::SkazonAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SKAZON", createLayout())
{
    Wavetables::get(); // build the tables here, not on the audio thread
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new Voice (snap, lastNote));
    synth.addSound (new Sound());
    for (auto& p : livePos)
        p = -1.0f;
}

bool SkazonAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SkazonAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.allNotesOff (0, false); // nothing hangs across an audio restart
    synth.setCurrentPlaybackSampleRate (sampleRate);
    fx.prepare (sampleRate, samplesPerBlock);
    held.clearQuick();
    monoVoice = nullptr;
}

void SkazonAudioProcessor::fillSnapshot (double bpm)
{
    for (int i = 0; i < 2; ++i)
    {
        const juce::String p (i == 0 ? "a" : "b");
        auto& o = snap.osc[i];
        o.on = played (p + "On") > 0.5f;
        o.table = (int) played (p + "Table");
        o.pos = played (p + "Pos");
        o.tune = played (p + "Oct") * 12.0f + played (p + "Semi") + played (p + "Fine") * 0.01f;
        o.unison = (int) played (p + "Uni");
        o.detune = played (p + "Det");
        o.level = played (p + "Level");
        o.warp = (int) played (p + "Warp");
        o.warpAmt = played (p + "WarpAmt");
    }
    snap.gOn = played ("gOn") > 0.5f;
    snap.gTable = (int) played ("gTable");
    snap.gPos = played ("gPos");
    snap.gSizeMs = played ("gSize");
    snap.gDensity = played ("gDens");
    snap.gSpray = played ("gSpray");
    snap.gSpread = played ("gSpread");
    snap.gPitch = played ("gPitch");
    snap.gLevel = played ("gLevel");
    snap.sub = played ("sub");
    snap.noise = played ("noise");
    snap.fType = (int) played ("fType");
    snap.cutoff = played ("fCut");
    snap.res = played ("fRes");
    snap.drive = played ("fDrive");
    snap.fEnv = played ("fEnv");
    snap.fKey = played ("fKey");
    for (int i = 0; i < 2; ++i)
    {
        const juce::String e ("e" + juce::String (i + 1));
        snap.env[i] = { played (e + "A"), played (e + "D"), played (e + "S"), played (e + "R") };
    }
    for (int i = 0; i < numLfos; ++i)
    {
        const juce::String l ("l" + juce::String (i + 1));
        snap.lfo[i].shape = (int) played (l + "Shape");
        snap.lfo[i].hz = played (l + "Sync") > 0.5f ? (float) (bpm / 60.0 / divBeats ((int) played (l + "Div"))) : played (l + "Rate");
    }
    for (int i = 0; i < numMacros; ++i)
        snap.macro[i] = played ("macro" + juce::String (i + 1));
    for (int i = 0; i < numSlots; ++i)
    {
        const juce::String m ("m" + juce::String (i + 1));
        snap.slots[i] = { (int) played (m + "Src"), (int) played (m + "Op"), (int) played (m + "Dst"), played (m + "Amt") };
    }
    snap.glide = played ("glide");
    snap.mwLfo = (int) played ("mwLfo");
    snap.mwDst = (int) played ("mwDst");
    snap.mwAmt = played ("mwAmt");
    snap.bend = (float) (bendRaw - 8192) / 8192.0f * played ("bendRange");
}

void SkazonAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    double bpm = 120.0;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (20.0, 999.0, *b);

    hostBpm = bpm;
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    if (const int b = uiBend.exchange (-1); b >= 0)
        midi.addEvent (juce::MidiMessage::pitchWheel (1, b), 0);
    if (const int w = uiModWheel.exchange (-1); w >= 0)
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, w), 0);
    addPreviewEvents (midi, n);

    // shortcut: wheel, bend and pressure apply per block, not per sample; split blocks at events if zipper shows up
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isPitchWheel())                   bendRaw = m.getPitchWheelValue();
        else if (m.isControllerOfType (1))      snap.wheel = (float) m.getControllerValue() / 127.0f;
        else if (m.isChannelPressure())         snap.aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())              snap.aftertouch = (float) m.getAfterTouchValue() / 127.0f;
    }
    shownBend = bendRaw;
    shownModWheel = juce::roundToInt (snap.wheel * 127.0f);
    blockLinks = std::atomic_load (&links);
    for (int i = 0; i < numMacros; ++i)
        blockMacro[i] = param ("macro" + juce::String (i + 1));
    fillSnapshot (bpm);
    audioUserTable = std::atomic_load (&userTable);
    snap.user = audioUserTable.get();

    buffer.clear();
    if (param ("mono") > 0.5f)
        renderMono (buffer, midi);
    else
    {
        monoVoice = nullptr;
        held.clearQuick();
        synth.renderNextBlock (buffer, midi, 0, n);
    }

    // Newest sounding voice drives the displays.
    Voice* newest = nullptr;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<Voice*> (synth.getVoice (i)); v != nullptr && v->isVoiceActive() && (newest == nullptr || newest->wasStartedBefore (*v)))
            newest = v;
    for (int i = 0; i < 3; ++i)
        livePos[(size_t) i] = newest != nullptr ? newest->livePos[i] : -1.0f;
    for (int i = 0; i < numTargets; ++i)
        liveMods[(size_t) i] = newest != nullptr ? newest->liveMods[i] : 0.0f;
    for (int i = 0; i < numLfos; ++i)
        liveLfo[(size_t) i] = newest != nullptr ? newest->liveLfo[i] : -1.0f;
    int active = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        active += synth.getVoice (i)->isVoiceActive() ? 1 : 0;
    activeVoices = active;

    // Mono hosts get the dry synth; the FX are stereo.
    if (buffer.getNumChannels() < 2)
        return;

    FxRack::Params f;
    f.bpm = bpm;
    f.distOn = played ("distOn") > 0.5f;
    f.distType = (int) played ("distType");
    f.distDrive = played ("fxDrive");
    f.distTone = played ("distTone");
    f.distMix = played ("distMix");
    f.eqOn = played ("eqOn") > 0.5f;
    f.eqLow = played ("eqLow");
    f.eqLowFreq = played ("eqLowFreq");
    f.eqHigh = played ("eqHigh");
    f.eqHighFreq = played ("eqHighFreq");
    f.phOn = played ("phOn") > 0.5f;
    f.phMix = played ("phMix");
    f.phRate = played ("phRate");
    f.phDepth = played ("phDepth");
    f.phFb = played ("phFb");
    f.chOn = played ("chOn") > 0.5f;
    f.chMix = played ("fxChorus");
    f.chRate = played ("chRate");
    f.chDepth = played ("chDepth");
    f.hyOn = played ("hyOn") > 0.5f;
    f.hyMix = played ("hyMix");
    f.hyDetune = played ("hyDetune");
    f.hyDim = played ("hyDim");
    f.dlOn = played ("dlOn") > 0.5f;
    f.dlMix = played ("fxDelay");
    f.dlFb = played ("fxDelayFb");
    f.dlBeats = divBeats ((int) played ("fxDelayDiv"));
    f.rvOn = played ("rvOn") > 0.5f;
    f.rvMix = played ("fxReverb");
    f.rvSize = played ("fxSize");
    f.ottOn = played ("ottOn") > 0.5f;
    f.ottDepth = played ("ottDepth");
    f.ottTime = played ("ottTime");
    f.ottUp = played ("ottUp");
    f.ottDown = played ("ottDown");
    f.ottGain = played ("ottGain");
    f.masterDb = played ("master");
    fx.process (buffer, f);
}

// Mono: one voice; with LEGATO a new key glides the sounding voice instead of retriggering it.
void SkazonAudioProcessor::renderMono (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const bool legato = param ("legato") > 0.5f;
    int pos = 0;
    const juce::MidiBuffer none;

    auto sounding = [this]() -> Voice* { return monoVoice != nullptr && monoVoice->isVoiceActive() && monoVoice->isKeyDown() ? monoVoice : nullptr; };
    auto start = [this] (int note, float vel)
    {
        if (monoVoice != nullptr && monoVoice->isVoiceActive())
            synth.noteOff (1, monoVoice->getCurrentlyPlayingNote(), 0.0f, true);
        synth.noteOn (1, note, vel);
        monoVoice = nullptr;
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<Voice*> (synth.getVoice (i)); v != nullptr && v->isKeyDown() && v->getCurrentlyPlayingNote() == note)
                monoVoice = v;
    };

    for (const auto meta : midi)
    {
        if (meta.samplePosition > pos)
        {
            synth.renderNextBlock (buffer, none, pos, meta.samplePosition - pos);
            pos = meta.samplePosition;
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            held.removeAllInstancesOf (m.getNoteNumber());
            held.add (m.getNoteNumber());
            if (auto* v = sounding(); v != nullptr && legato)
                v->legato (m.getNoteNumber(), m.getFloatVelocity());
            else
                start (m.getNoteNumber(), m.getFloatVelocity());
        }
        else if (m.isNoteOff())
        {
            held.removeAllInstancesOf (m.getNoteNumber());
            auto* v = sounding();
            if (v == nullptr || v->currentNote() != m.getNoteNumber())
                continue;
            if (! held.isEmpty())
            {
                if (legato) v->legato (held.getLast(), 0.8f);
                else        start (held.getLast(), 0.8f);
            }
            else
                synth.noteOff (1, v->getCurrentlyPlayingNote(), m.getFloatVelocity(), true);
        }
        else
        {
            if (m.isAllNotesOff() || m.isAllSoundOff())
                held.clearQuick();
            synth.handleMidiEvent (m);
        }
    }
    if (pos < buffer.getNumSamples())
        synth.renderNextBlock (buffer, none, pos, buffer.getNumSamples() - pos);
}

//==============================================================================
int SkazonAudioProcessor::getNumPrograms() { return (int) allPresets().size(); }
const juce::String SkazonAudioProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, getNumPrograms()) ? allPresets()[(size_t) i].name : juce::String(); }

void SkazonAudioProcessor::applyValues (const std::vector<std::pair<juce::String, float>>& values, float trimDb)
{
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    for (const auto& [id, value] : values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
            jassertfalse; // typo in a preset
    if (auto* m = apvts.getParameter ("master"))
        m->setValueNotifyingHost (m->convertTo0to1 (trimDb));
}

float SkazonAudioProcessor::played (const juce::String& id) const
{
    float v = param (id);
    if (blockLinks != nullptr)
        for (const auto& l : *blockLinks)
            if (l.id == id)
                v = l.param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, l.param->convertTo0to1 (v) + l.depth * blockMacro[l.macro]));
    return v;
}

void SkazonAudioProcessor::storeLinks (std::vector<MacroLink> v)
{
    juce::StringArray text;
    for (const auto& l : v)
        text.add (l.id + ":" + juce::String (l.macro) + ":" + juce::String (l.depth, 3));
    apvts.state.setProperty ("macroLinks", text.joinIntoString (";"), nullptr);
    previousLinks = std::atomic_load (&links);
    std::atomic_store (&links, v.empty() ? std::shared_ptr<const std::vector<MacroLink>>() : std::make_shared<const std::vector<MacroLink>> (std::move (v)));
}

void SkazonAudioProcessor::setMacroLink (int macro, const juce::String& id, float depth)
{
    auto* p = apvts.getParameter (id);
    if (p == nullptr || ! juce::isPositiveAndBelow (macro, numMacros) || id.startsWith ("macro"))
        return;
    auto v = macroLinks();
    const auto it = std::find_if (v.begin(), v.end(), [&] (const MacroLink& l) { return l.macro == macro && l.id == id; });
    depth = juce::jlimit (-1.0f, 1.0f, depth);
    if (it != v.end())
    {
        if (std::abs (depth) < 0.001f) v.erase (it);
        else it->depth = depth;
    }
    else if (std::abs (depth) >= 0.001f && v.size() < 64)
        v.push_back ({ id, p, macro, depth });
    storeLinks (std::move (v));
}

void SkazonAudioProcessor::clearMacroLinks()
{
    storeLinks ({});
}

void SkazonAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentPreset = index;
    clearMacroLinks();
    randomName = {};
    applyValues (allPresets()[(size_t) index].values, presetTrimDb.size() == allPresets().size() ? presetTrimDb[(size_t) index] : 0.0f);
    apvts.state.setProperty ("preset", index, nullptr);
    apvts.state.setProperty ("randomName", "", nullptr);
}

void SkazonAudioProcessor::randomize (int category)
{
    const auto& cats = generatedCategories();
    if (! juce::isPositiveAndBelow (category, cats.size()))
    {
        // Stay in the category of the current preset, else pick one.
        const auto current = (randomName.isNotEmpty() ? randomName : getProgramName (currentPreset)).upToFirstOccurrenceOf (" // ", false, false);
        category = cats.indexOf (current);
        if (category < 0)
            category = juce::Random::getSystemRandom().nextInt (cats.size());
    }
    auto preset = randomPreset (category, juce::Random::getSystemRandom().nextInt64());

    // Level-match like the factory presets of that category.
    float trim = 0;
    int count = 0;
    if (presetTrimDb.size() == allPresets().size())
        for (size_t i = 0; i < allPresets().size(); ++i)
            if (allPresets()[i].name.startsWith (cats[category] + " // "))
            {
                trim += presetTrimDb[i];
                ++count;
            }
    applyValues (preset.values, count > 0 ? trim / (float) count : 0.0f);
    randomName = preset.name;
    apvts.state.setProperty ("randomName", randomName, nullptr);
}

bool SkazonAudioProcessor::isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

bool SkazonAudioProcessor::readAudio (const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 64)
        return false;
    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0)); // cap at 30 s
    audio.setSize ((int) juce::jlimit (1u, 2u, reader->numChannels), len);
    reader->read (&audio, 0, len, 0, true, true);
    sampleRate = reader->sampleRate;
    return true;
}

void SkazonAudioProcessor::storeUserTable (std::shared_ptr<const skazon::UserTable> table, const juce::File& file, const juce::String& mode)
{
    previousUserTable = std::atomic_load (&userTable); // keeps the old one alive past the audio thread's block
    std::atomic_store (&userTable, table);
    // shortcut: the table is saved as the file path, so a moved file is lost; embed the frames if users share projects
    apvts.state.setProperty ("userTable", file.getFullPathName(), nullptr);
    apvts.state.setProperty ("userTableMode", mode, nullptr);
}

void SkazonAudioProcessor::addPreviewEvents (juce::MidiBuffer& midi, int numSamples)
{
    const bool on = previewOn;
    if (! on)
    {
        if (previewSounding >= 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, previewSounding), 0);
        previewSounding = -1;
        previewWasOn = false;
        return;
    }
    const juce::SpinLock::ScopedTryLockType l (previewLock);
    if (! l.isLocked() || previewNotes.empty())
        return;
    if (! previewWasOn)
        previewTime = previewNotes.front().start - 0.05; // start at the first note, not at the file's silence
    previewWasOn = true;

    const double sr = synth.getSampleRate(), t0 = previewTime, t1 = t0 + numSamples / sr; // set in prepareToPlay (getSampleRate() can be 0 offline)
    const auto& last = previewNotes.back();
    const double loopEnd = last.start + last.length + 0.6;
    auto offset = [&] (double t) { return juce::jlimit (0, numSamples - 1, (int) ((t - t0) * sr)); };
    for (const auto& note : previewNotes)
    {
        const double end = note.start + note.length;
        if (end >= t0 && end < t1 && previewSounding == note.pitch)
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, note.pitch), offset (end));
            previewSounding = -1;
        }
        if (note.start >= t0 && note.start < t1)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, note.pitch, (juce::uint8) note.velocity), offset (note.start));
            previewSounding = note.pitch;
        }
    }
    previewTime = t1;
    if (previewTime > loopEnd)
    {
        if (previewSounding >= 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, previewSounding), numSamples - 1);
        previewSounding = -1;
        previewTime = previewNotes.front().start - 0.05;
    }
}

juce::File SkazonAudioProcessor::writeMatchMidi() const
{
    std::vector<skazon::TrackedNote> notes;
    juce::String name;
    {
        const juce::SpinLock::ScopedLockType l (previewLock);
        notes = previewNotes;
        name = previewName;
    }
    if (notes.empty())
        return {};
    const double bpm = hostBpm.load();
    constexpr int ppq = 960;
    auto ticks = [&] (double sec) { return sec * bpm / 60.0 * ppq; }; // times stay where they were in the audio file
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0);
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "SKAZON MATCH " + name), 0);
    for (const auto& n : notes)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity), ticks (n.start));
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), ticks (n.start + n.length));
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SKAZON MATCH " + juce::File::createLegalFileName (name) + ".mid");
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! mf.writeTo (out))
        return {};
    return file;
}

skazon::MatchResult SkazonAudioProcessor::soundMatch (const juce::File& file)
{
    juce::AudioBuffer<float> audio;
    double rate = 44100.0;
    if (! readAudio (file, audio, rate))
    {
        skazon::MatchResult fail;
        fail.error = "Could not read " + file.getFileName();
        return fail;
    }
    auto result = matchSound (audio, rate, file.getFileNameWithoutExtension());
    if (! result.ok)
        return result;
    applyValues (result.values, 0.0f);
    storeUserTable (result.table, file, "match");
    previewOn = false;
    {
        const juce::SpinLock::ScopedLockType l (previewLock);
        previewNotes = result.notes;
        previewName = file.getFileNameWithoutExtension();
    }
    randomName = "MATCH // " + file.getFileNameWithoutExtension();
    apvts.state.setProperty ("randomName", randomName, nullptr);
    return result;
}

void SkazonAudioProcessor::mutate (float amount, int locks)
{
    auto group = [] (const juce::String& id)
    {
        if (id.startsWith ("fx") || id.startsWith ("dist") || id.startsWith ("eq") || id.startsWith ("ph") || id.startsWith ("ch")
            || id.startsWith ("hy") || id.startsWith ("ott") || id.startsWith ("dl") || id.startsWith ("rv"))
            return (int) lockFx;
        if (id.startsWith ("e1") || id.startsWith ("e2"))
            return (int) lockEnv;
        if (id.startsWith ("f"))
            return (int) lockFilter;
        if (id.startsWith ("l") || id.startsWith ("m") || id.startsWith ("macro"))
            return (int) lockMod;
        return (int) lockOsc;
    };
    auto& rng = juce::Random::getSystemRandom();
    for (auto* p : getParameters())
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (p);
        if (f == nullptr)
            continue;
        const auto id = f->getParameterID();
        if (id == "master" || id == "glide" || (group (id) & locks) != 0)
            continue;
        if (id.endsWith ("Amt") && id.startsWith ("m") && juce::CharacterFunctions::isDigit (id[1]) && (int) param (id.upToLastOccurrenceOf ("Amt", false, false) + "Src") == 0)
            continue; // empty matrix slot
        const float gauss = (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 1.5f) * 1.15f;
        f->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, f->convertTo0to1 (f->get()) + gauss * amount * 0.3f));
    }
    if (randomName.isEmpty())
        randomName = getProgramName (currentPreset);
    if (! randomName.endsWith (" ~"))
        randomName << " ~";
    apvts.state.setProperty ("randomName", randomName, nullptr);
}

bool SkazonAudioProcessor::loadUserTable (const juce::File& file, const juce::String& oscPrefix)
{
    juce::AudioBuffer<float> audio;
    double rate = 44100.0;
    if (! readAudio (file, audio, rate))
        return false;
    auto table = makeUserTable (audio, file.getFileNameWithoutExtension());
    if (table == nullptr)
        return false;
    storeUserTable (table, file, "chop");

    if (auto* p = apvts.getParameter (oscPrefix + "Table"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) userTableIndex));
    if (auto* on = apvts.getParameter (oscPrefix + "On"))
        on->setValueNotifyingHost (1.0f);
    return true;
}

juce::String SkazonAudioProcessor::userTableName() const
{
    const auto t = std::atomic_load (&userTable);
    return t != nullptr ? t->name : juce::String();
}

void SkazonAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SkazonAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset = apvts.state.getProperty ("preset", 0);
            randomName = apvts.state.getProperty ("randomName", "").toString();
            {
                std::vector<MacroLink> v;
                for (const auto& item : juce::StringArray::fromTokens (apvts.state.getProperty ("macroLinks", "").toString(), ";", ""))
                {
                    const auto parts = juce::StringArray::fromTokens (item, ":", "");
                    if (parts.size() == 3)
                        if (auto* prm = apvts.getParameter (parts[0]); prm != nullptr && juce::isPositiveAndBelow (parts[1].getIntValue(), numMacros))
                            v.push_back ({ parts[0], prm, parts[1].getIntValue(), juce::jlimit (-1.0f, 1.0f, parts[2].getFloatValue()) });
                }
                storeLinks (std::move (v));
            }
            const juce::File table (apvts.state.getProperty ("userTable", "").toString());
            const bool matched = apvts.state.getProperty ("userTableMode", "chop").toString() == "match";
            juce::AudioBuffer<float> audio;
            double rate = 44100.0;
            if (table.existsAsFile() && readAudio (table, audio, rate))
            {
                // Rebuild the table only; the saved knobs stay as they are.
                std::shared_ptr<const skazon::UserTable> t;
                if (matched)
                {
                    auto m = matchSound (audio, rate, table.getFileNameWithoutExtension());
                    t = m.table;
                    const juce::SpinLock::ScopedLockType l (previewLock); // MIDI button works again after a project reload
                    previewNotes = m.notes;
                    previewName = table.getFileNameWithoutExtension();
                }
                else
                    t = makeUserTable (audio, table.getFileNameWithoutExtension());
                if (t != nullptr)
                {
                    previousUserTable = std::atomic_load (&userTable);
                    std::atomic_store (&userTable, t);
                }
            }
        }
}

juce::AudioProcessorEditor* SkazonAudioProcessor::createEditor()
{
    return skazi::withUpdateNotice (new SkazonAudioProcessorEditor (*this));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SkazonAudioProcessor();
}
