#include "PluginEditor.h"

namespace
{
constexpr int contentW = 1220;
constexpr int cheekW = 34;                 // walnut side panels
constexpr int editorW = contentW + 2 * cheekW;
constexpr int keyboardH = 70;
constexpr int editorH = 606;
constexpr int tabTop = 100;                // where the tab pages start
constexpr int titleH = 20;
} // namespace

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text,
            bool onDeck, bool bipolar)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr, 1200);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.getProperties().set ("deck", onDeck);
    slider.getProperties().set ("bipolar", bipolar);
    if (auto* prm = state.getParameter (paramId))
        slider.setTooltip (prm->getName (64));
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (skin::labelFont (12.5f));
    label.setColour (juce::Label::textColourId, onDeck ? skin::deckLabel : skin::cream);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
}

void Knob::showValue()
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 76, 20);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, skin::lcd);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::black);
    slider.setColour (juce::Slider::textBoxTextColourId, skin::hotPink);
}

void Knob::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));
    slider.setBounds (b.reduced (2));
}

//==============================================================================
Selector::Selector (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text, bool onDeck)
{
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        box.addItemList (prm->choices, 1);
    addAndMakeVisible (box);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (skin::labelFont (12.5f));
    label.setColour (juce::Label::textColourId, onDeck ? skin::deckLabel : skin::cream);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
}

void Selector::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));
    box.setBounds (b.withSizeKeepingCentre (b.getWidth() - 6, juce::jmin (26, b.getHeight())));
}

//==============================================================================
void PresetDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (skin::lcd);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colour (0x10ffb444));
    for (float yy = r.getY() + 2.0f; yy < r.getBottom(); yy += 3.0f)
        g.drawHorizontalLine ((int) yy, r.getX() + 2.0f, r.getRight() - 2.0f);
    g.setColour (juce::Colour (0xff4a4d53));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    g.setColour (skin::amber);
    g.setFont (juce::Font (juce::FontOptions ("Consolas", 16.0f, juce::Font::bold)));
    g.drawFittedText (text, getLocalBounds().reduced (12, 0).withTrimmedRight (14), juce::Justification::centredLeft, 1);

    juce::Path arrow;
    const float ax = r.getRight() - 14.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.0f);
    g.setColour (skin::amber.withAlpha (0.7f));
    g.fillPath (arrow);
}

//==============================================================================
void Wheel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromBottom (11.0f);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawFittedText (label, text.toNearestInt().expanded (3, 0), juce::Justification::centred, 1, 0.5f);

    r = r.reduced (1.0f, 1.0f);
    g.setColour (juce::Colour (0xff0b0a0e));
    g.fillRoundedRectangle (r, 4.0f);
    auto wheel = r.reduced (3.0f, 2.0f);
    juce::ColourGradient shade (juce::Colour (0xff101014), 0.0f, wheel.getY(), juce::Colour (0xff101014), 0.0f, wheel.getBottom(), false);
    shade.addColour (0.5, juce::Colour (0xff4a4656));
    g.setGradientFill (shade);
    g.fillRoundedRectangle (wheel, 3.0f);

    // Ribs scroll with the value, so it reads as a turning wheel
    const double norm = (getValue() - getMinimum()) / (getMaximum() - getMinimum());
    const float y = wheel.getBottom() - (float) norm * wheel.getHeight();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (float ry = std::fmod (y, 5.0f) + wheel.getY(); ry < wheel.getBottom(); ry += 5.0f)
        g.drawHorizontalLine ((int) ry, wheel.getX() + 2.0f, wheel.getRight() - 2.0f);

    g.setColour (skin::accentOrange);
    g.fillRect (juce::Rectangle<float> (wheel.getX(), y - 2.0f, wheel.getWidth(), 4.0f).constrainedWithin (wheel));
}

