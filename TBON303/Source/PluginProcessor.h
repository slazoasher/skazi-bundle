#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "dsp/AcidVoice.h"
#include "dsp/Effects.h"
#include "dsp/PatternGenerator.h"
#include "dsp/PresetBank.h"
#include "dsp/Sequencer.h"

namespace ids
{
inline juce::String step (int i, const char* field) { return "s" + juce::String (i + 1) + "_" + field; }
}

class Skazi303Processor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    Skazi303Processor();
    ~Skazi303Processor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    // The factory bank doubles as the host's program list.
    int getNumPrograms() override { return s303::numFactoryPresets(); }
    int getCurrentProgram() override { const int p = pendingProgram.load(); return p >= 0 ? p : currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Pattern editing, called from the message thread. Writes go through the
    // parameters, so they are automatable, undoable by the host and saved with state.
    void generatePattern();
    void mutatePattern();
    void clearPattern();
    void shiftPattern (int direction);

    // Presets (message thread).
    void loadFactoryPreset (int index);
    void selectPreset (int index); // from the plugin's own UI: loads it and tells the host
    void randomizeSound();
    bool saveUserPreset (const juce::File&);
    bool loadUserPreset (const juce::File&);
    static juce::File getUserPresetFolder();
    juce::String getPresetDisplayName() const;

    s303::Pattern readPattern() const;
    void writePattern (const s303::Pattern&);

    int getCurrentStep() const { return currentStep.load(); }
    float getFilterActivity() const { return filterActivity.load(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState; // on-screen keyboard

    static const juce::StringArray lfoShapes, lfoRates, lfoDests;
    static const juce::StringArray distTypes, filter2Types, modModes, delaySyncs, seqRates, seqModes, noteNames;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam (const juce::String& id, float value);
    s303::VoiceParams readVoiceParams() const;
    s303::GenSettings readGenSettings() const;
    void handleMidi (const juce::MidiMessage&, bool sequencerActive);
    void renderChunk (float* left, float* right, int numSamples, juce::MidiBuffer& midi, int midiOffset);

    // Cached raw parameter pointers so the audio thread never looks anything up by name.
    struct RawParams
    {
        std::atomic<float> *wave, *tune, *fine, *drift, *subLevel, *subWave, *subOct, *vcaDecay, *fat;
        std::atomic<float> *bendUp, *bendDown, *mwDest, *mwAmt;
        std::atomic<float> *ampSus, *ampRel, *fltAtt, *fltSus, *fltRel;
        std::atomic<float> *fltTrack, *accDecay, *softAttack, *sweep, *pulseWidth, *noise;
        std::atomic<float> *cutoff, *reso, *envMod, *decay, *accent, *slideTime;
        std::atomic<float> *f2Type, *f2Cutoff, *f2Reso, *f2Vowel, *f2Mix;
        std::atomic<float> *preType, *preDrive, *postType, *postDrive, *distTone, *distMix;
        std::atomic<float> *modMode, *modRate, *modDepth, *modMix;
        std::atomic<float> *dlySync, *dlyFb, *dlyTone, *dlyMix, *revSize, *revDamp, *revMix, *width, *volume;
        std::atomic<float> *seqOn, *seqMode, *seqLen, *seqRate, *swing, *gateLen, *root, *intBpm;
        std::atomic<float> *genScale, *genDensity, *genAccent, *genSlide, *genOctave;
        std::atomic<float> *drive2Type, *drive2, *ottDepth, *ottTime, *turbo;
        std::atomic<float> *oscBLevel, *oscBSemi, *oscBFine, *uniVoices, *uniDetune, *fltModel;
        std::array<std::atomic<float>*, 2> lfoShape, lfoRate, lfoDest, lfoAmt;
        std::array<std::atomic<float>*, s303::kMaxSteps> note, octave, gate, acc, slide;
    } p {};

    s303::AcidVoice voice;
    s303::StepSequencer sequencer;
    s303::PatternGenerator generator;
    s303::ModFX modFx;
    s303::BBDDelay delay;
    s303::StereoWidth width;
    s303::FatStage fatStage;
    // POWER section: DRIVE 2 (at 2x, after the voice) -> OTT, level-matched. TURBO forces both up.
    s303::Distortion drive2;
    s303::OTT ott;
    s303::LevelMatch powerMatch;
    juce::LinearSmoothedValue<float> powerGain;
    bool powerWasOn = false;
    std::array<s303::Lfo, 2> lfos;
    double lfoBeat = 0.0;
    std::vector<float> lfoGain; // per-sample VOLUME modulation
    juce::Reverb reverb;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> monoBuffer;
    int maxBlock = 512;
    double sr = 44100.0;

    std::array<int, 16> heldNotes {};
    int heldCount = 0;
    int midiTranspose = 0;
    void applyWheels();
    void applyPreset (const s303::Preset&, bool soundOnly);
    void levelCurrentSound (int factoryIndex = -1);
    float measureLevelDb();
    void handleAsyncUpdate() override;

    std::atomic<int> pendingProgram { -1 };   // a host program change waiting for the message thread
    std::vector<float> factoryLevelCache;     // measured VOLUME per factory preset (NaN = not yet)
    void setPresetLabel (int index, const juce::String& name);

    int currentProgram = 0;
    int lastSeqMode = -1;
    bool lastSeqActive = false;
    double internalPpq = 0.0;
    juce::LinearSmoothedValue<float> outGain;

public:
    std::atomic<float> wheelBend { 0.0f }, wheelMod { 0.0f }; // -1..1 and 0..1, from MIDI or the on-screen wheels
private:
    std::atomic<int> currentStep { -1 };
    std::atomic<float> filterActivity { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Skazi303Processor)
};
