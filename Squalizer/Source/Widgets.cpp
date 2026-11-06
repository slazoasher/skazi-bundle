#include "Widgets.h"

namespace
{
    juce::Point<float> polar (juce::Point<float> centre, float radius, float angle)
    {
        return centre.translated (radius * std::sin (angle), -radius * std::cos (angle));
    }
}

//==============================================================================
Knob::Knob (const juce::String& labelText, bool isBipolar)
    : juce::Slider (RotaryHorizontalVerticalDrag, NoTextBox),
      label (labelText.toUpperCase()), bipolar (isBipolar)
{
    setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    setMouseDragSensitivity (baseSensitivity);
    setVelocityBasedMode (false);
    setRepaintsOnMouseActivity (true);
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    if (e.mouseWasClicked() && ! e.mods.isAnyModifierKeyDown() && e.position.y > (float) getHeight() - 17.0f && isEnabled())
        showTextEntry();
}

void Knob::showTextEntry()
{
    entry = std::make_unique<juce::TextEditor>();
    entry->setFont (Theme::mono (12.0f, true));
    entry->setJustification (juce::Justification::centred);
    entry->setColour (juce::TextEditor::backgroundColourId, Theme::glass);
    entry->setColour (juce::TextEditor::textColourId, accent);
    entry->setColour (juce::TextEditor::outlineColourId, accent.withAlpha (0.6f));
    entry->setColour (juce::TextEditor::focusedOutlineColourId, accent);
    entry->setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    entry->setColour (juce::CaretComponent::caretColourId, accent);
    entry->setText (getTextFromValue (getValue()), false);
    entry->setBounds (getLocalBounds().removeFromBottom (18).reduced (4, 0));
    addAndMakeVisible (*entry);
    entry->selectAll();
    entry->grabKeyboardFocus();

    juce::Component::SafePointer<Knob> safe (this);
    auto close = [safe] (bool apply)
    {
        // Deferred: the editor must not be deleted from inside its own callback.
        juce::MessageManager::callAsync ([safe, apply] { if (safe != nullptr) safe->closeTextEntry (apply); });
    };
    entry->onReturnKey = [close] { close (true); };
    entry->onEscapeKey = [close] { close (false); };
    entry->onFocusLost = [close] { close (false); };
}

void Knob::closeTextEntry (bool apply)
{
    if (entry == nullptr)
        return;
    if (apply)
    {
        const auto text = entry->getText().trim();
        if (text.isNotEmpty())
            setValue (getValueFromText (text), juce::sendNotificationSync);
    }
    entry.reset();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    // Shift held when grabbing = 8x finer for the whole drag.
    setMouseDragSensitivity (e.mods.isShiftDown() ? baseSensitivity * 8 : baseSensitivity);
    juce::Slider::mouseDown (e);
}

