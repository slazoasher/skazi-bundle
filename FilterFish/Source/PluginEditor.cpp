#include "PluginEditor.h"
#include "Presets.h"
#if SKAZI_HAS_LOGO
 #include "BinaryData.h"
#endif

using namespace juce;

//==============================================================================
namespace
{
    Font mono (float height, bool bold = false)
    {
        return Font (FontOptions (Font::getDefaultMonospacedFontName(), height, bold ? Font::bold : Font::plain));
    }

    Font stencil (float height)
    {
        return Font (FontOptions (height, Font::bold)).withExtraKerningFactor (0.12f);
    }

    Rectangle<float> circle (Point<float> centre, float radius)
    {
        return Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    }

    // Procedural brushed metal: base gradient + thousands of faint horizontal streaks.
    Image makeBrushedMetal (int w, int h, Colour base, int seed)
    {
        Image img (Image::RGB, jmax (1, w), jmax (1, h), true);
        Graphics g (img);
        g.setGradientFill (ColourGradient (base.brighter (0.25f), 0.0f, 0.0f, base.darker (0.35f), 0.0f, (float) h, false));
        g.fillAll();

        Random r (seed);
        for (int y = 0; y < h; ++y)
        {
            g.setColour ((r.nextBool() ? Colours::white : Colours::black).withAlpha (r.nextFloat() * 0.07f));
            g.fillRect (0, y, w, 1);
        }
        for (int i = 0; i < w * h / 300; ++i)
        {
            const int y = r.nextInt (h), x = r.nextInt (w), len = 20 + r.nextInt (160);
            g.setColour ((r.nextBool() ? Colours::white : Colours::black).withAlpha (0.04f + r.nextFloat() * 0.06f));
            g.fillRect (x, y, len, 1);
        }
        return img;
    }

    void drawScrew (Graphics& g, Point<float> c, float radius, float slotAngle)
    {
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillEllipse (circle (c.translated (0.6f, 0.9f), radius + 0.6f));
        g.setGradientFill (ColourGradient (Colour (0xffe9ecef), c.x - radius, c.y - radius, Colour (0xff50555c), c.x + radius, c.y + radius, false));
        g.fillEllipse (circle (c, radius));
        g.setColour (Colours::black.withAlpha (0.7f));
        g.drawLine (Line<float> (c.getPointOnCircumference (radius * 0.75f, slotAngle),
                                 c.getPointOnCircumference (radius * 0.75f, slotAngle + MathConstants<float>::pi)), 1.2f);
    }

    void drawLamp (Graphics& g, Point<float> c, float radius, Colour colour, bool lit)
    {
        g.setGradientFill (ColourGradient (Colour (0xffdfe3e7), c.x, c.y - radius * 1.5f, Colour (0xff4b5056), c.x, c.y + radius * 1.5f, false));
        g.fillEllipse (circle (c, radius * 1.45f));
        if (lit)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.fillEllipse (circle (c, radius * 2.4f));
            g.setGradientFill (ColourGradient (colour.brighter (0.9f), c.x - radius * 0.3f, c.y - radius * 0.4f, colour.darker (0.3f), c.x + radius, c.y + radius, true));
        }
        else
        {
            g.setGradientFill (ColourGradient (colour.darker (2.2f), c.x, c.y - radius, Colour (0xff0d0d0e), c.x, c.y + radius, false));
        }
        g.fillEllipse (circle (c, radius));
        g.setColour (Colours::white.withAlpha (lit ? 0.55f : 0.15f));
        g.fillEllipse (circle (c.translated (-radius * 0.3f, -radius * 0.35f), radius * 0.3f));
    }

    // Recessed black window cut into the metal (LCDs, CRT, title plates).
    void drawRecess (Graphics& g, Rectangle<float> r, float corner, Colour fill)
    {
        g.setColour (Colours::white.withAlpha (0.22f));
        g.drawRoundedRectangle (r.translated (0.0f, 1.0f), corner, 1.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.7f), 0.0f, r.getY(), Colours::transparentBlack, 0.0f, r.getY() + 7.0f, false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (Colours::black);
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    void drawGlowText (Graphics& g, const String& text, Rectangle<float> r, Colour colour, Justification just)
    {
        g.setColour (colour.withAlpha (0.25f));
        for (auto d : { Point<float> (-1, 0), Point<float> (1, 0), Point<float> (0, -1), Point<float> (0, 1) })
            g.drawText (text, r.translated (d.x, d.y), just, false);
        g.setColour (colour);
        g.drawText (text, r, just, false);
    }
}

