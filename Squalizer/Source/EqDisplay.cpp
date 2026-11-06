#include "EqDisplay.h"
#include <BinaryData.h>

namespace
{
    constexpr double minFreq = 20.0, maxFreq = 20000.0;

    juce::String freqText (double f)
    {
        return f < 1000.0 ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f / 1000.0, f < 10000.0 ? 2 : 1) + " kHz";
    }
}

HeaderView::HeaderView()
    : logo (juce::ImageCache::getFromMemory (BinaryData::skazi_logo_png, BinaryData::skazi_logo_pngSize).createCopy())
{
    // The logo ships on solid black: brightness becomes alpha and the white takes the silkscreen colour. The red ring stays red.
    logo = logo.convertedToFormat (juce::Image::ARGB);
    juce::Image::BitmapData pixels (logo, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < logo.getHeight(); ++y)
        for (int x = 0; x < logo.getWidth(); ++x)
        {
            const auto c = pixels.getPixelColour (x, y);
            const float level = juce::jmax (c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue());
            if (level <= 0.0f)
            {
                pixels.setPixelColour (x, y, juce::Colours::transparentBlack);
                continue;
            }
            const bool red = c.getFloatRed() > 0.5f && c.getFloatGreen() < 0.4f;
            const auto tint = red ? juce::Colour (0xffe0352b) : Theme::legend;
            pixels.setPixelColour (x, y, tint.withAlpha (juce::jmin (1.0f, level) * c.getFloatAlpha()));
        }
    setInterceptsMouseClicks (false, false);
}

void HeaderView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto logoBox = bounds.withWidth (92.0f).reduced (0.0f, 4.0f);
    g.drawImage (logo, logoBox, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    Theme::grooveV (g, logoBox.getRight() + 12.0f, bounds.getY() + 8.0f, bounds.getBottom() - 8.0f);

    const auto text = bounds.withTrimmedLeft (logoBox.getRight() + 28.0f);
    Theme::glowText (g, "S-QUALIZER", text.withHeight (40.0f), juce::Justification::bottomLeft, Theme::amber,
                     Theme::mono (34.0f, true).withExtraKerningFactor (0.08f));
    Theme::glowText (g, "// PHOSPHOR EQ  -  BY ASHER SWISSA", text.withTrimmedTop (42.0f).withHeight (16.0f),
                     juce::Justification::centredLeft, Theme::amber, Theme::mono (12.0f), 0.45f);
}

//==============================================================================
EqDisplay::EqDisplay (SqualizerAudioProcessor& p)
    : proc (p),
      fftData ((size_t) fftSize * 2), preDb ((size_t) bins, -120.0f), postDb ((size_t) bins, -120.0f),
      sourcePower ((size_t) bins, 0.0), refPower ((size_t) bins, 0.0), framePower ((size_t) bins, 0.0), postPower ((size_t) bins, 0.0), sidePower ((size_t) bins, 0.0)
{
    setWantsKeyboardFocus (true);
}

juce::Rectangle<float> EqDisplay::plot() const
{
    return getLocalBounds().toFloat().withTrimmedTop (26.0f).withTrimmedBottom (30.0f).reduced (10.0f, 0.0f);
}

float EqDisplay::xFor (double freq) const
{
    const auto r = plot();
    return r.getX() + r.getWidth() * (float) (std::log (freq / minFreq) / std::log (maxFreq / minFreq));
}

double EqDisplay::freqAt (float x) const
{
    const auto r = plot();
    return juce::jlimit (minFreq, maxFreq, minFreq * std::pow (maxFreq / minFreq, (double) ((x - r.getX()) / r.getWidth())));
}

float EqDisplay::yFor (double dbValue) const
{
    const auto r = plot();
    return r.getCentreY() - (float) (dbValue / displayRange) * r.getHeight() * 0.5f;
}

double EqDisplay::dbAt (float y) const
{
    const auto r = plot();
    return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * displayRange;
}

float EqDisplay::yForSpectrum (double level, double freq) const
{
    // 4.5 dB/octave tilt around 1 kHz so a balanced mix reads roughly flat.
    const auto r = plot();
    const double tilted = level + 4.5 * std::log2 (freq / 1000.0);
    return (float) juce::jmap (juce::jlimit (-96.0, 6.0, tilted), -96.0, 6.0, (double) r.getBottom(), (double) r.getY());
}

juce::Point<float> EqDisplay::nodePosition (int band) const
{
    const auto s = proc.bandSettings (band);
    return { xFor (s.freq), yFor (sq::hasGain (s.type) ? juce::jlimit (-displayRange, displayRange, s.gain) : 0.0) };
}

int EqDisplay::bandAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 14.0f;
    for (int b = 0; b < sq::numBands; ++b)
        if (const float d = nodePosition (b).getDistanceFrom (pos); d < bestDist || (b == selected && d < 14.0f))
        {
            best = b;
            bestDist = d;
        }
    return best;
}

double EqDisplay::curveDb (double freq, bool sidePath) const
{
    const double sr = proc.getSampleRateSafe();
    const int phase = proc.phaseMode();
    double m = 1.0;
    for (int b = 0; b < sq::numBands; ++b)
    {
        const auto s = proc.bandSettings (b);
        if (! s.on || (sidePath ? (s.channel != sq::stereo && s.channel != sq::side) : s.channel == sq::side))
            continue;
        m *= sq::bandMagnitude (s, freq, sr, phase);
    }
    if (proc.tubeOn())
        m *= sq::tubeMagnitude (proc.tubeSettings(), freq, sr);
    return 20.0 * std::log10 (m + 1e-12);
}

double EqDisplay::spectrumDbAt (const std::vector<float>& spectrum, double freq) const
{
    const double bin = juce::jlimit (1.0, (double) bins - 2.0, freq * fftSize / proc.getSampleRateSafe());
    const int i = (int) bin;
    const double t = bin - i;
    return spectrum[(size_t) i] * (1.0 - t) + spectrum[(size_t) i + 1] * t;
}

