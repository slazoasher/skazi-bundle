#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    // Global
    inline constexpr const char* inGain  = "inGain";
    inline constexpr const char* outGain = "outGain";
    inline constexpr const char* mix     = "mix";
    inline constexpr const char* width   = "width";
    inline constexpr const char* bypass  = "bypass";   // the header ON/OFF switch, also the host's bypass

    // Filter
    inline constexpr const char* filterOn    = "filterOn";
    inline constexpr const char* filterType  = "filterType";
    inline constexpr const char* cutoff      = "cutoff";
    inline constexpr const char* reso        = "reso";
    inline constexpr const char* filterDrive = "filterDrive";
    inline constexpr const char* filterMix   = "filterMix";

    // Modulation
    inline constexpr const char* lfoShape = "lfoShape";
    inline constexpr const char* lfoRate  = "lfoRate";
    inline constexpr const char* lfoSync  = "lfoSync";
    inline constexpr const char* lfoDiv   = "lfoDiv";
    inline constexpr const char* lfoDepth = "lfoDepth";
    inline constexpr const char* envDepth = "envDepth";
    inline constexpr const char* seqSeed  = "seqSeed";

    // Distortion
    inline constexpr const char* distOn    = "distOn";
    inline constexpr const char* distType  = "distType";
    inline constexpr const char* distDrive = "distDrive";
    inline constexpr const char* distMix   = "distMix";

    // Glitch
    inline constexpr const char* glitchOn     = "glitchOn";
    inline constexpr const char* glitchMode   = "glitchMode";
    inline constexpr const char* glitchDiv    = "glitchDiv";
    inline constexpr const char* glitchRepeat = "glitchRepeat";
    inline constexpr const char* glitchProb   = "glitchProb";
    inline constexpr const char* glitchMix    = "glitchMix";

    // Chorus
    inline constexpr const char* chorusOn    = "chorusOn";
    inline constexpr const char* chorusRate  = "chorusRate";
    inline constexpr const char* chorusDepth = "chorusDepth";
    inline constexpr const char* chorusMix   = "chorusMix";

    // Flanger
    inline constexpr const char* flangerOn       = "flangerOn";
    inline constexpr const char* flangerRate     = "flangerRate";
    inline constexpr const char* flangerDepth    = "flangerDepth";
    inline constexpr const char* flangerFeedback = "flangerFeedback";
    inline constexpr const char* flangerMix      = "flangerMix";

    // Delay
    inline constexpr const char* delayOn       = "delayOn";
    inline constexpr const char* delaySync     = "delaySync";
    inline constexpr const char* delayDiv      = "delayDiv";
    inline constexpr const char* delayTime     = "delayTime";
    inline constexpr const char* delayFeedback = "delayFeedback";
    inline constexpr const char* delayPingPong = "delayPingPong";
    inline constexpr const char* delayMix      = "delayMix";

    // Reverb
    inline constexpr const char* reverbOn   = "reverbOn";
    inline constexpr const char* reverbSize = "reverbSize";
    inline constexpr const char* reverbDamp = "reverbDamp";
    inline constexpr const char* reverbMix  = "reverbMix";
    inline constexpr const char* reverbType     = "reverbType";
    inline constexpr const char* reverbDecay    = "reverbDecay";
    inline constexpr const char* reverbPreDelay = "reverbPreDelay";

    // Inflator
    inline constexpr const char* inflateOn     = "inflateOn";
    inline constexpr const char* inflateAmount = "inflateAmount";
    inline constexpr const char* inflateCurve  = "inflateCurve";
}

namespace ft
{
    enum Type
    {
        LP12, LP24, HP12, HP24, BP12, BP24, Notch, Notch24, Peak, LadderLP,
        CombPlus, CombMinus, Formant, Phaser, BitCrush, RingMod, WaveFold, Scream, Telephone, Metallic, Squelch,
        count
    };
}

namespace ls
{
    enum Shape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, StepSeq, Bounce, Chaos, count };
}

inline const juce::StringArray& filterTypeNames()
{
    static const juce::StringArray s { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24", "Notch", "Notch 24", "Peak", "Ladder LP",
                                       "Comb +", "Comb -", "Formant", "Phaser", "Bit Crush", "Ring Mod", "Wave Fold", "Scream",
                                       "Telephone", "Metallic", "Squelch" };
    return s;
}

