#include "PluginEditor.h"

namespace
{
    juce::RangedAudioParameter& param (SqualizerAudioProcessor& p, const juce::String& id)
    {
        return *p.apvts.getParameter (id);
    }

    template <typename Comp>
    void setDimmed (Comp& c, bool dimmed)
    {
        if (c.isEnabled() == dimmed)
        {
            c.setEnabled (! dimmed);
            c.setAlpha (dimmed ? 0.4f : 1.0f);
        }
    }
}

SqualizerAudioProcessorEditor::SqualizerAudioProcessorEditor (SqualizerAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      display (p), scope (p),
      phaseSelector (param (p, ParamIDs::phase), juce::StringArray { "ZERO LAT", "NATURAL", "LINEAR" }),
      monitorMode (param (p, ParamIDs::monMode), Names::monitor)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &display, &scope, &phaseSelector, &presetBox, &prevPreset, &nextPreset,
                                                             &refCapture, &matchButton, &tubeIn, &undoButton, &redoButton, &slotA, &slotB, &copyButton,
                                                             &autoGain, &monitorWarning, &monitorMode, &polarityL, &polarityR, &swapLR })
        canvas.addAndMakeVisible (c);
    monitorWarning.setInterceptsMouseClicks (false, false);
    monitorWarning.toBack();

    for (auto [button, id] : { std::pair<juce::Button*, const char*> { &tubeIn, ParamIDs::tubeOn }, { &autoGain, ParamIDs::autoGain },
                               { &polarityL, ParamIDs::monPolL }, { &polarityR, ParamIDs::monPolR }, { &swapLR, ParamIDs::monSwap } })
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *button));

    undoButton.onClick = [this] { proc.undoManager.undo(); undoButton.flash = 1.0f; };
    redoButton.onClick = [this] { proc.undoManager.redo(); redoButton.flash = 1.0f; };
    for (auto* b : { &slotA, &slotB })
        b->setClickingTogglesState (false);
    slotA.onClick = [this] { proc.switchSlot (0); };
    slotB.onClick = [this] { proc.switchSlot (1); };
    copyButton.onClick = [this] { proc.copyToOtherSlot(); copyButton.flash = 1.0f; };

    for (int b = 0; b < sq::numBands; ++b)
    {
        strips[(size_t) b] = std::make_unique<BandStrip> (proc, b);
        strips[(size_t) b]->onSelect = [this] (int band) { selectBand (band); };
        canvas.addAndMakeVisible (*strips[(size_t) b]);
    }

    attach (output, ParamIDs::output, "-24", "+24", Theme::legend);
    attach (gainScale, ParamIDs::gainScale, "0", "200", Theme::legend);
    attach (lowFreq, ParamIDs::lowFreq, "20", "100", Theme::tuner);
    attach (lowBoost, ParamIDs::lowBoost, "0", "10", Theme::tuner);
    attach (lowAtten, ParamIDs::lowAtten, "0", "10", Theme::tuner);
    attach (highFreq, ParamIDs::highFreq, "3k", "16k", Theme::tuner);
    attach (bandwidth, ParamIDs::bandwidth, "SHARP", "BROAD", Theme::tuner);
    attach (highBoost, ParamIDs::highBoost, "0", "10", Theme::tuner);
    attach (attenSel, ParamIDs::attenFreq, "5k", "20k", Theme::tuner);
    attach (highAtten, ParamIDs::highAtten, "0", "10", Theme::tuner);
    attach (drive, ParamIDs::drive, "0", "100", Theme::tuner);

    display.onSelect = [this] (int band) { selectBand (band); };
    refCapture.setClickingTogglesState (true);
    refCapture.onClick = [this] { display.capturingReference = refCapture.getToggleState(); };
    matchButton.onClick = [this]
    {
        if (display.matchToReference() > 0)
            refCapture.setToggleState (false, juce::sendNotification);
        matchButton.flash = 1.0f;
    };

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setJustificationType (juce::Justification::centred);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != proc.getCurrentProgram())
            proc.setCurrentProgram (index);
    };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };

    canvas.setBounds (0, 0, designWidth, designHeight);
    layout();
    selectBand (1);

    float initialScale = 1.0f;
    if (auto* screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        initialScale = juce::jmin (1.0f, (float) screen->userArea.getHeight() * 0.85f / (float) designHeight);

    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (juce::roundToInt ((float) designWidth * initialScale), juce::roundToInt ((float) designHeight * initialScale));

    startTimerHz (60);
}