//==============================================================================
SynthPanel::SynthPanel (Skazi303Processor& p)
    : processor (p), grid (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);

    // Synth voice.
    knob ("wave", "WAVEFORM", false);
    knob ("tune", "TUNING", false, true);
    knob ("cutoff", "CUT OFF FREQ", false);
    knob ("reso", "RESONANCE", false);
    knob ("envmod", "ENV MOD", false);
    knob ("decay", "DECAY", false);
    knob ("accent", "ACCENT", false);
    knob ("slide_time", "SLIDE TIME", false);

    selector ("pre_type", "PRE TYPE", false);
    knob ("pre_drive", "PRE DRIVE", false);
    selector ("post_type", "POST TYPE", false);
    knob ("post_drive", "POST DRIVE", false);
    knob ("dist_tone", "TONE", false);
    knob ("dist_mix", "MIX", false);
    selector ("drive2_type", "DRIVE 2 TYPE", false);
    knob ("drive2", "DRIVE 2", false);
    knob ("ott_depth", "OTT", false);
    knob ("ott_time", "OTT TIME", false);

    knob ("fine", "FINE", false, true);
    knob ("drift", "DRIFT", false);
    knob ("sub_level", "SUB LEVEL", false);
    selector ("sub_wave", "SUB WAVE", false);
    selector ("sub_oct", "SUB OCTAVE", false);
    knob ("flt_track", "FILTER TRACK", false);
    knob ("acc_decay", "ACCENT DECAY", false);
    knob ("soft_attack", "ATTACK", false);
    knob ("sweep", "SWEEP SPEED", false);
    knob ("pulse_width", "PULSE WIDTH", false);
    knob ("noise", "NOISE", false);

    selector ("f2_type", "TYPE", false);
    knob ("f2_cutoff", "CUTOFF", false);
    knob ("f2_reso", "RESO", false);
    knob ("f2_vowel", "VOWEL", false);
    knob ("f2_mix", "MIX", false);

    knob ("vca_decay", "VCA DECAY", false);

    // Envelopes (MODS tab). DECAY knobs there share the SYNTH tab's VCA DECAY and DECAY.
    knobAs ("env_vca_decay", "vca_decay", "DECAY");
    knob ("amp_sus", "SUSTAIN", false);
    knob ("amp_rel", "RELEASE", false);
    knob ("flt_att", "ATTACK", false);
    knobAs ("env_decay", "decay", "DECAY");
    knob ("flt_sus", "SUSTAIN", false);
    knob ("flt_rel", "RELEASE", false);

    knob ("bend_up", "BEND UP", false);
    knob ("bend_down", "BEND DOWN", false);
    selector ("mw_dest", "MOD WHEEL", false);
    knob ("mw_amt", "MW AMOUNT", false);

    for (auto* w : { &pitchWheel, &modWheel })
    {
        w->setSliderStyle (juce::Slider::LinearVertical);
        w->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        w->setColour (juce::Slider::trackColourId, skin::accentOrange.withAlpha (0.85f));
        w->setColour (juce::Slider::backgroundColourId, juce::Colour (0xff15131a));
        addAndMakeVisible (*w);
    }
    pitchWheel.setRange (-1.0, 1.0);
    pitchWheel.setValue (0.0, juce::dontSendNotification);
    pitchWheel.setTooltip ("PITCH wheel: drag up or down, it springs back. Range: BEND UP / BEND DOWN on the ENV page");
    pitchWheel.onValueChange = [this] { processor.wheelBend.store ((float) pitchWheel.getValue()); };
    pitchWheel.onDragEnd = [this] { pitchWheel.setValue (0.0, juce::sendNotificationSync); };
    modWheel.setRange (0.0, 1.0);
    modWheel.setTooltip ("MOD wheel: opens the filter and/or adds vibrato (MOD WHEEL on the ENV page)");
    modWheel.onValueChange = [this] { processor.wheelMod.store ((float) modWheel.getValue()); };
    // MOD tab: OSC B, unison, filter model, LFOs
    knob ("osc_b_level", "OSC B LEVEL", false);
    knob ("osc_b_semi", "OSC B SEMI", false, true);
    knob ("osc_b_fine", "OSC B FINE", false, true);
    knob ("uni_voices", "UNISON", false);
    knob ("uni_detune", "DETUNE", false);
    selector ("flt_model", "FILTER MODEL", false);
    for (auto l : { "1", "2" })
    {
        const juce::String n (l);
        selector ("lfo" + n + "_shape", "SHAPE", false);
        selector ("lfo" + n + "_dest", "DESTINATION", false);
        knob ("lfo" + n + "_rate", "RATE (SYNC)", false).showValue();
        knob ("lfo" + n + "_amt", "AMOUNT", false, true);
    }
    knob ("fat", "FAT (TUBE)", false);
    knob ("width", "WIDTH", false);
    knob ("volume", "VOLUME", false);

    // FX and sequencer controls.
    selector ("seq_mode", "SYNC", false);
    knob ("int_bpm", "BPM", false);

    selector ("mod_mode", "MOD FX", false);
    knob ("mod_rate", "RATE", false);
    knob ("mod_depth", "DEPTH", false);
    knob ("mod_mix", "MIX", false);
    knob ("dly_sync", "TIME (SYNC)", false).showValue();
    knob ("dly_fb", "FEEDBACK", false);
    knob ("dly_tone", "TONE", false);
    knob ("dly_mix", "MIX", false);
    knob ("rev_size", "SIZE", false);
    knob ("rev_damp", "DAMPING", false);
    knob ("rev_mix", "MIX", false);

    selector ("seq_rate", "RATE", false);
    selector ("root", "ROOT", false);
    selector ("gen_scale", "SCALE", false);
    knob ("seq_len", "LENGTH", false);
    knob ("swing", "SWING", false);
    knob ("gate_len", "GATE", false);
    knob ("gen_density", "DENSITY", false);
    knob ("gen_accent", "ACC %", false);
    knob ("gen_slide", "SLIDE %", false);
    knob ("gen_octave", "OCT %", false);

    // Preset bar
    presetDisplay.onClick = [this] { showPresetMenu(); };
    presetDisplay.setText (processor.getPresetDisplayName());
    addAndMakeVisible (presetDisplay);
    for (auto* b : { &presetPrevBtn, &presetNextBtn, &randomBtn, &saveBtn })
    {
        b->setColour (juce::TextButton::textColourOffId, skin::ink);
        addAndMakeVisible (b);
    }
    randomBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    randomBtn.getProperties().set ("hot", true);
    randomBtn.setTooltip ("Roll a brand-new sound (keeps your pattern). Use GENERATE for a new pattern.");
    saveBtn.setTooltip ("Save the current sound and pattern as a user preset");
    presetPrevBtn.setTooltip ("Previous preset");
    presetNextBtn.setTooltip ("Next preset");
    presetPrevBtn.onClick = [this] { stepPreset (-1); };
    presetNextBtn.onClick = [this] { stepPreset (1); };
    randomBtn.onClick = [this] { processor.randomizeSound(); };
    saveBtn.onClick = [this]
    {
        auto folder = Skazi303Processor::getUserPresetFolder();
        folder.createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Save preset", folder.getChildFile ("My Acid.xml"), "*.xml");
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f != juce::File())
                                          processor.saveUserPreset (f.withFileExtension ("xml"));
                                  });
    };

    seqOnBtn.setClickingTogglesState (true);
    seqOnBtn.setColour (juce::TextButton::textColourOffId, skin::ink);
    seqOnBtn.setTooltip ("Off: plays from MIDI like a normal synth. On: the step sequencer plays (MIDI notes then transpose it)");
    addAndMakeVisible (seqOnBtn);
    seqOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "seq_on", seqOnBtn);

    turboBtn.setClickingTogglesState (true);
    turboBtn.getProperties().set ("hot", true);
    turboBtn.setColour (juce::TextButton::textColourOffId, skin::ink);
    turboBtn.setTooltip ("TURBO: DRIVE 2 and OTT slammed on at once for instant power, level-matched so it hits harder, not just louder");
    addAndMakeVisible (turboBtn);
    turboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "turbo", turboBtn);

    // Tabs: LED buttons, one lit at a time.
    for (auto* b : { &synthTabBtn, &modsTabBtn, &fxTabBtn, &seqTabBtn, &lfoTabBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (303);
        b->setColour (juce::TextButton::textColourOffId, skin::ink);
        addAndMakeVisible (b);
    }
    synthTabBtn.onClick = [this] { if (synthTabBtn.getToggleState()) showTab (0); };
    modsTabBtn.onClick = [this] { if (modsTabBtn.getToggleState()) showTab (1); };
    fxTabBtn.onClick = [this] { if (fxTabBtn.getToggleState()) showTab (2); };
    seqTabBtn.onClick = [this] { if (seqTabBtn.getToggleState()) showTab (3); };
    lfoTabBtn.onClick = [this] { if (lfoTabBtn.getToggleState()) showTab (4); };

    for (auto* b : { &generateBtn, &mutateBtn, &clearBtn, &shiftLeftBtn, &shiftRightBtn })
    {
        b->setColour (juce::TextButton::textColourOffId, skin::ink);
        addAndMakeVisible (b);
    }
    generateBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    generateBtn.getProperties().set ("hot", true);
    generateBtn.setTooltip ("New random line in the chosen scale, using the density / accent / slide / octave amounts");
    mutateBtn.setTooltip ("Change a few steps, keep the rest");
    clearBtn.setTooltip ("Reset every step to a plain root note");
    shiftLeftBtn.setTooltip ("Rotate the pattern one step earlier");
    shiftRightBtn.setTooltip ("Rotate the pattern one step later");

    generateBtn.onClick = [this] { processor.generatePattern(); };
    mutateBtn.onClick = [this] { processor.mutatePattern(); };
    clearBtn.onClick = [this] { processor.clearPattern(); };
    shiftLeftBtn.onClick = [this] { processor.shiftPattern (-1); };
    shiftRightBtn.onClick = [this] { processor.shiftPattern (1); };

    addAndMakeVisible (grid);

    // On-screen keyboard; also plays from the computer keyboard (A W S E D...) when clicked.
    keyboard.setAvailableRange (24, 96);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyWidth (22.0f);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setKeyPressBaseOctave (3);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xfff4efe4));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff141414));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff6d6a63));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, skin::accentOrange.withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, skin::amber.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colour (0x66000000));
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, skin::panel);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, skin::cream);
    keyboard.setWantsKeyboardFocus (true);
    addAndMakeVisible (keyboard);

    // Which controls live on which tab.
    for (auto id : { "wave", "tune", "cutoff", "reso", "envmod", "decay", "accent", "slide_time",
                     "pre_type", "pre_drive", "post_type", "post_drive", "dist_tone", "dist_mix",
                     "drive2_type", "drive2", "ott_depth", "ott_time",
                     "fine", "drift", "sub_level", "sub_wave", "f2_type", "f2_cutoff", "f2_reso", "f2_vowel", "f2_mix",
                     "vca_decay", "fat", "volume" })
        synthItems.push_back (controls[id].get());
    synthItems.push_back (&turboBtn);
    for (auto id : { "mod_mode", "mod_rate", "mod_depth", "mod_mix", "dly_sync", "dly_fb", "dly_tone", "dly_mix",
                     "rev_size", "rev_damp", "rev_mix", "width" })
        fxItems.push_back (controls[id].get());
    for (auto id : { "flt_track", "acc_decay", "soft_attack", "sweep", "pulse_width", "noise", "sub_oct",
                     "env_vca_decay", "amp_sus", "amp_rel", "flt_att", "env_decay", "flt_sus", "flt_rel",
                     "bend_up", "bend_down", "mw_dest", "mw_amt" })
        modsItems.push_back (controls[id].get());
    for (auto id : { "osc_b_level", "osc_b_semi", "osc_b_fine", "uni_voices", "uni_detune", "flt_model",
                     "lfo1_shape", "lfo1_dest", "lfo1_rate", "lfo1_amt", "lfo2_shape", "lfo2_dest", "lfo2_rate", "lfo2_amt" })
        lfoItems.push_back (controls[id].get());
    for (auto id : { "seq_mode", "int_bpm", "seq_rate", "root", "gen_scale", "seq_len", "swing", "gate_len",
                     "gen_density", "gen_accent", "gen_slide", "gen_octave" })
        seqItems.push_back (controls[id].get());
    for (auto* c : std::initializer_list<juce::Component*> { &seqOnBtn, &generateBtn, &mutateBtn, &clearBtn,
                                                              &shiftLeftBtn, &shiftRightBtn, &grid })
        seqItems.push_back (c);

    setSize (editorW, editorH);
    buildTexture();
    // SKAZI303_UI_TAB lets the screenshot tool open a given tab.
    const auto forcedTab = juce::SystemStats::getEnvironmentVariable ("SKAZI303_UI_TAB", {});
    showTab (forcedTab.isNotEmpty() ? forcedTab.getIntValue() : (int) processor.apvts.state.getProperty ("uiTab", 0));
    startTimerHz (30);
}

