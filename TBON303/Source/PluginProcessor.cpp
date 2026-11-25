#include "PluginProcessor.h"
#include "../../Common/UpdateNotice.h"
#include "PluginEditor.h"

const juce::StringArray Skazi303Processor::distTypes { "Tube", "Fuzz", "Crush", "Fold", "Overdrive", "Distortion",
                                                       "Super Dist", "Ring Mod", "Rectifier", "Diode",
                                                       "Tape", "Transistor", "Germanium", "Crunch" };
const juce::StringArray Skazi303Processor::filter2Types { "Off", "Ladder 24", "Formant" };
const juce::StringArray Skazi303Processor::modModes { "Off", "Chorus", "Flanger" };
const juce::StringArray Skazi303Processor::delaySyncs { "1/64", "1/32 T", "1/32", "1/16 T", "1/16", "1/16 D", "1/8 T", "1/8",
                                                        "1/8 D", "1/4 T", "1/4", "1/4 D", "1/2", "1/2 D", "1 Bar" };
const juce::StringArray Skazi303Processor::seqRates { "1/32", "1/16", "1/16 T", "1/8" };
const juce::StringArray Skazi303Processor::seqModes { "Host Sync", "Internal" };
const juce::StringArray Skazi303Processor::noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

namespace
{
// Delay lengths in beats, matching delaySyncs (T = triplet, D = dotted).
constexpr double delayBeats[] = { 1.0 / 16.0, 1.0 / 12.0, 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5,
                                  0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
constexpr double stepsPerBeat[] = { 8.0, 4.0, 6.0, 2.0 };

// A classic-sounding default line so the plugin plays something on first load.
constexpr int defNote[16]  = { 0, 0, 12, 0, 3, 0, 7, 10, 0, 0, 12, 3, 5, 0, 7, 0 };
constexpr int defOct[16]   = { 0, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1 };
constexpr bool defGate[16] = { 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1 };
constexpr bool defAcc[16]  = { 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0 };
constexpr bool defSld[16]  = { 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1 };

juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout Skazi303Processor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;

    auto flt = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* label = "")
    {
        ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                             AudioParameterFloatAttributes().withLabel (label)));
    };
    auto pct = [&] (const char* id, const char* name, float def) { flt (id, name, { 0.0f, 1.0f }, def); };
    auto choice = [&] (const char* id, const char* name, const StringArray& items, int def)
    {
        ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

    // Oscillator
    pct ("wave", "Waveform", 0.0f);
    flt ("tune", "Tune", { -24.0f, 24.0f, 1.0f }, 0.0f, "st");
    flt ("fine", "Fine", { -100.0f, 100.0f }, 0.0f, "ct");
    pct ("drift", "Drift", 0.3f);
    pct ("sub_level", "Sub Level", 0.0f);
    choice ("sub_wave", "Sub Wave", juce::StringArray { "Sine", "Triangle", "Square", "Saw" }, 0);
    choice ("sub_oct", "Sub Octave", juce::StringArray { "-1 Oct", "-2 Oct" }, 0);

    // Circuit mods
    pct ("flt_track", "Filter Tracking", 0.0f);
    flt ("acc_decay", "Accent Decay", skewed (0.05f, 1.0f, 0.2f), 0.2f, "s");
    flt ("soft_attack", "Amp Attack", skewed (0.5f, 5000.0f, 40.0f), 3.0f, "ms");
    flt ("sweep", "Sweep Speed", skewed (0.25f, 4.0f, 1.0f), 1.0f, "x");
    pct ("pulse_width", "Pulse Width", 0.0f);
    pct ("noise", "Noise", 0.0f);
    flt ("vca_decay", "VCA Decay", skewed (0.1f, 8.0f, 1.5f), 2.5f, "s");

    // Envelopes. The defaults are the stock 303: no sustain, a snappy release, an instant filter attack.
    // Wheels
    flt ("bend_up", "Bend Up", { 0.0f, 24.0f, 1.0f }, 2.0f, "st");
    flt ("bend_down", "Bend Down", { 0.0f, 24.0f, 1.0f }, 2.0f, "st");
    choice ("mw_dest", "Mod Wheel", juce::StringArray { "Cutoff", "Vibrato", "Cutoff + Vibrato" }, 0);
    pct ("mw_amt", "Mod Wheel Amount", 1.0f);

    pct ("amp_sus", "Amp Sustain", 0.0f);
    flt ("amp_rel", "Amp Release", skewed (1.0f, 5000.0f, 150.0f), 4.0f, "ms");
    flt ("flt_att", "Filter Attack", skewed (0.0f, 3000.0f, 150.0f), 0.0f, "ms");
    pct ("flt_sus", "Filter Sustain", 0.0f);
    flt ("flt_rel", "Filter Release", skewed (5.0f, 5000.0f, 300.0f), 300.0f, "ms");
    pct ("fat", "Fat (Tube)", 0.15f);

    // Diode filter + 303 envelope
    flt ("cutoff", "Cutoff", skewed (20.0f, 14000.0f, 600.0f), 420.0f, "Hz");
    pct ("reso", "Resonance", 0.75f);
    pct ("envmod", "Env Mod", 0.62f);
    flt ("decay", "Decay", skewed (0.1f, 2.5f, 0.5f), 0.45f, "s");
    pct ("accent", "Accent", 0.75f);
    flt ("slide_time", "Slide Time", skewed (10.0f, 300.0f, 60.0f), 60.0f, "ms");

    // Filter 2
    choice ("f2_type", "Filter 2 Type", filter2Types, 0);
    flt ("f2_cutoff", "Filter 2 Cutoff", skewed (80.0f, 12000.0f, 1000.0f), 1200.0f, "Hz");
    pct ("f2_reso", "Filter 2 Reso", 0.4f);
    flt ("f2_vowel", "Vowel", { 0.0f, 4.0f }, 0.0f);
    pct ("f2_mix", "Filter 2 Mix", 1.0f);

    // Drive
    choice ("pre_type", "Pre Drive Type", distTypes, 0);
    pct ("pre_drive", "Pre Drive", 0.0f);
    choice ("post_type", "Post Drive Type", distTypes, 0);
    pct ("post_drive", "Post Drive", 0.3f);
    flt ("dist_tone", "Drive Tone", skewed (500.0f, 16000.0f, 3500.0f), 6000.0f, "Hz");
    pct ("dist_mix", "Drive Mix", 1.0f);

    // FX
    choice ("mod_mode", "Mod FX", modModes, 0);
    flt ("mod_rate", "Mod Rate", skewed (0.05f, 5.0f, 0.6f), 0.4f, "Hz");
    pct ("mod_depth", "Mod Depth", 0.5f);
    pct ("mod_mix", "Mod Mix", 0.5f);
    choice ("dly_sync", "Delay Time", delaySyncs, 8);
    pct ("dly_fb", "Delay Feedback", 0.4f);
    flt ("dly_tone", "Delay Tone", skewed (600.0f, 12000.0f, 2500.0f), 2800.0f, "Hz");
    pct ("dly_mix", "Delay Mix", 0.0f);
    pct ("rev_size", "Reverb Size", 0.35f);
    pct ("rev_damp", "Reverb Damping", 0.55f);
    pct ("rev_mix", "Reverb Mix", 0.0f);
    flt ("width", "Width", { 0.0f, 2.0f }, 1.0f);
    flt ("volume", "Volume", { -36.0f, 12.0f }, -6.0f, "dB");

    // Sequencer
    // Off by default: the plugin loads as a normal MIDI-played synth.
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "seq_on", 1 }, "Sequencer On", false));
    choice ("seq_mode", "Seq Sync", seqModes, 0);
    ps.push_back (std::make_unique<AudioParameterInt> (ParameterID { "seq_len", 1 }, "Seq Length", 1, s303::kMaxSteps, 16));
    choice ("seq_rate", "Seq Rate", seqRates, 1);
    pct ("swing", "Swing", 0.0f);
    flt ("gate_len", "Gate Length", { 0.1f, 0.95f }, 0.5f);
    choice ("root", "Root Key", noteNames, 0);
    flt ("int_bpm", "Internal BPM", { 60.0f, 200.0f, 0.1f }, 128.0f, "bpm");

    StringArray scaleNames;
    for (auto& sc : s303::scales())
        scaleNames.add (sc.name);
    choice ("gen_scale", "Gen Scale", scaleNames, 0);
    pct ("gen_density", "Gen Density", 0.75f);
    pct ("gen_accent", "Gen Accents", 0.3f);
    pct ("gen_slide", "Gen Slides", 0.2f);
    pct ("gen_octave", "Gen Octaves", 0.25f);

    for (int i = 0; i < s303::kMaxSteps; ++i)
    {
        const String n = "Step " + String (i + 1) + " ";
        ps.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::step (i, "note"), 1 }, n + "Note", 0, 12, defNote[i]));
        ps.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::step (i, "oct"), 1 }, n + "Octave", -1, 1, defOct[i]));
        ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::step (i, "gate"), 1 }, n + "Gate", defGate[i]));
        ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::step (i, "acc"), 1 }, n + "Accent", defAcc[i]));
        ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::step (i, "sld"), 1 }, n + "Slide", defSld[i]));
    }

    // POWER (added in 1.3, kept last so old projects and presets load unchanged; all off by default)
    choice ("drive2_type", "Drive 2 Type", distTypes, 5); // Distortion
    pct ("drive2", "Drive 2", 0.0f);
    pct ("ott_depth", "OTT Depth", 0.0f);
    pct ("ott_time", "OTT Time", 0.3f);
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "turbo", 1 }, "Turbo", false));

    return { ps.begin(), ps.end() };
}

