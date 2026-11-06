#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace BandColours
{
    // One colour per band, shared by its node on the screen and its knob rings.
    inline juce::Colour of (int band)
    {
        static const juce::Colour colours[sq::numBands] { juce::Colour (0xffff6ec7), juce::Colour (0xffff5e57), juce::Colour (0xffffb340),
                                                          juce::Colour (0xff3ee886), juce::Colour (0xff4cc4ff), juce::Colour (0xffa58cff) };
        return colours[juce::jlimit (0, sq::numBands - 1, band)];
    }
}

// One band's channel-strip column: power + name, type (or slope for the cuts), channel, and its knobs.
// The two cut bands have FREQ and Q (resonance) only; the four EQ bands add GAIN and the dynamic RANGE / THRESHOLD.
class BandStrip : public juce::Component
{
public:
    BandStrip (SqualizerAudioProcessor&, int band);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();   // dims what the band's current type can't use

    bool selected = false;
    std::function<void (int)> onSelect;

private:
    void attach (Knob&, const char* name, const juce::String& scaleMin, const juce::String& scaleMax, int sensitivity);
    bool isCut() const { return band == sq::lowCutBand || band == sq::highCutBand; }

    SqualizerAudioProcessor& proc;
    const int band;
    const juce::Colour colour;

    LedToggle power, sidechain { "SC", Theme::tuner };
    juce::ComboBox typeBox, channelBox, slopeBox;
    Knob freq { "Freq" }, gain { "Gain", true }, q { "Q" }, range { "Dyn", true }, threshold { "Thresh" };

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> boxes;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment, sidechainAttachment;
};
