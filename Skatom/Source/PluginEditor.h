#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

// Hosts the web UI (web/*, embedded as BinaryData) and bridges it to the APVTS:
//   UI → host : "ready", "set" {id: value}, "gesture" {id, on}, "snaps" {snaps}, "drop", "savePreset"/"deletePreset" {name, json}
//   host → UI : "init" {params, defs, snaps, userPresets}, "params" {id: value}, "telemetry" {...}, "userPresets" {list}
class SkatomEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SkatomEditor (SkatomProcessor&);
    ~SkatomEditor() override
    {
        stopTimer();
        for (auto& id : gestures) if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture(); // never leave a drag open in the host
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff060708)); }
    void resized() override { web.setBounds (getLocalBounds().withTrimmedBottom (bannerUp ? 40 : 0)); } // the web view would cover the update banner

private:
    void timerCallback() override;
    void sendInit();
    void applyFromUi (const juce::var& v);
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    juce::var bands();
    void onMisc (const juce::String& type, const juce::var& v);
    static juce::File presetDir();
    static juce::var userPresets();

    SkatomProcessor& proc;
    juce::WebBrowserComponent web;
    bool ready = false;
    bool bannerUp = false;
    juce::StringArray gestures; // params the UI is dragging right now
    float lastSent[skatom::NUM_PARAMS] {};
    juce::dsp::FFT fft { 11 };
    std::array<float, 4096> fftBuf {};
    std::array<float, 1024> smooth {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkatomEditor)
};
