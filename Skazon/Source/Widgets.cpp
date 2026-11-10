#include "Widgets.h"

namespace
{
    // Short readout for a knob top: ints, 0..1 ranges as %, thousands as k.
    juce::String formatKnobValue (const juce::Slider& s)
    {
        const auto range = s.getRange();
        const double v = s.getValue();
        if (s.getInterval() >= 1.0)                                 return juce::String (juce::roundToInt (v));
        if (range.getStart() >= -1.0 && range.getEnd() <= 1.0)     return juce::String (juce::roundToInt (v * 100.0)) + "%";
        if (std::abs (v) >= 1000.0)                                 return juce::String (v / 1000.0, 1) + "k";
        return juce::String (v, std::abs (v) < 10.0 ? 2 : (std::abs (v) < 100.0 ? 1 : 0));
    }

    juce::Point<float> polar (juce::Point<float> centre, float radius, float angle)
    {
        return centre.translated (radius * std::sin (angle), -radius * std::cos (angle));
    }
}

//==============================================================================
NeonKnob::NeonKnob (const juce::String& labelText, juce::Colour accentColour, bool isBipolar)
    : juce::Slider (RotaryHorizontalVerticalDrag, NoTextBox),
      label (labelText.toUpperCase()), accent (accentColour), bipolar (isBipolar)
{
    setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    setMouseDragSensitivity (240);
    setRepaintsOnMouseActivity (true);
}

