#pragma once

// Acid hazard look: a safety-yellow warning-sign panel with heavy black type and
// hazard stripes, over a neon purple sequencer deck sprinkled with acid smileys.

#include <juce_gui_basics/juce_gui_basics.h>

namespace skin
{
const juce::Colour panel       { 0xffffc400 };   // top panel: hazard-sign safety yellow
const juce::Colour panelTop    { 0xffffd43a };
const juce::Colour panelLine   { 0xff0b0b0b };
const juce::Colour cream       { 0xff0b0b0b };   // legends on the yellow panel: sign black
const juce::Colour creamSoft   { 0xff3a3000 };
const juce::Colour deckTop     { 0xff8a2cff };   // sequencer deck, bright purple
const juce::Colour deckBot     { 0xff5b12c4 };
const juce::Colour deckLabel   { 0xfffff4fd };
const juce::Colour ink         { 0xff1c0b2b };   // text on light buttons / keys
const juce::Colour inkSoft     { 0xff4a2d66 };
const juce::Colour deckRaised  { 0xff1d0b30 };
const juce::Colour deckText    { 0xffe9d8ff };
const juce::Colour pink        { 0xffff2fb9 };
const juce::Colour hotPink     { 0xffff6fe0 };
const juce::Colour acidYellow  { 0xfff5ff2a };
const juce::Colour slideCyan   { 0xff22e3ff };
const juce::Colour lcd         { 0xff10041c };

// Roles used by the step grid and LEDs.
const juce::Colour amber        = hotPink;    // LCD text
const juce::Colour acid         = pink;       // normal notes / active states
const juce::Colour ledRed       = pink;       // gate + playhead LEDs
const juce::Colour accentOrange = acidYellow; // accents

inline juce::Font heavyFont (float h)
{
    return juce::Font (juce::FontOptions ("Arial Black", h, juce::Font::plain));
}

inline juce::Font labelFont (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions ("Arial Narrow", h, bold ? juce::Font::bold : juce::Font::plain))
        .withExtraKerningFactor (0.06f);
}

/** The classic acid-house smiley. */
inline void drawSmiley (juce::Graphics& g, juce::Rectangle<float> r, float alpha = 1.0f, bool outline = true)
{
    const auto c = r.getCentre();
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (acidYellow.withMultipliedAlpha (alpha));
    g.fillEllipse (r);
    if (outline)
    {
        g.setColour (juce::Colours::black.withAlpha (alpha));
        g.drawEllipse (r.reduced (rad * 0.03f), rad * 0.07f);
    }
    g.setColour (juce::Colours::black.withAlpha (alpha));
    const float ew = rad * 0.16f, eh = rad * 0.34f;
    g.fillEllipse (c.x - rad * 0.33f - ew * 0.5f, c.y - rad * 0.45f, ew, eh);
    g.fillEllipse (c.x + rad * 0.33f - ew * 0.5f, c.y - rad * 0.45f, ew, eh);
    juce::Path smile;
    smile.addCentredArc (c.x, c.y + rad * 0.02f, rad * 0.58f, rad * 0.55f, 0.0f,
                         juce::MathConstants<float>::pi * 0.62f, juce::MathConstants<float>::pi * 1.38f, true);
    g.strokePath (smile, juce::PathStrokeType (rad * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** Black/yellow diagonal hazard stripes. */
inline void fillHazard (juce::Graphics& g, juce::Rectangle<float> r, float stripe = 18.0f)
{
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (panel);
    g.fillRect (r);
    g.setColour (juce::Colour (0xff0b0b0b));
    for (float x = r.getX() - r.getHeight() - stripe * 2.0f; x < r.getRight() + r.getHeight(); x += stripe * 2.0f)
    {
        juce::Path p;
        p.addQuadrilateral (x, r.getBottom(), x + stripe, r.getBottom(),
                            x + stripe + r.getHeight(), r.getY(), x + r.getHeight(), r.getY());
        g.fillPath (p);
    }
}

/** Side panel: hazard stripes with a black inner edge. */
inline juce::Image makeSidePanel (int w, int h, bool leftSide)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    // Stripes run diagonally down the cheek: draw them rotated.
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi).translated ((float) w, 0.0f));
        fillHazard (g, { 0.0f, 0.0f, (float) h, (float) w }, 16.0f);
    }
    g.setColour (juce::Colour (0xff0b0b0b));
    g.fillRect (leftSide ? (float) w - 4.0f : 0.0f, 0.0f, 4.0f, (float) h);
    return img;
}