void EqDisplay::analyse (ScopeFifo& fifo, std::vector<float>& smoothed, std::vector<double>* power)
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    fifo.copyLatest (fftData.data(), fftSize);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const float scale = 4.0f / (float) fftSize;   // hann coherent gain 0.5, single-sided spectrum
    for (int k = 0; k < bins; ++k)
    {
        const float mag = fftData[(size_t) k] * scale;
        const float level = juce::Decibels::gainToDecibels (mag, -120.0f);
        auto& s = smoothed[(size_t) k];
        s += (level > s ? 0.5f : 0.12f) * (level - s);
        if (power != nullptr)
            (*power)[(size_t) k] = (double) mag * mag;
    }
}

void EqDisplay::tick()
{
    if (capturingReference && ! wasCapturing)
    {
        std::fill (refPower.begin(), refPower.end(), 0.0);
        refFrames = 0;
    }
    wasCapturing = capturingReference;

    analyse (proc.preScope, preDb, &framePower);
    // Silence would drag the long-term averages down for nothing; only count frames with signal.
    double total = 0.0;
    for (auto p : framePower)
        total += p;
    if (total > 1e-9)
    {
        for (int k = 0; k < bins; ++k)
        {
            sourcePower[(size_t) k] += (sourceFrames < 200 ? 1.0 / (sourceFrames + 1) : 0.005) * (framePower[(size_t) k] - sourcePower[(size_t) k]);
            if (capturingReference)
                refPower[(size_t) k] += framePower[(size_t) k];
        }
        ++sourceFrames;
        if (capturingReference)
            ++refFrames;
    }
    analyse (proc.postScope, postDb, &postPower);
    if (proc.sidechainActive.load())
    {
        std::vector<float> unused ((size_t) bins);
        analyse (proc.sideScope, unused, &sidePower);
    }
    if (option ("anFreeze", 0) == 0)
        updateBars();
    repaint();
}

void EqDisplay::resized()
{
    const float scale = juce::jmax (1.0f, juce::Component::getApproximateScaleFactorForComponent (this));
    grid = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) getWidth() * scale), juce::roundToInt ((float) getHeight() * scale), true);
    juce::Graphics g (grid);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto r = plot();
    columnX.clear();
    columnFreqs.clear();
    for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
    {
        columnX.push_back (x);
        columnFreqs.push_back (freqAt (x));
    }
    for (auto& cached : bandCurves)
        cached.phase = -1;
    tubeValid = false;

    g.setFont (Theme::mono (10.0f));
    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
        for (int m = 1; m <= 9; ++m)
        {
            const double f = decade * m;
            if (f < minFreq || f > maxFreq)
                continue;
            const float x = xFor (f);
            const bool major = m == 1 || m == 2 || m == 5;
            g.setColour (Theme::amber.withAlpha (major ? 0.12f : 0.05f));
            g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
            if (major)
            {
                g.setColour (Theme::amber.withAlpha (0.55f));
                const auto label = f < 1000.0 ? juce::String ((int) f) : juce::String ((int) (f / 1000.0)) + "k";
                g.drawText (label, juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, r.getBottom() + 21.0f }), juce::Justification::centred);
            }
        }
    // Piano strip under the plot: one key per semitone, C keys named, so frequencies read as notes.
    for (int note = 16; note <= 135; ++note)
    {
        const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const float k0 = xFor (f / std::pow (2.0, 1.0 / 24.0)), k1 = xFor (f * std::pow (2.0, 1.0 / 24.0));
        if (k1 < r.getX() || k0 > r.getRight())
            continue;
        const int pc = note % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const auto key = juce::Rectangle<float> (k0 + 0.3f, r.getBottom() + 2.0f, k1 - k0 - 0.6f, black ? 5.0f : 8.0f).getIntersection (r.withHeight (20.0f).withY (r.getBottom()));
        g.setColour (black ? Theme::amber.withAlpha (0.08f) : Theme::amber.withAlpha (pc == 0 ? 0.35f : 0.16f));
        g.fillRect (key);
    }

    for (int dbLine = -24; dbLine <= 24; dbLine += 6)
    {
        const float y = yFor (dbLine);
        g.setColour (Theme::amber.withAlpha (dbLine == 0 ? 0.2f : 0.06f));
        g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
        if (std::abs (dbLine) != 24)
        {
            g.setColour (Theme::amber.withAlpha (0.45f));
            g.drawText ((dbLine > 0 ? "+" : "") + juce::String (dbLine), juce::Rectangle<float> (r.getRight() - 30.0f, y - 12.0f, 28.0f, 11.0f), juce::Justification::centredRight);
        }
    }
}

float EqDisplay::barLevelFrom (const std::vector<double>& power, int bar) const
{
    const double binHz = proc.getSampleRateSafe() / fftSize, fc = barCentre (bar);
    const int k0 = juce::jmax (1, (int) std::ceil (fc / 1.1225 / binHz)), k1 = juce::jmin (bins - 1, juce::jmax (k0, (int) (fc * 1.1225 / binHz)));
    double sum = 0.0;
    for (int k = k0; k <= k1 && k < (int) power.size(); ++k)
        sum += power[(size_t) k];
    return (float) juce::jlimit (0.0, 1.0, (10.0 * std::log10 (sum + 1e-20) + barFloorDb) / barFloorDb);
}