//==============================================================================
GlitchLookAndFeel::GlitchLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, theme::hull);
    setColour (Label::textColourId, theme::ink);
    setColour (Slider::textBoxTextColourId, theme::lcdText);
    setColour (Slider::textBoxOutlineColourId, Colours::black);
    setColour (Slider::textBoxBackgroundColourId, theme::lcd);
    setColour (Slider::textBoxHighlightColourId, theme::lcdText.withAlpha (0.3f));
    setColour (ComboBox::backgroundColourId, theme::lcd);
    setColour (ComboBox::outlineColourId, Colours::black);
    setColour (ComboBox::textColourId, theme::lcdText);
    setColour (ComboBox::arrowColourId, theme::lcdText);
    setColour (PopupMenu::backgroundColourId, Colour (0xff111315));
    setColour (PopupMenu::textColourId, theme::lcdText);
    setColour (PopupMenu::headerTextColourId, theme::cream);
    setColour (PopupMenu::highlightedBackgroundColourId, theme::lcdText.withAlpha (0.3f));
    setColour (PopupMenu::highlightedTextColourId, theme::cream);
    setColour (TextButton::buttonColourId, Colour (0xff3a3e44));
    setColour (TextButton::textColourOffId, theme::cream);
    setColour (TextButton::textColourOnId, theme::cream);
    setColour (TextEditor::backgroundColourId, theme::lcd);
    setColour (TextEditor::textColourId, theme::lcdText);
}

void GlitchLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          float startAngle, float endAngle, Slider& s)
{
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const auto accent = s.findColour (Slider::rotarySliderFillColourId);
    const bool big = radius > 60.0f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Engraved scale
    const int ticks = big ? 41 : 11;
    for (int i = 0; i < ticks; ++i)
    {
        const float a = startAngle + (float) i / (float) (ticks - 1) * (endAngle - startAngle);
        const bool major = i % (big ? 10 : 5) == 0;
        const auto p1 = c.getPointOnCircumference (radius * 0.99f, a);
        const auto p2 = c.getPointOnCircumference (radius * (major ? 0.87f : 0.93f), a);
        g.setColour (Colours::white.withAlpha (0.3f));
        g.drawLine (Line<float> (p1.translated (0.6f, 0.8f), p2.translated (0.6f, 0.8f)), major ? 1.6f : 1.0f);
        g.setColour (theme::ink);
        g.drawLine (Line<float> (p1, p2), major ? 1.6f : 1.0f);
    }

    // Lamp ring = the value
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zero = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float lo = jmin (zero, sliderPos), hi = jmax (zero, sliderPos);
    const int lamps = big ? 31 : 11;
    const float lampR = radius * 0.79f;
    const float lampSize = jmax (1.7f, radius * (big ? 0.03f : 0.065f));
    for (int i = 0; i < lamps; ++i)
    {
        const float t = (float) i / (float) (lamps - 1);
        const auto p = c.getPointOnCircumference (lampR, startAngle + t * (endAngle - startAngle));
        const bool lit = t >= lo - 0.02f && t <= hi + 0.02f;
        if (lit)
        {
            g.setColour (accent.withAlpha (0.3f));
            g.fillEllipse (circle (p, lampSize * 2.3f));
            g.setColour (accent.brighter (0.5f));
        }
        else
        {
            g.setColour (Colour (0xff24262a));
        }
        g.fillEllipse (circle (p, lampSize));
    }

    // Knurled chrome skirt (the knurl turns with the knob)
    const float rs = radius * 0.66f;
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillEllipse (circle (c.translated (0.0f, rs * 0.08f), rs + 1.5f));
    g.setGradientFill (ColourGradient (Colour (0xfff4f6f8), c.x - rs, c.y - rs, Colour (0xff474c53), c.x + rs, c.y + rs, false));
    g.fillEllipse (circle (c, rs));
    const int knurls = big ? 72 : 32;
    g.setColour (Colours::black.withAlpha (0.3f));
    for (int i = 0; i < knurls; ++i)
    {
        const float a = angle + (float) i / (float) knurls * MathConstants<float>::twoPi;
        g.drawLine (Line<float> (c.getPointOnCircumference (rs, a), c.getPointOnCircumference (rs * 0.88f, a)), 1.0f);
    }

    // Black bakelite cap
    const float rc = rs * 0.8f;
    g.setGradientFill (ColourGradient (Colour (0xff3d3d40), c.x, c.y - rc, Colour (0xff050506), c.x, c.y + rc, false));
    g.fillEllipse (circle (c, rc));
    g.setColour (Colours::white.withAlpha (0.07f));
    g.fillEllipse (Rectangle<float> (rc * 1.3f, rc * 0.75f).withCentre (c.translated (0.0f, -rc * 0.42f)));
    g.setColour (Colours::black);
    g.drawEllipse (circle (c, rc), 1.0f);

    // Cream pointer + chrome centre
    g.setColour (theme::cream);
    g.drawLine (Line<float> (c.getPointOnCircumference (rc * 0.3f, angle), c.getPointOnCircumference (rs * 0.96f, angle)),
                jmax (2.0f, radius * 0.055f));
    const float rcc = rc * 0.24f;
    g.setGradientFill (ColourGradient (Colour (0xffffffff), c.x - rcc, c.y - rcc, Colour (0xff5d636a), c.x + rcc, c.y + rcc, false));
    g.fillEllipse (circle (c, rcc));

    // Live modulation lamp (big cutoff knob)
    if (s.getProperties().contains ("mod"))
    {
        const float m = jlimit (0.0f, 1.0f, (float) s.getProperties()["mod"]);
        const auto p = c.getPointOnCircumference (lampR, startAngle + m * (endAngle - startAngle));
        g.setColour (Colours::white.withAlpha (0.4f));
        g.fillEllipse (circle (p, lampSize * 3.0f));
        g.setColour (Colours::white);
        g.fillEllipse (circle (p, lampSize * 1.5f));
    }
}

void GlitchLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const auto accent = b.findColour (ToggleButton::tickColourId);
    const bool isOn = b.getToggleState();

    if (b.getButtonText().isEmpty())
    {
        // Jewel power button: chrome bezel + backlit cap
        if (isOn)
        {
            g.setColour (accent.withAlpha (0.3f));
            g.fillRoundedRectangle (r.expanded (1.5f), 6.0f);
        }
        g.setGradientFill (ColourGradient (Colour (0xffe3e6ea), 0.0f, r.getY(), Colour (0xff545960), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        const auto cap = r.reduced (3.0f);
        if (isOn) g.setGradientFill (ColourGradient (accent.brighter (0.6f), 0.0f, cap.getY(), accent.darker (0.4f), 0.0f, cap.getBottom(), false));
        else      g.setGradientFill (ColourGradient (Colour (0xff2c2e32), 0.0f, cap.getY(), Colour (0xff121315), 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        g.setColour (isOn ? Colours::black.withAlpha (0.8f) : Colour (0xff8a8f96).withAlpha (highlighted ? 1.0f : 0.8f));
        g.setFont (mono (11.0f, true));
        g.drawText (isOn ? "ON" : "OFF", cap, Justification::centred, false);
        return;
    }

    // Lamp + engraved label
    const float lampR = jmin (r.getHeight() * 0.3f, 6.0f);
    const Point<float> lc (r.getX() + lampR * 1.6f, r.getCentreY());
    drawLamp (g, lc, lampR, accent, isOn);
    g.setFont (stencil (11.0f));
    g.setColour (highlighted ? Colours::black : theme::ink);
    g.drawText (b.getButtonText(), r.withTrimmedLeft (lampR * 3.6f), Justification::centredLeft, false);
}

void GlitchLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f, 1.5f);
    drawRecess (g, r, 3.0f, box.findColour (ComboBox::backgroundColourId));

    Path arrow;
    const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.9f : 0.3f));
    g.fillPath (arrow);
}

Font GlitchLookAndFeel::getComboBoxFont (ComboBox& box)
{
    return mono (jmin (15.0f, (float) box.getHeight() * 0.56f));
}

void GlitchLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (5, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void GlitchLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& background, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 6.0f);
    g.setGradientFill (ColourGradient (Colour (0xffe6e9ed), 0.0f, r.getY(), Colour (0xff50555c), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 6.0f);

    auto base = background;
    if (down)             base = base.darker (0.35f);
    else if (highlighted) base = base.brighter (0.15f);
    const auto cap = r.reduced (3.0f).translated (0.0f, down ? 1.0f : 0.0f);
    g.setGradientFill (ColourGradient (base.brighter (0.4f), 0.0f, cap.getY(), base.darker (0.5f), 0.0f, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (Colours::white.withAlpha (0.12f));
    g.fillRoundedRectangle (cap.withHeight (cap.getHeight() * 0.45f).reduced (2.0f, 1.0f), 3.0f);
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

Font GlitchLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return stencil (jmin (15.0f, (float) buttonHeight * 0.45f));
}

Label* GlitchLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (mono (12.0f));
    return l;
}

//==============================================================================
Knob::Knob (APVTS& s, const String& paramId, const String& text, Colour accent, bool big)
    : attachment (s, paramId, slider), isBig (big)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, big ? 110 : 64, big ? 22 : 15);
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    slider.setDoubleClickReturnValue (true, (double) s.getParameter (paramId)->convertFrom0to1 (s.getParameter (paramId)->getDefaultValue()));
    addAndMakeVisible (slider);

    label.setText (text, dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setFont (stencil (big ? 16.0f : 11.0f));
    label.setColour (Label::textColourId, theme::ink);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (isBig ? 22 : 14));
    slider.setBounds (r);
}

ChoiceItem::ChoiceItem (APVTS& s, const String& paramId, const String& text)
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (s.getParameter (paramId)))
        box.addItemList (c->choices, 1);
    attachment = std::make_unique<APVTS::ComboBoxAttachment> (s, paramId, box);
    addAndMakeVisible (box);

    label.setText (text, dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setFont (stencil (11.0f));
    label.setColour (Label::textColourId, theme::ink);
    addAndMakeVisible (label);
}

void ChoiceItem::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (14));
    box.setBounds (r.reduced (0, 2));
}

ToggleItem::ToggleItem (APVTS& s, const String& paramId, const String& text, Colour accent)
    : button (text), attachment (s, paramId, button)
{
    button.setColour (ToggleButton::tickColourId, accent);
    addAndMakeVisible (button);
}

//==============================================================================
ModulePanel::ModulePanel (APVTS& s, String t, Colour a, const char* powerParam)
    : state (s), title (std::move (t)), accent (a)
{
    if (powerParam != nullptr)
    {
        power = std::make_unique<ToggleButton>();
        power->setColour (ToggleButton::tickColourId, accent);
        powerAttachment = std::make_unique<APVTS::ButtonAttachment> (state, powerParam, *power);
        power->onStateChange = [this] { repaint(); }; // title lamp follows the power switch
        addAndMakeVisible (*power);
    }
}

void ModulePanel::add (std::unique_ptr<Component> comp, int w, int h, const String& id)
{
    addAndMakeVisible (*comp);
    items.push_back ({ std::move (comp), w, h, id });
}

void ModulePanel::setDimmed (const char* paramId, bool dimmed)
{
    for (auto& it : items)
        if (it.id == paramId)
            it.comp->setAlpha (dimmed ? 0.35f : 1.0f);
}

ModulePanel& ModulePanel::big (const char* id, const String& text)
{
    bigKnob = std::make_unique<Knob> (state, id, text, accent, true);
    addAndMakeVisible (*bigKnob);
    return *this;
}

ModulePanel& ModulePanel::knob (const char* id, const String& text)
{
    add (std::make_unique<Knob> (state, id, text, accent, false), 64, 80, id);
    return *this;
}

ModulePanel& ModulePanel::choice (const char* id, const String& text)
{
    add (std::make_unique<ChoiceItem> (state, id, text), 96, 44, id);
    return *this;
}

ModulePanel& ModulePanel::toggle (const char* id, const String& text)
{
    add (std::make_unique<ToggleItem> (state, id, text, accent), 108, 34, id);
    return *this;
}

void ModulePanel::setExtra (Component* comp, int height)
{
    extra = comp;
    extraHeight = height;
    addAndMakeVisible (comp);
}

