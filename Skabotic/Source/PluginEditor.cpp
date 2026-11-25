#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::Font mono (float size, bool bold = true)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
    }

    void glowDot (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float intensity)
    {
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.55f * intensity), c, col.withAlpha (0.0f), c.translated (r * 3.5f, 0), true));
        g.fillEllipse (juce::Rectangle<float> (r * 7.0f, r * 7.0f).withCentre (c));
        g.setColour (col.brighter (0.6f).withAlpha (juce::jlimit (0.0f, 1.0f, intensity)));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    }

    void rivet (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (0.8f, 1.2f)));
        g.setGradientFill (juce::ColourGradient (Skin::steelHi, c.translated (-r * 0.5f, -r * 0.6f), Skin::steelLo, c.translated (r, r), true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    }

    // Phillips screw head, for panel corners.
    void screw (juce::Graphics& g, juce::Point<float> c, float r)
    {
        rivet (g, c, r);
        g.setColour (Skin::steelLo.darker (0.5f));
        g.drawLine (c.x - r * 0.55f, c.y, c.x + r * 0.55f, c.y, 1.4f);
        g.drawLine (c.x, c.y - r * 0.55f, c.x, c.y + r * 0.55f, 1.4f);
    }

    // Fine horizontal grain over a metal surface.
    void brushed (juce::Graphics& g, juce::Rectangle<float> r, float alpha)
    {
        for (float y = r.getY(); y < r.getBottom(); y += 2.0f)
        {
            const float v = 0.5f + 0.5f * std::sin (y * 12.9898f) * std::cos (y * 4.1414f);
            g.setColour ((v > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (alpha * std::abs (v - 0.5f) * 2.0f));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        }
    }

    // Dark armour plate with screws, hazard stripe and a stamped title.
    void plate (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r.translated (0, 4), 14.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a3139), r.getTopLeft(), juce::Colour (0xff12161b), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 14.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (r.toNearestInt());
            brushed (g, r, 0.05f);
        }
        g.setColour (Skin::steel.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawHorizontalLine ((int) r.getY() + 2, r.getX() + 14, r.getRight() - 14);

        auto head = r.removeFromTop (38.0f).reduced (16.0f, 8.0f);
        if (title.isEmpty())
            return;
        // hazard stripes on the right of the title strip
        {
            juce::Graphics::ScopedSaveState s (g);
            auto stripe = head.removeFromRight (70.0f);
            g.reduceClipRegion (stripe.toNearestInt());
            g.setColour (accent.withAlpha (0.8f));
            for (float x = stripe.getX() - 30; x < stripe.getRight(); x += 12)
            {
                juce::Path p;
                p.addQuadrilateral (x, stripe.getBottom(), x + 6, stripe.getBottom(), x + 6 + stripe.getHeight(), stripe.getY(), x + stripe.getHeight(), stripe.getY());
                g.fillPath (p);
            }
        }
        g.setColour (accent);
        g.fillRect (head.removeFromLeft (4.0f));
        g.setFont (mono (15.0f));
        g.setColour (Skin::text);
        g.drawText (title, head.withTrimmedLeft (10.0f), juce::Justification::centredLeft);
    }
}

//==============================================================================
RobotLookAndFeel::RobotLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Skin::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, Skin::lcd);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff101418));
    setColour (juce::PopupMenu::textColourId, Skin::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Skin::cyan.withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::textColourOffId, Skin::text);
    setColour (juce::TextButton::textColourOnId, Skin::cyan);
    setColour (juce::Label::textColourId, Skin::text);
    setColour (juce::TextEditor::textColourId, Skin::text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101418));
}

void RobotLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);

    // LED ring
    const int leds = 21;
    for (int i = 0; i < leds; ++i)
    {
        const float t = (float) i / (float) (leds - 1);
        const float a = start + t * (end - start);
        const auto p = c.getPointOnCircumference (r * 0.93f, a);
        if (t <= pos + 0.001f)
            glowDot (g, p, r * 0.055f, accent, 0.9f);
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillEllipse (juce::Rectangle<float> (r * 0.11f, r * 0.11f).withCentre (p));
        }
    }

    // chrome body with knurled rim
    const float br = r * 0.7f;
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (juce::Rectangle<float> (br * 2.1f, br * 2.1f).withCentre (c.translated (0, br * 0.12f)));
    g.setGradientFill (juce::ColourGradient (Skin::steelHi, c.translated (-br * 0.5f, -br * 0.7f), Skin::steelLo, c.translated (br * 0.6f, br), true));
    g.fillEllipse (juce::Rectangle<float> (br * 2.0f, br * 2.0f).withCentre (c));
    g.setColour (Skin::steelLo.withAlpha (0.8f));
    for (int i = 0; i < 40; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 40.0f;
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (br * 0.86f, a), c.getPointOnCircumference (br, a)), 1.0f);
    }
    const float cr = br * 0.72f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b444e), c.translated (0, -cr), juce::Colour (0xff15191e), c.translated (0, cr), false));
    g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c), 1.0f);

    juce::Path pointer;
    pointer.startNewSubPath (c.getPointOnCircumference (cr * 0.25f, angle));
    pointer.lineTo (c.getPointOnCircumference (cr * 0.95f, angle));
    g.setColour (accent.withAlpha (0.35f));
    g.strokePath (pointer, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.brighter (0.5f));
    g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void RobotLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (juce::Colour (0xff050807));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Skin::steel.withAlpha (0.8f));
    g.drawRoundedRectangle (r, 6.0f, 1.5f);
    g.setColour (Skin::lcd.withAlpha (0.05f));
    for (float y = r.getY() + 2; y < r.getBottom(); y += 3)
        g.drawHorizontalLine ((int) y, r.getX() + 2, r.getRight() - 2);

    juce::Path arrow;
    const float ax = r.getRight() - 16.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
    g.setColour (Skin::lcd);
    g.fillPath (arrow);
}

juce::Font RobotLookAndFeel::getComboBoxFont (juce::ComboBox&) { return mono (14.0f); }
juce::Font RobotLookAndFeel::getPopupMenuFont() { return mono (14.0f); }

juce::Label* RobotLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (mono (13.0f));
    l->setColour (juce::Label::textColourId, Skin::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

void RobotLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (down ? Skin::steelLo : Skin::steel, r.getTopLeft(), down ? Skin::steel : Skin::steelLo, r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (over ? Skin::cyan : Skin::steelHi.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 6.0f, 1.5f);
}

//==============================================================================
RobotKnob::RobotKnob (const juce::String& n, juce::Colour a) : name (n), accent (a)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 20);
    slider.setColour (juce::Slider::rotarySliderFillColourId, a);
    slider.setVelocityBasedMode (false);
    addAndMakeVisible (slider);
}

void RobotKnob::paint (juce::Graphics& g)
{
    g.setFont (mono (12.5f));
    g.setColour (accent.interpolatedWith (Skin::text, 0.4f));
    g.drawText (name, getLocalBounds().removeFromTop (18), juce::Justification::centred);
}

void RobotKnob::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (18)); }

//==============================================================================
void RobotFace::tick()
{
    const float a = 0.5f, rel = 0.12f;
    float maxBand = 0.0f;
    for (size_t i = 0; i < bands.size(); ++i)
    {
        const float env = proc.bandLevel[i].load();
        const float v = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (env, -100.0f) + 62.0f) / 50.0f);
        bands[i] += (v > bands[i] ? a : rel) * (v - bands[i]);
        maxBand = juce::jmax (maxBand, bands[i]);
    }
    const float out = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (proc.outLevel.load(), -100.0f) + 48.0f) / 48.0f);
    level += (out > level ? 0.6f : 0.15f) * (out - level);
    blink = proc.gateOpen.load() ? juce::jmin (1.0f, blink + 0.25f) : juce::jmax (0.0f, blink - 0.08f);
    time += 1.0f / 30.0f;
    repaint();
}

