#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../Common/UpdateNotice.h"

namespace
{
    using namespace sq;

    struct BandPreset { int type; double freq, gain, q; int channel = stereo, slope = 1; double dyn = 0, threshold = -24; };
    struct TubePreset { bool on; int lowFreq; double lowBoost, lowAtten; int highFreq; double highBoost, bandwidth; int attenFreq; double highAtten, drive; };

    struct Preset
    {
        const char* name;
        int phase;
        std::vector<std::pair<int, BandPreset>> bands;   // band index (0 = LOW CUT .. 5 = HIGH CUT) and its settings
        TubePreset tube { false, 2, 0, 0, 4, 0, 5, 1, 0, 20 };
        double output = 0.0;
    };

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list {
            { "INIT: FLAT", natural, {} },
            { "KICK: TUBE TRICK 60", natural, { { 0, { lowCut, 22, 0, 0.71, stereo, 2 } }, { 2, { bell, 320, -2.5, 1.4 } } },
              { true, 2, 7, 6, 4, 2, 6, 1, 0, 25 } },
            { "BASS: MONO SIDE LOWS", natural, { { 0, { lowCut, 160, 0, 0.71, side, 2 } }, { 1, { bell, 80, 2, 1.0, mid } }, { 2, { bell, 260, -2, 1.5, mid } } } },
            { "VOX: AIR + DE-ESS", natural, { { 0, { lowCut, 90, 0, 0.71, stereo, 1 } }, { 2, { bell, 300, -2, 1.2 } }, { 4, { bell, 6500, 0, 4, stereo, 1, -7, -30 } } },
              { true, 2, 0, 0, 5, 4, 7, 2, 0, 15 } },
            { "MASTER: POLISH", natural, { { 1, { lowShelf, 40, 1, 0.71 } }, { 2, { bell, 250, -1, 0.9 } }, { 4, { highShelf, 12000, 1.5, 0.6 } } },
              { true, 1, 3, 2, 6, 3, 8, 2, 0, 15 } },
            { "MASTER: LINEAR CLEAN", linear, { { 0, { lowCut, 20, 0, 0.71, stereo, 0 } }, { 1, { lowCut, 120, 0, 0.71, side, 1 } }, { 3, { bell, 3000, -0.8, 1.0 } } } },
            { "PSY: KICK + BASS SPLIT", natural, { { 0, { lowCut, 30, 0, 0.71, stereo, 3 } }, { 1, { bell, 55, 2.5, 1.4 } }, { 2, { bell, 120, 0, 2, stereo, 1, -4, -20 } },
                                                  { 3, { bell, 250, -3, 1.5 } } } },
            { "LEAD: BRIGHT, NO MUD", natural, { { 0, { lowCut, 150, 0, 0.71, stereo, 1 } }, { 1, { bell, 400, -3, 1.0 } }, { 3, { bell, 3000, 0, 3, stereo, 1, -4, -25 } },
                                                 { 4, { highShelf, 8000, 3, 0.71 } } } },
            { "DRUMS: PUNCH", natural, { { 1, { bell, 100, 3, 1.1 } }, { 2, { bell, 400, -2, 1.2 } }, { 4, { bell, 5000, 2, 0.9 } } },
              { true, 3, 2, 0, 4, 1, 5, 2, 0, 30 } },
            { "HATS: TAME", natural, { { 0, { lowCut, 400, 0, 0.71, stereo, 1 } }, { 3, { bell, 9000, 0, 2, stereo, 1, -6, -28 } }, { 4, { highShelf, 14000, 2, 0.71 } } } },
            { "PAD: WIDE SIDES", natural, { { 1, { lowCut, 200, 0, 0.71, side, 1 } }, { 2, { bell, 300, -2, 1.0, mid } }, { 4, { highShelf, 5000, 3, 0.71, side } } } },
            { "FX: TELEPHONE", zeroLatency, { { 0, { lowCut, 400, 0, 0.71, stereo, 3 } }, { 2, { bell, 1500, 4, 1.2 } }, { 5, { highCut, 3500, 0, 0.71, stereo, 3 } } } },
            { "TUBE: WARM GLUE", natural, {}, { true, 3, 2, 0, 4, 2, 7, 2, 2, 45 } },
            { "DYN: RESONANCE TAMER", natural, { { 2, { bell, 300, 0, 6, stereo, 1, -5, -30 } }, { 3, { bell, 2500, 0, 6, stereo, 1, -5, -32 } },
                                                 { 4, { bell, 6000, 0, 6, stereo, 1, -6, -34 } } } },
        };
        return list;
    }

    juce::String hz (float v)
    {
        return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz";
    }

    juce::String db (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }

    juce::String freqList (const double* list, int index)
    {
        return hz ((float) list[index]);
    }
}

