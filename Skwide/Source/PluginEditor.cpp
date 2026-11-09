#include "PluginEditor.h"

namespace
{
    juce::RangedAudioParameter& param (SkwideAudioProcessor& p, const char* id)
    {
        return *p.apvts.getParameter (id);
    }
}

SkwideAudioProcessorEditor::SkwideAudioProcessorEditor (SkwideAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      imager (p), scope (p), phaseWheel (p), depthView (p), spreadMeter (p), haasView (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &imager, &scope, &phaseWheel, &depthView, &spreadMeter, &haasView,
                                                             &presetBox, &prevPreset, &nextPreset, &randomButton })
        canvas.addAndMakeVisible (c);

    attach (width, ParamIDs::width);
    attach (spread, ParamIDs::spread);
    attach (depth, ParamIDs::depth);
    attach (phase, ParamIDs::phase);
    attach (driftRate, ParamIDs::driftRate);
    attach (driftDepth, ParamIDs::driftDepth);
    attach (lowFreq, ParamIDs::lowFreq);
    attach (mix, ParamIDs::mix);
    attach (output, ParamIDs::output);
    attach (lowOn, ParamIDs::lowOn);
    attach (monoBass, ParamIDs::monoBass);
    attach (swap, ParamIDs::swap);
    attach (monoCheck, ParamIDs::monoCheck);
    attach (haasOn, ParamIDs::haasOn);
    attach (haasSafe, ParamIDs::haasSafe);
    attach (haasLeft, ParamIDs::haasLeft);
    attach (haasRight, ParamIDs::haasRight);
    attach (haasAmount, ParamIDs::haasAmount);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setJustificationType (juce::Justification::centred);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && (index != proc.getCurrentProgram() || proc.randomised))
            proc.setCurrentProgram (index);
    };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    randomButton.onClick = [this]
    {
        proc.randomise();
        randomButton.flash = 1.0f;
    };

    canvas.setBounds (0, 0, designWidth, designHeight);
    layout();

    float initialScale = 1.0f;
    if (auto* screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        initialScale = juce::jmin (1.0f, (float) screen->userArea.getHeight() * 0.85f / (float) designHeight);

    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (juce::roundToInt ((float) designWidth * initialScale), juce::roundToInt ((float) designHeight * initialScale));

    startTimerHz (60);
}

SkwideAudioProcessorEditor::~SkwideAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SkwideAudioProcessorEditor::attach (NeonKnob& knob, const char* paramID)
{
    canvas.addAndMakeVisible (knob);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramID, knob));
    auto& p = param (proc, paramID);
    knob.setDoubleClickReturnValue (true, p.convertFrom0to1 (p.getDefaultValue()));
}

void SkwideAudioProcessorEditor::attach (NeonToggle& toggle, const char* paramID)
{
    canvas.addAndMakeVisible (toggle);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, paramID, toggle));
}

void SkwideAudioProcessorEditor::stepPreset (int delta)
{
    const int count = proc.getNumPrograms();
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + count) % count);
    (delta < 0 ? prevPreset : nextPreset).flash = 1.0f;
}

void SkwideAudioProcessorEditor::timerCallback()
{
    header.tick();
    imager.tick();
    scope.tick();
    phaseWheel.tick();
    depthView.tick();
    spreadMeter.tick();
    haasView.tick();

    for (auto* b : { &prevPreset, &nextPreset, &randomButton })
        if (b->flash > 0.0f)
        {
            b->flash = juce::jmax (0.0f, b->flash - 0.06f);
            b->repaint();
        }

    if (proc.randomised)
    {
        if (presetBox.getText() != "* RANDOM *")
            presetBox.setText ("* RANDOM *", juce::dontSendNotification);
    }
    else if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);

    const bool lowEnabled = proc.apvts.getRawParameterValue (ParamIDs::lowOn)->load() > 0.5f;
    if (monoBass.isEnabled() != lowEnabled)
    {
        monoBass.setEnabled (lowEnabled);
        lowFreq.setEnabled (lowEnabled);
    }
}

