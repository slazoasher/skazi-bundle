#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

// 800 factory presets: 16 banks x 50, generated from a fixed seed so every machine gets the same sounds.
// Program 0 is "Init". Each bank has its own rules so the randomness stays musical.
namespace presets
{
inline constexpr int perBank = 50;
inline constexpr int numBanks = 16;
inline constexpr int numFactory = perBank * numBanks;
inline constexpr int numPrograms = numFactory + 1;

inline const juce::StringArray& bankNames()
{
    static const juce::StringArray s { "Classic Sweep", "Acid", "Dub Space", "Glitch", "Lo-Fi", "Talk Box", "Comb Metal", "Phase Shift",
                                       "Destroy", "Inflate", "Wide Air", "Rhythm Gate", "Ring Bells", "Scream", "Tape Echo", "Chaos" };
    return s;
}

inline juce::String nameFor (int program)
{
    if (program <= 0) return "Init";
    static const char* adjectives[] = { "Molten", "Liquid", "Broken", "Frozen", "Neon", "Dusty", "Hyper", "Wobbly", "Rusty", "Velvet",
                                        "Toxic", "Golden", "Hollow", "Twisted", "Electric", "Lunar", "Savage", "Glassy", "Murky", "Cosmic",
                                        "Rubber", "Sonic", "Deep", "Shattered", "Psycho", "Silky", "Burning", "Phantom", "Crystal", "Heavy" };
    static const char* nouns[] = { "Lava", "Riser", "Tunnel", "Engine", "Ghost", "Storm", "Sweep", "Pulse", "Vortex", "Machine",
                                   "Comet", "Serpent", "Circuit", "Wave", "Swamp", "Reactor", "Mirror", "Siren", "Crater", "Drone",
                                   "Prism", "Beast", "Orbit", "Signal", "Monster", "Glacier", "Dragon", "Spiral", "Rocket", "Temple" };
    const int index = program - 1;
    juce::Random r (index * 104729 + 3);
    return bankNames()[index / perBank] + " " + juce::String (index % perBank + 1).paddedLeft ('0', 2) + " - "
         + adjectives[r.nextInt (30)] + " " + nouns[r.nextInt (30)];
}

inline void resetToDefaults (juce::AudioProcessorValueTreeState& state)
{
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ids::bypass)   // a preset never switches the plugin on or off
                rp->setValueNotifyingHost (rp->getDefaultValue());
}

