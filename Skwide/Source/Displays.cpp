#include "Displays.h"
#include "BinaryData.h"

namespace
{
    constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
    constexpr float minDb = -84.0f, maxDb = 0.0f;

    // 0% mono, 100% uncorrelated (side as loud as mid), 200% fully out of phase.
    float widthPercent (float midPower, float sidePower)
    {
        if (midPower + sidePower < 1.0e-12f)
            return 0.0f;
        return std::atan (std::sqrt (sidePower / juce::jmax (midPower, 1.0e-20f))) / juce::MathConstants<float>::pi * 400.0f;
    }

    juce::Colour widthColour (float percent)
    {
        const float t = juce::jlimit (0.0f, 1.0f, percent / 200.0f);
        if (t < 0.5f)
            return Theme::cyan.interpolatedWith (Theme::magenta, t * 2.0f);
        return Theme::magenta.interpolatedWith (Theme::red, (t - 0.5f) * 2.0f);
    }

    juce::Point<float> polar (juce::Point<float> centre, float radius, float angle)
    {
        return centre.translated (radius * std::sin (angle), -radius * std::cos (angle));
    }
}

//==============================================================================
HeaderView::HeaderView()
    : logo (juce::ImageCache::getFromMemory (BinaryData::skazi_logo_png, BinaryData::skazi_logo_pngSize).createCopy())
{
    // The logo ships on solid black; turn brightness into alpha so it floats on the header.
    logo = logo.convertedToFormat (juce::Image::ARGB);
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

    setInterceptsMouseClicks (false, false);
}

void HeaderView::tick()
{
    if (glitchFrames > 0)
        --glitchFrames;
    else if (random.nextFloat() < 0.008f)
        glitchFrames = 4 + random.nextInt (8);
    glitchOffset = (random.nextFloat() - 0.5f) * 8.0f;
    repaint();
}

void HeaderView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (Theme::panelHi, 0.0f, 0.0f, Theme::background, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    const auto line = bounds.withTop (bounds.getBottom() - 2.0f);
    g.setGradientFill (juce::ColourGradient (Theme::cyan, line.getX(), 0.0f, Theme::magenta, line.getRight(), 0.0f, false));
    g.fillRect (line);
    g.setGradientFill (juce::ColourGradient (Theme::cyan.withAlpha (0.25f), line.getX(), 0.0f, Theme::magenta.withAlpha (0.25f), line.getRight(), 0.0f, false));
    g.fillRect (line.expanded (0.0f, 3.0f).translated (0.0f, -3.0f));

    const auto logoBox = juce::Rectangle<float> (14.0f, 5.0f, 100.0f, 54.0f);
    if (glitchFrames > 0)
    {
        g.setOpacity (0.55f);
        g.drawImage (logo, logoBox.translated (glitchOffset, 0.0f), juce::RectanglePlacement::centred);
        g.drawImage (logo, logoBox.translated (-glitchOffset, 1.0f), juce::RectanglePlacement::centred);
    }
    g.setOpacity (1.0f);
    g.drawImage (logo, logoBox, juce::RectanglePlacement::centred);

    // The title is drawn twice, split left and right, as if it were itself being widened.
    const juce::String title ("SKWIDE");
    const auto titleArea = juce::Rectangle<float> (124.0f, 4.0f, 230.0f, 40.0f);
    g.setFont (Theme::sans (34.0f, true).withExtraKerningFactor (0.16f));
    const float split = glitchFrames > 0 ? std::abs (glitchOffset) + 2.0f : 1.6f;
    g.setColour (Theme::cyan.withAlpha (0.55f));
    g.drawText (title, titleArea.translated (-split, 0.0f), juce::Justification::centredLeft);
    g.setColour (Theme::magenta.withAlpha (0.55f));
    g.drawText (title, titleArea.translated (split, 0.0f), juce::Justification::centredLeft);
    g.setGradientFill (juce::ColourGradient (Theme::text, titleArea.getX(), 0.0f, Theme::cyan, titleArea.getX() + 200.0f, 0.0f, false));
    g.drawText (title, titleArea, juce::Justification::centredLeft);

    if (glitchFrames > 0)
        for (int i = 0; i < 3; ++i)
        {
            g.setColour ((random.nextBool() ? Theme::magenta : Theme::background).withAlpha (0.9f));
            g.fillRect (logoBox.getX() + random.nextFloat() * 300.0f, titleArea.getY() + random.nextFloat() * 36.0f, 30.0f + random.nextFloat() * 90.0f, 2.0f);
        }

    g.setFont (Theme::mono (10.0f, true));
    g.setColour (Theme::dim.brighter (0.4f));
    g.drawText ("PHASE-SAFE STEREO IMAGER", juce::Rectangle<float> (126.0f, 44.0f, 230.0f, 14.0f), juce::Justification::centredLeft);

    const auto credit = bounds.withTrimmedLeft (bounds.getWidth() - 278.0f).reduced (14.0f, 6.0f);
    const auto nameArea = credit.withHeight (32.0f);
    g.setFont (Theme::sans (25.0f, true).withExtraKerningFactor (0.08f));
    g.setColour (Theme::magenta.withAlpha (0.22f));
    for (auto offset : { juce::Point<float> (-1.5f, 0.0f), juce::Point<float> (1.5f, 0.0f), juce::Point<float> (0.0f, 1.5f) })
        g.drawText ("ASHER SWISSA", nameArea.translated (offset.x, offset.y), juce::Justification::centredRight);
    g.setGradientFill (juce::ColourGradient (Theme::magenta, nameArea.getRight() - 210.0f, 0.0f, Theme::cyan, nameArea.getRight(), 0.0f, false));
    g.drawText ("ASHER SWISSA", nameArea, juce::Justification::centredRight);

    g.setFont (Theme::mono (10.0f, true));
    g.setColour (Theme::lime);
    g.drawText ("MONO-SAFE ALLPASS ENGINE // ONLINE", credit.withTop (credit.getY() + 34.0f).withHeight (14.0f), juce::Justification::centredRight);
}

