#pragma once

// SKACID engine: one 303-style voice (saw/square + sub into a screaming 4-pole ladder, accent and slide),
// a 16-step sequencer that can evolve while it plays (chance, live mutation, ratchets, drift),
// and a corrosive FX rack (5 distortions, ping-pong delay, reverb).
// The voice and the distortion run at 2x the host rate.
#include "../../Common/SkaziCore.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace skacid
{
constexpr int NumSteps = 16;

enum Wave { Saw, Square };
enum Gate { Rest, Note, Tie };
enum Octave { Down, Mid, Up };
enum LfoShape { LfoSine, LfoTri, LfoSaw, LfoSquare, LfoSH };
enum LfoTarget { ToCutoff, ToReso, ToPitch, ToDrive };
enum DistType { Crunch, Fuzz, Fold, Rectify, Crush };
enum SeqRate { R8, R16, R16T, R32 };
enum PlayMode { ModeSeq, ModeArp, ModeMidi };
enum FilterType { F24, F18, F12, FBand };
enum ArpMode { ArpUp, ArpDown, ArpUpDown, ArpRandom, ArpPlayed };

const std::vector<skazi::ParamDef>& paramDefs();
juce::String stepId (int step, const char* field); // "s1_note", "s1_oct", "s1_gate", "s1_acc", "s1_sld"

struct Step
{
    int note = 0;  // 0..12 semitones above the root
    int oct = Mid;
    int gate = Note;
    bool acc = false, sld = false;
};
using Pattern = std::array<Step, NumSteps>;

struct Settings
{
    int wave = Saw;
    float sub = 0, tune = 0, fine = 0;
    float cutoff = 500, reso = 0.6f, envMod = 0.5f, decay = 300, accent = 0.6f, fDrive = 0.2f;
    int fltType = F24;
    float keyTrack = 0, fAttack = 0;                                  // ms
    float ampAtk = 2, ampDec = 1000, ampSus = 1, ampRel = 6;          // ms, sustain 0..1
    float vca = 0.5f, glideMs = 120;
    bool glideOn = false;
    float ottDepth = 0, ottTime = 0.3f;
    float slideMs = 60, gateLen = 0.55f;
    int lfoShape = LfoSine, lfoRate = 2, lfoTarget = ToCutoff;
    float lfoDepth = 0, drift = 0;
    int distType = Crunch;
    float distDrive = 0.3f, distTone = 0.7f, distMix = 1;
    int dlyTime = 11;
    float dlyFb = 0.35f, dlyMix = 0;
    float revSize = 0.4f, revMix = 0;
    float volume = 0;
    int mode = ModeSeq, arpMode = ArpUp, arpOct = 1;
    int seqRate = R16, seqLen = 16, key = 0;   // key: semitones from C, -5..+6 (the nearest C..B)
    float swing = 0, chance = 1, mutate = 0, ratchet = 0;

    template <typename Get> static Settings fromValues (Get v);
};

// A small, fast random source that is safe on the audio thread
struct FastRandom
{
    uint32_t s = 0x9e3779b9u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // 0..1
    int below (int n) { return (int) (uni() * (float) n) % juce::jmax (1, n); }
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Settings&, const Pattern&, const skazi::HostTime&);

    // For the editor
    std::atomic<int> playStep { -1 };          // pattern step now sounding, -1 when stopped
    std::atomic<uint32_t> mutatedMask { 0 };   // steps the live mutation changed this loop
    std::atomic<float> outLevel { 0.0f };
    std::atomic<float> cutoffNow { 500.0f };   // the filter's cutoff after env, accent, LFO and drift

private:
    // ---- voice ----
    void noteOn (int midiNote, bool accent, bool legato);
    void gateOff();
    float renderVoice (const Settings&, float lfo, float& osOut2); // returns the first of two oversampled samples

    // ---- sequencer ----
    void runSequencer (const Settings&, const Pattern&, double beatPos, bool running);
    void startStep (const Settings&, const Pattern&, long long count);
    void rollMutations (const Settings&, const Pattern&);
    int transpose() const { return heldCount > 0 ? lastHeld - 60 : 0; }
    int arpNote (const Settings&);

    double sr = 48000.0, osr = 96000.0;
    FastRandom rng;

    // oscillator
    double phase = 0.0, subPhase = 0.0;
    float pitchNow = 36.0f, pitchTarget = 36.0f; // MIDI note, glides when sliding
    bool sliding = false, portamento = false; // portamento: GLIDE is on, every new note glides from the last one

    // envelopes
    bool gate = false, accented = false, fRising = false;
    int ampStage = 0;   // 0 attack, 1 decay / sustain
    float amp = 0.0f, fEnv = 0.0f, accEnv = 0.0f, accCap = 0.0f;

    // ladder filter
    float y1 = 0, y2 = 0, y3 = 0, y4 = 0, hpState = 0, cutSmooth = 6.2f;

    // distortion
    float toneState = 0, crushHold = 0, crushCount = 0, dcIn = 0, dcOut = 0;

    // 2x decimator
    static constexpr int FirLen = 31;
    std::array<float, FirLen> fir {};
    std::array<float, FirLen * 2> firBuf {};
    int firPos = 0;
    float decimate (float a, float b);

    // OTT: three bands, each pushed up when quiet and down when loud (1x, mono, before the delay)
    float ott (float x, const Settings&);
    juce::dsp::LinkwitzRileyFilter<float> otLow, otHigh;
    float otEnv[3] {}, otGain[3] { 1.0f, 1.0f, 1.0f };

    // delay + reverb (1x, stereo)
    std::vector<float> dlyL, dlyR;
    int dlyPos = 0;
    float dlySmooth = 0;
    juce::Reverb reverb;

    // modulation
    double lfoPhase = 0.0, freeBeats = 0.0;
    float shValue = 0, driftNow = 0, driftTarget = 0;
    int lastShCycle = -1;

    // MIDI
    std::array<int, 16> held {};
    int heldCount = 0, lastHeld = 60;
    long long arpPos = 0;

    // sequencer state
    long long lastCount = -1;
    int curIndex = -1, curNote = 36, ratchets = 1, lastSub = 0;
    bool stepPlays = false, holdThrough = false, wasRunning = false;
    Step cur;
    Pattern live {};
    uint32_t liveMask = 0;
    float levelFollow = 0;
};