inline void generate (int bank, juce::Random& r, juce::AudioProcessorValueTreeState& state)
{
    resetToDefaults (state);

    auto set = [&] (const char* id, float v)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    auto rnd    = [&] (float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); };
    auto logRnd = [&] (float lo, float hi) { return lo * std::pow (hi / lo, r.nextFloat()); };
    auto chance = [&] (float p) { return r.nextFloat() < p; };
    auto pick   = [&] (std::initializer_list<int> l) { return (float) *(l.begin() + r.nextInt ((int) l.size())); };
    auto sign   = [&] { return chance (0.5f) ? 1.0f : -1.0f; };

    auto lfo = [&] (std::initializer_list<int> shapes, float syncChance, std::initializer_list<int> divs, float depthLo, float depthHi)
    {
        set (ids::lfoShape, pick (shapes));
        set (ids::lfoSync, chance (syncChance) ? 1.0f : 0.0f);
        set (ids::lfoDiv, pick (divs));
        set (ids::lfoRate, logRnd (0.05f, 4.0f));
        set (ids::lfoDepth, sign() * rnd (depthLo, depthHi));
        set (ids::seqSeed, (float) r.nextInt (1000));
    };
    auto delay = [&] (std::initializer_list<int> divs, float fbLo, float fbHi, float mixLo, float mixHi)
    {
        set (ids::delayOn, 1.0f);
        set (ids::delaySync, 1.0f);
        set (ids::delayDiv, pick (divs));
        set (ids::delayFeedback, rnd (fbLo, fbHi));
        set (ids::delayPingPong, chance (0.5f) ? 1.0f : 0.0f);
        set (ids::delayMix, rnd (mixLo, mixHi));
    };
    auto reverb = [&] (float sizeLo, float sizeHi, float mixLo, float mixHi)
    {
        set (ids::reverbOn, 1.0f);
        set (ids::reverbSize, rnd (sizeLo, sizeHi));
        set (ids::reverbDamp, rnd (0.2f, 0.8f));
        set (ids::reverbMix, rnd (mixLo, mixHi));
    };
    auto chorus = [&] (float depthLo, float depthHi, float mixLo, float mixHi)
    {
        set (ids::chorusOn, 1.0f);
        set (ids::chorusRate, logRnd (0.1f, 2.0f));
        set (ids::chorusDepth, rnd (depthLo, depthHi));
        set (ids::chorusMix, rnd (mixLo, mixHi));
    };
    auto flanger = [&]
    {
        set (ids::flangerOn, 1.0f);
        set (ids::flangerRate, logRnd (0.05f, 1.5f));
        set (ids::flangerDepth, rnd (0.3f, 0.9f));
        set (ids::flangerFeedback, sign() * rnd (0.3f, 0.85f));
        set (ids::flangerMix, rnd (0.3f, 0.6f));
    };
    auto dist = [&] (std::initializer_list<int> types, float driveLo, float driveHi, float mixLo, float mixHi)
    {
        set (ids::distOn, 1.0f);
        set (ids::distType, pick (types));
        set (ids::distDrive, rnd (driveLo, driveHi));
        set (ids::distMix, rnd (mixLo, mixHi));
    };
    auto glitch = [&] (std::initializer_list<int> modes, std::initializer_list<int> divs, std::initializer_list<int> repeats, float probLo, float probHi)
    {
        set (ids::glitchOn, 1.0f);
        set (ids::glitchMode, pick (modes));
        set (ids::glitchDiv, pick (divs));
        set (ids::glitchRepeat, pick (repeats));
        set (ids::glitchProb, rnd (probLo, probHi));
        set (ids::glitchMix, rnd (0.8f, 1.0f));
    };
    auto filter = [&] (std::initializer_list<int> types, float cutLo, float cutHi, float resLo, float resHi)
    {
        set (ids::filterType, pick (types));
        set (ids::cutoff, logRnd (cutLo, cutHi));
        set (ids::reso, rnd (resLo, resHi));
    };

    using namespace ft;
    using namespace ls;

    switch (bank)
    {
        case 0: // Classic Sweep
            filter ({ LP12, LP24, HP12, HP24, BP12, LadderLP }, 200.0f, 8000.0f, 0.1f, 0.7f);
            set (ids::filterDrive, rnd (0.0f, 0.3f));
            lfo ({ Sine, Triangle, SawUp, SawDown }, 0.7f, { 7, 9, 10, 11 }, 0.3f, 0.9f);
            break;

        case 1: // Acid
            filter ({ LadderLP, Squelch, LP24 }, 150.0f, 2500.0f, 0.6f, 0.95f);
            set (ids::filterDrive, rnd (0.3f, 0.8f));
            set (ids::envDepth, rnd (0.3f, 0.9f));
            lfo ({ StepSeq, SawDown, Square }, 1.0f, { 2, 4, 7 }, 0.2f, 0.6f);
            if (chance (0.5f)) dist ({ 0, 2 }, 0.2f, 0.5f, 0.5f, 1.0f);
            if (chance (0.4f)) delay ({ 4, 6 }, 0.3f, 0.5f, 0.15f, 0.3f);
            break;

        case 2: // Dub Space
            filter ({ LP12, BP12, HP12 }, 300.0f, 3000.0f, 0.3f, 0.7f);
            lfo ({ Sine, Triangle }, 1.0f, { 10, 11, 12 }, 0.3f, 0.7f);
            delay ({ 6, 7, 8 }, 0.5f, 0.85f, 0.3f, 0.5f);
            reverb (0.6f, 0.95f, 0.2f, 0.4f);
            break;

        case 3: // Glitch
            filter ({ LP12, LP24, HP12, BP12, BP24, LadderLP, CombPlus, Formant, Phaser, BitCrush, RingMod, Scream }, 300.0f, 12000.0f, 0.1f, 0.8f);
            glitch ({ 0, 1, 2, 3 }, { 0, 1, 2, 3, 4 }, { 1, 2, 3, 4 }, 0.3f, 0.9f);
            lfo ({ StepSeq, SampleHold }, 1.0f, { 2, 4 }, 0.2f, 0.7f);
            if (chance (0.4f)) delay ({ 2, 4 }, 0.2f, 0.5f, 0.15f, 0.35f);
            break;

        case 4: // Lo-Fi
            filter ({ BitCrush, Telephone, LP12 }, 800.0f, 8000.0f, 0.2f, 0.7f);
            if (chance (0.6f)) dist ({ 4 }, 0.2f, 0.6f, 0.5f, 1.0f);
            if (chance (0.5f)) chorus (0.3f, 0.7f, 0.3f, 0.5f);
            set (ids::width, rnd (0.6f, 1.0f));
            lfo ({ SmoothRandom }, 0.0f, { 10 }, 0.05f, 0.2f);
            break;

        case 5: // Talk Box
            filter ({ Formant }, 100.0f, 10000.0f, 0.5f, 0.9f);
            set (ids::filterDrive, rnd (0.1f, 0.4f));
            lfo ({ Sine, Triangle, SmoothRandom, StepSeq }, 0.6f, { 4, 7, 9, 10 }, 0.3f, 0.9f);
            if (chance (0.3f)) chorus (0.2f, 0.5f, 0.2f, 0.4f);
            break;

        case 6: // Comb Metal
            filter ({ CombPlus, CombMinus, Metallic }, 60.0f, 2000.0f, 0.5f, 0.95f);
            lfo ({ Sine, Triangle, SawUp, StepSeq, SmoothRandom }, 0.5f, { 4, 7, 9, 10, 11 }, 0.1f, 0.5f);
            if (chance (0.3f)) flanger();
            if (chance (0.4f)) reverb (0.4f, 0.8f, 0.15f, 0.35f);
            break;

        case 7: // Phase Shift
            filter ({ Phaser }, 200.0f, 4000.0f, 0.3f, 0.9f);
            lfo ({ Sine, Triangle }, 0.4f, { 9, 10, 11, 12 }, 0.4f, 0.9f);
            if (chance (0.4f)) flanger();
            if (chance (0.3f)) chorus (0.2f, 0.5f, 0.2f, 0.4f);
            set (ids::width, rnd (1.0f, 1.5f));
            break;

        case 8: // Destroy
            dist ({ 0, 1, 2, 3, 4, 5 }, 0.5f, 1.0f, 0.7f, 1.0f);
            filter ({ WaveFold, Scream, Squelch, LadderLP, BP24 }, 300.0f, 6000.0f, 0.4f, 0.9f);
            set (ids::filterDrive, rnd (0.4f, 1.0f));
            lfo ({ Square, StepSeq, SampleHold, SawDown }, 0.8f, { 2, 4, 7 }, 0.1f, 0.6f);
            if (chance (0.5f))
            {
                set (ids::inflateOn, 1.0f);
                set (ids::inflateAmount, rnd (0.5f, 1.0f));
                set (ids::inflateCurve, rnd (0.0f, 50.0f));
            }
            set (ids::outGain, rnd (-6.0f, -3.0f));
            break;

        case 9: // Inflate (mastering-ish: gentle filter + loudness curve)
            if (chance (0.5f)) filter ({ LP12 }, 8000.0f, 20000.0f, 0.0f, 0.2f);
            else               filter ({ HP12 }, 20.0f, 80.0f, 0.0f, 0.2f);
            set (ids::inflateOn, 1.0f);
            set (ids::inflateAmount, rnd (0.3f, 1.0f));
            set (ids::inflateCurve, rnd (-50.0f, 50.0f));
            set (ids::width, rnd (1.0f, 1.3f));
            break;

        case 10: // Wide Air
            filter ({ HP12, BP12, Peak, HP24 }, 1500.0f, 9000.0f, 0.1f, 0.5f);
            chorus (0.3f, 0.6f, 0.3f, 0.5f);
            reverb (0.5f, 0.9f, 0.25f, 0.45f);
            set (ids::width, rnd (1.3f, 2.0f));
            lfo ({ Sine }, 0.5f, { 10, 11, 12 }, 0.1f, 0.3f);
            break;

        case 11: // Rhythm Gate
            filter ({ LP12, LP24, BP12, LadderLP }, 300.0f, 6000.0f, 0.2f, 0.7f);
            glitch ({ 2 }, { 2, 4, 7 }, { 0, 1, 2 }, 0.7f, 1.0f);
            lfo ({ Square, StepSeq }, 1.0f, { 2, 4, 7 }, 0.3f, 0.8f);
            if (chance (0.3f)) delay ({ 4, 6 }, 0.2f, 0.5f, 0.15f, 0.3f);
            break;

        case 12: // Ring Bells
            filter ({ RingMod }, 50.0f, 3000.0f, 0.0f, 0.6f);
            set (ids::filterMix, rnd (0.4f, 1.0f));
            lfo ({ Sine, Triangle, SmoothRandom, StepSeq }, 0.5f, { 7, 9, 10, 11 }, 0.1f, 0.5f);
            if (chance (0.6f)) reverb (0.5f, 0.9f, 0.2f, 0.4f);
            if (chance (0.4f)) delay ({ 4, 6, 7 }, 0.3f, 0.6f, 0.2f, 0.35f);
            break;

        case 13: // Scream
            filter ({ Scream, BP24, Peak }, 400.0f, 5000.0f, 0.7f, 0.98f);
            set (ids::filterDrive, rnd (0.3f, 0.9f));
            lfo ({ SawUp, Sine, Bounce }, 0.8f, { 4, 7, 9, 10 }, 0.3f, 0.8f);
            if (chance (0.5f)) dist ({ 1, 3 }, 0.3f, 0.6f, 0.5f, 1.0f);
            set (ids::outGain, rnd (-4.0f, 0.0f));
            break;

        case 14: // Tape Echo
            filter ({ LP12, BP12 }, 1000.0f, 6000.0f, 0.1f, 0.4f);
            delay ({ 4, 6, 7 }, 0.3f, 0.75f, 0.25f, 0.5f);
            if (chance (0.5f))
            {
                set (ids::delaySync, 0.0f);
                set (ids::delayTime, logRnd (80.0f, 700.0f));
            }
            chorus (0.2f, 0.5f, 0.25f, 0.35f);
            if (chance (0.5f)) dist ({ 2 }, 0.1f, 0.3f, 0.5f, 1.0f);
            if (chance (0.3f)) reverb (0.4f, 0.7f, 0.1f, 0.25f);
            lfo ({ SmoothRandom, Sine }, 0.3f, { 10, 11 }, 0.05f, 0.25f);
            break;

        default: // Chaos: anything goes
            set (ids::filterType, (float) r.nextInt (ft::count));
            set (ids::cutoff, logRnd (50.0f, 15000.0f));
            set (ids::reso, rnd (0.0f, 0.95f));
            set (ids::filterDrive, rnd (0.0f, 0.7f));
            set (ids::lfoShape, (float) r.nextInt (ls::count));
            set (ids::lfoSync, chance (0.5f) ? 1.0f : 0.0f);
            set (ids::lfoDiv, (float) r.nextInt (numLfoDivs));
            set (ids::lfoRate, logRnd (0.05f, 15.0f));
            set (ids::lfoDepth, rnd (-1.0f, 1.0f));
            set (ids::envDepth, rnd (-0.8f, 0.8f));
            set (ids::seqSeed, (float) r.nextInt (1000));
            if (chance (0.4f)) dist ({ 0, 1, 2, 3, 4, 5 }, 0.1f, 0.8f, 0.3f, 1.0f);
            if (chance (0.5f)) glitch ({ 0, 1, 2, 3 }, { 0, 1, 2, 3, 4, 5, 7 }, { 0, 1, 2, 3, 4 }, 0.2f, 0.9f);
            if (chance (0.4f)) chorus (0.2f, 0.8f, 0.2f, 0.6f);
            if (chance (0.4f)) flanger();
            if (chance (0.4f)) delay ({ 2, 3, 4, 5, 6, 7 }, 0.2f, 0.8f, 0.15f, 0.45f);
            if (chance (0.4f)) reverb (0.3f, 0.95f, 0.15f, 0.45f);
            if (chance (0.3f))
            {
                set (ids::inflateOn, 1.0f);
                set (ids::inflateAmount, rnd (0.2f, 1.0f));
                set (ids::inflateCurve, rnd (-50.0f, 50.0f));
            }
            set (ids::width, rnd (0.7f, 1.8f));
            set (ids::outGain, rnd (-6.0f, 0.0f));
            break;
    }

    // A little extra space on some presets of the "dry" banks.
    if ((bank == 0 || bank == 5 || bank == 11) && chance (0.25f))
        reverb (0.3f, 0.7f, 0.1f, 0.25f);
}