//==============================================================================
ImagerView::ImagerView (SkwideAudioProcessor& p)
    : proc (p),
      lowFreqAttachment (*p.apvts.getParameter (ParamIDs::lowFreq), [this] (float v) { lowFreq = v; repaint(); })
{
    bufL.resize (fftSize);
    bufR.resize (fftSize);
    fftMid.resize (fftSize * 2);
    fftSide.resize (fftSize * 2);
    for (auto* v : { &outMid, &outSide, &inMid, &inSide })
        v->assign (numBins, 0.0f);
    lowFreqAttachment.sendInitialUpdate();
}

juce::Rectangle<float> ImagerView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (66.0f).withTrimmedLeft (30.0f).withTrimmedRight (4.0f).withTrimmedTop (4.0f);
}

juce::Rectangle<float> ImagerView::stripArea() const
{
    const auto plot = plotArea();
    return { plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 42.0f };
}

float ImagerView::xForFreq (float freq) const
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log (freq / minFreq) / std::log (maxFreq / minFreq);
}

float ImagerView::freqForX (float x) const
{
    const auto r = plotArea();
    return minFreq * std::pow (maxFreq / minFreq, (x - r.getX()) / r.getWidth());
}

float ImagerView::yForDb (float db) const
{
    const auto r = plotArea();
    return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, r.getBottom(), r.getY());
}

void ImagerView::analyse (const SampleRing& left, const SampleRing& right, std::vector<float>& mid, std::vector<float>& side)
{
    left.copyLatest (bufL.data(), fftSize);
    right.copyLatest (bufR.data(), fftSize);

    for (int i = 0; i < fftSize; ++i)
    {
        fftMid[(size_t) i] = 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]);
        fftSide[(size_t) i] = 0.5f * (bufL[(size_t) i] - bufR[(size_t) i]);
    }
    window.multiplyWithWindowingTable (fftMid.data(), (size_t) fftSize);
    window.multiplyWithWindowingTable (fftSide.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftMid.data());
    fft.performFrequencyOnlyForwardTransform (fftSide.data());

    for (int b = 0; b < numBins; ++b)
    {
        const float m = fftMid[(size_t) b] * fftMid[(size_t) b];
        const float s = fftSide[(size_t) b] * fftSide[(size_t) b];
        mid[(size_t) b] = m > mid[(size_t) b] ? m : mid[(size_t) b] * 0.72f + m * 0.28f;
        side[(size_t) b] = s > side[(size_t) b] ? s : side[(size_t) b] * 0.72f + s * 0.28f;
    }
}

void ImagerView::tick()
{
    lowOn = proc.apvts.getRawParameterValue (ParamIDs::lowOn)->load() > 0.5f;
    monoBass = proc.apvts.getRawParameterValue (ParamIDs::monoBass)->load() > 0.5f;

    if (++frame % 2 != 0)
        return;

    analyse (proc.outL, proc.outR, outMid, outSide);
    analyse (proc.inL, proc.inR, inMid, inSide);

    const float binHz = (float) proc.liveSampleRate.load() / (float) fftSize;
    for (int band = 0; band < numBands; ++band)
    {
        const float f0 = minFreq * std::pow (maxFreq / minFreq, (float) band / numBands);
        const float f1 = minFreq * std::pow (maxFreq / minFreq, (float) (band + 1) / numBands);
        const int b0 = juce::jlimit (1, numBins - 1, (int) (f0 / binHz));
        const int b1 = juce::jlimit (b0 + 1, numBins, (int) std::ceil (f1 / binHz));
        float om = 0, os = 0, im = 0, is = 0;
        for (int b = b0; b < b1; ++b)
        {
            om += outMid[(size_t) b]; os += outSide[(size_t) b];
            im += inMid[(size_t) b];  is += inSide[(size_t) b];
        }
        const float scale = 1.0f / (float) (fftSize * fftSize);
        bandWidth[(size_t) band] += (widthPercent (om * scale, os * scale) - bandWidth[(size_t) band]) * 0.35f;
        bandWidthIn[(size_t) band] += (widthPercent (im * scale, is * scale) - bandWidthIn[(size_t) band]) * 0.35f;
    }
    repaint();
}

