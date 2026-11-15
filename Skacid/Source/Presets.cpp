#include "Presets.h"
#include "PresetTrims.h"

namespace skacid
{
void putPattern (std::map<juce::String, float>& values, const Pattern& p)
{
    for (int i = 0; i < NumSteps; ++i)
    {
        const auto& st = p[(size_t) i];
        values[stepId (i, "note")] = (float) st.note;
        values[stepId (i, "oct")] = (float) st.oct;
        values[stepId (i, "gate")] = (float) st.gate;
        values[stepId (i, "acc")] = st.acc ? 1.0f : 0.0f;
        values[stepId (i, "sld")] = st.sld ? 1.0f : 0.0f;
    }
}

namespace
{
    // The voice every category starts from: a squelchy, slightly dirty 303
    void baseSound (skazi::PresetGen& g)
    {
        g.set ("wave", (float) (g.chance (0.7f) ? Saw : Square));
        g.range ("cutoff", 160, 1100); g.range ("reso", 45, 90); g.range ("envMod", 30, 85);
        g.range ("decay", 140, 900); g.range ("accent", 40, 95); g.range ("fDrive", 5, 50);
        g.range ("slide", 40, 110); g.range ("gateLen", 40, 80);
        g.set ("distType", (float) g.pick ({ Crunch, Crunch, Crunch, Fuzz, Fuzz, Fold, Rectify, Crush })); g.range ("distDrive", 10, 55); g.range ("distTone", 45, 90);
    }

    // 1.1: every preset also rolls the newer controls, so no two in a folder sound alike:
    // filter type, key tracking, a slow filter attack, OTT, sub and the amp envelope
    void flavour (skazi::PresetGen& g, bool keyboard)
    {
        g.set ("fltType", (float) g.pick ({ F24, F24, F24, F24, F18, F18, F12, FBand }));
        if (g.chance (0.4f)) g.range ("keyTrack", 20, 80);
        if (g.chance (0.2f)) g.range ("fAttack", 15, 220);
        if (g.chance (0.4f)) { g.range ("ottDepth", 25, 75); g.range ("ottTime", 15, 60); }
        if (g.chance (0.3f)) g.range ("sub", 15, 55);
        if (keyboard)
        {
            g.range ("ampAtk", 1, 35); g.range ("ampDec", 150, 1500); g.range ("ampSus", 40, 100); g.range ("ampRel", 20, 400);
        }
        else if (g.chance (0.35f)) g.range ("ampRel", 10, 80);
    }

    void fx (skazi::PresetGen& g, float delayChance, float reverbChance)
    {
        if (g.chance (delayChance)) { g.set ("dlyTime", (float) g.pick ({ 3, 11, 13, 8 })); g.range ("dlyFb", 25, 55); g.range ("dlyMix", 10, 28); }
        if (g.chance (reverbChance)) { g.range ("revSize", 30, 70); g.range ("revMix", 8, 22); }
    }

    struct Category { const char* name; int style; std::function<void (skazi::PresetGen&)> sound; };

    const std::vector<Category>& categories()
    {
        static const std::vector<Category> c = {
            { "CLASSIC", patterns::Classic, [] (skazi::PresetGen& g) {
                  g.range ("distDrive", 5, 20); fx (g, 0.3f, 0.2f); } },
            { "SQUELCH", patterns::Squelch, [] (skazi::PresetGen& g) {
                  g.range ("cutoff", 150, 400); g.range ("reso", 78, 95); g.range ("envMod", 65, 95); g.range ("accent", 75, 100);
                  g.range ("decay", 150, 400); fx (g, 0.3f, 0.2f); } },
            { "CORROSIVE", patterns::Mutant, [] (skazi::PresetGen& g) {
                  g.set ("distType", (float) g.pick ({ Fuzz, Fold, Rectify, Crush })); g.range ("distDrive", 50, 85);
                  g.range ("fDrive", 30, 70); g.range ("distTone", 40, 80); g.range ("volume", -6, -2); fx (g, 0.3f, 0.2f); } },
            { "ROLLING", patterns::Rolling, [] (skazi::PresetGen& g) {
                  g.range ("cutoff", 250, 500); g.range ("reso", 50, 72); g.range ("decay", 120, 300); g.range ("gateLen", 40, 60);
                  g.range ("sub", 20, 50); fx (g, 0.15f, 0.1f); } },
            { "EVOLVING", patterns::Classic, [] (skazi::PresetGen& g) {
                  g.range ("mutate", 25, 60); g.range ("chance", 80, 95); g.range ("ratchet", 10, 35); g.range ("drift", 35, 80);
                  g.set ("lfoShape", (float) g.pick ({ LfoSine, LfoTri, LfoSH })); g.set ("lfoRate", (float) g.pick ({ 0, 1, 14, 15 }));
                  g.range ("lfoDepth", 15, 40); fx (g, 0.5f, 0.4f); } },
            { "RAVE", patterns::Rave, [] (skazi::PresetGen& g) {
                  g.set ("wave", (float) Saw); g.range ("distDrive", 35, 65); g.set ("distType", (float) g.pick ({ Crunch, Fuzz }));
                  g.range ("reso", 70, 90); g.range ("ratchet", 0, 25); fx (g, 0.6f, 0.5f); } },
            { "DUB", patterns::Dub, [] (skazi::PresetGen& g) {
                  g.range ("cutoff", 180, 380); g.range ("decay", 500, 1400); g.range ("slide", 80, 200); g.range ("gateLen", 70, 95);
                  g.set ("dlyTime", (float) g.pick ({ 11, 12, 13 })); g.range ("dlyFb", 45, 70); g.range ("dlyMix", 22, 40);
                  g.range ("revSize", 50, 85); g.range ("revMix", 15, 30); } },
            { "WOBBLE", patterns::Rolling, [] (skazi::PresetGen& g) {
                  g.set ("lfoTarget", (float) g.pick ({ ToCutoff, ToCutoff, ToReso, ToDrive })); g.set ("lfoShape", (float) g.pick ({ LfoSine, LfoTri, LfoSaw, LfoSquare }));
                  g.set ("lfoRate", (float) g.pick ({ 2, 3, 7, 8, 6 })); g.range ("lfoDepth", 40, 80); g.range ("envMod", 25, 50); fx (g, 0.3f, 0.2f); } },
        };
        return c;
    }