SqualizerAudioProcessorEditor::~SqualizerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SqualizerAudioProcessorEditor::attach (Knob& knob, const juce::String& paramID, const juce::String& scaleMin, const juce::String& scaleMax, juce::Colour accent)
{
    canvas.addAndMakeVisible (knob);
    knob.setScaleLabels (scaleMin, scaleMax);
    knob.accent = accent;
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramID, knob));
    auto& p = param (proc, paramID);
    knob.setDoubleClickReturnValue (true, p.convertFrom0to1 (p.getDefaultValue()));
}

void SqualizerAudioProcessorEditor::selectBand (int band)
{
    if (band == selectedBand)
        return;
    selectedBand = band;
    display.selected = band;
    for (int b = 0; b < sq::numBands; ++b)
    {
        strips[(size_t) b]->selected = b == band;
        strips[(size_t) b]->repaint();
    }
    display.repaint();
}

void SqualizerAudioProcessorEditor::stepPreset (int delta)
{
    const int count = proc.getNumPrograms();
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + count) % count);
    (delta < 0 ? prevPreset : nextPreset).flash = 1.0f;
}

void SqualizerAudioProcessorEditor::timerCallback()
{
    display.tick();
    scope.tick();
    for (auto& s : strips)
        s->refresh();

    for (auto* b : { &prevPreset, &nextPreset, &matchButton, &undoButton, &redoButton, &copyButton })
        if (b->flash > 0.0f)
        {
            b->flash = juce::jmax (0.0f, b->flash - 0.12f);
            b->repaint();
        }

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);

    for (auto* k : { &lowFreq, &lowBoost, &lowAtten, &highFreq, &bandwidth, &highBoost, &attenSel, &highAtten, &drive })
        setDimmed (*k, ! proc.tubeOn());
    setDimmed (matchButton, ! display.hasReference());

    // Each mouse gesture or click becomes one undo step.
    if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        proc.undoManager.beginNewTransaction();
    setDimmed (undoButton, ! proc.undoManager.canUndo());
    setDimmed (redoButton, ! proc.undoManager.canRedo());

    const int slot = proc.activeSlot();
    slotA.setToggleState (slot == 0, juce::dontSendNotification);
    slotB.setToggleState (slot == 1, juce::dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "COPY A>B" : "COPY B>A");

    const juce::String autoText = proc.apvts.getRawParameterValue (ParamIDs::autoGain)->load() > 0.5f
                                      ? "AUTO " + juce::String (proc.autoGainDb.load(), 1) + " dB" : juce::String ("AUTO GAIN");
    if (autoGain.getButtonText() != autoText)
        autoGain.setButtonText (autoText);

    // Monitor warning: blinks about twice a second while mono/side/polarity/swap is on.
    blink = (blink + 1) % 60;
    const float level = proc.monitorActive() ? (blink < 30 ? 1.0f : 0.35f) : 0.0f;
    if (std::abs (level - monitorWarning.level) > 0.01f)
    {
        monitorWarning.level = level;
        monitorWarning.repaint();
    }
}

void SqualizerAudioProcessorEditor::MonitorWarning::paint (juce::Graphics& g)
{
    if (level <= 0.0f)
        return;
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Theme::ledRed.withAlpha (0.12f * level));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Theme::ledRed.withAlpha (0.8f * level));
    g.drawRoundedRectangle (r, 8.0f, 1.5f);
}

