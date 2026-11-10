#include "PluginEditor.h"
#include "BinaryData.h"

using namespace skazon;

namespace
{
    // Which matrix target a knob's parameter feeds; 0 = not a drop target.
    int targetFor (const juce::String& id)
    {
        static const std::map<juce::String, int> map {
            { "aPos", tAPos }, { "bPos", tBPos }, { "gPos", tGPos }, { "aLevel", tALevel }, { "bLevel", tBLevel }, { "gLevel", tGLevel },
            { "gSize", tGSize }, { "gDens", tGDensity }, { "gSpray", tGSpray }, { "fCut", tCutoff }, { "fRes", tRes }, { "fDrive", tDrive },
            { "fEnv", tFEnv }, { "l1Rate", tLfo1Rate }, { "l2Rate", tLfo2Rate }, { "l3Rate", tLfo3Rate }, { "l4Rate", tLfo4Rate },
            { "aWarpAmt", tAWarp }, { "bWarpAmt", tBWarp }, { "aDet", tADetune }, { "bDet", tBDetune }, { "sub", tSub }, { "noise", tNoise },
            { "aSemi", tAPitch }, { "bSemi", tBPitch },
        };
        const auto it = map.find (id);
        return it != map.end() ? it->second : 0;
    }

    bool unipolar (int src)
    {
        return src == sEnv1 || src == sEnv2 || src == sVelocity || src == sWheel || src == sAftertouch || (src >= sMacro1 && src <= sMacro4);
    }

    juce::String slotID (int slot, const char* field) { return "m" + juce::String (slot) + field; }
}

//==============================================================================
WaveDisplay::WaveDisplay (SkazonAudioProcessor& p, const juce::String& prefixToUse, int liveIndex, juce::Colour accentColour)
    : proc (p), prefix (prefixToUse), live (liveIndex), accent (accentColour)
{
    setTooltip ("Drop a WAV here (or double-click) to make a USER wavetable");
}

void WaveDisplay::tick()
{
    const auto q = [] (float v) { return (juce::int64) juce::roundToInt (v * 400.0f) + 500; };
    const bool osc = prefix != "g";
    juce::int64 state = (juce::int64) proc.param (prefix + "Table") * 3 + (proc.param (prefix + "On") > 0.5f ? 1 : 0) + (dropHover ? 2 : 0);
    state = state * 1000 + q (proc.param (prefix + "Pos"));
    state = state * 1000 + q (proc.livePos[(size_t) live].load());
    state = state * 1000 + q (osc ? proc.param (prefix + "WarpAmt") + proc.liveMods[(size_t) (prefix == "a" ? tAWarp : tBWarp)].load() : proc.param ("gSpray"));
    state = state * 16 + (osc ? (juce::int64) proc.param (prefix + "Warp") : 0);
    state ^= (juce::int64) (juce::pointer_sized_int) proc.currentUserTable().get();
    if (state != shown)
    {
        shown = state;
        repaint();
    }
}

// One frame drawn into the stack: depth 0 is the front (first frame), 1 the back.
juce::Path WaveDisplay::framePath (const float* base, float pos, juce::Rectangle<float> r, float depth, int warp, float warpAmt) const
{
    const float w = r.getWidth() * 0.74f, amp = r.getHeight() * 0.2f;
    const float x0 = r.getX() + 10.0f + depth * r.getWidth() * 0.2f;
    const float y0 = r.getBottom() - r.getHeight() * 0.27f - depth * r.getHeight() * 0.42f;
    juce::Path p;
    constexpr int points = 140;
    for (int i = 0; i <= points; ++i)
    {
        const float x = juce::jmin ((float) i / (float) points, 0.9999f);
        const float other = std::sin (juce::MathConstants<float>::twoPi * x); // stand-in for the other osc in FM / RM
        const float v = warpOutput (warp, warpAmt, readFrames (base, pos, 2, warpPhase (warp, warpAmt, x, other)), other);
        if (i == 0) p.startNewSubPath (x0 + x * w, y0 - amp * v);
        else        p.lineTo (x0 + x * w, y0 - amp * v);
    }
    return p;
}

void WaveDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Theme::background.withAlpha (0.75f));
    g.fillRect (r);
    g.setColour (Theme::edge.withAlpha (0.5f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY(), r.getBottom());
    g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());

    const bool osc = prefix != "g";
    const int table = (int) proc.param (prefix + "Table");
    const float pos = proc.param (prefix + "Pos");
    const float livePos = proc.livePos[(size_t) live].load();
    const bool on = proc.param (prefix + "On") > 0.5f;
    const float spray = osc ? 0.0f : proc.param ("gSpray");
    const int warp = osc ? (int) proc.param (prefix + "Warp") : 0;
    const float warpAmt = osc ? proc.param (prefix + "WarpAmt") : 0.0f;
    const float liveWarp = juce::jlimit (0.0f, 1.0f, warpAmt + (osc ? proc.liveMods[(size_t) (prefix == "a" ? tAWarp : tBWarp)].load() : 0.0f));
    const auto user = proc.currentUserTable();
    const float* base = tableBase (table, user.get());

    for (int f = numFrames - 1; f >= 0; --f)
    {
        const float fp = (float) f / (float) (numFrames - 1);
        const bool inSpray = spray > 0.0f && std::abs (fp - pos) <= spray * 0.5f;
        g.setColour (accent.withAlpha (inSpray ? 0.42f : 0.13f));
        g.strokePath (framePath (base, fp, r, fp, wNone, 0.0f), juce::PathStrokeType (1.0f));
    }

    Theme::glowStroke (g, framePath (base, pos, r, pos, warp, warpAmt), accent, 1.6f);
    if (livePos >= 0.0f && (std::abs (livePos - pos) > 0.002f || std::abs (liveWarp - warpAmt) > 0.002f))
        Theme::glowStroke (g, framePath (base, livePos, r, livePos, warp, liveWarp), Theme::text, 1.2f, 0.8f);

    g.setFont (Theme::mono (11.0f, true));
    g.setColour (accent);
    const auto name = table == userTableIndex ? "USER: " + (user != nullptr ? user->name.toUpperCase() : juce::String ("DROP AUDIO HERE")) : tableNames()[table];
    g.drawText (name + (warp != wNone ? "  //  " + warpNames()[warp] : juce::String()), r.reduced (8.0f, 5.0f), juce::Justification::topLeft);
    g.setColour (Theme::text);
    g.drawText ("POS " + juce::String (juce::roundToInt ((livePos >= 0.0f ? livePos : pos) * 100.0f)) + "%", r.reduced (8.0f, 5.0f), juce::Justification::topRight);

    if (! on || dropHover)
    {
        g.setColour (Theme::background.withAlpha (dropHover ? 0.8f : 0.65f));
        g.fillRect (r);
        g.setColour (dropHover ? Theme::lime : Theme::dim.brighter (0.4f));
        g.setFont (Theme::sans (dropHover ? 16.0f : 20.0f, true).withExtraKerningFactor (0.2f));
        g.drawText (dropHover ? "DROP TO MAKE A WAVETABLE" : "OFF", r, juce::Justification::centred);
    }
}

bool WaveDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    return std::any_of (files.begin(), files.end(), SkazonAudioProcessor::isAudioFile);
}

void WaveDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    if (const auto f = std::find_if (files.begin(), files.end(), SkazonAudioProcessor::isAudioFile); f != files.end())
        load (juce::File (*f));
}

void WaveDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    chooser = std::make_unique<juce::FileChooser> ("Audio to wavetable", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load (fc.getResult()); });
}

void WaveDisplay::load (const juce::File& file)
{
    if (! proc.loadUserTable (file, prefix))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SKAZON", "Could not read " + file.getFileName());
    shown = -1;
    tick();
}

//==============================================================================
LfoDisplay::LfoDisplay (SkazonAudioProcessor& p, int i) : proc (p), index (i) { setInterceptsMouseClicks (false, false); }

void LfoDisplay::tick()
{
    const juce::String l ("l" + juce::String (index + 1));
    const juce::int64 state = (juce::int64) proc.param (l + "Shape") * 100000000ll + (proc.param (l + "Sync") > 0.5f ? 50000000ll : 0)
                            + (juce::int64) proc.param (l + "Div") * 1000000ll + juce::roundToInt (proc.param (l + "Rate") * 100.0f) * 1000ll
                            + juce::roundToInt (proc.liveLfo[(size_t) index].load() * 300.0f) + 300;
    if (state != shown)
    {
        shown = state;
        repaint();
    }
}

void LfoDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Theme::background.withAlpha (0.75f));
    g.fillRect (r);
    g.setColour (Theme::edge.withAlpha (0.5f));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) i / 4.0f), r.getY(), r.getBottom());
    g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());

    const juce::String l ("l" + juce::String (index + 1));
    const int shape = (int) proc.param (l + "Shape");
    const bool random = shape == 4 || shape == 6;
    const float holds[] { 0.6f, -0.3f, 0.9f, -0.8f, 0.2f };
    const auto area = r.reduced (6.0f, 16.0f);
    auto valueAt = [&] (float x)
    {
        if (! random)
            return lfoShape (shape, x, 0, 0);
        const int step = juce::jmin (3, (int) (x * 4.0f));
        return lfoShape (shape, x * 4.0f - (float) step, holds[step + 1], holds[step]);
    };

    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = juce::jmin ((float) i / 200.0f, 0.9999f);
        const float y = area.getCentreY() - valueAt (x) * area.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath (area.getX() + x * area.getWidth(), y);
        else        p.lineTo (area.getX() + x * area.getWidth(), y);
    }
    Theme::glowStroke (g, p, Theme::cyan, 1.6f);

    if (const float ph = proc.liveLfo[(size_t) index].load(); ph >= 0.0f)
    {
        const float x = random ? ph * 0.25f : ph;
        Theme::glowDot (g, { area.getX() + x * area.getWidth(), area.getCentreY() - valueAt (x) * area.getHeight() * 0.5f }, 3.0f, Theme::text);
    }

    g.setFont (Theme::mono (10.0f, true));
    g.setColour (Theme::cyan);
    g.drawText (lfoShapeNames()[shape], r.reduced (6.0f, 3.0f), juce::Justification::topLeft);
    g.setColour (Theme::text);
    g.drawText (proc.param (l + "Sync") > 0.5f ? divNames()[(int) proc.param (l + "Div")] : juce::String (proc.param (l + "Rate"), 2) + " HZ",
                r.reduced (6.0f, 3.0f), juce::Justification::topRight);
}

//==============================================================================
FxScope::FxScope (SkazonAudioProcessor& p, Mode m) : proc (p), mode (m) { setInterceptsMouseClicks (false, false); }

void FxScope::tick()
{
    juce::int64 state;
    if (mode == distortion)
        state = (juce::int64) proc.param ("distType") * 100000 + juce::roundToInt (proc.param ("fxDrive") * 1000.0f) * 10 + (proc.param ("distOn") > 0.5f ? 1 : 0);
    else
        state = juce::roundToInt (proc.fx.ottGainDb[0].load() * 2.0f) * 10000ll + juce::roundToInt (proc.fx.ottGainDb[1].load() * 2.0f) * 100ll
              + juce::roundToInt (proc.fx.ottGainDb[2].load() * 2.0f) + (juce::int64) (proc.param ("ottDepth") * 100.0f) * 1000000ll;
    if (state != shown)
    {
        shown = state;
        repaint();
    }
}

void FxScope::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Theme::background.withAlpha (0.75f));
    g.fillRect (r);
    g.setColour (Theme::edge.withAlpha (0.5f));
    g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());
    g.setFont (Theme::mono (10.0f, true));

    if (mode == distortion)
    {
        g.drawVerticalLine (juce::roundToInt (r.getCentreX()), r.getY(), r.getBottom());
        const int type = (int) proc.param ("distType");
        const float drive = proc.param ("fxDrive");
        const auto area = r.reduced (8.0f);
        juce::Path curve;
        for (int i = 0; i <= 160; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 160.0f;
            const float y = juce::jlimit (-1.2f, 1.2f, drive > 0.0f ? FxRack::transfer (type, drive, x) : x);
            const float px = area.getX() + (x + 1.0f) * 0.5f * area.getWidth(), py = area.getCentreY() - y * area.getHeight() * 0.42f;
            if (i == 0) curve.startNewSubPath (px, py);
            else        curve.lineTo (px, py);
        }
        Theme::glowStroke (g, curve, Theme::red, 1.6f, proc.param ("distOn") > 0.5f ? 1.0f : 0.35f);
        g.setColour (Theme::red);
        g.drawText (distNames()[type] + "  //  TRANSFER CURVE", r.reduced (8.0f, 4.0f), juce::Justification::topLeft);
        return;
    }

    const char* names[] { "LOW", "MID", "HIGH" };
    const float w = r.getWidth() / 3.0f;
    for (int b = 0; b < 3; ++b)
    {
        const float db = proc.param ("ottDepth") > 0.0f ? proc.fx.ottGainDb[(size_t) b].load() : 0.0f;
        const auto col = juce::Rectangle<float> (r.getX() + w * (float) b, r.getY(), w, r.getHeight()).reduced (14.0f, 18.0f);
        const float h = juce::jlimit (-1.0f, 1.0f, db / 24.0f) * col.getHeight() * 0.5f;
        const auto bar = h >= 0 ? col.withTop (col.getCentreY() - h).withBottom (col.getCentreY()) : col.withTop (col.getCentreY()).withBottom (col.getCentreY() - h);
        g.setColour ((h >= 0 ? Theme::lime : Theme::magenta).withAlpha (0.8f));
        g.fillRect (bar);
        g.setColour (Theme::text);
        g.drawText (juce::String (names[b]) + "  " + (db >= 0 ? "+" : "") + juce::String (db, 1) + " DB", col.withY (r.getBottom() - 16.0f).withHeight (14.0f), juce::Justification::centred);
    }
    g.setColour (Theme::lime);
    g.drawText ("UPWARD  /  DOWNWARD GAIN PER BAND", r.reduced (8.0f, 4.0f), juce::Justification::topLeft);
}

//==============================================================================
ModChip::ModChip (int src, const juce::String& text, juce::Colour c) : source (src), label (text), accent (c)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag onto any knob to modulate it");
}

void ModChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool hot = isMouseOver();
    g.setColour (accent.withAlpha (hot ? 0.3f : 0.14f));
    g.fillRect (r);
    juce::Path outline;
    outline.addRectangle (r);
    Theme::glowStroke (g, outline, accent, 1.0f, hot ? 0.9f : 0.5f);

    // Diamond grip.
    juce::Path grip;
    const auto c = juce::Point<float> (r.getX() + 9.0f, r.getCentreY());
    grip.addQuadrilateral (c.x, c.y - 5.0f, c.x + 5.0f, c.y, c.x, c.y + 5.0f, c.x - 5.0f, c.y);
    g.setColour (accent);
    g.fillPath (grip);
    g.setColour (Theme::text);
    g.setFont (Theme::mono (10.0f, true));
    g.drawText (label, r.withTrimmedLeft (16.0f), juce::Justification::centred);
}

void ModChip::mouseDrag (const juce::MouseEvent&)
{
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
        container->startDragging ("mod:" + juce::String (source), this);
}

//==============================================================================
TargetKnob::TargetKnob (const juce::String& text, juce::Colour c, bool bipolar, int t) : NeonKnob (text, c, bipolar), target (t) {}

bool TargetKnob::isInterestedInDragSource (const SourceDetails& d)
{
    const auto desc = d.description.toString();
    if (! desc.startsWith ("mod:") || paramID.startsWith ("macro"))
        return false;
    const int src = desc.fromFirstOccurrenceOf ("mod:", false, false).getIntValue();
    return target > 0 || (src >= sMacro1 && src <= sMacro4); // macros link to every knob, other sources need a matrix target
}

void TargetKnob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    if (onDrop)
        onDrop (d.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue());
    repaint();
}

void TargetKnob::mouseDown (const juce::MouseEvent& e)
{
    if (onRemoveMenu != nullptr && e.mods.isPopupMenu())
    {
        if (onRemoveMenu) onRemoveMenu();
        return;
    }
    if (onDepthDrag != nullptr && e.mods.isShiftDown())
    {
        depthDragging = true;
        lastY = e.position.y;
        return;
    }
    NeonKnob::mouseDown (e);
}

void TargetKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! depthDragging)
        return NeonKnob::mouseDrag (e);
    if (onDepthDrag)
        onDepthDrag ((lastY - e.position.y) / 150.0f);
    lastY = e.position.y;
}

void TargetKnob::mouseUp (const juce::MouseEvent& e)
{
    if (! depthDragging)
        return NeonKnob::mouseUp (e);
    depthDragging = false;
}