    enum Bank { SeqBank, ArpBank, SoundBank };
    const char* const bankNames[] = { "SEQUENCER", "ARP", "ACID SOUNDS" };

    juce::String title (const char* flavour)
    {
        const juce::String f (flavour);
        return f.substring (0, 1) + f.substring (1).toLowerCase();
    }

    // SEQUENCER: the flavour's own pattern plays. ARP: the grid is a rhythm for the held keys.
    // ACID SOUNDS: played from the keyboard.
    skazi::Preset make (int bank, const Category& cat, juce::Random& r, const juce::String& name)
    {
        skazi::Preset p { title (cat.name) + " - " + name, bankNames[bank], skazi::defaultValues (paramDefs()) };
        skazi::PresetGen g { r, p.values };
        baseSound (g);
        cat.sound (g);
        flavour (g, bank == SoundBank);
        auto pattern = patterns::generate (r, bank == ArpBank ? g.pick ({ patterns::Rolling, patterns::Classic, patterns::Rave }) : cat.style);
        if (bank == ArpBank)
        {
            g.set ("playMode", (float) ModeArp);
            g.set ("arpMode", (float) g.pick ({ ArpUp, ArpUp, ArpDown, ArpUpDown, ArpUpDown, ArpRandom, ArpPlayed }));
            g.set ("arpOct", (float) g.pick ({ 1, 2, 2, 3 }));
            g.set ("seqRate", (float) g.pick ({ R16, R16, R16, R16T, R32 }));
            for (auto& st : pattern)
            {
                st.note = 0;
                if (st.oct != Mid && ! g.chance (0.3f)) st.oct = Mid; // the keys and the octave range make the melody
            }
        }
        else if (bank == SoundBank)
        {
            g.set ("playMode", (float) ModeMidi);
            g.range ("slide", 50, 120);
            if (g.chance (0.4f)) { g.set ("glideOn", 1); g.range ("glide", 40, 220); }
        }
        g.range ("vca", 35, 85);
        putPattern (p.values, pattern);
        return p;
    }
} // namespace

const std::vector<skazi::Preset>& presets()
{
    static const std::vector<skazi::Preset> list = []
    {
        std::vector<skazi::Preset> l;
        skazi::Preset init { "Init", "FACTORY", skazi::defaultValues (paramDefs()) };
        juce::Random r0 (303);
        putPattern (init.values, patterns::generate (r0, patterns::Classic));
        l.push_back (init);

        int g = 0;
        const auto& cats = categories();
        auto add = [&] (int bank, int perFlavour)
        {
            for (size_t c = 0; c < cats.size(); ++c)
                for (int i = 0; i < perFlavour; ++i, ++g)
                {
                    juce::Random r (5000 + g * 7919);
                    l.push_back (make (bank, cats[c], r, skazi::presetName (g * 3 + 7)));
                }
        };
        add (SoundBank, 4); // 32, first: plain sounds to play from the keys, no sequencer or arp
        add (SeqBank, 8);   // 64
        add (ArpBank, 4);   // 32

        // level-match: the trims were measured from these exact presets
        constexpr size_t numTrims = sizeof (presetTrims) / sizeof (presetTrims[0]);
        for (size_t i = 0; i < l.size() && i < numTrims; ++i)
            l[i].values["volume"] = juce::jlimit (-24.0f, 6.0f, l[i].values["volume"] + presetTrims[i]);
        return l;
    }();
    return list;
}

skazi::Preset randomPreset (juce::Random& rng)
{
    const auto& cats = categories();
    return make (rng.nextInt (3), cats[(size_t) rng.nextInt ((int) cats.size())], rng, "Random");
}
} // namespace skacid