void SynthPanel::showTab (int tab)
{
    currentTab = juce::jlimit (0, 4, tab);
    processor.apvts.state.setProperty ("uiTab", currentTab, nullptr);
    for (auto* c : synthItems) c->setVisible (currentTab == 0);
    for (auto* c : modsItems) c->setVisible (currentTab == 1);
    for (auto* c : fxItems) c->setVisible (currentTab == 2);
    for (auto* c : seqItems) c->setVisible (currentTab == 3);
    for (auto* c : lfoItems) c->setVisible (currentTab == 4);
    lfoTabBtn.setToggleState (currentTab == 4, juce::dontSendNotification);
    synthTabBtn.setToggleState (currentTab == 0, juce::dontSendNotification);
    modsTabBtn.setToggleState (currentTab == 1, juce::dontSendNotification);
    fxTabBtn.setToggleState (currentTab == 2, juce::dontSendNotification);
    seqTabBtn.setToggleState (currentTab == 3, juce::dontSendNotification);
    repaint();
}

SynthPanel::~SynthPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

Knob& SynthPanel::knob (const juce::String& id, const juce::String& text, bool deck, bool bipolar)
{
    auto k = std::make_unique<Knob> (processor.apvts, id, text, deck, bipolar);
    auto& ref = *k;
    addAndMakeVisible (ref);
    controls[id] = std::move (k);
    return ref;
}

