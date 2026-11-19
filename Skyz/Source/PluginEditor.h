#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// Hosts web/index.html (embedded as BinaryData) in WebView2 and bridges it to the processor:
//   UI -> host : ready, set {id: norm}, gesture {id, on}, preset {index}, random, mutate, save {name}, ab,
//                note {n, on, vel}, wheel {which, v}, lanes {data}, chain {order}, dragMidi
//   host -> UI : init {params, lanes, chain, presets, preset, presetName}, params {id: norm}, lanes {data},
//                preset {index, name, list}, telemetry {step, l, r, lfo[], voices, ppq, wheelP, wheelM}
class SkyzEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SkyzEditor (SkyzProcessor&);
    ~SkyzEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff04060e)); }
    // The web view is a native window that would cover normal components, so when the bundle's update banner shows,
    // the view gives up a strip at the bottom for it.
    void resized() override { web.setBounds (getLocalBounds().withTrimmedBottom (bannerUp ? 40 : 0)); }

private:
    void timerCallback() override;
    void sendInit();
    void sendPreset();
    void onEvent (const juce::String& type, const juce::var& v);
    std::optional<juce::WebBrowserComponent::Resource> resource (const juce::String& url);

    SkyzProcessor& proc;
    juce::WebBrowserComponent web;
    bool ready = false;
    juce::StringArray gestures;
    float lastSent[skyz::NUM_PARAMS] {};
    juce::String lastName;
    bool bannerUp = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkyzEditor)
};