void ModulePanel::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    constexpr float corner = 8.0f;

    // Drop shadow onto the hull, then the brushed plate
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f), corner);
    {
        Graphics::ScopedSaveState save (g);
        Path plate;
        plate.addRoundedRectangle (r, corner);
        g.reduceClipRegion (plate);
        g.drawImageAt (texture, 0, 0);
    }

    // Bevel
    g.setColour (Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.0f, 1.0f);
    g.setColour (Colours::black.withAlpha (0.85f));
    g.drawRoundedRectangle (r, corner, 1.2f);

    // Screws
    const float inset = 10.0f;
    drawScrew (g, { r.getX() + inset, r.getY() + inset }, 3.5f, 0.6f);
    drawScrew (g, { r.getRight() - inset, r.getY() + inset }, 3.5f, 1.9f);
    drawScrew (g, { r.getX() + inset, r.getBottom() - inset }, 3.5f, 2.6f);
    drawScrew (g, { r.getRight() - inset, r.getBottom() - inset }, 3.5f, 0.2f);

    // Engraved title plate with a status lamp
    const auto font = stencil (13.0f);
    const float textW = GlyphArrangement::getStringWidth (font, title);
    const Rectangle<float> plateR (22.0f, 7.0f, textW + 34.0f, 20.0f);
    drawRecess (g, plateR, 3.0f, Colour (0xff0b0c0e));
    const bool lit = power == nullptr || power->getToggleState();
    drawLamp (g, { plateR.getX() + 10.0f, plateR.getCentreY() }, 3.2f, accent, lit);
    g.setFont (font);
    drawGlowText (g, title, plateR.withTrimmedLeft (20.0f), lit ? accent : accent.withAlpha (0.45f), Justification::centredLeft);
}

void ModulePanel::resized()
{
    texture = makeBrushedMetal (getWidth(), getHeight(), theme::metal, title.hashCode());

    auto r = getLocalBounds().reduced (10);
    auto header = r.removeFromTop (24);
    header.removeFromRight (12);
    if (power != nullptr)
        power->setBounds (header.removeFromRight (54).reduced (0, 1));
    r.removeFromTop (4);
    r = r.withTrimmedBottom (4);

    if (extra != nullptr)
    {
        extra->setBounds (r.removeFromBottom (extraHeight).reduced (6, 0));
        r.removeFromBottom (4);
    }

    if (bigKnob != nullptr)
    {
        const int size = jmin (r.getHeight(), roundToInt ((float) r.getWidth() * 0.58f));
        bigKnob->setBounds (r.removeFromLeft (size).withSizeKeepingCentre (size, jmin (r.getHeight(), size + 20)));
    }

    int tallest = 0;
    for (auto& it : items) tallest = jmax (tallest, it.h);

    FlexBox fb;
    fb.flexWrap = r.getHeight() >= tallest * 2 + 8 ? FlexBox::Wrap::wrap : FlexBox::Wrap::noWrap;
    fb.justifyContent = FlexBox::JustifyContent::spaceAround;
    fb.alignContent = FlexBox::AlignContent::center;
    fb.alignItems = FlexBox::AlignItems::center;
    for (auto& it : items)
        fb.items.add (FlexItem (*it.comp).withFlex (0.0f, 1.0f, (float) it.w).withHeight ((float) it.h).withMargin (2.0f));
    fb.performLayout (r);
}

//==============================================================================
void LfoDisplay::paint (Graphics& g)
{
    const auto outer = getLocalBounds().toFloat().reduced (1.0f);

    // Chrome bezel around a green CRT
    g.setGradientFill (ColourGradient (Colour (0xffe3e6ea), 0.0f, outer.getY(), Colour (0xff4b5056), 0.0f, outer.getBottom(), false));
    g.fillRoundedRectangle (outer, 7.0f);
    const auto r = outer.reduced (4.0f);
    g.setGradientFill (ColourGradient (Colour (0xff0d2214), r.getCentreX(), r.getCentreY(), Colour (0xff020604), r.getX(), r.getY(), true));
    g.fillRoundedRectangle (r, 5.0f);

    // Graticule
    g.setColour (theme::phosphor.withAlpha (0.13f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY(), r.getBottom());
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());

    const int shape = (int) proc.apvts.getRawParameterValue (ids::lfoShape)->load();
    const auto steps = Lfo::makeSteps ((int) proc.apvts.getRawParameterValue (ids::seqSeed)->load());
    const float h = r.getHeight() * 0.38f;
    const auto plot = r.reduced (4.0f, 0.0f);

    Path p;
    constexpr int points = 240;
    for (int i = 0; i <= points; ++i)
    {
        const float ph = (float) i / (float) points;
        const float px = plot.getX() + ph * plot.getWidth();
        const float py = r.getCentreY() - Lfo::evaluate (shape, jmin (ph, 0.9999f), steps) * h;
        if (i == 0) p.startNewSubPath (px, py);
        else        p.lineTo (px, py);
    }
    g.setColour (theme::phosphor.withAlpha (0.18f));
    g.strokePath (p, PathStrokeType (6.0f));
    g.setColour (theme::phosphor);
    g.strokePath (p, PathStrokeType (1.6f));

    const float dotX = plot.getX() + proc.uiLfoPhase.load() * plot.getWidth();
    const float dotY = r.getCentreY() - proc.uiLfoValue.load() * h;
    g.setColour (theme::phosphor.withAlpha (0.4f));
    g.fillEllipse (circle ({ dotX, dotY }, 7.0f));
    g.setColour (Colours::white);
    g.fillEllipse (circle ({ dotX, dotY }, 3.0f));

    // Scanlines + glass glare
    g.setColour (Colours::black.withAlpha (0.22f));
    for (float yy = r.getY(); yy < r.getBottom(); yy += 3.0f)
        g.drawHorizontalLine ((int) yy, r.getX(), r.getRight());
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.4f).reduced (3.0f, 2.0f), 4.0f);
}

