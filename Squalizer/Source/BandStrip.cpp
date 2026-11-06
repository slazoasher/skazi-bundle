#include "BandStrip.h"

BandStrip::BandStrip (SqualizerAudioProcessor& p, int b)
    : proc (p), band (b), colour (BandColours::of (b)), power (Names::bands[b], BandColours::of (b))
{
    addAndMakeVisible (power);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::band (band, "ON"), power);

    channelBox.addItemList ({ "STEREO", "MID", "SIDE", "LEFT", "RIGHT" }, 1);
    channelBox.setColour (juce::ComboBox::textColourId, colour);
    channelBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (channelBox);
    boxes.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamIDs::band (band, "CH"), channelBox));

    if (isCut())
    {
        slopeBox.addItemList (Names::slopes, 1);
        slopeBox.setColour (juce::ComboBox::textColourId, colour);
        slopeBox.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (slopeBox);
        boxes.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamIDs::band (band, "SLOPE"), slopeBox));
    }
    else
    {
        typeBox.addItemList (Names::types, 1);
        typeBox.setColour (juce::ComboBox::textColourId, colour);
        typeBox.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (typeBox);
        boxes.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamIDs::band (band, "TYPE"), typeBox));
        // SC: the band's dynamics listen to the sidechain input instead of its own signal.
        addAndMakeVisible (sidechain);
        sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::band (band, "SC"), sidechain);
    }

    attach (freq, "FREQ", "20", "20k", 700);
    attach (q, "Q", "0.1", "18", 450);
    if (! isCut())
    {
        attach (gain, "GAIN", "-30", "+30", 450);
        attach (range, "DYN", "-24", "+24", 400);
        attach (threshold, "THR", "-60", "0", 400);
    }
    refresh();
}

void BandStrip::attach (Knob& knob, const char* name, const juce::String& scaleMin, const juce::String& scaleMax, int sensitivity)
{
    addAndMakeVisible (knob);
    knob.accent = colour;
    knob.baseSensitivity = sensitivity;
    knob.setScaleLabels (scaleMin, scaleMax);
    const auto id = ParamIDs::band (band, name);
    sliders.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, knob));
    auto& prm = *proc.apvts.getParameter (id);
    knob.setDoubleClickReturnValue (true, prm.convertFrom0to1 (prm.getDefaultValue()));
    knob.onDragStart = [this] { if (onSelect) onSelect (band); };
}

void BandStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    {
        auto top = r.removeFromTop (34);
        if (! isCut())
            sidechain.setBounds (top.removeFromRight (52).withTrimmedLeft (3));
        power.setBounds (top);
    }
    r.removeFromTop (4);
    if (isCut())
    {
        auto row = r.removeFromTop (28);
        channelBox.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (2));
        slopeBox.setBounds (row.withTrimmedLeft (2));
        r.removeFromTop (4);
    }
    else
    {
        auto row = r.removeFromTop (28);
        typeBox.setBounds (row.removeFromLeft (row.getWidth() * 3 / 5).withTrimmedRight (2));
        channelBox.setBounds (row.withTrimmedLeft (2));
        r.removeFromTop (4);
    }
    r.removeFromTop (4);

    freq.setBounds (r.removeFromTop (126));
    auto row = r.removeFromTop (96);
    if (isCut())
    {
        q.setBounds (row.withSizeKeepingCentre (row.getWidth() / 2, row.getHeight()));
    }
    else
    {
        gain.setBounds (row.removeFromLeft (row.getWidth() / 2));
        q.setBounds (row);
        auto dyn = r.removeFromTop (96);
        range.setBounds (dyn.removeFromLeft (dyn.getWidth() / 2));
        threshold.setBounds (dyn);
    }
}

void BandStrip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (3.0f);
    Theme::raisedPanel (g, r, 10.0f);
    // Colour cap along the top edge, brighter on the selected band.
    const auto cap = r.reduced (10.0f, 0.0f).withHeight (3.0f).translated (0.0f, 1.0f);
    g.setColour (colour.withAlpha (selected ? 0.35f : 0.12f));
    g.fillRoundedRectangle (cap.expanded (2.0f, 2.0f), 2.0f);
    g.setColour (colour.withAlpha (selected ? 1.0f : 0.55f));
    g.fillRoundedRectangle (cap, 1.5f);
    if (selected)
    {
        g.setColour (colour.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
    }
}

void BandStrip::mouseDown (const juce::MouseEvent&)
{
    if (onSelect)
        onSelect (band);
}

void BandStrip::refresh()
{
    const auto s = proc.bandSettings (band);
    auto dim = [] (juce::Component& c, bool off)
    {
        if (c.isEnabled() == off)
        {
            c.setEnabled (! off);
            c.setAlpha (off ? 0.4f : 1.0f);
        }
    };
    if (! isCut())
    {
        dim (gain, ! sq::hasGain (s.type));
        dim (range, ! sq::hasGain (s.type));
        dim (threshold, ! sq::hasGain (s.type) || std::abs (s.dyn) < 0.05);
        dim (sidechain, ! sq::hasGain (s.type) || std::abs (s.dyn) < 0.05);
    }
    // A switched-off band reads as off at a glance, but stays editable.
    const float alpha = s.on ? 1.0f : 0.55f;
    for (auto* c : { (juce::Component*) &freq, (juce::Component*) &q, (juce::Component*) &channelBox })
        if (c->isEnabled())
            c->setAlpha (alpha);
}