void NeonKnob::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto labelArea = bounds.withTop (bounds.getBottom() - 16.0f);
    const auto knobArea = bounds.withTrimmedBottom (18.0f);
    const float size = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    const auto centre = knobArea.getCentre();
    const float radius = size * 0.5f - 3.0f;

    const bool active = isEnabled();
    const bool hot = isMouseOverOrDragging() && active;
    const float alpha = active ? 1.0f : 0.3f;
    const auto rotary = getRotaryParameters();
    const float a0 = rotary.startAngleRadians, a1 = rotary.endAngleRadians;
    const float norm = (float) valueToProportionOfLength (getValue());
    const float angle = a0 + norm * (a1 - a0);
    const float origin = bipolar ? 0.5f : 0.0f;

    // Segmented outer ring.
    const int ticks = 41;
    for (int i = 0; i < ticks; ++i)
    {
        const float t = (float) i / (float) (ticks - 1);
        const float a = a0 + t * (a1 - a0);
        const bool lit = t >= juce::jmin (origin, norm) - 0.001f && t <= juce::jmax (origin, norm) + 0.001f && ! (bipolar && std::abs (norm - 0.5f) < 0.01f);
        const float inner = radius * 0.84f;
        const float outer = radius * (i % 5 == 0 ? 1.0f : 0.94f);
        g.setColour (lit ? accent.interpolatedWith (Theme::magenta, t * 0.5f).withAlpha (alpha)
                         : Theme::dim.withAlpha (0.28f * alpha));
        g.drawLine ({ polar (centre, inner, a), polar (centre, outer, a) }, lit ? 2.0f : 1.4f);
    }

    // Value arc.
    const float arcRadius = radius * 0.72f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, a0, a1, true);
    g.setColour (Theme::edge.withAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (3.0f));

    const float arcFrom = a0 + origin * (a1 - a0);
    if (std::abs (angle - arcFrom) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, arcFrom, angle, true);
        Theme::glowStroke (g, arc, accent, 2.6f, (hot ? 1.0f : 0.75f) * alpha);
    }

    // Live modulation marker.
    if (modRange && active)
        if (const auto range = modRange(); ! range.isEmpty())
        {
            const float lo = juce::jlimit (0.0f, 1.0f, range.getStart()), hi = juce::jlimit (0.0f, 1.0f, range.getEnd());
            juce::Path depth;
            depth.addCentredArc (centre.x, centre.y, radius * 1.06f, radius * 1.06f, 0.0f, a0 + lo * (a1 - a0), a0 + hi * (a1 - a0), true);
            Theme::glowStroke (g, depth, Theme::magenta, 2.0f, 0.8f);
        }

    if (dropHover)
    {
        g.setColour (Theme::magenta.withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.2f, radius * 2.2f).withCentre (centre));
        g.setColour (Theme::magenta);
        g.drawEllipse (juce::Rectangle<float> (radius * 2.2f, radius * 2.2f).withCentre (centre), 2.0f);
    }

    if (const float live = modulation && active ? modulation() : -1.0f; live >= 0.0f)
    {
        const float mod = juce::jlimit (0.0f, 1.0f, live);
        const float modAngle = a0 + mod * (a1 - a0);
        const float modR = radius * (Theme::skin == Theme::ironMachine || Theme::skin == Theme::blackBox ? 0.9f : 0.62f);
        if (std::abs (modAngle - angle) > 0.02f)
        {
            juce::Path modArc;
            modArc.addCentredArc (centre.x, centre.y, modR, modR, 0.0f, angle, modAngle, true);
            g.setColour (Theme::magenta.withAlpha (0.8f));
            g.strokePath (modArc, juce::PathStrokeType (1.6f));
        }
        Theme::glowDot (g, polar (centre, modR, modAngle), 2.2f, Theme::magenta);
    }

    // Body + pointer: one shape per skin.
    const float bodyRadius = radius * 0.54f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    if (Theme::skin == Theme::blackBox)
    {
        // Matte black fluted knob: soft shadow, ribbed skirt, domed top with a faint sheen, white index line.
        const float r = radius * 0.8f;
        const auto outer = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);
        for (int i = 4; i > 0; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.16f * alpha));
            g.fillEllipse (outer.translated (0.0f, (float) i * 1.2f).expanded ((float) i * 0.6f));
        }
        juce::Path flutes; // 18 rounded ribs around the rim, turning with the knob
        const int ribs = 18;
        for (int i = 0; i <= ribs * 4; ++i)
        {
            const float a = angle + (float) i / (float) (ribs * 4) * juce::MathConstants<float>::twoPi;
            const float rr = r * (0.93f + 0.07f * std::cos ((float) i / 4.0f * juce::MathConstants<float>::twoPi));
            if (i == 0) flutes.startNewSubPath (polar (centre, rr, a)); else flutes.lineTo (polar (centre, rr, a));
        }
        flutes.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d41), outer.getTopLeft(), juce::Colour (0xff050506), outer.getBottomRight(), false));
        g.fillPath (flutes);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.strokePath (flutes, juce::PathStrokeType (0.8f));
        const auto top = outer.reduced (r * 0.2f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2f33), top.getCentreX(), top.getY(), juce::Colour (0xff0d0e10), top.getCentreX(), top.getBottom(), false));
        g.fillEllipse (top);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), top.getCentreX(), top.getY() + top.getHeight() * 0.15f,
                                                 juce::Colours::white.withAlpha (0.0f), top.getCentreX(), top.getCentreY(), false));
        g.fillEllipse (top.reduced (top.getWidth() * 0.08f).withTrimmedBottom (top.getHeight() * 0.4f));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (top, 0.8f);
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.drawLine ({ polar (centre, r * 0.5f, angle), polar (centre, r * 0.98f, angle) }, 2.4f);
        // value printed on every knob top
        {
            g.setColour (Theme::text.withAlpha (alpha));
            g.setFont (Theme::mono (juce::jlimit (8.0f, 12.0f, top.getWidth() * 0.26f), true));
            g.drawFittedText (formatKnobValue (*this), top.reduced (top.getWidth() * 0.16f).toNearestInt(), juce::Justification::centred, 1, 0.5f);
        }
    }
    else if (Theme::skin == Theme::ironMachine)
    {
        // Big 3D knob: cast shadow, black skirt with ticks, knurled side, brushed aluminium cap with a specular spot, red index.
        const float r = radius * 0.82f;
        const auto outer = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);
        for (int i = 5; i > 0; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.09f * alpha));
            g.fillEllipse (outer.translated ((float) i * 0.7f, (float) i * 1.3f).expanded ((float) i * 0.5f));
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2f34), outer.getTopLeft(), juce::Colour (0xff050506), outer.getBottomRight(), false));
        g.fillEllipse (outer);
        g.setColour (juce::Colours::white.withAlpha (0.12f * alpha));
        g.drawEllipse (outer.reduced (0.6f), 0.8f);
        const float sr = r * 0.84f;
        const auto side = juce::Rectangle<float> (sr * 2.0f, sr * 2.0f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2ac5e), side.getTopLeft(), juce::Colour (0xff3a240c), side.getBottomRight(), false));
        g.fillEllipse (side);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        for (int i = 0; i < 48; ++i)
        {
            const float a = angle + (float) i / 48.0f * juce::MathConstants<float>::twoPi; // knurl turns with the knob
            g.drawLine ({ polar (centre, sr * 0.84f, a), polar (centre, sr, a) }, 0.9f);
        }
        const auto cap = side.reduced (sr * 0.17f).translated (0.0f, -sr * 0.05f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffde9a8), cap.getTopLeft(), juce::Colour (0xff8a5a1f), cap.getBottomRight(), false));
        g.fillEllipse (cap);
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip; clip.addEllipse (cap);
            g.reduceClipRegion (clip);
            g.setColour (juce::Colours::black.withAlpha (0.05f));
            for (float rr = 2.0f; rr < cap.getWidth() * 0.5f; rr += 1.6f) // concentric machining rings
                g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (cap.getCentre()), 0.6f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.75f), cap.getX() + cap.getWidth() * 0.32f, cap.getY() + cap.getHeight() * 0.25f,
                                                     juce::Colours::white.withAlpha (0.0f), cap.getX() + cap.getWidth() * 0.62f, cap.getY() + cap.getHeight() * 0.55f, true));
            g.fillEllipse (cap);
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (cap, 0.8f);
        g.setColour (juce::Colour (0xffd8240f).withAlpha (alpha));
        g.drawLine ({ polar (cap.getCentre(), cap.getWidth() * 0.34f, angle), polar (cap.getCentre(), cap.getWidth() * 0.48f, angle) }, 2.4f);
        // Value engraved into the brass top, on every knob.
        g.setColour (juce::Colour (0xff2a1806).withAlpha (alpha));
        g.setFont (Theme::mono (juce::jlimit (8.0f, 13.0f, cap.getWidth() * 0.24f), true));
        g.drawFittedText (formatKnobValue (*this), cap.reduced (cap.getWidth() * 0.13f).toNearestInt(), juce::Justification::centred, 1, 0.5f);
    }
    else if (Theme::skin == Theme::chromeIce)
    {
        // Machined metal cap: drop shadow, knurled rim, conical light, white index line.
        g.setColour (juce::Colours::black.withAlpha (0.55f * alpha));
        g.fillEllipse (body.translated (0.0f, 2.0f).expanded (1.5f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9d1da), centre.translated (-bodyRadius * 0.6f, -bodyRadius * 0.8f),
                                                 juce::Colour (0xff2a2f36), centre.translated (bodyRadius * 0.5f, bodyRadius), false));
        g.fillEllipse (body);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int i = 0; i < 36; ++i)
        {
            const float a = (float) i / 36.0f * juce::MathConstants<float>::twoPi;
            g.drawLine ({ polar (centre, bodyRadius * 0.88f, a), polar (centre, bodyRadius, a) }, 0.8f);
        }
        const auto cap = body.reduced (bodyRadius * 0.16f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a949f), cap.getTopLeft(), juce::Colour (0xff3a414a), cap.getBottomRight(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.drawLine ({ polar (centre, bodyRadius * 0.3f, angle), polar (centre, bodyRadius * 0.8f, angle) }, 2.2f);
    }
    else if (Theme::skin == Theme::sunsetGlass)
    {
        // Soft glass bead with a glowing dot pointer.
        g.setGradientFill (juce::ColourGradient (Theme::panelHi.brighter (0.5f), centre.translated (0.0f, -bodyRadius),
                                                 Theme::background, centre.translated (0.0f, bodyRadius), false));
        g.fillEllipse (body);
        g.setColour (juce::Colours::white.withAlpha (0.18f * alpha));
        g.drawEllipse (body.reduced (0.5f), 1.0f);
        Theme::glowDot (g, polar (centre, bodyRadius * 0.68f, angle), 2.4f, accent, alpha);
    }
    else if (Theme::skin == Theme::acidLab)
    {
        // Octagon nut with a hard accent rim and a straight index.
        juce::Path oct;
        for (int i = 0; i < 8; ++i)
        {
            const auto pt = polar (centre, bodyRadius, (float) i * juce::MathConstants<float>::pi / 4.0f + juce::MathConstants<float>::pi / 8.0f);
            if (i == 0) oct.startNewSubPath (pt); else oct.lineTo (pt);
        }
        oct.closeSubPath();
        g.setColour (Theme::panelHi.withAlpha (alpha));
        g.fillPath (oct);
        g.setColour (accent.withAlpha ((hot ? 1.0f : 0.7f) * alpha));
        g.strokePath (oct, juce::PathStrokeType (1.4f));
        g.drawLine ({ centre, polar (centre, bodyRadius * 0.9f, angle) }, 2.0f);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (Theme::panelHi.brighter (0.25f), centre.translated (-bodyRadius * 0.5f, -bodyRadius * 0.7f),
                                                 Theme::background, centre.translated (0.0f, bodyRadius), true));
        g.fillEllipse (body);
        g.setColour (accent.withAlpha ((hot ? 0.7f : 0.35f) * alpha));
        g.drawEllipse (body, 1.2f);

        juce::Path pointer;
        pointer.startNewSubPath (polar (centre, bodyRadius * 0.55f, angle));
        pointer.lineTo (polar (centre, bodyRadius * 0.95f, angle));
        Theme::glowStroke (g, pointer, accent, 2.0f, alpha);
    }

    // Readout. Compact knobs show it above the knob while hovered, since the body is too small.
    if (! compact && Theme::skin != Theme::ironMachine) // the machine knob shows its value on hover only, its cap is solid metal
    {
        g.setColour (Theme::text.withAlpha (alpha));
        g.setFont (Theme::mono (juce::jlimit (9.0f, 11.5f, size * 0.1f), true));
        g.drawFittedText (getTextFromValue (getValue()), body.reduced (4.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
    }
    else if (hot)
    {
        const auto tag = bounds.withHeight (14.0f);
        if (! compact) g.setFont (Theme::mono (11.0f, true));
        g.setColour (Theme::background.withAlpha (0.9f));
        g.fillRect (tag);
        g.setColour (accent);
        g.setFont (Theme::mono (10.0f, true));
        g.drawFittedText (getTextFromValue (getValue()), tag.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }

    g.setFont (Theme::mono (compact ? 10.0f : 11.0f, true));
    g.setColour ((hot ? accent : Theme::dim.brighter (0.5f)).withAlpha (alpha));
    g.drawText (label, labelArea, juce::Justification::centred);
}

//==============================================================================
SegmentedSelector::SegmentedSelector (juce::RangedAudioParameter& parameter, juce::StringArray labelTexts, juce::Colour accentColour)
    : labels (std::move (labelTexts)), accent (accentColour),
      attachment (parameter, [this] (float v) { value = v; repaint(); })
{
    attachment.sendInitialUpdate();
}

int SegmentedSelector::indexAt (juce::Point<float> p) const
{
    if (labels.isEmpty())
        return -1;
    return juce::jlimit (0, labels.size() - 1, (int) (p.x / ((float) getWidth() / (float) labels.size())));
}

void SegmentedSelector::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float cellWidth = bounds.getWidth() / (float) labels.size();

    g.setColour (Theme::background.withAlpha (0.8f));
    g.fillRect (bounds);

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto cell = juce::Rectangle<float> (bounds.getX() + cellWidth * (float) i, bounds.getY(), cellWidth, bounds.getHeight()).reduced (2.0f);
        const float weight = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (value - (float) i));
        const bool isOn = weight > 0.5f;
        const bool isHover = i == hovered;

        if (weight > 0.02f)
        {
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.45f * weight), cell.getTopLeft(), accent.withAlpha (0.12f * weight), cell.getBottomLeft(), false));
            g.fillRect (cell);
            juce::Path outline;
            outline.addRectangle (cell);
            Theme::glowStroke (g, outline, accent, 1.0f, 0.7f * weight);
        }
        if (weight < 0.98f)
        {
            g.setColour ((isHover ? accent.withAlpha (0.12f) : Theme::panelHi).withMultipliedAlpha (1.0f - weight));
            g.fillRect (cell);
            g.setColour (Theme::edge);
            g.drawRect (cell, 1.0f);
        }

        const auto colour = isOn ? Theme::text : (isHover ? accent : Theme::dim.brighter (0.4f));
        if (i < (int) icons.size() && icons[(size_t) i] != nullptr)
            icons[(size_t) i] (g, cell.reduced (cell.getWidth() * 0.2f, cell.getHeight() * 0.28f), colour);
        else
        {
            g.setColour (colour);
            g.setFont (Theme::mono (12.0f, true));
            g.drawText (labels[i], cell, juce::Justification::centred);
        }
    }
}