void ImagerView::paint (juce::Graphics& g)
{
    const auto plot = plotArea();
    const float binHz = (float) proc.liveSampleRate.load() / (float) fftSize;
    const float norm = 20.0f * std::log10 ((float) fftSize / 4.0f);

    g.setColour (Theme::background.withAlpha (0.75f));
    g.fillRect (plot);

    // Grid.
    g.setFont (Theme::mono (9.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (Theme::edge.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (Theme::dim);
        g.drawText (f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
    for (float db = -72.0f; db <= -12.0f; db += 12.0f)
    {
        const float y = yForDb (db);
        g.setColour (Theme::edge.withAlpha (0.45f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (Theme::dim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
    }

    // Low-end zone.
    const float lineX = xForFreq (lowFreq);
    if (lowOn)
    {
        const auto zone = plot.withRight (lineX);
        g.setGradientFill (juce::ColourGradient (Theme::amber.withAlpha (0.16f), zone.getX(), 0.0f, Theme::amber.withAlpha (0.03f), zone.getRight(), 0.0f, false));
        g.fillRect (zone);
        g.setColour (Theme::amber.withAlpha (0.08f));
        for (float y = zone.getY() - zone.getWidth(); y < zone.getBottom(); y += 9.0f)
            g.drawLine (zone.getX(), y + zone.getWidth(), zone.getRight(), y, 1.0f);
    }

    // Spectrum paths, tilted +4.5 dB/oct around 1 kHz so they read like a mix analyser.
    auto spectrumPath = [&] (const std::vector<float>& power, bool closed)
    {
        juce::Path p;
        bool started = false;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const float fa = freqForX (x), fb = freqForX (x + 2.0f);
            const int b0 = juce::jlimit (1, numBins - 1, (int) (fa / binHz));
            const int b1 = juce::jlimit (b0, numBins - 1, (int) (fb / binHz));
            float peak = 0.0f;
            for (int b = b0; b <= b1; ++b)
                peak = juce::jmax (peak, power[(size_t) b]);
            const float db = 10.0f * std::log10 (peak + 1.0e-20f) - norm + 4.5f * std::log2 (fa / 1000.0f);
            const float y = yForDb (db);
            if (! started)
            {
                if (closed)
                {
                    p.startNewSubPath (x, plot.getBottom());
                    p.lineTo (x, y);
                }
                else
                    p.startNewSubPath (x, y);
                started = true;
            }
            else
                p.lineTo (x, y);
        }
        if (closed)
        {
            p.lineTo (plot.getRight(), plot.getBottom());
            p.closeSubPath();
        }
        return p;
    };

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());

        const auto midFill = spectrumPath (outMid, true);
        g.setGradientFill (juce::ColourGradient (Theme::cyan.withAlpha (0.32f), 0.0f, plot.getY(), Theme::cyan.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
        g.fillPath (midFill);
        Theme::glowStroke (g, spectrumPath (outMid, false), Theme::cyan, 1.3f, 0.7f);

        const auto inSidePath = spectrumPath (inSide, false);
        juce::Path dashed;
        const float dashes[] { 3.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, inSidePath, dashes, 2);
        g.setColour (Theme::text.withAlpha (0.35f));
        g.fillPath (dashed);

        const auto sideFill = spectrumPath (outSide, true);
        g.setGradientFill (juce::ColourGradient (Theme::magenta.withAlpha (0.26f), 0.0f, plot.getY(), Theme::magenta.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (sideFill);
        Theme::glowStroke (g, spectrumPath (outSide, false), Theme::magenta, 1.5f, 0.9f);
    }

    // Crossover line with a handle.
    {
        juce::Path l;
        l.startNewSubPath (lineX, plot.getY());
        l.lineTo (lineX, plot.getBottom());
        Theme::glowStroke (g, l, lowOn ? Theme::amber : Theme::dim, hoverLine || dragging ? 2.0f : 1.3f, lowOn ? 1.0f : 0.5f);
        const auto handle = juce::Rectangle<float> (70.0f, 30.0f).withCentre ({ juce::jlimit (plot.getX() + 36.0f, plot.getRight() - 36.0f, lineX), plot.getY() + 20.0f });
        g.setColour (Theme::background.withAlpha (0.9f));
        g.fillRect (handle);
        g.setColour (lowOn ? Theme::amber : Theme::dim);
        g.drawRect (handle, 1.0f);
        g.setFont (Theme::mono (9.0f, true));
        g.drawText (! lowOn ? "LOW OFF" : (monoBass ? "MONO BASS" : "LOW BYPASS"), handle.withHeight (15.0f), juce::Justification::centred);
        g.setFont (Theme::mono (11.0f, true));
        g.drawText (juce::String (juce::roundToInt (lowFreq)) + " Hz", handle.withTrimmedTop (14.0f), juce::Justification::centred);
    }

    // Legend.
    {
        auto legend = juce::Rectangle<float> (plot.getRight() - 236.0f, plot.getY() + 6.0f, 230.0f, 14.0f);
        g.setFont (Theme::mono (9.5f, true));
        const std::pair<const char*, juce::Colour> items[] { { "MID", Theme::cyan }, { "SIDE", Theme::magenta }, { "SIDE IN", Theme::text.withAlpha (0.5f) } };
        for (const auto& [name, colour] : items)
        {
            auto cell = legend.removeFromLeft (76.0f);
            g.setColour (colour);
            g.fillRect (cell.removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, 3.0f));
            g.drawText (name, cell.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
        }
    }

    // Per-band width strip.
    const auto strip = stripArea();
    g.setColour (Theme::background.withAlpha (0.8f));
    g.fillRect (strip);
    g.setColour (Theme::edge);
    g.drawRect (strip, 1.0f);
    g.setFont (Theme::mono (8.5f, true));
    g.setColour (Theme::dim);

    const float cellW = strip.getWidth() / numBands;
    const float midLine = strip.getBottom() - strip.getHeight() * 0.5f;
    g.setColour (Theme::dim.withAlpha (0.4f));
    g.drawHorizontalLine ((int) midLine, strip.getX(), strip.getRight());
    for (int band = 0; band < numBands; ++band)
    {
        const float w = juce::jlimit (0.0f, 200.0f, bandWidth[(size_t) band]);
        const auto cell = juce::Rectangle<float> (strip.getX() + cellW * (float) band + 1.5f, strip.getY() + 2.0f, cellW - 3.0f, strip.getHeight() - 4.0f);
        const float h = cell.getHeight() * w / 200.0f;
        const auto bar = cell.withTop (cell.getBottom() - h);
        const auto c = widthColour (w);
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.95f), 0.0f, bar.getY(), c.withAlpha (0.25f), 0.0f, bar.getBottom(), false));
        g.fillRect (bar);
        g.setColour (c.brighter (0.5f));
        g.fillRect (bar.withHeight (1.5f));

        const float inY = cell.getBottom() - cell.getHeight() * juce::jlimit (0.0f, 200.0f, bandWidthIn[(size_t) band]) / 200.0f;
        g.setColour (Theme::text.withAlpha (0.65f));
        g.fillRect (cell.getX(), inY, cell.getWidth(), 1.0f);
    }
    g.setColour (Theme::dim.brighter (0.3f));
    g.setFont (Theme::mono (8.5f, true));
    g.drawText ("WIDTH PER BAND  //  BAR = OUT, LINE = IN", strip.reduced (6.0f, 3.0f), juce::Justification::topLeft);
    g.setColour (Theme::dim);
    g.setFont (Theme::mono (8.0f));
    g.drawText ("200", strip.withHeight (10.0f).translated (-26.0f, 0.0f).withWidth (24.0f), juce::Justification::centredRight);
    g.drawText ("100", juce::Rectangle<float> (strip.getX() - 26.0f, midLine - 5.0f, 24.0f, 10.0f), juce::Justification::centredRight);
}

void ImagerView::mouseMove (const juce::MouseEvent& e)
{
    const bool near = std::abs (e.position.x - xForFreq (lowFreq)) < 10.0f && plotArea().contains (e.position);
    if (near != hoverLine)
    {
        hoverLine = near;
        setMouseCursor (near ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ImagerView::mouseDown (const juce::MouseEvent& e)
{
    if (! plotArea().contains (e.position))
        return;
    dragging = true;
    lowFreqAttachment.beginGesture();
    mouseDrag (e);
}

void ImagerView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        lowFreqAttachment.setValueAsPartOfGesture (juce::jlimit (20.0f, 500.0f, freqForX (e.position.x)));
}

void ImagerView::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        lowFreqAttachment.endGesture();
    dragging = false;
}

//==============================================================================
ScopeView::ScopeView (SkwideAudioProcessor& p) : proc (p)
{
    for (auto* v : { &l, &r, &il, &ir })
        v->resize (2048);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ScopeView::scopeArea() const
{
    const auto b = getLocalBounds().toFloat().withTrimmedBottom (78.0f);
    const float size = juce::jmin (b.getWidth(), b.getHeight());
    return b.withSizeKeepingCentre (size, size);
}

void ScopeView::resized()
{
    const auto area = scopeArea();
    // Software image: it is redrawn 60 times a second, and on a Direct2D (native) image every
    // redraw ends in a blocking GPU readback that stalls the host's UI thread.
    trail = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) area.getWidth() * 2), juce::jmax (1, (int) area.getHeight() * 2), true,
                         juce::SoftwareImageType());
}

void ScopeView::mouseDown (const juce::MouseEvent& e)
{
    if (scopeArea().contains (e.position))
    {
        polarMode = ! polarMode;
        trail.clear (trail.getBounds());
        repaint();
    }
}

void ScopeView::tick()
{
    const int n = (int) l.size();
    proc.outL.copyLatest (l.data(), n);
    proc.outR.copyLatest (r.data(), n);
    proc.inL.copyLatest (il.data(), n);
    proc.inR.copyLatest (ir.data(), n);

    double lr = 0, ll = 0, rr = 0, monoOut = 0, monoIn = 0;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double a = l[(size_t) i], b = r[(size_t) i];
        lr += a * b; ll += a * a; rr += b * b;
        monoOut += (a + b) * (a + b);
        const double c = (double) il[(size_t) i] + ir[(size_t) i];
        monoIn += c * c;
        peak = juce::jmax (peak, std::abs ((float) a), std::abs ((float) b));
    }

    hasSignal = ll + rr > 1.0e-9;
    const float corr = hasSignal ? (float) (lr / std::sqrt (juce::jmax (1.0e-20, ll * rr))) : 1.0f;
    correlation += (corr - correlation) * 0.18f;
    const float bal = hasSignal ? (float) ((std::sqrt (rr) - std::sqrt (ll)) / (std::sqrt (rr) + std::sqrt (ll))) : 0.0f;
    balance += (bal - balance) * 0.18f;
    if (monoIn > 1.0e-9 && monoOut > 1.0e-12)
        monoDelta += ((float) (10.0 * std::log10 (monoOut / monoIn)) - monoDelta) * 0.2f;
    level = peak > level ? peak : level * 0.97f + peak * 0.03f;

    // Fade the old frame, then add the new points.
    {
        juce::Image::BitmapData pixels (trail, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < trail.getHeight(); ++y)
        {
            auto* row = pixels.getLinePointer (y);
            for (int x = 0; x < trail.getWidth() * pixels.pixelStride; ++x)
                row[x] = (juce::uint8) ((row[x] * 205) >> 8);
        }
    }

    const float w = (float) trail.getWidth(), h = (float) trail.getHeight();
    const float gain = 0.92f / juce::jmax (0.06f, level * 1.15f);
    juce::Graphics g (trail);
    for (int i = 0; i < n; i += 1)
    {
        const float a = l[(size_t) i], b = r[(size_t) i];
        float m = (a + b) * 0.70710678f * gain, s = (b - a) * 0.70710678f * gain;
        juce::Point<float> pt;
        bool outOfPhase = false;
        if (polarMode)
        {
            outOfPhase = m < 0.0f;
            if (m < 0.0f) { m = -m; s = -s; }
            const float radius = juce::jmin (1.0f, std::sqrt (m * m + s * s));
            const float angle = std::atan2 (s, m);
            pt = { w * 0.5f + radius * std::sin (angle) * w * 0.48f, h * 0.92f - radius * std::cos (angle) * h * 0.86f };
        }
        else
        {
            outOfPhase = std::abs (s) > std::abs (m);
            pt = { w * 0.5f + s * w * 0.5f, h * 0.5f - m * h * 0.5f };
        }
        g.setColour ((outOfPhase ? Theme::amber : (i % 2 ? Theme::cyan : Theme::magenta)).withAlpha (0.55f));
        g.fillRect (pt.x - 1.0f, pt.y - 1.0f, 2.2f, 2.2f);
    }
    repaint();
}

void ScopeView::paint (juce::Graphics& g)
{
    const auto area = scopeArea();
    const auto c = area.getCentre();

    g.setColour (Theme::background.withAlpha (0.85f));
    if (polarMode)
        g.fillRect (area);
    else
        g.fillEllipse (area);

    // Guides: L and R diagonals, the mono axis, and the out-of-phase sides.
    g.setFont (Theme::mono (10.0f, true));
    if (polarMode)
    {
        const juce::Point<float> base { c.x, area.getY() + area.getHeight() * 0.92f };
        const float rx = area.getWidth() * 0.48f, ry = area.getHeight() * 0.86f;
        for (float frac : { 0.33f, 0.66f, 1.0f })
        {
            juce::Path arc;
            arc.addCentredArc (base.x, base.y, rx * frac, ry * frac, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.setColour (Theme::edge.withAlpha (0.8f));
            g.strokePath (arc, juce::PathStrokeType (1.0f));
        }
        for (float deg : { -90.0f, -45.0f, 0.0f, 45.0f, 90.0f })
        {
            const float a = juce::degreesToRadians (deg);
            const juce::Point<float> end { base.x + std::sin (a) * rx, base.y - std::cos (a) * ry };
            g.setColour ((std::abs (deg) > 50.0f ? Theme::amber : Theme::edge.brighter (0.4f)).withAlpha (0.7f));
            g.drawLine ({ base, end }, 1.0f);
        }
        g.setColour (Theme::dim.brighter (0.4f));
        g.drawText ("L", juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ base.x - rx * 0.74f, base.y - ry * 0.74f }), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ base.x + rx * 0.74f, base.y - ry * 0.74f }), juce::Justification::centred);
        g.drawText ("M", juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ base.x, base.y - ry - 2.0f }).translated (0.0f, 10.0f), juce::Justification::centred);
    }
    else
    {
        for (float frac : { 0.5f, 1.0f })
        {
            g.setColour (Theme::edge.withAlpha (0.8f));
            g.drawEllipse (area.withSizeKeepingCentre (area.getWidth() * frac, area.getHeight() * frac), 1.0f);
        }
        const float rad = area.getWidth() * 0.5f;
        for (float deg : { 0.0f, 45.0f, 90.0f, 135.0f })
        {
            const float a = juce::degreesToRadians (deg);
            g.setColour ((deg == 90.0f ? Theme::amber : Theme::edge.brighter (0.4f)).withAlpha (0.55f));
            g.drawLine ({ polar (c, rad, a), polar (c, rad, a + juce::MathConstants<float>::pi) }, 1.0f);
        }
        g.setColour (Theme::dim.brighter (0.4f));
        g.drawText ("L", juce::Rectangle<float> (14.0f, 14.0f).withCentre (polar (c, rad * 0.82f, -juce::MathConstants<float>::pi / 4.0f)), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (14.0f, 14.0f).withCentre (polar (c, rad * 0.82f, juce::MathConstants<float>::pi / 4.0f)), juce::Justification::centred);
        g.drawText ("M", juce::Rectangle<float> (14.0f, 14.0f).withCentre (polar (c, rad * 0.9f, 0.0f)), juce::Justification::centred);
        g.drawText ("+S", juce::Rectangle<float> (20.0f, 14.0f).withCentre (polar (c, rad * 0.86f, juce::MathConstants<float>::halfPi)), juce::Justification::centred);
        g.drawText ("-S", juce::Rectangle<float> (20.0f, 14.0f).withCentre (polar (c, rad * 0.86f, -juce::MathConstants<float>::halfPi)), juce::Justification::centred);
    }

    g.drawImage (trail, area, juce::RectanglePlacement::stretchToFit);

    g.setFont (Theme::mono (9.0f, true));
    g.setColour (Theme::cyan.withAlpha (0.8f));
    g.drawText (polarMode ? "POLAR SAMPLE" : "LISSAJOUS", getLocalBounds().toFloat().withHeight (14.0f), juce::Justification::topLeft);
    g.setColour (Theme::dim);
    g.drawText ("CLICK TO SWITCH", getLocalBounds().toFloat().withHeight (14.0f), juce::Justification::topRight);

    // Correlation meter.
    const auto bottom = getLocalBounds().toFloat().withTop (area.getBottom() + 8.0f);
    auto meter = bottom.withHeight (18.0f).reduced (22.0f, 0.0f);
    g.setColour (Theme::background);
    g.fillRect (meter);
    g.setGradientFill (juce::ColourGradient (Theme::red.withAlpha (0.35f), meter.getX(), 0.0f, Theme::red.withAlpha (0.0f), meter.getCentreX(), 0.0f, false));
    g.fillRect (meter.withRight (meter.getCentreX()));
    g.setGradientFill (juce::ColourGradient (Theme::lime.withAlpha (0.0f), meter.getCentreX(), 0.0f, Theme::lime.withAlpha (0.3f), meter.getRight(), 0.0f, false));
    g.fillRect (meter.withLeft (meter.getCentreX()));
    g.setColour (Theme::edge);
    g.drawRect (meter, 1.0f);
    for (int i = 0; i <= 8; ++i)
        g.drawVerticalLine ((int) (meter.getX() + meter.getWidth() * (float) i / 8.0f), meter.getBottom() - (i % 4 == 0 ? 18.0f : 5.0f), meter.getBottom());

    const float cx = meter.getX() + meter.getWidth() * (correlation + 1.0f) * 0.5f;
    const auto corrColour = correlation < 0.0f ? Theme::red : (correlation < 0.3f ? Theme::amber : Theme::lime);
    juce::Path needle;
    needle.startNewSubPath (cx, meter.getY() - 2.0f);
    needle.lineTo (cx, meter.getBottom() + 2.0f);
    Theme::glowStroke (g, needle, corrColour, 2.0f, hasSignal ? 1.0f : 0.3f);

    g.setFont (Theme::mono (9.0f, true));
    g.setColour (Theme::dim);
    g.drawText ("-1", meter.withX (meter.getX() - 22.0f).withWidth (20.0f), juce::Justification::centredRight);
    g.drawText ("+1", meter.withX (meter.getRight() + 2.0f).withWidth (20.0f), juce::Justification::centredLeft);

    // Readouts.
    auto row = bottom.withTop (meter.getBottom() + 8.0f).withHeight (40.0f);
    const float cellW = row.getWidth() / 3.0f;
    auto readout = [&] (const juce::String& label, const juce::String& value, juce::Colour colour)
    {
        auto cell = row.removeFromLeft (cellW).reduced (3.0f, 0.0f);
        g.setColour (Theme::panelHi.withAlpha (0.8f));
        g.fillRect (cell);
        g.setColour (colour.withAlpha (0.5f));
        g.drawRect (cell, 1.0f);
        g.setColour (Theme::dim.brighter (0.3f));
        g.setFont (Theme::mono (8.5f, true));
        g.drawText (label, cell.withHeight (16.0f), juce::Justification::centred);
        g.setColour (colour);
        g.setFont (Theme::mono (13.0f, true));
        g.drawText (value, cell.withTrimmedTop (14.0f), juce::Justification::centred);
    };

    readout ("CORRELATION", hasSignal ? juce::String (correlation, 2) : "--", corrColour);
    const float delta = std::abs (monoDelta) < 0.05f ? 0.0f : monoDelta;
    readout ("MONO SUM", hasSignal ? (delta >= 0.0f ? "+" : "") + juce::String (delta, 1) + " dB" : "--",
             std::abs (monoDelta) < 0.5f ? Theme::lime : Theme::amber);
    readout ("BALANCE", ! hasSignal ? "--" : std::abs (balance) < 0.01f ? "C" : (balance < 0 ? "L " : "R ") + juce::String (juce::roundToInt (std::abs (balance) * 100.0f)),
             Theme::cyan);
}