//==============================================================================
GlitchMagmaEditor::GlitchMagmaEditor (GlitchMagmaProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      lfoPanel     (p.apvts, "MODULATION", theme::lfo),
      filterPanel  (p.apvts, "FILTER", theme::filter, ids::filterOn),
      masterPanel  (p.apvts, "MASTER", theme::master),
      distPanel    (p.apvts, "DISTORTION", theme::dist, ids::distOn),
      glitchPanel  (p.apvts, "GLITCH", theme::glitch, ids::glitchOn),
      chorusPanel  (p.apvts, "CHORUS", theme::chorus, ids::chorusOn),
      flangerPanel (p.apvts, "FLANGER", theme::flanger, ids::flangerOn),
      delayPanel   (p.apvts, "DELAY", theme::delay, ids::delayOn),
      reverbPanel  (p.apvts, "REVERB", theme::reverb, ids::reverbOn),
      inflatePanel (p.apvts, "INFLATOR", theme::inflate, ids::inflateOn),
      lfoDisplay (p)
{
    setLookAndFeel (&lnf);

    lfoPanel.choice (ids::lfoShape, "SHAPE").toggle (ids::lfoSync, "SYNC").choice (ids::lfoDiv, "DIVISION")
            .knob (ids::lfoRate, "RATE").knob (ids::lfoDepth, "LFO AMT").knob (ids::envDepth, "ENV AMT").knob (ids::seqSeed, "PATTERN");
    lfoPanel.setExtra (&lfoDisplay, 74);

    filterPanel.big (ids::cutoff, "CUTOFF").choice (ids::filterType, "TYPE")
               .knob (ids::reso, "RESO").knob (ids::filterDrive, "DRIVE").knob (ids::filterMix, "MIX");

    masterPanel.big (ids::mix, "DRY / WET").knob (ids::inGain, "INPUT").knob (ids::outGain, "OUTPUT").knob (ids::width, "WIDTH");

    distPanel.choice (ids::distType, "TYPE").knob (ids::distDrive, "DRIVE").knob (ids::distMix, "MIX");
    glitchPanel.choice (ids::glitchMode, "MODE").choice (ids::glitchDiv, "STEP").choice (ids::glitchRepeat, "REPEATS")
               .knob (ids::glitchProb, "CHANCE").knob (ids::glitchMix, "MIX");
    chorusPanel.knob (ids::chorusRate, "RATE").knob (ids::chorusDepth, "DEPTH").knob (ids::chorusMix, "MIX");
    flangerPanel.knob (ids::flangerRate, "RATE").knob (ids::flangerDepth, "DEPTH").knob (ids::flangerFeedback, "FEEDBACK").knob (ids::flangerMix, "MIX");
    delayPanel.toggle (ids::delaySync, "SYNC").choice (ids::delayDiv, "DIVISION").knob (ids::delayTime, "TIME")
              .knob (ids::delayFeedback, "FEEDBACK").toggle (ids::delayPingPong, "PING PONG").knob (ids::delayMix, "MIX");
    reverbPanel.choice (ids::reverbType, "TYPE").knob (ids::reverbSize, "SIZE").knob (ids::reverbDecay, "DECAY")
               .knob (ids::reverbPreDelay, "PRE-DELAY").knob (ids::reverbDamp, "DAMP").knob (ids::reverbMix, "MIX");
    inflatePanel.knob (ids::inflateAmount, "EFFECT").knob (ids::inflateCurve, "CURVE");

    for (auto* panel : { &lfoPanel, &filterPanel, &masterPanel, &distPanel, &glitchPanel, &chorusPanel,
                         &flangerPanel, &delayPanel, &reverbPanel, &inflatePanel })
        addAndMakeVisible (*panel);

    // Preset browser: Init + 16 banks x 50
    presetBox.addItem (presets::nameFor (0), 1);
    for (int bank = 0; bank < presets::numBanks; ++bank)
    {
        presetBox.addSectionHeading (presets::bankNames()[bank]);
        for (int i = 0; i < presets::perBank; ++i)
        {
            const int program = 1 + bank * presets::perBank + i;
            presetBox.addItem (presets::nameFor (program), program + 1);
        }
    }
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0)
            proc.setCurrentProgram (id - 1);
    };
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    randomButton.setColour (TextButton::buttonColourId, Colour (0xffc0281e));
    for (auto* b : { &prevButton, &nextButton })
        b->setColour (TextButton::buttonColourId, Colour (0xff6a7078));
    randomButton.onClick = [this] { proc.randomize(); refreshPresetBox(); };

    // ON/OFF: lit means the plugin is on. The parameter underneath is the host's bypass, so it's inverted here.
    powerButton.setColour (ToggleButton::tickColourId, Colour (0xff4cff7a));
    powerButton.setTooltip ("Switch The FilterFish on or off (the same as your DAW's bypass)");
    powerAttachment = std::make_unique<ParameterAttachment> (*p.apvts.getParameter (ids::bypass),
        [this] (float bypassed) { powerButton.setToggleState (bypassed < 0.5f, dontSendNotification); });
    powerButton.onClick = [this] { powerAttachment->setValueAsCompleteGesture (powerButton.getToggleState() ? 0.0f : 1.0f); };
    powerAttachment->sendInitialUpdate();
    addAndMakeVisible (powerButton);
    for (auto* c : std::initializer_list<Component*> { &presetBox, &prevButton, &nextButton, &randomButton })
        addAndMakeVisible (c);

    setResizable (true, true);
    setResizeLimits (1100, 688, 1920, 1200);
    getConstrainer()->setFixedAspectRatio (1.6);
    setSize (1280, 800);
    refreshPresetBox();
    startTimerHz (30);
}

