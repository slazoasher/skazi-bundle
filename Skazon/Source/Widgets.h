#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

// Rotary knob drawn as a segmented HUD ring with a glowing value arc.
class NeonKnob : public juce::Slider
{
public:
    NeonKnob (const juce::String& labelText, juce::Colour accentColour, bool isBipolar = false);

    // Small knobs hide the centre readout until hovered.
    void setCompact (bool shouldBeCompact) { compact = shouldBeCompact; }

    // Normalised position of the live (modulated) value, drawn as a second marker. Unset or < 0 means none.
    std::function<float()> modulation;
    // Normalised range the modulation can reach, drawn as an outer depth arc. Empty means none.
    std::function<juce::Range<float>()> modRange;
    bool dropHover = false; // a modulation source is being dragged over this knob

    void paint (juce::Graphics&) override;

private:
    juce::String label;
    juce::Colour accent;
    bool bipolar;
    bool compact = false;
};

// Row of buttons bound to a parameter. Each button sets the value to its index; on a continuous
// parameter (like Morph) the two neighbouring buttons share the highlight.
class SegmentedSelector : public juce::Component
{
public:
    using IconPainter = std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)>;

    SegmentedSelector (juce::RangedAudioParameter& parameter, juce::StringArray labels, juce::Colour accentColour);

    std::vector<IconPainter> icons;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int indexAt (juce::Point<float>) const;

    juce::StringArray labels;
    juce::Colour accent;
    float value = 0.0f;
    int hovered = -1;
    juce::ParameterAttachment attachment;
};

class NeonToggle : public juce::ToggleButton
{
public:
    NeonToggle (const juce::String& text, juce::Colour accentColour);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour accent;
};

// Momentary push button. Draws a die instead of a plain label when dice is set.
class NeonButton : public juce::Button
{
public:
    NeonButton (const juce::String& text, juce::Colour accentColour, bool drawDice = false);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    float flash = 0.0f; // set to 1 to light the button up; the owner fades it out
    bool filled = false; // big solid-colour action button
    juce::String subText; // small second line under the label (filled buttons)

private:
    juce::Colour accent;
    bool dice;
};

class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};