void EqDisplay::updateBars()
{
    // Third-octave bands as segment counts with a falling peak hold; SLOW / MED / FAST set the fall (60 frames a second).
    static constexpr float falls[] { 0.035f, 0.07f, 0.14f };
    const float fall = falls[juce::jlimit (0, 2, option ("anSpeed", 1))];
    const auto& source = option ("anSource", post) == pre ? framePower : postPower;
    for (int b = 0; b < numBars; ++b)
    {
        const float level = barLevelFrom (source, b);
        auto& v = barLevel[(size_t) b];
        v += (level > v ? 0.6f : fall) * (level - v);
        auto& p = barPeak[(size_t) b];
        if (v >= p)
        {
            p = v;
            barHold[(size_t) b] = 90;
        }
        else if (barHold[(size_t) b] > 0)
            --barHold[(size_t) b];
        else
            p = juce::jmax (v, p - 0.004f);

        auto& s = sideLevel[(size_t) b];
        const float sideTarget = proc.sidechainActive.load() ? barLevelFrom (sidePower, b) : 0.0f;
        s += (sideTarget > s ? 0.6f : fall) * (sideTarget - s);
    }
}

void EqDisplay::refreshCurves()
{
    const int n = (int) columnFreqs.size();
    const double sr = proc.getSampleRateSafe();
    const int phase = proc.phaseMode();
    auto same = [] (const sq::BandSettings& a, const sq::BandSettings& b)
    {
        return a.on == b.on && a.type == b.type && a.freq == b.freq && a.gain == b.gain && a.q == b.q && a.slope == b.slope && a.channel == b.channel;
    };
    mainDb.assign ((size_t) n, 0.0f);
    sideDb.assign ((size_t) n, 0.0f);
    for (int b = 0; b < sq::numBands; ++b)
    {
        const auto s = proc.bandSettings (b);
        auto& cached = bandCurves[(size_t) b];
        if (! s.on)
        {
            cached.settings.on = false;
            continue;
        }
        if (cached.phase != phase || ! same (cached.settings, s) || (int) cached.db.size() != n)
        {
            cached.db.resize ((size_t) n);
            sq::bandCurveDb (s, columnFreqs.data(), n, sr, phase, cached.db.data());
            cached.settings = s;
            cached.phase = phase;
        }
        for (int i = 0; i < n; ++i)
        {
            if (s.channel != sq::side)
                mainDb[(size_t) i] += cached.db[(size_t) i];
            if (s.channel == sq::stereo || s.channel == sq::side)
                sideDb[(size_t) i] += cached.db[(size_t) i];
        }
    }
    if (proc.tubeOn())
    {
        const auto t = proc.tubeSettings();
        const bool tubeSame = tubeValid && t.lowFreq == tubeShown.lowFreq && t.lowBoost == tubeShown.lowBoost && t.lowAtten == tubeShown.lowAtten
                           && t.highFreq == tubeShown.highFreq && t.highBoost == tubeShown.highBoost && t.bandwidth == tubeShown.bandwidth
                           && t.attenFreq == tubeShown.attenFreq && t.highAtten == tubeShown.highAtten && (int) tubeDb.size() == n;
        if (! tubeSame)
        {
            tubeDb.resize ((size_t) n);
            sq::tubeCurveDb (t, columnFreqs.data(), n, sr, tubeDb.data());
            tubeShown = t;
            tubeValid = true;
        }
        for (int i = 0; i < n; ++i)
        {
            mainDb[(size_t) i] += tubeDb[(size_t) i];
            sideDb[(size_t) i] += tubeDb[(size_t) i];
        }
    }
}