template <typename Get>
Settings Settings::fromValues (Get v)
{
    Settings s;
    s.wave = (int) v ("wave"); s.sub = v ("sub") * 0.01f; s.tune = v ("tune"); s.fine = v ("fine");
    s.cutoff = v ("cutoff"); s.reso = v ("reso") * 0.01f; s.envMod = v ("envMod") * 0.01f; s.decay = v ("decay");
    s.accent = v ("accent") * 0.01f; s.fDrive = v ("fDrive") * 0.01f;
    s.fltType = (int) v ("fltType"); s.keyTrack = v ("keyTrack") * 0.01f; s.fAttack = v ("fAttack");
    s.vca = v ("vca") * 0.01f; s.glideOn = v ("glideOn") > 0.5f; s.glideMs = v ("glide");
    s.ampAtk = v ("ampAtk"); s.ampDec = v ("ampDec"); s.ampSus = v ("ampSus") * 0.01f; s.ampRel = v ("ampRel");
    s.ottDepth = v ("ottDepth") * 0.01f; s.ottTime = v ("ottTime") * 0.01f;
    s.slideMs = v ("slide"); s.gateLen = v ("gateLen") * 0.01f;
    s.lfoShape = (int) v ("lfoShape"); s.lfoRate = (int) v ("lfoRate"); s.lfoTarget = (int) v ("lfoTarget");
    s.lfoDepth = v ("lfoDepth") * 0.01f; s.drift = v ("drift") * 0.01f;
    s.distType = (int) v ("distType"); s.distDrive = v ("distDrive") * 0.01f; s.distTone = v ("distTone") * 0.01f; s.distMix = v ("distMix") * 0.01f;
    s.dlyTime = (int) v ("dlyTime"); s.dlyFb = v ("dlyFb") * 0.01f; s.dlyMix = v ("dlyMix") * 0.01f;
    s.revSize = v ("revSize") * 0.01f; s.revMix = v ("revMix") * 0.01f;
    s.volume = v ("volume");
    s.mode = (int) v ("playMode"); s.arpMode = (int) v ("arpMode"); s.arpOct = (int) v ("arpOct");
    s.seqRate = (int) v ("seqRate"); s.seqLen = (int) v ("seqLen");
    { const int k = (int) v ("seqKey"); s.key = k > 6 ? k - 12 : k; }   // G..B sit below C, so basslines stay low
    s.swing = v ("swing") * 0.01f; s.chance = v ("chance") * 0.01f; s.mutate = v ("mutate") * 0.01f; s.ratchet = v ("ratchet") * 0.01f;
    return s;
}

// Pattern tools shared by the editor buttons, the presets and the tests
namespace patterns
{
    enum Style { Classic, Rolling, Squelch, Rave, Dub, Mutant };
    Pattern generate (juce::Random& r, int style);
    Pattern mutate (const Pattern& p, juce::Random& r, float amount);
    Pattern shift (const Pattern& p, int direction, int length);
}
} // namespace skacid