Skazi303Processor::Skazi303Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SKAZI303", createLayout())
{
    auto get = [this] (const juce::String& id)
    {
        auto* ptr = apvts.getRawParameterValue (id);
        jassert (ptr != nullptr);
        return ptr;
    };

    p.wave = get ("wave");          p.tune = get ("tune");          p.fine = get ("fine");
    p.drift = get ("drift");        p.subLevel = get ("sub_level"); p.subWave = get ("sub_wave");    p.subOct = get ("sub_oct");
    p.fltTrack = get ("flt_track");  p.accDecay = get ("acc_decay");  p.softAttack = get ("soft_attack");
    p.sweep = get ("sweep");          p.pulseWidth = get ("pulse_width"); p.noise = get ("noise");
    p.vcaDecay = get ("vca_decay");    p.fat = get ("fat");
    p.bendUp = get ("bend_up");       p.bendDown = get ("bend_down");
    p.mwDest = get ("mw_dest");       p.mwAmt = get ("mw_amt");
    p.ampSus = get ("amp_sus");       p.ampRel = get ("amp_rel");
    p.fltAtt = get ("flt_att");       p.fltSus = get ("flt_sus");       p.fltRel = get ("flt_rel");
    p.cutoff = get ("cutoff");      p.reso = get ("reso");          p.envMod = get ("envmod");
    p.decay = get ("decay");        p.accent = get ("accent");      p.slideTime = get ("slide_time");
    p.f2Type = get ("f2_type");     p.f2Cutoff = get ("f2_cutoff"); p.f2Reso = get ("f2_reso");
    p.f2Vowel = get ("f2_vowel");   p.f2Mix = get ("f2_mix");
    p.preType = get ("pre_type");   p.preDrive = get ("pre_drive");
    p.postType = get ("post_type"); p.postDrive = get ("post_drive");
    p.distTone = get ("dist_tone"); p.distMix = get ("dist_mix");
    p.modMode = get ("mod_mode");   p.modRate = get ("mod_rate");   p.modDepth = get ("mod_depth");
    p.modMix = get ("mod_mix");
    p.dlySync = get ("dly_sync");   p.dlyFb = get ("dly_fb");       p.dlyTone = get ("dly_tone");
    p.dlyMix = get ("dly_mix");     p.revSize = get ("rev_size");   p.revDamp = get ("rev_damp");   p.revMix = get ("rev_mix");
    p.width = get ("width");        p.volume = get ("volume");
    p.seqOn = get ("seq_on");       p.seqMode = get ("seq_mode");   p.seqLen = get ("seq_len");     p.seqRate = get ("seq_rate");
    p.swing = get ("swing");        p.gateLen = get ("gate_len");   p.root = get ("root");
    p.intBpm = get ("int_bpm");
    p.genScale = get ("gen_scale"); p.genDensity = get ("gen_density"); p.genAccent = get ("gen_accent");
    p.genSlide = get ("gen_slide"); p.genOctave = get ("gen_octave");
    p.drive2Type = get ("drive2_type"); p.drive2 = get ("drive2");
    p.ottDepth = get ("ott_depth");     p.ottTime = get ("ott_time");   p.turbo = get ("turbo");

    for (int i = 0; i < s303::kMaxSteps; ++i)
    {
        p.note[(size_t) i] = get (ids::step (i, "note"));
        p.octave[(size_t) i] = get (ids::step (i, "oct"));
        p.gate[(size_t) i] = get (ids::step (i, "gate"));
        p.acc[(size_t) i] = get (ids::step (i, "acc"));
        p.slide[(size_t) i] = get (ids::step (i, "sld"));
    }

    // Start on the first factory preset, so "program 0" really is that sound.
    loadFactoryPreset (0);
}

