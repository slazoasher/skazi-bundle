#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../Common/PresetFader.h"
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "DSP/FilterEngine.h"
#include "DSP/Lfo.h"
#include "DSP/Glitch.h"
#include "DSP/Effects.h"
#include "DSP/ReverbDSP.h"

class GlitchMagmaProcessor : public juce::AudioProcessor
{
public:
    GlitchMagmaProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (ids::bypass); }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Loads a brand-new random preset from a random bank (UI "RANDOM" button).
    void randomize();
    bool isRandomPreset() const noexcept { return randomPreset; }
    const juce::String& getRandomName() const noexcept { return randomName; }

    juce::AudioProcessorValueTreeState apvts;
    skazi::PresetFader fader; // fades around preset changes

    // Read by the editor for the live displays.
    std::atomic<float> uiLfoPhase { 0.0f }, uiLfoValue { 0.0f }, uiModCutoff { 1000.0f };
    std::atomic<int> presetVersion { 0 };

private:
    void resetDsp();

    double sr = 44100.0;
    FilterEngine filter;
    Lfo lfo;
    Glitch glitch;
    fx::Distortion dist;
    std::array<fx::DcBlocker, 2> dcBlock;
    fx::StereoDelay delay;
    juce::dsp::Chorus<float> chorus, flanger;
    juce::dsp::Reverb reverb;
    verb::Engine spaces;                         // the SKAZI REVERB algorithms
    juce::AudioBuffer<float> classicBuffer, spaceWet, bypassDry;
    juce::SmoothedValue<float> classicBlend, bypassBlend;
    bool spacesIdle = true, fullyBypassed = false;

    void processReverb (juce::AudioBuffer<float>&, int numCh, int numSamples);

    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> inGainSm, outGainSm, mixSm, filterMixSm;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSm;
    float env = 0.0f, envAttack = 0.0f, envRelease = 0.0f;

    int currentProgram = 0;
    bool randomPreset = false;
    juce::String randomName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchMagmaProcessor)
};
