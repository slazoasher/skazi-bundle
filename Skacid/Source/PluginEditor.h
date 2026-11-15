#pragma once

#include "PluginProcessor.h"
#include "../../Common/SkaziUI.h"

// The 16 steps: drag a note up or down, click the octave, gate, accent and slide rows.
// The playing step lights up; steps the live MUTATE changed this loop get a dot.
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (SkacidProcessor& p) : proc (p) {}
    void update(); // from the editor timer: repaints only when something it shows changed
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum Row { NoteRow, OctRow, GateRow, AccRow, SlideRow, NumRows };
    juce::Rectangle<float> cell (int step, int row) const;
    int stepAt (float x) const;
    int rowAt (float y) const;
    void setNote (int step, int note);

    SkacidProcessor& proc;
    skacid::Pattern shown {};
    int shownStep = -2, shownLen = -1, shownKey = -1;
    uint32_t shownMask = 0xffffffffu;
    int dragStep = -1, dragStartNote = 0;
    float dragStartY = 0;
};

// The filter's live cutoff as a glowing bar, so you can watch the envelope, accents, LFO and drift move it
class CutoffView : public juce::Component
{
public:
    explicit CutoffView (SkacidProcessor& p) : proc (p) {}
    void update();
    void paint (juce::Graphics&) override;

private:
    SkacidProcessor& proc;
    float shown = -1.0f, peakHold = 0.0f;
};

// Bass-machine knob: black ribbed skirt, silver cap with a black index line and an acid-green dot, ticks printed on the panel
class TbLook : public skin::MetalLookAndFeel
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
};

class SkacidEditor : public skazi::EditorBase
{
public:
    explicit SkacidEditor (SkacidProcessor&);
    ~SkacidEditor() override;

protected:
    void layout() override;
    void tick() override;
    void paintExtras (juce::Graphics&) override;

private:
    struct Section { juce::Rectangle<int> area; juce::String title; };
    void section (juce::Rectangle<int> area, const juce::String& title) { sections.push_back ({ area, title }); }
    void ink (juce::Rectangle<int> area, const juce::String& text) { inkLabels.push_back ({ area, text }); }

    TbLook tbLook;
    std::vector<skazi::Knob*> allKnobs;
    std::vector<Section> sections, inkLabels;
    juce::Rectangle<int> deck;
    SkacidProcessor& sp;
    StepGrid grid;
    CutoffView cutoffView;

    skazi::ChoiceRow *wave, *lfoShape, *lfoTarget, *distType, *seqRate, *playMode, *fltType, *seqKey;
    skazi::Knob *sub, *tune, *fine, *cutoff, *reso, *envMod, *decay, *accent, *fDrive;
    skazi::Knob *lfoDepth, *drift, *distDrive, *distTone, *distMix, *dlyFb, *dlyMix, *revSize, *revMix, *volume;
    skazi::Knob *seqLen, *swing, *slide, *gateLen, *chance, *mutate, *ratchet, *arpOct;
    skazi::Knob *keyTrack, *fAttack, *ampAtk, *ampDec, *ampSus, *ampRel, *ottDepth, *ottTime, *vca, *glide;
    skazi::ParamToggle* glideOn;
    juce::ComboBox *lfoRate, *dlyTime, *arpMode;
    skazi::Meter* outMeter;

    juce::ComboBox style;
    juce::TextButton generateBtn { "GENERATE" }, mutateBtn { "MUTATE" }, clearBtn { "CLEAR" }, leftBtn { "<" }, rightBtn { ">" };
    juce::OwnedArray<juce::TextButton> toolBtns;
};