//==============================================================================
PhaseWheel::PhaseWheel (SkwideAudioProcessor& p)
    : proc (p),
      attachment (*p.apvts.getParameter (ParamIDs::phase), [this] (float v) { value = v; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void PhaseWheel::tick()
{
    live = proc.livePhase.load();
    history[(size_t) historyPos] = live;
    historyPos = (historyPos + 1) % (int) history.size();
    repaint();
}

void PhaseWheel::setFromMouse (juce::Point<float> pos)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    float deg = juce::radiansToDegrees (std::atan2 (pos.x - c.x, c.y - pos.y));
    if (deg < 0.0f)
        deg += 360.0f;
    attachment.setValueAsPartOfGesture (deg);
}

void PhaseWheel::mouseDown (const juce::MouseEvent& e)
{
    attachment.beginGesture();
    setFromMouse (e.position);
}

void PhaseWheel::mouseDrag (const juce::MouseEvent& e) { setFromMouse (e.position); }
void PhaseWheel::mouseUp (const juce::MouseEvent&) { attachment.endGesture(); }

void PhaseWheel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float radius = size * 0.5f - 4.0f;
    const float drift = proc.apvts.getRawParameterValue (ParamIDs::driftDepth)->load();

    g.setGradientFill (juce::ColourGradient (Theme::panelHi.brighter (0.1f), c, Theme::background, c.translated (radius, 0.0f), true));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));

    for (int i = 0; i < 72; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 72.0f;
        const bool major = i % 9 == 0;
        g.setColour ((major ? Theme::cyan : Theme::dim).withAlpha (major ? 0.8f : 0.35f));
        g.drawLine ({ polar (c, radius * (major ? 0.84f : 0.9f), a), polar (c, radius, a) }, major ? 1.6f : 1.0f);
    }

    // Drift sweep.
    if (drift > 0.5f)
    {
        juce::Path sweep;
        const float a = juce::degreesToRadians (value);
        const float d = juce::degreesToRadians (drift);
        sweep.addCentredArc (c.x, c.y, radius * 0.74f, radius * 0.74f, 0.0f, a - d, a + d, true);
        Theme::glowStroke (g, sweep, Theme::magenta, 3.0f, 0.6f);
    }

    // Ghost trail of the needle.
    for (size_t k = 0; k < history.size(); ++k)
    {
        const int idx = (historyPos + (int) k) % (int) history.size();
        const float age = (float) k / (float) history.size();
        const float a = juce::degreesToRadians (history[(size_t) idx]);
        g.setColour (Theme::magenta.withAlpha (0.12f * age));
        g.drawLine ({ polar (c, radius * 0.2f, a), polar (c, radius * 0.7f, a) }, 2.0f);
    }

    juce::Path needle;
    const float la = juce::degreesToRadians (live);
    needle.startNewSubPath (polar (c, radius * 0.18f, la));
    needle.lineTo (polar (c, radius * 0.8f, la));
    Theme::glowStroke (g, needle, Theme::cyan, 2.2f);
    Theme::glowDot (g, polar (c, radius * 0.8f, la), 2.5f, Theme::cyan);

    // Set-point marker.
    Theme::glowDot (g, polar (c, radius * 0.94f, juce::degreesToRadians (value)), 2.2f, Theme::lime);

    g.setColour (Theme::background);
    g.fillEllipse (juce::Rectangle<float> (radius * 0.6f, radius * 0.6f).withCentre (c));
    g.setColour (Theme::cyan.withAlpha (0.4f));
    g.drawEllipse (juce::Rectangle<float> (radius * 0.6f, radius * 0.6f).withCentre (c), 1.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::mono (12.0f, true));
    g.drawText (juce::String (juce::roundToInt (live)) + juce::String::fromUTF8 ("\xc2\xb0"),
                juce::Rectangle<float> (radius, 16.0f).withCentre (c), juce::Justification::centred);
}

