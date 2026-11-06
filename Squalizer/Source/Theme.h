#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// CRT LAB: a dark lab console with curved phosphor screens, backlit glass keys and glowing knobs.
// Green phosphor is the accent; the frequency tuner glows amber with a red needle.
namespace Theme
{
    const juce::Colour plateTop    { 0xff1c201e };
    const juce::Colour plateBottom { 0xff0b0d0c };
    const juce::Colour ear         { 0xff0e100f };
    const juce::Colour groove      { 0xff030404 };
    const juce::Colour bevel       { 0xff343a37 };
    const juce::Colour legend      { 0xffc6e8d2 };
    const juce::Colour legendDim   { 0xff5e7a68 };
    const juce::Colour glass       { 0xff020a04 };
    const juce::Colour amber       { 0xff39ff8f };   // the accent: green phosphor
    const juce::Colour amberDim    { 0xff14592f };
    const juce::Colour tuner       { 0xffffb347 };
    const juce::Colour needle      { 0xffff4a3a };
    const juce::Colour ledRed      { 0xffff4433 };
    const juce::Colour ledGreen    { 0xff39ff8f };
    const juce::Colour ledYellow   { 0xffffd23a };
    const juce::Colour ledIce      { 0xff5ce1ff };

    inline juce::Font face (float size, bool bold = true)
    {
        static const juce::String family = []
        {
            const auto names = juce::Font::findAllTypefaceNames();
            for (auto* candidate : { "Bahnschrift", "DIN Alternate", "DIN Condensed" })
                if (names.contains (candidate))
                    return juce::String (candidate);
            return juce::Font::getDefaultSansSerifFontName();
        }();
        static const juce::StringArray styles = juce::Font::findAllTypefaceStyles (family);

        juce::String style = bold ? "Bold" : "Regular";
        if (bold && styles.contains ("SemiBold"))
            style = "SemiBold";
        if (! styles.contains (style))
            style = styles.isEmpty() ? juce::String ("Regular") : styles[0];
        return juce::Font (juce::FontOptions (family, style, size));
    }

