#pragma once

#include "PluginProcessor.h"
#include "Skin.h"
#include "StepGrid.h"

/** Rotary knob with its label above it, bound to one parameter. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text,
          bool onDeck, bool bipolar = false);
    void resized() override;
    void showValue(); // keep the current value visible under the knob

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** LCD-style selector with its label above it. */
class Selector : public juce::Component
{
public:
    Selector (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text, bool onDeck);
    void resized() override;

private:
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** LCD showing the current preset; click it for the full browser menu. */
class PresetDisplay : public juce::Component
{
public:
    std::function<void()> onClick;
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    juce::String text;
};

/** A pitch or mod wheel: a ribbed wheel in a slot with a bright marker line. */
class Wheel : public juce::Slider
{
public:
    explicit Wheel (juce::String name) : label (std::move (name)) {}
    void paint (juce::Graphics&) override;

private:
    juce::String label;
};

/** The whole front panel, laid out at a fixed design size. */
class SynthPanel : public juce::Component, private juce::Timer
{
public:
    explicit SynthPanel (Skazi303Processor&);
    ~SynthPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
        int tab; // 0 = synth, 1 = mods, 2 = fx, 3 = sequencer
    };

    void timerCallback() override;
    Knob& knob (const juce::String& id, const juce::String& text, bool deck, bool bipolar = false);
    Knob& knobAs (const juce::String& key, const juce::String& id, const juce::String& text); // a second knob for a parameter shown elsewhere too
    Selector& selector (const juce::String& id, const juce::String& text, bool deck);
    void placeRow (juce::Rectangle<int> area, const juce::StringArray& ids, int itemWidth);
    void buildTexture();
    void showTab (int tab);

    Skazi303Processor& processor;
    skin::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    std::map<juce::String, std::unique_ptr<juce::Component>> controls;
    StepGrid grid;
    PresetDisplay presetDisplay;
    juce::TextButton presetPrevBtn { "<" }, presetNextBtn { ">" }, randomBtn { "RANDOM" }, saveBtn { "SAVE" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    void showPresetMenu();
    void stepPreset (int delta);

    juce::TextButton synthTabBtn { "SYNTH" }, modsTabBtn { "ENV" }, fxTabBtn { "FX" }, seqTabBtn { "SEQ" };
    std::vector<juce::Component*> synthItems, modsItems, fxItems, seqItems;
    int currentTab = 0;

    juce::TextButton seqOnBtn { "SEQ ON" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqOnAttachment;
    juce::TextButton turboBtn { "TURBO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> turboAttachment;
    juce::MidiKeyboardComponent keyboard;
    Wheel pitchWheel { "PITCH" }, modWheel { "MOD" }; // on-screen wheels; the pitch wheel springs back to the middle
    juce::Rectangle<int> wheelFrame;
    juce::TextButton generateBtn { "GENERATE" }, mutateBtn { "MUTATE" }, clearBtn { "CLEAR" },
                     shiftLeftBtn { "<" }, shiftRightBtn { ">" };

    std::vector<Section> sections;
    juce::Image background, sideLeft, sideRight;
    juce::Rectangle<int> ledArea, keyboardFrame;
    float shownActivity = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};

/** Hosts the panel and scales it, so the window can be resized to fit any screen. */
class Skazi303Editor : public juce::AudioProcessorEditor
{
public:
    explicit Skazi303Editor (Skazi303Processor&);
    void resized() override;

private:
    SynthPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Skazi303Editor)
};