//==============================================================================
DepthView::DepthView (SkwideAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
}

void DepthView::tick()
{
    time += 1.0f / 60.0f;
    shown += (proc.apvts.getRawParameterValue (ParamIDs::depth)->load() * 0.01f - shown) * 0.2f;
    width += (proc.apvts.getRawParameterValue (ParamIDs::width)->load() * 0.01f - width) * 0.2f;
    repaint();
}

void DepthView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Theme::background.withAlpha (0.85f));
    g.fillRect (b);

    // Room in one-point perspective.
    const auto back = b.withSizeKeepingCentre (b.getWidth() * 0.34f, b.getHeight() * 0.34f).translated (0.0f, -b.getHeight() * 0.08f);
    g.setColour (Theme::lime.withAlpha (0.25f));
    g.drawRect (back, 1.0f);
    for (auto [outer, inner] : { std::pair (b.getTopLeft(), back.getTopLeft()), std::pair (b.getTopRight(), back.getTopRight()),
                                 std::pair (b.getBottomLeft(), back.getBottomLeft()), std::pair (b.getBottomRight(), back.getBottomRight()) })
        g.drawLine ({ outer, inner }, 1.0f);
    for (int i = 1; i < 5; ++i)
    {
        const float t = (float) i / 5.0f;
        const float y = juce::jmap (t * t, back.getBottom(), b.getBottom());
        const float xl = juce::jmap (t * t, back.getX(), b.getX()), xr = juce::jmap (t * t, back.getRight(), b.getRight());
        g.setColour (Theme::lime.withAlpha (0.08f + 0.1f * t));
        g.drawHorizontalLine ((int) y, xl, xr);
    }

    // Source: z = 0 at the back wall (far), 1 at the front (near).
    const float z = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * shown);
    const float persp = juce::jmap (z, 0.34f, 1.0f);
    const juce::Point<float> centre { b.getCentreX(), juce::jmap (z, back.getCentreY(), b.getCentreY() + b.getHeight() * 0.14f) };
    const float spread = b.getWidth() * 0.22f * persp * juce::jlimit (0.0f, 2.0f, width);

    for (int k = 0; k < 3; ++k)
    {
        const float phaseT = std::fmod (time * 0.6f + (float) k / 3.0f, 1.0f);
        g.setColour (Theme::lime.withAlpha (0.35f * (1.0f - phaseT)));
        g.drawEllipse (juce::Rectangle<float> ((spread + 10.0f) * 2.0f * (0.4f + phaseT), 14.0f * persp * (0.4f + phaseT) * 2.0f).withCentre (centre), 1.0f);
    }

    juce::Path bar;
    bar.startNewSubPath (centre.x - spread, centre.y);
    bar.lineTo (centre.x + spread, centre.y);
    Theme::glowStroke (g, bar, Theme::cyan, 2.0f * persp + 0.5f, 0.8f);
    Theme::glowDot (g, centre, 3.0f + 4.0f * persp, Theme::lime);

    g.setFont (Theme::mono (9.0f, true));
    g.setColour (Theme::dim.brighter (0.4f));
    g.drawText (shown > 0.05f ? "NEAR" : shown < -0.05f ? "FAR" : "CENTER", b.reduced (4.0f, 2.0f), juce::Justification::topLeft);
}

