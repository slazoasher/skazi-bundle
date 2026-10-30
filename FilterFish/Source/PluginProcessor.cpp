#include "PluginProcessor.h"
#include "../../Common/UpdateNotice.h"
#include "PluginEditor.h"
#include "Presets.h"

GlitchMagmaProcessor::GlitchMagmaProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
}

void GlitchMagmaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const auto numCh = (juce::uint32) juce::jlimit (1, 2, getTotalNumOutputChannels());
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, numCh };

    filter.prepare (sampleRate);
    lfo.prepare (sampleRate);
    glitch.prepare (sampleRate);
    delay.prepare (sampleRate);
    chorus.prepare (spec);
    flanger.prepare (spec);
    reverb.prepare (spec);
    spaces.prepare (sampleRate);
    dryBuffer.setSize ((int) numCh, samplesPerBlock);
    for (auto* b : { &classicBuffer, &spaceWet, &bypassDry })
        b->setSize (2, samplesPerBlock);
    resetDsp();

    auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    for (auto* s : { &inGainSm, &outGainSm, &mixSm, &filterMixSm })
        s->reset (sampleRate, 0.02);
    inGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (ids::inGain)));
    outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (ids::outGain)));
    mixSm.setCurrentAndTargetValue (value (ids::mix));
    filterMixSm.setCurrentAndTargetValue (value (ids::filterMix));
    classicBlend.reset (sampleRate, 0.03);
    classicBlend.setCurrentAndTargetValue (value (ids::reverbType) < 0.5f ? 1.0f : 0.0f);
    bypassBlend.reset (sampleRate, 0.02);
    bypassBlend.setCurrentAndTargetValue (value (ids::bypass) > 0.5f ? 1.0f : 0.0f);
    fullyBypassed = bypassBlend.getCurrentValue() >= 1.0f;
    cutoffSm.reset (sampleRate, 0.03);
    cutoffSm.setCurrentAndTargetValue (value (ids::cutoff));

    envAttack  = (float) std::exp (-1.0 / (0.005 * sampleRate));
    envRelease = (float) std::exp (-1.0 / (0.15 * sampleRate));
}

void GlitchMagmaProcessor::resetDsp()
{
    filter.reset();
    glitch.reset();
    delay.reset();
    dist.reset();
    for (auto& d : dcBlock) d.reset();
    chorus.reset();
    flanger.reset();
    reverb.reset();
    spaces.reset();
    spacesIdle = true;
    env = 0.0f;
}