bool Skazi303Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void Skazi303Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);

    // 2x oversampling around the voice: the resonant filter and drive stages alias
    // badly at 1x. Polyphase IIR keeps the latency to a few samples.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampling->initProcessing ((size_t) maxBlock);
    monoBuffer.setSize (1, maxBlock);
    setLatencySamples ((int) std::ceil (oversampling->getLatencyInSamples()));

    voice.prepare (sr * 2.0);
    modFx.prepare (sr);
    delay.prepare (sr);
    width.prepare (sr);
    fatStage.prepare (sr);
    drive2.prepare (sr * 2.0);
    ott.prepare (sr);
    powerMatch.reset();
    powerGain.reset (sr, 0.05);
    powerGain.setCurrentAndTargetValue (1.0f);
    powerWasOn = false;
    reverb.setSampleRate (sr);
    reverb.reset();

    sequencer.reset();
    heldCount = 0;
    internalPpq = 0.0;
    lastSeqMode = -1;
    lastSeqActive = false;

    outGain.reset (sr, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.volume->load()));
}

s303::VoiceParams Skazi303Processor::readVoiceParams() const
{
    s303::VoiceParams v;
    v.wave = p.wave->load();
    v.tuneSemis = p.tune->load();
    v.fineCents = p.fine->load();
    v.drift = p.drift->load();
    v.subLevel = p.subLevel->load();
    v.subWave = (int) p.subWave->load();
    v.subOctaves = 1 + (int) p.subOct->load();
    v.filterTrack = p.fltTrack->load();
    v.accentDecaySec = p.accDecay->load();
    v.softAttackSec = p.softAttack->load() * 0.001f;
    v.ampSustain = p.ampSus->load();
    v.ampReleaseSec = p.ampRel->load() * 0.001f;
    v.filterAttackSec = p.fltAtt->load() * 0.001f;
    v.filterSustain = p.fltSus->load();
    v.filterReleaseSec = p.fltRel->load() * 0.001f;
    v.sweepSpeed = p.sweep->load();
    v.pulseWidth = p.pulseWidth->load();
    v.noise = p.noise->load();
    v.ampDecaySec = p.vcaDecay->load();
    v.cutoffHz = p.cutoff->load();
    v.resonance = p.reso->load();
    v.envMod = p.envMod->load();
    v.decaySec = p.decay->load();
    v.accent = p.accent->load();
    v.slideSec = p.slideTime->load() * 0.001f;
    v.f2Type = (s303::Filter2Type) (int) p.f2Type->load();
    v.f2CutoffHz = p.f2Cutoff->load();
    v.f2Resonance = p.f2Reso->load();
    v.f2Vowel = p.f2Vowel->load();
    v.f2Mix = p.f2Mix->load();
    v.preType = (s303::DistType) (int) p.preType->load();
    v.preDrive = p.preDrive->load();
    v.postType = (s303::DistType) (int) p.postType->load();
    v.postDrive = p.postDrive->load();
    v.postToneHz = p.distTone->load();
    v.postMix = p.distMix->load();
    return v;
}