//==============================================================================
SpreadMeter::SpreadMeter (SkwideAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
}

void SpreadMeter::tick()
{
    shown += (proc.liveSpread.load() - shown) * 0.25f;
    repaint();
}

void SpreadMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setFont (Theme::mono (9.0f, true));
    g.setColour (Theme::dim.brighter (0.4f));
    g.drawText ("ALLPASS SPREAD ENGINE", b.withHeight (14.0f), juce::Justification::centredLeft);
    g.setColour (Theme::magenta);
    g.drawText (juce::String (juce::roundToInt (shown * 100.0f)) + "%", b.withHeight (14.0f), juce::Justification::centredRight);

    const auto track = b.withTrimmedTop (18.0f).withHeight (12.0f);
    const int segments = 30;
    const float segW = track.getWidth() / (float) segments;
    for (int i = 0; i < segments; ++i)
    {
        const float t = (float) i / (float) segments;
        const bool lit = t < shown;
        const auto c = Theme::cyan.interpolatedWith (Theme::magenta, t);
        g.setColour (lit ? c : Theme::edge.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (track.getX() + segW * (float) i + 1.0f, track.getY(), segW - 2.0f, track.getHeight()));
    }
}

//==============================================================================
HaasView::HaasView (SkwideAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
}

void HaasView::tick()
{
    auto value = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    time += 1.0f / 60.0f;
    left += (value (ParamIDs::haasLeft) - left) * 0.3f;
    right += (value (ParamIDs::haasRight) - right) * 0.3f;
    on = value (ParamIDs::haasOn) > 0.5f;
    safe = value (ParamIDs::haasSafe) > 0.5f;
    amount += ((on ? value (ParamIDs::haasAmount) * 0.01f : 0.0f) - amount) * 0.2f;
    repaint();
}