// RANDOM button: a random bank for the filter + LFO, then every effect rolled on its own:
// each one is switched on or off at random and gets random settings (at least two end up on).
inline void randomizeAll (int bank, juce::Random& r, juce::AudioProcessorValueTreeState& state)
{
    generate (bank, r, state);

    auto set = [&] (const char* id, float v)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    auto rnd    = [&] (float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); };
    auto logRnd = [&] (float lo, float hi) { return lo * std::pow (hi / lo, r.nextFloat()); };
    auto pick   = [&] (std::initializer_list<int> l) { return (float) *(l.begin() + r.nextInt ((int) l.size())); };

    constexpr int numFx = 7;
    bool on[numFx];
    int count = 0;
    for (auto& o : on) { o = r.nextFloat() < 0.45f; count += o ? 1 : 0; }
    while (count < 2) { auto& o = on[r.nextInt (numFx)]; if (! o) { o = true; ++count; } }

    set (ids::distOn, on[0] ? 1.0f : 0.0f);
    if (on[0])
    {
        set (ids::distType, pick ({ 0, 1, 2, 3, 4, 5 }));
        set (ids::distDrive, rnd (0.1f, 0.75f));
        set (ids::distMix, rnd (0.3f, 1.0f));
    }
    set (ids::glitchOn, on[1] ? 1.0f : 0.0f);
    if (on[1])
    {
        set (ids::glitchMode, pick ({ 0, 1, 2, 3 }));
        set (ids::glitchDiv, pick ({ 0, 1, 2, 3, 4, 5, 7 }));
        set (ids::glitchRepeat, pick ({ 0, 1, 2, 3, 4 }));
        set (ids::glitchProb, rnd (0.2f, 0.9f));
        set (ids::glitchMix, rnd (0.7f, 1.0f));
    }
    set (ids::chorusOn, on[2] ? 1.0f : 0.0f);
    if (on[2])
    {
        set (ids::chorusRate, logRnd (0.1f, 2.0f));
        set (ids::chorusDepth, rnd (0.2f, 0.8f));
        set (ids::chorusMix, rnd (0.2f, 0.55f));
    }
    set (ids::flangerOn, on[3] ? 1.0f : 0.0f);
    if (on[3])
    {
        set (ids::flangerRate, logRnd (0.05f, 1.5f));
        set (ids::flangerDepth, rnd (0.3f, 0.9f));
        set (ids::flangerFeedback, (r.nextBool() ? 1.0f : -1.0f) * rnd (0.3f, 0.85f));
        set (ids::flangerMix, rnd (0.3f, 0.6f));
    }
    set (ids::delayOn, on[4] ? 1.0f : 0.0f);
    if (on[4])
    {
        set (ids::delaySync, 1.0f);
        set (ids::delayDiv, pick ({ 2, 3, 4, 5, 6, 7, 8 }));
        set (ids::delayFeedback, rnd (0.2f, 0.75f));
        set (ids::delayPingPong, r.nextBool() ? 1.0f : 0.0f);
        set (ids::delayMix, rnd (0.15f, 0.45f));
    }
    set (ids::reverbOn, on[5] ? 1.0f : 0.0f);
    if (on[5])
    {
        set (ids::reverbSize, rnd (0.3f, 0.95f));
        set (ids::reverbDamp, rnd (0.2f, 0.8f));
        set (ids::reverbMix, rnd (0.1f, 0.45f));
        // Two in three get one of the SKAZI REVERB spaces instead of CLASSIC
        if (r.nextInt (3) > 0)
        {
            const int type = 1 + r.nextInt (reverbTypeNames().size() - 1);
            set (ids::reverbType, (float) type);
            set (ids::reverbDecay, type == 10 ? logRnd (6.0f, 18.0f) : type == 9 ? rnd (0.25f, 0.8f) : logRnd (0.8f, 6.0f));
            set (ids::reverbPreDelay, rnd (0.0f, 120.0f));
        }
    }
    set (ids::inflateOn, on[6] ? 1.0f : 0.0f);
    if (on[6])
    {
        set (ids::inflateAmount, rnd (0.2f, 1.0f));
        set (ids::inflateCurve, rnd (-50.0f, 50.0f));
    }
    set (ids::width, rnd (0.8f, 1.6f));
    // drive and inflate add level: pull the output down so RANDOM never jumps out loud
    set (ids::outGain, (on[0] || on[6]) ? rnd (-6.0f, -3.0f) : rnd (-2.0f, 0.0f));
}

inline void applyFactory (int program, juce::AudioProcessorValueTreeState& state)
{
    if (program <= 0)
    {
        resetToDefaults (state);
        return;
    }
    const int index = program - 1;
    juce::Random r (index * 7919 + 17);
    generate (index / perBank, r, state);
}
} // namespace presets
