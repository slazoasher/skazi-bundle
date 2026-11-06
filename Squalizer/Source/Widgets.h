#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

// Machined aluminium knob on a printed scale: legend above, value readout below.
class Knob : public juce::Slider
{
public:
    explicit Knob (const juce::String& labelText, bool isBipolar = false);

    // Printed at the two ends of the scale, e.g. "0.1" and "30".
    void setScaleLabels (const juce::String& minText, const juce::String& maxText) { scaleMin = minText; scaleMax = maxText; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Pixels for a full sweep; big knobs get a longer throw.
    int baseSensitivity = 300;
    juce::Colour accent { Theme::amber };   // value ring colour

private:
    // Click the value readout to type an exact value ("3.2k", "-1.5"); Enter applies, Esc or clicking away cancels.
    void showTextEntry();
    void closeTextEntry (bool apply);
    std::unique_ptr<juce::TextEditor> entry;

    juce::String label, scaleMin, scaleMax;
    bool bipolar;
};

// Row of push buttons with an LED above each; picks one value of a choice parameter.
class LedSelector : public juce::Component
{
public:
    // vertical stacks the buttons with the LED to the left of each one.
    LedSelector (juce::RangedAudioParameter& parameter, juce::StringArray labels, bool vertical = false);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int indexAt (juce::Point<float>) const;

    juce::StringArray labels;
    bool vertical;
    float value = 0.0f;
    int hovered = -1, pressed = -1;
    juce::ParameterAttachment attachment;
};

// Latching push button with an LED in the cap.
class LedToggle : public juce::ToggleButton
{
public:
    LedToggle (const juce::String& text, juce::Colour ledColour);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour ledColour;
};

// Momentary push button. flash lights its edge briefly after a click; the owner fades it out.
class PanelButton : public juce::Button
{
public:
    explicit PanelButton (const juce::String& text);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    float flash = 0.0f;
};

class PanelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};