void Knob::paint (juce::Graphics& g)
{
    // Console-style knob: matte charcoal body with a soft 3D shade, a thin value ring in the band colour and a white pointer.
    const auto bounds = getLocalBounds().toFloat();
    const auto labelArea = bounds.withHeight (15.0f);
    const auto valueArea = bounds.withTop (bounds.getBottom() - 15.0f);
    const auto knobArea = bounds.withTrimmedTop (16.0f).withTrimmedBottom (16.0f);
    const float size = juce::jmin (knobArea.getWidth() - 12.0f, knobArea.getHeight());
    const auto centre = knobArea.getCentre();
    const float radius = size * 0.5f - 1.0f;

    const bool active = isEnabled();
    const bool hot = isMouseOverOrDragging() && active;
    const auto rotary = getRotaryParameters();
    const float a0 = rotary.startAngleRadians, a1 = rotary.endAngleRadians;
    const float norm = (float) valueToProportionOfLength (getValue());
    const float angle = a0 + norm * (a1 - a0);
    const float origin = bipolar ? 0.5f : 0.0f;

    if (! active)
        g.beginTransparencyLayer (0.35f);

    // Ring: a dark groove, the value in colour from the origin.
    const float ringRadius = radius - 2.0f, ringWidth = juce::jmax (2.5f, radius * 0.07f);
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, a0, a1, true);
    g.setColour (juce::Colour (0xff0b0d0e));
    g.strokePath (track, juce::PathStrokeType (ringWidth + 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff2a2f33));
    g.strokePath (track, juce::PathStrokeType (ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float arcFrom = a0 + origin * (a1 - a0);
    if (std::abs (angle - arcFrom) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, juce::jmin (arcFrom, angle), juce::jmax (arcFrom, angle), true);
        g.setColour (accent.withAlpha (hot ? 0.35f : 0.18f));
        g.strokePath (arc, juce::PathStrokeType (ringWidth + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (hot ? accent.brighter (0.2f) : accent);
        g.strokePath (arc, juce::PathStrokeType (ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (scaleMin.isNotEmpty() && size > 70.0f)
    {
        g.setFont (Theme::face (9.5f, false));
        g.setColour (Theme::legendDim);
        const auto pMin = polar (centre, ringRadius, a0), pMax = polar (centre, ringRadius, a1);
        g.drawText (scaleMin, juce::Rectangle<float> (40.0f, 10.0f).withCentre (pMin.translated (-4.0f, 10.0f)), juce::Justification::centred);
        g.drawText (scaleMax, juce::Rectangle<float> (40.0f, 10.0f).withCentre (pMax.translated (4.0f, 10.0f)), juce::Justification::centred);
    }

    // Body: soft drop shadow, skirt, then a slightly concave face.
    const float bodyR = radius * 0.72f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.65f), centre.translated (0.0f, bodyR * 0.25f),
                                             juce::Colours::transparentBlack, centre.translated (bodyR * 1.35f, bodyR * 0.25f), true));
    g.fillEllipse (body.expanded (bodyR * 0.35f).translated (0.0f, bodyR * 0.2f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff50565c), body.getX(), body.getY(), juce::Colour (0xff1b1e21), body.getX(), body.getBottom(), false));
    g.fillEllipse (body);
    const auto face = body.reduced (bodyR * 0.1f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2f33), face.getX(), face.getY(), juce::Colour (0xff3b4045), face.getX(), face.getBottom(), false));
    g.fillEllipse (face);
    g.setColour (juce::Colours::white.withAlpha (hot ? 0.16f : 0.09f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawEllipse (body.expanded (0.5f), 1.0f);

    // Pointer.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawLine ({ polar (centre, bodyR * 0.3f, angle).translated (0.0f, 1.0f), polar (centre, bodyR * 0.86f, angle).translated (0.0f, 1.0f) }, juce::jmax (2.0f, bodyR * 0.08f));
    g.setColour (juce::Colour (0xffeef1f3));
    g.drawLine ({ polar (centre, bodyR * 0.3f, angle), polar (centre, bodyR * 0.86f, angle) }, juce::jmax (2.0f, bodyR * 0.08f));

    Theme::legendText (g, label, labelArea, juce::Justification::centred, hot ? Theme::legend.brighter (0.3f) : Theme::legend.withAlpha (0.85f), 0.12f, 10.5f);
    g.setFont (Theme::mono (11.5f, true));
    g.setColour (hot ? accent.brighter (0.3f) : juce::Colour (0xffdfe5e2));
    g.drawFittedText (getTextFromValue (getValue()), valueArea.toNearestInt(), juce::Justification::centred, 1, 0.7f);

    if (! active)
        g.endTransparencyLayer();
}

//==============================================================================
LedSelector::LedSelector (juce::RangedAudioParameter& parameter, juce::StringArray labelTexts, bool isVertical)
    : labels (std::move (labelTexts)), vertical (isVertical),
      attachment (parameter, [this] (float v) { value = v; repaint(); })
{
    attachment.sendInitialUpdate();
}

int LedSelector::indexAt (juce::Point<float> p) const
{
    if (labels.isEmpty())
        return -1;
    const float along = vertical ? p.y / (float) getHeight() : p.x / (float) getWidth();
    return juce::jlimit (0, labels.size() - 1, (int) (along * (float) labels.size()));
}

void LedSelector::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float cellWidth = bounds.getWidth() / (float) labels.size();
    const int selected = juce::roundToInt (value);

    if (vertical)
    {
        const float cellHeight = bounds.getHeight() / (float) labels.size();
        for (int i = 0; i < labels.size(); ++i)
        {
            const auto cell = juce::Rectangle<float> (bounds.getX(), bounds.getY() + cellHeight * (float) i, bounds.getWidth(), cellHeight).reduced (0.0f, 4.0f);
            const bool isOn = i == selected;
            const auto capArea = cell.reduced (2.0f, 0.0f);
            Theme::buttonCap (g, capArea, isOn || i == pressed, i == hovered, Theme::amber, isOn ? 1.0f : 0.0f);
            const auto text = capArea.translated (0.0f, (isOn || i == pressed) ? 1.5f : 0.0f);
            if (isOn)
                Theme::glowText (g, labels[i], text, juce::Justification::centred, Theme::amber.brighter (0.4f), Theme::face (11.5f).withExtraKerningFactor (0.1f));
            else
                Theme::legendText (g, labels[i], text, juce::Justification::centred, Theme::legend.withAlpha (0.75f), 0.1f, 11.0f);
        }
        return;
    }

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto cell = juce::Rectangle<float> (bounds.getX() + cellWidth * (float) i, bounds.getY(), cellWidth, bounds.getHeight()).reduced (5.0f, 0.0f);
        const bool isOn = i == selected;
        const auto capArea = cell.reduced (0.0f, 3.0f);
        Theme::buttonCap (g, capArea, isOn || i == pressed, i == hovered, Theme::amber, isOn ? 1.0f : 0.0f);
        const auto text = capArea.translated (0.0f, (isOn || i == pressed) ? 1.5f : 0.0f);
        if (isOn)
            Theme::glowText (g, labels[i], text, juce::Justification::centred, Theme::amber.brighter (0.4f), Theme::face (12.0f).withExtraKerningFactor (0.1f));
        else
            Theme::legendText (g, labels[i], text, juce::Justification::centred, Theme::legend.withAlpha (0.75f), 0.1f, 11.5f);
    }
}