//==============================================================================
SkazonAudioProcessorEditor::SkazonAudioProcessorEditor (SkazonAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      logo (juce::ImageCache::getFromMemory (BinaryData::skazi_logo_png, BinaryData::skazi_logo_pngSize).createCopy()),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // The logo ships on solid black; turn brightness into alpha so it floats on the header.
    logo = logo.convertedToFormat (juce::Image::ARGB);
    {
        juce::Image::BitmapData pixels (logo, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < logo.getHeight(); ++y)
            for (int x = 0; x < logo.getWidth(); ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                const float level = juce::jmax (c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue());
                if (level < 0.999f)
                    pixels.setPixelColour (x, y, level <= 0.0f ? juce::Colours::transparentBlack
                                                               : juce::Colour::fromFloatRGBA (c.getFloatRed() / level, c.getFloatGreen() / level, c.getFloatBlue() / level,
                                                                                              level * c.getFloatAlpha()));
            }
    }

    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);

    // Header: page tabs, MAGIC dice, presets.
    const char* tabNames[] { "OSC", "MOD", "FX" };
    for (int i = 0; i < numPages; ++i)
    {
        tabs[i] = std::make_unique<NeonToggle> (tabNames[i], Theme::lime);
        tabs[i]->setClickingTogglesState (false);
        tabs[i]->onClick = [this, i] { showPage (i); };
        canvas.addAndMakeVisible (*tabs[i]);
        tabs[i]->setBounds (330 + i * 60, 12, 56, 40);
    }

    dice.setTooltip ("MAGIC: a brand-new random preset in this category");
    dice.onClick = [this] { proc.randomize(); dice.flash = 1.0f; };
    diceMenu.setTooltip ("Pick the category for the MAGIC dice");
    diceMenu.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("MAGIC RANDOM PRESET");
        for (int i = 0; i < generatedCategories().size(); ++i)
            menu.addItem (i + 1, generatedCategories()[i]);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&diceMenu),
                            [this] (int r) { if (r > 0) { proc.randomize (r - 1); dice.flash = 1.0f; } });
    };

    // One submenu per category ("BASS // Name" goes under BASS); item id = preset index + 1.
    {
        const auto names = SkazonAudioProcessor::presetNames();
        juce::StringArray categories;
        std::vector<juce::PopupMenu> menus;
        for (int i = 0; i < names.size(); ++i)
        {
            const auto cat = names[i].contains (" // ") ? names[i].upToFirstOccurrenceOf (" // ", false, false) : juce::String ("FACTORY");
            if (! categories.contains (cat))
            {
                categories.add (cat);
                menus.emplace_back();
            }
            menus[(size_t) categories.indexOf (cat)].addItem (i + 1, names[i]);
        }
        for (int c = 0; c < categories.size(); ++c)
            presetBox.getRootMenu()->addSubMenu (categories[c] + "  (" + juce::String (menus[(size_t) c].getNumItems()) + ")", menus[(size_t) c]);
    }
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    prevPreset.onClick = [this] { presetBox.setSelectedId ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms() + 1); };
    nextPreset.onClick = [this] { presetBox.setSelectedId ((proc.getCurrentProgram() + 1) % proc.getNumPrograms() + 1); };
    matchButton.setTooltip ("SOUND MATCH: load (or drop anywhere) a lead, bass or vocal (one note or a whole line): SKAZON rebuilds the sound AND converts the line to MIDI");
    matchButton.onClick = [this]
    {
        matchChooser = std::make_unique<juce::FileChooser> ("Sound match: pick a recording (one note or a mono line)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        matchChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) runMatch (fc.getResult()); });
    };
    midiButton.setTooltip ("MIDI from SOUND MATCH: click = play the line on this patch, drag = drop the .mid on a track in your DAW");
    midiButton.onClick = [this] { proc.setPreview (! proc.isPreviewing()); };
    midiButton.onDragOut = [this]
    {
        proc.setPreview (false);
        const auto file = proc.writeMatchMidi();
        if (file.existsAsFile())
            performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    };
    for (NeonButton* b : { &matchButton, (NeonButton*) &midiButton, &mutateButton, &dice })
        b->filled = true;
    matchButton.subText = "DROP AUDIO / CLICK";
    midiButton.subText = "PLAY / DRAG";
    mutateButton.subText = "VARIATION";
    dice.setButtonText ("RANDOM");
    mutateButton.setTooltip ("MUTATE: a variation of this patch. Pick the amount and lock what you want to keep");
    mutateButton.onClick = [this] { showMutateMenu(); };

    for (auto [c, b] : { std::pair<juce::Component*, juce::Rectangle<int>> { &matchButton, { 516, 8, 150, 48 } }, { &midiButton, { 670, 8, 84, 48 } },
                         { &mutateButton, { 758, 8, 92, 48 } }, { &dice, { 854, 8, 104, 48 } }, { &diceMenu, { 960, 14, 22, 36 } },
                         { &prevPreset, { 988, 14, 26, 36 } }, { &presetBox, { 1016, 14, 180, 36 } }, { &nextPreset, { 1198, 14, 26, 36 } } })
    {
        canvas.addAndMakeVisible (c);
        c->setBounds (b);
    }

    building = 0; buildOscPage();
    building = 1; buildModPage();
    building = 2; buildFxPage();
    building = -1;

    // Mod sources on every page: drag one onto any knob.
    label ("MOD SOURCES  >>", { 22, 738, 176, 38 }, Theme::magenta);
    {
        const std::tuple<int, const char*, juce::Colour> chips[] {
            { sLfo1, "LFO 1", Theme::cyan }, { sLfo2, "LFO 2", Theme::cyan }, { sLfo3, "LFO 3", Theme::cyan }, { sLfo4, "LFO 4", Theme::cyan },
            { sEnv2, "MOD ENV", Theme::lime }, { sEnv1, "AMP ENV", Theme::lime },
            { sMacro1, "MACRO 1", Theme::amber }, { sMacro2, "MACRO 2", Theme::amber }, { sMacro3, "MACRO 3", Theme::amber }, { sMacro4, "MACRO 4", Theme::amber },
            { sVelocity, "VELO", Theme::magenta }, { sNote, "NOTE", Theme::magenta }, { sRandom, "RANDOM", Theme::magenta },
            { sWheel, "WHEEL", Theme::magenta }, { sAftertouch, "PRESS", Theme::magenta } };
        int x = 200;
        for (auto [src, text, colour] : chips)
        {
            place (std::make_unique<ModChip> (src, text, colour), { x, 742, 64, 30 });
            x += 68;
        }
    }

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, Theme::panelHi);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, Theme::background);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::edge);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::lime.withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::magenta.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setAvailableRange (36, 96);
    keyboard.setKeyWidth (1208.0f / 36.0f); // 36 white keys fill the strip
    canvas.addAndMakeVisible (keyboard);
    keyboard.setKeyWidth (904.0f / 36.0f);
    keyboard.setBounds (320, 784, 904, 66);

    pitchWheel.setTooltip ("PITCH WHEEL: drag up / down (SHIFT = fine), springs back. How far = BEND");
    modWheel.setTooltip ("MOD WHEEL: drag up / down (SHIFT = fine), double-click = 0. What it does = the 3 boxes on its right");
    pitchWheel.readout = [this] (float v) { const float st = v * proc.param ("bendRange"); return (st > 0 ? "+" : "") + juce::String (st, 1); };
    modWheel.readout = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
    pitchWheel.onMove = [this] (float v) { proc.uiBend = juce::jlimit (0, 16383, juce::roundToInt (8192.0f + v * 8191.0f)); };
    modWheel.onMove = [this] (float v) { proc.uiModWheel = juce::roundToInt (v * 127.0f); };
    canvas.addAndMakeVisible (pitchWheel);
    canvas.addAndMakeVisible (modWheel);
    pitchWheel.setBounds (16, 784, 42, 66);
    modWheel.setBounds (122, 784, 42, 66);
    {
        building = -1;
        // PITCH: wheel + its range.
        auto& box = place (std::make_unique<juce::ComboBox>(), { 60, 784, 58, 26 });
        for (int st = 0; st <= 24; ++st)
            box.addItem (juce::String::charToString ((juce::juce_wchar) 0xb1) + juce::String (st), st + 1);
        box.setTooltip ("BEND RANGE: how far the pitch wheel goes up AND down, in semitones (24 = two octaves)");
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "bendRange", box));
        label ("BEND ST", { 62, 812, 56, 14 }, Theme::cyan);
        label ("UP + DOWN", { 62, 828, 56, 14 }, Theme::dim.brighter (0.5f));

        // MOD: wheel -> which LFO (or DIRECT) -> which target, how much. Items in parameter order (the attachment maps by index).
        auto& lfo = place (std::make_unique<juce::ComboBox>(), { 168, 784, 148, 22 });
        lfo.addItemList ({ "WHEEL DIRECT", "WHEEL x LFO 1", "WHEEL x LFO 2", "WHEEL x LFO 3", "WHEEL x LFO 4" }, 1);
        lfo.setTooltip ("What the mod wheel turns up: an LFO's depth (LFO 1 on PITCH = vibrato, on CUTOFF = wobble) or the target directly");
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mwLfo", lfo));
        auto& dst = place (std::make_unique<juce::ComboBox>(), { 168, 808, 148, 22 });
        dst.addItem ("-> OFF", 1);
        for (int t = 1; t < targetNames().size(); ++t)
            dst.addItem ("-> " + targetNames()[t], t + 1);
        dst.setTooltip ("Where the mod wheel goes");
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mwDst", dst));
        auto& amt = place (std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox), { 168, 832, 148, 18 });
        amt.setColour (juce::Slider::trackColourId, Theme::magenta);
        amt.setTooltip ("Mod wheel depth (center = 0, left = inverted). Double-click = vibrato default");
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "mwAmt", amt));
        amt.setDoubleClickReturnValue (true, 0.03);
    }

    canvas.setBounds (0, 0, designWidth, designHeight);
    addChildComponent (dropOverlay);
    showPage (proc.uiPage);

    float initialScale = 1.0f;
    if (auto* screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        initialScale = juce::jmin (1.0f, (float) screen->userArea.getHeight() * 0.85f / (float) designHeight);

    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (juce::roundToInt ((float) designWidth * initialScale), juce::roundToInt ((float) designHeight * initialScale));

    startTimerHz (30);
}