SqualizerAudioProcessor::SqualizerAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "SQUALIZER", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() != ParamIDs::output && ! ranged->getParameterID().startsWith ("MON_"))
                apvts.addParameterListener (ranged->getParameterID(), this);
    startTimerHz (20);
}

SqualizerAudioProcessor::~SqualizerAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout SqualizerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using FloatAttr = juce::AudioParameterFloatAttributes;
    auto freqRange = juce::NormalisableRange<float> (20.0f, 20000.0f, [] (float s, float e, float t) { return s * std::pow (e / s, t); },
                                                     [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); });
    auto qRange = juce::NormalisableRange<float> (0.1f, 18.0f, [] (float s, float e, float t) { return s * std::pow (e / s, t); },
                                                  [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); });

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::phase, 1 }, "Phase Mode", Names::phases, natural));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::output, 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                             FloatAttr().withStringFromValueFunction ([] (float v, int) { return db (v); })));

    // Console defaults: the four EQ bands sit flat and on, ready to grab; the cuts wait switched off.
    constexpr float defaultFreqs[numBands] { 30.0f, 100.0f, 600.0f, 2500.0f, 8000.0f, 18000.0f };
    for (int b = 0; b < numBands; ++b)
    {
        const bool isCut = b == lowCutBand || b == highCutBand;
        const auto prefix = Names::bands[b].toLowerCase().replaceCharacter ('_', ' ') + " ";
        const int defaultType = b == lowCutBand ? lowCut : (b == highCutBand ? highCut : bell);
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::band (b, "ON"), 1 }, prefix + "On", ! isCut));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::band (b, "TYPE"), 1 }, prefix + "Type", Names::types, defaultType));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::band (b, "FREQ"), 1 }, prefix + "Freq", freqRange, defaultFreqs[b],
                                                                 FloatAttr().withStringFromValueFunction ([] (float v, int) { return hz (v); })
                                                                            .withValueFromStringFunction ([] (const juce::String& text)
                                                                            {
                                                                                // "3.2k", "3.2 kHz", "250" all work.
                                                                                const auto t = text.trim().toLowerCase();
                                                                                return (float) (t.getDoubleValue() * (t.containsChar ('k') ? 1000.0 : 1.0));
                                                                            })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::band (b, "GAIN"), 1 }, prefix + "Gain", juce::NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
                                                                 FloatAttr().withStringFromValueFunction ([] (float v, int) { return db (v); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::band (b, "Q"), 1 }, prefix + "Q", qRange, isCut ? 0.71f : 1.0f,
                                                                 FloatAttr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1); })));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::band (b, "CH"), 1 }, prefix + "Channel", Names::channels, stereo));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::band (b, "SLOPE"), 1 }, prefix + "Slope", Names::slopes, 2));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::band (b, "DYN"), 1 }, prefix + "Dynamic Range", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                                 FloatAttr().withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.05f ? juce::String ("OFF") : db (v); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::band (b, "THR"), 1 }, prefix + "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
                                                                 FloatAttr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    }

    auto choiceFrom = [] (const double* list, int count)
    {
        juce::StringArray names;
        for (int i = 0; i < count; ++i)
            names.add (freqList (list, i));
        return names;
    };
    auto knob10 = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
                                                            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
    };
    for (int b = 0; b < numBands; ++b)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::band (b, "SC"), 1 },
                                                                Names::bands[b].toLowerCase() + " Sidechain", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::monMode, 1 }, "Monitor", Names::monitor, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::monPolL, 1 }, "Polarity Left", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::monPolR, 1 }, "Polarity Right", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::monSwap, 1 }, "Swap L/R", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::autoGain, 1 }, "Auto Gain", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::gainScale, 1 }, "Gain Scale", juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
                                                             FloatAttr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::tubeOn, 1 }, "Tube EQ In", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::lowFreq, 1 }, "Tube Low Freq", choiceFrom (lowFreqs, 4), 2));
    layout.add (knob10 (ParamIDs::lowBoost, "Tube Low Boost", 0.0f));
    layout.add (knob10 (ParamIDs::lowAtten, "Tube Low Atten", 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::highFreq, 1 }, "Tube High Freq", choiceFrom (highFreqs, 7), 4));
    layout.add (knob10 (ParamIDs::highBoost, "Tube High Boost", 0.0f));
    layout.add (knob10 (ParamIDs::bandwidth, "Tube Bandwidth", 5.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::attenFreq, 1 }, "Tube Atten Sel", choiceFrom (attenFreqs, 3), 1));
    layout.add (knob10 (ParamIDs::highAtten, "Tube High Atten", 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::drive, 1 }, "Tube Drive", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f,
                                                             FloatAttr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
    return layout;
}