Knob& SynthPanel::knobAs (const juce::String& key, const juce::String& id, const juce::String& text)
{
    auto k = std::make_unique<Knob> (processor.apvts, id, text, false);
    auto& ref = *k;
    addAndMakeVisible (ref);
    controls[key] = std::move (k);
    return ref;
}

Selector& SynthPanel::selector (const juce::String& id, const juce::String& text, bool deck)
{
    auto s = std::make_unique<Selector> (processor.apvts, id, text, deck);
    auto& ref = *s;
    addAndMakeVisible (ref);
    controls[id] = std::move (s);
    return ref;
}

void SynthPanel::placeRow (juce::Rectangle<int> area, const juce::StringArray& idList, int itemWidth)
{
    for (auto& id : idList)
        if (auto it = controls.find (id); it != controls.end())
            it->second->setBounds (area.removeFromLeft (itemWidth));
}

void SynthPanel::buildTexture()
{
    // Yellow panel with a faint scatter of black acid smileys.
    background = juce::Image (juce::Image::ARGB, contentW, editorH, true);
    {
        juce::Graphics g (background);
        g.setGradientFill (juce::ColourGradient (skin::panelTop, 0.0f, 0.0f, skin::panel, 0.0f, (float) editorH, false));
        g.fillAll();
        juce::Random rng (303);
        for (int i = 0; i < 55; ++i)
        {
            const float d = 16.0f + rng.nextFloat() * 26.0f;
            const juce::Rectangle<float> r (rng.nextFloat() * (float) contentW, rng.nextFloat() * (float) editorH, d, d);
            const auto c = r.getCentre();
            const float rad = d * 0.5f;
            g.setColour (juce::Colours::black.withAlpha (0.07f));
            g.drawEllipse (r, 1.5f);
            g.fillEllipse (c.x - rad * 0.38f, c.y - rad * 0.42f, rad * 0.16f, rad * 0.32f);
            g.fillEllipse (c.x + rad * 0.22f, c.y - rad * 0.42f, rad * 0.16f, rad * 0.32f);
            juce::Path smile;
            smile.addCentredArc (c.x, c.y, rad * 0.55f, rad * 0.5f, 0.0f,
                                 juce::MathConstants<float>::pi * 0.65f, juce::MathConstants<float>::pi * 1.35f, true);
            g.strokePath (smile, juce::PathStrokeType (1.5f));
        }
    }

    sideLeft = skin::makeSidePanel (cheekW, editorH, true);
    sideRight = skin::makeSidePanel (cheekW, editorH, false);
}

