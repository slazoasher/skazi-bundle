#pragma once

#include "PluginProcessor.h"
#include "EqDisplay.h"
#include "BandStrip.h"
#include "Widgets.h"

class SqualizerAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SqualizerAudioProcessorEditor (SqualizerAudioProcessor&);
    ~SqualizerAudioProcessorEditor() override;

    void resized() override;

private:
    // Everything is laid out once at this size; the canvas is scaled to fit the window.
    static constexpr int designWidth = 1200, designHeight = 1125;

    struct Canvas : public juce::Component
    {
        juce::Image background;
        void paint (juce::Graphics& g) override { g.drawImage (background, getLocalBounds().toFloat()); }
    };

    void timerCallback() override;
    void attach (Knob&, const juce::String& paramID, const juce::String& scaleMin, const juce::String& scaleMax, juce::Colour accent);
    void selectBand (int band);
    void layout();
    void renderBackground();
    juce::Image renderFaceTexture() const;
    void stepPreset (int delta);

    SqualizerAudioProcessor& proc;
    PanelLookAndFeel lookAndFeel;
    Canvas canvas;
    float renderedScale = 0.0f;
    juce::Image faceTexture;

    HeaderView header;
    EqDisplay display;
    ScopePanel scope;
    LedSelector phaseSelector;
    std::array<std::unique_ptr<BandStrip>, sq::numBands> strips;
    int selectedBand = -1;

    Knob output { "Output", true };
    Knob gainScale { "Scale" };

    // Toolbar: edit history, A/B compare, auto gain and the monitor section.
    PanelButton undoButton { "UNDO" }, redoButton { "REDO" }, copyButton { "COPY A>B" };
    LedToggle slotA { "A", Theme::amber }, slotB { "B", Theme::amber };
    LedToggle autoGain { "AUTO GAIN", Theme::tuner };
    LedSelector monitorMode;
    LedToggle polarityL { juce::String::charToString ((juce::juce_wchar) 0x00d8) + " L", Theme::ledRed };   // slashed O = polarity
    LedToggle polarityR { juce::String::charToString ((juce::juce_wchar) 0x00d8) + " R", Theme::ledRed };
    LedToggle swapLR { "SWAP", Theme::ledRed };
    // Red frame behind the monitor buttons that blinks while anything there is changing what you hear.
    struct MonitorWarning : public juce::Component
    {
        float level = 0.0f;
        void paint (juce::Graphics&) override;
    } monitorWarning;
    int blink = 0;
    LedToggle refCapture { "REF CAPTURE", Theme::ledRed };
    PanelButton matchButton { "MATCH" };

    LedToggle tubeIn { "IN", Theme::ledRed };
    Knob lowFreq   { "Low Freq" };
    Knob lowBoost  { "Boost" };
    Knob lowAtten  { "Atten" };
    Knob highFreq  { "High Freq" };
    Knob bandwidth { "Bandwidth" };
    Knob highBoost { "Boost" };
    Knob attenSel  { "Atten Sel" };
    Knob highAtten { "Atten" };
    Knob drive     { "Drive" };

    juce::ComboBox presetBox;
    PanelButton prevPreset { "<" }, nextPreset { ">" };

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    struct Section
    {
        juce::Rectangle<int> bounds;
        juce::String title;
    };
    std::vector<Section> sections;
    const juce::Rectangle<int> displayBounds { 60, 144, 800, 364 }, scopeBounds { 898, 144, 248, 334 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SqualizerAudioProcessorEditor)
};
