#pragma once
#include "PluginProcessor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// "Spaceship" skin: brushed-metal hull plates, chrome/bakelite vintage knobs, jewel lamps, amber LCDs, green CRT.
namespace theme
{
    inline const juce::Colour hull       { 0xff2b2e33 };
    inline const juce::Colour metal      { 0xff9aa0a8 };
    inline const juce::Colour ink        { 0xff15171a };   // engraved text on metal
    inline const juce::Colour cream      { 0xfff3ead2 };
    inline const juce::Colour lcd        { 0xff070908 };
    inline const juce::Colour lcdText    { 0xffffb347 };   // amber readouts
    inline const juce::Colour phosphor   { 0xff6dff9a };   // CRT green
    inline const juce::Colour filter     { 0xffff7a1a };
    inline const juce::Colour lfo        { 0xff6dff9a };
    inline const juce::Colour master     { 0xfffff1c9 };
    inline const juce::Colour dist       { 0xffff3b30 };
    inline const juce::Colour glitch     { 0xffff4fd8 };
    inline const juce::Colour chorus     { 0xffb28cff };
    inline const juce::Colour flanger    { 0xff59a6ff };
    inline const juce::Colour delay      { 0xff7dff6a };
    inline const juce::Colour reverb     { 0xff58e6ff };
    inline const juce::Colour inflate    { 0xffffc23a };
}

class GlitchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlitchLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

class Knob : public juce::Component
{
public:
    Knob (APVTS&, const juce::String& paramId, const juce::String& text, juce::Colour accent, bool big);
    void resized() override;

    juce::Slider slider;

private:
    juce::Label label;
    APVTS::SliderAttachment attachment;
    bool isBig;
};

class ChoiceItem : public juce::Component
{
public:
    ChoiceItem (APVTS&, const juce::String& paramId, const juce::String& text);
    void resized() override;

private:
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

class ToggleItem : public juce::Component
{
public:
    ToggleItem (APVTS&, const juce::String& paramId, const juce::String& text, juce::Colour accent);
    void resized() override { button.setBounds (getLocalBounds().reduced (0, 4)); }

private:
    juce::ToggleButton button;
    APVTS::ButtonAttachment attachment;
};

// A riveted metal plate with an engraved title, an optional power lamp, an optional big knob and a row of small controls.
class ModulePanel : public juce::Component
{
public:
    ModulePanel (APVTS&, juce::String title, juce::Colour accent, const char* powerParam = nullptr);

    ModulePanel& big (const char* id, const juce::String& text);
    ModulePanel& knob (const char* id, const juce::String& text);
    ModulePanel& choice (const char* id, const juce::String& text);
    ModulePanel& toggle (const char* id, const juce::String& text);
    void setExtra (juce::Component* comp, int height);
    // Greys out a control that does nothing in the current setting (it stays usable).
    void setDimmed (const char* paramId, bool dimmed);
    Knob* getBig() const noexcept { return bigKnob.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Item { std::unique_ptr<juce::Component> comp; int w, h; juce::String id; };
    void add (std::unique_ptr<juce::Component> comp, int w, int h, const juce::String& id);

    APVTS& state;
    juce::String title;
    juce::Colour accent;
    std::vector<Item> items;
    std::unique_ptr<Knob> bigKnob;
    std::unique_ptr<juce::ToggleButton> power;
    std::unique_ptr<APVTS::ButtonAttachment> powerAttachment;
    juce::Component* extra = nullptr;
    int extraHeight = 0;
    juce::Image texture;
};

class LfoDisplay : public juce::Component
{
public:
    explicit LfoDisplay (GlitchMagmaProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    GlitchMagmaProcessor& proc;
};

class GlitchMagmaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GlitchMagmaEditor (GlitchMagmaProcessor&);
    ~GlitchMagmaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetBox();
    void stepPreset (int delta);

    GlitchMagmaProcessor& proc;
    GlitchLookAndFeel lnf;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, randomButton { "RANDOM" };
    juce::ToggleButton powerButton;                       // lit = plugin ON; drives the bypass parameter
    std::unique_ptr<juce::ParameterAttachment> powerAttachment;
    juce::Rectangle<int> titleArea;
    juce::Image hullTexture;

    ModulePanel lfoPanel, filterPanel, masterPanel;
    ModulePanel distPanel, glitchPanel, chorusPanel, flangerPanel;
    ModulePanel delayPanel, reverbPanel, inflatePanel;
    LfoDisplay lfoDisplay;
    int lastPresetVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchMagmaEditor)
};
