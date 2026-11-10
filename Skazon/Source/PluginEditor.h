#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

// Serum-style 3D stack of a table's frames, with the knob position (colour, warp applied) and the
// live modulated position (white). Drop an audio file on it to make a USER table.
class WaveDisplay : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    WaveDisplay (SkazonAudioProcessor&, const juce::String& prefix, int liveIndex, juce::Colour accent);
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    juce::Path framePath (const float* base, float pos, juce::Rectangle<float> area, float depth, int warp, float warpAmt) const;
    void load (const juce::File&);

    SkazonAudioProcessor& proc;
    juce::String prefix;
    int live;
    juce::Colour accent;
    juce::int64 shown = -1;
    bool dropHover = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

// LFO shape with the newest voice's phase running over it.
class LfoDisplay : public juce::Component
{
public:
    LfoDisplay (SkazonAudioProcessor&, int index);
    void tick();
    void paint (juce::Graphics&) override;

private:
    SkazonAudioProcessor& proc;
    int index;
    juce::int64 shown = -1;
};

// Draws the distortion transfer curve (DIST) or the OTT band gains (OTT).
class FxScope : public juce::Component
{
public:
    enum Mode { distortion, ott };
    FxScope (SkazonAudioProcessor&, Mode);
    void tick();
    void paint (juce::Graphics&) override;

private:
    SkazonAudioProcessor& proc;
    Mode mode;
    juce::int64 shown = -1;
};

// A modulation source you drag onto any knob.
class ModChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModChip (int source, const juce::String& label, juce::Colour);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    int source;
    juce::String label;
    juce::Colour accent;
};

// A knob that accepts dropped modulation sources, shows its routings as a depth arc, removes them on
// right-click and changes their depth on shift-drag.
class TargetKnob : public NeonKnob, public juce::DragAndDropTarget
{
public:
    TargetKnob (const juce::String& label, juce::Colour, bool bipolar, int target);

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    const int target;
    juce::String paramID;
    std::function<void (int source)> onDrop;
    std::function<void()> onRemoveMenu;
    std::function<void (float delta)> onDepthDrag; // shift-drag
    juce::int64 shown = -1;

private:
    bool depthDragging = false;
    float lastY = 0;
};

class SkazonAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer,
                                   public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit SkazonAudioProcessorEditor (SkazonAudioProcessor&);
    ~SkazonAudioProcessorEditor() override;

    void resized() override;

    // Audio dropped anywhere except a wave display = SOUND MATCH.
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropOverlay.setVisible (true); }
    void fileDragExit (const juce::StringArray&) override { dropOverlay.setVisible (false); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    static constexpr int designWidth = 1240, designHeight = 880, numPages = 3;

    struct Canvas : public juce::Component
    {
        juce::Image background[numPages];
        int page = 0;
        void paint (juce::Graphics& g) override { g.drawImage (background[page], getLocalBounds().toFloat()); }
    };

    void timerCallback() override;
    void renderBackground();
    void showPage (int);
    void runMatch (const juce::File&);
    void showMutateMenu();
    void buildOscPage();
    void buildModPage();
    void buildFxPage();

    // Each creates a control bound to a parameter, placed in design coordinates on the page being built.
    TargetKnob& knob (const juce::String& id, const juce::String& label, juce::Colour, juce::Rectangle<int>, bool bipolar = false);
    void knobRow (juce::Rectangle<int> area, std::initializer_list<std::tuple<const char*, const char*, bool>> items, juce::Colour);
    void combo (const juce::String& id, juce::Rectangle<int>);
    void toggle (const juce::String& id, const juce::String& text, juce::Colour, juce::Rectangle<int>);
    void amountBar (const juce::String& id, juce::Rectangle<int>);
    void label (const juce::String& text, juce::Rectangle<int>, juce::Colour);
    void panel (juce::Rectangle<int>, const juce::String& index, const juce::String& title, juce::Colour);
    template <typename T> T& place (std::unique_ptr<T>, juce::Rectangle<int>);

    // Modulation routing through the matrix parameters.
    void addRoute (int source, int target);
    void removeRoutesMenu (const juce::String& paramID, int target);
    void adjustDepth (const juce::String& paramID, int target, float delta);
    void dropSource (const juce::String& paramID, int target, int source);
    juce::Range<float> macroRange (const juce::String& paramID) const;
    juce::Range<float> routeRange (const juce::String& paramID, int target) const;
    float liveValue (const juce::String& paramID, int target) const;
    float shiftNorm (const juce::String& paramID, int target, float base, float mod) const;

    SkazonAudioProcessor& proc;
    NeonLookAndFeel lookAndFeel;
    Canvas canvas;
    float renderedScale = 0.0f;
    juce::Image logo;
    int building = -1; // page the helpers put controls on; -1 = every page

    juce::ComboBox presetBox;
    NeonButton prevPreset { "<", Theme::lime }, nextPreset { ">", Theme::lime };
    NeonButton dice { "", Theme::lime, true }, diceMenu { "v", Theme::lime };
    NeonButton matchButton { "SOUND MATCH", Theme::magenta }, mutateButton { "MUTATE", Theme::amber };

    // Click = hear the matched line on this patch; drag = the .mid into the DAW.
    struct MidiButton : public NeonButton
    {
        MidiButton() : NeonButton ("MIDI", Theme::cyan) {}
        std::function<void()> onDragOut;
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! draggingOut && isEnabled() && e.getDistanceFromDragStart() > 6 && onDragOut != nullptr)
            {
                draggingOut = true;
                onDragOut();
            }
            else if (! draggingOut)
                NeonButton::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! draggingOut)
                return NeonButton::mouseUp (e);
            draggingOut = false;
            setState (buttonNormal);
        }
        bool draggingOut = false;
    } midiButton;
    std::unique_ptr<juce::FileChooser> matchChooser;
    float mutateAmount = 0.35f;

    struct DropOverlay : public juce::Component
    {
        DropOverlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    } dropOverlay;
    std::unique_ptr<NeonToggle> tabs[numPages];
    juce::MidiKeyboardComponent keyboard;

    // PITCH (springs back to the middle) and MOD wheels; drag up/down. They also follow the host's wheels.
    struct Wheel : public juce::Component, public juce::SettableTooltipClient
    {
        Wheel (bool bipolar, juce::String name, juce::Colour colour) : pitch (bipolar), text (std::move (name)), accent (colour) {}
        bool pitch;
        juce::String text;
        juce::Colour accent;
        float value = 0;                 // pitch -1..1, mod 0..1
        std::function<void (float)> onMove;
        std::function<juce::String (float)> readout; // text shown while it is off its rest position
        void paint (juce::Graphics&) override;
        // Relative drag: 160 px (design units) = the whole range, wherever you grab it. Double-click = rest.
        void mouseDown (const juce::MouseEvent&) override { grab = value; }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float range = pitch ? 2.0f : 1.0f, fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
            set (juce::jlimit (pitch ? -1.0f : 0.0f, 1.0f, grab - (float) e.getDistanceFromDragStartY() / 160.0f * range * fine));
        }
        void mouseUp (const juce::MouseEvent&) override { if (pitch) set (0.0f); } // the pitch wheel springs back
        void mouseDoubleClick (const juce::MouseEvent&) override { set (0.0f); }
        void set (float v) { if (v != value) { value = v; repaint(); if (onMove) onMove (v); } }
        void show (float v) { if (! isMouseButtonDown() && std::abs (v - value) > 0.004f) { value = v; repaint(); } }
        float grab = 0;
    } pitchWheel { true, "PITCH", Theme::cyan }, modWheel { false, "MOD", Theme::magenta };
    juce::TooltipWindow tooltips { this, 700 };

    std::vector<std::unique_ptr<juce::Component>> controls;
    std::vector<juce::Component*> pageComponents[numPages];
    std::vector<WaveDisplay*> waves;
    std::vector<LfoDisplay*> lfoDisplays;
    std::vector<FxScope*> scopes;
    std::vector<std::pair<TargetKnob*, juce::String>> targetKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    struct Panel { int page; juce::Rectangle<int> bounds; juce::String index, title; juce::Colour accent; };
    struct Text { int page; juce::String text; juce::Rectangle<int> bounds; juce::Colour colour; };
    std::vector<Panel> panels;
    std::vector<Text> texts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkazonAudioProcessorEditor)
};