s303::GenSettings Skazi303Processor::readGenSettings() const
{
    s303::GenSettings g;
    g.scale = (int) p.genScale->load();
    g.density = p.genDensity->load();
    g.accent = p.genAccent->load();
    g.slide = p.genSlide->load();
    g.octave = p.genOctave->load();
    return g;
}

s303::Pattern Skazi303Processor::readPattern() const
{
    s303::Pattern pat;
    pat.length = (int) p.seqLen->load();
    for (size_t i = 0; i < (size_t) s303::kMaxSteps; ++i)
    {
        auto& s = pat.steps[i];
        s.note = (int) p.note[i]->load();
        s.octave = (int) p.octave[i]->load();
        s.gate = p.gate[i]->load() > 0.5f;
        s.accent = p.acc[i]->load() > 0.5f;
        s.slide = p.slide[i]->load() > 0.5f;
    }
    return pat;
}

void Skazi303Processor::setParam (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        const float norm = prm->convertTo0to1 (value);
        if (std::abs (prm->getValue() - norm) < 1.0e-6f)
            return;
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (norm);
        prm->endChangeGesture();
    }
}

void Skazi303Processor::writePattern (const s303::Pattern& pat)
{
    for (int i = 0; i < s303::kMaxSteps; ++i)
    {
        const auto& s = pat.steps[i];
        setParam (ids::step (i, "note"), (float) s.note);
        setParam (ids::step (i, "oct"), (float) s.octave);
        setParam (ids::step (i, "gate"), s.gate ? 1.0f : 0.0f);
        setParam (ids::step (i, "acc"), s.accent ? 1.0f : 0.0f);
        setParam (ids::step (i, "sld"), s.slide ? 1.0f : 0.0f);
    }
}