void RobotFace::paint (juce::Graphics& g)
{
    using juce::Rectangle;
    const float W = (float) getWidth();
    const float cx = W * 0.5f;
    g.fillAll (Skin::bg);

    const auto& vts = proc.apvts;
    const float hot = (vts.getRawParameterValue (ParamIDs::drive)->load() + vts.getRawParameterValue (ParamIDs::ring)->load()) * 0.01f;
    const auto eyeCol = hot > 0.55f ? Skin::red : Skin::cyan;

    // halo behind the head
    g.setGradientFill (juce::ColourGradient (eyeCol.withAlpha (0.10f + 0.18f * level), { cx, 260.0f }, eyeCol.withAlpha (0.0f), { cx + 260.0f, 260.0f }, true));
    g.fillRect (getLocalBounds());

    // antenna
    g.setColour (Skin::steel);
    g.fillRect (Rectangle<float> (cx - 3.0f, 34.0f, 6.0f, 56.0f));
    g.setColour (Skin::steelHi.withAlpha (0.6f));
    g.fillRect (Rectangle<float> (cx - 3.0f, 34.0f, 2.0f, 56.0f));
    const float pulse = 0.25f + 0.75f * juce::jmax (blink * (0.5f + 0.5f * std::sin (time * 9.0f)), level);
    glowDot (g, { cx, 26.0f }, 11.0f, Skin::red, pulse);
    g.setGradientFill (juce::ColourGradient (Skin::steelHi, { cx - 20, 78 }, Skin::steelLo, { cx + 20, 96 }, false));
    g.fillEllipse (cx - 26.0f, 76.0f, 52.0f, 26.0f);

    // ears
    for (int side = 0; side < 2; ++side)
    {
        const float ex = side == 0 ? 18.0f : W - 18.0f - 34.0f;
        auto ear = Rectangle<float> (ex, 190.0f, 34.0f, 120.0f);
        g.setGradientFill (juce::ColourGradient (Skin::steelHi, ear.getTopLeft(), Skin::steelLo, ear.getTopRight(), false));
        g.fillRoundedRectangle (ear, 10.0f);
        g.setColour (Skin::steelLo);
        for (int i = 1; i < 6; ++i)
            g.drawHorizontalLine ((int) (ear.getY() + (float) i * 20.0f), ear.getX() + 5, ear.getRight() - 5);
        glowDot (g, ear.getCentre(), 3.5f, eyeCol, 0.3f + 0.7f * level);
    }

    // head shell
    const auto head = Rectangle<float> (48.0f, 92.0f, W - 96.0f, 372.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (head.translated (0, 8), 58.0f);
    g.setGradientFill (juce::ColourGradient (Skin::steelHi, head.getTopLeft(), Skin::steelLo, head.getBottomRight(), false));
    g.fillRoundedRectangle (head, 58.0f);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (head, 58.0f);
        g.reduceClipRegion (clip);
        brushed (g, head, 0.09f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), head.getTopLeft(), juce::Colours::transparentWhite, head.getTopLeft().translated (0, 120), false));
        g.fillRect (head.withHeight (120.0f));
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (head, 58.0f, 2.0f);
    for (int i = 0; i < 7; ++i)
        rivet (g, { head.getX() + 70.0f + (float) i * (head.getWidth() - 140.0f) / 6.0f, head.getY() + 16.0f }, 4.0f);

    // forehead LCD
    auto lcdBox = Rectangle<float> (cx - 110.0f, 118.0f, 220.0f, 44.0f);
    g.setColour (juce::Colour (0xff030604));
    g.fillRoundedRectangle (lcdBox, 6.0f);
    g.setColour (Skin::steelLo);
    g.drawRoundedRectangle (lcdBox, 6.0f, 2.0f);
    {
        const float hz = proc.voiceHz.load(), carHz = proc.carrierHz.load();
        auto noteOf = [] (float f) { return f > 0 ? juce::MidiMessage::getMidiNoteName ((int) std::lround (12.0f * std::log2 (f / 440.0f) + 69.0f), true, true, 3) : juce::String ("--"); };
        const auto* carrier = dynamic_cast<juce::AudioParameterChoice*> (vts.getParameter (ParamIDs::carrier));
        g.setFont (mono (12.5f));
        g.setColour (Skin::lcd);
        g.drawText ("VOICE  " + noteOf (hz).paddedRight (' ', 4) + (hz > 0 ? juce::String (juce::roundToInt (hz)) + "Hz" : juce::String ("---")),
                    lcdBox.withTrimmedLeft (12).removeFromTop (24).translated (0, 3), juce::Justification::centredLeft);
        g.setColour (Skin::lcd.withAlpha (0.75f));
        g.drawText ("ROBOT  " + noteOf (carHz).paddedRight (' ', 4) + (carrier != nullptr ? carrier->getCurrentChoiceName() : juce::String()),
                    lcdBox.withTrimmedLeft (12).removeFromBottom (22).translated (0, -3), juce::Justification::centredLeft);
        g.setColour (Skin::lcd.withAlpha (0.06f));
        for (float y = lcdBox.getY() + 2; y < lcdBox.getBottom(); y += 3)
            g.drawHorizontalLine ((int) y, lcdBox.getX() + 2, lcdBox.getRight() - 2);
    }

    // face plate
    const auto face = Rectangle<float> (80.0f, 176.0f, W - 160.0f, 262.0f);
    g.setColour (juce::Colour (0xff171b20));
    g.fillRoundedRectangle (face, 30.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (face.reduced (1), 30.0f, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (face.translated (0, 2), 30.0f, 1.0f);
    for (auto p : { face.getTopLeft().translated (16, 16), face.getTopRight().translated (-16, 16),
                    face.getBottomLeft().translated (16, -16), face.getBottomRight().translated (-16, -16) })
        screw (g, p, 5.0f);

    // eyes
    const float eyeR = 40.0f;
    for (int side = 0; side < 2; ++side)
    {
        const juce::Point<float> c { cx + (side == 0 ? -64.0f : 64.0f), 244.0f };
        const auto outer = Rectangle<float> (eyeR * 2.0f + 14.0f, eyeR * 2.0f + 14.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (Skin::steelHi, outer.getTopLeft(), Skin::steelLo, outer.getBottomRight(), false));
        g.fillEllipse (outer);
        g.setColour (juce::Colour (0xff020304));
        g.fillEllipse (Rectangle<float> (eyeR * 2.0f, eyeR * 2.0f).withCentre (c));

        juce::Graphics::ScopedSaveState s (g);
        juce::Path socket;
        socket.addEllipse (Rectangle<float> (eyeR * 2.0f, eyeR * 2.0f).withCentre (c));
        g.reduceClipRegion (socket);

        const float lit = 0.3f + 0.7f * level;
        g.setGradientFill (juce::ColourGradient (eyeCol.withAlpha (0.9f * lit), c, eyeCol.withAlpha (0.0f), c.translated (eyeR, 0), true));
        g.fillEllipse (Rectangle<float> (eyeR * 2.0f, eyeR * 2.0f).withCentre (c));
        for (int ring = 0; ring < 3; ++ring)
        {
            const float rr = eyeR * (0.35f + 0.2f * (float) ring);
            g.setColour (eyeCol.brighter (0.4f).withAlpha (lit * (0.9f - 0.25f * (float) ring)));
            g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 2.0f);
        }
        glowDot (g, c, eyeR * 0.16f, juce::Colours::white.interpolatedWith (eyeCol, 0.4f), lit);

        // scanning bar
        const float scanY = c.y - eyeR + std::fmod (time * 50.0f, eyeR * 2.0f);
        g.setColour (eyeCol.brighter (0.8f).withAlpha (0.25f + 0.4f * level));
        g.fillRect (Rectangle<float> (c.x - eyeR, scanY, eyeR * 2.0f, 2.0f));

        // eyelid closes while the gate is shut: the robot is asleep
        const float lid = (1.0f - blink) * 0.55f;
        if (lid > 0.01f)
        {
            g.setGradientFill (juce::ColourGradient (Skin::steel, { c.x, c.y - eyeR }, Skin::steelLo, { c.x, c.y - eyeR + eyeR * 2.0f * lid }, false));
            g.fillRect (Rectangle<float> (c.x - eyeR, c.y - eyeR, eyeR * 2.0f, eyeR * 2.0f * lid));
            g.setColour (juce::Colours::black);
            g.fillRect (Rectangle<float> (c.x - eyeR, c.y - eyeR + eyeR * 2.0f * lid - 1.5f, eyeR * 2.0f, 3.0f));
        }
    }

    // nose vents
    for (int i = -1; i <= 1; ++i)
    {
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (cx + (float) i * 12.0f - 3.0f, 300.0f, 6.0f, 22.0f, 3.0f);
    }

    // mouth: LED grille driven by the vocoder bands
    const int cols = bot::Vocoder::numBands, rows = 7;
    auto mouth = Rectangle<float> (cx - 118.0f, 336.0f, 236.0f, 82.0f);
    g.setColour (juce::Colour (0xff050607));
    g.fillRoundedRectangle (mouth.expanded (6.0f), 12.0f);
    g.setColour (Skin::steel);
    g.drawRoundedRectangle (mouth.expanded (6.0f), 12.0f, 2.0f);
    const float cw = mouth.getWidth() / (float) cols, ch = mouth.getHeight() / (float) rows;
    for (int col = 0; col < cols; ++col)
    {
        const float v = bands[(size_t) col] * (float) rows;
        for (int row = 0; row < rows; ++row)
        {
            // light from the middle row outward so the mouth "opens"
            const float dist = std::abs ((float) row - (rows - 1) * 0.5f) * 2.0f + 1.0f;
            const bool on = v >= dist - 0.5f;
            auto cell = Rectangle<float> (mouth.getX() + (float) col * cw, mouth.getY() + (float) row * ch, cw, ch).reduced (1.2f);
            const auto col1 = dist > 5.0f ? Skin::red : dist > 3.0f ? Skin::amber : eyeCol;
            if (on)
            {
                g.setColour (col1.withAlpha (0.25f));
                g.fillRect (cell.expanded (1.5f));
                g.setColour (col1.brighter (0.3f));
            }
            else
                g.setColour (col1.withAlpha (0.08f));
            g.fillRect (cell);
        }
    }

    // neck and shoulders
    auto neck = Rectangle<float> (cx - 50.0f, 464.0f, 100.0f, 34.0f);
    g.setGradientFill (juce::ColourGradient (Skin::steelLo, neck.getTopLeft(), Skin::steel, neck.getCentre(), false));
    g.fillRect (neck);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (neck.getY() + (float) i * 8.5f), neck.getX(), neck.getRight());

    juce::Path shoulders;
    shoulders.addRoundedRectangle (20.0f, 496.0f, W - 40.0f, 80.0f, 26.0f);
    g.setGradientFill (juce::ColourGradient (Skin::steelHi, { 0, 496 }, Skin::steelLo, { 0, 576 }, false));
    g.fillPath (shoulders);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.strokePath (shoulders, juce::PathStrokeType (2.0f));
    g.setFont (mono (16.0f));
    g.setColour (Skin::steelLo.darker (0.6f));
    g.drawText ("SKZ-01  //  VOICE UNIT", Rectangle<float> (0, 506, W, 24), juce::Justification::centred);
    for (int i = 0; i < 2; ++i)
        glowDot (g, { i == 0 ? 60.0f : W - 60.0f, 518.0f }, 4.0f, Skin::lcd, 0.4f + 0.6f * blink);
}

