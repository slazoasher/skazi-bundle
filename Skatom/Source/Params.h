#pragma once
// SKATOM parameter tree. Ids, ranges and order mirror web/state.js 1:1 —
// the editor checks the two tables against each other on every UI load.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace skatom
{
enum Type { Float, Bool, Enum };

struct PDef
{
    const char* id;
    const char* hostName;
    float min, max, def;
    Type type;
    bool log;
    juce::StringArray options;
};

enum Pid
{
    dryWet, output, bypass, macro,
    stutDiv, stutter, reverse, tapeStop, tapeTime, glitchAmt,
    freeze, freqShift, contrast, resonFreq, resonTension,
    filtType, filtCut, filtRes, filtDrive, vowel, formantMix,
    distType, distDrive, distMix, magicBass,
    hpf, noise, detune, wash, dropDuck,
    comp, ceiling, limiter,
    lfo1Rate, lfo1Shape, lfo1Depth, lfo1Target,
    lfo2Rate, lfo2Shape, lfo2Depth, lfo2Target,
    envSens, envAtk, envRel, envDepth, envTarget,
    xyX, xyY, roll, jet, spin, riseOn, riseBars, autoDrop, buildMode, pitch,
    mod1On, mod2On, mod3On, mod4On, mod5On, mod6On, fxOrder, risePitch, tailType,
    accPump, accAccel, accWidth, accDrive, accCurve, accEndless, accHigh,
    smile1T, smile2T, smile3T, smile4T, smile5T, smile6T, smile7T, smile8T,
    smile1A, smile2A, smile3A, smile4A, smile5A, smile6A, smile7A, smile8A,
    NUM_PARAMS
};
constexpr int NUM_TAILS = 9; // entries in TAILS below

constexpr int MOD_TARGETS[] = { filtCut, vowel, resonTension, resonFreq, freqShift, hpf, wash, distDrive, glitchAmt, contrast, roll, jet, spin, pitch };
// SMILE slot targets (index = slot enum value; mirrors SMILE_FX in state.js). -1 = special: PUMP / WIDTH / ENDLESS add to the ACELERA amounts
constexpr int SMILE_PARAM[] = { -1, filtCut, hpf, wash, pitch, distDrive, roll, jet, spin, detune, resonTension, freqShift, glitchAmt, noise, -1, -1, -1 };
constexpr int SMILE_PUMP = 14, SMILE_WIDTH = 15, SMILE_ENDLESS = 16, SMILE_SLOTS = 8;
constexpr int RISE_BARS[] = { 1, 2, 4, 8, 16, 32 };
constexpr double LFO_BEATS[] = { 16, 8, 4, 2, 1, 0.5, 0.25, 0.125 };

inline const std::vector<PDef>& defs()
{
    static const juce::StringArray divs { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128", "1/4T", "1/8T", "1/16T", "1/32T" };
    static const juce::StringArray rates { "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    static const juce::StringArray shapes { "SIN", "TRI", "SAW", "SQR", "S&H" };
    static const juce::StringArray smileNames { "--", "FILTER", "HPF", "REVERB", "PITCH", "DRIVE", "ROLL", "JET", "SPIN", "DETUNE", "RESON", "SHIFT", "GLITCH", "NOISE", "PUMP", "WIDTH", "ENDLESS" };
    static const juce::StringArray tgt { "CUTOFF", "VOWEL", "TENSION", "RESON FREQ", "FREQ SHIFT", "HPF", "WASH", "DIST DRIVE", "GLITCH DENS", "CONTRAST", "ROLL", "JET", "SPIN", "PITCH" };
    static const std::vector<PDef> d {
        { "dryWet", "Mix", 0, 1, 1, Float, false, {} },
        { "output", "Output", -24, 12, 0, Float, false, {} },
        { "bypass", "Bypass", 0, 1, 0, Bool, false, {} },
        { "macro", "Mega Macro", 0, 1, 0, Float, false, {} },
        { "stutDiv", "Stutter Division", 0, 11, 4, Enum, false, divs },
        { "stutter", "Stutter", 0, 1, 0, Bool, false, {} },
        { "reverse", "Reverse", 0, 1, 0, Bool, false, {} },
        { "tapeStop", "Tape Stop", 0, 1, 0, Bool, false, {} },
        { "tapeTime", "Tape Time", 0.05f, 2, 0.6f, Float, true, {} },
        { "glitchAmt", "Glitch Density", 0, 1, 0, Float, false, {} },
        { "freeze", "Freeze", 0, 1, 0, Bool, false, {} },
        { "freqShift", "Freq Shift", -500, 500, 0, Float, false, {} },
        { "contrast", "Contrast", 0, 1, 0, Float, false, {} },
        { "resonFreq", "Reson Freq", 40, 2000, 220, Float, true, {} },
        { "resonTension", "Reson Tension", 0, 1, 0, Float, false, {} },
        { "filtType", "Filter A Type", 0, 2, 0, Enum, false, { "LP", "BP", "HP" } },
        { "filtCut", "Filter Cutoff", 20, 20000, 20000, Float, true, {} },
        { "filtRes", "Filter Reso", 0.5f, 18, 0.7f, Float, true, {} },
        { "filtDrive", "Filter Drive", 0, 1, 0, Float, false, {} },
        { "vowel", "Vowel", 0, 4, 0, Float, false, {} },
        { "formantMix", "Formant Mix", 0, 1, 0, Float, false, {} },
        { "distType", "Dist Type", 0, 3, 0, Enum, false, { "TUBE", "MECH", "CRUSH", "XCITE" } },
        { "distDrive", "Dist Drive", 0, 1, 0, Float, false, {} },
        { "distMix", "Dist Mix", 0, 1, 1, Float, false, {} },
        { "magicBass", "Magic Bass", 0, 1, 0, Float, false, {} },
        { "hpf", "Wash HPF", 20, 8000, 20, Float, true, {} },
        { "noise", "Wash Noise", 0, 1, 0, Float, false, {} },
        { "detune", "Wash Detune", 0, 1, 0, Float, false, {} },
        { "wash", "Wash Send", 0, 1, 0, Float, false, {} },
        { "dropDuck", "Drop Duck", 0, 1, 0.7f, Float, false, {} },
        { "comp", "Phat Comp", 0, 1, 0.15f, Float, false, {} },
        { "ceiling", "Ceiling", -12, 0, -0.3f, Float, false, {} },
        { "limiter", "Limiter", 0, 1, 1, Bool, false, {} },
        { "lfo1Rate", "LFO1 Rate", 0, 7, 4, Enum, false, rates },
        { "lfo1Shape", "LFO1 Shape", 0, 4, 0, Enum, false, shapes },
        { "lfo1Depth", "LFO1 Depth", -1, 1, 0, Float, false, {} },
        { "lfo1Target", "LFO1 Target", 0, 13, 0, Enum, false, tgt },
        { "lfo2Rate", "LFO2 Rate", 0, 7, 2, Enum, false, rates },
        { "lfo2Shape", "LFO2 Shape", 0, 4, 1, Enum, false, shapes },
        { "lfo2Depth", "LFO2 Depth", -1, 1, 0, Float, false, {} },
        { "lfo2Target", "LFO2 Target", 0, 13, 1, Enum, false, tgt },
        { "envSens", "Env Sens", 0, 1, 0.5f, Float, false, {} },
        { "envAtk", "Env Attack", 1, 200, 10, Float, true, {} },
        { "envRel", "Env Release", 10, 1000, 150, Float, true, {} },
        { "envDepth", "Env Depth", -1, 1, 0, Float, false, {} },
        { "envTarget", "Env Target", 0, 13, 2, Enum, false, tgt },
        { "xyX", "Matrix X", 0, 1, 0.125f, Float, false, {} },
        { "xyY", "Matrix Y", 0, 1, 0.125f, Float, false, {} },
        { "roll", "Roll Gate", 0, 1, 0, Float, false, {} },
        { "jet", "Jet Flanger", 0, 1, 0, Float, false, {} },
        { "spin", "Spin Pan", 0, 1, 0, Float, false, {} },
        { "riseOn", "Rise", 0, 1, 0, Bool, false, {} },
        { "riseBars", "Rise Bars", 0, 5, 3, Enum, false, { "1", "2", "4", "8", "16", "32" } },
        { "autoDrop", "Auto Drop", 0, 1, 1, Bool, false, {} },
        { "buildMode", "Build Mode", 0, 6, 0, Enum, false, { "WASH OUT", "PITCH UP", "ROLL RUSH", "RESON DRONE", "JET SWEEP", "NOISE CLASSIC", "RANDOM" } },
        { "pitch", "Pitch", -24, 48, 0, Float, false, {} },
        { "mod1On", "Glitch On", 0, 1, 1, Bool, false, {} },
        { "mod2On", "Spectral On", 0, 1, 1, Bool, false, {} },
        { "mod3On", "Filter On", 0, 1, 1, Bool, false, {} },
        { "mod4On", "Dist On", 0, 1, 1, Bool, false, {} },
        { "mod5On", "Build Bus On", 0, 1, 1, Bool, false, {} },
        { "mod6On", "Master On", 0, 1, 1, Bool, false, {} },
        { "fxOrder", "FX Order", 0, 3, 0, Enum, false, { "GLITCH > FILTER > DIST", "GLITCH > DIST > FILTER", "FILTER > DIST > GLITCH", "DIST > FILTER > GLITCH" } },
        { "risePitch", "Rise Pitch", 0, 48, 0, Float, false, {} },
        { "tailType", "Reverb Type", 0, NUM_TAILS - 1, 0, Enum, false, { "HALL", "PLATE", "CATHEDRAL", "SHIMMER", "SKY SHIMMER", "INFINITE", "ECHO WASH", "REVERSE SWELL", "ROOM" } },
        // ACELERA: what else the build drives — tempo pump (accelerating 1/4 → 1/32), width to 200%, drive, and the ride curve
        { "accPump", "Acelera Pump", 0, 1, 0, Float, false, {} },
        { "accAccel", "Acelera Accelerate", 0, 1, 1, Bool, false, {} },
        { "accWidth", "Acelera Width", 0, 1, 0, Float, false, {} },
        { "accDrive", "Acelera Drive", 0, 1, 0, Float, false, {} },
        { "accCurve", "Acelera Curve", 0, 3, 0, Enum, false, { "LINEAR", "SMOOTH", "LATE", "EARLY" } },
        // ENDLESS: Shepard climb made from the input (two shifted copies an octave apart, rising + crossfading, up to 8 octave cycles)
        { "accEndless", "Acelera Endless", 0, 1, 0, Float, false, {} },
        { "accHigh", "Acelera Higher", 0, 1, 0, Bool, false, {} },
        // SMILE slots: effects dragged onto the big knob (target + amount); any slot in use = the knob drives only these
        { "smile1T", "Knob Slot 1", 0, 16, 0, Enum, false, smileNames }, { "smile2T", "Knob Slot 2", 0, 16, 0, Enum, false, smileNames },
        { "smile3T", "Knob Slot 3", 0, 16, 0, Enum, false, smileNames }, { "smile4T", "Knob Slot 4", 0, 16, 0, Enum, false, smileNames },
        { "smile5T", "Knob Slot 5", 0, 16, 0, Enum, false, smileNames }, { "smile6T", "Knob Slot 6", 0, 16, 0, Enum, false, smileNames },
        { "smile7T", "Knob Slot 7", 0, 16, 0, Enum, false, smileNames }, { "smile8T", "Knob Slot 8", 0, 16, 0, Enum, false, smileNames },
        { "smile1A", "Knob Slot 1 Amount", -1, 1, 0, Float, false, {} }, { "smile2A", "Knob Slot 2 Amount", -1, 1, 0, Float, false, {} },
        { "smile3A", "Knob Slot 3 Amount", -1, 1, 0, Float, false, {} }, { "smile4A", "Knob Slot 4 Amount", -1, 1, 0, Float, false, {} },
        { "smile5A", "Knob Slot 5 Amount", -1, 1, 0, Float, false, {} }, { "smile6A", "Knob Slot 6 Amount", -1, 1, 0, Float, false, {} },
        { "smile7A", "Knob Slot 7 Amount", -1, 1, 0, Float, false, {} }, { "smile8A", "Knob Slot 8 Amount", -1, 1, 0, Float, false, {} },
    };
    jassert (d.size() == NUM_PARAMS);
    return d;
}

/* BUILD modes — what the Mega-Macro's five curves drive (mirrors BUILD_MAPS in state.js).
   Curves: 0 late-surge, 1 x^1.8, 2 x^2.7, 3 S-curve peaking 85%, 4 threshold 55%.
   Every mode also gets: wash += c1*.95, comp += c4*.6, and "swallow" (the dry bus sinks into the reverb). */
struct MacroRoute { int curve; int param; float amount; };
constexpr int BUILD_RANDOM = 6, BUILD_NOISE = 5;
inline const std::vector<MacroRoute>& buildRoutes (int mode)
{
    static const std::vector<MacroRoute> m[6] = {
        { { 0, hpf, 0.85f }, { 1, spin, 0.3f }, { 2, detune, 0.45f }, { 3, resonTension, 0.25f } },                    // WASH OUT
        { { 0, hpf, 0.5f }, { 1, pitch, 12.0f / 72 }, { 2, detune, 0.3f }, { 3, jet, 0.25f } },                         // PITCH UP (+12 st, + RISE PITCH)
        { { 0, hpf, 0.6f }, { 2, roll, 0.95f }, { 3, spin, 0.3f }, { 4, glitchAmt, 0.8f } },                           // ROLL RUSH
        { { 0, hpf, 0.4f }, { 1, detune, 0.6f }, { 2, contrast, 0.3f }, { 3, resonTension, 0.85f } },                  // RESON DRONE
        { { 0, hpf, 0.6f }, { 1, spin, 0.6f }, { 2, freqShift, 0.06f }, { 3, jet, 0.8f } },                            // JET SWEEP
        { { 0, hpf, 0.85f }, { 1, spin, 0.6f }, { 2, noise, 0.8f }, { 2, detune, 0.7f }, { 2, freqShift, 0.08f },      // NOISE CLASSIC
          { 2, roll, 0.95f }, { 3, resonTension, 0.75f }, { 3, jet, 0.5f }, { 4, glitchAmt, 1.0f } },
    };
    return m[juce::jlimit (0, 5, mode)];
}
// REVERB TYPE (tailType): what the big reverb is when the build is fully open. fb = feedback loop through the reverb, semis = shimmer pitch in it
struct TailDef { double seconds; float bright, shape, predelay; bool reverse; float fb, semis; bool echo; };
constexpr TailDef TAILS[] = {
    { 6.5, 0.22f, 2.2f, 0.0f, false, 0, 0, false },     // HALL
    { 3.2, 0.60f, 1.5f, 0.0f, false, 0, 0, false },     // PLATE: bright, dense
    { 11.0, 0.14f, 1.6f, 0.045f, false, 0, 0, false },  // CATHEDRAL: huge, dark, pre-delay
    { 6.5, 0.30f, 2.0f, 0.0f, false, 0.55f, 12, false },// SHIMMER: +1 octave climbing in the tail
    { 7.5, 0.35f, 1.8f, 0.0f, false, 0.62f, 24, false },// SKY SHIMMER: +2 octaves
    { 9.0, 0.18f, 1.4f, 0.0f, false, 0.85f, 0, false }, // INFINITE: near-frozen sea
    { 5.0, 0.25f, 2.2f, 0.0f, false, 0, 0, true },      // ECHO WASH: dotted echo feeding the hall
    { 3.0, 0.35f, 2.2f, 0.0f, true, 0, 0, false },      // REVERSE SWELL: the reverb swells in, cuts on the beat
    { 1.2, 0.45f, 1.8f, 0.008f, false, 0, 0, false },   // ROOM: short, natural (new types go at the end: presets store the index)
};
static_assert (std::size (TAILS) == NUM_TAILS);
inline float buildSwallow (int mode) { return mode == BUILD_NOISE ? 0.4f : 0.75f; }

// a module switched OFF pins its effects here (mirrors MODULE_NEUTRAL in state.js)
struct Neutral { int module; int param; float value; };
constexpr Neutral MODULE_NEUTRAL[] = {
    { 0, stutter, 0 }, { 0, reverse, 0 }, { 0, tapeStop, 0 }, { 0, glitchAmt, 0 },
    { 1, freeze, 0 }, { 1, freqShift, 0 }, { 1, contrast, 0 }, { 1, resonTension, 0 }, { 1, pitch, 0 },
    { 2, filtType, 0 }, { 2, filtCut, 20000 }, { 2, filtRes, 0.7f }, { 2, filtDrive, 0 }, { 2, formantMix, 0 },
    { 3, distDrive, 0 }, { 3, magicBass, 0 },
    { 4, hpf, 20 }, { 4, noise, 0 }, { 4, detune, 0 }, { 4, wash, 0 }, { 4, roll, 0 }, { 4, jet, 0 }, { 4, spin, 0 },
    { 5, comp, 0 }, { 5, limiter, 0 },
};

// matrix snapshots may push this param (same rule as `snap` in state.js)
inline bool isSnap (int i)
{
    switch (i)
    {
        case dryWet: case output: case macro: case lfo1Target: case lfo2Target: case envTarget:
        case xyX: case xyY: case riseBars: case buildMode: case fxOrder: case risePitch: case tailType:
        case accPump: case accWidth: case accDrive: case accCurve: case accEndless: return false;
        default: return (i < smile1T || i > smile8A) && defs()[(size_t) i].type != Bool;
    }
}

inline float norm (const PDef& p, float v)
{
    return p.log ? std::log (v / p.min) / std::log (p.max / p.min) : (v - p.min) / (p.max - p.min);
}

inline float denorm (const PDef& p, float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    const float v = p.log ? p.min * std::pow (p.max / p.min, n) : p.min + n * (p.max - p.min);
    return p.type == Float ? v : std::round (v);
}

inline juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& d : defs())
    {
        const juce::ParameterID pid { d.id, 1 };
        if (d.type == Bool)
            layout.add (std::make_unique<juce::AudioParameterBool> (pid, d.hostName, d.def > 0.5f));
        else if (d.type == Enum)
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.hostName, d.options, (int) d.def));
        else
        {
            // same log mapping as state.js, so host-normalized == UI-normalized
            auto range = d.log ? juce::NormalisableRange<float> (d.min, d.max,
                                     [] (float s, float e, float n) { return s * std::pow (e / s, n); },
                                     [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); })
                               : juce::NormalisableRange<float> (d.min, d.max);
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, d.hostName, range, d.def));
        }
    }
    return layout;
}
} // namespace skatom