void Skazi303Processor::generatePattern()
{
    auto pat = readPattern();
    generator.generate (pat, readGenSettings());
    writePattern (pat);
}

void Skazi303Processor::mutatePattern()
{
    auto pat = readPattern();
    generator.mutate (pat, readGenSettings());
    writePattern (pat);
}

void Skazi303Processor::clearPattern()
{
    s303::Pattern pat;
    for (auto& s : pat.steps)
        s = s303::Step {};
    writePattern (pat);
}

void Skazi303Processor::shiftPattern (int direction)
{
    const auto src = readPattern();
    auto dst = src;
    const int len = juce::jlimit (1, s303::kMaxSteps, src.length);
    for (int i = 0; i < len; ++i)
        dst.steps[(i + direction + len) % len] = src.steps[i];
    writePattern (dst);
}

void Skazi303Processor::applyWheels()
{
    const float b = wheelBend.load();
    voice.setBend (b >= 0.0f ? b * p.bendUp->load() : b * p.bendDown->load());
    voice.setModWheel (wheelMod.load(), (int) p.mwDest->load(), p.mwAmt->load());
}

void Skazi303Processor::handleMidi (const juce::MidiMessage& m, bool sequencerActive)
{
    if (m.isPitchWheel())
    {
        wheelBend.store ((float) juce::jlimit (-1.0, 1.0, (m.getPitchWheelValue() - 8192) / 8191.0));
        applyWheels();
        return;
    }
    if (m.isController() && m.getControllerNumber() == 1)
    {
        wheelMod.store ((float) m.getControllerValue() / 127.0f);
        applyWheels();
        return;
    }

    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        if (sequencerActive)
        {
            // While the sequencer runs, keys transpose the pattern (C3 = no change).
            midiTranspose = juce::jlimit (-24, 24, note - 60);
            return;
        }

        // 303-style keyboard play: legato = slide, hard hits = accent.
        const bool legato = heldCount > 0;
        if (heldCount < (int) heldNotes.size())
            heldNotes[(size_t) heldCount++] = note;
        voice.noteOn (note, m.getVelocity() >= 100, legato);
    }
    else if (m.isNoteOff())
    {
        if (sequencerActive)
            return;

        const int note = m.getNoteNumber();
        const bool wasTop = heldCount > 0 && heldNotes[(size_t) heldCount - 1] == note;
        for (int i = 0; i < heldCount; ++i)
        {
            if (heldNotes[(size_t) i] == note)
            {
                for (int j = i; j < heldCount - 1; ++j)
                    heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                --heldCount;
                break;
            }
        }

        if (heldCount == 0)
            voice.noteOff();
        else if (wasTop)
            voice.noteOn (heldNotes[(size_t) heldCount - 1], false, true);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldCount = 0;
        voice.noteOff();
    }
}

void Skazi303Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - start);
        renderChunk (left + start, right != nullptr ? right + start : nullptr, n, midi, start);
    }

    midi.clear();
}

