#include "PluginProcessor.h"
#if ! SKYZ_NO_EDITOR
 #include "PluginEditor.h"
 #include "../../Common/UpdateNotice.h"
#endif

using namespace skyz;

juce::AudioProcessorValueTreeState::ParameterLayout SkyzProcessor::layout()
{
    // shortcut: every parameter is normalised 0..1 in the host (the UI shows the real units); add value-to-text later
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& d : paramDefs)
    {
        juce::NormalisableRange<float> range (0.0f, 1.0f, d.steps > 1 ? 1.0f / (float) (d.steps - 1) : 0.0f);
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { d.id, 1 }, d.id, range, d.def));
    }
    return l;
}

#if SKYZ_CRASH_LOG
static void skyzCrashLog (void*)
{
    juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("SKYZ-crash.txt")
        .replaceWithText (juce::Time::getCurrentTime().toString (true, true) + juce::newLine + juce::SystemStats::getStackBacktrace());
}
#endif

SkyzProcessor::SkyzProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SKYZ", layout())
{
    for (int i = 0; i < NUM_PARAMS; ++i) raw[(size_t) i] = apvts.getRawParameterValue (paramDefs[i].id);
   #if SKYZ_CRASH_LOG
    juce::SystemStats::setApplicationCrashHandler (skyzCrashLog);
   #endif
    std::atomic_store (&lanes, std::shared_ptr<const Lanes> (std::make_shared<Lanes> (lanesFromPattern (0))));
    setCurrentProgram (0);
}

void SkyzProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synthPtr = std::make_unique<Synth>(); synthPtr->prepare ((float) sampleRate);
    fxPtr = std::make_unique<FxChain>(); fxPtr->prepare (sampleRate, samplesPerBlock);
    setChain (chain);
    arpPtr = std::make_unique<Arp>(); arpPtr->reset();
    keyboard.reset();
    freeBeat = 0; timeSec = 0; std::fill (std::begin (lfoFree), std::end (lfoFree), 0.0f); // a restart starts every clock from zero
}

void SkyzProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int i = 0; i < NUM_PARAMS; ++i) vals.v[i] = raw[(size_t) i]->load();

    double bpm = 120, ppq = -1; bool playing = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition(); q && playing) ppq = *q;
        }
    const double beat0 = ppq >= 0 ? ppq : freeBeat;
    freeBeat = beat0 + n * bpm / 60.0 / sr;
    bpmNow = bpm; ppqNow = beat0;

    // on-screen keyboard + wheels
    keyboard.processNextMidiBuffer (midi, 0, n, true);
    if (const int b = uiPitch.exchange (-1); b >= 0) midi.addEvent (juce::MidiMessage::pitchWheel (1, b), 0);
    if (const int w = uiMod.exchange (-1); w >= 0) midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, w), 0);

    audioLanes = std::atomic_load (&lanes);
    auto& arp = *arpPtr; auto& synth = *synthPtr; auto& fx = *fxPtr;
    arp.process (midi, n, sr, bpm, ppq, vals, *audioLanes);
    arpStep = arp.currentStep();

    // block-level modulation setup
    shared.p = &vals; shared.sr = (float) sr; shared.bpm = bpm;
    for (int m = 0; m < 4; ++m) shared.macro[m] = vals[P_mac1 + m];
    shared.numSlots = 0;
    for (int s = 0; s < 16; ++s)
    {
        const int src = vals.idx (P_m1Src + s * 3), dst = vals.idx (P_m1Dst + s * 3);
        if (src > 0 && dst > 0) shared.slots[shared.numSlots++] = { src, dst, vals.bi (P_m1Amt + s * 3) };
    }
    for (int i = 0; i < 4; ++i)
    {
        const int L = P_lfo1Shape + i * 6;
        const bool sync = vals.on (L + 4);
        const double div = divBeats (vals.list (L + 1, numDivs));
        shared.lfoHz[i] = sync ? (float) (bpm / 60.0 / div) : 0.02f * std::pow (1000.0f, vals[L + 1]);
        // the shared phase locks to the bar while the host plays, so non-retrig LFOs sit on the grid
        if (sync && ppq >= 0) lfoFree[i] = (float) std::fmod (ppq / div + vals[L + 3], 1.0);
        else { lfoFree[i] += shared.lfoHz[i] * (float) n / (float) sr; lfoFree[i] -= std::floor (lfoFree[i]); }
        shared.lfoGlobal[i] = lfoFree[i];
        lfoPhase[i] = lfoFree[i];
    }
    timeSec += (float) n / (float) sr;
    if (timeSec > 10000.0f) timeSec = 0;
    shared.time = timeSec;

    // render with sample-accurate MIDI
    buffer.clear();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    juce::AudioBuffer<float> mono;
    if (r == nullptr) { mono.setSize (1, n, false, false, true); mono.clear(); r = mono.getWritePointer (0); }
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { synth.render (l + pos, r + pos, at - pos, shared); pos = at; }
        synth.handle (meta.getMessage(), shared);
    }
    if (pos < n) synth.render (l + pos, r + pos, n - pos, shared);
    wheelP = 0.5f + synth.wheelBend * 0.5f; wheelM = synth.wheelMod;
    voices = synth.activeCount();

    fx.trimDb = juce::Decibels::gainToDecibels (trimGain.load(), -100.0f);
    if (buffer.getNumChannels() < 2) { for (int i = 0; i < n; ++i) l[i] = 0.5f * (l[i] + r[i]); return; }

    FxContext ctx; ctx.bpm = bpm; ctx.beat = beat0; ctx.sr = (float) sr;
    fx.process (buffer, vals, ctx);
    peakL = fx.peakL.load(); peakR = fx.peakR.load();
}

