#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    // Colour roles keep the shared widgets' names: lime = main accent, cyan = second, magenta = hot/mod, amber = fourth.
    // They are variables so a skin can swap them; set the skin before an editor is built (widgets copy accents).
    // Defaults are the BLACK BOX skin (the shipping look); applySkin() swaps them.
    inline juce::Colour background { 0xff0a0b0c };
    inline juce::Colour panel      { 0xff111214 };
    inline juce::Colour panelHi    { 0xff1b1d1f };
    inline juce::Colour edge       { 0xff2b2e31 };
    inline juce::Colour cyan       { 0xff7af0c8 };
    inline juce::Colour magenta    { 0xffff4a3a };
    inline juce::Colour lime       { 0xff35d97f };
    inline juce::Colour amber      { 0xffe6e6e6 };
    inline juce::Colour red        { 0xffff4b4b };
    inline juce::Colour text       { 0xfff2f2f0 };
    inline juce::Colour dim        { 0xff8b9095 };

    enum Skin { neonViolet, chromeIce, sunsetGlass, acidLab, ironMachine, blackBox, numSkins };
    inline int skin = blackBox;
    inline const char* skinName (int s) { static const char* n[] { "NEON VIOLET", "CHROME ICE", "SUNSET GLASS", "ACID LAB", "IRON MACHINE", "BLACK BOX" }; return n[juce::jlimit (0, numSkins - 1, s)]; }
    inline float glowAmount() { return skin == blackBox ? 0.45f : skin == ironMachine ? 0.25f : skin == chromeIce ? 0.35f : skin == acidLab ? 0.7f : 1.0f; }
    inline float corner()     { return skin == blackBox ? 2.0f : skin == ironMachine ? 3.0f : skin == chromeIce ? 6.0f : skin == sunsetGlass ? 10.0f : 0.0f; }

    inline void applySkin (int s)
    {
        skin = juce::jlimit (0, numSkins - 1, s);
        struct P { juce::uint32 bg, pa, hi, ed, cy, ma, li, am, tx, di; };
        static const P palettes[] {
            { 0xff07040f, 0xff0e0920, 0xff1a1236, 0xff2e2358, 0xff3ff5d0, 0xffff3d9a, 0xffffc93c, 0xffa77bff, 0xfff1e9ff, 0xff65588f }, // royal violet, gold, pink, aqua
            { 0xff0a0c0f, 0xff14171c, 0xff242931, 0xff3b424d, 0xff5ad1ff, 0xffff6a3d, 0xffdfe7ef, 0xff8fa6bd, 0xffeef3f8, 0xff5f6976 }, // brushed steel, ice blue, ember
            { 0xff10031a, 0xff1c0a2b, 0xff2d1244, 0xff5b2b78, 0xff2de2e6, 0xffff2a6d, 0xffff9f43, 0xffc47dff, 0xfffff0f7, 0xff86609c }, // dusk purple, sunset orange, neon pink
            { 0xff040705, 0xff09110a, 0xff112015, 0xff1f3d26, 0xff00e5ff, 0xffff4fd8, 0xffb6ff1a, 0xff3dffa0, 0xffe8ffe6, 0xff4f7d58 }, // lab black, toxic lime, cyan
            { 0xff16181b, 0xff141619, 0xff24272b, 0xff0d0e10, 0xff59e3a2, 0xffff4b2e, 0xffffa62b, 0xffd9dee4, 0xfff4f1ea, 0xff8f969f }, // gunmetal machine, amber lamps, red, green LED
            { 0xff0a0b0c, 0xff111214, 0xff1b1d1f, 0xff2b2e31, 0xff7af0c8, 0xffff4a3a, 0xff35d97f, 0xffe6e6e6, 0xfff2f2f0, 0xff8b9095 }, // matte black hardware, silkscreen white, green
        };
        const auto& p = palettes[skin];
        background = juce::Colour (p.bg); panel = juce::Colour (p.pa); panelHi = juce::Colour (p.hi); edge = juce::Colour (p.ed);
        cyan = juce::Colour (p.cy); magenta = juce::Colour (p.ma); lime = juce::Colour (p.li); amber = juce::Colour (p.am);
        text = juce::Colour (p.tx); dim = juce::Colour (p.di);
    }

    inline juce::Font mono (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font sans (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Neon look: a few wide translucent passes under a bright core.
    inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity = 1.0f)
    {
        const float extra[] { 10.0f, 5.0f, 2.0f };
        const float alpha[] { 0.07f, 0.13f, 0.28f };
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (c.withAlpha (alpha[i] * intensity * glowAmount()));
            g.strokePath (p, juce::PathStrokeType (width + extra[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c.brighter (0.35f).withAlpha (juce::jmin (1.0f, intensity)));
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    inline void glowDot (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour c, float intensity = 1.0f)
    {
        juce::ColourGradient halo (c.withAlpha (0.5f * intensity), centre, c.withAlpha (0.0f), centre.translated (radius * 4.0f, 0.0f), true);
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (radius * 8.0f, radius * 8.0f).withCentre (centre));
        g.setColour (c.brighter (0.7f).withAlpha (intensity));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }

    // Rectangle with the top-right and bottom-left corners cut off.
    inline juce::Path chamfered (juce::Rectangle<float> r, float cut)
    {
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getY());
        p.lineTo (r.getRight() - cut, r.getY());
        p.lineTo (r.getRight(), r.getY() + cut);
        p.lineTo (r.getRight(), r.getBottom());
        p.lineTo (r.getX() + cut, r.getBottom());
        p.lineTo (r.getX(), r.getBottom() - cut);
        p.closeSubPath();
        return p;
    }

    inline void cornerBrackets (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float len = 10.0f)
    {
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getY() + len);           p.lineTo (r.getX(), r.getY());           p.lineTo (r.getX() + len, r.getY());
        p.startNewSubPath (r.getRight() - len, r.getY());       p.lineTo (r.getRight(), r.getY());       p.lineTo (r.getRight(), r.getY() + len);
        p.startNewSubPath (r.getRight(), r.getBottom() - len);  p.lineTo (r.getRight(), r.getBottom());  p.lineTo (r.getRight() - len, r.getBottom());
        p.startNewSubPath (r.getX() + len, r.getBottom());      p.lineTo (r.getX(), r.getBottom());      p.lineTo (r.getX(), r.getBottom() - len);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    // Module frame with an indexed title strip, e.g. "01 // FILTER CORE".
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& index, const juce::String& title, juce::Colour accent)
    {
        if (skin == blackBox)
        {
            // Hardware silkscreen: the panel is the black case itself; a section is a printed title with rules either side.
            g.setColour (juce::Colour (0xff101113));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            const auto head = r.withHeight (26.0f);
            g.setFont (sans (13.0f, true));
            const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 24.0f;
            const auto titleBox = head.withSizeKeepingCentre (tw, head.getHeight());
            g.setColour (accent.withAlpha (0.85f));
            g.drawLine (r.getX() + 12.0f, head.getCentreY(), titleBox.getX(), head.getCentreY(), 1.2f);
            g.drawLine (titleBox.getRight(), head.getCentreY(), r.getRight() - 12.0f, head.getCentreY(), 1.2f);
            g.drawLine (r.getX() + 12.0f, head.getCentreY(), r.getX() + 12.0f, head.getCentreY() + 6.0f, 1.2f);
            g.drawLine (r.getRight() - 12.0f, head.getCentreY(), r.getRight() - 12.0f, head.getCentreY() + 6.0f, 1.2f);
            g.setColour (text);
            g.drawText (title, titleBox, juce::Justification::centred);
            g.setFont (mono (9.0f, true));
            g.setColour (dim);
            g.drawText (index, head.withTrimmedRight (16.0f).translated (0.0f, -9.0f), juce::Justification::centredRight);
            return;
        }
        if (skin == ironMachine)
        {
            // Bolted steel plate: soft drop shadow, bevelled edge, brushed face, four screws, engraved title, amber pilot lamp.
            for (int i = 4; i > 0; --i)
            {
                g.setColour (juce::Colours::black.withAlpha (0.10f));
                g.fillRoundedRectangle (r.translated ((float) i * 0.8f, (float) i * 1.4f).expanded ((float) i * 0.6f), 5.0f);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5d636b), r.getTopLeft(), juce::Colour (0xff3a3e44), r.getBottomRight(), false));
            g.fillRoundedRectangle (r, 4.0f);
            juce::Random grain ((juce::int64) (r.getX() * 13.0f + r.getY()));
            for (float y = r.getY() + 3.0f; y < r.getBottom() - 3.0f; y += 1.5f)
            {
                g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.02f + grain.nextFloat() * 0.035f));
                g.drawHorizontalLine ((int) y, r.getX() + 3.0f, r.getRight() - 3.0f);
            }
            // Bevel: light top/left, dark bottom/right.
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawLine (r.getX() + 4.0f, r.getY() + 1.0f, r.getRight() - 4.0f, r.getY() + 1.0f, 1.2f);
            g.drawLine (r.getX() + 1.0f, r.getY() + 4.0f, r.getX() + 1.0f, r.getBottom() - 4.0f, 0.8f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawLine (r.getX() + 4.0f, r.getBottom() - 0.8f, r.getRight() - 4.0f, r.getBottom() - 0.8f, 1.4f);
            g.drawLine (r.getRight() - 0.8f, r.getY() + 4.0f, r.getRight() - 0.8f, r.getBottom() - 4.0f, 1.2f);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.drawRoundedRectangle (r.expanded (0.5f), 4.5f, 1.0f);
            // Engraved title groove.
            const auto head = r.withHeight (28.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawHorizontalLine ((int) head.getBottom(), r.getX() + 8.0f, r.getRight() - 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.drawHorizontalLine ((int) head.getBottom() + 1, r.getX() + 8.0f, r.getRight() - 8.0f);
            // Screws.
            for (auto pt : { juce::Point<float> (r.getX() + 9.0f, r.getY() + 9.0f), juce::Point<float> (r.getRight() - 9.0f, r.getY() + 9.0f),
                             juce::Point<float> (r.getX() + 9.0f, r.getBottom() - 9.0f), juce::Point<float> (r.getRight() - 9.0f, r.getBottom() - 9.0f) })
            {
                const auto sc = juce::Rectangle<float> (9.0f, 9.0f).withCentre (pt);
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillEllipse (sc.expanded (1.0f).translated (0.5f, 0.8f));
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6d68a), sc.getTopLeft(), juce::Colour (0xff6b4316), sc.getBottomRight(), false));
                g.fillEllipse (sc);
                const float a = (pt.x * 0.37f + pt.y * 0.21f);
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.drawLine ({ pt.translated (std::cos (a) * 3.2f, std::sin (a) * 3.2f), pt.translated (-std::cos (a) * 3.2f, -std::sin (a) * 3.2f) }, 1.3f);
                g.drawLine ({ pt.translated (-std::sin (a) * 3.2f, std::cos (a) * 3.2f), pt.translated (std::sin (a) * 3.2f, -std::cos (a) * 3.2f) }, 1.3f);
            }
            // Pilot lamp in a chrome bezel.
            const juce::Point<float> lamp (r.getX() + 28.0f, head.getCentreY());
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6d68a), lamp.translated (-7.0f, -7.0f), juce::Colour (0xff5a3810), lamp.translated (7.0f, 7.0f), false));
            g.fillEllipse (juce::Rectangle<float> (15.0f, 15.0f).withCentre (lamp));
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (lamp));
            glowDot (g, lamp, 3.2f, accent, 0.9f);
            g.setFont (sans (13.0f, true));
            const auto titleArea = head.withTrimmedLeft (40.0f);
            g.setColour (juce::Colours::black.withAlpha (0.65f)); // engraved: dark cut with a light lower lip
            g.drawText (title, titleArea.translated (0.0f, -1.0f), juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xffeae6dc));
            g.drawText (title, titleArea, juce::Justification::centredLeft);
            g.setFont (mono (10.0f, true));
            g.setColour (juce::Colour (0xffc7ccd2));
            g.drawText (index, head.withTrimmedRight (22.0f), juce::Justification::centredRight);
            return;
        }
        if (skin == chromeIce)
        {
            // Machined aluminium module: rounded, top bevel light, dark recessed body, accent LED in the title bar.
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 8.0f);
            g.setGradientFill (juce::ColourGradient (panelHi.brighter (0.15f), r.getTopLeft(), panel.darker (0.2f), r.getBottomLeft(), false));
            g.fillRoundedRectangle (r, 8.0f);
            const auto head = r.withHeight (30.0f);
            juce::Path top;
            top.addRoundedRectangle (head.getX(), head.getY(), head.getWidth(), head.getHeight(), 8.0f, 8.0f, true, true, false, false);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a414c), head.getTopLeft(), juce::Colour (0xff22272e), head.getBottomLeft(), false));
            g.fillPath (top);
            g.setColour (juce::Colours::white.withAlpha (0.025f)); // brushed lines
            for (float x = head.getX() + 2.0f; x < head.getRight(); x += 3.0f)
                g.drawVerticalLine ((int) x, head.getY() + 1.0f, head.getBottom() - 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 8.0f, r.getRight() - 8.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawHorizontalLine ((int) head.getBottom(), r.getX(), r.getRight());
            g.setColour (edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            glowDot (g, { r.getX() + 14.0f, head.getCentreY() }, 2.5f, accent);
            g.setFont (sans (13.0f, true));
            g.setColour (text);
            g.drawText (title, head.withTrimmedLeft (26.0f), juce::Justification::centredLeft);
            g.setFont (mono (10.0f, true));
            g.setColour (dim.brighter (0.4f));
            g.drawText (index, head.withTrimmedRight (12.0f), juce::Justification::centredRight);
            return;
        }
        if (skin == sunsetGlass)
        {
            // Frosted glass card: translucent body, soft top light, a sunset gradient ribbon under the title.
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r.translated (0.0f, 4.0f).expanded (1.0f), 14.0f);
            g.setGradientFill (juce::ColourGradient (panelHi.withAlpha (0.82f), r.getTopLeft(), panel.withAlpha (0.9f), r.getBottomRight(), false));
            g.fillRoundedRectangle (r, 12.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + 60.0f, false));
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
            juce::Path ribbon;
            ribbon.addRoundedRectangle (r.getX() + 14.0f, r.getY() + 26.0f, 110.0f, 3.0f, 1.5f);
            g.setGradientFill (juce::ColourGradient (accent, r.getX() + 14.0f, 0.0f, magenta, r.getX() + 124.0f, 0.0f, false));
            g.fillPath (ribbon);
            g.setFont (sans (13.0f, true));
            g.setColour (text);
            g.drawText (title, r.withHeight (26.0f).withTrimmedLeft (14.0f), juce::Justification::centredLeft);
            g.setFont (mono (10.0f, true));
            g.setColour (accent.withAlpha (0.8f));
            g.drawText (index, r.withHeight (26.0f).withTrimmedRight (14.0f), juce::Justification::centredRight);
            return;
        }
        if (skin == acidLab)
        {
            // Lab instrument: square frame, solid accent title tab with black text, grid floor, corner brackets.
            g.setColour (panel);
            g.fillRect (r);
            g.setColour (edge.withAlpha (0.35f));
            for (float x = r.getX() + 12.0f; x < r.getRight(); x += 12.0f) g.drawVerticalLine ((int) x, r.getY() + 30.0f, r.getBottom());
            for (float y = r.getY() + 42.0f; y < r.getBottom(); y += 12.0f) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (edge.brighter (0.3f));
            g.drawRect (r, 1.0f);
            const auto head = r.withHeight (24.0f);
            const auto tab = head.withWidth (juce::jmin (head.getWidth() - 40.0f, 40.0f + (float) title.length() * 8.2f));
            g.setColour (accent);
            g.fillRect (tab);
            g.setColour (background);
            g.setFont (mono (12.0f, true));
            g.drawText (index + " " + title, tab.withTrimmedLeft (8.0f), juce::Justification::centredLeft);
            g.setColour (accent.withAlpha (0.5f));
            g.drawHorizontalLine ((int) head.getBottom(), tab.getRight(), r.getRight());
            cornerBrackets (g, r.expanded (3.0f), accent.withAlpha (0.9f), 12.0f);
            return;
        }

        const auto shape = chamfered (r, 14.0f);
        g.setGradientFill (juce::ColourGradient (panelHi, r.getTopLeft(), panel, r.getBottomLeft(), false));
        g.fillPath (shape);

        // Fine scanlines give the panels a CRT texture.
        g.setColour (juce::Colours::white.withAlpha (0.012f));
        for (float y = r.getY() + 2.0f; y < r.getBottom(); y += 3.0f)
            g.drawHorizontalLine ((int) y, r.getX() + 1.0f, r.getRight() - 1.0f);

        g.setColour (edge);
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        const auto head = r.withHeight (30.0f);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.16f), head.getTopLeft(), accent.withAlpha (0.0f), head.getTopRight(), false));
        g.fillRect (head.reduced (1.0f).withTrimmedRight (16.0f));

        juce::Path underline;
        underline.startNewSubPath (r.getX() + 1.0f, head.getBottom());
        underline.lineTo (r.getX() + 90.0f, head.getBottom());
        glowStroke (g, underline, accent, 1.2f, 0.8f);
        g.setColour (edge.brighter (0.2f));
        g.drawHorizontalLine ((int) head.getBottom(), r.getX() + 90.0f, r.getRight() - 1.0f);

        g.setFont (mono (12.0f, true));
        g.setColour (accent);
        g.drawText (index + " //", head.withTrimmedLeft (12.0f), juce::Justification::centredLeft);
        g.setColour (text);
        g.drawText (title, head.withTrimmedLeft (52.0f), juce::Justification::centredLeft);

        for (int i = 0; i < 4; ++i)
        {
            g.setColour (accent.withAlpha (0.2f + 0.2f * (float) i));
            g.fillRect (juce::Rectangle<float> (r.getRight() - 64.0f + (float) i * 9.0f, head.getCentreY() - 3.0f, 5.0f, 6.0f));
        }

        g.setColour (accent.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), 3.0f, 30.0f));
    }
}