void EqDisplay::paint (juce::Graphics& g)
{
    const auto screen = getLocalBounds().toFloat();
    const auto r = plot();
    g.setColour (Theme::glass);
    g.fillRoundedRectangle (screen, 10.0f);
    // Faint phosphor haze in the middle of the tube.
    g.setGradientFill (juce::ColourGradient (Theme::amber.withAlpha (0.05f), screen.getCentre(), Theme::amber.withAlpha (0.0f),
                                             screen.getCentre().translated (screen.getWidth() * 0.55f, 0.0f), true));
    g.fillRoundedRectangle (screen, 10.0f);

    // Segmented third-octave bars: green, yellow at the top, red into the last segments, with peak-hold caps.
    const float segH = r.getHeight() / (float) numSegments;
    const bool showBars = option ("anSource", post) != off;
    const bool showSide = proc.sidechainActive.load() || std::any_of (sideLevel.begin(), sideLevel.end(), [] (float v) { return v > 0.01f; });
    for (int b = 0; b < numBars && showBars; ++b)
    {
        const double fc = barCentre (b);
        const float x0 = xFor (juce::jmax (minFreq, fc / 1.1225)) + 1.5f, x1 = xFor (juce::jmin (maxFreq, fc * 1.1225)) - 1.5f;
        const int lit = juce::roundToInt (barLevel[(size_t) b] * numSegments), peak = juce::roundToInt (barPeak[(size_t) b] * numSegments);
        for (int s = 0; s < numSegments; ++s)
        {
            const float f = (float) s / (float) numSegments;
            const auto colour = f > 0.86f ? Theme::ledRed : (f > 0.68f ? Theme::ledYellow : Theme::ledGreen);
            const bool on = s < lit, cap = s == peak - 1;
            g.setColour (colour.withAlpha (on ? 0.42f : (cap ? 0.95f : 0.035f)));
            g.fillRect (x0, r.getBottom() - (float) (s + 1) * segH + 1.0f, x1 - x0, segH - 2.0f);
        }

        if (showSide)
        {
            // Sidechain level as a cap line; where both are loud and within ~6 dB the band is masking: marked red.
            const float sideY = r.getBottom() - sideLevel[(size_t) b] * r.getHeight();
            const bool masking = barLevel[(size_t) b] > 0.4f && sideLevel[(size_t) b] > 0.4f && std::abs (barLevel[(size_t) b] - sideLevel[(size_t) b]) < 0.08f;
            if (masking)
            {
                g.setColour (Theme::ledRed.withAlpha (0.22f));
                g.fillRect (x0, juce::jmin (sideY, r.getBottom() - barLevel[(size_t) b] * r.getHeight()), x1 - x0, r.getBottom() - juce::jmin (sideY, r.getBottom() - barLevel[(size_t) b] * r.getHeight()));
            }
            g.setColour ((masking ? Theme::ledRed : Theme::tuner).withAlpha (0.9f));
            g.fillRect (x0, sideY - 1.0f, x1 - x0, 2.0f);
        }
    }

    g.drawImage (grid, screen);

    g.saveState();
    g.reduceClipRegion (r.expanded (0.0f, 6.0f).toNearestInt());

    const auto sel = proc.bandSettings (selected);
    const double sr = proc.getSampleRateSafe();
    const int phase = proc.phaseMode();
    refreshCurves();
    auto curvePath = [&] (const std::vector<float>& db)
    {
        juce::Path p;
        for (size_t i = 0; i < columnX.size() && i < db.size(); ++i)
        {
            const float y = juce::jlimit (r.getY() - 4.0f, r.getBottom() + 4.0f, yFor (db[i]));
            if (i == 0)
                p.startNewSubPath (columnX[i], y);
            else
                p.lineTo (columnX[i], y);
        }
        return p;
    };
    const float zeroY = yFor (0.0);
    if (sel.on && ! bandCurves[(size_t) selected].db.empty())
    {
        auto p = curvePath (bandCurves[(size_t) selected].db);
        auto fill = p;
        fill.lineTo (columnX.back(), zeroY);
        fill.lineTo (columnX.front(), zeroY);
        fill.closeSubPath();
        const auto colour = BandColours::of (selected);
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.22f), 0.0f, r.getY(), colour.withAlpha (0.04f), 0.0f, zeroY, false));
        g.fillPath (fill);
        g.setColour (colour.withAlpha (0.5f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    bool anySide = false;
    for (int b = 0; b < sq::numBands; ++b)
        if (const auto s = proc.bandSettings (b); s.on && s.channel == sq::side)
            anySide = true;
    if (anySide)
        Theme::glowStroke (g, curvePath (sideDb), Theme::ledIce, 1.6f, 0.8f);
    Theme::glowStroke (g, curvePath (mainDb), Theme::amber, 2.2f, 1.0f);
    g.restoreState();

    // Band nodes: glowing rings. A dynamic band also shows where it is right now.
    for (int b = 0; b < sq::numBands; ++b)
    {
        const auto s = proc.bandSettings (b);
        const auto pos = nodePosition (b);
        const auto colour = BandColours::of (b);
        if (! s.on)
        {
            // Switched off: a dim hollow marker you can still grab.
            const auto box = juce::Rectangle<float> (16.0f, 16.0f).withCentre (pos);
            g.setColour (Theme::glass.withAlpha (0.8f));
            g.fillEllipse (box);
            g.setColour (colour.withAlpha (b == selected ? 0.7f : 0.35f));
            g.drawEllipse (box, 1.2f);
            g.setFont (Theme::mono (10.0f, true));
            g.drawText (b == sq::lowCutBand ? "L" : (b == sq::highCutBand ? "H" : juce::String (b)), box, juce::Justification::centred);
            continue;
        }
        if (sq::hasGain (s.type) && std::abs (s.dyn) > 0.05)
        {
            const float dynY = yFor (juce::jlimit (-displayRange, displayRange, s.gain + proc.liveDyn[(size_t) b].load()));
            const float endY = yFor (juce::jlimit (-displayRange, displayRange, s.gain + s.dyn));
            g.setColour (colour.withAlpha (0.35f));
            g.drawLine (pos.x, pos.y, pos.x, endY, 1.0f);
            juce::Path ring;
            ring.addEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ pos.x, dynY }));
            Theme::glowStroke (g, ring, colour, 1.2f, 0.8f);
        }
        const float radius = b == selected ? 11.0f : 9.0f;
        const auto box = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (pos);
        g.setColour (Theme::glass.withAlpha (0.9f));
        g.fillEllipse (box);
        if (b == selected)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillEllipse (box);
        }
        juce::Path ring;
        ring.addEllipse (box);
        Theme::glowStroke (g, ring, colour, b == selected ? 1.8f : 1.3f, b == selected ? 1.0f : 0.7f);
        Theme::glowText (g, b == sq::lowCutBand ? "L" : (b == sq::highCutBand ? "H" : juce::String (b)), box, juce::Justification::centred,
                         b == selected ? juce::Colours::white : colour, Theme::mono (b == selected ? 12.5f : 11.0f, true), 0.8f);
        if (s.channel != sq::stereo)
            Theme::glowText (g, juce::String::charToString (Names::channels[s.channel][0]), box.translated (radius + 4.0f, -radius - 2.0f).withSizeKeepingCentre (14.0f, 12.0f),
                             juce::Justification::centred, colour, Theme::mono (10.0f, true), 0.6f);
    }

    if (const int solo = proc.soloBand.load(); solo >= 0)
    {
        // Shade everything outside the soloed band's region, like a spotlight on what you hear.
        const auto s = proc.bandSettings (solo);
        const bool peaky = s.type == sq::bell || s.type == sq::notch || s.type == sq::bandPass;
        const double q = peaky ? juce::jlimit (0.4, 12.0, s.q) : 0.9;
        const double half = std::asinh (1.0 / (2.0 * q)) / std::log (2.0);   // half the -3 dB width in octaves
        const float x0 = xFor (juce::jmax (minFreq, s.freq / std::pow (2.0, half))), x1 = xFor (juce::jmin (maxFreq, s.freq * std::pow (2.0, half)));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (r.withRight (x0));
        g.fillRect (r.withLeft (x1));
        g.setColour (BandColours::of (solo).withAlpha (0.08f));
        g.fillRect (r.withLeft (x0).withRight (x1));
        Theme::glowText (g, "SOLO  " + Names::bands[solo], juce::Rectangle<float> (x0, r.getBottom() - 22.0f, juce::jmax (90.0f, x1 - x0), 16.0f),
                         juce::Justification::centred, BandColours::of (solo), Theme::mono (11.5f, true));
    }

    if (dragBand >= 0)
    {
        const auto s = proc.bandSettings (dragBand);
        const auto pos = nodePosition (dragBand);
        juce::String text = freqText (s.freq);
        if (sq::hasGain (s.type))
            text << "  " << (s.gain > 0 ? "+" : "") << juce::String (s.gain, 1) << " dB";
        const auto box = juce::Rectangle<float> (150.0f, 18.0f).withCentre (pos.translated (0.0f, pos.y > r.getY() + 40.0f ? -26.0f : 26.0f));
        g.setColour (Theme::glass.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 4.0f);
        Theme::glowText (g, text, box, juce::Justification::centred, Theme::amber, Theme::mono (12.0f, true));
    }

    // Readout strip.
    const auto strip = screen.withHeight (24.0f).reduced (14.0f, 0.0f).translated (0.0f, 2.0f);
    const auto font = Theme::mono (11.5f, true);
    if (sel.on)
    {
        juce::String text = Names::bands[selected] + "  " + Names::types[sel.type] + "  " + freqText (sel.freq);
        if (sq::hasGain (sel.type))
            text << "  " << (sel.gain > 0 ? "+" : "") << juce::String (sel.gain, 1) << " dB";
        text << "  Q " << juce::String (sel.q, 2) << "  " << Names::channels[sel.channel];
        if (sq::hasGain (sel.type) && std::abs (sel.dyn) > 0.05)
            text << "  DYN " << juce::String (proc.liveDyn[(size_t) selected].load(), 1);
        Theme::glowText (g, text, strip, juce::Justification::centredLeft, Theme::amber, font);
    }
    else
        Theme::glowText (g, Names::bands[selected] + "  OFF  -  DRAG ITS NODE TO SWITCH IT ON", strip, juce::Justification::centredLeft, Theme::amber, font, 0.5f);
    // Analyzer chips on the right of the strip: click to set.
    chips.clear();
    {
        const int source = option ("anSource", post), speed = option ("anSpeed", 1);
        const bool frozen = option ("anFreeze", 0) != 0;
        const std::vector<std::tuple<juce::String, bool, std::function<void()>>> list {
            { "POST", source == post, [this] { setOption ("anSource", post); } },
            { "PRE", source == pre, [this] { setOption ("anSource", pre); } },
            { "OFF", source == off, [this] { setOption ("anSource", off); } },
            { "FREEZE", frozen, [this, frozen] { setOption ("anFreeze", frozen ? 0 : 1); } },
            { "SLOW", speed == 0, [this] { setOption ("anSpeed", 0); } },
            { "MED", speed == 1, [this] { setOption ("anSpeed", 1); } },
            { "FAST", speed == 2, [this] { setOption ("anSpeed", 2); } },
        };
        float x = strip.getRight();
        const auto font = Theme::mono (10.5f, true);
        for (int i = (int) list.size() - 1; i >= 0; --i)
        {
            const auto& [text, lit, click] = list[(size_t) i];
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 12.0f;
            x -= w + (i == 2 || i == 3 ? 10.0f : 3.0f);
            const auto area = juce::Rectangle<float> (x, strip.getY() + 3.0f, w, strip.getHeight() - 6.0f);
            chips.push_back ({ area, text, lit, click });
            g.setColour (lit ? Theme::amber.withAlpha (0.2f) : Theme::glass.withAlpha (0.6f));
            g.fillRoundedRectangle (area, 3.0f);
            g.setColour (Theme::amber.withAlpha (lit ? 0.8f : 0.25f));
            g.drawRoundedRectangle (area, 3.0f, 1.0f);
            Theme::glowText (g, text, area, juce::Justification::centred, lit ? Theme::amber : Theme::legendDim, font, lit ? 0.8f : 0.3f);
        }
        juce::String status;
        if (capturingReference)
            status = "REC REF " + juce::String (refFrames / 60) + "s";
        else if (phase == sq::linear)
            status = "LIN " + juce::String (1000.0 * proc.linearLatency() / sr, 0) + " ms";
        if (proc.sidechainActive.load())
            status = "SC  " + status;
        Theme::glowText (g, status, juce::Rectangle<float> (r.getRight() - 200.0f, r.getBottom() - 18.0f, 194.0f, 14.0f), juce::Justification::centredRight,
                         capturingReference ? Theme::ledRed : Theme::tuner, Theme::mono (11.0f, true), 0.6f);
    }

    if (mouseInside && dragBand < 0 && r.contains (mouse))
    {
        const double f = freqAt (mouse.x);
        const int note = juce::roundToInt (69.0 + 12.0 * std::log2 (f / 440.0));
        const auto label = freqText (f) + "  " + juce::MidiMessage::getMidiNoteName (note, true, true, 4) + "  " + juce::String (dbAt (mouse.y), 1) + " dB";
        g.setColour (Theme::amber.withAlpha (0.18f));
        g.drawVerticalLine (juce::roundToInt (mouse.x), r.getY(), r.getBottom());
        const float lx = mouse.x + 170.0f < r.getRight() ? mouse.x + 8.0f : mouse.x - 178.0f;
        Theme::glowText (g, label, juce::Rectangle<float> (lx, r.getY() + 4.0f, 170.0f, 14.0f), juce::Justification::centredLeft, Theme::amber, Theme::mono (11.0f), 0.6f);
    }

    // Scanlines, then the curved glass.
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    for (float y = screen.getY(); y < screen.getBottom(); y += 3.0f)
        g.fillRect (screen.getX(), y, screen.getWidth(), 1.0f);
    Theme::glassSheen (g, screen);
}