inline const juce::StringArray& lfoShapeNames()
{
    static const juce::StringArray s { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd", "Step Seq", "Bounce", "Chaos" };
    return s;
}

inline const juce::StringArray& divNames()
{
    static const juce::StringArray s { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2", "1 Bar", "2 Bars", "4 Bars" };
    return s;
}

// Length of each division in quarter-note beats (same order as divNames).
inline constexpr double divBeats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0 };
inline constexpr int numDivs = 13;

// The LFO also goes slower: 8, 16 and 32 bar sweeps (added after the shared list, so older settings keep their place)
inline const juce::StringArray& lfoDivNames()
{
    static const juce::StringArray s = [] { auto a = divNames(); a.addArray (juce::StringArray { "8 Bars", "16 Bars", "32 Bars" }); return a; }();
    return s;
}
inline constexpr double lfoDivBeats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0 };
inline constexpr int numLfoDivs = 16;

inline const juce::StringArray& distTypeNames()
{
    static const juce::StringArray s { "Soft Clip", "Hard Clip", "Tube", "Fold", "Bit Crush", "Rectify" };
    return s;
}

inline const juce::StringArray& glitchModeNames()
{
    static const juce::StringArray s { "Repeat", "Reverse", "Gate", "Mixed" };
    return s;
}

inline const juce::StringArray& glitchRepeatNames()
{
    static const juce::StringArray s { "x1", "x2", "x4", "x8", "x16" };
    return s;
}

inline constexpr int glitchRepeatValues[] = { 1, 2, 4, 8, 16 };

// CLASSIC is the original FilterFish reverb; the rest are SKAZI REVERB algorithms (verb::Mode + 1).
inline const juce::StringArray& reverbTypeNames()
{
    static const juce::StringArray s { "CLASSIC", "HALL", "PLATE", "ROOM", "CHAMBER", "CATHEDRAL",
                                       "SPRING", "SHIMMER", "REVERSE", "GATED", "BLACKHOLE" };
    return s;
}