GlitchMagmaEditor::~GlitchMagmaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GlitchMagmaEditor::stepPreset (int delta)
{
    const int count = presets::numPrograms;
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + count) % count);
    refreshPresetBox();
}

void GlitchMagmaEditor::refreshPresetBox()
{
    lastPresetVersion = proc.presetVersion.load();
    if (proc.isRandomPreset())
        presetBox.setText (proc.getRandomName(), dontSendNotification);
    else
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
}

void GlitchMagmaEditor::timerCallback()
{
    if (auto* cutoffKnob = filterPanel.getBig())
    {
        const float norm = proc.apvts.getParameter (ids::cutoff)->convertTo0to1 (proc.uiModCutoff.load());
        cutoffKnob->slider.getProperties().set ("mod", norm);
        cutoffKnob->slider.repaint();
    }
    lfoDisplay.repaint();

    // Grey out what the current settings ignore
    auto value = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const bool delaySynced = value (ids::delaySync) > 0.5f;
    delayPanel.setDimmed (ids::delayTime, delaySynced);
    delayPanel.setDimmed (ids::delayDiv, ! delaySynced);
    const bool classic = value (ids::reverbType) < 0.5f;
    reverbPanel.setDimmed (ids::reverbDecay, classic);
    reverbPanel.setDimmed (ids::reverbPreDelay, classic);

    if (proc.presetVersion.load() != lastPresetVersion)
        refreshPresetBox();
}