//==============================================================================
void SkyzProcessor::applyValues (const std::vector<std::pair<int, float>>& v)
{
    std::vector<float> target (NUM_PARAMS);
    for (int i = 0; i < NUM_PARAMS; ++i) target[(size_t) i] = paramDefs[i].def;
    for (auto& [id, x] : v) if (id >= 0 && id < NUM_PARAMS) target[(size_t) id] = x;
    for (int i = 0; i < NUM_PARAMS; ++i)
        if (i != P_uiEnv && i != P_uiLfo) // page selectors are not part of a sound
            if (auto* p = apvts.getParameter (paramDefs[i].id)) p->setValueNotifyingHost (target[(size_t) i]);
}

void SkyzProcessor::applyPreset (const Preset& p)
{
    applyValues (p.values);
    std::atomic_store (&lanes, std::shared_ptr<const Lanes> (std::make_shared<Lanes> (p.lanes)));
    trimGain = juce::Decibels::decibelsToGain (p.trimDb);
    presetName = p.name;
    lanesChanged = true;
}

juce::Array<juce::File> SkyzProcessor::userPresetFiles()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SKY Z").getChildFile ("Presets");
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.skyz");
    files.sort();
    return files;
}

juce::StringArray SkyzProcessor::presetNames()
{
    juce::StringArray s;
    for (auto& p : factoryPresets()) s.add (p.name);
    for (auto& f : userPresetFiles()) s.add ("USER // " + f.getFileNameWithoutExtension());
    return s;
}

const juce::String SkyzProcessor::getProgramName (int i)
{
    const auto& f = factoryPresets();
    if (i >= 0 && i < (int) f.size()) return f[(size_t) i].name;
    auto u = userPresetFiles();
    i -= (int) f.size();
    return i >= 0 && i < u.size() ? "USER // " + u[i].getFileNameWithoutExtension() : juce::String();
}

void SkyzProcessor::setCurrentProgram (int i)
{
    const auto& f = factoryPresets();
    if (i >= 0 && i < (int) f.size()) { currentPreset = i; applyPreset (f[(size_t) i]); return; }
    auto u = userPresetFiles();
    const int k = i - (int) f.size();
    if (k >= 0 && k < u.size())
        if (auto xml = juce::XmlDocument::parse (u[k])) { loadFullState (juce::ValueTree::fromXml (*xml)); currentPreset = i; presetName = "USER // " + u[k].getFileNameWithoutExtension(); }
}

void SkyzProcessor::randomPreset()
{
    // RANDOM: a new sound in a random category, with every parameter reshuffled (oscillators, filter, envelopes, LFOs, matrix, FX, arp)
    auto& rnd = juce::Random::getSystemRandom();
    Preset p;
    for (int attempt = 0; attempt < 5; ++attempt) // a sound that needs more than +29 dB is too weak to be useful: roll again
    {
        p = generatePreset (rnd.nextInt (presetCategories().size()), (uint32_t) rnd.nextInt() ^ (uint32_t) juce::Time::getMillisecondCounter());
        p.trimDb = measureTrimDb (p, sr);
        if (p.trimDb < 29.0f) break;
    }
    p.name = p.name + " *";
    applyPreset (p);
}

