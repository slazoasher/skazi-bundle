#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace skazon
{
    constexpr int tableSize = 2048, numFrames = 16, numMips = 11, numSlots = 16, maxUnison = 8, maxGrains = 12;
    constexpr int numLfos = 4, numMacros = 4, combSize = 4096, numBuiltInTables = 12, userTableIndex = 12;

    const juce::StringArray& tableNames();
    const juce::StringArray& sourceNames();
    const juce::StringArray& targetNames();
    const juce::StringArray& opNames();
    const juce::StringArray& lfoShapeNames();
    const juce::StringArray& divNames();
    const juce::StringArray& warpNames();
    const juce::StringArray& filterNames();
    double divBeats (int index);

    // New entries only ever go at the end: presets store these as indices.
    enum Source { sNone, sEnv1, sEnv2, sLfo1, sLfo2, sVelocity, sNote, sWheel, sAftertouch, sRandom,
                  sLfo3, sLfo4, sMacro1, sMacro2, sMacro3, sMacro4, numSources };
    enum Target { tNone, tAPos, tBPos, tGPos, tPitch, tAPitch, tBPitch, tALevel, tBLevel, tGLevel,
                  tGSize, tGDensity, tCutoff, tRes, tDrive, tPan, tLfo1Rate, tLfo2Rate,
                  tAWarp, tBWarp, tADetune, tBDetune, tGSpray, tSub, tNoise, tLfo3Rate, tLfo4Rate, tFEnv, numTargets };
    // The spec's math nodes, one per matrix slot, applied to the source before the amount.
    enum Op { opLinear, opInvert, opAbs, opSquare, opQuantize, opFold, numOps };
    enum Warp { wNone, wSync, wBendUp, wBendDown, wPwm, wFm, wRing, wQuantize, numWarps };
    enum FilterType { fLow, fBand, fHigh, fNotch, fComb };
    float applyOp (int op, float x);

    // 12 tables x 16 frames, each frame band-limited into 11 octave mips (mip m keeps 1024 >> m harmonics).
    class Wavetables
    {
    public:
        static const Wavetables& get();
        float read (int table, float pos, int mip, float phase) const; // pos 0..1 morphs frames, phase 0..1
        const float* frame (int table, int frame, int mip) const;      // tableSize + 1 samples (wrap copy)
        static int mipFor (double hz, double sampleRate);

    private:
        Wavetables();
        std::vector<float> data;
    };

    float lfoShape (int shape, float phase, float hold, float prevHold); // -1..1

    // A wavetable made from a dropped audio file: 16 frames x 11 mips, same layout as a built-in table.
    struct UserTable
    {
        std::vector<float> data;
        juce::String name;
    };
    std::shared_ptr<const UserTable> makeUserTable (const juce::AudioBuffer<float>& audio, const juce::String& name);
    // 16 single-cycle frames of tableSize samples each, back to back.
    std::shared_ptr<const UserTable> makeUserTableFromFrames (const std::vector<float>& frames, const juce::String& name);

    float readFrames (const float* tableBase, float pos, int mip, float phase);
    const float* tableBase (int table, const UserTable* user); // USER falls back to BASIC when empty
    float warpPhase (int mode, float amt, float phase, float other);
    float warpOutput (int mode, float amt, float value, float other);

    // Everything a voice reads, filled by the processor once per block.
    struct Snapshot
    {
        struct Osc { bool on = true; int table = 0, warp = 0; float pos = 0, level = 0.7f, tune = 0, detune = 0.2f, warpAmt = 0; int unison = 1; } osc[2];
        bool gOn = false;
        int gTable = 0;
        float gPos = 0.5f, gSizeMs = 80, gDensity = 20, gSpray = 0.2f, gSpread = 0, gPitch = 0, gLevel = 0.6f;
        float sub = 0, noise = 0;
        int fType = 0;
        float cutoff = 20000, res = 0, drive = 0, fEnv = 0, fKey = 0;
        juce::ADSR::Parameters env[2];
        struct Lfo { int shape = 0; float hz = 1; } lfo[numLfos];
        struct Slot { int src = 0, op = 0, dst = 0; float amount = 0; } slots[numSlots];
        float macro[numMacros] {};
        float glide = 0, bend = 0, wheel = 0, aftertouch = 0;
        int mwLfo = 1, mwDst = 0; float mwAmt = 0; // MOD WHEEL: wheel x (LFO or 1) x amount -> target
        const UserTable* user = nullptr;
    };

    struct Sound : juce::SynthesiserSound
    {
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    class Voice : public juce::SynthesiserVoice
    {
    public:
        Voice (const Snapshot&, int& lastNote);

        bool canPlaySound (juce::SynthesiserSound*) override { return true; }
        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override;
        void stopNote (float, bool allowTailOff) override;
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void aftertouchChanged (int value) override { polyPressure = (float) value / 127.0f; } // polyphonic aftertouch
        void setCurrentPlaybackSampleRate (double) override;
        void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

        // Mono legato: glide to a new note without retriggering the envelopes.
        void legato (int newNote, float newVelocity);
        int currentNote() const { return note; }

        // Last values, for the displays.
        float livePos[3] {};
        float liveMods[numTargets] {};
        float liveLfo[numLfos] {};

    private:
        static constexpr int chunk = 16; // control-rate step for modulation

        struct Grain { bool on = false; float phase = 0, inc = 0, pos = 0; int age = 0, length = 1, mip = 0; float gl = 0, gr = 0; };

        void controlTick();
        float source (int src) const;

        const Snapshot& s;
        int& lastNote;
        juce::ADSR amp, modEnv;
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::Random rng;

        int note = 60;
        float polyPressure = 0;
        float bendNow = 0; // smoothed pitch bend, semitones (the wheel arrives once per block)
        float velocity = 1, randomValue = 0, semi = 60, env1Value = 0, env2Value = 0;
        float phase[2][maxUnison] {}, subPhase = 0, oscOut[2] {};
        float lfoPhase[numLfos] {}, lfoHold[numLfos] {}, lfoPrev[numLfos] {}, rateMod[numLfos] {};
        Grain grains[maxGrains];
        int grainCountdown = 0;
        int sinceTick = 0;
        float levelNow[3] {}, levelStep[3] {}; // osc A, osc B, grains: ramped per chunk so level mods don't click
        std::vector<float> comb[2];
        int combWrite = 0;

        // Per-chunk values from controlTick.
        struct Control
        {
            int n[2] { 1, 1 }, mip[2] {}, gLength = 1000, combDelay = 100;
            float inc[2][maxUnison] {}, panL[2][maxUnison] {}, panR[2][maxUnison] {};
            float pos[2] {}, warp[2] {}, levelTarget[3] {}, gHz = 440, gPos = 0, gInterval = 1000, gSpray = 0, subInc = 0;
            float sub = 0, noise = 0, resonance = 1, combFb = 0;
            float preGain = 1, postGain = 1, outL = 1, outR = 1;
        } ctl;
    };
}