void Skazi303Processor::renderChunk (float* left, float* right, int numSamples, juce::MidiBuffer& midi, int midiOffset)
{
    const auto vp = readVoiceParams();
    const auto pat = readPattern();
    applyWheels(); // the on-screen wheels and range knobs; MIDI wheel moves also apply right away

    const bool seqOn = p.seqOn->load() > 0.5f;
    const int seqMode = (int) p.seqMode->load(); // 0 = follow host transport, 1 = internal clock
    const double rateSteps = stepsPerBeat[juce::jlimit (0, 3, (int) p.seqRate->load())];
    const float swing = p.swing->load();
    const float gateLen = p.gateLen->load();
    const int baseNote = 36 + (int) p.root->load() + midiTranspose;

    if (seqMode != lastSeqMode)
    {
        internalPpq = 0.0;
        lastSeqMode = seqMode;
    }

    // Host transport
    double bpm = 120.0, ppq0 = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq0 = *q;
            hostPlaying = pos->getIsPlaying();
        }
    }
    // Later chunks of one host block start part-way through it.
    ppq0 += midiOffset * bpm / (60.0 * sr);

    if (seqMode == 1)
        bpm = p.intBpm->load();

    // The sequencer only takes over while it is switched on and actually running;
    // the rest of the time MIDI notes play the synth directly.
    const bool seqActive = seqOn && (seqMode == 1 || hostPlaying);
    if (seqActive != lastSeqActive)
    {
        sequencer.stop();
        voice.noteOff();
        heldCount = 0;
        if (seqActive)
            internalPpq = 0.0;
        else
            midiTranspose = 0;
        lastSeqActive = seqActive;
    }

    const double ppqPerSample = bpm / (60.0 * sr);

    // POWER section settings. TURBO = DRIVE 2 and OTT pushed hard at once.
    const bool turbo = p.turbo->load() > 0.5f;
    const auto d2Type = (s303::DistType) (int) p.drive2Type->load();
    const float d2Amt = turbo ? juce::jmax (p.drive2->load(), 0.5f) : p.drive2->load();
    const float ottAmt = turbo ? juce::jmax (p.ottDepth->load(), 0.8f) : p.ottDepth->load();
    const float ottTime = p.ottTime->load();
    const bool powerOn = d2Amt > 0.0001f || ottAmt > 0.0001f;
    if (powerOn != powerWasOn)
    {
        drive2.reset();
        ott.reset();
        powerMatch.reset();
        powerGain.setCurrentAndTargetValue (1.0f);
        powerWasOn = powerOn;
    }
    double powerIn = 0.0, powerOut = 0.0;

    // Run the voice at 2x: upsample a silent block, overwrite it, then downsample.
    monoBuffer.clear();
    juce::dsp::AudioBlock<float> block (monoBuffer.getArrayOfWritePointers(), 1, (size_t) numSamples);
    auto osBlock = oversampling->processSamplesUp (block);
    float* os = osBlock.getChannelPointer (0);

    auto midiIt = midi.findNextSamplePosition (midiOffset);
    const auto midiEnd = midi.cend();

    for (int i = 0; i < numSamples; ++i)
    {
        while (midiIt != midiEnd && (*midiIt).samplePosition <= midiOffset + i)
        {
            handleMidi ((*midiIt).getMessage(), seqActive);
            ++midiIt;
        }

        if (seqActive)
        {
            const double ppq = seqMode == 0 ? ppq0 + i * ppqPerSample : internalPpq;
            const auto ev = sequencer.tick (ppq * rateSteps, pat, swing, gateLen, baseNote);
            if (ev.type == s303::SeqEvent::NoteOn)
                voice.noteOn (ev.note, ev.accent, ev.slide);
            else if (ev.type == s303::SeqEvent::NoteOff)
                voice.noteOff();

            if (seqMode == 1)
                internalPpq += ppqPerSample;
        }

        float a = voice.render (vp), b = voice.render (vp);
        if (powerOn)
        {
            powerIn += (double) a * a + (double) b * b;
            a = drive2.process (a, d2Type, d2Amt);
            b = drive2.process (b, d2Type, d2Amt);
        }
        os[2 * i] = a;
        os[2 * i + 1] = b;
    }

    oversampling->processSamplesDown (block);
    float* mono = monoBuffer.getWritePointer (0);

    if (powerOn)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            mono[i] = ott.process (mono[i], ottAmt, ottTime);
            powerOut += (double) mono[i] * mono[i];
        }
        const bool wasSilent = ! powerMatch.isPrimed();
        const auto g = (float) powerMatch.update (powerIn / (2.0 * numSamples), powerOut / numSamples, numSamples / sr);
        if (wasSilent) powerGain.setCurrentAndTargetValue (g); // first block after switching on: no ramp from unity
        else powerGain.setTargetValue (g);
        for (int i = 0; i < numSamples; ++i)
            mono[i] *= powerGain.getNextValue();
    }

    currentStep.store (seqActive ? sequencer.getCurrentIndex() : -1);
    filterActivity.store ((float) voice.getFilterEnv());

    // FX chain at base rate.
    const int modMode = (int) p.modMode->load();
    const float modRate = p.modRate->load(), modDepth = p.modDepth->load(), modMix = p.modMix->load();
    const double dlyTime = delayBeats[juce::jlimit (0, 14, (int) p.dlySync->load())] * 60.0 / bpm;
    const float dlyFb = p.dlyFb->load(), dlyTone = p.dlyTone->load(), dlyMix = p.dlyMix->load();
    const float widthAmt = p.width->load();
    const float fatAmt = p.fat->load();

    juce::Reverb::Parameters rp;
    rp.roomSize = 0.15f + 0.8f * p.revSize->load(); // small room up to a big hall
    rp.damping = p.revDamp->load();
    float revDry, revWet;
    s303::mixGains (p.revMix->load(), revDry, revWet);
    rp.wetLevel = revWet * 0.45f;
    rp.dryLevel = revDry;
    rp.width = 1.0f;
    reverb.setParameters (rp);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (p.volume->load()));

    for (int i = 0; i < numSamples; ++i)
    {
        float l, r;
        modFx.process (fatStage.process (mono[i], fatAmt), modMode, modRate, modDepth, modMix, l, r);
        delay.process (l, r, dlyTime, dlyFb, dlyTone, dlyMix, l, r);
        left[i] = l;
        if (right != nullptr)
            right[i] = r;
    }

    if (right != nullptr)
        reverb.processStereo (left, right, numSamples);
    else
        reverb.processMono (left, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGain.getNextValue();
        if (right != nullptr)
        {
            width.process (left[i], right[i], widthAmt);
            right[i] *= g;
        }
        left[i] *= g;
    }
}