bool SqualizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() || layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    if (layouts.inputBuses.size() < 2)
        return true;
    const auto sidechain = layouts.getChannelSet (true, 1);
    return sidechain.isDisabled() || sidechain == juce::AudioChannelSet::stereo() || sidechain == juce::AudioChannelSet::mono();
}

sq::BandSettings SqualizerAudioProcessor::bandSettings (int b) const
{
    using namespace ParamIDs;
    sq::BandSettings s;
    s.on = get (band (b, "ON")) > 0.5f;
    s.type = b == sq::lowCutBand ? sq::lowCut : (b == sq::highCutBand ? sq::highCut : (int) get (band (b, "TYPE")));
    s.freq = get (band (b, "FREQ"));
    // GAIN SCALE stretches every band's gain and dynamic range together (100% = as set, 0% = flat, 200% = double).
    const double scale = get (ParamIDs::gainScale) / 100.0;
    s.gain = get (band (b, "GAIN")) * scale;
    s.q = get (band (b, "Q"));
    s.channel = (int) get (band (b, "CH"));
    s.slope = (int) get (band (b, "SLOPE"));
    s.dyn = get (band (b, "DYN")) * scale;
    s.threshold = get (band (b, "THR"));
    s.external = get (band (b, "SC")) > 0.5f;
    return s;
}

sq::TubeSettings SqualizerAudioProcessor::tubeSettings() const
{
    using namespace ParamIDs;
    return { (int) get (lowFreq), get (lowBoost), get (lowAtten), (int) get (highFreq), get (highBoost), get (bandwidth),
             (int) get (attenFreq), get (highAtten), get (drive) };
}

void SqualizerAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    for (auto& b : bands)
        b.prepare (sampleRate);
    tube.prepare (sampleRate);
    soloMix = 0.0;
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (get (ParamIDs::output)));

    // Long enough to hold a 20 Hz shelf: about 0.34 s of taps, so 16k at 44.1/48k and 32k at 96k.
    firSize = sampleRate <= 50000.0 ? 16384 : (sampleRate <= 100000.0 ? 32768 : 65536);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    firDirect.prepare (spec);
    firCross.prepare (spec);
    crossBuffer.setSize (2, samplesPerBlock);
    firDirty = true;
    rebuildLinearPhase();
    setLatencySamples (phaseMode() == linear ? linearLatency() : 0);
}

void SqualizerAudioProcessor::timerCallback()
{
    const int wanted = phaseMode() == linear ? linearLatency() : 0;
    if (getLatencySamples() != wanted)
    {
        firDirty = true;
        setLatencySamples (wanted);
    }
    if (firDirty && phaseMode() == linear)
        rebuildLinearPhase();
    if (gainDirty)
        updateAutoGain();
}