float SkyzProcessor::measureTrimDb (const Preset& pr, double rate)
{
    // Plays the patch in a private, silent instance exactly like the plugin does (arp and FX included), with the same rule as
    // `SkyzTest trim`: loudest 50 ms at -15 dBFS RMS. Two passes, because a very loud first pass is squashed by the limiter.
    float trim = 0;
    for (int pass = 0; pass < 6; ++pass) // until within 0.5 dB: a very loud patch is squashed by the limiter, so one pass falls short
    {
        auto tmp = std::make_unique<SkyzProcessor>();
        Preset p = pr; p.trimDb = trim;
        tmp->applyPreset (p);
        tmp->prepareToPlay (rate, 512);
        const bool bass = pr.name.contains ("BASS") || pr.name.startsWith ("ACID") || pr.name.contains ("RUMBLE");
        const bool slow = pr.name.contains ("PAD") || pr.name.contains ("ATMOS") || pr.name.contains ("RISER") || pr.name.contains ("RUMBLE");
        const std::vector<int> notes = bass ? std::vector<int> { 40 } : pr.name.startsWith ("PSY LEAD") ? std::vector<int> { 64 } : std::vector<int> { 52, 59, 64, 67 };
        juce::AudioBuffer<float> buf (2, 512);
        std::vector<double> ring (2400, 0.0); size_t ri = 0; double win = 0, best = 0;
        const int total = (int) (rate * (slow ? 2.5 : 1.4));
        for (int done = 0; done < total; done += 512)
        {
            juce::MidiBuffer m;
            if (done == 0) for (int n : notes) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
            tmp->processBlock (buf, m);
            for (int i = 0; i < 512; ++i)
            {
                const double e = 0.5 * ((double) buf.getSample (0, i) * buf.getSample (0, i) + (double) buf.getSample (1, i) * buf.getSample (1, i));
                win += e - ring[ri]; ring[ri] = e; ri = (ri + 1) % ring.size();
                best = juce::jmax (best, win / (double) ring.size());
            }
        }
        const float db = (float) juce::Decibels::gainToDecibels (std::sqrt (best), -200.0);
        if (db < -80.0f) return trim;
        const float err = -15.0f - db;
        trim = juce::jlimit (-30.0f, 30.0f, trim + err);
        if (std::abs (err) < 0.5f) break;
    }
    return trim;
}

void SkyzProcessor::mutate()
{
    // only the sound-shaping knobs move; choices, levels, the matrix and the arp stay
    static const int ids[] { P_aTilt, P_aSpread, P_aOdd, P_aMorph, P_aBlur, P_aShimmer, P_bDet, P_bPw, P_bFm, P_bPan, P_fCut, P_fRes, P_fDrive, P_fEnv,
                             P_nColor, P_xTone, P_cRate, P_cDepth, P_yFb, P_rSize, P_rDecay, P_rDamp, P_lfo1Depth, P_lfo2Depth, P_lfo3Depth, P_lfo4Depth };
    juce::Random rnd;
    for (int i : ids)
        if (auto* p = apvts.getParameter (paramDefs[i].id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + (rnd.nextFloat() - 0.5f) * 0.16f));
    presetName = presetName.trimCharactersAtEnd (" *") + " *";
}

juce::ValueTree SkyzProcessor::fullState()
{
    auto s = apvts.copyState();
    s.setProperty ("lanes", lanesString(), nullptr);
    s.setProperty ("chain", chain, nullptr);
    s.setProperty ("preset", currentPreset, nullptr);
    s.setProperty ("presetName", presetName, nullptr);
    s.setProperty ("trim", trimGain.load(), nullptr);
    return s;
}

void SkyzProcessor::loadFullState (const juce::ValueTree& t)
{
    if (! t.hasType (apvts.state.getType())) return;
    apvts.replaceState (t.createCopy());
    if (t.hasProperty ("lanes")) setLanes (t["lanes"].toString());
    if (t.hasProperty ("chain")) setChain (t["chain"].toString());
    currentPreset = t.getProperty ("preset", 0);
    presetName = t.getProperty ("presetName", "INIT").toString();
    trimGain = (float) t.getProperty ("trim", 1.0f);
    lanesChanged = true;
}