bool GlitchMagmaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void GlitchMagmaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const int numCh = juce::jmin (2, buffer.getNumChannels());
    if (numCh == 0 || numSamples == 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    // ---- ON/OFF: a short crossfade to the untouched input; fully off skips all the processing
    for (auto* b : { &classicBuffer, &spaceWet, &bypassDry })
        if (b->getNumSamples() < numSamples)
            b->setSize (2, numSamples, false, false, true);
    bypassBlend.setTargetValue (apvts.getRawParameterValue (ids::bypass)->load() > 0.5f ? 1.0f : 0.0f);
    if (bypassBlend.getCurrentValue() >= 1.0f && bypassBlend.getTargetValue() >= 1.0f)
    {
        fullyBypassed = true;
        return;   // the buffer already holds the input
    }
    if (fullyBypassed)
    {
        resetDsp();   // coming back on: no stale echoes or tails from before it was switched off
        fullyBypassed = false;
    }
    for (int ch = 0; ch < numCh; ++ch)
        bypassDry.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto on    = [&] (const char* id) { return param (id) > 0.5f; };
    auto index = [&] (const char* id) { return (int) param (id); };
    auto beatsOf = [&] (const char* id) { return divBeats[juce::jlimit (0, numDivs - 1, index (id))]; };

    // Host transport (falls back to 120 BPM when the host gives nothing).
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    const double samplesPerBeat = sr * 60.0 / bpm;

    // ---- Input gain + dry copy for the global mix
    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (param (ids::inGain)));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inGainSm.getNextValue();
        L[i] *= g;
        if (R != nullptr) R[i] *= g;
    }
    if (dryBuffer.getNumChannels() < numCh || dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize (numCh, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // ---- Distortion
    if (on (ids::distOn))
    {
        const int type = index (ids::distType);
        const float drive = param (ids::distDrive), amount = param (ids::distMix);
        for (int i = 0; i < numSamples; ++i)
        {
            L[i] += (dcBlock[0].process (dist.process (0, L[i], type, drive)) - L[i]) * amount;
            if (R != nullptr) R[i] += (dcBlock[1].process (dist.process (1, R[i], type, drive)) - R[i]) * amount;
        }
    }

    // ---- Filter with LFO + envelope modulation
    const bool lfoSync = on (ids::lfoSync);
    const double lfoBeats = lfoDivBeats[juce::jlimit (0, numLfoDivs - 1, index (ids::lfoDiv))];
    lfo.setShape (index (ids::lfoShape));
    lfo.setSeed (index (ids::seqSeed));
    lfo.setFrequency (lfoSync ? bpm / (60.0 * lfoBeats) : (double) param (ids::lfoRate));
    if (lfoSync && playing)
        lfo.syncTo (ppq, lfoBeats);

    const bool filterOn = on (ids::filterOn);
    filter.setType (index (ids::filterType));
    cutoffSm.setTargetValue (param (ids::cutoff));
    filterMixSm.setTargetValue (param (ids::filterMix));
    const float reso = param (ids::reso), fDrive = param (ids::filterDrive);
    const float lfoDepth = param (ids::lfoDepth), envDepth = param (ids::envDepth);
    const float maxCutoff = (float) juce::jmin (20000.0, sr * 0.45);
    float lfoValue = 0.0f, modCutoff = cutoffSm.getCurrentValue();

    for (int i = 0; i < numSamples; ++i)
    {
        lfoValue = lfo.next();
        const float level = R != nullptr ? 0.5f * (std::abs (L[i]) + std::abs (R[i])) : std::abs (L[i]);
        env = level + (level > env ? envAttack : envRelease) * (env - level);

        const float octaves = lfoValue * lfoDepth * 4.0f + juce::jmin (1.0f, env * 3.0f) * envDepth * 6.0f;
        modCutoff = juce::jlimit (20.0f, maxCutoff, cutoffSm.getNextValue() * std::exp2 (octaves));
        const float fm = filterMixSm.getNextValue();

        if (filterOn)
        {
            L[i] += (filter.process (0, L[i], modCutoff, reso, fDrive) - L[i]) * fm;
            if (R != nullptr) R[i] += (filter.process (1, R[i], modCutoff, reso, fDrive) - R[i]) * fm;
        }
    }
    uiLfoValue = lfoValue;
    uiLfoPhase = (float) lfo.getPhase();
    uiModCutoff = modCutoff;

    // ---- Glitch
    if (on (ids::glitchOn))
    {
        const double stepBeats = beatsOf (ids::glitchDiv);
        glitch.setParams (index (ids::glitchMode), stepBeats * samplesPerBeat,
                          glitchRepeatValues[juce::jlimit (0, 4, index (ids::glitchRepeat))], param (ids::glitchProb));
        if (playing)
            glitch.resync (ppq, stepBeats);
        glitch.process (L, R, numSamples, param (ids::glitchMix));
    }

    // ---- Chorus / Flanger / Reverb (JUCE dsp modules)
    auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, (size_t) numCh);
    juce::dsp::ProcessContextReplacing<float> context (block);

    if (on (ids::chorusOn))
    {
        chorus.setRate (param (ids::chorusRate));
        chorus.setDepth (param (ids::chorusDepth));
        chorus.setCentreDelay (7.0f);
        chorus.setFeedback (0.0f);
        chorus.setMix (param (ids::chorusMix));
        chorus.process (context);
    }

    if (on (ids::flangerOn))
    {
        flanger.setRate (param (ids::flangerRate));
        flanger.setDepth (param (ids::flangerDepth));
        flanger.setCentreDelay (2.0f);
        flanger.setFeedback (param (ids::flangerFeedback));
        flanger.setMix (param (ids::flangerMix));
        flanger.process (context);
    }

    // ---- Delay
    if (on (ids::delayOn))
    {
        const double delaySamples = on (ids::delaySync) ? beatsOf (ids::delayDiv) * samplesPerBeat
                                                        : param (ids::delayTime) * 0.001 * sr;
        delay.process (L, R, numSamples, (float) delaySamples, param (ids::delayFeedback), on (ids::delayPingPong), param (ids::delayMix));
    }

    if (on (ids::reverbOn))
        processReverb (buffer, numCh, numSamples);
    else
        classicBlend.setCurrentAndTargetValue (index (ids::reverbType) == 0 ? 1.0f : 0.0f);

    // ---- Inflator
    if (on (ids::inflateOn))
    {
        const float amount = param (ids::inflateAmount), curve = param (ids::inflateCurve);
        for (int i = 0; i < numSamples; ++i)
        {
            L[i] = fx::inflate (L[i], amount, curve);
            if (R != nullptr) R[i] = fx::inflate (R[i], amount, curve);
        }
    }

    // ---- Stereo width
    if (R != nullptr)
    {
        const float w = param (ids::width);
        if (std::abs (w - 1.0f) > 1.0e-3f)
            for (int i = 0; i < numSamples; ++i)
                fx::applyWidth (L[i], R[i], w);
    }

    // ---- Global dry/wet + output gain + safety net
    mixSm.setTargetValue (param (ids::mix));
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (param (ids::outGain)));
    const float* dL = dryBuffer.getReadPointer (0);
    const float* dR = R != nullptr ? dryBuffer.getReadPointer (1) : nullptr;
    bool blewUp = false;
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSm.getNextValue(), g = outGainSm.getNextValue();
        L[i] = (dL[i] + (L[i] - dL[i]) * m) * g;
        blewUp |= ! std::isfinite (L[i]);
        if (R != nullptr)
        {
            R[i] = (dR[i] + (R[i] - dR[i]) * m) * g;
            blewUp |= ! std::isfinite (R[i]);
        }
    }
    if (blewUp)
    {
        buffer.clear();
        resetDsp();
    }
    fader.process (buffer, getSampleRate());

    // ---- ON/OFF crossfade
    if (bypassBlend.isSmoothing() || bypassBlend.getCurrentValue() > 0.0f)
        for (int i = 0; i < numSamples; ++i)
        {
            const float b = bypassBlend.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                out[i] += (bypassDry.getSample (ch, i) - out[i]) * b;
            }
        }
}

