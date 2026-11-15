#include "PluginEditor.h"

namespace
{
// 303 colours: silver panel, black knobs and ink, red LEDs, grey rubber keys
const juce::Colour led     { 0xffff2a1f }; // the playing step, accents, selected buttons
const juce::Colour amber   { 0xffffa31a }; // slides
const juce::Colour keyGrey { 0xffcfd2d6 }; // note keys
const juce::Colour inkC    { 0xff161616 }; // printing on the silver panel
const juce::Colour rust    { 0xffffd23d }; // live mutations
const juce::Colour acidStripe { 0xffa6ff2e }; // SKACID's own mark: the deck stripe and the knob dots

// a step's note as it sounds in the track key (12 = the key's root an octave up)
juce::String noteName (int n, int key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[(juce::jlimit (0, 12, n) + key) % 12]) + (n >= 12 ? "+" : "");
}

constexpr int kLabelW = 64, kNoteH = 84, kRowH = 22, kGap = 3;
} // namespace

// ============================================================================================ StepGrid

juce::Rectangle<float> StepGrid::cell (int step, int row) const
{
    const float w = (getWidth() - kLabelW) / (float) skacid::NumSteps;
    const float y = row == NoteRow ? 0.0f : (float) (kNoteH + kGap + (row - 1) * (kRowH + kGap));
    return { kLabelW + step * w + 1.5f, y, w - 3.0f, (float) (row == NoteRow ? kNoteH : kRowH) };
}

int StepGrid::stepAt (float x) const
{
    const float w = (getWidth() - kLabelW) / (float) skacid::NumSteps;
    return x < kLabelW ? -1 : juce::jlimit (0, skacid::NumSteps - 1, (int) ((x - kLabelW) / w));
}

int StepGrid::rowAt (float y) const
{
    if (y < kNoteH) return NoteRow;
    return juce::jlimit (1, NumRows - 1, 1 + (int) ((y - kNoteH - kGap) / (kRowH + kGap)));
}

void StepGrid::update()
{
    const auto p = proc.pattern();
    const int step = proc.engine.playStep.load();
    const int len = (int) proc.v ("seqLen");
    const uint32_t mask = proc.v ("mutate") > 0.0f ? proc.engine.mutatedMask.load() : 0u;
    const int key = (int) proc.v ("seqKey");
    bool same = step == shownStep && len == shownLen && mask == shownMask && key == shownKey;
    for (int i = 0; i < skacid::NumSteps && same; ++i)
    {
        const auto &a = p[(size_t) i], &b = shown[(size_t) i];
        same = a.note == b.note && a.oct == b.oct && a.gate == b.gate && a.acc == b.acc && a.sld == b.sld;
    }
    if (same) return;
    shown = p; shownStep = step; shownLen = len; shownMask = mask; shownKey = key;
    repaint();
}

