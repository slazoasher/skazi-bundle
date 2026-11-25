#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "RobotDSP.h"

namespace ParamIDs
{
    inline constexpr auto vocode    = "VOCODE";
    inline constexpr auto carrier   = "CARRIER";
    inline constexpr auto source    = "SOURCE";
    inline constexpr auto note      = "NOTE";
    inline constexpr auto octave    = "OCTAVE";
    inline constexpr auto formant   = "FORMANT";
    inline constexpr auto react     = "REACT";
    inline constexpr auto hiss      = "HISS";
    inline constexpr auto pitch     = "PITCH";
    inline constexpr auto ring      = "RING";
    inline constexpr auto ringFreq  = "RING_FREQ";
    inline constexpr auto bits      = "BITS";
    inline constexpr auto crush     = "CRUSH";
    inline constexpr auto metal     = "METAL";
    inline constexpr auto metalFreq = "METAL_FREQ";
    inline constexpr auto drive     = "DRIVE";
    inline constexpr auto gate      = "GATE";
    inline constexpr auto mix       = "MIX";
    inline constexpr auto output    = "OUTPUT";
    inline constexpr auto glide     = "GLIDE";
    inline constexpr auto key       = "KEY";
    inline constexpr auto scale     = "SCALE";
    inline constexpr auto shift     = "SHIFT";
    inline constexpr auto stutter   = "STUTTER";
    inline constexpr auto stutSize  = "STUT_SIZE";
    inline constexpr auto chop      = "CHOP";
    inline constexpr auto chopRate  = "CHOP_RATE";
    inline constexpr auto army      = "ARMY";
    inline constexpr auto width     = "WIDTH";
}

class SkaboticAudioProcessor : public juce::AudioProcessor
{
public:
    enum Source { srcNote, srcTrack, srcAutoTune, srcMidiPoly, srcMidiMono };

    SkaboticAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    // Rolls every sound control; MIX, OUTPUT and GATE stay put.
    void randomise();
    std::atomic<bool> randomised { false };

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // What the robot face draws.
    std::array<std::atomic<float>, bot::Vocoder::numBands> bandLevel {};
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f }, voiceHz { 0.0f }, carrierHz { 0.0f };
    std::atomic<bool> gateOpen { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    double sampleRate = 44100.0;
    bot::Vocoder vocoder;
    bot::Carrier carrierOsc;
    bot::PitchTracker tracker;
    bot::PitchShifter shifter;
    bot::Comb comb[2];
    bot::Hilbert hilbert;
    bot::Army armyFx;
    std::vector<float> widthBuf; // mid history for the mono-safe side above 100% width
    int widthPos = 0, widthTapA = 1, widthTapB = 2;
    float widthLow = 0.0f;
    juce::AudioBuffer<float> stutterBuf; // last live slice, replayed when a stutter fires

    juce::SmoothedValue<float> widthAmt, shiftHz, armyAmt, vocodeAmt, ringAmt, metalAmt, driveAmt, mixAmt, outGain, shiftRatio, metalDelay, ringHz;
    float carrierFreq = 110.0f, ringPhase = 0.0f, holdSample = 0.0f, holdCount = 0.0f;
    float crushEnv = 0.0f, inEnv = 0.0f, gateGain = 0.0f, dryPow = 0.0f, wetPow = 0.0f, autoGain = 1.0f;
    std::array<juce::uint32, 128> heldNotes {}; // 0 = up, otherwise the order it was pressed (mono plays the newest)
    juce::uint32 noteCounter = 0;
    double beatPos = 0.0, shiftPhase = 0.0;
    juce::int64 lastSlice = -1;
    bool repeating = false;
    float chopGain = 1.0f;
    juce::AudioBuffer<float> dryBuffer;

    int currentPreset = 0;
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkaboticAudioProcessor)
};
