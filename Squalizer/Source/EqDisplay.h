#pragma once

#include "PluginProcessor.h"
#include "Theme.h"
#include "BandStrip.h"

// Logo, model name and subtitle, silkscreened on the faceplate.
class HeaderView : public juce::Component
{
public:
    HeaderView();
    void paint (juce::Graphics&) override;

private:
    juce::Image logo;
};

// The phosphor screen: bar analyzer, the EQ curve and one coloured node per band.
//   hold a node: solo, hear only that band (Pro-Q style)    drag it: frequency + gain (Shift = fine; turns an off band on)    wheel: Q
//   double-click a node: band on/off    double-click empty space: the selected EQ band jumps onto the nearest peak
//   right-click a node: type / channel  arrow keys: nudge the selected band (Shift = fine)    Delete: band off
class EqDisplay : public juce::Component
{
public:
    explicit EqDisplay (SqualizerAudioProcessor&);
    ~EqDisplay() override { proc.soloBand = -1; }

    void tick();   // pulls new analyzer frames, called from the editor timer
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    int selected = 0;
    std::function<void (int)> onSelect;

    // EQ MATCH: capture a reference while it plays, then fit free bands so the input's long-term spectrum matches it.
    bool capturingReference = false;
    bool hasReference() const { return refFrames > 10; }
    int matchToReference();   // returns how many bands were placed

private:
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder, bins = fftSize / 2 + 1;
    static constexpr double displayRange = 24.0;   // +/- dB on the curve grid

    juce::Rectangle<float> plot() const;
    float xFor (double freq) const;
    double freqAt (float x) const;
    float yFor (double dbValue) const;
    double dbAt (float y) const;
    float yForSpectrum (double level, double freq) const;
    juce::Point<float> nodePosition (int band) const;
    int bandAt (juce::Point<float>) const;
    double curveDb (double freq, bool sidePath) const;
    double spectrumDbAt (const std::vector<float>& spectrum, double freq) const;

    void analyse (ScopeFifo&, std::vector<float>& smoothed, std::vector<double>* power);
    void updateBars();
    float barLevelFrom (const std::vector<double>& power, int bar) const;

    // Analyzer options, kept in the plugin state: source (POST / PRE / OFF), freeze, and fall speed.
    enum AnalyzerSource { post, pre, off };
    int option (const char* key, int fallback) const { return (int) proc.apvts.state.getProperty (key, fallback); }
    void setOption (const char* key, int value) { proc.apvts.state.setProperty (key, value, nullptr); repaint(); }
    struct Chip { juce::Rectangle<float> area; juce::String text; bool lit; std::function<void()> click; };
    std::vector<Chip> chips;

    // DigiCheck-style analyzer: 31 third-octave bars of 36 segments over a 78 dB range.
    static constexpr int numBars = 31, numSegments = 36;
    static constexpr double barFloorDb = 78.0;
    static double barCentre (int b) { return 1000.0 * std::pow (2.0, (b - 17) / 3.0); }
    std::array<float, numBars> barLevel {}, barPeak {};
    std::array<int, numBars> barHold {};
    std::array<float, numBars> sideLevel {};
    std::vector<double> postPower, sidePower;

    // Node drags are relative to where the band was, so grabbing never makes it jump.
    double dragStartFreq = 0.0, dragStartGain = 0.0;
    void startDrag (int band);
    void nudge (int band, double octaves, double db);

    // Curves are sampled once per screen column and only recomputed when a band changes.
    struct CachedCurve
    {
        sq::BandSettings settings;
        int phase = -1;
        std::vector<float> db;
    };
    void refreshCurves();
    std::vector<double> columnFreqs;
    std::vector<float> columnX, mainDb, sideDb, tubeDb;
    std::array<CachedCurve, sq::numBands> bandCurves;
    sq::TubeSettings tubeShown;
    bool tubeValid = false;
    void setParam (int band, const char* name, double value);
    void showBandMenu (int band);

    SqualizerAudioProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftData, preDb, postDb;
    std::vector<double> sourcePower, refPower, framePower;
    int refFrames = 0, sourceFrames = 0;
    bool wasCapturing = false;

    int dragBand = -1;
    juce::Point<float> mouse;
    bool mouseInside = false;
    juce::Image grid;
};

// Second screen: a goniometer with phosphor persistence, peak/RMS meters and the L/R correlation.
class ScopePanel : public juce::Component
{
public:
    explicit ScopePanel (SqualizerAudioProcessor&);
    void tick();
    void paint (juce::Graphics&) override;

private:
    static constexpr int samples = 4096;
    SqualizerAudioProcessor& proc;
    std::vector<float> l, r;
    juce::Image trace;
    float peakL = 0, peakR = 0, holdL = 0, holdR = 0, rms = 0, corr = 0;
};