void StepGrid::paint (juce::Graphics& g)
{
    using namespace juce;
    static const char* rowNames[] = { "NOTE", "OCTAVE", "GATE", "ACCENT", "GLIDE" };
    g.setFont (skin::font (9.5f));
    for (int r = 0; r < NumRows; ++r)
    {
        auto c = cell (0, r);
        g.setColour (inkC);
        g.drawText (rowNames[r], 0, (int) c.getY(), kLabelW - 6, (int) c.getHeight(), Justification::centredLeft);
    }

    auto drawLed = [&g] (Rectangle<float> l, Colour col, bool on, float alpha)
    {
        if (on)
        {
            g.setColour (col.withAlpha (alpha * 0.35f));
            g.fillEllipse (l.expanded (3.0f));
        }
        g.setColour (on ? col.withAlpha (alpha) : col.darker (0.85f).withAlpha (alpha));
        g.fillEllipse (l);
        g.setColour (Colours::white.withAlpha (on ? 0.5f : 0.12f));
        g.fillEllipse (l.reduced (l.getWidth() * 0.3f).translated (-0.6f, -0.6f));
    };

    for (int i = 0; i < skacid::NumSteps; ++i)
    {
        const auto& st = shown[(size_t) i];
        const bool inLoop = i < shownLen, playing = i == shownStep, rest = st.gate == skacid::Rest;
        const float alpha = inLoop ? 1.0f : 0.35f;

        // note cell: a white key strip with a dark bar as high as the note, the name printed on top
        auto nc = cell (i, NoteRow);
        g.setGradientFill (ColourGradient (Colour (0xfff1f1f0).withAlpha (alpha), 0, nc.getY(), Colour (0xffcfd1d4).withAlpha (alpha), 0, nc.getBottom(), false));
        g.fillRoundedRectangle (nc, 2.5f);
        if (! rest)
        {
            const float h = 8.0f + (nc.getHeight() - 42.0f) * st.note / 12.0f; // stays under the printed name
            auto bar = nc.reduced (6.0f, 0).withTop (nc.getBottom() - h - 4.0f).withBottom (nc.getBottom() - 4.0f);
            const auto col = (st.acc ? led : Colour (0xff2a2b2e)).withAlpha (alpha * (st.gate == skacid::Tie ? 0.4f : 1.0f));
            g.setColour (col);
            g.fillRoundedRectangle (bar, 2.0f);
        }
        g.setColour (inkC.withAlpha (alpha * (rest ? 0.4f : 1.0f)));
        g.setFont (skin::font (11.0f));
        g.drawText (rest ? String ("-") : (st.gate == skacid::Tie ? String ("~") : noteName (st.note, shownKey)),
                    nc.withTrimmedTop (12.0f).withHeight (16.0f).toNearestInt(), Justification::centred);
        drawLed ({ nc.getCentreX() - 3.5f, nc.getY() + 4.0f, 7.0f, 7.0f }, led, playing, 1.0f);
        g.setColour (playing ? led : Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (nc, 2.5f, playing ? 2.0f : 1.0f);
        if ((shownMask >> i) & 1u)
            drawLed ({ nc.getRight() - 9.0f, nc.getY() + 4.0f, 5.0f, 5.0f }, amber, true, 1.0f);

        // the small rows: dark rubber keys, with an LED for accent and slide
        for (int r = OctRow; r < NumRows; ++r)
        {
            auto c = cell (i, r);
            bool on = false; String text;
            switch (r)
            {
                case OctRow:  on = st.oct != skacid::Mid; text = st.oct == skacid::Up ? "UP" : (st.oct == skacid::Down ? "DN" : ""); break;
                case GateRow: on = st.gate != skacid::Rest; text = st.gate == skacid::Tie ? "TIE" : (on ? "NOTE" : ""); break;
                case AccRow:  on = st.acc; break;
                default:      on = st.sld; break;
            }
            const bool lit = on && r < AccRow;
            g.setGradientFill (ColourGradient ((lit ? Colour (0xff5b5e63) : Colour (0xff45474b)).withAlpha (alpha), 0, c.getY(),
                                               (lit ? Colour (0xff3a3c40) : Colour (0xff2a2b2e)).withAlpha (alpha), 0, c.getBottom(), false));
            g.fillRoundedRectangle (c, 2.5f);
            g.setColour (Colours::black.withAlpha (0.7f));
            g.drawRoundedRectangle (c, 2.5f, 1.0f);
            if (r >= AccRow)
                drawLed (c.withSizeKeepingCentre (8.0f, 8.0f), r == AccRow ? led : amber, on, alpha);
            else if (text.isNotEmpty())
            {
                g.setColour (Colours::white.withAlpha (alpha));
                g.setFont (skin::font (9.0f));
                g.drawText (text, c.toNearestInt(), Justification::centred);
            }
        }
    }
}

void StepGrid::setNote (int step, int note)
{
    proc.setStepField (step, "note", (float) juce::jlimit (0, 12, note));
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int step = stepAt ((float) e.x);
    if (step < 0) return;
    const auto& st = shown[(size_t) step];
    switch (rowAt ((float) e.y))
    {
        case NoteRow: dragStep = step; dragStartNote = st.note; dragStartY = (float) e.y; proc.beginStepEdit (step, "note"); return;
        case OctRow:  proc.setStepField (step, "oct", (float) (st.oct == skacid::Mid ? skacid::Up : (st.oct == skacid::Up ? skacid::Down : skacid::Mid))); break;
        case GateRow: proc.setStepField (step, "gate", (float) ((st.gate + 1) % 3)); break;
        case AccRow:  proc.setStepField (step, "acc", st.acc ? 0.0f : 1.0f); break;
        default:      proc.setStepField (step, "sld", st.sld ? 0.0f : 1.0f); break;
    }
    dragStep = -1;
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0) return;
    setNote (dragStep, dragStartNote + juce::roundToInt ((dragStartY - (float) e.y) / 7.0f));
    update(); // show the new note right away, not on the next timer tick
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (dragStep >= 0) proc.endStepEdit (dragStep, "note");
    dragStep = -1;
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int step = stepAt ((float) e.x);
    if (step < 0 || rowAt ((float) e.y) != NoteRow) return;
    proc.setStepField (step, "gate", shown[(size_t) step].gate == skacid::Rest ? (float) skacid::Note : (float) skacid::Rest);
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int step = stepAt ((float) e.x);
    if (step < 0 || rowAt ((float) e.y) != NoteRow) return;
    proc.undo.beginNewTransaction();
    setNote (step, shown[(size_t) step].note + (w.deltaY > 0 ? 1 : -1));
}