void SynthPanel::resized()
{
    // Everything is laid out in content coordinates, then shifted right past the left cheek.
    sections.clear();
    auto addSection = [this] (const juce::String& t, juce::Rectangle<int> r, int tab)
    {
        sections.push_back ({ t, r, tab });
        return r.withTrimmedTop (titleH).reduced (6, 2);
    };

    // Header: logo (painted), preset bar, tabs, activity LED.
    {
        auto bar = juce::Rectangle<int> (318, 16, 570, 36);
        presetPrevBtn.setBounds (bar.removeFromLeft (34));
        bar.removeFromLeft (4);
        presetDisplay.setBounds (bar.removeFromLeft (272));
        bar.removeFromLeft (4);
        presetNextBtn.setBounds (bar.removeFromLeft (34));
        bar.removeFromLeft (10);
        randomBtn.setBounds (bar.removeFromLeft (96));
        bar.removeFromLeft (6);
        saveBtn.setBounds (bar.removeFromLeft (70));

        // SYNTH  ENV  MOD  FX  SEQ (MOD is tab 4, so saved "uiTab" values keep their meaning)
        synthTabBtn.setBounds (868, 16, 72, 36);
        modsTabBtn.setBounds (944, 16, 60, 36);
        lfoTabBtn.setBounds (1008, 16, 62, 36);
        fxTabBtn.setBounds (1074, 16, 52, 36);
        seqTabBtn.setBounds (1130, 16, 58, 36);
        ledArea = { 1194, 34, 12, 12 };
    }

    // ---- SYNTH tab
    {
        const int y = tabTop;
        // CUTOFF and RESONANCE get the big knobs.
        auto voice = addSection ("BASS LINE", { 14, y, contentW - 28, 150 }, 0);
        for (auto [id, w] : { std::pair<const char*, int> { "wave", 130 }, { "tune", 130 }, { "cutoff", 195 },
                              { "reso", 195 }, { "envmod", 132 }, { "decay", 132 }, { "accent", 133 }, { "slide_time", 133 } })
        {
            auto cell = voice.removeFromLeft (w);
            controls[id]->setBounds (w > 150 ? cell : cell.withSizeKeepingCentre (90, cell.getHeight()));
        }

        const int y2 = y + 158;
        auto osc = addSection ("OSCILLATOR  /  SUB", { 14, y2, 360, 132 }, 0);
        placeRow (osc, { "fine", "drift", "sub_level" }, 87);
        controls["sub_wave"]->setBounds (osc.withTrimmedLeft (87 * 3).removeFromLeft (87).withSizeKeepingCentre (84, 50));

        auto f2 = addSection ("FILTER 2  /  LADDER 24 + FORMANT", { 384, y2, 480, 132 }, 0);
        controls["f2_type"]->setBounds (f2.removeFromLeft (100).withSizeKeepingCentre (96, 50));
        placeRow (f2, { "f2_cutoff", "f2_reso", "f2_vowel", "f2_mix" }, 92);

        auto out = addSection ("VCA  /  FAT TUBE  /  OUTPUT", { 874, y2, 332, 132 }, 0);
        placeRow (out, { "vca_decay", "fat", "volume" }, 106);

        // Drive gets its own full row: PRE > FILTER > POST > DRIVE 2 > OTT, then TURBO.
        const int y3 = y2 + 140;
        auto drive = addSection ("DRIVE  /  PRE > FILTER > POST > DRIVE 2 > OTT  /  TURBO", { 14, y3, contentW - 28, 108 }, 0);
        drive.removeFromLeft (6);
        for (auto [id, isSelector] : { std::pair<const char*, bool> { "pre_type", true }, { "pre_drive", false },
                                       { "post_type", true }, { "post_drive", false },
                                       { "dist_tone", false }, { "dist_mix", false },
                                       { "drive2_type", true }, { "drive2", false },
                                       { "ott_depth", false }, { "ott_time", false } })
        {
            auto cell = drive.removeFromLeft (isSelector ? 128 : 86);
            controls[id]->setBounds (isSelector ? cell.withSizeKeepingCentre (122, 50) : cell);
            drive.removeFromLeft (isSelector ? 4 : 8);
        }
        turboBtn.setBounds (drive.withSizeKeepingCentre (juce::jmin (120, drive.getWidth() - 8), 44));
    }

    // ---- MODS tab: circuit-level tweaks, like the hardware mods people did to their 303s
    {
        const int y = tabTop;
        const int half = (contentW - 38) / 2;
        auto amp = addSection ("AMP ENVELOPE  /  A  D  S  R", { 14, y, half, 196 }, 1);
        placeRow (amp.withTrimmedLeft (10), { "soft_attack", "env_vca_decay", "amp_sus", "amp_rel" }, (half - 20) / 4);
        auto fenv = addSection ("FILTER ENVELOPE  /  A  D  S  R", { 24 + half, y, half, 196 }, 1);
        placeRow (fenv.withTrimmedLeft (10), { "flt_att", "env_decay", "flt_sus", "flt_rel" }, (half - 20) / 4);

        const int y2 = y + 206;
        auto mods = addSection ("CIRCUIT MODS", { 14, y2, 560, 200 }, 1);
        placeRow (mods.withTrimmedLeft (5), { "flt_track", "acc_decay", "sweep", "pulse_width", "noise" }, 110);
        auto sub = addSection ("SUB OCTAVE", { 584, y2, 150, 200 }, 1);
        controls["sub_oct"]->setBounds (sub.withSizeKeepingCentre (130, 50));
        auto wheels = addSection ("WHEELS  /  BEND 0-24 UP + DOWN", { 744, y2, contentW - 758, 200 }, 1);
        placeRow (wheels, { "bend_up", "bend_down" }, 110);
        controls["mw_dest"]->setBounds (wheels.withTrimmedLeft (220).removeFromLeft (130).withSizeKeepingCentre (126, 50));
        controls["mw_amt"]->setBounds (wheels.withTrimmedLeft (350).removeFromLeft (110));
    }

    // ---- FX tab
    {
        const int y = tabTop;
        auto mod = addSection ("CHORUS  /  FLANGER", { 14, y, 590, 196 }, 2);
        controls["mod_mode"]->setBounds (mod.removeFromLeft (150).withSizeKeepingCentre (130, 50));
        placeRow (mod, { "mod_rate", "mod_depth", "mod_mix" }, 142);

        auto dly = addSection ("BBD DELAY  /  TEMPO SYNCED PING-PONG", { 616, y, 590, 196 }, 2);
        placeRow (dly.withTrimmedLeft (4), { "dly_sync", "dly_fb", "dly_tone", "dly_mix" }, 142);

        const int y2 = y + 206;
        auto rev = addSection ("REVERB  /  ROOM TO HALL", { 14, y2, 590, 200 }, 2);
        placeRow (rev.withTrimmedLeft (20), { "rev_size", "rev_damp", "rev_mix" }, 182);

        auto wide = addSection ("STEREO WIDENER  /  BASS STAYS MONO", { 616, y2, 590, 200 }, 2);
        controls["width"]->setBounds (wide.withSizeKeepingCentre (200, wide.getHeight()));
    }

    // ---- MOD tab: second oscillator, unison, filter model, two tempo-synced LFOs
    {
        const int y = tabTop;
        auto osc = addSection ("OSC B  /  UNISON  /  UP TO 7 VOICES", { 14, y, 780, 196 }, 4);
        placeRow (osc.withTrimmedLeft (15), { "osc_b_level", "osc_b_semi", "osc_b_fine", "uni_voices", "uni_detune" }, 150);
        auto model = addSection ("FILTER MODEL", { 804, y, contentW - 818, 196 }, 4);
        controls["flt_model"]->setBounds (model.withSizeKeepingCentre (220, 50));

        const int y2 = y + 206;
        for (int l = 0; l < 2; ++l)
        {
            const juce::String n (l + 1);
            auto lfo = addSection ("LFO " + n + "  /  TEMPO SYNCED  /  PUMP = SIDECHAIN DUCK ON VOLUME",
                                   { 14 + l * 602, y2, 590, 200 }, 4);
            lfo.removeFromLeft (8);
            controls["lfo" + n + "_shape"]->setBounds (lfo.removeFromLeft (140).withSizeKeepingCentre (130, 50));
            controls["lfo" + n + "_dest"]->setBounds (lfo.removeFromLeft (150).withSizeKeepingCentre (140, 50));
            placeRow (lfo, { "lfo" + n + "_rate", "lfo" + n + "_amt" }, 140);
        }
    }

    // ---- SEQUENCER tab
    {
        auto seq = addSection ("STEP SEQUENCER  /  PATTERN GENERATOR", { 14, tabTop, contentW - 28, 406 }, 3);
        auto ctl = seq.removeFromTop (66);

        seqOnBtn.setBounds (ctl.removeFromLeft (112).withSizeKeepingCentre (106, 34));
        ctl.removeFromLeft (6);
        controls["seq_mode"]->setBounds (ctl.removeFromLeft (100).withSizeKeepingCentre (100, 50));
        controls["int_bpm"]->setBounds (ctl.removeFromLeft (52));
        for (auto id : { "seq_rate", "root", "gen_scale" })
        {
            const int w = juce::String (id) == "gen_scale" ? 116 : 78;
            controls[id]->setBounds (ctl.removeFromLeft (w).withSizeKeepingCentre (w, 50));
        }
        ctl.removeFromLeft (4);
        placeRow (ctl, { "seq_len", "swing", "gate_len", "gen_density", "gen_accent", "gen_slide", "gen_octave" }, 52);
        ctl.removeFromLeft (52 * 7 + 6);

        auto buttons = ctl.withSizeKeepingCentre (ctl.getWidth(), 32);
        generateBtn.setBounds (buttons.removeFromLeft (86).reduced (2, 0));
        mutateBtn.setBounds (buttons.removeFromLeft (66).reduced (2, 0));
        shiftLeftBtn.setBounds (buttons.removeFromLeft (28).reduced (2, 0));
        shiftRightBtn.setBounds (buttons.removeFromLeft (28).reduced (2, 0));
        clearBtn.setBounds (buttons.removeFromLeft (58).reduced (2, 0));

        seq.removeFromTop (6);
        grid.setBounds (seq);
    }

    // Keyboard in a black slot along the bottom, visible on both tabs.
    wheelFrame = { 14, editorH - keyboardH - 14, 64, keyboardH };
    pitchWheel.setBounds (wheelFrame.reduced (6, 5).removeFromLeft (24));
    modWheel.setBounds (wheelFrame.reduced (6, 5).removeFromRight (24));
    keyboardFrame = { 86, editorH - keyboardH - 14, contentW - 100, keyboardH };
    keyboard.setBounds (keyboardFrame.reduced (6, 5));
    keyboard.setKeyWidth ((float) (keyboardFrame.getWidth() - 12) / 36.0f); // C2..C7 = 36 white keys
    keyboard.setLowestVisibleKey (36);

    for (auto* child : getChildren())
        child->setTopLeftPosition (child->getPosition().translated (cheekW, 0));
}

