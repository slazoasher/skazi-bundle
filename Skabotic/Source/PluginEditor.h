#pragma once

#include "PluginProcessor.h"

namespace Skin
{
    const juce::Colour bg        { 0xff07090d };
    const juce::Colour steelHi   { 0xffc9d2dc };
    const juce::Colour steel     { 0xff7d8894 };
    const juce::Colour steelLo   { 0xff2c333b };
    const juce::Colour cyan      { 0xff19e6ff };
    const juce::Colour red       { 0xffff2b3d };
    const juce::Colour amber     { 0xffffb020 };
    const juce::Colour magenta   { 0xffd34dff };
    const juce::Colour lcd       { 0xff8dff5a };
    const juce::Colour text      { 0xffdfe8f2 };
    const juce::Colour dim       { 0xff6b7785 };
}

class RobotLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RobotLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

// Knob with its name stamped above it and the value readout below.
struct RobotKnob : public juce::Component
{
    RobotKnob (const juce::String& name, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;
    juce::String name;
    juce::Colour accent;
};

// The robot head: LED eyes follow the voice, the mouth grille shows the vocoder bands.
class RobotFace : public juce::Component
{
public:
    explicit RobotFace (SkaboticAudioProcessor& p) : proc (p) { setOpaque (true); }
    void paint (juce::Graphics&) override;
    void tick();

private:
    SkaboticAudioProcessor& proc;
    std::array<float, bot::Vocoder::numBands> bands {};
    float level = 0.0f, blink = 0.0f, time = 0.0f;
};

class SkaboticAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SkaboticAudioProcessorEditor (SkaboticAudioProcessor&);
    ~SkaboticAudioProcessorEditor() override;
    void resized() override;

    static constexpr int designWidth = 1140, designHeight = 900;

private:
    struct Canvas : public juce::Component
    {
        juce::Image logo;
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override;
    void attach (RobotKnob&, const char* id);
    void syncPresetBox();

    SkaboticAudioProcessor& proc;
    RobotLookAndFeel lnf;
    Canvas canvas;
    RobotFace face { proc };

    RobotKnob vocode { "VOCODE", Skin::cyan }, note { "NOTE", Skin::cyan }, octave { "OCTAVE", Skin::cyan },
              formant { "FORMANT", Skin::cyan }, react { "REACT", Skin::cyan }, hiss { "HISS", Skin::cyan },
              pitch { "PITCH", Skin::red }, drive { "DRIVE", Skin::red }, ring { "RING MOD", Skin::red },
              ringFreq { "RING FREQ", Skin::red }, bits { "BITS", Skin::amber }, crush { "CRUSH", Skin::amber },
              metal { "METAL", Skin::amber }, metalFreq { "METAL FREQ", Skin::amber },
              gate { "GATE", Skin::lcd }, mix { "MIX", Skin::lcd }, output { "OUTPUT", Skin::lcd },
              glide { "GLIDE", Skin::cyan },
              shift { "FREQ SHIFT", Skin::magenta }, army { "ROBOT ARMY", Skin::magenta },
              stutter { "STUTTER", Skin::magenta }, stutSize { "STUT SIZE", Skin::magenta },
              chop { "CHOP", Skin::magenta }, chopRate { "CHOP RATE", Skin::magenta }, width { "WIDTH", Skin::magenta };

    juce::ComboBox carrierBox, sourceBox, keyBox, scaleBox, presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, randomButton { "RANDOM" };

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> carrierAttachment, sourceAttachment, keyAttachment, scaleAttachment;
    int shownPreset = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkaboticAudioProcessorEditor)
};
