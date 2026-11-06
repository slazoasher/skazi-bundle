#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "EqDSP.h"

namespace ParamIDs
{
    inline constexpr auto phase     = "PHASE";
    inline constexpr auto output    = "OUTPUT";
    inline constexpr auto tubeOn    = "T_IN";
    inline constexpr auto lowFreq   = "T_LF";
    inline constexpr auto lowBoost  = "T_LB";
    inline constexpr auto lowAtten  = "T_LA";
    inline constexpr auto highFreq  = "T_HF";
    inline constexpr auto highBoost = "T_HB";
    inline constexpr auto bandwidth = "T_BW";
    inline constexpr auto attenFreq = "T_AF";
    inline constexpr auto highAtten = "T_HA";
    inline constexpr auto drive     = "T_DRIVE";
    inline constexpr auto monMode   = "MON_MODE";
    inline constexpr auto monPolL   = "MON_POLL";
    inline constexpr auto monPolR   = "MON_POLR";
    inline constexpr auto monSwap   = "MON_SWAP";
    inline constexpr auto autoGain  = "AUTOGAIN";
    inline constexpr auto gainScale = "GAINSCALE";

    // Per-band parameter names: band (2, "FREQ") -> "B3_FREQ".
    inline constexpr const char* bandParams[] { "ON", "TYPE", "FREQ", "GAIN", "Q", "CH", "SLOPE", "DYN", "THR", "SC" };
    inline juce::String band (int index, const char* name) { return "B" + juce::String (index + 1) + "_" + name; }
}

namespace Names
{
    inline const juce::StringArray types    { "BELL", "LO SHELF", "HI SHELF", "LO CUT", "HI CUT", "NOTCH", "BAND", "TILT" };
    inline const juce::StringArray channels { "STEREO", "MID", "SIDE", "LEFT", "RIGHT" };
    inline const juce::StringArray slopes   { "6 dB", "12 dB", "24 dB", "36 dB", "48 dB", "72 dB", "96 dB", "BRICK" };
    inline const juce::StringArray monitor  { "STEREO", "MONO", "SIDE" };
    inline const juce::StringArray phases   { "ZERO LATENCY", "NATURAL", "LINEAR" };
    inline const juce::StringArray bands    { "LOW CUT", "BAND 1", "BAND 2", "BAND 3", "BAND 4", "HIGH CUT" };
}

// Lock-free ring the audio thread writes and the analyzer reads; a torn read only costs one display frame.
struct ScopeFifo
{
    static constexpr int size = 16384;
    std::array<float, size> left {}, right {};
    std::atomic<int> writePos { 0 };

    void push (const float* l, const float* r, int n)
    {
        int pos = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) pos] = l[i];
            right[(size_t) pos] = r[i];
            pos = (pos + 1) & (size - 1);
        }
        writePos.store (pos, std::memory_order_release);
    }

    // Mono sum, or both channels when destRight is given.
    void copyLatest (float* dest, int count, float* destRight = nullptr) const
    {
        const int end = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            const auto k = (size_t) ((end - count + i + size) & (size - 1));
            if (destRight != nullptr)
            {
                dest[i] = left[k];
                destRight[i] = right[k];
            }
            else
                dest[i] = 0.5f * (left[k] + right[k]);
        }
    }
};

class SqualizerAudioProcessor : public juce::AudioProcessor,
                                private juce::Timer,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    SqualizerAudioProcessor();
    ~SqualizerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    sq::BandSettings bandSettings (int band) const;
    sq::TubeSettings tubeSettings() const;
    bool tubeOn() const { return get (ParamIDs::tubeOn) > 0.5f; }
    int phaseMode() const { return (int) get (ParamIDs::phase); }
    double getSampleRateSafe() const { return sampleRate; }
    int linearLatency() const { return firSize / 2; }

    // Rebuilds the LINEAR mode FIR from the current bands. Runs on the message thread (timer), or directly in tests.
    void rebuildLinearPhase();

    // Declared before apvts, which records its parameter changes here.
    juce::UndoManager undoManager { 30000, 50 };
    juce::AudioProcessorValueTreeState apvts;
    ScopeFifo preScope, postScope, sideScope;
    std::atomic<bool> sidechainActive { false };   // a sidechain is connected and carrying signal
    std::atomic<float> autoGainDb { 0.0f };

    // A/B compare: two complete settings; switching stores the current one and recalls the other.
    int activeSlot() const { return slot; }
    void switchSlot (int newSlot);
    void copyToOtherSlot();

    // Level compensation for AUTO GAIN, from the static curve (bands + tube) averaged over a pink spectrum.
    void updateAutoGain();
    bool monitorActive() const;
    std::array<std::atomic<float>, sq::numBands> liveDyn {};
    std::atomic<float> outPeak { 0.0f };

    // Band solo (Pro-Q style audition): while >= 0 you hear only that band's region, a band-pass at its
    // frequency as wide as its Q. The editor sets it while a node is held down.
    std::atomic<int> soloBand { -1 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    float get (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override { firDirty = true; gainDirty = true; }

    std::array<sq::Band, sq::numBands> bands;
    sq::TubeEq tube;
    void processSolo (float* l, float* r, int n);
    std::array<std::array<sq::Filter, 2>, 2> soloFilters;   // [channel][section]
    double soloMix = 0.0;
    int soloShown = 0;
    juce::SmoothedValue<float> outGain;
    double sampleRate = 44100.0;

    // LINEAR mode: a 2x2 FIR matrix (so L/R and M/S bands combine into one pass) as a direct pair and a cross pair.
    juce::dsp::Convolution firDirect, firCross;
    juce::AudioBuffer<float> crossBuffer;
    int firSize = 16384;
    std::atomic<bool> firDirty { true }, gainDirty { true };
    std::array<juce::ValueTree, 2> slots;
    int slot = 0;

    int currentPreset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SqualizerAudioProcessor)
};