    // Terminal numerals: Consolas on Windows, Menlo on macOS.
    inline juce::Font mono (float size, bool bold = false)
    {
        static const juce::String family = []
        {
            const auto names = juce::Font::findAllTypefaceNames();
            for (auto* candidate : { "Consolas", "Menlo", "Lucida Console" })
                if (names.contains (candidate))
                    return juce::String (candidate);
            return juce::Font::getDefaultMonospacedFontName();
        }();
        return juce::Font (juce::FontOptions (family, size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Phosphor bloom: the same path stroked wide and faint, then narrow and bright.
    inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity = 1.0f)
    {
        for (auto [w, a] : { std::pair { 7.0f, 0.07f }, { 4.0f, 0.14f }, { 2.2f, 0.3f } })
        {
            g.setColour (c.withAlpha (a * intensity));
            g.strokePath (p, juce::PathStrokeType (width * w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c.withAlpha (juce::jmin (1.0f, intensity)));
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.brighter (0.8f).withAlpha (0.5f * juce::jmin (1.0f, intensity)));
        g.strokePath (p, juce::PathStrokeType (width * 0.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Glowing text: a soft halo behind crisp characters.
    inline void glowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification just,
                          juce::Colour c, const juce::Font& font, float intensity = 1.0f)
    {
        g.setFont (font);
        g.setColour (c.withAlpha (0.18f * intensity));
        for (auto d : { juce::Point<float> (-1.2f, 0), { 1.2f, 0 }, { 0, -1.2f }, { 0, 1.2f } })
            g.drawText (text, area + d, just, false);
        g.setColour (c.withAlpha (juce::jmin (1.0f, 0.55f + 0.45f * intensity)));
        g.drawText (text, area, just, false);
    }

    // Embossed legend on the console metal.
    inline void legendText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification just,
                            juce::Colour colour = legend, float tracking = 0.12f, float size = 11.0f)
    {
        g.setFont (face (size).withExtraKerningFactor (tracking));
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawText (text, area.translated (0.0f, 1.0f), just, false);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawText (text, area.translated (0.0f, -1.0f), just, false);
        g.setColour (colour);
        g.drawText (text, area, just, false);
    }

    inline void grooveH (juce::Graphics& g, float x0, float x1, float y)
    {
        g.setColour (groove);
        g.fillRect (x0, y, x1 - x0, 1.0f);
        g.setColour (bevel.withAlpha (0.6f));
        g.fillRect (x0, y + 1.0f, x1 - x0, 1.0f);
    }

    inline void grooveV (juce::Graphics& g, float x, float y0, float y1)
    {
        g.setColour (groove);
        g.fillRect (x, y0, 1.0f, y1 - y0);
        g.setColour (bevel.withAlpha (0.6f));
        g.fillRect (x + 1.0f, y0, 1.0f, y1 - y0);
    }

    // Section heading: a tiny phosphor dot, the legend and a groove running off to the right.
    inline void sectionTitle (juce::Graphics& g, const juce::String& title, juce::Rectangle<float> area)
    {
        g.setFont (face (10.5f).withExtraKerningFactor (0.22f));
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 10.0f;
        g.setColour (amber.withAlpha (0.25f));
        g.fillEllipse (area.getX() - 1.0f, area.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (amber);
        g.fillEllipse (area.getX() + 1.5f, area.getCentreY() - 1.5f, 3.0f, 3.0f);
        legendText (g, title, area.withTrimmedLeft (11.0f).withWidth (w + 4.0f), juce::Justification::centredLeft, legendDim.brighter (0.3f), 0.22f, 10.5f);
        grooveH (g, area.getX() + w + 16.0f, area.getRight(), area.getCentreY());
    }

    // Raised 3D sub-panel: a machined block standing off the console with a lit top edge and a cast shadow.
    inline void raisedPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner = 8.0f)
    {
        juce::Path p;
        p.addRoundedRectangle (r, corner);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 18, { 0, 8 }).drawForPath (g, p);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff232826), r.getX(), r.getY(), juce::Colour (0xff111413), r.getX(), r.getBottom(), false));
        g.fillPath (p);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX(), r.getY() + 14.0f, false));
        g.strokePath (p, juce::PathStrokeType (1.2f));
        g.setColour (juce::Colours::black.withAlpha (0.9f));
        g.drawRoundedRectangle (r.expanded (0.5f), corner + 0.5f, 1.0f);
    }

    // Recessed window (preset readout).
    inline void recess (juce::Graphics& g, juce::Rectangle<float> r, float corner = 3.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
        g.setColour (bevel.withAlpha (0.7f));
        g.drawRoundedRectangle (r.expanded (2.5f), corner + 2.5f, 1.0f);
        g.setColour (glass);
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), r.getX(), r.getY(),
                                                 juce::Colours::transparentBlack, r.getX(), r.getY() + 10.0f, false));
        g.fillRoundedRectangle (r, corner);
    }

    // Thick CRT housing around a screen: deep rounded frame, chamfer and inner shadow, so the glass sits far back.
    inline void bezel (juce::Graphics& g, juce::Rectangle<float> window, float frame = 14.0f)
    {
        const auto outer = window.expanded (frame);
        juce::Path outerPath;
        outerPath.addRoundedRectangle (outer, 14.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.9f), 24, { 0, 10 }).drawForPath (g, outerPath);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff353b38), outer.getX(), outer.getY(), juce::Colour (0xff0c0e0d), outer.getX(), outer.getBottom(), false));
        g.fillPath (outerPath);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine ((int) outer.getY() + 1, outer.getX() + 12.0f, outer.getRight() - 12.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (outer, 14.0f, 1.2f);

        // Screws in the frame corners.
        for (auto c : { outer.getTopLeft().translated (8, 8), outer.getTopRight().translated (-8, 8),
                        outer.getBottomLeft().translated (8, -8), outer.getBottomRight().translated (-8, -8) })
        {
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c.translated (0.0f, 0.6f)));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff7d847f), c.translated (-2, -2), juce::Colour (0xff1d201e), c.translated (2, 3), true));
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
        }

        const auto chamfer = window.expanded (6.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff000000), chamfer.getX(), chamfer.getY(), juce::Colour (0xff2c322f), chamfer.getX(), chamfer.getBottom(), false));
        g.fillRoundedRectangle (chamfer, 12.0f);
        g.setColour (glass);
        g.fillRoundedRectangle (window, 10.0f);
    }

    // Curved-glass reflection and corner darkening of a CRT, drawn over the screen content.
    inline void glassSheen (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto c = r.getCentre();
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, c, juce::Colours::black.withAlpha (0.55f),
                                                 c.translated (r.getWidth() * 0.62f, r.getHeight() * 0.62f), true));
        g.fillRoundedRectangle (r, 10.0f);

        juce::Path sheen;
        sheen.addRoundedRectangle (r.reduced (6.0f).withHeight (r.getHeight() * 0.42f), 10.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), r.getX(), r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.42f, false));
        g.fillPath (sheen);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 1.0f);
    }

    // Indicator lamp: a glass dome that glows from inside when lit.
    inline void led (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float level)
    {
        const auto lens = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
        if (level > 0.02f)
        {
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f * level), centre, colour.withAlpha (0.0f), centre.translated (radius * 4.5f, 0.0f), true));
            g.fillEllipse (lens.expanded (radius * 3.5f));
        }
        g.setColour (juce::Colours::black);
        g.fillEllipse (lens.expanded (1.6f));
        const auto off = colour.withMultipliedBrightness (0.18f).withMultipliedSaturation (0.6f);
        const auto on = off.interpolatedWith (colour, level);
        g.setGradientFill (juce::ColourGradient (on.brighter (1.2f * level + 0.1f), centre.translated (-radius * 0.2f, -radius * 0.25f),
                                                 on.darker (0.6f - 0.4f * level), centre.translated (radius, radius), true));
        g.fillEllipse (lens);
        g.setColour (juce::Colours::white.withAlpha (0.25f + 0.5f * level));
        g.fillEllipse (juce::Rectangle<float> (radius * 0.8f, radius * 0.5f).withCentre (centre.translated (-radius * 0.28f, -radius * 0.45f)));
    }

    // Backlit glass key: dark smoked glass that lights up in colour from below when on.
    inline void buttonCap (juce::Graphics& g, juce::Rectangle<float> r, bool pressed, bool hover, juce::Colour light = amber, float level = 0.0f)
    {
        const auto hole = r.expanded (2.5f);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (hole, 6.0f);
        if (level > 0.01f)
        {
            g.setColour (light.withAlpha (0.25f * level));
            g.drawRoundedRectangle (hole.expanded (1.5f), 7.0f, 2.0f);
        }

        r = r.withTrimmedBottom (pressed ? 0.0f : 2.5f).translated (0.0f, pressed ? 1.5f : 0.0f);
        // Side wall of the key, visible below the face when it stands up.
        if (! pressed)
        {
            g.setColour (juce::Colour (0xff050605));
            g.fillRoundedRectangle (r.translated (0.0f, 2.5f), 4.5f);
        }
        const auto base = juce::Colour (0xff1b201d).interpolatedWith (light.withMultipliedBrightness (0.35f), level * 0.85f);
        const float lift = hover ? 0.12f : 0.0f;
        g.setGradientFill (juce::ColourGradient (base.brighter (0.35f + lift), r.getX(), r.getY(), base.darker (0.5f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.5f);
        if (level > 0.01f)
        {
            g.setGradientFill (juce::ColourGradient (light.withAlpha (0.35f * level), r.getCentreX(), r.getBottom(), light.withAlpha (0.0f), r.getCentreX(), r.getY(), false));
            g.fillRoundedRectangle (r, 4.5f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (pressed ? 0.06f : 0.16f), r.getX(), r.getY(),
                                                 juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.5f, false));
        g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
        g.setColour (juce::Colours::white.withAlpha (pressed ? 0.08f : 0.22f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 5.0f, r.getRight() - 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.9f));
        g.drawRoundedRectangle (r, 4.5f, 1.0f);
    }

    inline void screw (juce::Graphics& g, juce::Point<float> c, float radius)
    {
        const auto head = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillEllipse (head.expanded (1.5f).translated (0.0f, 1.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a918d), c.translated (-radius * 0.5f, -radius * 0.6f),
                                                 juce::Colour (0xff222624), c.translated (radius * 0.6f, radius), true));
        g.fillEllipse (head);
        juce::Path hex;
        hex.addPolygon (c, 6, radius * 0.48f, 0.3f);
        g.setColour (juce::Colour (0xff101211));
        g.fillPath (hex);
    }

    // Glossy black dome: the body of every knob. Lit from the top left with a hot specular spot.
    inline void dome (juce::Graphics& g, juce::Point<float> c, float radius, float shine = 1.0f)
    {
        const auto box = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
        juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.8f), c.translated (radius * 0.15f, radius * 0.3f),
                                     juce::Colours::transparentBlack, c.translated (radius * 1.55f, radius * 0.3f), true);
        g.setGradientFill (shadow);
        g.fillEllipse (box.expanded (radius * 0.55f).translated (radius * 0.15f, radius * 0.3f));

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a403c), c.translated (-radius * 0.4f, -radius * 0.55f),
                                                 juce::Colour (0xff030403), c.translated (radius * 0.5f, radius * 0.9f), true));
        g.fillEllipse (box);
        // Rim light on the far edge, then a specular hotspot.
        juce::Path rim;
        rim.addCentredArc (c.x, c.y, radius - 1.0f, radius - 1.0f, 0.0f, 1.9f, 3.6f, true);
        g.setColour (juce::Colours::white.withAlpha (0.10f * shine));
        g.strokePath (rim, juce::PathStrokeType (1.5f));
        const auto spot = c.translated (-radius * 0.35f, -radius * 0.42f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.55f * shine), spot, juce::Colours::white.withAlpha (0.0f),
                                                 spot.translated (radius * 0.55f, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (radius * 1.1f, radius * 0.8f).withCentre (spot));
        g.setColour (juce::Colours::black);
        g.drawEllipse (box, 1.0f);
    }
}