//==============================================================================
const juce::String Skazi303Processor::getProgramName (int index)
{
    const auto pre = s303::factoryPreset (index);
    return juce::String (pre.category) + " - " + juce::String (pre.name);
}

void Skazi303Processor::applyPreset (const s303::Preset& preset, bool soundOnly)
{
    for (auto& [id, value] : preset.values)
    {
        const juce::String pid (id);
        const bool sequencerSetting = pid.startsWith ("seq_") || pid.startsWith ("gen_")
                                   || pid == "swing" || pid == "gate_len" || pid == "root";
        if (soundOnly && sequencerSetting)
            continue;
        setParam (pid, value);
    }

    if (! soundOnly)
        writePattern (preset.pattern);
}

void Skazi303Processor::setPresetLabel (int index, const juce::String& name)
{
    apvts.state.setProperty ("presetIndex", index, nullptr);
    apvts.state.setProperty ("presetName", name, nullptr);
}

void Skazi303Processor::setCurrentProgram (int index)
{
    // Hosts can change programs from the audio thread (VST3 program parameter). Loading a preset
    // sets ~150 parameters, so do it on the message thread instead, never inside the audio callback.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        pendingProgram = -1;
        loadFactoryPreset (index);
    }
    else
    {
        pendingProgram = juce::jlimit (0, s303::numFactoryPresets() - 1, index);
        triggerAsyncUpdate();
    }
}

void Skazi303Processor::handleAsyncUpdate()
{
    const int index = pendingProgram.exchange (-1);
    if (index >= 0)
        loadFactoryPreset (index);
}