SkazonAudioProcessorEditor::~SkazonAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void SkazonAudioProcessorEditor::buildOscPage()
{
    struct Src { const char* prefix; const char* index; const char* title; juce::Colour accent; int x, w, live; };
    for (const auto& s : { Src { "a", "01", "OSC A // WAVETABLE", Theme::lime, 16, 400, 0 },
                           Src { "b", "02", "OSC B // WAVETABLE", Theme::cyan, 424, 400, 1 },
                           Src { "g", "03", "GRAIN CLOUD // WAVETABLE GRANULAR", Theme::magenta, 832, 392, 2 } })
    {
        const juce::String p (s.prefix);
        panel ({ s.x, 72, s.w, 304 }, s.index, s.title, s.accent);
        toggle (p + "On", "ON", s.accent, { s.x + 10, 112, 62, 26 });
        if (p == "g")
            combo ("gTable", { s.x + 76, 112, s.w - 86, 26 });
        else
        {
            combo (p + "Table", { s.x + 76, 112, 156, 26 });
            combo (p + "Warp", { s.x + 236, 112, s.w - 246, 26 });
        }
        waves.push_back (&place (std::make_unique<WaveDisplay> (proc, p, s.live, s.accent), { s.x + 10, 144, s.w - 20, 128 }));
        const juce::Rectangle<int> row (s.x + 10, 278, s.w - 20, 92);
        if (p == "g")
            knobRow (row, { { "gPos", "Pos", false }, { "gSize", "Size", false }, { "gDens", "Density", false }, { "gSpray", "Spray", false },
                            { "gSpread", "Spread", false }, { "gPitch", "Pitch", true }, { "gLevel", "Level", false } }, s.accent);
        else
            for (int i = 0; i < 8; ++i)
            {
                static const char* ids[] { "Pos", "WarpAmt", "Oct", "Semi", "Fine", "Uni", "Det", "Level" };
                static const char* names[] { "WT Pos", "Warp", "Oct", "Semi", "Fine", "Unison", "Detune", "Level" };
                const int w = row.getWidth() / 8;
                knob (p + ids[i], names[i], s.accent, { row.getX() + i * w, row.getY(), w, row.getHeight() }, i >= 2 && i <= 4);
            }
    }

    panel ({ 16, 384, 400, 170 }, "04", "FILTER // SUB // NOISE", Theme::amber);
    place (std::make_unique<SegmentedSelector> (*proc.apvts.getParameter ("fType"), juce::StringArray { "LOW", "BAND", "HIGH", "NOTCH", "COMB" }, Theme::amber),
           { 26, 422, 380, 26 });
    knobRow ({ 26, 456, 380, 92 }, { { "fCut", "Cutoff", false }, { "fRes", "Reso", false }, { "fDrive", "Drive", false }, { "fEnv", "Env Amt", true },
                                     { "fKey", "Key Trk", false }, { "sub", "Sub", false }, { "noise", "Noise", false } }, Theme::amber);

    panel ({ 424, 384, 400, 170 }, "05", "ENVELOPES // AMP + MOD", Theme::lime);
    label ("AMP ENV  ->  VOLUME", { 436, 420, 180, 16 }, Theme::lime);
    label ("MOD ENV  ->  FILTER / MATRIX", { 630, 420, 190, 16 }, Theme::cyan);
    knobRow ({ 434, 442, 190, 106 }, { { "e1A", "Att", false }, { "e1D", "Dec", false }, { "e1S", "Sus", false }, { "e1R", "Rel", false } }, Theme::lime);
    knobRow ({ 624, 442, 190, 106 }, { { "e2A", "Att", false }, { "e2D", "Dec", false }, { "e2S", "Sus", false }, { "e2R", "Rel", false } }, Theme::cyan);

    panel ({ 832, 384, 392, 170 }, "06", "VOICE // MONO // GLIDE", Theme::cyan);
    toggle ("mono", "MONO", Theme::cyan, { 842, 422, 84, 26 });
    toggle ("legato", "LEGATO", Theme::cyan, { 932, 422, 100, 26 });
    knobRow ({ 842, 456, 372, 92 }, { { "glide", "Glide", false }, { "bendRange", "Bend", false }, { "master", "Master", true } }, Theme::cyan);

    panel ({ 16, 562, 800, 168 }, "07", "MACROS // DRAG A MACRO ONTO ANY KNOB", Theme::amber);
    for (int i = 0; i < numMacros; ++i)
    {
        auto& m = knob ("macro" + juce::String (i + 1), "Macro " + juce::String (i + 1), Theme::amber, { 26 + i * 196, 596, 190, 108 });
        m.setTooltip ("Automate MACRO " + juce::String (i + 1) + " in your DAW to move every knob it is linked to");
        place (std::make_unique<ModChip> (sMacro1 + i, "DRAG M" + juce::String (i + 1), Theme::amber), { 26 + i * 196 + 50, 704, 90, 22 });
    }

    panel ({ 824, 562, 400, 168 }, "08", "QUICK FX", Theme::lime);
    knobRow ({ 834, 600, 380, 124 }, { { "fxDrive", "Dist", false }, { "ottDepth", "OTT", false }, { "hyMix", "Hyper", false },
                                       { "fxDelay", "Delay", false }, { "fxReverb", "Reverb", false } }, Theme::lime);
}

void SkazonAudioProcessorEditor::buildModPage()
{
    for (int i = 0; i < numLfos; ++i)
    {
        const juce::String l ("l" + juce::String (i + 1));
        const int x = 16 + i * 304;
        panel ({ x, 72, 296, 290 }, juce::String (i + 1).paddedLeft ('0', 2), "LFO " + juce::String (i + 1), Theme::cyan);
        lfoDisplays.push_back (&place (std::make_unique<LfoDisplay> (proc, i), { x + 10, 112, 276, 116 }));
        combo (l + "Shape", { x + 10, 236, 140, 26 });
        toggle (l + "Sync", "SYNC", Theme::cyan, { x + 154, 236, 62, 26 });
        combo (l + "Div", { x + 10, 268, 140, 26 });
        knob (l + "Rate", "Rate", Theme::cyan, { x + 218, 232, 72, 96 });
        const int lfoSource[] { sLfo1, sLfo2, sLfo3, sLfo4 };
        place (std::make_unique<ModChip> (lfoSource[i], "DRAG LFO " + juce::String (i + 1), Theme::cyan), { x + 10, 304, 140, 28 });
    }

    panel ({ 16, 370, 1208, 360 }, "05", "MOD MATRIX // 16 SLOTS // SOURCE -> MATH -> TARGET -> AMOUNT", Theme::magenta);
    for (int i = 0; i < numSlots; ++i)
    {
        const int x = 26 + (i / 8) * 604, y = 410 + (i % 8) * 39;
        label (juce::String (i + 1), { x, y + 3, 18, 26 }, Theme::magenta);
        combo (slotID (i + 1, "Src"), { x + 20, y + 3, 130, 26 });
        combo (slotID (i + 1, "Op"), { x + 154, y + 3, 104, 26 });
        combo (slotID (i + 1, "Dst"), { x + 262, y + 3, 140, 26 });
        amountBar (slotID (i + 1, "Amt"), { x + 406, y + 3, 180, 26 });
    }
}