inline juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float n) { return s * std::pow (e / s, n); },
             [] (float s, float e, float v) { return juce::jlimit (0.0f, 1.0f, std::log (v / s) / std::log (e / s)); } };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pctText     = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto bipolarText = [] (float v, int) { const int p = roundToInt (v * 100.0f); return (p > 0 ? "+" : "") + String (p) + " %"; };
    // Note: String (float, 0) means "full precision" in JUCE, so whole numbers go through roundToInt.
    auto hzText      = [] (float v, int) { return v < 10.0f ? String (v, 2) + " Hz" : v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 2) + " kHz"; };
    auto dbText      = [] (float v, int) { return String (v, 1) + " dB"; };
    auto msText      = [] (float v, int) { return String (roundToInt (v)) + " ms"; };

    auto addFloat = [&] (const char* id, const char* name, NormalisableRange<float> range, float def,
                         std::function<String (float, int)> text)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    };
    auto addPct    = [&] (const char* id, const char* name, float def) { addFloat (id, name, { 0.0f, 1.0f }, def, pctText); };
    auto addChoice = [&] (const char* id, const char* name, const StringArray& items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };
    auto addBool = [&] (const char* id, const char* name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    addFloat (ids::inGain,  "Input",  { -24.0f, 24.0f }, 0.0f, dbText);
    addFloat (ids::outGain, "Output", { -24.0f, 24.0f }, 0.0f, dbText);
    addPct   (ids::mix,     "Dry/Wet", 1.0f);
    addFloat (ids::width,   "Width",  { 0.0f, 2.0f }, 1.0f, pctText);

    addBool   (ids::filterOn,    "Filter On", true);
    addChoice (ids::filterType,  "Filter Type", filterTypeNames(), ft::LP24);
    addFloat  (ids::cutoff,      "Cutoff", logRange (20.0f, 20000.0f), 1200.0f, hzText);
    addPct    (ids::reso,        "Resonance", 0.3f);
    addPct    (ids::filterDrive, "Filter Drive", 0.0f);
    addPct    (ids::filterMix,   "Filter Mix", 1.0f);

    addChoice (ids::lfoShape, "LFO Shape", lfoShapeNames(), ls::Sine);
    addFloat  (ids::lfoRate,  "LFO Rate", logRange (0.01f, 20.0f), 1.0f, hzText);
    addBool   (ids::lfoSync,  "LFO Sync", true);
    addChoice (ids::lfoDiv,   "LFO Division", lfoDivNames(), 10);
    addFloat  (ids::lfoDepth, "LFO Amount", { -1.0f, 1.0f }, 0.0f, bipolarText);
    addFloat  (ids::envDepth, "Env Amount", { -1.0f, 1.0f }, 0.0f, bipolarText);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::seqSeed, 1 }, "Step Pattern", 0, 999, 0));

    addBool   (ids::distOn,    "Dist On", false);
    addChoice (ids::distType,  "Dist Type", distTypeNames(), 0);
    addPct    (ids::distDrive, "Dist Drive", 0.3f);
    addPct    (ids::distMix,   "Dist Mix", 1.0f);

    addBool   (ids::glitchOn,     "Glitch On", false);
    addChoice (ids::glitchMode,   "Glitch Mode", glitchModeNames(), 0);
    addChoice (ids::glitchDiv,    "Glitch Step", divNames(), 4);
    addChoice (ids::glitchRepeat, "Glitch Repeats", glitchRepeatNames(), 2);
    addPct    (ids::glitchProb,   "Glitch Chance", 0.5f);
    addPct    (ids::glitchMix,    "Glitch Mix", 1.0f);

    addBool  (ids::chorusOn,    "Chorus On", false);
    addFloat (ids::chorusRate,  "Chorus Rate", logRange (0.05f, 5.0f), 0.8f, hzText);
    addPct   (ids::chorusDepth, "Chorus Depth", 0.4f);
    addPct   (ids::chorusMix,   "Chorus Mix", 0.5f);

    addBool  (ids::flangerOn,       "Flanger On", false);
    addFloat (ids::flangerRate,     "Flanger Rate", logRange (0.05f, 5.0f), 0.3f, hzText);
    addPct   (ids::flangerDepth,    "Flanger Depth", 0.6f);
    addFloat (ids::flangerFeedback, "Flanger Feedback", { -0.95f, 0.95f }, 0.6f, bipolarText);
    addPct   (ids::flangerMix,      "Flanger Mix", 0.5f);

    addBool   (ids::delayOn,       "Delay On", false);
    addBool   (ids::delaySync,     "Delay Sync", true);
    addChoice (ids::delayDiv,      "Delay Division", divNames(), 6);
    addFloat  (ids::delayTime,     "Delay Time", logRange (1.0f, 2000.0f), 350.0f, msText);
    addFloat  (ids::delayFeedback, "Delay Feedback", { 0.0f, 0.95f }, 0.4f, pctText);
    addBool   (ids::delayPingPong, "Ping Pong", false);
    addPct    (ids::delayMix,      "Delay Mix", 0.3f);

    addBool (ids::reverbOn,   "Reverb On", false);
    addPct  (ids::reverbSize, "Reverb Size", 0.6f);
    addPct  (ids::reverbDamp, "Reverb Damping", 0.5f);
    addPct  (ids::reverbMix,  "Reverb Mix", 0.3f);

    addBool  (ids::inflateOn,     "Inflator On", false);
    addPct   (ids::inflateAmount, "Inflator Effect", 0.5f);
    addFloat (ids::inflateCurve,  "Inflator Curve", { -50.0f, 50.0f }, 0.0f, [] (float v, int) { return String (roundToInt (v)); });

    // Added later, so they go last: hosts that number parameters keep their old automation.
    addChoice (ids::reverbType,     "Reverb Type", reverbTypeNames(), 0);
    addFloat  (ids::reverbDecay,    "Reverb Decay", logRange (0.2f, 20.0f), 2.5f,
               [] (float v, int) { return String (v, v < 10.0f ? 2 : 1) + " s"; });
    addFloat  (ids::reverbPreDelay, "Reverb Pre-Delay", { 0.0f, 500.0f }, 20.0f, msText);
    addBool   (ids::bypass,         "Bypass", false);

    return layout;
}