void SqualizerAudioProcessorEditor::layout()
{
    header.setBounds (50, 10, 420, 66);
    phaseSelector.setBounds (486, 30, 276, 40);
    prevPreset.setBounds (772, 28, 36, 38);
    presetBox.setBounds (810, 26, 296, 42);
    nextPreset.setBounds (1110, 28, 36, 38);

    // Toolbar.
    undoButton.setBounds (50, 90, 70, 32);
    redoButton.setBounds (124, 90, 70, 32);
    slotA.setBounds (214, 89, 54, 34);
    slotB.setBounds (270, 89, 54, 34);
    copyButton.setBounds (328, 90, 100, 32);
    autoGain.setBounds (448, 89, 160, 34);
    monitorWarning.setBounds (684, 84, 470, 44);
    monitorMode.setBounds (690, 86, 228, 40);
    polarityL.setBounds (922, 89, 70, 34);
    polarityR.setBounds (994, 89, 70, 34);
    swapLR.setBounds (1066, 89, 82, 34);

    display.setBounds (displayBounds);
    scope.setBounds (scopeBounds);
    refCapture.setBounds (894, 494, 128, 34);
    matchButton.setBounds (1026, 495, 124, 32);

    // Six channel-strip columns: LOW CUT, BAND 1-4, HIGH CUT.
    const float stripWidth = 1112.0f / (float) sq::numBands;
    for (int b = 0; b < sq::numBands; ++b)
        strips[(size_t) b]->setBounds (juce::roundToInt (44.0f + stripWidth * (float) b), 536, juce::roundToInt (stripWidth), 404);

    sections = {
        { { 56, 952, 58, 16 },    "TUBE" },
        { { 124, 952, 244, 16 },  "LOW" },
        { { 378, 952, 244, 16 },  "HIGH BOOST" },
        { { 632, 952, 162, 16 },  "HIGH ATTEN" },
        { { 804, 952, 80, 16 },   "COLOUR" },
        { { 908, 952, 240, 16 },  "MASTER" },
    };

    auto row = [] (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
    {
        const int w = area.getWidth() / (int) knobs.size();
        int i = 0;
        for (auto* k : knobs)
            k->setBounds (area.getX() + i++ * w, area.getY(), w, area.getHeight());
    };
    tubeIn.setBounds (50, 1012, 64, 36);
    row ({ 120, 974, 246, 118 }, { &lowFreq, &lowBoost, &lowAtten });
    row ({ 376, 974, 246, 118 }, { &highFreq, &bandwidth, &highBoost });
    row ({ 632, 974, 164, 118 }, { &attenSel, &highAtten });
    row ({ 802, 974, 82, 118 }, { &drive });
    row ({ 906, 974, 240, 118 }, { &output, &gainScale });
}

juce::Image SqualizerAudioProcessorEditor::renderFaceTexture() const
{
    // A plain software image: fast to write pixel by pixel, and nothing for the graphics driver to free later.
    juce::Image texture (juce::Image::RGB, designWidth, designHeight, false, juce::SoftwareImageType());
    {
        juce::Graphics g (texture);
        g.setGradientFill (juce::ColourGradient (Theme::plateTop, 0.0f, 0.0f, Theme::plateBottom, 0.0f, (float) designHeight, false));
        g.fillAll();
        juce::Random brush (2024);
        for (float y = 0.0f; y < (float) designHeight; y += 1.0f)
        {
            float x = 0.0f;
            while (x < (float) designWidth)
            {
                const float len = 40.0f + brush.nextFloat() * 260.0f;
                g.setColour ((brush.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.012f + brush.nextFloat() * 0.018f));
                g.fillRect (x, y, len, 1.0f);
                x += len + brush.nextFloat() * 30.0f;
            }
        }
    }

    // Fine powder-coat grain: nudge each pixel's brightness a little.
    juce::Image::BitmapData pixels (texture, juce::Image::BitmapData::readWrite);
    juce::Random noise (77);
    for (int y = 0; y < designHeight; ++y)
    {
        auto* p = pixels.getLinePointer (y);
        for (int x = 0; x < designWidth; ++x, p += pixels.pixelStride)
        {
            const int delta = (int) ((noise.nextFloat() - 0.5f) * 9.0f);
            for (int c = 0; c < 3; ++c)
                p[c] = (juce::uint8) juce::jlimit (0, 255, (int) p[c] + delta);
        }
    }
    return texture;
}

void SqualizerAudioProcessorEditor::renderBackground()
{
    const float scale = juce::Component::getApproximateScaleFactorForComponent (&canvas);
    auto& image = canvas.background;
    image = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) designWidth * scale), juce::roundToInt ((float) designHeight * scale), true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) designWidth, (float) designHeight);
    const float earWidth = 34.0f;

    if (! faceTexture.isValid())
        faceTexture = renderFaceTexture();
    g.drawImage (faceTexture, bounds);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0.0f, 0.0f, juce::Colours::transparentWhite, 0.0f, 260.0f, false));
    g.fillRect (bounds);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.25f), 0.0f, bounds.getBottom(), juce::Colours::transparentBlack, 0.0f, bounds.getBottom() - 160.0f, false));
    g.fillRect (bounds);

    // Rack ears with mounting slots and front handles.
    for (float x : { 0.0f, bounds.getRight() - earWidth })
    {
        const auto ear = juce::Rectangle<float> (x, 0.0f, earWidth, bounds.getHeight());
        const bool left = x < 1.0f;
        g.setGradientFill (juce::ColourGradient (Theme::ear.brighter (0.08f), ear.getX(), 0.0f, Theme::ear, ear.getRight(), 0.0f, false));
        g.fillRect (ear);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillRect (left ? ear.getRight() - 1.0f : ear.getX(), 0.0f, 1.0f, ear.getHeight());
        g.setColour (Theme::bevel.withAlpha (0.4f));
        g.fillRect (left ? ear.getRight() : ear.getX() - 1.0f, 0.0f, 1.0f, ear.getHeight());
        for (float y : { 52.0f, bounds.getBottom() - 52.0f })
        {
            const auto slot = juce::Rectangle<float> (16.0f, 26.0f).withCentre ({ ear.getCentreX(), y });
            g.setColour (juce::Colours::black);
            g.fillRoundedRectangle (slot, 8.0f);
            g.setColour (Theme::bevel.withAlpha (0.35f));
            g.drawRoundedRectangle (slot.translated (0.0f, 1.0f), 8.0f, 0.8f);
            Theme::screw (g, slot.getCentre().translated (0.0f, -3.0f), 6.5f);
        }

        const float hx = ear.getCentreX(), top = 112.0f, bottom = bounds.getBottom() - 112.0f;
        juce::Path tube;
        tube.addRoundedRectangle (juce::Rectangle<float> (hx - 7.0f, top, 14.0f, bottom - top), 7.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 10, { 4, 6 }).drawForPath (g, tube);
        juce::ColourGradient chrome (juce::Colour (0xff2a2c30), hx - 7.0f, 0.0f, juce::Colour (0xff1a1b1d), hx + 7.0f, 0.0f, false);
        chrome.addColour (0.28, juce::Colour (0xffd9dce1));
        chrome.addColour (0.45, juce::Colour (0xff8e9196));
        chrome.addColour (0.75, juce::Colour (0xff3c3f43));
        g.setGradientFill (chrome);
        g.fillPath (tube);
        for (float y : { top, bottom - 30.0f })
        {
            const auto block = juce::Rectangle<float> (hx - 12.0f, y - 2.0f, 24.0f, 34.0f);
            juce::Path b;
            b.addRoundedRectangle (block, 4.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 8, { 2, 4 }).drawForPath (g, b);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d41), block.getX(), block.getY(), juce::Colour (0xff141517), block.getX(), block.getBottom(), false));
            g.fillPath (b);
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.drawHorizontalLine ((int) block.getY() + 1, block.getX() + 3.0f, block.getRight() - 3.0f);
            Theme::screw (g, block.getCentre(), 4.5f);
        }
    }
    g.setColour (Theme::bevel.withAlpha (0.8f));
    g.fillRect (earWidth, 0.0f, bounds.getWidth() - earWidth * 2.0f, 1.0f);
    g.setColour (juce::Colours::black);
    g.fillRect (0.0f, bounds.getBottom() - 1.0f, bounds.getWidth(), 1.0f);

    Theme::legendText (g, "PHASE", { 486.0f, 10.0f, 200.0f, 14.0f }, juce::Justification::centredLeft, Theme::legendDim, 0.22f, 10.0f);
    Theme::legendText (g, "PROGRAM", { 810.0f, 10.0f, 200.0f, 14.0f }, juce::Justification::centredLeft, Theme::legendDim, 0.22f, 10.0f);

    Theme::grooveH (g, 50.0f, 1150.0f, 84.0f);
    Theme::bezel (g, displayBounds.toFloat());
    Theme::bezel (g, scopeBounds.toFloat());
    Theme::raisedPanel (g, { 44.0f, 946.0f, 852.0f, 152.0f }, 12.0f);
    Theme::raisedPanel (g, { 900.0f, 946.0f, 256.0f, 152.0f }, 12.0f);
    for (float x : { 117.0f, 371.0f, 627.0f, 799.0f })
        Theme::grooveV (g, x, 954.0f, 1090.0f);
    Theme::grooveV (g, 204.0f, 92.0f, 120.0f);
    Theme::grooveV (g, 438.0f, 92.0f, 120.0f);
    Theme::legendText (g, "MONITOR", { 618.0f, 98.0f, 64.0f, 16.0f }, juce::Justification::centredRight, Theme::legendDim, 0.18f, 10.0f);

    for (const auto& section : sections)
        Theme::sectionTitle (g, section.title, section.bounds.toFloat());

    const auto footer = juce::Rectangle<float> (50.0f, bounds.getBottom() - 22.0f, 1100.0f, 14.0f);
    Theme::legendText (g, "S-QUALIZER  V1.0", footer, juce::Justification::centredLeft, Theme::legendDim, 0.22f, 9.5f);
    Theme::legendText (g, "DRAG NODES  /  SHIFT = FINE  /  WHEEL = Q  /  ARROWS = NUDGE  /  DOUBLE-CLICK NODE = ON-OFF  /  DOUBLE-CLICK PEAK = GRAB",
                       footer, juce::Justification::centred, Theme::legendDim.withAlpha (0.7f), 0.12f, 9.0f);
    Theme::legendText (g, "DESIGNED BY ASHER SWISSA", footer, juce::Justification::centredRight, Theme::legendDim, 0.22f, 9.5f);
}

void SqualizerAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));

    if (std::abs (scale - renderedScale) > 0.001f)
    {
        renderedScale = scale;
        renderBackground();
        canvas.repaint();
    }
}