/** SKAZI mark: a rounded hazard triangle holding a melting acid smiley. */
inline void drawAcidMark (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float w = r.getWidth(), h = r.getHeight();
    juce::Path tri;
    tri.addTriangle (r.getCentreX(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getBottom());
    tri = tri.createPathWithRoundedCorners (w * 0.08f);
    g.setColour (juce::Colour (0xff0b0b0b));
    g.fillPath (tri);
    g.setColour (pink);
    g.strokePath (tri, juce::PathStrokeType (w * 0.045f));

    // Smiley sitting low in the triangle, with drips running off its chin.
    const float d = w * 0.46f;
    const auto face = juce::Rectangle<float> (r.getCentreX() - d * 0.5f, r.getBottom() - h * 0.12f - d, d, d);
    g.setColour (acidYellow);
    for (auto [dx, len] : { std::pair<float, float> { -0.22f, 0.16f }, { 0.05f, 0.24f }, { 0.25f, 0.12f } })
    {
        const float x = face.getCentreX() + dx * d;
        g.fillRoundedRectangle (x - d * 0.05f, face.getCentreY(), d * 0.1f, d * 0.5f + len * d, d * 0.05f);
        g.fillEllipse (x - d * 0.07f, face.getCentreY() + d * 0.42f + len * d, d * 0.14f, d * 0.14f);
    }
    drawSmiley (g, face, 1.0f, false);
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, lcd);
        setColour (juce::ComboBox::textColourId, hotPink);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::black);
        setColour (juce::ComboBox::arrowColourId, hotPink.withAlpha (0.7f));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff160826));
        // Menus sit on a dark background, so their text must stay light (the panel ink is black).
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffffe3f7));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, pink.withAlpha (0.35f));
        setColour (juce::PopupMenu::highlightedTextColourId, acidYellow);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff160826));
        setColour (juce::TooltipWindow::textColourId, juce::Colour (0xffffe3f7));
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xff160826));
        setColour (juce::BubbleComponent::outlineColourId, pink.withAlpha (0.6f));
        setColour (juce::Slider::textBoxTextColourId, hotPink);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float size = juce::jmin (area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float radius = size * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool onDeck = s.getProperties()["deck"];
        const bool bipolar = s.getProperties()["bipolar"];

        // Neon value ring
        const float ringR = radius - 3.0f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colours::black.withAlpha (onDeck ? 0.35f : 0.55f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        juce::Path val;
        val.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        const auto ringCol = onDeck ? acidYellow : juce::Colour (0xffe8008f);
        g.setColour (ringCol.withAlpha (0.3f));
        g.strokePath (val, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (ringCol);
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Small printed scale just inside the ring
        const float tickOuter = ringR - 5.0f;
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const auto p1 = c.getPointOnCircumference (tickOuter - (i % 5 == 0 ? 4.0f : 2.5f), a);
            const auto p2 = c.getPointOnCircumference (tickOuter, a);
            g.setColour (onDeck ? deckLabel.withAlpha (0.6f) : cream.withAlpha (0.75f));
            g.drawLine (p1.x, p1.y, p2.x, p2.y, 1.0f);
        }

        const float skirtR = tickOuter - 6.0f;
        const float capR = skirtR * 0.66f;

        g.setColour (juce::Colour (0x88000000));
        g.fillEllipse (c.x - skirtR + 1.5f, c.y - skirtR + 3.0f, skirtR * 2.0f, skirtR * 2.0f);

        // Black knurled skirt
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a2a48), c.x, c.y - skirtR,
                                                 juce::Colour (0xff07040a), c.x, c.y + skirtR, false));
        g.fillEllipse (c.x - skirtR, c.y - skirtR, skirtR * 2.0f, skirtR * 2.0f);
        g.setColour (juce::Colour (0xff5d4a6e));
        for (int i = 0; i < 32; ++i)
        {
            const float a = angle + juce::MathConstants<float>::twoPi * (float) i / 32.0f;
            const auto p1 = c.getPointOnCircumference (skirtR * 0.82f, a);
            const auto p2 = c.getPointOnCircumference (skirtR * 0.98f, a);
            g.drawLine (p1.x, p1.y, p2.x, p2.y, 0.9f);
        }

        // Glossy candy cap: pink on the violet panel, yellow on the purple deck
        const auto capLight = onDeck ? juce::Colour (0xfffbff8a) : juce::Colour (0xfff7f5ee);
        const auto capDark  = onDeck ? juce::Colour (0xffc2c800) : juce::Colour (0xffa29f96);
        g.setGradientFill (juce::ColourGradient (capLight, c.x - capR * 0.5f, c.y - capR,
                                                 capDark, c.x + capR * 0.4f, c.y + capR, false));
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        // Spin marks on the metal cap.
        for (float rr = capR * 0.3f; rr < capR; rr += 2.2f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.06f));
            g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 0.7f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillEllipse (c.x - capR * 0.55f, c.y - capR * 0.8f, capR * 0.8f, capR * 0.45f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

        // Pointer
        const auto tip = c.getPointOnCircumference (skirtR * 0.95f, angle);
        const auto tail = c.getPointOnCircumference (capR * 0.2f, angle);
        g.setColour (juce::Colour (0xff0b0410));
        g.drawLine (tail.x, tail.y, tip.x, tip.y, juce::jmax (2.2f, size * 0.035f));
        const auto dot = c.getPointOnCircumference (skirtR * 0.9f, angle);
        g.setColour (juce::Colours::white);
        g.fillEllipse (dot.x - 1.8f, dot.y - 1.8f, 3.6f, 3.6f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (lcd);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.hasKeyboardFocus (false) ? hotPink : pink.withAlpha (0.45f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        juce::Path arrow;
        const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
        g.setColour (hotPink.withAlpha (0.8f));
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions ("Consolas", 13.0f, juce::Font::bold)); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 0, box.getWidth() - 18, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions ("Consolas", 14.0f, juce::Font::plain)); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        // RANDOM / GENERATE: hot pink. A hot toggle (TURBO) only glows while it is on.
        const bool hot = b.getProperties()["hot"] && (! b.getClickingTogglesState() || b.getToggleState());
        auto top = hot ? juce::Colour (0xffff5cd0) : juce::Colour (0xfff6effa);
        auto bot = hot ? juce::Colour (0xffc4007f) : juce::Colour (0xffcdbfe0);
        if (over) { top = top.brighter (0.15f); bot = bot.brighter (0.1f); }
        if (down) std::swap (top, bot);

        g.setColour (juce::Colour (0x77000000));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bot, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colour (0xff1c0b2b));
        g.drawRoundedRectangle (r, 5.0f, 1.2f);

        if (b.getClickingTogglesState())
        {
            const bool on = b.getToggleState();
            const auto led = juce::Rectangle<float> (r.getX() + 10.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
            if (on)
            {
                g.setColour (pink.withAlpha (0.45f));
                g.fillEllipse (led.expanded (4.0f));
            }
            g.setColour (on ? (hot ? juce::Colours::white : pink) : juce::Colour (0xff4a1a3c));
            g.fillEllipse (led);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (led, 0.8f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds();
        if (b.getClickingTogglesState())
            r.removeFromLeft (22);
        g.setFont (labelFont (13.5f));
        const bool hotOn = b.getProperties()["hot"] && b.getClickingTogglesState() && b.getToggleState();
        g.setColour (hotOn ? juce::Colours::white : b.findColour (juce::TextButton::textColourOffId));
        g.drawFittedText (b.getButtonText(), r, juce::Justification::centred, 1);
    }
};
} // namespace skin
