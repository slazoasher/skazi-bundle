#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Loudness.h"

// Pass-through analyser. The audio thread only copies samples into a ring and runs the loudness
// meter; the editor does the FFT and the meter maths on the message thread.
class SkzlProcessor : public juce::AudioProcessor
{
public:
    static constexpr int RING = 1 << 16; // power of two

    SkzlProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          ringL ((size_t) RING), ringR ((size_t) RING) {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
            && l.getMainInputChannelSet() == out;
    }

    void prepareToPlay (double sr, int) override { sampleRate = sr; loud.prepare (sr); }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int n = buffer.getNumSamples(), ch = juce::jmin (2, getTotalNumInputChannels());
        if (ch == 0 || n == 0) return;
        if (testTone) // SKAZILAYZER_TESTTONE=1: 1 kHz at -20 dBFS + 55 Hz at -12 dBFS, for checking the whole chain
            for (int i = 0; i < n; ++i, tonePhase += 1.0 / sampleRate)
                for (int c = 0; c < ch; ++c)
                    buffer.setSample (c, i, (float) (0.1 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * tonePhase)
                                                   + 0.251 * std::sin (juce::MathConstants<double>::twoPi * 55.0 * tonePhase)));
        const float* L = buffer.getReadPointer (0);
        const float* R = ch > 1 ? buffer.getReadPointer (1) : L;
        const auto w = written.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((w + (uint64_t) i) & (RING - 1));
            ringL[idx] = L[i]; ringR[idx] = R[i];
        }
        written.store (w + (uint64_t) n, std::memory_order_release);
        const float* chans[2] = { L, R };
        loud.process (chans, ch, n);
        if (wrapperType == wrapperType_Standalone) buffer.clear(); // standalone = meter only, no monitoring feedback
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SKAZILAYZER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // the UI settings (skin, view, resolution, ballistics) travel with the DAW project
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        const juce::ScopedLock sl (stateLock);
        juce::MemoryOutputStream (dest, false).writeString (uiState);
    }
    void setStateInformation (const void* data, int size) override
    {
        const juce::ScopedLock sl (stateLock);
        uiState = juce::MemoryInputStream (data, (size_t) size, false).readString();
    }
    juce::String getUiState() { const juce::ScopedLock sl (stateLock); return uiState; }
    void setUiState (const juce::String& s) { const juce::ScopedLock sl (stateLock); uiState = s; }

    std::vector<float> ringL, ringR;
    std::atomic<uint64_t> written { 0 };
    std::atomic<double> sampleRate { 48000 };
    Loudness loud;

private:
    juce::CriticalSection stateLock;
    juce::String uiState;
    const bool testTone = juce::SystemStats::getEnvironmentVariable ("SKAZILAYZER_TESTTONE", {}) == "1";
    double tonePhase = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkzlProcessor)
};
