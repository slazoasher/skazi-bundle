#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "SkatomDSP.h"

class SkatomProcessor : public juce::AudioProcessor
{
public:
    SkatomProcessor();
    ~SkatomProcessor() override
    {
        loader.stopThread (3000);
        delete pendingIR.exchange (nullptr); delete trashIR.exchange (nullptr);
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SKATOM"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // 16 matrix snapshots, sparse: { paramId: normalized value } per snapshot. Saved with the project,
    // and the DSP morphs them at the Matrix X/Y position (so XY is automatable and nothing floods the host).
    juce::String getSnaps() const { const juce::ScopedLock l (snapLock); return snapsJson; }
    void setSnaps (const juce::String& json);

    struct Telemetry
    {
        std::atomic<double> ppq { 0 }, bpm { 128 };
        std::atomic<bool> playing { false }, glActive { false }, riseDone { false };
        std::atomic<float> env { 0 }, rms { 0 }, gr { 0 }, pos { 0 }, rate { 1 }, macroEff { 0 }, riseProg { 0 };
        std::atomic<int> mode { 0 }, len { 1 }, drops { 0 }, activeBuild { 0 };
    } tele;
    std::atomic<bool> dropRequest { false };
    static constexpr int scopeSize = 8192;
    float scope[scopeSize] {};
    std::atomic<int> scopeWrite { 0 };
    double getSr() const { return sr; }

private:
    using SnapTable = std::array<std::array<float, skatom::NUM_PARAMS>, 16>; // -1 = not pushed

    void control (double beats, double blockBeats, bool advance, const float* L, const float* R, int m);
    float eff (int id) const;
    void fireDrop();
    void process (float* L, float* R, int n, double ppq, double bpm, bool advance);

    std::array<std::atomic<float>*, skatom::NUM_PARAMS> raw {};
    float macroOff[skatom::NUM_PARAMS] {}, modOff[skatom::NUM_PARAMS] {}, matOff[skatom::NUM_PARAMS] {}, matEnum[skatom::NUM_PARAMS] {}, effv[skatom::NUM_PARAMS] {};
    double sr = 48000, freeBeats = 0, riseProg = 0, riseStart = 0, riseEnd = 4, riseLast = 0;
    bool wasPlaying = false, riseDone = false, riseArmed = false, riseOnHost = false, hostPlaying = false, building = false;
    int activeBuild = 0;
    float swallowNow = 0, bloomNow = 1;
    float pumpNow = 0, widthNow = 0, pumpG = 1; double pumpRate = 1; // ACELERA
    float endlessMix = 0, endlessOct = 0; skatom::PitchShifter shep[2]; skatom::Ramp gEndless; // ACELERA ENDLESS (Shepard climb)
    float env = 0, envMs = 0, hi = 0, sh[3] {}, xs = 0.125f, ys = 0.125f, macroEffNow = 0;
    long shN[3] { -1, -1, -1 };
    juce::Random rng;

    // matrix
    SnapTable snapMsg {}, snapRT {};
    bool anyPush[skatom::NUM_PARAMS] {};
    juce::SpinLock snapSpin;
    std::atomic<int> snapVer { 0 };
    int snapVerRT = -1;
    juce::CriticalSection snapLock;
    juce::String snapsJson;

    skatom::GlitchSpectral glitch;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    skatom::Biquad filtA, form[3], bassShelf, bassMud, hpf1, hpf2, hpfSend, hpfSend2, noiseBP, dlyLP;
    skatom::Ramp gFormDry, gFormWet, gDistWet, gDistDry, gNoise, gChorus, gSend, gSize, gJet, gSwallow, gWet, gDry, gOut, gMake, gBloom, gPump, gWidth;
    std::vector<float> chorusBuf, jetBuf[2], dlyBuf[2], dryBuf[2];
    int chorusW = 0, jetW = 0, dlyW = 0, dryW = 0, latency = 0;
    double chPh[2] {}, spinPh = 0;
    float duck = 1, rollGain = 1; int duckHold = 0;
    juce::dsp::Convolution verbS { juce::dsp::Convolution::NonUniform { 1024 } }, verbL { juce::dsp::Convolution::NonUniform { 1024 } };
    juce::dsp::Limiter<float> lim;
    skatom::Comp glue;
    juce::AudioBuffer<float> sendS, sendL, dlyIn, dryCopy, monoR, tailFb;
    skatom::PitchShifter shim;
    /* TAIL IRs: the loader thread builds the (heavy) IR, the audio thread picks it up lock-free and hands it to
       the convolution (JUCE wants loads from the processing side; its prepare() drops queued loads).
       gen changes on every prepareToPlay, so an IR built for the old sample rate is thrown away. */
    std::atomic<juce::AudioBuffer<float>*> pendingIR { nullptr }, trashIR { nullptr };
    std::atomic<double> pendingSr { 48000 };
    std::atomic<int> gen { 0 }, builtKey { -1 };
    struct TailLoader : juce::Thread
    {
        explicit TailLoader (SkatomProcessor& o) : juce::Thread ("SKATOM tails"), p (o) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                delete p.trashIR.exchange (nullptr);
                const int g = p.gen.load(), t = juce::jlimit (0, skatom::NUM_TAILS - 1, (int) p.raw[skatom::tailType]->load()), key = g * 64 + t;
                if (g > 0 && key != p.builtKey.load())
                {
                    const double sr = p.pendingSr.load();
                    const auto& d = skatom::TAILS[t];
                    auto* b = new juce::AudioBuffer<float> (makeTailIR (sr, d, t));
                    if (p.gen.load() == g) { p.builtKey = key; delete p.pendingIR.exchange (b); }
                    else delete b;
                }
                wait (40);
            }
        }
        SkatomProcessor& p;
    } loader { *this };
    static juce::AudioBuffer<float> makeTailIR (double sr, const skatom::TailDef& d, int type);
    float rmsAcc = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkatomProcessor)
};