// CLASSIC is JUCE's reverb (the original FilterFish sound); the other types run the SKAZI REVERB engine.
// Switching type crossfades between the two over 30 ms.
void GlitchMagmaProcessor::processReverb (juce::AudioBuffer<float>& buffer, int numCh, int numSamples)
{
    auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const int type = juce::jlimit (0, reverbTypeNames().size() - 1, (int) param (ids::reverbType));
    const float mix = param (ids::reverbMix);
    classicBlend.setTargetValue (type == 0 ? 1.0f : 0.0f);
    const bool runClassic = type == 0 || classicBlend.getCurrentValue() > 0.0f;
    const bool runSpaces  = type != 0 || classicBlend.getCurrentValue() < 1.0f;

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : L;

    if (runClassic)
    {
        for (int ch = 0; ch < numCh; ++ch)
            classicBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        juce::Reverb::Parameters rp;
        rp.roomSize = param (ids::reverbSize);
        rp.damping  = param (ids::reverbDamp);
        rp.wetLevel = mix;
        rp.dryLevel = 1.0f - mix;
        rp.width    = 1.0f;
        reverb.setParameters (rp);
        auto block = juce::dsp::AudioBlock<float> (classicBuffer).getSubsetChannelBlock (0, (size_t) numCh).getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);
    }

    if (runSpaces)
    {
        if (spacesIdle)
        {
            spaces.reset();
            spacesIdle = false;
        }
        verb::Settings s;
        s.mode       = type == 0 ? verb::hall : type - 1;
        s.decay      = param (ids::reverbDecay);
        s.preDelayMs = param (ids::reverbPreDelay);
        s.size       = param (ids::reverbSize);
        s.damping    = param (ids::reverbDamp);
        s.lowCut     = 60.0f;
        s.highCut    = 16000.0f;
        spaces.process (L, R, spaceWet.getWritePointer (0), spaceWet.getWritePointer (1), numSamples, s);
    }
    else
        spacesIdle = true;

    const float* wetL = spaceWet.getReadPointer (0);
    const float* wetR = spaceWet.getReadPointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float c = classicBlend.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* out = buffer.getWritePointer (ch);
            // x2.5 puts the SKAZI REVERB tails at about the CLASSIC level for the same MIX
            const float space = runSpaces ? out[i] * (1.0f - mix) + (ch == 0 ? wetL[i] : wetR[i]) * mix * 2.5f : 0.0f;
            const float classic = runClassic ? classicBuffer.getSample (ch, i) : 0.0f;
            out[i] = classic * c + space * (1.0f - c);
        }
    }
}

int GlitchMagmaProcessor::getNumPrograms() { return presets::numPrograms; }

const juce::String GlitchMagmaProcessor::getProgramName (int index) { return presets::nameFor (index); }

void GlitchMagmaProcessor::setCurrentProgram (int index)
{
    // Ignore repeats: some hosts re-send the current program after restoring a project.
    if (index < 0 || index >= presets::numPrograms || (index == currentProgram && ! randomPreset))
        return;
    currentProgram = index;
    randomPreset = false;
    fader.change ([this, index] { presets::applyFactory (index, apvts); ++presetVersion; }); // fade out, switch, fade in
}

void GlitchMagmaProcessor::randomize()
{
    fader.change ([this]
    {
        juce::Random r (juce::Time::currentTimeMillis());
        const int bank = r.nextInt (presets::numBanks);
        presets::randomizeAll (bank, r, apvts);
        randomPreset = true;
        randomName = "Random " + presets::bankNames()[bank] + " #" + juce::String (r.nextInt (10000)).paddedLeft ('0', 4);
        ++presetVersion;
    });
}

void GlitchMagmaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GlitchMagmaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    fader.cancel(); // the saved sound wins over any preset change still waiting
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, presets::numPrograms - 1, (int) state.getProperty ("program", 0));
            randomPreset = false;
            apvts.replaceState (state);
            ++presetVersion;
        }
}

juce::AudioProcessorEditor* GlitchMagmaProcessor::createEditor() { return skazi::withUpdateNotice (new GlitchMagmaEditor (*this)); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlitchMagmaProcessor(); }