void SynthPanel::stepPreset (int delta)
{
    const int n = processor.getNumPrograms();
    processor.selectPreset ((processor.getCurrentProgram() + delta + n) % n);
}

void SynthPanel::showPresetMenu()
{
    static constexpr int userBase = 100000;
    juce::PopupMenu menu;
    const auto& templates = s303::presetTemplates();

    for (int c = 0; c < (int) templates.size(); ++c)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < s303::kPresetsPerCategory; ++i)
        {
            const int index = c * s303::kPresetsPerCategory + i;
            const auto name = processor.getProgramName (index).fromFirstOccurrenceOf (" - ", false, false);
            sub.addItem (index + 1, name, true, index == processor.getCurrentProgram());
        }
        menu.addSubMenu (juce::String (templates[(size_t) c].getCategory()), sub);
    }

    const auto userFiles = Skazi303Processor::getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.xml");
    juce::PopupMenu user;
    for (int i = 0; i < userFiles.size(); ++i)
        user.addItem (userBase + i, userFiles[i].getFileNameWithoutExtension());
    if (userFiles.isEmpty())
        user.addItem (-1, "(none saved yet)", false);
    menu.addSeparator();
    menu.addSubMenu ("User", user);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetDisplay),
                        [this, userFiles] (int result)
                        {
                            if (result >= userBase && result - userBase < userFiles.size())
                                processor.loadUserPreset (userFiles[result - userBase]);
                            else if (result > 0 && result < userBase)
                                processor.selectPreset (result - 1);
                        });
}