void SkazonAudioProcessorEditor::buildFxPage()
{
    panel ({ 16, 72, 400, 320 }, "01", "DISTORTION", Theme::red);
    toggle ("distOn", "ON", Theme::red, { 26, 112, 62, 26 });
    combo ("distType", { 94, 112, 180, 26 });
    knobRow ({ 26, 146, 380, 104 }, { { "fxDrive", "Drive", false }, { "distTone", "Tone", false }, { "distMix", "Mix", false } }, Theme::red);
    scopes.push_back (&place (std::make_unique<FxScope> (proc, FxScope::distortion), { 26, 256, 380, 128 }));

    panel ({ 424, 72, 400, 320 }, "02", "OTT // 3-BAND UP + DOWN COMPRESSION", Theme::lime);
    toggle ("ottOn", "ON", Theme::lime, { 434, 112, 62, 26 });
    knobRow ({ 434, 146, 380, 104 }, { { "ottDepth", "Depth", false }, { "ottTime", "Time", false }, { "ottUp", "Upward", false },
                                       { "ottDown", "Downward", false }, { "ottGain", "Gain", true } }, Theme::lime);
    scopes.push_back (&place (std::make_unique<FxScope> (proc, FxScope::ott), { 434, 256, 380, 128 }));

    panel ({ 832, 72, 392, 320 }, "03", "EQ // LOW + HIGH SHELF", Theme::cyan);
    toggle ("eqOn", "ON", Theme::cyan, { 842, 112, 62, 26 });
    knobRow ({ 842, 146, 372, 104 }, { { "eqLow", "Low", true }, { "eqLowFreq", "Low Freq", false }, { "eqHigh", "High", true }, { "eqHighFreq", "High Freq", false } }, Theme::cyan);
    label ("CHAIN:  DIST > EQ > PHASER > CHORUS > HYPER >", { 842, 270, 372, 16 }, Theme::dim.brighter (0.5f));
    label ("        DELAY > REVERB > OTT > LIMITER", { 842, 288, 372, 16 }, Theme::dim.brighter (0.5f));
    label ("A SAFETY LIMITER ON THE OUTPUT CATCHES PEAKS OVER -0.3 DB", { 842, 316, 372, 16 }, Theme::dim.brighter (0.5f));

    struct Unit { const char* index; const char* title; const char* on; juce::Colour accent; std::vector<std::pair<const char*, const char*>> knobs; };
    const Unit units[] {
        { "04", "PHASER", "phOn", Theme::amber, { { "phMix", "Mix" }, { "phRate", "Rate" }, { "phDepth", "Depth" }, { "phFb", "Feedback" } } },
        { "05", "CHORUS", "chOn", Theme::cyan, { { "fxChorus", "Mix" }, { "chRate", "Rate" }, { "chDepth", "Depth" } } },
        { "06", "HYPER // DIMENSION", "hyOn", Theme::magenta, { { "hyMix", "Hyper" }, { "hyDetune", "Detune" }, { "hyDim", "Dimension" } } },
        { "07", "DELAY // SYNCED", "dlOn", Theme::lime, { { "fxDelay", "Mix" }, { "fxDelayFb", "Feedback" } } },
        { "08", "REVERB", "rvOn", Theme::amber, { { "fxReverb", "Mix" }, { "fxSize", "Size" } } },
    };
    for (int u = 0; u < 5; ++u)
    {
        const auto& unit = units[u];
        const int x = 16 + u * 243;
        panel ({ x, 400, 235, 330 }, unit.index, unit.title, unit.accent);
        toggle (unit.on, "ON", unit.accent, { x + 10, 440, 62, 26 });
        if (u == 3)
            combo ("fxDelayDiv", { x + 80, 440, 145, 26 });
        for (size_t k = 0; k < unit.knobs.size(); ++k)
            knob (unit.knobs[k].first, unit.knobs[k].second, unit.accent,
                  { x + 10 + (int) (k % 3) * 72, 480 + (int) (k / 3) * 116, 72, 110 }, juce::String (unit.knobs[k].first) == "phFb");
    }
}

//==============================================================================
template <typename T>
T& SkazonAudioProcessorEditor::place (std::unique_ptr<T> c, juce::Rectangle<int> bounds)
{
    auto& ref = *c;
    ref.setBounds (bounds);
    canvas.addAndMakeVisible (ref);
    if (building >= 0)
        pageComponents[building].push_back (&ref);
    controls.push_back (std::move (c));
    return ref;
}

void SkazonAudioProcessorEditor::panel (juce::Rectangle<int> b, const juce::String& index, const juce::String& title, juce::Colour c)
{
    panels.push_back ({ building, b, index, title, c });
}

void SkazonAudioProcessorEditor::label (const juce::String& text, juce::Rectangle<int> bounds, juce::Colour colour)
{
    texts.push_back ({ building, text, bounds, colour });
}

TargetKnob& SkazonAudioProcessorEditor::knob (const juce::String& id, const juce::String& text, juce::Colour colour, juce::Rectangle<int> bounds, bool bipolar)
{
    const int t = targetFor (id);
    auto& k = place (std::make_unique<TargetKnob> (text, colour, bipolar, t), bounds);
    k.setCompact (true);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k));
    auto& p = *proc.apvts.getParameter (id);
    k.setDoubleClickReturnValue (true, p.convertFrom0to1 (p.getDefaultValue()));
    k.paramID = id;
    if (! id.startsWith ("macro"))
    {
        k.onDrop = [this, id, t] (int src) { dropSource (id, t, src); };
        k.onRemoveMenu = [this, id, t] { removeRoutesMenu (id, t); };
        k.onDepthDrag = [this, id, t] (float d) { adjustDepth (id, t, d); };
        k.modulation = [this, id, t] { return liveValue (id, t); };
        k.modRange = [this, id, t] { return routeRange (id, t); };
        k.setTooltip (t > 0 ? "Drop a mod source or MACRO here  //  shift-drag = depth  //  right-click = remove"
                            : "Drop a MACRO here  //  shift-drag = depth  //  right-click = remove");
        targetKnobs.push_back ({ &k, id });
    }
    return k;
}

void SkazonAudioProcessorEditor::knobRow (juce::Rectangle<int> area, std::initializer_list<std::tuple<const char*, const char*, bool>> items, juce::Colour colour)
{
    const int w = area.getWidth() / (int) items.size();
    int i = 0;
    for (auto [id, text, bipolar] : items)
        knob (id, text, colour, { area.getX() + w * i++, area.getY(), w, area.getHeight() }, bipolar);
}

void SkazonAudioProcessorEditor::combo (const juce::String& id, juce::Rectangle<int> bounds)
{
    auto& box = place (std::make_unique<juce::ComboBox>(), bounds);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        box.addItemList (choice->choices, 1);
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, box));
}

void SkazonAudioProcessorEditor::toggle (const juce::String& id, const juce::String& text, juce::Colour colour, juce::Rectangle<int> bounds)
{
    auto& t = place (std::make_unique<NeonToggle> (text, colour), bounds);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t));
}

void SkazonAudioProcessorEditor::amountBar (const juce::String& id, juce::Rectangle<int> bounds)
{
    auto& s = place (std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::NoTextBox), bounds);
    s.setDoubleClickReturnValue (true, 0.0);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s));
}

//==============================================================================
namespace
{
    void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

void SkazonAudioProcessorEditor::addRoute (int source, int target)
{
    int free = -1;
    for (int i = 1; i <= numSlots; ++i)
    {
        const int src = (int) proc.param (slotID (i, "Src")), dst = (int) proc.param (slotID (i, "Dst"));
        if (src == source && dst == target)
            return; // already routed: shift-drag the knob to change the depth
        if (free < 0 && (src == sNone || dst == tNone))
            free = i;
    }
    if (free < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "SKAZON", "All 16 mod slots are in use. Right-click a knob to remove a routing.");
        return;
    }
    const float amount = target == tAPitch || target == tBPitch || target == tPitch ? 0.25f : target == tCutoff ? 0.3f : 0.5f;
    setParam (proc.apvts, slotID (free, "Src"), (float) source);
    setParam (proc.apvts, slotID (free, "Dst"), (float) target);
    setParam (proc.apvts, slotID (free, "Op"), (float) opLinear);
    setParam (proc.apvts, slotID (free, "Amt"), amount);
}