void EqDisplay::setParam (int band, const char* name, double value)
{
    if (auto* p = proc.apvts.getParameter (ParamIDs::band (band, name)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
}

void EqDisplay::startDrag (int band)
{
    dragBand = band;
    dragStartFreq = proc.bandSettings (band).freq;
    dragStartGain = proc.bandSettings (band).gain;
    for (auto* name : { "FREQ", "GAIN" })
        proc.apvts.getParameter (ParamIDs::band (band, name))->beginChangeGesture();
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    for (const auto& chip : chips)
        if (chip.area.contains (e.position))
        {
            chip.click();
            return;
        }
    const int band = bandAt (e.position);
    if (band < 0)
        return;
    if (onSelect)
        onSelect (band);
    if (e.mods.isPopupMenu())
    {
        showBandMenu (band);
        return;
    }
    proc.soloBand = band;   // hold = listen to just this band
    startDrag (band);
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;
    // Relative to where the band started; Shift = 10x finer.
    const auto r = plot();
    const double fine = e.mods.isShiftDown() ? 0.1 : 1.0;
    const double octaves = e.getDistanceFromDragStartX() * fine / r.getWidth() * std::log2 (maxFreq / minFreq);
    if (! proc.bandSettings (dragBand).on && e.getDistanceFromDragStart() > 2)
        setParam (dragBand, "ON", 1.0);
    setParam (dragBand, "FREQ", juce::jlimit (minFreq, maxFreq, dragStartFreq * std::pow (2.0, octaves)));
    if (sq::hasGain (proc.bandSettings (dragBand).type))
        setParam (dragBand, "GAIN", juce::jlimit (-30.0, 30.0, dragStartGain - e.getDistanceFromDragStartY() * fine / (r.getHeight() * 0.5) * displayRange));
    repaint();
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
        for (auto* name : { "FREQ", "GAIN" })
            proc.apvts.getParameter (ParamIDs::band (dragBand, name))->endChangeGesture();
    dragBand = -1;
    proc.soloBand = -1;
    repaint();
}

void EqDisplay::nudge (int band, double octaves, double db)
{
    const auto s = proc.bandSettings (band);
    if (octaves != 0.0)
        setParam (band, "FREQ", juce::jlimit (minFreq, maxFreq, s.freq * std::pow (2.0, octaves)));
    if (db != 0.0 && sq::hasGain (s.type))
        setParam (band, "GAIN", juce::jlimit (-30.0, 30.0, s.gain + db));
    repaint();
}

bool EqDisplay::keyPressed (const juce::KeyPress& key)
{
    if (! proc.bandSettings (selected).on)
        return false;
    // Arrows: 1/12 octave and 0.5 dB a step, Shift for 1/96 octave and 0.1 dB.
    const bool fine = key.getModifiers().isShiftDown();
    const double oct = fine ? 1.0 / 96.0 : 1.0 / 12.0, db = fine ? 0.1 : 0.5;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey)  { nudge (selected, -oct, 0.0); return true; }
    if (code == juce::KeyPress::rightKey) { nudge (selected, oct, 0.0);  return true; }
    if (code == juce::KeyPress::upKey)    { nudge (selected, 0.0, db);   return true; }
    if (code == juce::KeyPress::downKey)  { nudge (selected, 0.0, -db);  return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        setParam (selected, "ON", 0.0);
        return true;
    }
    return false;
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int band = bandAt (e.position); band >= 0)
    {
        setParam (band, "ON", proc.bandSettings (band).on ? 0.0 : 1.0);
        return;
    }
    if (! plot().contains (e.position))
        return;

    // Spectrum grab: the selected EQ band (or the nearest one) jumps onto a clear peak within a quarter octave, narrow.
    const double freq = freqAt (e.position.x);
    int band = selected;
    if (band == sq::lowCutBand || band == sq::highCutBand)
    {
        band = 1;
        for (int b = 2; b < sq::highCutBand; ++b)
            if (std::abs (std::log (proc.bandSettings (b).freq / freq)) < std::abs (std::log (proc.bandSettings (band).freq / freq)))
                band = b;
    }
    double target = freq, q = proc.bandSettings (band).q;
    const double binHz = proc.getSampleRateSafe() / fftSize;
    const int k0 = juce::jmax (1, (int) (freq / 1.19 / binHz)), k1 = juce::jmin (bins - 1, (int) (freq * 1.19 / binHz) + 1);
    if (k1 - k0 >= 2)
    {
        int peak = k0;
        double sum = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            sum += postDb[(size_t) k];
            if (postDb[(size_t) k] > postDb[(size_t) peak])
                peak = k;
        }
        if (postDb[(size_t) peak] > -90.0f && postDb[(size_t) peak] - sum / (k1 - k0 + 1) > 6.0)
        {
            target = peak * binHz;
            q = 4.0;
        }
    }
    setParam (band, "FREQ", target);
    setParam (band, "Q", q);
    setParam (band, "ON", 1.0);
    if (onSelect)
        onSelect (band);
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int band = bandAt (e.position);
    if (band < 0)
        band = proc.bandSettings (selected).on ? selected : -1;
    if (band < 0)
        return;
    const auto s = proc.bandSettings (band);
    setParam (band, "Q", juce::jlimit (0.1, 18.0, s.q * std::exp (wheel.deltaY * 1.5)));
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    mouse = e.position;
    mouseInside = true;
    setMouseCursor (bandAt (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
}

void EqDisplay::showBandMenu (int band)
{
    const auto s = proc.bandSettings (band);
    juce::PopupMenu menu, types, channels, slopes;
    for (int i = 0; i < Names::types.size(); ++i)
        types.addItem (1 + i, Names::types[i], true, s.type == i);
    for (int i = 0; i < Names::channels.size(); ++i)
        channels.addItem (20 + i, Names::channels[i], true, s.channel == i);
    for (int i = 0; i < Names::slopes.size(); ++i)
        slopes.addItem (40 + i, Names::slopes[i], true, s.slope == i);
    menu.addSectionHeader ("BAND " + juce::String (band + 1));
    menu.addSubMenu ("Type", types);
    menu.addSubMenu ("Channel", channels);
    menu.addSubMenu ("Slope", slopes, s.type == sq::lowCut || s.type == sq::highCut);
    menu.addSeparator();
    menu.addItem (99, "Remove band");

    juce::Component::SafePointer<EqDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, band] (int id)
    {
        if (safe == nullptr || id == 0)
            return;
        if (id == 99)        safe->setParam (band, "ON", 0.0);
        else if (id >= 40)   safe->setParam (band, "SLOPE", id - 40);
        else if (id >= 20)   safe->setParam (band, "CH", id - 20);
        else                 safe->setParam (band, "TYPE", id - 1);
    });
}