void SegmentedSelector::mouseDown (const juce::MouseEvent& e)
{
    const int index = indexAt (e.position);
    if (index >= 0 && std::abs (value - (float) index) > 0.001f)
        attachment.setValueAsCompleteGesture ((float) index);
}

void SegmentedSelector::mouseMove (const juce::MouseEvent& e)
{
    const int index = indexAt (e.position);
    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void SegmentedSelector::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
NeonToggle::NeonToggle (const juce::String& text, juce::Colour accentColour)
    : juce::ToggleButton (text), accent (accentColour)
{
}

void NeonToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = getToggleState();

    g.setColour (on ? accent.withAlpha (0.3f) : (highlighted ? accent.withAlpha (0.1f) : Theme::panelHi));
    g.fillRoundedRectangle (bounds, Theme::corner());

    juce::Path outline;
    outline.addRoundedRectangle (bounds, Theme::corner());
    if (on)
        Theme::glowStroke (g, outline, accent, 1.0f, 0.8f);
    else
    {
        g.setColour (Theme::edge.brighter (0.2f));
        g.strokePath (outline, juce::PathStrokeType (1.0f));
    }

    const auto led = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ bounds.getX() + 12.0f, bounds.getCentreY() });
    if (on)
        Theme::glowDot (g, led.getCentre(), 3.0f, accent);
    else
    {
        g.setColour (Theme::dim);
        g.drawEllipse (led, 1.0f);
    }

    g.setColour (on ? Theme::text : Theme::dim.brighter (0.4f));
    g.setFont (Theme::mono (12.0f, true));
    g.drawText (getButtonText(), bounds.withTrimmedLeft (20.0f), juce::Justification::centred);
}