void SkazonAudioProcessorEditor::dropSource (const juce::String& id, int target, int source)
{
    if (source >= sMacro1 && source <= sMacro4)
    {
        const int m = source - sMacro1;
        for (const auto& l : proc.macroLinks())
            if (l.macro == m && l.id == id)
                return; // already linked: shift-drag the knob to change the depth
        proc.setMacroLink (m, id, 0.5f);
        return;
    }
    if (target > 0)
        addRoute (source, target);
}

juce::Range<float> SkazonAudioProcessorEditor::macroRange (const juce::String& id) const
{
    float lo = 0, hi = 0;
    bool any = false;
    for (const auto& l : proc.macroLinks())
        if (l.id == id)
        {
            any = true;
            lo += juce::jmin (0.0f, l.depth);
            hi += juce::jmax (0.0f, l.depth);
        }
    if (! any)
        return {};
    const float base = proc.apvts.getParameter (id)->getValue();
    return { juce::jlimit (0.0f, 1.0f, base + lo), juce::jlimit (0.0f, 1.0f, base + hi) + 0.0001f };
}

void SkazonAudioProcessorEditor::removeRoutesMenu (const juce::String& id, int target)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ((target > 0 ? targetNames()[target] : id.toUpperCase()) + " MODULATION");
    const auto linked = proc.macroLinks();
    for (const auto& l : linked)
        if (l.id == id)
            menu.addItem (1000 + l.macro, "Remove MACRO " + juce::String (l.macro + 1) + "  (" + juce::String (juce::roundToInt (l.depth * 100.0f)) + "%)");
    for (int i = 1; i <= numSlots && target > 0; ++i)
        if ((int) proc.param (slotID (i, "Dst")) == target && (int) proc.param (slotID (i, "Src")) != sNone)
            menu.addItem (i, "Remove " + sourceNames()[(int) proc.param (slotID (i, "Src"))] + "  ("
                               + juce::String (juce::roundToInt (proc.param (slotID (i, "Amt")) * 100.0f)) + "%, slot " + juce::String (i) + ")");
    if (menu.getNumItems() <= 1)
        menu.addItem (-1, target > 0 ? "No modulation yet: drag a MOD SOURCE or MACRO onto this knob"
                                      : "No modulation yet: drag a MACRO onto this knob", false);
    menu.showMenuAsync ({}, [this, id] (int slot)
    {
        if (slot <= 0)
            return;
        if (slot >= 1000)
        {
            proc.setMacroLink (slot - 1000, id, 0.0f);
            return;
        }
        setParam (proc.apvts, slotID (slot, "Src"), 0);
        setParam (proc.apvts, slotID (slot, "Dst"), 0);
        setParam (proc.apvts, slotID (slot, "Op"), 0);
        setParam (proc.apvts, slotID (slot, "Amt"), 0);
    });
}

void SkazonAudioProcessorEditor::adjustDepth (const juce::String& id, int target, float delta)
{
    for (int i = numSlots; i >= 1 && target > 0; --i) // the newest routing (highest slot) is the one shift-drag changes
        if ((int) proc.param (slotID (i, "Dst")) == target && (int) proc.param (slotID (i, "Src")) != sNone)
        {
            setParam (proc.apvts, slotID (i, "Amt"), juce::jlimit (-1.0f, 1.0f, proc.param (slotID (i, "Amt")) + delta));
            return;
        }
    const auto linked = proc.macroLinks();
    for (auto it = linked.rbegin(); it != linked.rend(); ++it) // else the newest macro link on this knob
        if (it->id == id)
        {
            const float d = juce::jlimit (-1.0f, 1.0f, it->depth + delta);
            proc.setMacroLink (it->macro, id, std::abs (d) < 0.001f ? 0.002f : d); // shift-drag never deletes; right-click does
            return;
        }
}

// Where the engine's modulation of target lands on a knob's 0..1 travel.
float SkazonAudioProcessorEditor::shiftNorm (const juce::String& id, int target, float base, float mod) const
{
    auto* p = proc.apvts.getParameter (id);
    const auto& range = p->getNormalisableRange();
    float v = p->convertFrom0to1 (base);
    switch (target)
    {
        case tCutoff:                          v *= std::exp2 (mod * 8.0f); break;
        case tGSize: case tGDensity:           v *= std::exp2 (mod * 3.0f); break;
        case tLfo1Rate: case tLfo2Rate: case tLfo3Rate: case tLfo4Rate: v *= std::exp2 (mod * 4.0f); break;
        case tAPitch: case tBPitch:            v += mod * 24.0f; break;
        default:                               v += mod; break;
    }
    return p->convertTo0to1 (juce::jlimit (range.start, range.end, v));
}

juce::Range<float> SkazonAudioProcessorEditor::routeRange (const juce::String& id, int target) const
{
    const auto macros = macroRange (id);
    float lo = 0, hi = 0;
    bool any = false;
    for (int i = 1; i <= numSlots && target > 0; ++i)
    {
        const int src = (int) proc.param (slotID (i, "Src"));
        if ((int) proc.param (slotID (i, "Dst")) != target || src == sNone)
            continue;
        any = true;
        const int op = (int) proc.param (slotID (i, "Op"));
        const float amt = proc.param (slotID (i, "Amt"));
        const float sLo = unipolar (src) ? 0.0f : -1.0f;
        float cLo = 1e9f, cHi = -1e9f;
        for (int k = 0; k <= 8; ++k)
        {
            const float c = applyOp (op, sLo + (1.0f - sLo) * (float) k / 8.0f) * amt;
            cLo = juce::jmin (cLo, c);
            cHi = juce::jmax (cHi, c);
        }
        lo += cLo;
        hi += cHi;
    }
    if (! any)
        return macros;
    const float base = proc.apvts.getParameter (id)->getValue();
    const float a = shiftNorm (id, target, base, lo), b = shiftNorm (id, target, base, hi);
    const juce::Range<float> r { juce::jmin (a, b), juce::jmax (a, b) + 0.0001f };
    return macros.isEmpty() ? r : r.getUnionWith (macros);
}

float SkazonAudioProcessorEditor::liveValue (const juce::String& id, int target) const
{
    // Macro links are exact and voice independent: show where the macros put the knob right now.
    float linked = proc.apvts.getParameter (id)->getValue();
    bool anyLink = false;
    for (const auto& l : proc.macroLinks())
        if (l.id == id)
        {
            anyLink = true;
            linked = juce::jlimit (0.0f, 1.0f, linked + l.depth * proc.param ("macro" + juce::String (l.macro + 1)));
        }
    if (anyLink && (target == 0 || proc.activeVoices.load() == 0))
        return linked;
    if (target == 0 || proc.activeVoices.load() == 0 || routeRange (id, target).isEmpty())
        return -1.0f;
    return shiftNorm (id, target, proc.apvts.getParameter (id)->getValue(), proc.liveMods[(size_t) target].load());
}

//==============================================================================
bool SkazonAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return std::any_of (files.begin(), files.end(), SkazonAudioProcessor::isAudioFile);
}

void SkazonAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropOverlay.setVisible (false);
    if (const auto f = std::find_if (files.begin(), files.end(), SkazonAudioProcessor::isAudioFile); f != files.end())
        runMatch (juce::File (*f));
}

void SkazonAudioProcessorEditor::DropOverlay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.fillAll (Theme::background.withAlpha (0.82f));
    juce::Path box;
    box.addRectangle (r.reduced (r.getWidth() * 0.02f));
    juce::Path dashed;
    const float dash[] { 12.0f, 8.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, box, dash, 2);
    g.setColour (Theme::magenta);
    g.fillPath (dashed);
    g.setFont (Theme::sans (r.getHeight() * 0.06f, true).withExtraKerningFactor (0.15f));
    g.drawText ("DROP TO SOUND MATCH", r.withTrimmedBottom (r.getHeight() * 0.1f), juce::Justification::centred);
    g.setColour (Theme::text);
    g.setFont (Theme::mono (r.getHeight() * 0.02f, true));
    g.drawText ("A LEAD / BASS / VOCAL LINE  ->  A SKAZON PATCH + ITS MIDI", r.withTrimmedTop (r.getHeight() * 0.12f), juce::Justification::centred);
    g.setColour (Theme::dim.brighter (0.5f));
    g.drawText ("(DROP ON A WAVE DISPLAY INSTEAD TO JUST MAKE A WAVETABLE)", r.withTrimmedTop (r.getHeight() * 0.22f), juce::Justification::centred);
}