int EqDisplay::matchToReference()
{
    if (! hasReference() || sourceFrames < 30)
        return 0;

    // The four EQ bands are rewritten from flat; the cuts and the tube EQ stay as they are.
    for (int b = 1; b < sq::highCutBand; ++b)
    {
        setParam (b, "GAIN", 0.0);
        setParam (b, "DYN", 0.0);
    }

    // Third-octave smoothed difference between the reference and the input, on a log grid.
    constexpr int points = 160;
    const double sr = proc.getSampleRateSafe(), binHz = sr / fftSize;
    std::array<double, points> freqs {}, residual {};
    double meanSum = 0.0;
    int meanCount = 0;
    for (int i = 0; i < points; ++i)
    {
        const double f = 25.0 * std::pow (18000.0 / 25.0, (double) i / (points - 1));
        const int k0 = juce::jmax (1, (int) (f / 1.1225 / binHz)), k1 = juce::jmin (bins - 1, juce::jmax (k0, (int) (f * 1.1225 / binHz)));
        double ref = 0.0, src = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            ref += refPower[(size_t) k] / refFrames;
            src += sourcePower[(size_t) k];
        }
        freqs[(size_t) i] = f;
        residual[(size_t) i] = 10.0 * std::log10 ((ref + 1e-20) / (src + 1e-20));
        if (f >= 100.0 && f <= 10000.0)
        {
            meanSum += residual[(size_t) i];
            ++meanCount;
        }
    }
    // Level-independent: only the shape is matched. Then take off what the EQ already does.
    for (int i = 0; i < points; ++i)
        residual[(size_t) i] = juce::jlimit (-12.0, 12.0, residual[(size_t) i] - meanSum / juce::jmax (1, meanCount)) - curveDb (freqs[(size_t) i], false);

    // Greedy fit: a bell on the biggest remaining error, sized by where that error halves.
    int placed = 0;
    for (int band = 1; band < sq::highCutBand; ++band)
    {
        int worst = -1;
        for (int i = 0; i < points; ++i)
            if (freqs[(size_t) i] >= 30.0 && freqs[(size_t) i] <= 16000.0 && (worst < 0 || std::abs (residual[(size_t) i]) > std::abs (residual[(size_t) worst])))
                worst = i;
        if (worst < 0 || std::abs (residual[(size_t) worst]) < 1.0)
            break;
        const double gain = juce::jlimit (-12.0, 12.0, residual[(size_t) worst]);
        int lo = worst, hi = worst;
        while (lo > 0 && residual[(size_t) lo - 1] * gain > gain * gain * 0.5)
            --lo;
        while (hi < points - 1 && residual[(size_t) hi + 1] * gain > gain * gain * 0.5)
            ++hi;
        const double octaves = juce::jmax (0.25, std::log2 (freqs[(size_t) hi] / freqs[(size_t) lo]));
        const double q = juce::jlimit (0.3, 6.0, 1.0 / (2.0 * std::sinh (std::log (2.0) * 0.5 * octaves)));

        sq::BandSettings s;
        s.on = true;
        s.freq = freqs[(size_t) worst];
        s.gain = gain;
        s.q = q;
        setParam (band, "TYPE", sq::bell);
        setParam (band, "FREQ", s.freq);
        setParam (band, "GAIN", s.gain);
        setParam (band, "Q", s.q);
        setParam (band, "CH", sq::stereo);
        setParam (band, "DYN", 0.0);
        setParam (band, "ON", 1.0);
        for (int i = 0; i < points; ++i)
            residual[(size_t) i] -= 20.0 * std::log10 (sq::bandMagnitude (s, freqs[(size_t) i], sr, proc.phaseMode()) + 1e-12);
        ++placed;
    }
    return placed;
}

