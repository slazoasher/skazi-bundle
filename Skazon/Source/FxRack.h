#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace skazon
{
    const juce::StringArray& distNames();

    // Global stereo FX chain: DIST -> EQ -> PHASER -> CHORUS -> HYPER/DIMENSION -> DELAY -> REVERB -> OTT -> master -> limiter.
    class FxRack
    {
    public:
        struct Params
        {
            bool distOn = true; int distType = 0; float distDrive = 0, distTone = 20000, distMix = 1;
            bool eqOn = true; float eqLow = 0, eqLowFreq = 120, eqHigh = 0, eqHighFreq = 6000;
            bool phOn = true; float phMix = 0, phRate = 0.5f, phDepth = 0.6f, phFb = 0.4f;
            bool chOn = true; float chMix = 0, chRate = 0.7f, chDepth = 0.35f;
            bool hyOn = true; float hyMix = 0, hyDetune = 0.4f, hyDim = 0;
            bool dlOn = true; float dlMix = 0, dlFb = 0.4f; double dlBeats = 0.75;
            bool rvOn = true; float rvMix = 0, rvSize = 0.6f;
            bool ottOn = true; float ottDepth = 0, ottTime = 0.5f, ottUp = 1, ottDown = 1, ottGain = 0;
            float masterDb = 0;
            double bpm = 120;
        };

        void prepare (double sampleRate, int blockSize);
        static float transfer (int distType, float drive, float x); // the distortion curve, also drawn by the editor
        void process (juce::AudioBuffer<float>&, const Params&);

        std::array<std::atomic<float>, 3> ottGainDb {}; // per band, for the meters

    private:
        void distort (float* l, float* r, int n, const Params&);
        void ott (float* l, float* r, int n, const Params&);
        void hyper (float* l, float* r, int n, const Params&);

        double sr = 48000;
        // distortion state
        float crushHold[2] {}, dcX[2] {}, dcY[2] {}, toneZ[2] {};
        int crushCount = 0;
        // EQ
        juce::dsp::IIR::Filter<float> lowShelf[2], highShelf[2];
        float eqState[4] { 1e9f, 1e9f, 1e9f, 1e9f };
        juce::dsp::Phaser<float> phaser;
        juce::dsp::Chorus<float> chorus;
        static constexpr int hyperSize = 8192; // power of two, > 40 ms at 192k
        std::vector<float> hyperBuf[2];
        int hyperWrite = 0;
        float hyperPhase[4] {};
        juce::AudioBuffer<float> delayBuffer;
        int delayWrite = 0;
        juce::Reverb reverb;
        // OTT: 3 bands split at 120 Hz and 2.5 kHz
        juce::dsp::LinkwitzRileyFilter<float> xLow, xHigh;
        float ottEnv[3] {};
        juce::SmoothedValue<float> outGain;
        juce::dsp::Limiter<float> limiter;
    };
}