//==============================================================================
void SkaboticAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    using juce::Rectangle;
    const auto all = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff10151b), all.getTopLeft(), Skin::bg, all.getBottomLeft(), false));
    g.fillAll();

    // circuit grid
    g.setColour (Skin::cyan.withAlpha (0.035f));
    for (float x = 0; x < all.getWidth(); x += 24) g.drawVerticalLine ((int) x, 0, all.getHeight());
    for (float y = 0; y < all.getHeight(); y += 24) g.drawHorizontalLine ((int) y, 0, all.getWidth());

    // header bar
    auto header = all.withHeight (76.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b333c), header.getTopLeft(), juce::Colour (0xff0c0f13), header.getBottomLeft(), false));
    g.fillRect (header);
    brushed (g, header, 0.05f);
    g.setColour (Skin::cyan.withAlpha (0.6f));
    g.fillRect (header.withTop (header.getBottom() - 2.0f));
    for (auto p : { juce::Point<float> (10, 10), { 10, 66 }, { all.getWidth() - 10, 10 }, { all.getWidth() - 10, 66 } })
        screw (g, p, 4.0f);

    // title with a glow pass under chrome letters
    const auto titleBox = Rectangle<float> (30, 6, 420, 50);
    g.setFont (mono (44.0f));
    g.setColour (Skin::cyan.withAlpha (0.25f));
    for (auto d : { juce::Point<float> (-2, 0), { 2, 0 }, { 0, -2 }, { 0, 2 } })
        g.drawText ("SKABOTIC", titleBox.translated (d.x, d.y), juce::Justification::centredLeft);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, titleBox.getTopLeft(), Skin::steel, titleBox.getBottomLeft(), false));
    g.drawText ("SKABOTIC", titleBox, juce::Justification::centredLeft);
    g.setFont (mono (11.5f));
    g.setColour (Skin::cyan);
    g.drawText ("ROBOTIC VOICE MUTATOR  //  by ASHER SWISSA", Rectangle<float> (32, 52, 420, 16), juce::Justification::centredLeft);

    if (logo.isValid())
    {
        const float h = 54.0f, w = h * (float) logo.getWidth() / (float) logo.getHeight();
        g.drawImage (logo, Rectangle<float> (all.getWidth() - 24.0f - w, 11.0f, w, h), juce::RectanglePlacement::centred);
    }

    plate (g, { 20, 92, 330, 632 }, "VOICE CORE", Skin::cyan);
    plate (g, { 790, 92, 330, 632 }, "MUTATOR", Skin::red);

    g.setFont (mono (12.5f));
    g.setColour (Skin::cyan.interpolatedWith (Skin::text, 0.4f));
    g.drawText ("CARRIER", Rectangle<float> (44, 140, 140, 18), juce::Justification::centredLeft);
    g.drawText ("SOURCE", Rectangle<float> (190, 140, 140, 18), juce::Justification::centredLeft);
    g.drawText ("AUTO-TUNE KEY", Rectangle<float> (44, 198, 140, 18), juce::Justification::centredLeft);
    g.drawText ("SCALE", Rectangle<float> (190, 198, 140, 18), juce::Justification::centredLeft);

    plate (g, { 20, 740, 1100, 146 }, "SCI-FI LAB", Skin::magenta);

    // chest plate under the head
    plate (g, { 370, 632, 400, 92 }, "", Skin::lcd);
}