//==============================================================================
NeonButton::NeonButton (const juce::String& text, juce::Colour accentColour, bool drawDice)
    : juce::Button (text), accent (accentColour), dice (drawDice)
{
}

void NeonButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const float glow = juce::jlimit (0.0f, 1.0f, flash + (down ? 0.6f : highlighted ? 0.25f : 0.0f));

    if (filled)
    {
        const auto base = isEnabled() ? accent : Theme::dim;
        const auto top = base.brighter (0.25f + 0.5f * glow), bottom = base.darker (0.35f - 0.25f * glow);
        if (glow > 0.0f)
        {
            g.setColour (base.withAlpha (0.35f * glow));
            g.fillRoundedRectangle (bounds.expanded (2.0f), 8.0f);
        }
        g.setGradientFill (juce::ColourGradient (top, 0.0f, bounds.getY(), bottom, 0.0f, bounds.getBottom(), false));
        const float round = Theme::skin == Theme::blackBox ? 2.0f : Theme::skin == Theme::ironMachine ? 3.0f : Theme::skin == Theme::acidLab ? 0.0f : Theme::skin == Theme::sunsetGlass ? 12.0f : 6.0f;
        g.fillRoundedRectangle (bounds, round);
        g.setColour (juce::Colours::white.withAlpha (0.35f + 0.4f * glow));
        g.drawRoundedRectangle (bounds.reduced (0.5f), round, 1.2f);

        auto area = bounds.reduced (6.0f, 3.0f);
        if (dice)
        {
            const auto die = area.removeFromLeft (area.getHeight() * 0.8f).reduced (3.0f).withSizeKeepingCentre (24.0f, 24.0f);
            juce::Graphics::ScopedSaveState save (g);
            g.addTransform (juce::AffineTransform::rotation (0.25f + flash * 1.5f, die.getCentreX(), die.getCentreY()));
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (die, 4.0f);
            g.setColour (Theme::background);
            const float pip = die.getWidth() * 0.17f;
            const float pips[5][2] { { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.5f, 0.5f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
            for (const auto& pp : pips)
                g.fillEllipse (juce::Rectangle<float> (pip, pip).withCentre ({ die.getX() + die.getWidth() * pp[0], die.getY() + die.getHeight() * pp[1] }));
        }
        // dark text on light colours, white on dark ones
        const auto ink = base.getPerceivedBrightness() > 0.62f ? Theme::background : juce::Colours::white;
        g.setColour (ink);
        if (subText.isEmpty())
        {
            g.setFont (Theme::sans (17.0f, true));
            g.drawFittedText (getButtonText(), area.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
        else
        {
            g.setFont (Theme::sans (16.0f, true));
            g.drawFittedText (getButtonText(), area.removeFromTop (area.getHeight() * 0.6f).toNearestInt(), juce::Justification::centredBottom, 1, 0.8f);
            g.setColour (ink.withAlpha (0.8f));
            g.setFont (Theme::mono (9.5f, true));
            g.drawFittedText (subText, area.toNearestInt(), juce::Justification::centredTop, 1, 0.8f);
        }
        return;
    }

    g.setColour (Theme::panelHi.interpolatedWith (accent, 0.12f + 0.3f * glow));
    g.fillRoundedRectangle (bounds, Theme::corner());
    juce::Path outline;
    outline.addRoundedRectangle (bounds, Theme::corner());
    Theme::glowStroke (g, outline, accent, 1.0f, 0.35f + 0.65f * glow);

    auto content = bounds;
    if (dice)
    {
        // A tilted die with five pips; it spins when the button flashes.
        const auto die = content.removeFromLeft (bounds.getHeight()).reduced (5.0f);
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (0.25f + flash * 1.5f, die.getCentreX(), die.getCentreY()));
        g.setColour (accent.withAlpha (0.25f + 0.5f * glow));
        g.fillRoundedRectangle (die, 3.0f);
        g.setColour (accent);
        g.drawRoundedRectangle (die, 3.0f, 1.4f);
        const float pip = die.getWidth() * 0.16f;
        const float pips[5][2] { { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.5f, 0.5f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
        for (const auto& pp : pips)
            g.fillEllipse (juce::Rectangle<float> (pip, pip).withCentre ({ die.getX() + die.getWidth() * pp[0], die.getY() + die.getHeight() * pp[1] }));
    }

    g.setColour (glow > 0.3f ? juce::Colours::white : Theme::text);
    g.setFont (Theme::mono (13.0f, true));
    g.drawText (getButtonText(), content, juce::Justification::centred);
}

//==============================================================================
NeonLookAndFeel::NeonLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, Theme::text);
    setColour (juce::ComboBox::arrowColourId, Theme::magenta);
    setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::magenta.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void NeonLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (2.0f);
    const auto accent = Theme::magenta;
    const float alpha = box.isEnabled() ? 1.0f : 0.35f;

    g.setColour (Theme::panelHi.withAlpha (alpha));
    g.fillRoundedRectangle (bounds, Theme::corner() * 0.7f);
    g.setColour ((box.isMouseOver() ? accent : Theme::edge.brighter (0.2f)).withAlpha (alpha));
    g.drawRoundedRectangle (bounds, Theme::corner() * 0.7f, 1.0f);

    juce::Path arrow;
    const float cx = bounds.getRight() - 14.0f, cy = bounds.getCentreY();
    arrow.addTriangle (cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 4.0f);
    g.setColour (accent.withAlpha (alpha));
    g.fillPath (arrow);
}

juce::Font NeonLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Theme::mono (box.getHeight() < 26 ? 11.0f : 13.0f, true);
}

// Bipolar bar for the mod matrix amounts: fills from the centre, gold up, pink down.
void NeonLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float, float, float,
                                        juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f, 4.0f);
    const float v = (float) slider.getValue();
    g.setColour (Theme::panelHi);
    g.fillRect (r);
    const float mid = r.getCentreX();
    const auto bar = v >= 0 ? r.withLeft (mid).withWidth (r.getWidth() * 0.5f * v) : r.withLeft (mid + r.getWidth() * 0.5f * v).withRight (mid);
    g.setColour ((v >= 0 ? Theme::lime : Theme::magenta).withAlpha (slider.isMouseOverOrDragging() ? 0.9f : 0.65f));
    g.fillRect (bar);
    g.setColour (Theme::edge.brighter (0.3f));
    g.drawRect (r, 1.0f);
    g.drawVerticalLine ((int) mid, r.getY(), r.getBottom());
    g.setColour (Theme::text);
    g.setFont (Theme::mono (11.0f, true));
    g.drawText (juce::String (juce::roundToInt (v * 100.0f)) + "%", r, juce::Justification::centred);
}

juce::Font NeonLookAndFeel::getPopupMenuFont()
{
    return Theme::mono (14.0f, true);
}

void NeonLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::magenta.withAlpha (0.6f));
    g.drawRect (0, 0, width, height, 1);
}