void SynthPanel::timerCallback()
{
    presetDisplay.setText (processor.getPresetDisplayName());

    // Show MIDI wheel moves on the on-screen wheels (not while the mouse is holding them)
    if (! pitchWheel.isMouseButtonDown() && std::abs (pitchWheel.getValue() - processor.wheelBend.load()) > 0.002)
        pitchWheel.setValue (processor.wheelBend.load(), juce::dontSendNotification);
    if (! modWheel.isMouseButtonDown() && std::abs (modWheel.getValue() - processor.wheelMod.load()) > 0.002)
        modWheel.setValue (processor.wheelMod.load(), juce::dontSendNotification);

    const float a = processor.getFilterActivity();
    if (std::abs (a - shownActivity) > 0.01f)
    {
        shownActivity = a;
        repaint (ledArea.translated (cheekW, 0).expanded (8));
    }
}

void SynthPanel::paint (juce::Graphics& g)
{
    using namespace skin;

    g.drawImageAt (sideLeft, 0, 0);
    g.drawImageAt (sideRight, editorW - cheekW, 0);

    juce::Graphics::ScopedSaveState state (g);
    g.setOrigin (cheekW, 0);
    g.reduceClipRegion (0, 0, contentW, editorH);
    g.drawImageAt (background, 0, 0);

    // Screws
    for (auto pt : { juce::Point<float> (10, 10), juce::Point<float> ((float) contentW - 10, 10),
                     juce::Point<float> (10, (float) editorH - 10), juce::Point<float> ((float) contentW - 10, (float) editorH - 10) })
    {
        g.setColour (juce::Colour (0xff2a2a2a));
        g.fillEllipse (pt.x - 4.5f, pt.y - 4.5f, 9.0f, 9.0f);
        g.setColour (juce::Colour (0xff8a8a8a));
        g.drawLine (pt.x - 3.0f, pt.y - 3.0f, pt.x + 3.0f, pt.y + 3.0f, 1.2f);
    }

    // Logo: the melting-smiley hazard mark, the name in heavy sign type, and the byline.
    {
        drawAcidMark (g, { 18.0f, 4.0f, 64.0f, 58.0f });
        auto r = juce::Rectangle<int> (90, 0, 220, 46);
        g.setColour (cream);
        g.setFont (heavyFont (40.0f));
        g.drawText ("TBON", r.removeFromLeft (128), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xffe8008f));
        g.drawText ("303", r, juce::Justification::centredLeft);

        g.setColour (cream);
        g.setFont (labelFont (15.5f).withExtraKerningFactor (0.2f));
        g.drawText ("BY ASHER SWISSA", juce::Rectangle<int> (92, 42, 220, 20), juce::Justification::centredLeft);
    }

    // Black legend strip under the header, hazard stripes at both ends.
    {
        const juce::Rectangle<float> strip (14.0f, 66.0f, (float) contentW - 28.0f, 22.0f);
        g.setColour (panelLine);
        g.fillRoundedRectangle (strip, 3.0f);
        fillHazard (g, strip.withWidth (120.0f).reduced (3.0f), 9.0f);
        fillHazard (g, strip.withTrimmedLeft (strip.getWidth() - 120.0f).reduced (3.0f), 9.0f);
        g.setColour (panel);
        g.setFont (labelFont (13.0f));
        g.drawText (currentTab == 0 ? "ACID BASS LINE  /  DIODE LADDER  /  ACCENT  /  SLIDE  /  14 DRIVE TYPES"
                    : currentTab == 1 ? "AMP + FILTER ENVELOPES (ADSR)  /  CIRCUIT MODS  /  TRACKING  /  ACCENT  /  SWEEP  /  PULSE  /  NOISE  /  SUB"
                    : currentTab == 4 ? "OSC B  /  UNISON  /  DIODE OR TRANSISTOR FILTER  /  2 LFOS  /  PUMP  /  MIDI CC 74 71 76 75 77 78"
                    : currentTab == 2 ? "EFFECTS  /  CHORUS  /  FLANGER  /  BBD DELAY  /  REVERB  /  STEREO WIDENER"
                                      : "COMPUTER CONTROLLED  /  16 STEPS  /  PITCH  /  ACCENT  /  SLIDE  /  GENERATOR",
                    strip, juce::Justification::centred);
    }

    // Activity LED pulses with the filter envelope.
    {
        const auto r = ledArea.toFloat();
        const float a = juce::jlimit (0.0f, 1.0f, shownActivity);
        g.setColour (ledRed.withAlpha (0.45f * a));
        g.fillEllipse (r.expanded (5.0f * a));
        g.setColour (juce::Colour (0xff4a1a3c).interpolatedWith (ledRed, a));
        g.fillEllipse (r);
        g.setColour (creamSoft);
        g.setFont (labelFont (10.0f));
        g.drawText ("ENV", ledArea.translated (0, -14).expanded (10, 0), juce::Justification::centred);
    }

    // Section frames: thick black sign borders with black title tabs.
    for (auto& s : sections)
    {
        if (s.tab != currentTab)
            continue;

        auto r = s.bounds.toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (panelLine);
        g.drawRoundedRectangle (r, 9.0f, 3.0f);

        auto t = r.removeFromTop ((float) titleH).reduced (12.0f, 0.0f).translated (0.0f, 3.0f);
        g.setFont (labelFont (12.5f));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s.title) + 16.0f;
        g.setColour (panelLine);
        g.fillRoundedRectangle (t.withWidth (tw).reduced (0.0f, 1.0f), 3.0f);
        g.setColour (panel);
        g.drawText (s.title, t.withWidth (tw), juce::Justification::centred);
    }

    // Keyboard slot
    g.setColour (panelLine);
    g.fillRoundedRectangle (keyboardFrame.toFloat().expanded (2.0f), 7.0f);
    g.fillRoundedRectangle (wheelFrame.toFloat().expanded (2.0f), 7.0f);
}

//==============================================================================
Skazi303Editor::Skazi303Editor (Skazi303Processor& p)
    : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    setResizable (true, true);
    setResizeLimits (editorW / 2, editorH / 2, editorW * 3 / 2, editorH * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) editorW / (double) editorH);
    setSize (editorW * 3 / 4, editorH * 3 / 4); // fits a 1080p laptop screen; drag the corner to resize
}

void Skazi303Editor::resized()
{
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) editorW));
}