//==============================================================================
SkaboticAudioProcessorEditor::SkaboticAudioProcessorEditor (SkaboticAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    canvas.logo = juce::ImageFileFormat::loadFrom (BinaryData::skazi_logo_png, BinaryData::skazi_logo_pngSize);
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (face);
    face.setBounds (370, 80, 400, 552);
    face.setInterceptsMouseClicks (false, false);

    // VOICE CORE
    auto setupBox = [this] (juce::ComboBox& box, const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
            box.addItemList (choice->choices, 1);
        canvas.addAndMakeVisible (box);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, box);
    };
    setupBox (carrierBox, ParamIDs::carrier, carrierAttachment);
    setupBox (sourceBox, ParamIDs::source, sourceAttachment);
    setupBox (keyBox, ParamIDs::key, keyAttachment);
    setupBox (scaleBox, ParamIDs::scale, scaleAttachment);
    carrierBox.setBounds (40, 160, 136, 32);
    sourceBox.setBounds (186, 160, 146, 32);
    keyBox.setBounds (40, 218, 136, 32);
    scaleBox.setBounds (186, 218, 146, 32);

    const std::pair<RobotKnob*, const char*> left[] { { &vocode, ParamIDs::vocode }, { &hiss, ParamIDs::hiss },
                                                      { &note, ParamIDs::note }, { &octave, ParamIDs::octave },
                                                      { &formant, ParamIDs::formant }, { &react, ParamIDs::react },
                                                      { &glide, ParamIDs::glide } };
    for (int i = 0; i < 7; ++i)
    {
        attach (*left[i].first, left[i].second);
        left[i].first->setBounds (30 + (i % 2) * 155 + (i == 6 ? 77 : 0), 262 + (i / 2) * 115, 155, 112);
    }

    // SCI-FI LAB
    const std::pair<RobotKnob*, const char*> lab[] { { &shift, ParamIDs::shift }, { &army, ParamIDs::army },
                                                     { &stutter, ParamIDs::stutter }, { &stutSize, ParamIDs::stutSize },
                                                     { &chop, ParamIDs::chop }, { &chopRate, ParamIDs::chopRate },
                                                     { &width, ParamIDs::width } };
    for (int i = 0; i < 7; ++i)
    {
        attach (*lab[i].first, lab[i].second);
        lab[i].first->setBounds (40 + i * 152, 776, 150, 106);
    }

    // MUTATOR
    const std::pair<RobotKnob*, const char*> right[] { { &pitch, ParamIDs::pitch }, { &drive, ParamIDs::drive },
                                                       { &ring, ParamIDs::ring }, { &ringFreq, ParamIDs::ringFreq },
                                                       { &bits, ParamIDs::bits }, { &crush, ParamIDs::crush },
                                                       { &metal, ParamIDs::metal }, { &metalFreq, ParamIDs::metalFreq } };
    for (int i = 0; i < 8; ++i)
    {
        attach (*right[i].first, right[i].second);
        right[i].first->setBounds (800 + (i % 2) * 155, 136 + (i / 2) * 146, 155, 140);
    }

    // chest: output section
    const std::pair<RobotKnob*, const char*> chest[] { { &gate, ParamIDs::gate }, { &mix, ParamIDs::mix }, { &output, ParamIDs::output } };
    for (int i = 0; i < 3; ++i)
    {
        attach (*chest[i].first, chest[i].second);
        chest[i].first->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        chest[i].first->setBounds (382 + i * 128, 640, 124, 78);
    }

    // presets in the header
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0)
            proc.setCurrentProgram (presetBox.getSelectedId() - 1);
    };
    auto step = [this] (int delta)
    {
        const int count = proc.getNumPrograms();
        proc.setCurrentProgram ((juce::jmax (0, proc.getCurrentProgram()) + delta + count) % count);
    };
    prevButton.onClick = [step] { step (-1); };
    nextButton.onClick = [step] { step (1); };
    randomButton.onClick = [this] { proc.randomise(); };
    for (juce::Component* c : { (juce::Component*) &presetBox, (juce::Component*) &prevButton, (juce::Component*) &nextButton, (juce::Component*) &randomButton })
        canvas.addAndMakeVisible (c);
    prevButton.setBounds (470, 20, 34, 36);
    presetBox.setBounds (510, 20, 250, 36);
    nextButton.setBounds (766, 20, 34, 36);
    randomButton.setBounds (812, 20, 100, 36);
    syncPresetBox();

    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (designWidth, designHeight);
    startTimerHz (30);
}