void SqualizerAudioProcessor::rebuildLinearPhase()
{
    firDirty = false;
    const int n = firSize, half = n / 2, bins = half + 1;
    std::vector<double> hl ((size_t) bins, 1.0), hr ((size_t) bins, 1.0), hm ((size_t) bins, 1.0), hs ((size_t) bins, 1.0);
    for (int b = 0; b < numBands; ++b)
    {
        const auto s = bandSettings (b);
        if (! s.on)
            continue;
        for (int k = 0; k < bins; ++k)
        {
            const double m = bandMagnitude (s, std::max (1.0, k * sampleRate / n), sampleRate, linear);
            switch (s.channel)
            {
                case stereo: hm[(size_t) k] *= m; hs[(size_t) k] *= m; break;
                case mid:    hm[(size_t) k] *= m; break;
                case side:   hs[(size_t) k] *= m; break;
                case left:   hl[(size_t) k] *= m; break;
                default:     hr[(size_t) k] *= m; break;
            }
        }
    }

    // out = MS^-1 * diag(Hm, Hs) * MS * diag(HL, HR): direct paths L->L, R->R and cross paths R->L, L->R.
    juce::dsp::FFT fft ((int) std::log2 (n));
    std::vector<float> work ((size_t) n * 2);
    juce::AudioBuffer<float> direct (2, n), cross (2, n);
    auto build = [&] (auto magnitude, float* dest)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int k = 0; k < bins; ++k)
            work[(size_t) k * 2] = (float) magnitude ((size_t) k);
        fft.performRealOnlyInverseTransform (work.data());
        for (int i = 0; i < n; ++i)
        {
            const float window = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) n);
            dest[i] = work[(size_t) ((i + half) % n)] * window;
        }
    };
    build ([&] (size_t k) { return 0.5 * (hm[k] + hs[k]) * hl[k]; }, direct.getWritePointer (0));
    build ([&] (size_t k) { return 0.5 * (hm[k] + hs[k]) * hr[k]; }, direct.getWritePointer (1));
    build ([&] (size_t k) { return 0.5 * (hm[k] - hs[k]) * hr[k]; }, cross.getWritePointer (0));   // R into L
    build ([&] (size_t k) { return 0.5 * (hm[k] - hs[k]) * hl[k]; }, cross.getWritePointer (1));   // L into R

    using C = juce::dsp::Convolution;
    firDirect.loadImpulseResponse (std::move (direct), sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
    firCross.loadImpulseResponse (std::move (cross), sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
}

void SqualizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0)
        return;

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    preScope.push (l, r, n);

    // Sidechain: drives EXTERNAL dynamic bands and the masking view.
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            scL = sc.getReadPointer (0);
            scR = sc.getReadPointer (sc.getNumChannels() > 1 ? 1 : 0);
            sideScope.push (scL, scR, n);
            sidechainActive.store (sc.getMagnitude (0, n) > 1.0e-5f, std::memory_order_relaxed);
        }
    }
    else
        sidechainActive.store (false, std::memory_order_relaxed);

    const int phase = phaseMode();
    if (phase == linear && n <= crossBuffer.getNumSamples())
    {
        crossBuffer.copyFrom (0, 0, buffer, 1, 0, n);
        crossBuffer.copyFrom (1, 0, buffer, 0, 0, n);
        juce::dsp::AudioBlock<float> direct (buffer), cross (crossBuffer.getArrayOfWritePointers(), 2, (size_t) n);
        firDirect.process (juce::dsp::ProcessContextReplacing<float> (direct));
        firCross.process (juce::dsp::ProcessContextReplacing<float> (cross));
        buffer.addFrom (0, 0, crossBuffer, 0, 0, n);
        buffer.addFrom (1, 0, crossBuffer, 1, 0, n);
    }

    for (int b = 0; b < numBands; ++b)
    {
        bands[(size_t) b].process (l, r, n, bandSettings (b), phase, scL, scR);
        liveDyn[(size_t) b].store (bands[(size_t) b].liveDyn, std::memory_order_relaxed);
    }

    tube.process (l, r, n, tubeSettings(), tubeOn());
    processSolo (l, r, n);

    const float autoDb = get (ParamIDs::autoGain) > 0.5f ? autoGainDb.load (std::memory_order_relaxed) : 0.0f;
    outGain.setTargetValue (juce::Decibels::decibelsToGain (get (ParamIDs::output) + autoDb));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }

    // Monitor section: polarity, swap, then mono / side listen. Applied last, so what you hear is what the meters show.
    // shortcut: these switch instantly (a click is possible); add short crossfades if that bothers anyone.
    const bool polL = get (ParamIDs::monPolL) > 0.5f, polR = get (ParamIDs::monPolR) > 0.5f, swap = get (ParamIDs::monSwap) > 0.5f;
    const int mode = (int) get (ParamIDs::monMode);
    if (polL || polR || swap || mode != 0)
        for (int i = 0; i < n; ++i)
        {
            float L = polL ? -l[i] : l[i], R = polR ? -r[i] : r[i];
            if (swap)
                std::swap (L, R);
            if (mode == 1)
                L = R = 0.5f * (L + R);
            else if (mode == 2)
                L = R = 0.5f * (L - R);
            l[i] = L;
            r[i] = R;
        }
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    postScope.push (l, r, n);
    const float peak = juce::jmax (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n));
    if (peak > outPeak.load())
        outPeak.store (peak);
}