void SkazonAudioProcessorEditor::runMatch (const juce::File& file)
{
    matchButton.flash = 1.0f;
    const auto result = proc.soundMatch (file);
    if (! result.ok)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SOUND MATCH", result.error);
        return;
    }
    showPage (0);
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "SOUND MATCH: " + file.getFileNameWithoutExtension(),
                                            result.summary + "\n\nTweak it like any patch: WARP, filter, FX, the MUTATE button...");
}

void SkazonAudioProcessorEditor::showMutateMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("MUTATE THIS PATCH");
    menu.addItem (1, "Subtle  (small nudge)");
    menu.addItem (2, "Medium");
    menu.addItem (3, "Wild  (big jumps)");
    menu.addSeparator();
    menu.addSectionHeader ("LOCKED (KEPT AS IS)");
    const std::pair<int, const char*> groups[] { { SkazonAudioProcessor::lockOsc, "Oscillators + grains" }, { SkazonAudioProcessor::lockFilter, "Filter" },
                                                 { SkazonAudioProcessor::lockEnv, "Envelopes" }, { SkazonAudioProcessor::lockMod, "LFOs + matrix + macros" },
                                                 { SkazonAudioProcessor::lockFx, "FX" } };
    for (auto [bit, text] : groups)
        menu.addItem (100 + bit, text, true, (proc.mutateLocks & bit) != 0);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mutateButton), [this] (int r)
    {
        if (r >= 100)
        {
            proc.mutateLocks ^= r - 100;
            showMutateMenu(); // keep the menu open while picking locks
            return;
        }
        if (r <= 0)
            return;
        mutateAmount = r == 1 ? 0.15f : r == 2 ? 0.35f : 0.7f;
        proc.mutate (mutateAmount, proc.mutateLocks);
        mutateButton.flash = 1.0f;
    });
}

void SkazonAudioProcessorEditor::showPage (int page)
{
    page = juce::jlimit (0, numPages - 1, page);
    for (int i = 0; i < numPages; ++i)
    {
        for (auto* c : pageComponents[i])
            c->setVisible (i == page);
        tabs[i]->setToggleState (i == page, juce::dontSendNotification);
    }
    proc.uiPage = page;
    canvas.page = page;
    canvas.repaint();
}

void SkazonAudioProcessorEditor::Wheel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    auto track = r.reduced (4.0f, 8.0f).withTrimmedBottom (12.0f);
    g.setColour (Theme::background);
    g.fillRoundedRectangle (r.reduced (1.0f).withTrimmedBottom (12.0f), 5.0f);
    g.setColour (accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (1.0f).withTrimmedBottom (12.0f), 5.0f, 1.2f);
    // ribs that roll with the wheel
    const float norm = pitch ? (value + 1.0f) * 0.5f : value;
    g.saveState();
    g.reduceClipRegion (track.toNearestInt());
    for (int i = -12; i < 24; ++i)
    {
        const float y = track.getY() + std::fmod (i * 6.0f - norm * 40.0f + 400.0f, track.getHeight() + 72.0f) - 36.0f;
        g.setColour (Theme::edge.brighter (0.3f));
        g.drawHorizontalLine ((int) y, track.getX() + 3.0f, track.getRight() - 3.0f);
    }
    if (pitch)
    {
        g.setColour (Theme::dim);
        g.drawHorizontalLine ((int) track.getCentreY(), track.getX(), track.getRight());
    }
    // fill from the rest point to the value, plus a bright handle
    const float y = track.getBottom() - norm * track.getHeight();
    const float from = pitch ? track.getCentreY() : track.getBottom();
    g.setColour (accent.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (track.getX(), juce::jmin (y, from), track.getWidth(), std::abs (from - y)));
    g.setColour (accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), y - 4.0f, track.getWidth(), 8.0f), 3.0f);
    g.restoreState();
    g.setColour (accent);
    g.setFont (Theme::mono (9.5f, true));
    g.drawText (value != 0.0f && readout != nullptr ? readout (value) : text, r.withTop (r.getBottom() - 12.0f), juce::Justification::centred);
}

void SkazonAudioProcessorEditor::timerCallback()
{
    pitchWheel.show ((float) (proc.shownBend.load() - 8192) / 8192.0f);
    modWheel.show ((float) proc.shownModWheel.load() / 127.0f);
    for (auto* w : waves)        if (w->isShowing()) w->tick();
    for (auto* l : lfoDisplays)  if (l->isShowing()) l->tick();
    for (auto* s : scopes)       if (s->isShowing()) s->tick();

    // Modulated knobs repaint only when their depth arc or live marker moves.
    for (auto& [k, id] : targetKnobs)
        if (k->isShowing())
        {
            const auto range = routeRange (id, k->target);
            const juce::int64 state = range.isEmpty() ? (k->dropHover ? 1 : 0)
                                    : (juce::int64) juce::roundToInt (range.getStart() * 500.0f) * 1000000ll + juce::roundToInt (range.getEnd() * 500.0f) * 1000ll
                                      + juce::roundToInt (liveValue (id, k->target) * 500.0f) + 500;
            if (state != k->shown)
            {
                k->shown = state;
                k->repaint();
            }
        }

    if (proc.randomName.isNotEmpty())
    {
        if (presetBox.getText() != proc.randomName)
            presetBox.setText (proc.randomName, juce::dontSendNotification);
    }
    else if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);

    midiButton.setEnabled (! proc.matchedNotes().empty());
    midiButton.setButtonText (proc.isPreviewing() ? "STOP" : "MIDI");
    if (proc.isPreviewing())
        midiButton.flash = juce::jmax (midiButton.flash, 0.5f + 0.3f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f));
    for (NeonButton* b : { &dice, &matchButton, &mutateButton, (NeonButton*) &midiButton })
        if (b->flash > 0.0f)
        {
            b->flash = juce::jmax (0.0f, b->flash - 0.08f);
            b->repaint();
        }
}