void SkwideAudioProcessorEditor::layout()
{
    header.setBounds (0, 0, designWidth, 64);
    prevPreset.setBounds (380, 16, 36, 32);
    presetBox.setBounds (420, 16, 300, 32);
    nextPreset.setBounds (724, 16, 36, 32);
    randomButton.setBounds (770, 16, 130, 32);

    panels = {
        { { 16, 76, 768, 364 },   "01", "IMAGER", Theme::cyan },
        { { 796, 76, 388, 364 },  "02", "SCOPE", Theme::magenta },
        { { 16, 452, 280, 312 },  "03", "WIDTH", Theme::cyan },
        { { 308, 452, 190, 312 }, "04", "DEPTH", Theme::lime },
        { { 510, 452, 300, 312 }, "05", "PHASE", Theme::magenta },
        { { 822, 452, 160, 312 }, "06", "BASS", Theme::amber },
        { { 994, 452, 190, 312 }, "07", "OUTPUT", Theme::cyan },
        { { 16, 776, 1168, 156 }, "08", "SUPER WIDE // HAAS DELAY", Theme::magenta },
    };

    auto content = [this] (size_t i) { return panels[i].bounds.reduced (10).withTrimmedTop (32); };

    auto row = [] (juce::Rectangle<int> area, std::initializer_list<NeonKnob*> knobs, int maxHeight)
    {
        const int w = area.getWidth() / (int) knobs.size();
        int i = 0;
        for (auto* k : knobs)
            k->setBounds (juce::Rectangle<int> (area.getX() + i++ * w, area.getY(), w, area.getHeight())
                              .withSizeKeepingCentre (w - 4, juce::jmin (area.getHeight(), maxHeight)));
    };

    imager.setBounds (content (0));
    scope.setBounds (content (1));

    auto widthArea = content (2);
    auto knobs = widthArea.removeFromTop (172);
    width.setBounds (knobs.removeFromLeft (156));
    spread.setBounds (knobs.withSizeKeepingCentre (knobs.getWidth(), 118));
    widthArea.removeFromTop (10);
    spreadMeter.setBounds (widthArea.removeFromTop (32));

    auto depthArea = content (3);
    depthView.setBounds (depthArea.removeFromTop (112));
    depthArea.removeFromTop (6);
    depth.setBounds (depthArea.withSizeKeepingCentre (150, juce::jmin (depthArea.getHeight(), 144)));

    auto phaseArea = content (4);
    phaseWheel.setBounds (phaseArea.removeFromTop (146).withSizeKeepingCentre (146, 146));
    phaseArea.removeFromTop (6);
    row (phaseArea, { &phase, &driftRate, &driftDepth }, 110);

    auto lowArea = content (5);
    lowFreq.setBounds (lowArea.removeFromTop (150).withSizeKeepingCentre (140, 150));
    lowArea.removeFromTop (10);
    lowOn.setBounds (lowArea.removeFromTop (32));
    lowArea.removeFromTop (8);
    monoBass.setBounds (lowArea.removeFromTop (32));

    auto outArea = content (6);
    row (outArea.removeFromTop (130), { &mix, &output }, 130);
    outArea.removeFromTop (14);
    swap.setBounds (outArea.removeFromTop (32));
    outArea.removeFromTop (8);
    monoCheck.setBounds (outArea.removeFromTop (34));

    auto haasArea = content (7);
    auto toggles = haasArea.removeFromLeft (160).withSizeKeepingCentre (160, 80);
    haasOn.setBounds (toggles.removeFromTop (36));
    toggles.removeFromTop (8);
    haasSafe.setBounds (toggles.removeFromTop (36));
    haasArea.removeFromLeft (12);
    row (haasArea.removeFromLeft (342), { &haasLeft, &haasRight, &haasAmount }, 110);
    haasArea.removeFromLeft (12);
    haasView.setBounds (haasArea);
}

void SkwideAudioProcessorEditor::renderBackground()
{
    const float scale = juce::Component::getApproximateScaleFactorForComponent (&canvas);
    auto& image = canvas.background;
    // Drawn in software (no GPU readback per resize step), uploaded once in resized().
    image = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) designWidth * scale), juce::roundToInt ((float) designHeight * scale), true,
                         juce::SoftwareImageType());
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) designWidth, (float) designHeight);
    g.fillAll (Theme::background);

    // Concentric "sound field" arcs radiating from the centre, split cyan left / magenta right.
    const juce::Point<float> centre { bounds.getCentreX(), 470.0f };
    for (int i = 1; i < 26; ++i)
    {
        const float rad = (float) i * 34.0f;
        juce::Path leftArc, rightArc;
        leftArc.addCentredArc (centre.x, centre.y, rad, rad * 0.62f, 0.0f, -juce::MathConstants<float>::pi, 0.0f, true);
        rightArc.addCentredArc (centre.x, centre.y, rad, rad * 0.62f, 0.0f, 0.0f, juce::MathConstants<float>::pi, true);
        const float a = 0.05f * (1.0f - (float) i / 26.0f) + 0.012f;
        g.setColour (Theme::cyan.withAlpha (a));
        g.strokePath (leftArc, juce::PathStrokeType (0.8f));
        g.setColour (Theme::magenta.withAlpha (a));
        g.strokePath (rightArc, juce::PathStrokeType (0.8f));
    }

    g.setGradientFill (juce::ColourGradient (Theme::cyan.withAlpha (0.06f), 400.0f, 250.0f, Theme::cyan.withAlpha (0.0f), 400.0f, 700.0f, true));
    g.fillRect (bounds);
    g.setGradientFill (juce::ColourGradient (Theme::magenta.withAlpha (0.05f), 990.0f, 250.0f, Theme::magenta.withAlpha (0.0f), 990.0f, 600.0f, true));
    g.fillRect (bounds);

    for (const auto& panel : panels)
        Theme::drawPanel (g, panel.bounds.toFloat(), panel.index, panel.title, panel.accent);

    // Little captions inside the panels.
    g.setFont (Theme::mono (9.0f));
    g.setColour (Theme::dim.brighter (0.2f));
    g.drawFittedText ("WIDTH > 100% ALSO FEEDS THE SPREAD ENGINE.\nSUMS TO MONO WITH ZERO CANCELLATION.",
                      juce::Rectangle<int> (26, 712, 260, 40), juce::Justification::centredLeft, 3);

    g.setFont (Theme::mono (10.0f));
    g.setColour (Theme::dim);
    const auto footer = bounds.withTop (bounds.getBottom() - 20.0f).reduced (18.0f, 0.0f);
    g.drawText ("SKAZI // SKWIDE v1.0 // BY ASHER SWISSA", footer, juce::Justification::centredLeft);
    g.drawText ("ALLPASS DECORRELATION  //  HILBERT PHASE ROTATION  //  HAAS SUPER WIDE  //  LR4 LOW BYPASS", footer, juce::Justification::centredRight);
}

void SkwideAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));

    if (std::abs (scale - renderedScale) > 0.001f)
    {
        renderedScale = scale;
        renderBackground();
        canvas.background = juce::NativeImageType().convert (canvas.background); // painted every frame, so keep it on the GPU
        canvas.repaint();
    }
}