void SkyzProcessor::abSwap()
{
    auto now = fullState();
    if (! abOther.isValid()) abOther = now.createCopy();
    auto other = abOther;
    abOther = now.createCopy();
    loadFullState (other);
    abOnB = ! abOnB;
}

bool SkyzProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim()).substring (0, 64);
    if (clean.isEmpty()) return false;
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SKY Z").getChildFile ("Presets");
    dir.createDirectory();
    auto s = fullState();
    s.setProperty ("presetName", "USER // " + clean, nullptr);
    if (auto xml = s.createXml()) if (xml->writeTo (dir.getChildFile (clean + ".skyz"))) { presetName = "USER // " + clean; currentPreset = presetNames().indexOf (presetName); return true; }
    return false;
}

void SkyzProcessor::setLanes (const juce::String& s)
{
    auto l = std::make_shared<Lanes>();
    l->fromString (s);
    previousLanes = std::atomic_load (&lanes); // keep the old one alive until the audio thread has moved on
    std::atomic_store (&lanes, std::shared_ptr<const Lanes> (l));
}

void SkyzProcessor::setChain (const juce::String& s)
{
    static const juce::StringArray names { "DRIVE", "CHORUS", "PHASER", "DELAY", "REVERB", "GATE", "DYNAMICS", "EQ", "STEREO", "LO-FI" };
    auto parts = juce::StringArray::fromTokens (s, ",", "");
    std::array<int, FxChain::numUnits> order {};
    int k = 0;
    for (auto& p : parts) { const int i = names.indexOf (p.trim()); if (i >= 0 && k < FxChain::numUnits && std::find (order.begin(), order.begin() + k, i) == order.begin() + k) order[(size_t) k++] = i; }
    for (int i = 0; i < FxChain::numUnits && k < FxChain::numUnits; ++i) if (std::find (order.begin(), order.begin() + k, i) == order.begin() + k) order[(size_t) k++] = i;
    fxPtr->setOrder (order);
    juce::StringArray out; for (int i : order) out.add (names[i]);
    chain = out.joinIntoString (",");
}

juce::File SkyzProcessor::writeArpMidi() const
{
    // two bars of the lanes on C3, at the arp rate
    const auto L = *std::atomic_load (&lanes);
    Values v; for (int i = 0; i < NUM_PARAMS; ++i) v.v[i] = raw[(size_t) i]->load();
    const double stepB = divBeats (v.list (P_aRate, numDivs));
    const int length = 1 + (int) std::lround (v[P_aSteps] * 31.0f);
    juce::MidiMessageSequence seq;
    const int ppqn = 960;
    int s = 0;
    for (double t = 0; t < 8.0 - 1e-9; t += stepB, ++s)
    {
        const int li = s % length;
        const int g = (int) std::lround (L.v[Lanes::gate][li]);
        if (g == 0 || g == 3) continue;
        const int note = juce::jlimit (0, 127, 48 + (int) std::lround ((L.v[Lanes::pitch][li] - 0.5f) * 48.0f) + ((int) std::lround (L.v[Lanes::oct][li]) - 2) * 12);
        const int vel = juce::jlimit (1, 127, (int) (L.v[Lanes::vel][li] * 127.0f));
        const double len = stepB * juce::jlimit (0.05, 1.0, (double) (v[P_aGate] * L.v[Lanes::len][li]));
        seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), t * ppqn);
        seq.addEvent (juce::MidiMessage::noteOff (1, note), (t + len) * ppqn);
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote (ppqn); mf.addTrack (seq);
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SKY Z Arp.mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) mf.writeTo (os);
    return f;
}

void SkyzProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = fullState().createXml()) copyXmlToBinary (*xml, dest);
}

void SkyzProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) loadFullState (juce::ValueTree::fromXml (*xml));
}

#if SKYZ_NO_EDITOR
juce::AudioProcessorEditor* SkyzProcessor::createEditor() { return nullptr; }
#else
juce::AudioProcessorEditor* SkyzProcessor::createEditor() { return skazi::withUpdateNotice (new SkyzEditor (*this)); }
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkyzProcessor(); }