void SqualizerAudioProcessor::processSolo (float* l, float* r, int n)
{
    const int solo = soloBand.load (std::memory_order_relaxed);
    if (solo < 0 && soloMix <= 0.0)
        return;
    if (solo >= 0)
    {
        if (soloMix <= 0.0 || solo != soloShown)
            for (auto& ch : soloFilters)
                for (auto& f : ch)
                    f.reset();
        soloShown = solo;
    }

    // Two band-pass sections at the band's frequency: steep enough to isolate the region, Q follows the band
    // (shelves and cuts get a medium width around their corner).
    const auto s = bandSettings (soloShown);
    const bool peaky = s.type == bell || s.type == notch || s.type == bandPass;
    const double q = peaky ? juce::jlimit (0.4, 12.0, s.q) : 0.9;
    const auto c = design (prototype (bandPass, 0.0, q), s.freq, sampleRate, zeroLatency);
    for (auto& ch : soloFilters)
        for (auto& f : ch)
            f.c = c;

    // 15 ms fades in and out so pressing and releasing never clicks.
    const double target = solo >= 0 ? 1.0 : 0.0, step = 1.0 / (0.015 * sampleRate);
    float* chans[] { l, r };
    double mix = soloMix;
    for (int i = 0; i < n; ++i)
    {
        mix = target > mix ? juce::jmin (target, mix + step) : juce::jmax (target, mix - step);
        for (int ch = 0; ch < 2; ++ch)
        {
            const double x = chans[ch][i];
            const double y = soloFilters[(size_t) ch][1].process (soloFilters[(size_t) ch][0].process (x));
            chans[ch][i] = (float) (x + mix * (y - x));
        }
    }
    soloMix = mix;
}

int SqualizerAudioProcessor::getNumPrograms()
{
    return (int) presets().size();
}

const juce::String SqualizerAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets()[(size_t) index].name : "";
}

void SqualizerAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentPreset = index;
    const auto& p = presets()[(size_t) index];

    auto set = [this] (const juce::String& id, float value)
    {
        auto* param = apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    set (ParamIDs::phase, (float) p.phase);
    set (ParamIDs::output, (float) p.output);
    for (const auto& [i, s] : p.bands)
    {
        set (ParamIDs::band (i, "ON"), 1.0f);
        set (ParamIDs::band (i, "TYPE"), (float) s.type);
        set (ParamIDs::band (i, "FREQ"), (float) s.freq);
        set (ParamIDs::band (i, "GAIN"), (float) s.gain);
        set (ParamIDs::band (i, "Q"), (float) s.q);
        set (ParamIDs::band (i, "CH"), (float) s.channel);
        set (ParamIDs::band (i, "SLOPE"), (float) s.slope + 1.0f);   // preset slopes are written as 0 = 12 dB .. 3 = 48 dB
        set (ParamIDs::band (i, "DYN"), (float) s.dyn);
        set (ParamIDs::band (i, "THR"), (float) s.threshold);
    }
    const auto& t = p.tube;
    set (ParamIDs::tubeOn, t.on ? 1.0f : 0.0f);
    set (ParamIDs::lowFreq, (float) t.lowFreq);
    set (ParamIDs::lowBoost, (float) t.lowBoost);
    set (ParamIDs::lowAtten, (float) t.lowAtten);
    set (ParamIDs::highFreq, (float) t.highFreq);
    set (ParamIDs::highBoost, (float) t.highBoost);
    set (ParamIDs::bandwidth, (float) t.bandwidth);
    set (ParamIDs::attenFreq, (float) t.attenFreq);
    set (ParamIDs::highAtten, (float) t.highAtten);
    set (ParamIDs::drive, (float) t.drive);
}

void SqualizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    state.setProperty ("slot", slot, nullptr);
    for (int i = 0; i < 2; ++i)
        if (i != slot && slots[(size_t) i].isValid())
        {
            juce::ValueTree holder ("SLOT");
            holder.setProperty ("index", i, nullptr);
            holder.appendChild (slots[(size_t) i].createCopy(), nullptr);
            state.appendChild (holder, nullptr);
        }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SqualizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentPreset = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("preset", 0));
            slot = juce::jlimit (0, 1, (int) state.getProperty ("slot", 0));
            slots = {};
            for (auto child = state.getChildWithName ("SLOT"); child.isValid(); child = state.getChildWithName ("SLOT"))
            {
                if (child.getNumChildren() > 0)
                    slots[(size_t) juce::jlimit (0, 1, (int) child.getProperty ("index", 0))] = child.getChild (0).createCopy();
                state.removeChild (child, nullptr);
            }
            apvts.replaceState (state);
            undoManager.clearUndoHistory();
            firDirty = true;
            gainDirty = true;
        }
}

void SqualizerAudioProcessor::switchSlot (int newSlot)
{
    if (newSlot == slot || ! juce::isPositiveAndBelow (newSlot, 2))
        return;
    slots[(size_t) slot] = apvts.copyState();
    if (! slots[(size_t) newSlot].isValid())
        slots[(size_t) newSlot] = apvts.copyState();
    slot = newSlot;
    apvts.replaceState (slots[(size_t) newSlot].createCopy());
    firDirty = true;
    gainDirty = true;
}

void SqualizerAudioProcessor::copyToOtherSlot()
{
    slots[(size_t) (1 - slot)] = apvts.copyState();
}

void SqualizerAudioProcessor::updateAutoGain()
{
    gainDirty = false;
    // Average power of the static EQ (mid and side paths, bands + tube) over 64 log-spaced points = pink weighting.
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < 64; ++i)
    {
        const double f = 30.0 * std::pow (16000.0 / 30.0, i / 63.0);
        double midDb = 0.0, sideDb = 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            auto s = bandSettings (b);
            if (! s.on)
                continue;
            const double db = 20.0 * std::log10 (bandMagnitude (s, f, sampleRate, natural) + 1e-12);
            if (s.channel != side)
                midDb += db;
            if (s.channel == stereo || s.channel == side)
                sideDb += db;
        }
        if (tubeOn())
        {
            const double t = 20.0 * std::log10 (tubeMagnitude (tubeSettings(), f, sampleRate) + 1e-12);
            midDb += t;
            sideDb += t;
        }
        // Mid carries most of a mix's energy.
        sum += 0.8 * std::pow (10.0, midDb / 10.0) + 0.2 * std::pow (10.0, sideDb / 10.0);
        ++count;
    }
    autoGainDb = (float) juce::jlimit (-24.0, 24.0, -10.0 * std::log10 (sum / count));
}

bool SqualizerAudioProcessor::monitorActive() const
{
    return get (ParamIDs::monMode) > 0.5f || get (ParamIDs::monPolL) > 0.5f || get (ParamIDs::monPolR) > 0.5f || get (ParamIDs::monSwap) > 0.5f;
}

juce::AudioProcessorEditor* SqualizerAudioProcessor::createEditor()
{
    return skazi::withUpdateNotice (new SqualizerAudioProcessorEditor (*this));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SqualizerAudioProcessor();
}