// ============================================================================================ CutoffView

void CutoffView::update()
{
    const float f = proc.engine.cutoffNow.load();
    peakHold = juce::jmax (f, peakHold * 0.97f);
    if (shown < 0.0f || std::abs (std::log2 (f / shown)) > 0.02f)
    {
        shown = f;
        repaint();
    }
}

void CutoffView::paint (juce::Graphics& g)
{
    using namespace juce;
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0b0b0c));
    g.fillRoundedRectangle (r, 3.0f);
    auto in = r.reduced (2.0f);
    auto pos = [&] (float hz) { return jlimit (0.0f, 1.0f, std::log2 (hz / 40.0f) / std::log2 (18000.0f / 40.0f)); };
    const float x = pos (jmax (40.0f, shown));
    g.setGradientFill (ColourGradient (led.withAlpha (0.25f), in.getX(), 0, led, in.getX() + in.getWidth(), 0, false));
    g.fillRoundedRectangle (in.withWidth (in.getWidth() * x), 2.0f);
    g.setColour (rust.withAlpha (0.8f));
    const float px = in.getX() + in.getWidth() * pos (jmax (40.0f, peakHold));
    g.drawLine (px, in.getY(), px, in.getBottom(), 1.5f);
    g.setColour (skin::white);
    g.setFont (skin::font (10.0f));
    g.drawText ("CUTOFF NOW  " + skazi::formatValue (shown, "Hz"), r.toNearestInt().reduced (8, 0), Justification::centredRight);
    g.setColour (skin::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
}

// ============================================================================================ editor