void LedSelector::mouseDown (const juce::MouseEvent& e)
{
    pressed = indexAt (e.position);
    if (pressed >= 0 && std::abs (value - (float) pressed) > 0.001f)
        attachment.setValueAsCompleteGesture ((float) pressed);
    repaint();
}

void LedSelector::mouseUp (const juce::MouseEvent&)
{
    pressed = -1;
    repaint();
}

void LedSelector::mouseMove (const juce::MouseEvent& e)
{
    const int index = indexAt (e.position);
    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void LedSelector::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
LedToggle::LedToggle (const juce::String& text, juce::Colour colour)
    : juce::ToggleButton (text), ledColour (colour)
{
}

void LedToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto cap = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const bool on = getToggleState();
    Theme::buttonCap (g, cap, on || down, highlighted, ledColour, on ? 1.0f : 0.0f);
    if (on || down)
        cap = cap.translated (0.0f, 1.5f);

    Theme::led (g, { cap.getX() + 13.0f, cap.getCentreY() }, 3.4f, ledColour, on ? 1.0f : 0.0f);
    if (on)
        Theme::glowText (g, getButtonText(), cap.withTrimmedLeft (20.0f), juce::Justification::centred, ledColour.brighter (0.5f), Theme::face (11.5f).withExtraKerningFactor (0.12f));
    else
        Theme::legendText (g, getButtonText(), cap.withTrimmedLeft (20.0f), juce::Justification::centred, Theme::legend.withAlpha (0.8f), 0.12f, 11.5f);
}

//==============================================================================
PanelButton::PanelButton (const juce::String& text) : juce::Button (text)
{
}

void PanelButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto cap = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const bool sunk = down || flash > 0.6f;
    Theme::buttonCap (g, cap, sunk, highlighted, Theme::amber, juce::jlimit (0.0f, 1.0f, flash));
    if (sunk)
        cap = cap.translated (0.0f, 1.5f);
    Theme::legendText (g, getButtonText(), cap, juce::Justification::centred,
                       Theme::legend.interpolatedWith (Theme::amber, juce::jlimit (0.0f, 1.0f, flash)), 0.12f, 11.5f);
}

//==============================================================================
PanelLookAndFeel::PanelLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, Theme::amber);
    setColour (juce::ComboBox::arrowColourId, Theme::amber);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0a120d));
    setColour (juce::PopupMenu::textColourId, Theme::legend);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::amber.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, Theme::amber);
    setColour (juce::PopupMenu::headerTextColourId, Theme::legendDim);
}

void PanelLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (3.0f);
    Theme::recess (g, bounds, 2.0f);

    juce::Path arrow;
    const float cx = bounds.getRight() - 13.0f, cy = bounds.getCentreY();
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour ((box.isMouseOver (true) ? Theme::amber : Theme::amberDim));
    g.fillPath (arrow);
    Theme::glassSheen (g, bounds);
}

void PanelLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 2, box.getWidth() - 26, box.getHeight() - 4);
    label.setFont (getComboBoxFont (box));
}

juce::Font PanelLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Theme::mono (box.getHeight() < 30 ? 12.0f : 15.0f, true);
}

juce::Font PanelLookAndFeel::getPopupMenuFont()
{
    return Theme::face (14.0f, false);
}

void PanelLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (juce::Colour (0xff0a120d));
    g.setColour (Theme::bevel);
    g.drawRect (0, 0, width, height, 1);
}
