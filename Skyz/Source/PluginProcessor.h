#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"
#include "Fx.h"
#include "Arp.h"
#include "Presets.h"

class SkyzProcessor : public juce::AudioProcessor
{
public:
    SkyzProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SKY Z"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return (int) skyz::factoryPresets().size() + userPresetFiles().size(); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboard;

    // UI actions (message thread)
    juce::StringArray presetNames();
    juce::String currentPresetName() const { return presetName; }
    void randomPreset();
    static float measureTrimDb (const skyz::Preset&, double sampleRate); // offline loudness -> trim for generated sounds
    void applyPreset (const skyz::Preset&);
    void mutate();
    void abSwap();
    bool saveUserPreset (const juce::String& name);
    void setLanes (const juce::String&);
    juce::String lanesString() const { return std::atomic_load (&lanes)->toString(); }
    void setChain (const juce::String&);
    juce::String chainString() const { return chain; }
    juce::File writeArpMidi() const;
    static juce::Array<juce::File> userPresetFiles();

    // UI -> audio
    std::atomic<int> uiPitch { -1 }, uiMod { -1 };
    // audio -> UI
    std::atomic<int> arpStep { -1 }, voices { 0 };
    std::atomic<float> lfoPhase[4] {}, wheelP { 0.5f }, wheelM { 0.0f };
    std::atomic<double> ppqNow { 0 }, bpmNow { 120 };
    std::atomic<bool> lanesChanged { false }; // a preset changed the lanes: the editor resends them
    std::atomic<float> peakL { 0 }, peakR { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void applyValues (const std::vector<std::pair<int, float>>&);
    juce::ValueTree fullState();
    void loadFullState (const juce::ValueTree&);

    std::array<std::atomic<float>*, skyz::NUM_PARAMS> raw {};
    skyz::Values vals;
    std::unique_ptr<skyz::Synth> synthPtr = std::make_unique<skyz::Synth>();   // rebuilt on every restart: a restart is a clean slate
    std::unique_ptr<skyz::FxChain> fxPtr = std::make_unique<skyz::FxChain>();
    skyz::Shared shared;
    std::unique_ptr<skyz::Arp> arpPtr = std::make_unique<skyz::Arp>();
    std::shared_ptr<const skyz::Lanes> lanes, previousLanes, audioLanes;
    std::atomic<float> trimGain { 1.0f };
    juce::String chain = "DRIVE,CHORUS,PHASER,DELAY,REVERB,GATE,DYNAMICS,EQ,STEREO,LO-FI";
    int currentPreset = 0;
    juce::String presetName = "INIT";
    juce::ValueTree abOther; bool abOnB = false;
    double freeBeat = 0, sr = 48000;
    float timeSec = 0;
    float lfoFree[4] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkyzProcessor)
};