void GlitchMagmaEditor::paint (Graphics& g)
{
    // Dark gunmetal hull with panel seams and rivets
    g.drawImageAt (hullTexture, 0, 0);
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRect (getLocalBounds(), 2);
    for (float x = 14.0f; x < w - 10.0f; x += 40.0f)
    {
        drawScrew (g, { x, 5.0f }, 2.2f, x * 0.1f);
        drawScrew (g, { x, h - 5.0f }, 2.2f, x * 0.07f);
    }

    // Title plate, then the SKAZI logo on its own black plate
    auto t = titleArea.toFloat().reduced (0.0f, 6.0f);
    drawRecess (g, t, 6.0f, Colour (0xff0a0b0d));
    const auto inner = t.reduced (14.0f, 4.0f);
    float lx = t.getRight() + 18.0f;
#if SKAZI_HAS_LOGO
    // Not a static: one still alive at DLL unload frees Direct2D resources under the loader lock and
    // deadlocks the DAW on quit. ImageCache hands back the same decoded image every paint.
    const auto logo = ImageCache::getFromMemory (BinaryData::skazi_logo_png, BinaryData::skazi_logo_pngSize);
    const Rectangle<float> plate (t.getRight() + 10.0f, t.getY(), t.getHeight() * 1.8f, t.getHeight());
    drawRecess (g, plate, 6.0f, Colours::black);
    g.drawImage (logo, plate.reduced (4.0f, 2.0f), RectanglePlacement::centred);
    lx = plate.getRight() + 18.0f;
#endif
    g.setFont (stencil (jmin (28.0f, inner.getHeight() * 0.62f)));
    drawGlowText (g, "THE FILTERFISH", inner.withTrimmedBottom (inner.getHeight() * 0.3f), theme::lcdText, Justification::centredLeft);
    g.setFont (mono (11.0f, true));
    drawGlowText (g, "BY ASHER SWISSA", inner.withTrimmedTop (inner.getHeight() * 0.68f), theme::phosphor, Justification::centredLeft);

    // Row of status lamps next to the title
    const float lampY = t.getCentreY();
    for (auto colour : { theme::dist, theme::inflate, theme::delay, theme::reverb })
    {
        drawLamp (g, { lx, lampY }, 4.0f, colour, true);
        lx += 18.0f;
    }
}

void GlitchMagmaEditor::resized()
{
    hullTexture = makeBrushedMetal (getWidth(), getHeight(), theme::hull, 7);

    auto r = getLocalBounds().reduced (12);
    constexpr int gap = 8;

    auto header = r.removeFromTop (roundToInt ((float) r.getHeight() * 0.08f));
    titleArea = header.removeFromLeft (header.getWidth() / 4);
    header.removeFromLeft (90 + roundToInt ((float) (header.getHeight() - 12) * 1.8f) + 10); // logo plate + lamps
    randomButton.setBounds (header.removeFromRight (120).reduced (4, 6));
    nextButton.setBounds (header.removeFromRight (46).reduced (2, 6));
    presetBox.setBounds (header.removeFromRight (jmin (460, header.getWidth() - 50)).reduced (2, 9));
    prevButton.setBounds (header.removeFromRight (46).reduced (2, 6));
    header.removeFromRight (10);
    powerButton.setBounds (header.removeFromRight (64).reduced (2, 9));
    r.removeFromTop (gap);

    auto top = r.removeFromTop (roundToInt ((float) r.getHeight() * 0.47f));
    const int topW = top.getWidth();
    lfoPanel.setBounds (top.removeFromLeft (roundToInt ((float) topW * 0.30f)));
    top.removeFromLeft (gap);
    filterPanel.setBounds (top.removeFromLeft (roundToInt ((float) topW * 0.42f)));
    top.removeFromLeft (gap);
    masterPanel.setBounds (top);
    r.removeFromTop (gap);

    auto layoutRow = [] (Rectangle<int> row, std::initializer_list<std::pair<Component*, float>> parts)
    {
        const int total = row.getWidth() - gap * ((int) parts.size() - 1);
        int n = 0;
        for (const auto& [comp, frac] : parts)
        {
            if (++n == (int) parts.size())
            {
                comp->setBounds (row);
            }
            else
            {
                comp->setBounds (row.removeFromLeft (roundToInt ((float) total * frac)));
                row.removeFromLeft (gap);
            }
        }
    };

    auto row1 = r.removeFromTop ((r.getHeight() - gap) / 2);
    r.removeFromTop (gap);
    layoutRow (row1, { { &distPanel, 0.21f }, { &glitchPanel, 0.37f }, { &chorusPanel, 0.19f }, { &flangerPanel, 0.23f } });
    layoutRow (r, { { &delayPanel, 0.43f }, { &reverbPanel, 0.39f }, { &inflatePanel, 0.18f } });
}