void SkazonAudioProcessorEditor::renderBackground()
{
    const float scale = juce::Component::getApproximateScaleFactorForComponent (&canvas);
    for (int page = 0; page < numPages; ++page)
    {
        auto& image = canvas.background[page];
        image = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) designWidth * scale), juce::roundToInt ((float) designHeight * scale), true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));

        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) designWidth, (float) designHeight);
        g.fillAll (Theme::background);

        const auto head = bounds.withHeight (64.0f);
        if (Theme::skin == Theme::blackBox)
        {
            // Matte black powder-coat case with a faint texture and a thin green rule under the header.
            g.fillAll (juce::Colour (0xff0a0b0c));
            juce::Random grain (5);
            for (int i = 0; i < 12000; ++i)
            {
                g.setColour (juce::Colours::white.withAlpha (0.012f + grain.nextFloat() * 0.025f));
                g.fillRect (grain.nextFloat() * bounds.getWidth(), grain.nextFloat() * bounds.getHeight(), 1.0f, 1.0f);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17191b), 0.0f, 0.0f, juce::Colour (0xff0a0b0c), 0.0f, head.getBottom(), false));
            g.fillRect (head);
            g.setColour (Theme::lime.withAlpha (0.7f));
            g.drawHorizontalLine ((int) head.getBottom() - 1, 16.0f, bounds.getRight() - 16.0f);
        }
        else if (Theme::skin == Theme::ironMachine)
        {
            // Cast iron chassis: dark textured casting, a lit gunmetal header rail with rack screws.
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2e33), 620.0f, 300.0f, juce::Colour (0xff0e0f11), 0.0f, bounds.getBottom(), true));
            g.fillRect (bounds);
            juce::Random grain (11);
            for (int i = 0; i < 9000; ++i) // sand-cast speckle
            {
                g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.03f + grain.nextFloat() * 0.05f));
                g.fillRect (grain.nextFloat() * bounds.getWidth(), 64.0f + grain.nextFloat() * (bounds.getHeight() - 64.0f), 1.0f, 1.0f);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a7078), 0.0f, 0.0f, juce::Colour (0xff2a2d32), 0.0f, head.getBottom(), false));
            g.fillRect (head);
            for (float y = 1.0f; y < head.getBottom() - 2.0f; y += 1.5f)
            {
                g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.02f + grain.nextFloat() * 0.04f));
                g.drawHorizontalLine ((int) y, 0.0f, bounds.getRight());
            }
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.drawHorizontalLine (0, 0.0f, bounds.getRight());
            g.setColour (juce::Colours::black);
            g.fillRect (head.withTop (head.getBottom() - 2.0f));
            for (float x : { 8.0f, bounds.getRight() - 8.0f })
                for (float y : { 14.0f, 50.0f })
                {
                    const auto sc = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, y });
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6d68a), sc.getTopLeft(), juce::Colour (0xff6b4316), sc.getBottomRight(), false));
                    g.fillEllipse (sc);
                    g.setColour (juce::Colours::black.withAlpha (0.7f));
                    g.drawLine (x - 2.8f, y - 1.0f, x + 2.8f, y + 1.0f, 1.2f);
                }
        }
        else if (Theme::skin == Theme::chromeIce)
        {
            // Dark anodised chassis with fine brushed grain; the header is a brushed steel bar.
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1e24), 0.0f, 0.0f, juce::Colour (0xff07080a), 0.0f, bounds.getBottom(), false));
            g.fillRect (bounds);
            juce::Random grain (7);
            for (float y = 64.0f; y < bounds.getBottom(); y += 2.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.006f + grain.nextFloat() * 0.012f));
                g.drawHorizontalLine ((int) y, 0.0f, bounds.getRight());
            }
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a525d), 0.0f, 0.0f, juce::Colour (0xff1c2026), 0.0f, head.getBottom(), false));
            g.fillRect (head);
            for (float x = 0.0f; x < bounds.getRight(); x += 2.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.01f + grain.nextFloat() * 0.03f));
                g.drawVerticalLine ((int) x, 0.0f, head.getBottom() - 2.0f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawHorizontalLine (0, 0.0f, bounds.getRight());
        }
        else if (Theme::skin == Theme::sunsetGlass)
        {
            // Synthwave dusk: sky gradient, striped sun and a perspective grid behind the glass panels.
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff12021f), 0.0f, 0.0f, juce::Colour (0xff4a0d4f), 0.0f, 560.0f, false));
            g.fillRect (bounds);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a0d4f), 0.0f, 560.0f, juce::Colour (0xff160320), 0.0f, bounds.getBottom(), false));
            g.fillRect (bounds.withTop (560.0f));
            const juce::Point<float> sun (620.0f, 470.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0x66ff2a6d), sun, juce::Colour (0x00ff2a6d), sun.translated (420.0f, 0.0f), true));
            g.fillEllipse (juce::Rectangle<float> (840.0f, 840.0f).withCentre (sun));
            {
                juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (juce::Rectangle<int> (0, 0, designWidth, 560));
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffd166), sun.x, sun.y - 260.0f, juce::Colour (0xffff2a6d), sun.x, sun.y + 80.0f, false));
                g.fillEllipse (juce::Rectangle<float> (520.0f, 520.0f).withCentre (sun));
                for (int i = 0; i < 9; ++i) // the sun's stripes, thicker toward the horizon
                {
                    const float y = sun.y + 20.0f + (float) i * 14.0f;
                    g.setColour (juce::Colour (0xff2a0838));
                    g.fillRect (juce::Rectangle<float> (sun.x - 270.0f, y, 540.0f, 2.0f + (float) i * 0.9f));
                }
            }
            g.setColour (juce::Colour (0xffff2a6d).withAlpha (0.35f));
            for (int i = -30; i <= 30; ++i)
                g.drawLine (sun.x + (float) i * 18.0f, 560.0f, sun.x + (float) i * 120.0f, bounds.getBottom(), 1.0f);
            for (int i = 0; i < 12; ++i)
            {
                const float y = 560.0f + std::pow ((float) i / 11.0f, 1.8f) * (bounds.getBottom() - 560.0f);
                g.drawHorizontalLine ((int) y, 0.0f, bounds.getRight());
            }
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xcc12021f), 0.0f, 0.0f, juce::Colour (0x0012021f), 0.0f, head.getBottom() + 20.0f, false));
            g.fillRect (head.withHeight (84.0f));
        }
        else if (Theme::skin == Theme::acidLab)
        {
            // Oscilloscope room: black, a green measurement grid with major lines, a glowing header rule.
            g.fillAll (Theme::background);
            for (int x = 0; x < designWidth; x += 24)
            {
                g.setColour (Theme::edge.withAlpha (x % 120 == 0 ? 0.55f : 0.22f));
                g.drawVerticalLine (x, 64.0f, bounds.getBottom());
            }
            for (int y = 64; y < designHeight; y += 24)
            {
                g.setColour (Theme::edge.withAlpha ((y - 64) % 120 == 0 ? 0.55f : 0.22f));
                g.drawHorizontalLine (y, 0.0f, bounds.getRight());
            }
            g.setColour (Theme::panel);
            g.fillRect (head);
            for (int x = 0; x < designWidth; x += 6) // hazard ticks along the top edge
            {
                g.setColour (Theme::lime.withAlpha (x % 24 == 0 ? 0.6f : 0.15f));
                g.drawVerticalLine (x, 0.0f, x % 24 == 0 ? 6.0f : 3.0f);
            }
        }
        else
        {
            // Violet nebula, plus fine diagonal rays.
            g.setGradientFill (juce::ColourGradient (Theme::amber.withAlpha (0.16f), 620.0f, 220.0f, Theme::amber.withAlpha (0.0f), 620.0f, 760.0f, true));
            g.fillRect (bounds);
            for (int i = -20; i < 60; ++i)
            {
                g.setColour (Theme::lime.withAlpha (0.015f));
                g.drawLine ((float) i * 32.0f, 64.0f, (float) i * 32.0f + 400.0f, bounds.getBottom(), 1.0f);
            }
            g.setGradientFill (juce::ColourGradient (Theme::panelHi, 0.0f, 0.0f, Theme::background, 0.0f, head.getBottom(), false));
            g.fillRect (head);
        }

        // Header.
        g.setGradientFill (juce::ColourGradient (Theme::lime, 0.0f, 0.0f, Theme::magenta, bounds.getRight(), 0.0f, false));
        g.fillRect (head.withTop (62.0f));
        g.drawImage (logo, juce::Rectangle<float> (14.0f, 5.0f, 100.0f, 54.0f), juce::RectanglePlacement::centred);
        g.setFont (Theme::sans (36.0f, true).withExtraKerningFactor (0.2f));
        g.setGradientFill (juce::ColourGradient (Theme::lime, 124.0f, 0.0f, Theme::magenta, 340.0f, 0.0f, false));
        g.drawText ("SKAZON", juce::Rectangle<float> (124.0f, 3.0f, 360.0f, 40.0f), juce::Justification::centredLeft);
        g.setFont (Theme::mono (10.0f));
        g.setColour (Theme::dim.brighter (0.5f));
        g.drawText ("WAVETABLE + GRANULAR", juce::Rectangle<float> (126.0f, 41.0f, 200.0f, 16.0f), juce::Justification::centredLeft);

        // Mod source strip.
        g.setColour (Theme::panel.withAlpha (Theme::skin == Theme::sunsetGlass ? 0.85f : 1.0f));
        g.fillRoundedRectangle (juce::Rectangle<float> (16.0f, 738.0f, 1208.0f, 38.0f), Theme::corner());
        g.setColour (Theme::edge);
        g.drawRoundedRectangle (juce::Rectangle<float> (16.0f, 738.0f, 1208.0f, 38.0f), Theme::corner(), 1.0f);

        for (const auto& p : panels)
            if (p.page == page || p.page < 0)
                Theme::drawPanel (g, p.bounds.toFloat(), p.index, p.title, p.accent);

        g.setFont (Theme::mono (10.0f, true));
        for (const auto& t : texts)
            if (t.page == page || t.page < 0)
            {
                g.setColour (t.colour);
                g.drawText (t.text, t.bounds, juce::Justification::centredLeft);
            }

        g.setColour (Theme::edge.brighter (0.2f));
        if (page == 0)
            g.drawVerticalLine (624, 422.0f, 548.0f);
        if (page == 1)
            g.drawVerticalLine (622, 410.0f, 722.0f);

        g.setFont (Theme::mono (10.0f));
        g.setColour (Theme::dim);
        const auto footer = bounds.withTop (bounds.getBottom() - 26.0f).reduced (18.0f, 0.0f);
        g.drawText ("SKAZI // SKAZON v2.0 // BY ASHER SWISSA", footer, juce::Justification::centredLeft);
        g.drawText ("DROP AUDIO = SOUND MATCH  //  ON A WAVE = WAVETABLE  //  DRAG SOURCE -> KNOB  //  SHIFT-DRAG = DEPTH  //  RIGHT-CLICK = REMOVE",
                    footer, juce::Justification::centredRight);
    }
}

void SkazonAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));
    dropOverlay.setBounds (getLocalBounds());

    if (std::abs (scale - renderedScale) > 0.001f)
    {
        renderedScale = scale;
        renderBackground();
        canvas.repaint();
    }
}