// Square-root time axis so sub-millisecond settings like 0.23 ms are still visible next to 30 ms.
float HaasView::xForMs (juce::Rectangle<float> lane, float ms) const
{
    return lane.getX() + lane.getWidth() * std::sqrt (juce::jlimit (0.0f, 30.0f, ms) / 30.0f);
}

void HaasView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Theme::background.withAlpha (0.85f));
    g.fillRect (b);
    g.setColour (Theme::edge);
    g.drawRect (b, 1.0f);

    const auto inner = b.reduced (10.0f, 6.0f);
    const auto header = inner.withHeight (14.0f);
    auto lanes = inner.withTrimmedTop (18.0f).withTrimmedBottom (14.0f).withTrimmedLeft (20.0f);
    const float laneH = lanes.getHeight() / 2.0f;
    const juce::Rectangle<float> laneRects[2] { lanes.removeFromTop (laneH), lanes };
    const float delays[2] { left, right };
    const juce::Colour colours[2] { Theme::cyan, Theme::magenta };
    const float alpha = 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, amount);

    // Mode readout.
    g.setFont (Theme::mono (9.5f, true));
    if (! on)
    {
        g.setColour (Theme::dim);
        g.drawText ("SUPER WIDE OFF", header, juce::Justification::centredLeft);
    }
    else if (safe)
    {
        g.setColour (Theme::lime);
        g.drawText ("MONO-SAFE HAAS // DELAYS IN THE SIDE ONLY", header, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (Theme::amber.withAlpha (0.6f + 0.4f * std::abs (std::sin (time * 3.0f))));
        g.drawText ("CLASSIC HAAS // MAXIMUM WIDTH // MONO SUM WILL COMB", header, juce::Justification::centredLeft);
    }

    const float diff = right - left;
    g.setColour (Theme::text.withAlpha (alpha));
    g.drawText (std::abs (diff) < 0.005f ? juce::String ("NO OFFSET")
                                         : (diff > 0.0f ? "L LEADS BY " : "R LEADS BY ") + juce::String (std::abs (diff), 2) + " ms",
                header, juce::Justification::centredRight);

    // Time grid.
    g.setFont (Theme::mono (8.5f));
    for (float ms : { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 30.0f })
    {
        const float x = xForMs (laneRects[0], ms);
        g.setColour (Theme::edge.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, laneRects[0].getY(), laneRects[1].getBottom());
        g.setColour (Theme::dim);
        g.drawText (ms < 1.0f && ms > 0.0f ? juce::String (ms, 1) : juce::String ((int) ms),
                    juce::Rectangle<float> (x - 14.0f, laneRects[1].getBottom() + 1.0f, 28.0f, 12.0f), juce::Justification::centred);
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto lane = laneRects[ch].reduced (0.0f, 3.0f);
        g.setColour (colours[ch].withAlpha (0.06f));
        g.fillRect (lane);
        g.setColour (colours[ch].withAlpha (0.8f));
        g.setFont (Theme::mono (11.0f, true));
        g.drawText (ch == 0 ? "L" : "R", lane.withX (lane.getX() - 20.0f).withWidth (16.0f), juce::Justification::centred);

        // Dry hit at 0, delayed hit at the set time, joined by a glowing trace.
        const float x0 = xForMs (lane, 0.0f), x1 = xForMs (lane, delays[ch]);
        juce::Path trace;
        trace.startNewSubPath (x0, lane.getCentreY());
        trace.lineTo (x1, lane.getCentreY());
        Theme::glowStroke (g, trace, colours[ch], 1.4f, 0.5f * alpha);

        juce::Path pulse;
        pulse.startNewSubPath (x1, lane.getBottom());
        pulse.lineTo (x1, lane.getY());
        Theme::glowStroke (g, pulse, colours[ch], 2.2f, alpha);
        Theme::glowDot (g, { x1, lane.getCentreY() }, 3.0f, colours[ch], alpha);

        g.setFont (Theme::mono (10.0f, true));
        g.setColour (Theme::text.withAlpha (alpha));
        const bool labelLeft = x1 > lane.getRight() - 80.0f;
        g.drawText (juce::String (delays[ch], 2) + " ms",
                    juce::Rectangle<float> (labelLeft ? x1 - 76.0f : x1 + 6.0f, lane.getY(), 70.0f, lane.getHeight()),
                    labelLeft ? juce::Justification::centredRight : juce::Justification::centredLeft);
    }
}