SkacidEditor::SkacidEditor (SkacidProcessor& p)
    : EditorBase (p, {}, {}, {}, // SKACID, the byline and SHIGAON are drawn big in paintExtras
                  juce::String::fromUTF8 ("SKACID BY ASHER SWISSA  \xc2\xb7  SPECIAL SHIGAON EDITION  \xc2\xb7  v1.1")),
      sp (p), grid (p), cutoffView (p)
{
    using namespace skazi;
    wave = &addChoiceRow ("wave", { icons::saw, icons::square }, led);
    sub = &addKnob ("sub", "SUB", led, "%", 34);
    tune = &addKnob ("tune", "TUNE", led, "st", 34);
    fine = &addKnob ("fine", "FINE", led, "ct", 34);

    cutoff = &addKnob ("cutoff", "CUTOFF", led, "Hz", 58);
    reso = &addKnob ("reso", "RESONANCE", led, "%", 58);
    envMod = &addKnob ("envMod", "ENV MOD", led, "%", 38);
    decay = &addKnob ("decay", "DECAY", led, "ms", 38);
    accent = &addKnob ("accent", "ACCENT", led, "%", 38);
    fDrive = &addKnob ("fDrive", "DRIVE", led, "%", 38);

    lfoShape = &addChoiceRow ("lfoShape", { icons::sine, icons::tri, icons::saw, icons::square, icons::sh }, led);
    lfoTarget = &addChoiceRow ("lfoTarget", {}, led);
    lfoRate = &addCombo ("lfoRate");
    lfoDepth = &addKnob ("lfoDepth", "DEPTH", led, "%", 34);
    drift = &addKnob ("drift", "DRIFT", rust, "%", 34);

    outMeter = &addMeter (sp.engine.outLevel);
    volume = &addKnob ("volume", "VOLUME", skin::white, "dB", 48);

    distType = &addChoiceRow ("distType", {}, led);
    distDrive = &addKnob ("distDrive", "DRIVE", led, "%", 40);
    distTone = &addKnob ("distTone", "TONE", led, "%", 40);
    distMix = &addKnob ("distMix", "MIX", led, "%", 40);

    dlyTime = &addCombo ("dlyTime");
    dlyFb = &addKnob ("dlyFb", "FEEDBACK", led, "%", 40);
    dlyMix = &addKnob ("dlyMix", "MIX", led, "%", 40);
    revSize = &addKnob ("revSize", "SIZE", led, "%", 40);
    revMix = &addKnob ("revMix", "MIX", led, "%", 40);

    fltType = &addChoiceRow ("fltType", {}, led);
    keyTrack = &addKnob ("keyTrack", "KEY TRACK", led, "%", 40);
    fAttack = &addKnob ("fAttack", "ENV ATTACK", led, "ms", 40);
    ampAtk = &addKnob ("ampAtk", "ATTACK", led, "ms", 40);
    ampDec = &addKnob ("ampDec", "DECAY", led, "ms", 40);
    ampSus = &addKnob ("ampSus", "SUSTAIN", led, "%", 40);
    ampRel = &addKnob ("ampRel", "RELEASE", led, "ms", 40);
    vca = &addKnob ("vca", "VCA POWER", led, "%", 40);
    vca->slider.setTooltip ("Pushes the 303 VCA harder: louder, punchier accents, fuller bass");
    glideOn = &addToggle ("glideOn", "GLIDE");
    glideOn->setTooltip ("Every note glides from the last one (portamento), not just the slide steps");
    glide = &addKnob ("glide", "TIME", led, "ms", 30);
    ottDepth = &addKnob ("ottDepth", "DEPTH", rust, "%", 40);
    ottTime = &addKnob ("ottTime", "TIME", led, "%", 40);
    keyTrack->slider.setTooltip ("The cutoff follows the notes: higher notes open the filter more");
    fAttack->slider.setTooltip ("The filter envelope rises over this time instead of snapping open");
    ottDepth->slider.setTooltip ("3-band upward and downward compression after the distortion: louder, denser, more power");

    // SEQ: the pattern plays with the host (or while a key is held), keys transpose it, C3 = as written.
    // ARP: held keys are arpeggiated on the grid's rhythm, accents and slides. MIDI: overlapping notes slide, velocity 100+ accents.
    playMode = &addChoiceRow ("playMode", {}, led);
    seqKey = &addChoiceRow ("seqKey", {}, led);
    arpMode = &addCombo ("arpMode");
    arpMode->setTooltip ("Arp order for the held keys");
    arpOct = &addKnob ("arpOct", "ARP OCT", led, "", 30);
    seqRate = &addChoiceRow ("seqRate", {}, led);
    seqLen = &addKnob ("seqLen", "LENGTH", led, "", 30);
    swing = &addKnob ("swing", "SWING", led, "%", 30);
    slide = &addKnob ("slide", "GLIDE", led, "ms", 30);
    gateLen = &addKnob ("gateLen", "GATE", led, "%", 30);
    chance = &addKnob ("chance", "CHANCE", rust, "%", 30);
    mutate = &addKnob ("mutate", "MUTATE", rust, "%", 30);
    ratchet = &addKnob ("ratchet", "RATCHET", rust, "%", 30);
    chance->slider.setTooltip ("Chance that each note step plays");
    mutate->slider.setTooltip ("Live mutation: every loop, changes some notes, accents, slides and octaves. Your pattern stays as written.");
    ratchet->slider.setTooltip ("Chance that a step plays as a fast 2 or 3 note roll");
    drift->slider.setTooltip ("Slow random wander of the cutoff, so the line never repeats exactly");

    style.addItemList ({ "CLASSIC", "ROLLING", "SQUELCH", "RAVE", "DUB", "MUTANT" }, 1);
    style.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (style);
    for (auto* b : { &generateBtn, &mutateBtn, &clearBtn, &leftBtn, &rightBtn })
        addAndMakeVisible (b);
    generateBtn.onClick = [this] { sp.generatePattern (style.getSelectedId() - 1); };
    mutateBtn.onClick = [this] { sp.mutatePattern(); };
    clearBtn.onClick = [this] { sp.clearPattern(); };
    leftBtn.onClick = [this] { sp.shiftPattern (-1); };
    rightBtn.onClick = [this] { sp.shiftPattern (1); };
    generateBtn.setTooltip ("New pattern in the chosen style");
    const std::pair<const char*, const char*> tools[] = {
        { "NOTES +", "Every note one semitone up" }, { "NOTES -", "Every note one semitone down" },
        { "RND NOTES", "New notes from the pattern's own scale, rhythm stays" }, { "RND ACCENT", "New accents" },
        { "RND GLIDE", "New glides" }, { "GLIDE ALL", "Glide on every step" }, { "GLIDE OFF", "No glides" },
        { "REVERSE", "Play the pattern backwards" }, { "COPY", "Copy this pattern" }, { "PASTE", "Paste the copied pattern" } };
    for (int t = 0; t < (int) std::size (tools); ++t)
    {
        auto* b = toolBtns.add (new juce::TextButton (tools[t].first));
        b->setTooltip (tools[t].second);
        b->onClick = [this, t] { sp.patternTool (t); };
        addAndMakeVisible (b);
    }
    mutateBtn.setTooltip ("Change a few steps of the pattern for good");

    addAndMakeVisible (grid);
    addAndMakeVisible (cutoffView);
    // bass-machine dress: capped knobs on the silver panel, black printing everywhere
    allKnobs = { sub, tune, fine, cutoff, reso, envMod, decay, accent, fDrive, lfoDepth, drift, volume, distDrive, distTone, distMix,
                 dlyFb, dlyMix, revSize, revMix, seqLen, swing, slide, gateLen, chance, mutate, ratchet, arpOct,
                 keyTrack, fAttack, ampAtk, ampDec, ampSus, ampRel, ottDepth, ottTime, vca, glide };
    for (auto* k : allKnobs)
    {
        k->slider.setLookAndFeel (&tbLook);
        k->nameColour = inkC;
        for (auto* child : k->getChildren())
            if (auto* l = dynamic_cast<juce::Label*> (child))
                l->setColour (juce::Label::textColourId, inkC.withAlpha (0.75f));
    }

    setSize (1120, 870);
}