//==============================================================================
ScopePanel::ScopePanel (SqualizerAudioProcessor& p) : proc (p), l ((size_t) samples), r ((size_t) samples)
{
    setInterceptsMouseClicks (false, false);
}

void ScopePanel::tick()
{
    proc.postScope.copyLatest (l.data(), samples, r.data());
    double sl = 0, sr = 0, slr = 0;
    float pl = 0, pr = 0;
    for (int i = 0; i < samples; ++i)
    {
        sl += l[(size_t) i] * l[(size_t) i];
        sr += r[(size_t) i] * r[(size_t) i];
        slr += l[(size_t) i] * r[(size_t) i];
        pl = juce::jmax (pl, std::abs (l[(size_t) i]));
        pr = juce::jmax (pr, std::abs (r[(size_t) i]));
    }
    peakL = juce::jmax (pl, peakL * 0.9f);
    peakR = juce::jmax (pr, peakR * 0.9f);
    holdL = juce::jmax (pl, holdL * 0.995f);
    holdR = juce::jmax (pr, holdR * 0.995f);
    rms += 0.3f * ((float) std::sqrt ((sl + sr) / (2.0 * samples)) - rms);
    corr += 0.2f * ((sl > 1e-9 && sr > 1e-9 ? (float) (slr / std::sqrt (sl * sr)) : 0.0f) - corr);

    // Goniometer with persistence: fade the old trace, then plot side across, mid up.
    const int side = 236;
    if (! trace.isValid())
        trace = juce::Image (juce::Image::ARGB, side, side, true);
    juce::Graphics g (trace);
    g.setColour (Theme::glass.withAlpha (0.3f));
    g.fillAll();
    const float level = juce::jmax (0.05f, juce::jmax (pl, pr));
    const float k = (float) side * 0.36f / level;
    g.setColour (Theme::amber.withAlpha (0.55f));
    for (int i = 0; i < samples; i += 2)
    {
        const float x = (float) side * 0.5f + (l[(size_t) i] - r[(size_t) i]) * k * 0.7071f;
        const float y = (float) side * 0.5f - (l[(size_t) i] + r[(size_t) i]) * k * 0.7071f;
        g.fillRect (x, y, 1.6f, 1.6f);
    }
    repaint();
}