SkaboticAudioProcessorEditor::~SkaboticAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SkaboticAudioProcessorEditor::attach (RobotKnob& k, const char* id)
{
    canvas.addAndMakeVisible (k);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider));
    if (auto* param = proc.apvts.getParameter (id))
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void SkaboticAudioProcessorEditor::resized()
{
    canvas.setBounds (0, 0, designWidth, designHeight);
    canvas.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) designWidth));
}

void SkaboticAudioProcessorEditor::syncPresetBox()
{
    const int want = proc.randomised ? -1 : proc.getCurrentProgram();
    if (want == shownPreset)
        return;
    shownPreset = want;
    if (want < 0)
        presetBox.setText ("~ RANDOM BOT ~", juce::dontSendNotification);
    else
        presetBox.setSelectedId (want + 1, juce::dontSendNotification);
}

void SkaboticAudioProcessorEditor::timerCallback()
{
    face.tick();
    syncPresetBox();

    auto value = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const int src = (int) value (ParamIDs::source);
    auto dim = [] (juce::Component& c, bool active) { c.setAlpha (active ? 1.0f : 0.3f); };
    dim (note, src == SkaboticAudioProcessor::srcNote);
    dim (keyBox, src == SkaboticAudioProcessor::srcAutoTune);
    dim (scaleBox, src == SkaboticAudioProcessor::srcAutoTune);
    dim (glide, src != SkaboticAudioProcessor::srcMidiPoly);
    dim (stutSize, value (ParamIDs::stutter) > 0.0f);
    dim (chopRate, value (ParamIDs::chop) > 0.0f);
}