void Skazi303Processor::selectPreset (int index)
{
    loadFactoryPreset (index);
    // Keep the host's program in step, or it may later "restore" the old one
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void Skazi303Processor::loadFactoryPreset (int index)
{
    index = juce::jlimit (0, s303::numFactoryPresets() - 1, index);
    const auto preset = s303::factoryPreset (index);
    applyPreset (preset, false);
    levelCurrentSound (index);
    currentProgram = index;
    setPresetLabel (index, juce::String (preset.category) + " / " + juce::String (preset.name));
}

void Skazi303Processor::randomizeSound()
{
    const auto preset = s303::randomPreset (juce::Random::getSystemRandom().nextInt() & 0x7fffffff);
    applyPreset (preset, true);
    levelCurrentSound();
    setPresetLabel (-1, juce::String (preset.name) + " *");
}

void Skazi303Processor::levelCurrentSound (int factoryIndex)
{
    // Factory presets never change, so each one is measured once and remembered.
    if (factoryIndex >= 0)
    {
        if (factoryLevelCache.empty())
            factoryLevelCache.assign ((size_t) s303::numFactoryPresets(), std::numeric_limits<float>::quiet_NaN());
        auto& cached = factoryLevelCache[(size_t) factoryIndex];
        if (std::isnan (cached))
            cached = measureLevelDb();
        setParam ("volume", cached);
        return;
    }
    setParam ("volume", measureLevelDb());
}

float Skazi303Processor::measureLevelDb()
{
    // Play the current sound offline over its pattern (every step gated, so sparse
    // patterns don't read as quiet) and return the VOLUME that lands it at the shared level.
    constexpr double renderRate = 44100.0;
    constexpr double bpm = 125.0;
    constexpr double targetRms = 0.18; // about -15 dBFS for normal notes; accents sit a few dB above

    const auto vp = readVoiceParams();
    auto pat = readPattern();
    for (auto& s : pat.steps)
        s.gate = true;

    s303::AcidVoice v;
    v.prepare (renderRate * 2.0);
    s303::StepSequencer seq;
    seq.reset();
    // The same FX chain as the real output (FAT -> chorus/flanger -> delay -> reverb),
    // so wet presets are levelled by what you actually hear.
    s303::FatStage fat;
    s303::ModFX mod;
    s303::BBDDelay dly;
    juce::Reverb rev;
    const float fatAmt = p.fat->load();
    const int modMode = (int) p.modMode->load();
    const double dlyTime = delayBeats[juce::jlimit (0, 14, (int) p.dlySync->load())] * 60.0 / bpm;

    auto resetChain = [&]
    {
        fat.prepare (renderRate);
        mod.prepare (renderRate);
        dly.prepare (renderRate);
        rev.setSampleRate (renderRate);
        rev.reset();
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.15f + 0.8f * p.revSize->load();
        rp.damping = p.revDamp->load();
        float revDry, revWet;
        s303::mixGains (p.revMix->load(), revDry, revWet);
        rp.wetLevel = revWet * 0.45f;
        rp.dryLevel = revDry;
        rp.width = 1.0f;
        rev.setParameters (rp);
    };
    auto chain = [&] (float x)
    {
        float l, r;
        mod.process (fat.process (x, fatAmt), modMode, p.modRate->load(), p.modDepth->load(), p.modMix->load(), l, r);
        dly.process (l, r, dlyTime, p.dlyFb->load(), p.dlyTone->load(), p.dlyMix->load(), l, r);
        rev.processStereo (&l, &r, 1);
        return l;
    };

    resetChain();
    const int n = (int) (16.0 * 60.0 / bpm / 4.0 * renderRate); // one bar of 16ths
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto ev = seq.tick (i * bpm / 60.0 / renderRate * 4.0, pat, 0.0f, 0.5f, 36);
        if (ev.type == s303::SeqEvent::NoteOn) v.noteOn (ev.note, ev.accent, ev.slide);
        else if (ev.type == s303::SeqEvent::NoteOff) v.noteOff();

        const float a = v.render (vp);
        const float b = v.render (vp);
        const float y = chain (0.5f * (a + b));
        sum += (double) y * y;
    }

    const double patternRms = std::sqrt (sum / n);

    // A long held note can ring much louder than the pattern (resonant / vowel sounds),
    // so measure that too and level by whichever is louder.
    v.reset();
    resetChain();
    v.noteOn (45, false, false);
    sum = 0.0;
    const int held = (int) (0.3 * renderRate);
    for (int i = 0; i < held; ++i)
    {
        const float a = v.render (vp);
        const float b = v.render (vp);
        const float y = chain (0.5f * (a + b));
        sum += (double) y * y;
    }
    const double heldRms = std::sqrt (sum / held);

    const double rms = std::max (patternRms, heldRms);
    if (rms <= 1.0e-5)
        return p.volume->load();
    return (float) juce::jlimit (-30.0, 9.0, juce::Decibels::gainToDecibels (targetRms / rms));
}

juce::File Skazi303Processor::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("SKAZI 303").getChildFile ("Presets");
}

bool Skazi303Processor::saveUserPreset (const juce::File& file)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", "User / " + file.getFileNameWithoutExtension(), nullptr);
    state.setProperty ("presetIndex", -1, nullptr);
    if (auto xml = state.createXml())
    {
        file.getParentDirectory().createDirectory();
        if (xml->writeTo (file))
        {
            setPresetLabel (-1, "User / " + file.getFileNameWithoutExtension());
            return true;
        }
    }
    return false;
}

bool Skazi303Processor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    // Go through the parameters (not replaceState) so the host sees every change.
    const auto tree = juce::ValueTree::fromXml (*xml);
    for (const auto& child : tree)
        if (child.hasProperty ("id") && child.hasProperty ("value"))
            setParam (child["id"].toString(), (float) child["value"]);

    setPresetLabel (-1, "User / " + file.getFileNameWithoutExtension());
    return true;
}

juce::String Skazi303Processor::getPresetDisplayName() const
{
    const auto name = apvts.state.getProperty ("presetName").toString();
    const int index = apvts.state.getProperty ("presetIndex", -1);
    if (name.isEmpty())
        return "INIT";
    return index >= 0 ? juce::String (index + 1).paddedLeft ('0', 3) + "  " + name : name;
}

juce::AudioProcessorEditor* Skazi303Processor::createEditor()
{
    return skazi::withUpdateNotice (new Skazi303Editor (*this));
}

void Skazi303Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void Skazi303Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentProgram = juce::jmax (0, (int) apvts.state.getProperty ("presetIndex", 0));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Skazi303Processor();
}