SkacidEditor::~SkacidEditor()
{
    for (auto* k : allKnobs) k->slider.setLookAndFeel (nullptr);
}

void SkacidEditor::paintExtras (juce::Graphics& g)
{
    using namespace juce;
    // the title, big: SK in black, ACID in red, the byline under it, then SPECIAL SHIGAON EDITION
    {
        const auto tf = skin::titleFont (66.0f);
        const int w1 = (int) GlyphArrangement::getStringWidth (tf, "SK"), w2 = (int) GlyphArrangement::getStringWidth (tf, "ACID");
        skin::engrave (g, "SK", { 202, 0, w1 + 4, 62 }, tf, Colour (0xff26282c), Justification::left);
        skin::engrave (g, "ACID", { 202 + w1, 0, w2 + 4, 62 }, tf, skin::red, Justification::left);
        skin::engrave (g, "BY ASHER SWISSA", { 205, 56, 300, 13 }, skin::font (10.5f).withExtraKerningFactor (0.12f), Colour (0xff3b3e43), Justification::left);
        const int x = 202 + w1 + w2 + 16;
        const auto small = skin::font (10.9f).withExtraKerningFactor (0.3f);
        skin::engrave (g, "SPECIAL", { x + 1, 9, 180, 13 }, small, Colour (0xff3b3e43), Justification::left);
        skin::engrave (g, "SHIGAON", { x, 20, 230, 32 }, skin::titleFont (31.0f), skin::red, Justification::left);
        skin::engrave (g, "EDITION", { x + 1, 52, 180, 13 }, small, Colour (0xff3b3e43), Justification::left);
    }
    // the step deck: a darker grey plate with an acid-green stripe along its top edge
    {
        const auto d = deck.toFloat();
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (d.translated (0.0f, 2.0f), 6.0f);
        g.setGradientFill (ColourGradient (Colour (0xffb3b6ba), 0, d.getY(), Colour (0xff8f9297), 0, d.getBottom(), false));
        g.fillRoundedRectangle (d, 6.0f);
        g.setColour (acidStripe);
        g.fillRect (d.getX() + 6.0f, d.getY() + 2.0f, d.getWidth() - 12.0f, 4.0f);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (d, 6.0f, 1.2f);
        g.setColour (inkC);
        g.setFont (skin::font (12.5f));
        g.drawText ("SEQUENCER", deck.getX() + 12, deck.getY() + 10, 140, 18, Justification::left);
    }

    // section titles printed on the panel, with an engraved line under each
    for (auto& sct : sections)
    {
        auto a = sct.area;
        g.setColour (inkC);
        g.setFont (skin::font (12.5f));
        const int tw = GlyphArrangement::getStringWidthInt (g.getCurrentFont(), sct.title);
        g.drawText (sct.title, a.getX() + 4, a.getY() + 4, tw + 4, 18, Justification::left);
        const float ly = (float) a.getY() + 13.5f;
        g.setColour (inkC.withAlpha (0.55f));
        g.drawLine ((float) a.getX() + tw + 14.0f, ly, (float) a.getRight() - 4.0f, ly, 1.2f);
        g.setColour (Colours::white.withAlpha (0.6f));
        g.drawLine ((float) a.getX() + tw + 14.0f, ly + 1.2f, (float) a.getRight() - 4.0f, ly + 1.2f, 0.8f);
        // thin divider on the right, between sections
        g.setColour (inkC.withAlpha (0.18f));
        g.drawLine ((float) a.getRight() + 4.0f, (float) a.getY() + 4.0f, (float) a.getRight() + 4.0f, (float) a.getBottom() - 4.0f, 1.0f);
    }
    g.setFont (skin::font (9.5f));
    for (auto& l : inkLabels)
    {
        g.setColour (inkC.withAlpha (0.8f));
        g.drawText (l.title, l.area, Justification::centredLeft);
    }
}

void TbLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    using namespace juce;
    const float size = (float) jmin (w, h);
    const auto c = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    const float r = size * 0.5f - 1.5f;
    juce::ignoreUnused (s);
    const Colour print { 0xff161616 };

    // scale: 11 ticks printed around the knob
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * i / 10.0f;
        const auto p1 = c.getPointOnCircumference (r * 0.86f, a), p2 = c.getPointOnCircumference (r, a);
        g.setColour (print.withAlpha (i == 0 || i == 10 || i == 5 ? 0.9f : 0.55f));
        g.drawLine ({ p1, p2 }, i == 0 || i == 10 || i == 5 ? 1.6f : 1.0f);
    }

    // black cap with ribbed skirt
    const float kr = r * 0.74f;
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (c.x - kr + 1.0f, c.y - kr + 2.5f, kr * 2, kr * 2);
    g.setColour (Colour (0xff0c0c0d));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    for (int i = 0; i < 24; ++i)
    {
        const float a = MathConstants<float>::twoPi * i / 24.0f;
        g.setColour (Colour (0xff2c2d30));
        g.drawLine ({ c.getPointOnCircumference (kr * 0.82f, a), c.getPointOnCircumference (kr * 0.98f, a) }, 1.0f);
    }
    const float tr = kr * 0.7f;
    ColourGradient cap (Colour (0xfff4f5f6), c.x - tr * 0.5f, c.y - tr * 0.8f, Colour (0xff8a8d92), c.x + tr * 0.6f, c.y + tr, true);
    cap.addColour (0.5, Colour (0xffc9ccd0));
    g.setGradientFill (cap);
    g.fillEllipse (c.x - tr, c.y - tr, tr * 2, tr * 2);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - tr, c.y - tr, tr * 2, tr * 2, 0.8f);

    // black index line across the cap, with an acid dot on the skirt where it points
    const float ang = a0 + pos * (a1 - a0);
    g.setColour (Colour (0xff141414));
    g.drawLine ({ c.getPointOnCircumference (tr * 0.15f, ang), c.getPointOnCircumference (tr * 0.95f, ang) }, jmax (1.6f, kr * 0.1f));
    const auto dot = c.getPointOnCircumference (kr * 0.86f, ang);
    const float dr = jmax (1.6f, kr * 0.09f);
    g.setColour (acidStripe);
    g.fillEllipse (dot.x - dr, dot.y - dr, dr * 2, dr * 2);
}