void ScopePanel::paint (juce::Graphics& g)
{
    const auto screen = getLocalBounds().toFloat();
    g.setColour (Theme::glass);
    g.fillRoundedRectangle (screen, 10.0f);

    const auto scope = juce::Rectangle<float> (236.0f, 236.0f).withCentre ({ screen.getCentreX(), 130.0f });
    g.setColour (Theme::amber.withAlpha (0.12f));
    g.drawLine (scope.getCentreX(), scope.getY() + 6, scope.getCentreX(), scope.getBottom() - 6, 1.0f);
    g.drawLine (scope.getX() + 6, scope.getCentreY(), scope.getRight() - 6, scope.getCentreY(), 1.0f);
    g.setColour (Theme::amber.withAlpha (0.07f));
    g.drawLine ({ scope.getTopLeft().translated (30, 30), scope.getBottomRight().translated (-30, -30) }, 1.0f);
    g.drawLine ({ scope.getTopRight().translated (-30, 30), scope.getBottomLeft().translated (30, -30) }, 1.0f);
    for (auto [text, pos] : { std::pair { "M", scope.getCentre().translated (0, -108) }, { "S", scope.getCentre().translated (108, 0) },
                              { "L", scope.getTopLeft().translated (34, 34) }, { "R", scope.getTopRight().translated (-34, 34) } })
        Theme::glowText (g, text, juce::Rectangle<float> (16, 12).withCentre (pos), juce::Justification::centred, Theme::amber, Theme::mono (10.5f, true), 0.4f);
    if (trace.isValid())
    {
        // Additive-looking bloom: the trace drawn soft and wide underneath, then sharp.
        g.setOpacity (0.35f);
        g.drawImage (trace, scope.expanded (1.5f));
        g.setOpacity (1.0f);
        g.drawImage (trace, scope);
    }

    // Meters.
    auto meter = [&] (const char* label, float y, float value, float hold, juce::String text)
    {
        const auto row = juce::Rectangle<float> (14.0f, y, screen.getWidth() - 28.0f, 14.0f);
        Theme::glowText (g, label, row.withWidth (40.0f), juce::Justification::centredLeft, Theme::amber, Theme::mono (10.5f, true), 0.5f);
        const auto bar = row.withTrimmedLeft (42.0f).withTrimmedRight (56.0f).reduced (0.0f, 3.0f);
        const int segs = 30;
        for (int s = 0; s < segs; ++s)
        {
            const float f = (float) s / segs;
            const auto colour = f > 0.86f ? Theme::ledRed : (f > 0.7f ? Theme::ledYellow : Theme::ledGreen);
            const bool on = f < value, cap = std::abs (f - hold) < 0.5f / segs;
            g.setColour (colour.withAlpha (on ? 0.85f : (cap ? 1.0f : 0.07f)));
            g.fillRect (bar.getX() + bar.getWidth() * f + 0.5f, bar.getY(), bar.getWidth() / segs - 1.5f, bar.getHeight());
        }
        Theme::glowText (g, text, row.withTrimmedLeft (row.getWidth() - 52.0f), juce::Justification::centredRight, Theme::tuner, Theme::mono (11.5f, true), 0.6f);
    };
    auto norm = [] (float gain) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain, -60.0f) + 60.0f) / 60.0f); };
    auto dbText = [] (float gain) { return gain < 0.001f ? juce::String ("-INF") : juce::String (juce::Decibels::gainToDecibels (gain), 1); };
    meter ("PK L", 262.0f, norm (peakL), norm (holdL), dbText (holdL));
    meter ("PK R", 280.0f, norm (peakR), norm (holdR), dbText (holdR));
    meter ("RMS", 298.0f, norm (rms), -1.0f, dbText (rms));

    // Correlation: -1 .. +1, centre mark at 0.
    const auto row = juce::Rectangle<float> (14.0f, 320.0f, screen.getWidth() - 28.0f, 14.0f);
    Theme::glowText (g, "CORR", row.withWidth (40.0f), juce::Justification::centredLeft, Theme::amber, Theme::mono (10.5f, true), 0.5f);
    const auto bar = row.withTrimmedLeft (42.0f).withTrimmedRight (56.0f).reduced (0.0f, 4.0f);
    g.setColour (Theme::amber.withAlpha (0.1f));
    g.fillRect (bar);
    g.setColour (Theme::amber.withAlpha (0.4f));
    g.fillRect (bar.getCentreX() - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);
    const float x = bar.getX() + bar.getWidth() * (corr + 1.0f) * 0.5f;
    g.setColour ((corr < 0.0f ? Theme::ledRed : Theme::amber));
    g.fillRect (juce::Rectangle<float> (4.0f, bar.getHeight() + 6.0f).withCentre ({ x, bar.getCentreY() }));
    Theme::glowText (g, juce::String (corr, 2), row.withTrimmedLeft (row.getWidth() - 52.0f), juce::Justification::centredRight,
                     corr < 0.0f ? Theme::ledRed : Theme::tuner, Theme::mono (11.5f, true), 0.6f);

    g.setColour (juce::Colours::black.withAlpha (0.22f));
    for (float y = 0.0f; y < screen.getBottom(); y += 3.0f)
        g.fillRect (0.0f, y, screen.getWidth(), 1.0f);
    Theme::glassSheen (g, screen);
}
