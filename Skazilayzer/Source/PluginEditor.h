#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

// Hosts web/index.html and feeds it at 30 Hz:
//   host → UI : "init" {ui}, "tele" {sr, fft, spec (b64 int16 dB*100), g (b64 int16 L/R pairs),
//               pL pR sL sR sLR aL aR n mn mx, M S I LRA, lb [[m,s]...]}
//   UI → host : "ready", "state" {json}, "resetLoud", "fft" {size}
class SkzlEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SkzlEditor (SkzlProcessor&);
    ~SkzlEditor() override { stopTimer(); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff06070b)); }
    void resized() override { web.setBounds (getLocalBounds().withTrimmedBottom (bannerUp ? 40 : 0)); } // the web view would cover the update banner

private:
    static constexpr int GONIO = 1024;
    void setFftOrder (int order); // 12..15 (4K..32K), chosen in the UI

    void timerCallback() override;
    void sendInit();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    SkzlProcessor& proc;
    juce::WebBrowserComponent web;
    bool ready = false;
    bool bannerUp = false;
    uint64_t lastRead = 0;
    int N = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuf;
    std::vector<int16_t> spec;
    std::array<int16_t, GONIO * 2> gonio {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkzlEditor)
};