void SkacidEditor::layout()
{
    // row 1: oscillator, filter, modulation, output
    int y = 72;
    const int h1 = 172;
    section ({ 20, y, 176, h1 }, "OSCILLATOR");
    wave->setBounds (30, y + 32, 156, 28);
    put (*sub, 24, y + 84, 34, 56);
    put (*tune, 80, y + 84, 34, 56);
    put (*fine, 136, y + 84, 34, 56);

    section ({ 204, y, 444, h1 }, "FILTER");
    put (*cutoff, 214, y + 28, 58, 84);
    put (*reso, 298, y + 28, 58, 84);
    int kx = 384;
    for (auto* k : { envMod, decay, accent, fDrive }) { put (*k, kx, y + 40, 38, 64); kx += 64; }
    cutoffView.setBounds (214, y + 134, 424, 24);

    section ({ 656, y, 250, h1 }, "MOD");
    lfoShape->setBounds (666, y + 30, 230, 26);
    lfoTarget->setBounds (666, y + 62, 230, 22);
    ink ({ 666, y + 92, 80, 14 }, "LFO RATE");
    lfoRate->setBounds (666, y + 108, 80, 24);
    put (*lfoDepth, 756, y + 94, 34, 66);
    put (*drift, 828, y + 94, 34, 66);

    section ({ 914, y, 186, h1 }, "OUTPUT");
    outMeter->setBounds (928, y + 32, 14, 124);
    put (*volume, 958, y + 52, 48, 128);

    // row 2: the corrosive rack
    y = 252;
    const int h2 = 118;
    section ({ 20, y, 520, h2 }, "DISTORTION");
    distType->setBounds (30, y + 40, 236, 30);
    kx = 286;
    for (auto* k : { distDrive, distTone, distMix }) { put (*k, kx, y + 30, 40, 80); kx += 84; }

    section ({ 548, y, 300, h2 }, "DELAY");
    ink ({ 560, y + 36, 90, 14 }, "TIME");
    dlyTime->setBounds (558, y + 52, 90, 24);
    put (*dlyFb, 662, y + 30, 40, 80);
    put (*dlyMix, 750, y + 30, 40, 80);

    section ({ 856, y, 244, h2 }, "REVERB");
    put (*revSize, 876, y + 30, 40, 90);
    put (*revMix, 984, y + 30, 40, 90);

    // row 3: filter type, amp envelope, OTT
    y = 378;
    section ({ 20, y, 404, h2 }, "FILTER TYPE");
    fltType->setBounds (30, y + 40, 220, 30);
    put (*keyTrack, 270, y + 30, 40, 80);
    put (*fAttack, 350, y + 30, 40, 80);

    section ({ 432, y, 330, h2 }, "AMP / VCA");
    kx = 440;
    for (auto* k : { ampAtk, ampDec, ampSus, ampRel, vca }) { put (*k, kx, y + 30, 40, 64); kx += 64; }

    section ({ 770, y, 78, h2 }, "GLIDE");
    glideOn->setBounds (778, y + 30, 62, 22);
    put (*glide, 780, y + 58, 30, 58);

    section ({ 856, y, 244, h2 }, "OTT");
    put (*ottDepth, 876, y + 30, 40, 80);
    put (*ottTime, 984, y + 30, 40, 80);

    // row 4: sequencer
    y = 504;
    deck = { 20, y, 1080, 322 };
    ink ({ 150, y + 12, 70, 14 }, "TRACK KEY");
    seqKey->setBounds (222, y + 8, 456, 22);
    ink ({ 32, y + 36, 140, 12 }, "PLAY MODE");
    playMode->setBounds (30, y + 50, 150, 28);
    ink ({ 190, y + 36, 140, 12 }, "RATE");
    seqRate->setBounds (188, y + 50, 160, 28);
    ink ({ 358, y + 36, 80, 12 }, "ARP");
    arpMode->setBounds (356, y + 52, 86, 24);
    kx = 446;
    for (auto* k : { arpOct, seqLen, swing, slide, gateLen, chance, mutate, ratchet }) { put (*k, kx, y + 26, 30, 46); kx += 46; }
    style.setBounds (820, y + 30, 86, 24);
    generateBtn.setBounds (912, y + 30, 86, 24);
    mutateBtn.setBounds (1002, y + 30, 86, 24);
    ink ({ 820, y + 62, 90, 14 }, "PATTERN STYLE");
    clearBtn.setBounds (912, y + 60, 86, 24);
    leftBtn.setBounds (1002, y + 60, 41, 24);
    rightBtn.setBounds (1047, y + 60, 41, 24);

    // tools row
    {
        const int bw = 98, gap = 8;
        int bx = 30;
        for (auto* b : toolBtns) { b->setBounds (bx, y + 92, bw, 24); bx += bw + gap; }
    }
    grid.setBounds (30, y + 128, 1060, kNoteH + 4 * (kRowH + kGap));
}

void SkacidEditor::tick()
{
    grid.update();
    cutoffView.update();
}
