#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"
#include "FxRack.h"
#include "Presets.h"
#include "SoundMatch.h"

class SkazonAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numVoices = 16;

    SkazonAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SKAZON"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::StringArray presetNames();
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    // MACRO LINKS: MACRO 1-4 drive any knob directly (works on every parameter, not only matrix targets).
    // The knob's normalised value moves by depth * macro. Saved with the project; a factory preset clears them.
    struct MacroLink { juce::String id; juce::RangedAudioParameter* param; int macro; float depth; };
    std::vector<MacroLink> macroLinks() const { const auto l = std::atomic_load (&links); return l != nullptr ? *l : std::vector<MacroLink>(); }
    void setMacroLink (int macro, const juce::String& id, float depth); // depth 0 removes the link; message thread only
    void clearMacroLinks();

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    // Random preset in a generated category (-1 = the current preset's category, else any).
    void randomize (int category = -1);

    // Audio file -> USER wavetable (16 frames). Sets the given oscillator ("a", "b" or "g") to USER.
    bool loadUserTable (const juce::File&, const juce::String& oscPrefix);

    // SOUND MATCH: analyse one recorded note and rebuild it as a patch (USER table + envelopes + vibrato).
    skazon::MatchResult soundMatch (const juce::File&);

    // MUTATE: nudge the continuous knobs by amount (0..1), leaving locked groups alone.
    enum LockGroup { lockOsc = 1, lockFilter = 2, lockEnv = 4, lockMod = 8, lockFx = 16 };
    void mutate (float amount, int locks);
    int mutateLocks = 0;

    // The last SOUND MATCH as MIDI: played back through the patch (MIDI button) and dragged out as a .mid.
    std::vector<skazon::TrackedNote> matchedNotes() const { const juce::SpinLock::ScopedLockType l (previewLock); return previewNotes; }
    void setPreview (bool on) { previewOn = on && ! matchedNotes().empty(); }
    bool isPreviewing() const { return previewOn; }
    juce::File writeMatchMidi() const; // temp .mid at the host tempo, empty File if there are no notes
    std::atomic<double> hostBpm { 120.0 };

    // On-screen wheels: the editor posts moves (-1 = none pending), the audio thread reports what is in effect.
    std::atomic<int> uiBend { -1 }, uiModWheel { -1 };
    std::atomic<int> shownBend { 8192 }, shownModWheel { 0 };
    juce::String userTableName() const;
    std::shared_ptr<const skazon::UserTable> currentUserTable() const { return std::atomic_load (&userTable); }
    int uiPage = 0; // editor page, kept while the window is closed
    static bool isAudioFile (const juce::String& path);
    static bool readAudio (const juce::File&, juce::AudioBuffer<float>&, double& sampleRate);
    juce::String randomName; // set while a random preset is loaded (message thread only)

    // Live values of the newest sounding voice, for the displays. -1 positions when silent.
    std::array<std::atomic<float>, 3> livePos {};
    std::array<std::atomic<float>, skazon::numTargets> liveMods {};
    std::array<std::atomic<float>, skazon::numLfos> liveLfo {};
    std::atomic<int> activeVoices { 0 };
    skazon::FxRack fx;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void fillSnapshot (double bpm);
    void renderMono (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void applyValues (const std::vector<std::pair<juce::String, float>>&, float trimDb);
    void storeUserTable (std::shared_ptr<const skazon::UserTable>, const juce::File&, const juce::String& mode);

    skazon::Snapshot snap;
    int lastNote = -1;
    int currentPreset = 0;
    int bendRaw = 8192;
    struct Synth : juce::Synthesiser { using juce::Synthesiser::handleMidiEvent; }; // mono mode feeds events one by one
    Synth synth;

    juce::Array<int> held; // mono key stack
    skazon::Voice* monoVoice = nullptr;

    std::shared_ptr<const skazon::UserTable> userTable, previousUserTable; // swapped with std::atomic_store
    std::shared_ptr<const std::vector<MacroLink>> links, previousLinks;      // same pattern: previous keeps the audio thread's copy alive
    std::shared_ptr<const std::vector<MacroLink>> blockLinks;                // this block's links, read by played()
    float blockMacro[skazon::numMacros] {};
    float played (const juce::String& id) const; // param() plus macro links
    void storeLinks (std::vector<MacroLink>);
    std::shared_ptr<const skazon::UserTable> audioUserTable;              // the audio thread's hold for this block

    void addPreviewEvents (juce::MidiBuffer&, int numSamples);
    mutable juce::SpinLock previewLock;
    std::vector<skazon::TrackedNote> previewNotes;
    juce::String previewName;
    std::atomic<bool> previewOn { false };
    bool previewWasOn = false;
    int previewSounding = -1; // the preview's note that is on now: only it is released (keys you play stay)
    double previewTime = 0; // seconds into the line (audio thread)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkazonAudioProcessor)
};
