#include "Presets.h"
#include "Engine.h"

namespace skazon
{
namespace
{
    using Values = std::initializer_list<std::pair<const char*, float>>;
    struct HandPreset { const char* name; Values values; };

    // Values are in parameter units (choice = index). Anything not listed is reset to its default.
    // Tables: 0 BASIC 1 PWM 2 SYNC 3 FOLD 4 FORMANT 5 GROWL 6 FM 7 HARMONIC 8 BITS 9 SPECTRAL 10 PHASE DIST 11 VOX
    // Sources: 2 MOD ENV 3 LFO1 4 LFO2 5 VEL 7 WHEEL 9 RANDOM. Targets: 1 A POS 2 B POS 3 GRAIN POS 4 PITCH 11 GRAIN DENS 12 CUTOFF
    const HandPreset handPresets[] {
        { "INIT", {} },
        { "BASS // Psy Rolling", { { "aOct", -1 }, { "sub", 0.5f }, { "fCut", 300 }, { "fRes", 0.35f }, { "fEnv", 0.45f },
                                   { "e1A", 0.001f }, { "e1D", 0.18f }, { "e1S", 0 }, { "e1R", 0.06f }, { "e2D", 0.12f }, { "e2S", 0 }, { "fxReverb", 0 } } },
        { "BASS // Growl Monster", { { "aTable", 5 }, { "aOct", -1 }, { "bOn", 1 }, { "bTable", 3 }, { "bOct", -1 }, { "bLevel", 0.5f },
                                     { "m1Src", 3 }, { "m1Dst", 1 }, { "m1Amt", 0.5f }, { "m2Src", 3 }, { "m2Dst", 2 }, { "m2Amt", 0.4f },
                                     { "l1Sync", 1 }, { "l1Div", 2 }, { "fCut", 900 }, { "fRes", 0.3f }, { "fDrive", 0.4f }, { "fxDrive", 0.3f }, { "fxReverb", 0.05f } } },
        { "BASS // FM Reese", { { "aTable", 6 }, { "aPos", 0.3f }, { "aUni", 4 }, { "aDet", 0.25f }, { "aOct", -1 }, { "bOn", 1 }, { "bOct", -2 }, { "bLevel", 0.5f },
                                { "m1Src", 4 }, { "m1Dst", 1 }, { "m1Amt", 0.3f }, { "l2Rate", 0.2f }, { "fCut", 1500 }, { "fxChorus", 0.3f } } },
        { "BASS // Sync Snarl", { { "aTable", 2 }, { "aPos", 0.1f }, { "aOct", -1 }, { "m1Src", 2 }, { "m1Dst", 1 }, { "m1Amt", 0.8f }, { "e2D", 0.25f }, { "e2S", 0 },
                                  { "fCut", 2500 }, { "fRes", 0.2f }, { "e1D", 0.3f }, { "e1S", 0.4f }, { "sub", 0.4f } } },
        { "LEAD // Supersaw Gold", { { "aUni", 7 }, { "aDet", 0.35f }, { "bOn", 1 }, { "bOct", 1 }, { "bUni", 5 }, { "bDet", 0.3f }, { "bLevel", 0.4f },
                                     { "fCut", 9000 }, { "e1A", 0.01f }, { "e1R", 0.4f }, { "fxChorus", 0.3f }, { "fxDelay", 0.25f }, { "fxReverb", 0.3f }, { "glide", 0.03f } } },
        { "LEAD // Acid Formant", { { "aTable", 4 }, { "aPos", 0.3f }, { "m1Src", 3 }, { "m1Dst", 1 }, { "m1Amt", 0.45f }, { "l1Sync", 1 }, { "l1Div", 4 },
                                    { "fCut", 2000 }, { "fRes", 0.55f }, { "fEnv", 0.3f }, { "e2D", 0.2f }, { "e2S", 0 }, { "fxDelay", 0.3f }, { "glide", 0.08f } } },
        { "LEAD // Bit Screamer", { { "aTable", 8 }, { "aPos", 0.5f }, { "bOn", 1 }, { "bTable", 2 }, { "bLevel", 0.5f }, { "bSemi", 7 },
                                    { "m1Src", 7 }, { "m1Dst", 1 }, { "m1Amt", 0.5f }, { "fxDrive", 0.4f }, { "fxDelay", 0.2f }, { "fCut", 7000 } } },
        { "PLUCK // Glass Harp", { { "aTable", 7 }, { "aPos", 0.4f }, { "bOn", 1 }, { "bTable", 6 }, { "bPos", 0.2f }, { "bOct", 1 }, { "bLevel", 0.3f },
                                   { "e1A", 0.001f }, { "e1D", 0.45f }, { "e1S", 0 }, { "e1R", 0.4f }, { "fCut", 3000 }, { "fEnv", 0.45f }, { "e2D", 0.3f }, { "e2S", 0 },
                                   { "fxDelay", 0.3f }, { "fxReverb", 0.35f } } },
        { "PLUCK // Spectral Drops", { { "aTable", 9 }, { "m1Src", 9 }, { "m1Dst", 1 }, { "m1Amt", 0.6f }, { "e1D", 0.3f }, { "e1S", 0 }, { "e1R", 0.3f },
                                       { "fxDelay", 0.35f }, { "fxReverb", 0.3f } } },
        { "PAD // Grain Cathedral", { { "aTable", 11 }, { "aLevel", 0.3f }, { "gOn", 1 }, { "gTable", 11 }, { "gPos", 0.4f }, { "gSize", 220 }, { "gDens", 40 },
                                      { "gSpray", 0.5f }, { "gSpread", 0.15f }, { "gLevel", 0.8f }, { "m1Src", 4 }, { "m1Dst", 3 }, { "m1Amt", 0.35f }, { "l2Rate", 0.1f },
                                      { "e1A", 1.2f }, { "e1R", 2.5f }, { "fCut", 6000 }, { "fxChorus", 0.3f }, { "fxReverb", 0.6f }, { "fxSize", 0.9f } } },
        { "PAD // Vox Choir", { { "aTable", 11 }, { "aUni", 4 }, { "aDet", 0.15f }, { "m1Src", 3 }, { "m1Dst", 1 }, { "m1Amt", 0.4f }, { "l1Rate", 0.15f }, { "l1Shape", 1 },
                                { "bOn", 1 }, { "bTable", 11 }, { "bOct", -1 }, { "bLevel", 0.4f }, { "e1A", 0.6f }, { "e1R", 1.5f }, { "fxReverb", 0.5f }, { "fxChorus", 0.4f } } },
        { "PAD // Frozen Harmonics", { { "aTable", 7 }, { "aPos", 0.2f }, { "aLevel", 0.5f }, { "gOn", 1 }, { "gTable", 7 }, { "gSize", 400 }, { "gDens", 30 },
                                       { "gSpray", 0.8f }, { "gPitch", 12 }, { "e1A", 1.5f }, { "e1S", 1 }, { "e1R", 3 }, { "fxReverb", 0.7f }, { "fxSize", 0.95f } } },
        { "TEXTURE // Grain Storm", { { "aOn", 0 }, { "gOn", 1 }, { "gTable", 3 }, { "gSize", 30 }, { "gDens", 150 }, { "gSpray", 1 }, { "gSpread", 7 }, { "gLevel", 1 },
                                      { "m1Src", 3 }, { "m1Dst", 11 }, { "m1Amt", 0.5f }, { "l1Shape", 4 }, { "l1Rate", 6 }, { "m2Src", 4 }, { "m2Dst", 3 }, { "m2Amt", 0.5f },
                                      { "fxDelay", 0.3f }, { "fxReverb", 0.5f } } },
        { "TEXTURE // Glitch Dust", { { "aOn", 0 }, { "gOn", 1 }, { "gTable", 8 }, { "gSize", 15 }, { "gDens", 60 }, { "gSpray", 1 }, { "gSpread", 12 }, { "gLevel", 1 },
                                      { "m1Src", 3 }, { "m1Dst", 12 }, { "m1Amt", 0.4f }, { "l1Shape", 4 }, { "l1Sync", 1 }, { "l1Div", 2 },
                                      { "fCut", 3000 }, { "fRes", 0.5f }, { "fxDelay", 0.4f } } },
        { "FX // Uplift Rise", { { "aTable", 2 }, { "aUni", 6 }, { "aDet", 0.4f }, { "noise", 0.3f }, { "e2A", 8 }, { "e2S", 1 },
                                 { "m1Src", 2 }, { "m1Dst", 4 }, { "m1Amt", 0.5f }, { "m2Src", 2 }, { "m2Dst", 1 }, { "m2Amt", 1 }, { "m3Src", 2 }, { "m3Dst", 12 }, { "m3Amt", 0.4f },
                                 { "fCut", 800 }, { "fxReverb", 0.5f }, { "fxDelay", 0.3f } } },
        { "FX // Downlifter", { { "aTable", 10 }, { "noise", 0.6f }, { "e2D", 6 }, { "e2S", 0 }, { "m1Src", 2 }, { "m1Dst", 4 }, { "m1Amt", 0.6f },
                                { "m2Src", 2 }, { "m2Dst", 12 }, { "m2Amt", 0.5f }, { "e1R", 2 }, { "fxReverb", 0.6f } } },
        { "KEYS // Phase Piano", { { "aTable", 10 }, { "aPos", 0.4f }, { "bOn", 1 }, { "bPos", 0 }, { "bLevel", 0.4f }, { "bOct", 1 }, { "m1Src", 5 }, { "m1Dst", 1 }, { "m1Amt", 0.4f },
                                   { "e1A", 0.002f }, { "e1D", 1.2f }, { "e1S", 0.2f }, { "e1R", 0.5f }, { "fCut", 5000 }, { "fKey", 0.5f }, { "fxChorus", 0.25f }, { "fxReverb", 0.25f } } },
        { "KEYS // FM Bell", { { "aTable", 6 }, { "aPos", 0.6f }, { "bOn", 1 }, { "bTable", 6 }, { "bPos", 0.2f }, { "bOct", 2 }, { "bLevel", 0.3f },
                               { "m1Src", 2 }, { "m1Dst", 1 }, { "m1Amt", 0.4f }, { "e2D", 1 }, { "e2S", 0 }, { "e1D", 2 }, { "e1S", 0 }, { "e1R", 1.5f },
                               { "fxReverb", 0.4f }, { "fxDelay", 0.2f } } },
        { "ARP // Psy Stab", { { "aTable", 3 }, { "aPos", 0.3f }, { "m1Src", 2 }, { "m1Dst", 1 }, { "m1Amt", 0.6f }, { "e2D", 0.15f }, { "e2S", 0 },
                               { "e1D", 0.15f }, { "e1S", 0 }, { "e1R", 0.1f }, { "fCut", 4000 }, { "fEnv", 0.3f }, { "fxDelay", 0.3f }, { "fxDelayFb", 0.5f } } },
        { "SUB // Deep 808", { { "aPos", 0 }, { "aOct", -1 }, { "m1Src", 2 }, { "m1Dst", 4 }, { "m1Amt", 0.1f }, { "e2D", 0.08f }, { "e2S", 0 },
                               { "e1D", 1.5f }, { "e1S", 0 }, { "e1R", 0.3f }, { "fxDrive", 0.2f }, { "fxReverb", 0 } } },
        { "WOBBLE // LFO Bass", { { "aTable", 5 }, { "aOct", -1 }, { "sub", 0.4f }, { "m1Src", 3 }, { "m1Dst", 12 }, { "m1Amt", 0.5f }, { "l1Sync", 1 }, { "l1Div", 4 },
                                  { "fCut", 600 }, { "fRes", 0.4f }, { "m2Src", 3 }, { "m2Dst", 1 }, { "m2Op", 2 }, { "m2Amt", 0.3f }, { "fxDrive", 0.3f }, { "fxReverb", 0 } } },
        { "LEAD // Hyper Vox", { { "aTable", 11 }, { "aUni", 5 }, { "aDet", 0.3f }, { "m1Src", 3 }, { "m1Dst", 1 }, { "m1Amt", 0.4f }, { "l1Sync", 1 }, { "l1Div", 2 },
                                 { "gOn", 1 }, { "gTable", 11 }, { "gLevel", 0.4f }, { "gSize", 60 }, { "gDens", 40 }, { "fxDelay", 0.3f }, { "fxReverb", 0.35f }, { "glide", 0.05f } } },
        { "SEQ // Quantize Steps", { { "aTable", 7 }, { "m1Src", 3 }, { "m1Dst", 1 }, { "m1Op", 4 }, { "m1Amt", 0.8f }, { "l1Shape", 2 }, { "l1Sync", 1 }, { "l1Div", 9 },
                                     { "m2Src", 3 }, { "m2Dst", 12 }, { "m2Op", 4 }, { "m2Amt", 0.3f }, { "fCut", 3000 }, { "fRes", 0.4f }, { "fxDelay", 0.25f } } },
    };


    // Tables
    enum { BASIC, PWM, SYNC, FOLD, FORMANT, GROWL, FM, HARMONIC, BITS, SPECTRAL, PHASEDIST, VOX };

    // Seeded builder for one generated preset.
    struct B
    {
        juce::Random r;
        Preset p;
        int slot = 1;

        explicit B (juce::int64 seed) : r (seed) {}
        float rnd (float a, float b) { return a + (b - a) * r.nextFloat(); }
        int ri (int a, int b) { return a + r.nextInt (b - a + 1); }
        bool chance (float x) { return r.nextFloat() < x; }
        template <typename T> T pick (std::initializer_list<T> l) { return *(l.begin() + r.nextInt ((int) l.size())); }
        void set (const juce::String& id, float v) { p.values.push_back ({ id, v }); }
        void mod (int src, int dst, float amt, int op = opLinear)
        {
            if (slot > numSlots)
                return;
            const juce::String m ("m" + juce::String (slot++));
            set (m + "Src", (float) src);
            set (m + "Dst", (float) dst);
            set (m + "Op", (float) op);
            set (m + "Amt", amt);
        }
        void osc (const char* o, int table, float pos, int oct, int uni, float det, float level = 0.75f)
        {
            const juce::String x (o);
            set (x + "On", 1);
            set (x + "Table", (float) table);
            set (x + "Pos", pos);
            set (x + "Oct", (float) oct);
            set (x + "Uni", (float) uni);
            set (x + "Det", det);
            set (x + "Level", level);
        }
        void env (int e, float a, float d, float s, float rel)
        {
            const juce::String x ("e" + juce::String (e));
            set (x + "A", a); set (x + "D", d); set (x + "S", s); set (x + "R", rel);
        }
        void lfo (int l, int shape, bool sync, int div, float rate = 2.0f)
        {
            const juce::String x ("l" + juce::String (l));
            set (x + "Shape", (float) shape); set (x + "Sync", sync ? 1.0f : 0.0f); set (x + "Div", (float) div); set (x + "Rate", rate);
        }
        void filter (float cut, float res, float envAmt = 0, float drive = 0)
        {
            set ("fCut", cut); set ("fRes", res); set ("fEnv", envAmt); set ("fDrive", drive);
        }
        void fx (float drive, float chorus, float delay, float reverb, int div = 5, float fb = 0.4f)
        {
            set ("fxDrive", drive); set ("fxChorus", chorus); set ("fxDelay", delay); set ("fxReverb", reverb); set ("fxDelayDiv", (float) div); set ("fxDelayFb", fb);
        }
        void grains (int table, float size, float dens, float spray, float spread, float level)
        {
            set ("gOn", 1); set ("gTable", (float) table); set ("gPos", rnd (0, 1)); set ("gSize", size); set ("gDens", dens);
            set ("gSpray", spray); set ("gSpread", spread); set ("gLevel", level);
        }
    };

    // Unique "Adjective Noun" per category.
    juce::String makeName (B& b, const juce::StringArray& adj, const juce::StringArray& noun, juce::StringArray& used)
    {
        for (int tries = 0; tries < 500; ++tries)
        {
            const auto n = adj[b.r.nextInt (adj.size())] + " " + noun[b.r.nextInt (noun.size())];
            if (! used.contains (n)) { used.add (n); return n; }
        }
        return "Variant " + juce::String (used.size() + 1);
    }

    void lfoTweak (B& b, int i)
    {
        b.osc ("a", b.pick ({ GROWL, BASIC, FOLD, SYNC, FM, PWM, SPECTRAL, PHASEDIST }), b.rnd (0, 1), b.pick ({ -1, -1, 0 }), b.ri (1, 4), b.rnd (0.05f, 0.25f));
        if (b.chance (0.6f))
            b.osc ("b", b.pick ({ FOLD, GROWL, BITS, SYNC, BASIC }), b.rnd (0, 1), -1, 1, 0.1f, b.rnd (0.3f, 0.6f));
        b.set ("sub", b.rnd (0, 0.45f));
        b.filter (b.rnd (250, 2500), b.rnd (0.2f, 0.65f), 0, b.rnd (0, 0.5f));
        b.lfo (1, b.ri (0, 4), true, b.pick ({ 1, 2, 3, 4, 5, 6 }));
        b.lfo (2, b.pick ({ 2, 4, 1, 0 }), true, b.pick ({ 8, 9, 10, 11 }));
        b.mod (sLfo1, tCutoff, b.rnd (0.3f, 0.7f), b.pick ({ opLinear, opAbs, opSquare }));
        b.mod (sLfo1, tAPos, b.rnd (-0.7f, 0.7f), b.pick ({ opLinear, opAbs, opFold, opQuantize }));
        if (b.chance (0.6f)) b.mod (sLfo2, tLfo1Rate, b.rnd (0.25f, 0.5f), b.pick ({ opQuantize, opLinear })); // the wobble speeds up and down
        if (b.chance (0.35f)) b.mod (sLfo1, tPitch, b.pick ({ 0.5f, 0.25f, 0.125f }), opQuantize);
        if (b.chance (0.4f)) b.mod (sLfo2, tBPos, b.rnd (0.3f, 0.8f));
        if (b.chance (0.3f)) b.mod (sLfo1, tPan, b.rnd (0.3f, 0.7f));
        b.mod (sWheel, tLfo1Rate, 0.5f);
        b.env (1, 0.003f, 0.3f, 0.9f, 0.12f);
        b.fx (b.rnd (0, 0.45f), 0, b.chance (0.3f) ? 0.15f : 0.0f, b.rnd (0, 0.12f));
        juce::ignoreUnused (i);
    }

    void bass (B& b, int i)
    {
        const int oct = b.pick ({ -1, -1, -2 });
        b.osc ("a", b.pick ({ BASIC, GROWL, SYNC, FOLD, FM, PHASEDIST, PWM, SPECTRAL }), b.rnd (0, 1), oct, b.pick ({ 1, 1, 2, 3 }), b.rnd (0.05f, 0.2f));
        if (b.chance (0.5f))
        {
            b.osc ("b", b.pick ({ BASIC, PWM, FOLD, GROWL, HARMONIC }), b.rnd (0, 1), b.pick ({ oct, oct - 1 }), 1, 0.1f, b.rnd (0.3f, 0.6f));
            b.set ("bFine", b.rnd (-8, 8));
        }
        b.set ("sub", b.rnd (0, 0.6f));
        b.filter (b.rnd (150, 1800), b.rnd (0, 0.5f), b.rnd (0.15f, 0.6f), b.rnd (0, 0.5f));
        b.set ("fKey", 0.3f);
        b.env (2, 0.001f, b.rnd (0.05f, 0.35f), b.rnd (0, 0.3f), 0.1f);
        switch (i % 4)
        {
            case 0:  b.env (1, 0.001f, b.rnd (0.12f, 0.25f), 0, 0.05f); break;   // rolling psy
            case 1:  b.env (1, 0.002f, 0.3f, 0.9f, 0.1f); break;                   // sustained
            case 2:  b.env (1, 0.001f, b.rnd (0.3f, 0.6f), 0.2f, 0.15f); break;   // plucked
            default: b.set ("aUni", 4); b.set ("aDet", b.rnd (0.2f, 0.35f));
                     b.lfo (2, 0, false, 9, b.rnd (0.1f, 0.4f)); b.mod (sLfo2, tAPos, b.rnd (0.2f, 0.4f)); break; // reese
        }
        if (b.chance (0.6f)) b.mod (sEnv2, tAPos, b.rnd (0.2f, 0.6f));
        if (b.chance (0.3f)) b.mod (sVelocity, tCutoff, b.rnd (0.1f, 0.3f));
        if (b.chance (0.3f)) b.set ("glide", b.rnd (0.02f, 0.08f));
        b.fx (b.chance (0.4f) ? b.rnd (0.1f, 0.4f) : 0.0f, i % 4 == 3 ? 0.2f : 0.0f, 0, b.rnd (0, 0.05f));
    }

    void lead (B& b, int)
    {
        b.osc ("a", b.pick ({ BASIC, BASIC, PWM, SYNC, PHASEDIST, HARMONIC, VOX, FORMANT }), b.rnd (0.3f, 1), 0, b.ri (3, 8), b.rnd (0.15f, 0.4f));
        if (b.chance (0.7f))
        {
            b.osc ("b", b.pick ({ BASIC, PWM, HARMONIC, SYNC }), b.rnd (0, 1), b.pick ({ 0, 1, -1 }), b.ri (1, 6), b.rnd (0.1f, 0.35f), b.rnd (0.3f, 0.6f));
            b.set ("bSemi", (float) b.pick ({ 0, 7, 0, 5 }));
        }
        b.filter (b.rnd (2500, 12000), b.rnd (0, 0.3f), b.rnd (0, 0.3f));
        b.env (1, b.rnd (0.002f, 0.03f), 0.5f, b.rnd (0.6f, 1), b.rnd (0.15f, 0.5f));
        b.env (2, 0.001f, b.rnd (0.1f, 0.5f), 0, 0.2f);
        if (b.chance (0.6f)) b.set ("glide", b.rnd (0.02f, 0.12f));
        b.lfo (2, 0, false, 6, b.rnd (4.5f, 6.5f));
        b.mod (sLfo2, tPitch, b.rnd (0.004f, 0.012f)); // vibrato
        b.mod (sWheel, tAPos, b.rnd (0.3f, 0.6f));
        if (b.chance (0.5f)) b.mod (sEnv2, tAPos, b.rnd (-0.4f, 0.4f));
        if (b.chance (0.3f)) b.grains (b.pick ({ VOX, HARMONIC, FORMANT }), b.rnd (40, 150), b.rnd (20, 60), b.rnd (0.2f, 0.6f), 0.1f, b.rnd (0.2f, 0.4f));
        b.fx (b.rnd (0, 0.2f), b.rnd (0, 0.3f), b.rnd (0.15f, 0.35f), b.rnd (0.2f, 0.4f), b.pick ({ 4, 5, 6 }));
    }

    void crazyLead (B& b, int)
    {
        b.osc ("a", b.pick ({ SYNC, FOLD, FM, BITS, GROWL, SPECTRAL, PHASEDIST }), b.rnd (0, 1), 0, b.ri (1, 6), b.rnd (0.1f, 0.4f));
        if (b.chance (0.6f))
        {
            b.osc ("b", b.pick ({ BITS, FM, SYNC, FOLD, FORMANT }), b.rnd (0, 1), b.pick ({ 0, 1 }), b.ri (1, 3), 0.2f, b.rnd (0.3f, 0.7f));
            b.set ("bSemi", (float) b.ri (-12, 12));
        }
        b.filter (b.rnd (1500, 9000), b.rnd (0.1f, 0.6f), b.rnd (0, 0.4f), b.rnd (0.2f, 0.7f));
        b.env (1, 0.002f, 0.4f, b.rnd (0.5f, 1), b.rnd (0.1f, 0.4f));
        b.env (2, 0.001f, b.rnd (0.05f, 0.6f), b.rnd (0, 0.3f), 0.2f);
        b.lfo (1, b.pick ({ 4, 3, 2 }), true, b.pick ({ 0, 1, 2, 3 }));
        b.lfo (2, b.ri (0, 4), true, b.pick ({ 4, 6, 8 }));
        const int count = b.ri (3, 5);
        for (int k = 0; k < count; ++k)
            switch (b.ri (0, 7))
            {
                case 0: b.mod (sEnv2, tPitch, b.rnd (0.1f, 0.5f)); break;
                case 1: b.mod (sLfo1, tAPos, b.rnd (0.4f, 0.9f), opQuantize); break;
                case 2: b.mod (sLfo1, tPitch, b.pick ({ 0.5f, 0.25f }), opQuantize); break;
                case 3: b.mod (sRandom, tBPos, b.rnd (0.5f, 1)); break;
                case 4: b.mod (sLfo2, tCutoff, b.rnd (0.3f, 0.6f), opFold); break;
                case 5: b.mod (sVelocity, tDrive, b.rnd (0.3f, 0.7f)); break;
                case 6: b.mod (sLfo2, tBPitch, b.rnd (0.1f, 0.5f), opFold); break;
                default: b.mod (sEnv2, tAPos, b.rnd (-0.8f, 0.8f), opSquare); break;
            }
        if (b.chance (0.4f)) b.grains (b.pick ({ BITS, FM, SYNC, SPECTRAL }), b.rnd (15, 80), b.rnd (40, 150), b.rnd (0.5f, 1), b.rnd (0, 12), b.rnd (0.3f, 0.6f));
        if (b.chance (0.5f)) b.set ("glide", b.rnd (0.05f, 0.2f));
        b.fx (b.rnd (0.2f, 0.6f), 0, b.rnd (0.2f, 0.45f), b.rnd (0.15f, 0.35f), b.pick ({ 1, 2, 3, 5 }), b.rnd (0.3f, 0.7f));
    }

    void effects (B& b, int i)
    {
        b.osc ("a", b.pick ({ SYNC, SPECTRAL, FM, BASIC, PHASEDIST, VOX }), b.rnd (0, 1), 0, b.ri (2, 8), b.rnd (0.2f, 0.6f));
        b.filter (b.rnd (500, 4000), b.rnd (0.2f, 0.6f));
        b.set ("noise", b.rnd (0.1f, 0.6f));
        switch (i % 5)
        {
            case 0: // riser
                b.env (2, b.rnd (4, 10), 1, 1, 1); b.env (1, 0.5f, 1, 1, 2);
                b.mod (sEnv2, tPitch, b.rnd (0.3f, 1)); b.mod (sEnv2, tCutoff, b.rnd (0.3f, 0.6f)); b.mod (sEnv2, tAPos, 1); break;
            case 1: // downlifter
                b.env (2, 0.001f, b.rnd (3, 8), 0, 1); b.env (1, 0.001f, 8, 0, 2);
                b.mod (sEnv2, tPitch, b.rnd (0.4f, 1)); b.mod (sEnv2, tCutoff, 0.5f); break;
            case 2: // impact
                b.set ("aOct", -2); b.env (1, 0.001f, 2.5f, 0, 2); b.env (2, 0.001f, 0.2f, 0, 0.3f);
                b.mod (sEnv2, tPitch, b.rnd (0.3f, 0.7f)); b.set ("fxSize", 0.95f); b.set ("fxDrive", 0.5f); break;
            case 3: // grain sweep
                b.set ("aOn", 0); b.grains (b.pick ({ SPECTRAL, VOX, FM, FOLD }), b.rnd (20, 300), b.rnd (60, 200), 1, b.rnd (2, 12), 1);
                b.set ("fType", 1); b.lfo (1, 0, false, 9, b.rnd (0.05f, 0.3f)); b.mod (sLfo1, tCutoff, 0.7f); b.mod (sLfo1, tGPos, 0.5f); b.env (1, 1.5f, 1, 1, 3); break;
            default: // laser zap
                b.env (2, 0.001f, b.rnd (0.05f, 0.3f), 0, 0.1f); b.env (1, 0.001f, 0.35f, 0, 0.2f);
                b.mod (sEnv2, tPitch, b.rnd (0.6f, 1)); b.set ("fxDelay", 0.4f); break;
        }
        b.set ("fxReverb", b.rnd (0.3f, 0.7f));
        b.set ("fxDelay", b.rnd (0.1f, 0.4f));
    }

    void arp (B& b, int)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, FOLD, HARMONIC, SPECTRAL, PHASEDIST, FM, SYNC }), b.rnd (0, 1), b.pick ({ 0, 0, -1 }), b.ri (1, 3), b.rnd (0.05f, 0.2f));
        if (b.chance (0.4f)) b.osc ("b", b.pick ({ BASIC, HARMONIC, FM }), b.rnd (0, 1), 1, 1, 0.1f, b.rnd (0.2f, 0.4f));
        b.env (1, 0.001f, b.rnd (0.08f, 0.3f), b.rnd (0, 0.15f), b.rnd (0.05f, 0.2f));
        b.env (2, 0.001f, b.rnd (0.05f, 0.2f), 0, 0.1f);
        b.filter (b.rnd (800, 5000), b.rnd (0.1f, 0.5f), b.rnd (0.2f, 0.5f), b.rnd (0, 0.3f));
        b.mod (sEnv2, tAPos, b.rnd (0.2f, 0.6f));
        b.mod (sVelocity, tCutoff, b.rnd (0.1f, 0.3f));
        if (b.chance (0.4f)) b.mod (sRandom, b.pick ({ (int) tCutoff, (int) tAPos, (int) tPan }), b.rnd (0.2f, 0.5f));
        if (b.chance (0.3f)) { b.lfo (1, 4, true, 2); b.mod (sLfo1, tCutoff, b.rnd (0.2f, 0.4f)); }
        b.fx (b.rnd (0, 0.25f), 0, b.rnd (0.2f, 0.4f), b.rnd (0.1f, 0.3f), b.pick ({ 2, 4, 5, 3 }), b.rnd (0.3f, 0.6f));
    }

    void metal (B& b, int)
    {
        const int oct = b.pick ({ -1, -1, 0, -2 });
        b.osc ("a", b.pick ({ FOLD, GROWL, SYNC, BITS, PWM, FM }), b.rnd (0.2f, 1), oct, b.ri (2, 6), b.rnd (0.1f, 0.3f));
        if (b.chance (0.7f))
        {
            b.osc ("b", b.pick ({ FOLD, GROWL, SYNC, PWM }), b.rnd (0, 1), oct, b.ri (1, 4), 0.2f, b.rnd (0.4f, 0.7f));
            b.set ("bSemi", (float) b.pick ({ 0, 7, -5, 12 }));
        }
        b.set ("sub", b.rnd (0.2f, 0.5f));
        b.filter (b.rnd (1500, 8000), b.rnd (0.1f, 0.4f), b.rnd (0, 0.3f), b.rnd (0.6f, 1));
        if (b.chance (0.2f)) b.set ("fType", 1);
        b.env (1, 0.001f, 0.3f, b.rnd (0.6f, 1), 0.1f);
        b.env (2, 0.001f, b.rnd (0.1f, 0.4f), b.rnd (0, 0.4f), 0.1f);
        b.mod (sEnv2, tAPos, b.rnd (0.2f, 0.5f));
        switch (b.ri (0, 2))
        {
            case 0: b.lfo (1, 3, true, 2); b.mod (sLfo1, tDrive, b.rnd (0.3f, 0.6f)); break;     // chugging gate on the drive
            case 1: b.lfo (1, 4, true, b.pick ({ 2, 4 })); b.mod (sLfo1, tAPos, b.rnd (0.4f, 0.8f), opQuantize); break;
            default: b.mod (sVelocity, tDrive, 0.5f); break;
        }
        if (b.chance (0.4f)) b.mod (sWheel, tCutoff, 0.4f);
        b.fx (b.rnd (0.6f, 1), 0, 0, b.rnd (0, 0.15f));
    }

    void hardstyle (B& b, int i)
    {
        switch (i % 3)
        {
            case 0: // screech lead with a pitch drop
                b.osc ("a", b.pick ({ SYNC, PHASEDIST, PWM, GROWL }), b.rnd (0.2f, 0.9f), 0, b.ri (5, 8), b.rnd (0.3f, 0.5f));
                b.osc ("b", b.pick ({ SYNC, BASIC, PWM }), b.rnd (0, 1), 1, b.ri (3, 6), 0.3f, 0.5f);
                b.env (2, 0.001f, b.rnd (0.04f, 0.12f), 0, 0.1f);
                b.mod (sEnv2, tPitch, b.rnd (0.15f, 0.35f));
                b.mod (sEnv2, tAPos, b.rnd (0.3f, 0.7f));
                b.lfo (1, b.pick ({ 2, 3, 4 }), true, 2); b.mod (sLfo1, tAPos, b.rnd (0.2f, 0.5f));
                b.filter (b.rnd (4000, 12000), b.rnd (0.1f, 0.4f), 0, b.rnd (0.4f, 0.8f));
                b.set ("glide", b.rnd (0.04f, 0.12f));
                b.fx (b.rnd (0.5f, 0.9f), 0, 0.2f, 0.3f, 6, 0.35f);
                break;
            case 1: // reverse bass: swells in, cuts off
            {
                const float swell = b.rnd (0.12f, 0.25f);
                b.osc ("a", b.pick ({ BASIC, GROWL, FOLD }), b.rnd (0.3f, 1), -1, b.ri (1, 3), 0.15f);
                b.set ("sub", b.rnd (0.3f, 0.5f));
                b.env (1, swell, 0.1f, 1, 0.02f);
                b.env (2, swell, 0.1f, 1, 0.02f);
                b.filter (b.rnd (300, 900), b.rnd (0.2f, 0.5f), 0, b.rnd (0.4f, 0.7f));
                b.mod (sEnv2, tCutoff, b.rnd (0.3f, 0.5f));
                b.fx (b.rnd (0.6f, 0.9f), 0, 0, 0);
                break;
            }
            default: // hard supersaw lead
                b.osc ("a", BASIC, 0.66f, 0, 8, b.rnd (0.35f, 0.5f));
                b.osc ("b", b.pick ({ BASIC, PWM }), b.rnd (0.5f, 0.9f), 1, 6, 0.35f, 0.45f);
                b.filter (b.rnd (6000, 14000), 0.1f, 0, b.rnd (0.2f, 0.5f));
                b.env (1, 0.003f, 0.5f, 0.9f, 0.3f);
                b.lfo (2, 0, false, 6, 5.5f); b.mod (sLfo2, tPitch, 0.006f);
                b.set ("glide", 0.03f);
                b.fx (b.rnd (0.4f, 0.7f), 0.2f, 0.25f, 0.35f, 6, 0.35f);
                break;
        }
    }

    // ---- 2026-11 styles. Every one wires MACRO 1-2 (and often 3-4) so the macros do something musical out of the box.
    void macroPair (B& b, int t1, float a1, int t2, float a2)
    {
        b.mod (sMacro1, t1, a1);
        b.mod (sMacro2, t2, a2);
    }
    void fxPlus (B& b, float phaser, float hyper, float ott)
    {
        b.set ("phMix", phaser); b.set ("phRate", b.rnd (0.1f, 1.5f));
        b.set ("hyMix", hyper); b.set ("hyDim", hyper * 0.6f);
        b.set ("ottDepth", ott);
    }

    void pad (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, HARMONIC, VOX, FORMANT, SPECTRAL, PHASEDIST }), b.rnd (0.1f, 0.9f), 0, b.ri (3, 8), b.rnd (0.2f, 0.45f));
        b.osc ("b", b.pick ({ BASIC, HARMONIC, PWM, VOX }), b.rnd (0, 1), b.pick ({ 0, 1, -1 }), b.ri (2, 6), b.rnd (0.15f, 0.4f), b.rnd (0.3f, 0.6f));
        if (i % 3 == 0) b.grains (b.pick ({ VOX, HARMONIC, SPECTRAL }), b.rnd (120, 450), b.rnd (15, 50), b.rnd (0.3f, 0.8f), b.rnd (0, 0.3f), b.rnd (0.3f, 0.6f));
        b.filter (b.rnd (1500, 9000), b.rnd (0, 0.3f));
        b.env (1, b.rnd (0.4f, 2.5f), 1, b.rnd (0.7f, 1), b.rnd (1.2f, 4));
        b.lfo (1, b.pick ({ 0, 1, 6 }), false, 9, b.rnd (0.05f, 0.3f));
        b.lfo (2, 6, false, 9, b.rnd (0.03f, 0.15f));
        b.mod (sLfo1, tAPos, b.rnd (0.2f, 0.5f));
        b.mod (sLfo2, tCutoff, b.rnd (0.1f, 0.3f));
        if (b.chance (0.5f)) b.mod (sLfo2, tPan, b.rnd (0.2f, 0.5f));
        macroPair (b, tCutoff, 0.5f, tAPos, 0.6f);
        b.mod (sMacro3, tGLevel, 0.5f);
        b.fx (0, b.rnd (0.2f, 0.5f), b.rnd (0, 0.25f), b.rnd (0.45f, 0.75f), 6, 0.45f);
        b.set ("fxSize", b.rnd (0.75f, 0.98f));
        fxPlus (b, b.chance (0.3f) ? b.rnd (0.2f, 0.5f) : 0.0f, b.rnd (0, 0.4f), 0);
    }

    void pluck (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, HARMONIC, FM, SYNC, PHASEDIST, FOLD }), b.rnd (0, 1), 0, b.ri (1, 5), b.rnd (0.1f, 0.3f));
        if (b.chance (0.5f)) b.osc ("b", b.pick ({ HARMONIC, FM, BASIC }), b.rnd (0, 1), 1, 1, 0.1f, b.rnd (0.2f, 0.45f));
        b.env (1, 0.001f, b.rnd (0.15f, 0.6f), 0, b.rnd (0.15f, 0.5f));
        b.env (2, 0.001f, b.rnd (0.06f, 0.3f), 0, 0.1f);
        b.filter (b.rnd (400, 2500), b.rnd (0.15f, 0.55f), b.rnd (0.4f, 0.8f));
        b.set ("fKey", 0.6f);
        b.mod (sEnv2, tAPos, b.rnd (0.2f, 0.6f));
        b.mod (sVelocity, tCutoff, b.rnd (0.15f, 0.35f));
        macroPair (b, tCutoff, 0.5f, tFEnv, 0.5f);
        b.fx (0, b.rnd (0, 0.25f), b.rnd (0.2f, 0.45f), b.rnd (0.2f, 0.45f), b.pick ({ 3, 4, 5, 6 }), b.rnd (0.35f, 0.6f));
        juce::ignoreUnused (i);
    }

    void keys (B& b, int i)
    {
        switch (i % 4)
        {
            case 0: b.osc ("a", PHASEDIST, b.rnd (0.2f, 0.6f), 0, 1, 0.1f); b.osc ("b", FM, b.rnd (0.1f, 0.4f), 1, 1, 0.1f, 0.3f); break; // e-piano
            case 1: b.osc ("a", HARMONIC, b.rnd (0.1f, 0.5f), 0, 2, 0.08f); b.osc ("b", HARMONIC, b.rnd (0.3f, 0.8f), 1, 1, 0.1f, 0.35f); break; // organ-ish
            case 2: b.osc ("a", PWM, b.rnd (0.3f, 0.7f), 0, 2, 0.12f); break;                                                            // clav
            default: b.osc ("a", FM, b.rnd (0.3f, 0.8f), 0, 1, 0.1f); b.osc ("b", BASIC, 0, -1, 1, 0.1f, 0.3f); break;                 // dx keys
        }
        b.env (1, 0.002f, b.rnd (0.8f, 2.5f), b.rnd (0.1f, 0.4f), b.rnd (0.3f, 0.8f));
        b.env (2, 0.001f, b.rnd (0.3f, 1.2f), 0, 0.3f);
        b.filter (b.rnd (2500, 7000), b.rnd (0, 0.2f), b.rnd (0.1f, 0.3f));
        b.set ("fKey", 0.5f);
        b.mod (sVelocity, tAPos, b.rnd (0.2f, 0.5f));
        b.mod (sEnv2, tAPos, b.rnd (0.1f, 0.4f));
        macroPair (b, tAPos, 0.5f, tCutoff, 0.4f);
        b.fx (b.rnd (0, 0.15f), b.rnd (0.15f, 0.4f), b.rnd (0, 0.15f), b.rnd (0.15f, 0.35f));
        fxPlus (b, b.chance (0.4f) ? b.rnd (0.2f, 0.5f) : 0.0f, 0, 0);
    }

    void bell (B& b, int)
    {
        b.osc ("a", b.pick ({ FM, HARMONIC, SPECTRAL }), b.rnd (0.3f, 1), b.pick ({ 0, 1 }), 1, 0.1f);
        b.osc ("b", FM, b.rnd (0, 1), b.pick ({ 1, 2 }), 1, 0.1f, b.rnd (0.2f, 0.4f));
        b.set ("bSemi", (float) b.pick ({ 0, 7, 5, 12 }));
        b.set ("bFine", b.rnd (-6, 6));
        b.env (1, 0.001f, b.rnd (1.5f, 4), 0, b.rnd (1, 3));
        b.env (2, 0.001f, b.rnd (0.5f, 2), 0, 0.5f);
        b.mod (sEnv2, tAPos, b.rnd (0.3f, 0.7f));
        b.mod (sEnv2, tBPos, b.rnd (0.2f, 0.5f));
        macroPair (b, tAPos, 0.6f, tBPitch, 0.25f);
        b.fx (0, b.rnd (0, 0.2f), b.rnd (0.2f, 0.4f), b.rnd (0.35f, 0.6f), 6, 0.5f);
    }

    void sub808 (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, BASIC, HARMONIC }), b.chance (0.6f) ? 0.0f : b.rnd (0, 0.3f), b.pick ({ -1, -2 }), 1, 0.1f, 0.85f);
        b.env (1, 0.001f, b.rnd (0.8f, 3), b.rnd (0, 0.3f), b.rnd (0.2f, 0.6f));
        b.env (2, 0.001f, b.rnd (0.04f, 0.12f), 0, 0.05f);
        b.mod (sEnv2, tPitch, b.rnd (0.05f, 0.25f));
        b.set ("glide", b.rnd (0.04f, 0.15f));
        b.set ("mono", i % 2 == 0 ? 1.0f : 0.0f);
        b.filter (b.rnd (400, 3000), b.rnd (0, 0.2f));
        macroPair (b, tDrive, 0.6f, tPitch, 0.2f);
        b.fx (b.rnd (0.1f, 0.6f), 0, 0, 0);
        b.set ("distType", (float) b.pick ({ 0, 2, 1 }));
    }

    void psytrance (B& b, int i)
    {
        if (i % 2 == 0) // rolling bass
        {
            b.osc ("a", b.pick ({ BASIC, SYNC, FOLD, PWM }), b.rnd (0, 0.6f), -1, 1, 0.1f);
            b.set ("sub", b.rnd (0.3f, 0.6f));
            b.filter (b.rnd (180, 600), b.rnd (0.2f, 0.5f), b.rnd (0.35f, 0.7f));
            b.env (1, 0.001f, b.rnd (0.1f, 0.2f), 0, 0.04f);
            b.env (2, 0.001f, b.rnd (0.06f, 0.15f), 0, 0.05f);
            b.mod (sEnv2, tAPos, b.rnd (0.2f, 0.5f));
            macroPair (b, tCutoff, 0.5f, tRes, 0.4f);
            b.fx (b.rnd (0, 0.25f), 0, 0, 0);
        }
        else            // squelchy psy lead / zap
        {
            b.osc ("a", b.pick ({ SYNC, FM, FORMANT, BITS, PHASEDIST }), b.rnd (0, 1), 0, b.ri (1, 3), 0.2f);
            b.filter (b.rnd (800, 3500), b.rnd (0.45f, 0.75f), b.rnd (0.2f, 0.5f), b.rnd (0.2f, 0.5f));
            b.env (1, 0.002f, 0.3f, 0.7f, 0.15f);
            b.lfo (1, b.pick ({ 0, 2, 4 }), true, b.pick ({ 2, 4, 6 }));
            b.mod (sLfo1, tCutoff, b.rnd (0.3f, 0.6f));
            b.mod (sLfo1, tAPos, b.rnd (0.3f, 0.7f));
            b.set ("glide", b.rnd (0.04f, 0.12f));
            macroPair (b, tAPos, 0.6f, tLfo1Rate, 0.5f);
            b.fx (b.rnd (0.1f, 0.35f), 0, b.rnd (0.25f, 0.45f), b.rnd (0.15f, 0.3f), b.pick ({ 3, 5 }), 0.55f);
            fxPlus (b, b.rnd (0, 0.4f), 0, 0);
        }
    }

    void techno (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, SYNC, FOLD, BITS }), b.rnd (0, 1), b.pick ({ -1, 0 }), b.ri (1, 2), 0.1f);
        b.filter (b.rnd (300, 2000), b.rnd (0.3f, 0.75f), b.rnd (0.3f, 0.6f), b.rnd (0.2f, 0.6f));
        b.env (1, 0.001f, b.rnd (0.1f, 0.4f), b.rnd (0, 0.3f), 0.1f);
        b.env (2, 0.001f, b.rnd (0.08f, 0.3f), 0, 0.1f);
        b.lfo (1, b.pick ({ 0, 1, 6 }), true, b.pick ({ 9, 10, 11 }));
        b.mod (sLfo1, tCutoff, b.rnd (0.2f, 0.45f)); // slow filter drift across bars
        if (i % 3 == 0) { b.lfo (2, 4, true, 2); b.mod (sLfo2, tAPos, b.rnd (0.3f, 0.6f)); }
        macroPair (b, tCutoff, 0.6f, tRes, 0.4f);
        b.mod (sMacro3, tDrive, 0.6f);
        b.fx (b.rnd (0.1f, 0.5f), 0, b.rnd (0.15f, 0.4f), b.rnd (0.1f, 0.35f), b.pick ({ 3, 5, 7 }), b.rnd (0.4f, 0.7f));
        b.set ("distType", (float) b.pick ({ 0, 2, 4 }));
    }

    void dubstep (B& b, int i)
    {
        b.osc ("a", b.pick ({ GROWL, FM, FOLD, SYNC, FORMANT, SPECTRAL }), b.rnd (0, 1), -1, b.ri (1, 4), b.rnd (0.1f, 0.25f));
        b.osc ("b", b.pick ({ GROWL, FOLD, FM, BITS }), b.rnd (0, 1), -1, b.ri (1, 3), 0.15f, b.rnd (0.4f, 0.7f));
        b.set ("sub", b.rnd (0.3f, 0.55f));
        b.filter (b.rnd (600, 3000), b.rnd (0.25f, 0.6f), 0, b.rnd (0.3f, 0.8f));
        b.env (1, 0.002f, 0.3f, 1, 0.08f);
        b.lfo (1, b.pick ({ 0, 1, 2, 3 }), true, b.pick ({ 2, 4, 6, 8 }));
        b.mod (sLfo1, tCutoff, b.rnd (0.4f, 0.8f), b.pick ({ opLinear, opAbs }));
        b.mod (sLfo1, tAPos, b.rnd (0.4f, 0.9f));
        b.mod (sLfo1, tBPos, b.rnd (-0.8f, 0.8f));
        if (i % 2 == 0) b.mod (sWheel, tLfo1Rate, 0.5f, opQuantize);
        macroPair (b, tLfo1Rate, 0.5f, tAPos, 0.6f);
        b.mod (sMacro3, tDrive, 0.6f);
        b.fx (b.rnd (0.4f, 0.9f), 0, 0, b.rnd (0, 0.1f));
        b.set ("distType", (float) b.pick ({ 1, 3, 6, 2 }));
        fxPlus (b, 0, b.rnd (0, 0.3f), b.rnd (0.3f, 0.7f));
    }

    void dnb (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, FM, FOLD }), b.rnd (0, 1), -1, b.ri (3, 6), b.rnd (0.2f, 0.4f)); // reese
        b.osc ("b", b.pick ({ BASIC, PWM }), b.rnd (0, 1), -1, b.ri (2, 5), b.rnd (0.2f, 0.35f), b.rnd (0.4f, 0.6f));
        b.set ("bFine", b.rnd (-14, 14));
        b.set ("sub", b.rnd (0.3f, 0.55f));
        b.filter (b.rnd (400, 2500), b.rnd (0.2f, 0.55f), b.rnd (0, 0.3f), b.rnd (0.2f, 0.7f));
        b.env (1, 0.002f, 0.3f, 1, 0.1f);
        b.lfo (1, b.pick ({ 0, 6 }), false, 9, b.rnd (0.1f, 0.6f));
        b.mod (sLfo1, tAPos, b.rnd (0.2f, 0.5f));
        b.mod (sLfo1, tCutoff, b.rnd (0.15f, 0.4f));
        if (i % 2 == 1) { b.set ("fType", 1); b.lfo (2, 0, true, b.pick ({ 4, 6 })); b.mod (sLfo2, tCutoff, b.rnd (0.3f, 0.5f)); } // neuro band sweep
        macroPair (b, tCutoff, 0.5f, tADetune, 0.4f);
        b.mod (sMacro3, tDrive, 0.6f);
        b.fx (b.rnd (0.3f, 0.7f), b.rnd (0, 0.2f), 0, b.rnd (0, 0.1f));
        fxPlus (b, b.rnd (0, 0.5f), 0, b.rnd (0.2f, 0.6f));
    }

    void futureBass (B& b, int)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, SYNC }), b.rnd (0.3f, 0.9f), 0, b.ri (6, 8), b.rnd (0.3f, 0.5f));
        b.osc ("b", b.pick ({ BASIC, PWM, VOX }), b.rnd (0, 1), b.pick ({ 0, 1 }), b.ri (4, 7), b.rnd (0.25f, 0.45f), b.rnd (0.35f, 0.55f));
        b.filter (b.rnd (4000, 12000), b.rnd (0, 0.25f));
        b.env (1, b.rnd (0.003f, 0.05f), 0.6f, b.rnd (0.6f, 0.9f), b.rnd (0.2f, 0.5f));
        b.lfo (1, b.pick ({ 2, 5 }), true, b.pick ({ 4, 6 }));
        b.mod (sLfo1, tALevel, b.rnd (0.3f, 0.6f));      // sidechain-style pump
        b.mod (sLfo1, tBLevel, b.rnd (0.3f, 0.6f));
        b.lfo (2, 0, false, 6, 5.5f); b.mod (sLfo2, tPitch, 0.005f);
        macroPair (b, tCutoff, 0.5f, tAPos, 0.5f);
        b.mod (sMacro3, tALevel, -0.5f);
        b.fx (b.rnd (0, 0.2f), b.rnd (0.2f, 0.45f), b.rnd (0.15f, 0.35f), b.rnd (0.25f, 0.5f), 6, 0.4f);
        fxPlus (b, 0, b.rnd (0.3f, 0.7f), b.rnd (0.2f, 0.5f));
    }

    void synthwave (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, PWM }), b.rnd (0.3f, 0.9f), i % 3 == 0 ? -1 : 0, b.ri (2, 4), b.rnd (0.15f, 0.3f));
        b.osc ("b", b.pick ({ BASIC, PWM }), b.rnd (0, 1), b.pick ({ 0, -1, 1 }), b.ri (1, 3), 0.2f, b.rnd (0.3f, 0.6f));
        b.set ("bFine", b.rnd (-10, 10));
        b.filter (b.rnd (1200, 6000), b.rnd (0.15f, 0.45f), b.rnd (0.1f, 0.4f));
        b.env (1, i % 3 == 2 ? b.rnd (0.3f, 1.2f) : 0.005f, 0.6f, b.rnd (0.5f, 1), b.rnd (0.3f, 1.2f));
        b.env (2, 0.001f, b.rnd (0.2f, 0.8f), b.rnd (0, 0.4f), 0.3f);
        b.lfo (1, 1, false, 9, b.rnd (0.2f, 0.8f)); b.mod (sLfo1, tAPos, b.rnd (0.1f, 0.3f)); // slow PWM
        b.lfo (2, 0, false, 6, b.rnd (4.5f, 6)); b.mod (sLfo2, tPitch, 0.004f);
        macroPair (b, tCutoff, 0.5f, tFEnv, 0.4f);
        b.fx (0, b.rnd (0.35f, 0.7f), b.rnd (0.2f, 0.4f), b.rnd (0.3f, 0.55f), b.pick ({ 5, 6, 7 }), 0.45f);
        fxPlus (b, b.chance (0.3f) ? b.rnd (0.2f, 0.4f) : 0.0f, 0, 0);
    }

    void trance (B& b, int i)
    {
        if (i % 2 == 0) // supersaw / hoover lead
        {
            b.osc ("a", BASIC, b.rnd (0.6f, 0.75f), 0, 8, b.rnd (0.3f, 0.5f));
            b.osc ("b", b.pick ({ BASIC, PWM }), b.rnd (0.4f, 0.9f), 1, 7, b.rnd (0.3f, 0.45f), b.rnd (0.35f, 0.5f));
            b.filter (b.rnd (5000, 14000), b.rnd (0, 0.2f));
            b.env (1, 0.003f, 0.6f, 0.9f, b.rnd (0.3f, 0.6f));
            b.set ("glide", b.rnd (0.02f, 0.06f));
        }
        else            // gated trance pluck
        {
            b.osc ("a", b.pick ({ BASIC, PWM, HARMONIC }), b.rnd (0, 1), 0, b.ri (3, 6), b.rnd (0.2f, 0.35f));
            b.env (1, 0.001f, b.rnd (0.15f, 0.35f), 0, 0.2f);
            b.env (2, 0.001f, b.rnd (0.1f, 0.25f), 0, 0.1f);
            b.filter (b.rnd (600, 2500), b.rnd (0.2f, 0.45f), b.rnd (0.4f, 0.7f));
        }
        b.lfo (2, 0, false, 6, 5.5f); b.mod (sLfo2, tPitch, 0.005f);
        macroPair (b, tCutoff, 0.5f, tADetune, 0.5f);
        b.fx (0, b.rnd (0.1f, 0.3f), b.rnd (0.25f, 0.45f), b.rnd (0.3f, 0.5f), b.pick ({ 5, 3 }), 0.5f);
        fxPlus (b, 0, b.rnd (0.2f, 0.5f), 0);
    }

    void ambient (B& b, int)
    {
        b.osc ("a", b.pick ({ HARMONIC, SPECTRAL, VOX, FORMANT }), b.rnd (0, 1), 0, b.ri (2, 5), b.rnd (0.1f, 0.3f), 0.5f);
        b.grains (b.pick ({ HARMONIC, SPECTRAL, VOX, FM }), b.rnd (200, 800), b.rnd (8, 35), b.rnd (0.4f, 1), b.rnd (0, 7), b.rnd (0.5f, 0.9f));
        if (b.chance (0.4f)) b.set ("gPitch", (float) b.pick ({ 12, 7, -12 }));
        b.filter (b.rnd (2000, 8000), b.rnd (0, 0.25f));
        b.env (1, b.rnd (1.5f, 5), 1, 1, b.rnd (3, 8));
        b.lfo (1, 6, false, 9, b.rnd (0.02f, 0.1f));
        b.lfo (2, 0, false, 9, b.rnd (0.03f, 0.12f));
        b.mod (sLfo1, tGPos, b.rnd (0.3f, 0.6f));
        b.mod (sLfo2, tAPos, b.rnd (0.2f, 0.5f));
        b.mod (sRandom, tPan, b.rnd (0.2f, 0.5f));
        macroPair (b, tGDensity, 0.5f, tCutoff, 0.5f);
        b.mod (sMacro3, tGSpray, 0.6f);
        b.fx (0, b.rnd (0.2f, 0.5f), b.rnd (0.2f, 0.45f), b.rnd (0.6f, 0.85f), 7, 0.6f);
        b.set ("fxSize", b.rnd (0.9f, 1));
    }

    void cinematic (B& b, int i)
    {
        b.osc ("a", b.pick ({ SPECTRAL, PHASEDIST, VOX, FM, FOLD }), b.rnd (0, 1), b.pick ({ -1, -2, 0 }), b.ri (3, 7), b.rnd (0.2f, 0.5f));
        b.osc ("b", b.pick ({ HARMONIC, GROWL, BASIC }), b.rnd (0, 1), -2, 2, 0.2f, b.rnd (0.3f, 0.6f));
        if (i % 2 == 0) b.grains (b.pick ({ SPECTRAL, VOX, FOLD }), b.rnd (80, 400), b.rnd (30, 120), b.rnd (0.5f, 1), b.rnd (0, 12), b.rnd (0.4f, 0.7f));
        b.set ("noise", b.rnd (0, 0.2f));
        b.filter (b.rnd (600, 4000), b.rnd (0.1f, 0.4f), 0, b.rnd (0, 0.4f));
        b.env (1, b.rnd (0.5f, 3), 2, b.rnd (0.6f, 1), b.rnd (2, 6));
        b.env (2, b.rnd (2, 8), 1, 1, 2);
        b.mod (sEnv2, tCutoff, b.rnd (0.3f, 0.6f));
        b.mod (sEnv2, tAPos, b.rnd (0.3f, 0.8f));
        b.lfo (1, 6, false, 9, b.rnd (0.05f, 0.2f)); b.mod (sLfo1, tBPos, 0.4f);
        macroPair (b, tCutoff, 0.6f, tGLevel, 0.6f);
        b.mod (sMacro3, tDrive, 0.5f);
        b.fx (b.rnd (0, 0.3f), b.rnd (0, 0.3f), b.rnd (0.1f, 0.3f), b.rnd (0.5f, 0.8f), 8, 0.55f);
        b.set ("fxSize", 0.95f);
    }

    void vocal (B& b, int i)
    {
        b.osc ("a", b.pick ({ VOX, FORMANT }), b.rnd (0, 1), 0, b.ri (1, 5), b.rnd (0.1f, 0.3f));
        if (b.chance (0.6f)) b.osc ("b", b.pick ({ VOX, FORMANT, HARMONIC }), b.rnd (0, 1), b.pick ({ 0, -1 }), b.ri (1, 3), 0.15f, b.rnd (0.3f, 0.5f));
        if (i % 3 == 1) b.grains (VOX, b.rnd (60, 250), b.rnd (20, 60), b.rnd (0.2f, 0.6f), b.rnd (0, 0.2f), b.rnd (0.3f, 0.6f));
        b.filter (b.rnd (3000, 10000), b.rnd (0, 0.2f));
        b.env (1, b.rnd (0.02f, 0.6f), 0.8f, b.rnd (0.7f, 1), b.rnd (0.3f, 1.5f));
        b.lfo (1, b.pick ({ 0, 1, 2 }), b.chance (0.5f), b.pick ({ 4, 6, 8 }), b.rnd (0.2f, 3));
        b.mod (sLfo1, tAPos, b.rnd (0.3f, 0.7f)); // the vowel moves
        b.lfo (2, 0, false, 6, b.rnd (4.5f, 6)); b.mod (sLfo2, tPitch, 0.006f);
        if (b.chance (0.5f)) b.set ("glide", b.rnd (0.04f, 0.12f));
        macroPair (b, tAPos, 0.7f, tBPos, 0.6f);
        b.fx (0, b.rnd (0.2f, 0.4f), b.rnd (0.1f, 0.35f), b.rnd (0.3f, 0.6f), 6, 0.45f);
    }

    void brass (B& b, int)
    {
        b.osc ("a", b.pick ({ BASIC, PWM }), b.rnd (0.5f, 0.8f), 0, b.ri (2, 5), b.rnd (0.1f, 0.25f));
        b.osc ("b", BASIC, b.rnd (0.4f, 0.8f), b.pick ({ 0, -1 }), b.ri (1, 3), 0.15f, b.rnd (0.3f, 0.5f));
        b.filter (b.rnd (500, 1500), b.rnd (0.05f, 0.3f), b.rnd (0.45f, 0.8f));
        b.env (1, b.rnd (0.02f, 0.08f), 0.4f, b.rnd (0.7f, 0.9f), b.rnd (0.15f, 0.4f));
        b.env (2, b.rnd (0.03f, 0.12f), b.rnd (0.2f, 0.5f), b.rnd (0.3f, 0.6f), 0.3f);
        b.mod (sVelocity, tCutoff, b.rnd (0.2f, 0.4f));
        b.lfo (2, 0, false, 6, 5); b.mod (sLfo2, tPitch, 0.004f);
        macroPair (b, tFEnv, 0.5f, tCutoff, 0.4f);
        b.fx (b.rnd (0, 0.2f), b.rnd (0.1f, 0.3f), 0, b.rnd (0.2f, 0.4f));
    }

    void strings (B& b, int)
    {
        b.osc ("a", BASIC, b.rnd (0.55f, 0.75f), 0, b.ri (5, 8), b.rnd (0.2f, 0.35f));
        b.osc ("b", b.pick ({ BASIC, PWM, HARMONIC }), b.rnd (0.3f, 0.9f), b.pick ({ 0, 1, -1 }), b.ri (3, 6), b.rnd (0.15f, 0.3f), b.rnd (0.3f, 0.5f));
        b.filter (b.rnd (2500, 7000), b.rnd (0, 0.2f));
        b.env (1, b.rnd (0.15f, 0.9f), 1, 0.9f, b.rnd (0.6f, 2));
        b.lfo (2, 0, false, 6, b.rnd (4.5f, 5.8f)); b.mod (sLfo2, tPitch, 0.004f);
        b.lfo (1, 6, false, 9, 0.2f); b.mod (sLfo1, tCutoff, 0.15f);
        macroPair (b, tCutoff, 0.5f, tADetune, 0.5f);
        b.fx (0, b.rnd (0.3f, 0.5f), 0, b.rnd (0.4f, 0.65f));
        fxPlus (b, 0, b.rnd (0.2f, 0.5f), 0);
    }

    void lofi (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, HARMONIC, PHASEDIST, PWM, FM }), b.rnd (0, 0.7f), 0, b.ri (1, 3), 0.1f);
        if (i % 2 == 0) b.osc ("b", HARMONIC, b.rnd (0, 0.5f), 1, 1, 0.1f, 0.25f);
        b.set ("noise", b.rnd (0.03f, 0.1f));
        b.filter (b.rnd (900, 3000), b.rnd (0.05f, 0.25f));
        b.env (1, 0.003f, b.rnd (0.6f, 2), b.rnd (0, 0.4f), b.rnd (0.3f, 0.7f));
        b.lfo (1, 6, false, 9, b.rnd (0.3f, 1.2f));
        b.mod (sLfo1, tPitch, b.rnd (0.003f, 0.008f)); // tape wobble
        b.mod (sRandom, tCutoff, b.rnd (0.05f, 0.15f));
        macroPair (b, tCutoff, 0.5f, tNoise, 0.4f);
        b.fx (b.rnd (0.1f, 0.35f), b.rnd (0.2f, 0.45f), b.rnd (0, 0.2f), b.rnd (0.2f, 0.4f));
        b.set ("distType", 4); // bit crush
        b.set ("distMix", b.rnd (0.2f, 0.5f));
        b.set ("eqHigh", b.rnd (-9, -3));
    }

    void houseOrgan (B& b, int i)
    {
        if (i % 2 == 0) // M1-style organ / piano stab
        {
            b.osc ("a", HARMONIC, b.rnd (0.2f, 0.7f), 0, 1, 0.1f);
            b.osc ("b", b.pick ({ HARMONIC, BASIC }), b.rnd (0, 0.6f), 1, 1, 0.1f, b.rnd (0.3f, 0.5f));
            b.env (1, 0.001f, b.rnd (0.25f, 0.6f), b.rnd (0, 0.3f), 0.15f);
        }
        else            // deep house bass
        {
            b.osc ("a", b.pick ({ BASIC, HARMONIC }), b.rnd (0, 0.4f), -1, 1, 0.1f);
            b.set ("sub", b.rnd (0.2f, 0.5f));
            b.env (1, 0.001f, b.rnd (0.2f, 0.5f), b.rnd (0.2f, 0.6f), 0.1f);
            b.env (2, 0.001f, b.rnd (0.1f, 0.3f), 0, 0.1f);
            b.filter (b.rnd (300, 900), b.rnd (0.1f, 0.35f), b.rnd (0.3f, 0.5f));
        }
        macroPair (b, tCutoff, 0.5f, tAPos, 0.5f);
        b.fx (b.rnd (0, 0.2f), b.rnd (0, 0.3f), b.rnd (0, 0.25f), b.rnd (0.1f, 0.3f), 3, 0.35f);
    }

    void grainTexture (B& b, int i)
    {
        b.set ("aOn", i % 3 == 0 ? 1.0f : 0.0f);
        b.grains (b.pick ({ SPECTRAL, VOX, FM, FOLD, BITS, HARMONIC, FORMANT, PHASEDIST }), b.rnd (10, 600), b.rnd (10, 200), b.rnd (0.2f, 1), b.rnd (0, 12), 1);
        if (b.chance (0.3f)) b.set ("gPitch", (float) b.pick ({ -12, 12, 7, 24 }));
        b.filter (b.rnd (1000, 12000), b.rnd (0.1f, 0.5f));
        b.set ("fType", (float) b.pick ({ 0, 1, 2, 4 }));
        b.env (1, b.rnd (0.01f, 2), 1, 1, b.rnd (0.5f, 4));
        b.lfo (1, b.pick ({ 0, 4, 6, 9 }), b.chance (0.5f), b.pick ({ 2, 4, 6, 9 }), b.rnd (0.1f, 4));
        b.lfo (2, 6, false, 9, b.rnd (0.05f, 0.5f));
        b.mod (sLfo1, b.pick ({ (int) tGDensity, (int) tGSize, (int) tCutoff }), b.rnd (0.3f, 0.6f));
        b.mod (sLfo2, tGPos, b.rnd (0.3f, 0.8f));
        b.mod (sRandom, tGSpray, b.rnd (0.2f, 0.6f));
        macroPair (b, tGDensity, 0.6f, tGSize, 0.6f);
        b.mod (sMacro3, tGPos, 0.7f);
        b.mod (sMacro4, tGSpray, 0.6f);
        b.fx (b.rnd (0, 0.2f), 0, b.rnd (0.1f, 0.4f), b.rnd (0.3f, 0.7f), 4, 0.5f);
    }

    void drone (B& b, int)
    {
        b.osc ("a", b.pick ({ GROWL, FOLD, SPECTRAL, PHASEDIST, HARMONIC }), b.rnd (0, 1), b.pick ({ -1, -2 }), b.ri (3, 8), b.rnd (0.2f, 0.6f));
        b.osc ("b", b.pick ({ HARMONIC, FM, VOX }), b.rnd (0, 1), b.pick ({ -1, 0 }), b.ri (2, 5), b.rnd (0.2f, 0.5f), 0.5f);
        b.set ("bSemi", (float) b.pick ({ 7, 0, 5 }));
        b.grains (b.pick ({ SPECTRAL, FOLD, HARMONIC }), b.rnd (300, 900), b.rnd (10, 40), b.rnd (0.5f, 1), b.rnd (0, 5), b.rnd (0.3f, 0.6f));
        b.filter (b.rnd (400, 3000), b.rnd (0.1f, 0.5f), 0, b.rnd (0, 0.5f));
        b.env (1, b.rnd (2, 6), 1, 1, b.rnd (4, 9));
        b.lfo (1, 6, false, 9, b.rnd (0.02f, 0.08f));
        b.lfo (2, 0, true, b.pick ({ 11, 12, 13 })); // drifts over 4-16 bars
        b.mod (sLfo1, tAPos, b.rnd (0.3f, 0.7f));
        b.mod (sLfo2, tCutoff, b.rnd (0.3f, 0.6f));
        b.mod (sLfo1, tGPos, 0.5f);
        macroPair (b, tCutoff, 0.6f, tDrive, 0.5f);
        b.fx (b.rnd (0, 0.4f), b.rnd (0, 0.3f), b.rnd (0.1f, 0.3f), b.rnd (0.5f, 0.8f), 9, 0.6f);
        b.set ("fxSize", 0.97f);
    }

    void sequence (B& b, int)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, FOLD, HARMONIC, SYNC, FM, BITS }), b.rnd (0, 1), b.pick ({ 0, -1 }), b.ri (1, 3), 0.15f);
        b.env (1, 0.001f, b.rnd (0.15f, 0.5f), b.rnd (0.3f, 0.8f), 0.1f);
        b.filter (b.rnd (800, 4000), b.rnd (0.25f, 0.6f));
        b.lfo (1, b.pick ({ 2, 5, 8 }), true, b.pick ({ 8, 9, 10 }));   // saw / ramp / stairs = a step pattern
        b.lfo (2, 4, true, b.pick ({ 2, 4 }));                           // S&H per step
        b.mod (sLfo1, tPitch, b.pick ({ 0.5f, 0.25f, 0.583f }), opQuantize); // steps in semitone-ish jumps
        b.mod (sLfo2, tCutoff, b.rnd (0.2f, 0.5f));
        b.mod (sLfo2, tAPos, b.rnd (0.2f, 0.6f), opQuantize);
        macroPair (b, tCutoff, 0.5f, tLfo1Rate, 0.5f);
        b.fx (b.rnd (0, 0.3f), 0, b.rnd (0.2f, 0.45f), b.rnd (0.15f, 0.35f), b.pick ({ 3, 5 }), 0.5f);
    }

    // ---- 2026-11-10 styles: lean on warp modes, comb/notch filters, mono legato, the dist types and LFO 3/4.
    void warp (B& b, const char* o, int mode, float amt) { b.set (juce::String (o) + "Warp", (float) mode); b.set (juce::String (o) + "WarpAmt", amt); }

    void hardTechno (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, FOLD, GROWL, SYNC, BITS }), b.rnd (0, 1), b.pick ({ -1, 0 }), b.ri (1, 3), 0.15f);
        warp (b, "a", b.pick ({ (int) wSync, (int) wBendUp, (int) wPwm }), b.rnd (0.2f, 0.7f));
        b.filter (b.rnd (400, 3000), b.rnd (0.3f, 0.65f), b.rnd (0.2f, 0.5f), b.rnd (0.4f, 0.8f));
        b.env (1, 0.001f, b.rnd (0.1f, 0.3f), i % 2 ? 0.0f : 0.6f, 0.05f);
        b.env (2, 0.001f, b.rnd (0.05f, 0.2f), 0, 0.05f);
        b.mod (sEnv2, tAWarp, b.rnd (0.3f, 0.6f));
        b.lfo (3, 4, true, b.pick ({ 2, 4 })); b.mod (sLfo3, tCutoff, b.rnd (0.15f, 0.35f));
        macroPair (b, tCutoff, 0.6f, tAWarp, 0.6f);
        b.mod (sMacro3, tDrive, 0.6f);
        b.set ("distType", (float) b.ri (0, 6));
        b.fx (b.rnd (0.5f, 0.9f), 0, b.rnd (0, 0.2f), b.rnd (0.05f, 0.2f), 3, 0.35f);
        fxPlus (b, 0, 0, b.rnd (0.2f, 0.5f));
    }

    void goa (B& b, int i)
    {
        b.osc ("a", b.pick ({ SYNC, FORMANT, PHASEDIST, FM, SPECTRAL }), b.rnd (0, 1), 0, b.ri (1, 3), 0.15f);
        warp (b, "a", b.pick ({ (int) wSync, (int) wFm, (int) wBendDown }), b.rnd (0.2f, 0.6f));
        b.filter (b.rnd (900, 4000), b.rnd (0.45f, 0.8f), b.rnd (0.2f, 0.5f));
        b.set ("fType", (float) b.pick ({ 0, 0, 1 }));
        b.env (1, 0.002f, 0.4f, b.rnd (0.5f, 0.9f), 0.15f);
        b.env (2, 0.001f, b.rnd (0.1f, 0.4f), 0.1f, 0.1f);
        b.lfo (1, b.pick ({ 0, 1, 2 }), true, b.pick ({ 4, 5, 6, 8 }));
        b.mod (sLfo1, tCutoff, b.rnd (0.25f, 0.5f));
        b.mod (sEnv2, tAWarp, b.rnd (0.3f, 0.7f));
        b.mod (sLfo1, tPan, b.rnd (0.2f, 0.5f));
        if (i % 2) b.set ("glide", b.rnd (0.04f, 0.12f));
        macroPair (b, tCutoff, 0.5f, tRes, 0.4f);
        b.mod (sMacro3, tAWarp, 0.6f);
        b.fx (b.rnd (0.1f, 0.3f), 0, b.rnd (0.25f, 0.45f), b.rnd (0.15f, 0.35f), b.pick ({ 4, 5 }), 0.55f);
        fxPlus (b, b.rnd (0.2f, 0.5f), 0, 0);
    }

    void trap (B& b, int i)
    {
        if (i % 2 == 0)
        {
            b.osc ("a", BASIC, 0, -1, 1, 0); // 808 sine with a pitch dive
            b.set ("sub", b.rnd (0.3f, 0.6f));
            b.env (1, 0.001f, b.rnd (0.8f, 2.5f), 0, 0.3f);
            b.env (2, 0.001f, b.rnd (0.04f, 0.1f), 0, 0.05f);
            b.mod (sEnv2, tPitch, b.rnd (0.08f, 0.2f));
            b.set ("mono", 1); b.set ("glide", b.rnd (0.05f, 0.15f));
            b.set ("distType", (float) b.ri (0, 6));
            b.fx (b.rnd (0.2f, 0.6f), 0, 0, 0);
            macroPair (b, tDrive, 0.6f, tPitch, 0.1f);
        }
        else
        {
            b.osc ("a", b.pick ({ HARMONIC, SPECTRAL, FM, PWM }), b.rnd (0, 1), b.pick ({ 0, 1 }), 1, 0.1f);
            b.env (1, 0.001f, b.rnd (0.3f, 0.8f), 0, b.rnd (0.3f, 0.8f));
            b.filter (b.rnd (2000, 8000), 0.1f);
            b.lfo (2, 0, false, 6, 5); b.mod (sLfo2, tPitch, 0.006f);
            macroPair (b, tCutoff, 0.5f, tAPos, 0.5f);
            b.fx (0, 0, b.rnd (0.2f, 0.4f), b.rnd (0.25f, 0.45f), 4, 0.4f);
        }
    }

    void hyperpop (B& b, int)
    {
        b.osc ("a", b.pick ({ BITS, PWM, SYNC, VOX, FM }), b.rnd (0, 1), b.pick ({ 0, 1 }), b.ri (2, 6), b.rnd (0.2f, 0.4f));
        warp (b, "a", b.pick ({ (int) wQuantize, (int) wBendUp, (int) wRing }), b.rnd (0.3f, 0.8f));
        b.osc ("b", b.pick ({ BASIC, PWM }), b.rnd (0, 1), 1, b.ri (2, 5), 0.3f, 0.4f);
        b.env (1, 0.001f, b.rnd (0.2f, 0.5f), b.rnd (0.3f, 0.7f), 0.15f);
        b.env (2, 0.001f, b.rnd (0.05f, 0.15f), 0, 0.05f);
        b.mod (sEnv2, tPitch, b.pick ({ 0.25f, 0.5f }), opQuantize);
        b.lfo (3, 4, true, 1); b.mod (sLfo3, tAWarp, b.rnd (0.3f, 0.6f), opQuantize);
        b.set ("glide", b.rnd (0.03f, 0.1f));
        macroPair (b, tAWarp, 0.7f, tPitch, 0.5f);
        b.mod (sMacro3, tDrive, 0.6f);
        b.set ("distType", (float) b.ri (0, 6));
        b.fx (b.rnd (0.3f, 0.7f), b.rnd (0, 0.4f), b.rnd (0.15f, 0.35f), b.rnd (0.2f, 0.4f), 2, 0.45f);
        fxPlus (b, 0, b.rnd (0.2f, 0.5f), b.rnd (0.3f, 0.6f));
    }

    void melodicTechno (B& b, int i)
    {
        b.osc ("a", b.pick ({ HARMONIC, PWM, SPECTRAL, PHASEDIST, BASIC }), b.rnd (0, 1), 0, b.ri (2, 5), b.rnd (0.1f, 0.3f));
        b.osc ("b", b.pick ({ HARMONIC, BASIC, VOX }), b.rnd (0, 1), b.pick ({ 0, 1 }), b.ri (1, 3), 0.2f, b.rnd (0.3f, 0.5f));
        b.set ("bSemi", (float) b.pick ({ 7, 3, 0, 10 }));
        b.filter (b.rnd (600, 3500), b.rnd (0.2f, 0.5f), b.rnd (0.2f, 0.5f));
        b.env (1, b.rnd (0.001f, 0.05f), b.rnd (0.3f, 0.9f), b.rnd (0, 0.5f), b.rnd (0.3f, 0.9f));
        b.env (2, 0.001f, b.rnd (0.2f, 0.6f), 0, 0.2f);
        b.lfo (2, 0, true, b.pick ({ 11, 12 })); // slow 4-8 bar cutoff journey
        b.mod (sLfo2, tCutoff, b.rnd (0.3f, 0.5f));
        b.mod (sEnv2, tAPos, b.rnd (0.2f, 0.5f));
        if (i % 3 == 0) b.grains (b.pick ({ HARMONIC, VOX }), b.rnd (80, 300), b.rnd (15, 40), 0.5f, 0.1f, 0.3f);
        macroPair (b, tCutoff, 0.6f, tFEnv, 0.5f);
        b.mod (sMacro3, tAPos, 0.6f);
        b.fx (b.rnd (0, 0.2f), b.rnd (0, 0.3f), b.rnd (0.25f, 0.45f), b.rnd (0.3f, 0.55f), b.pick ({ 3, 4, 5 }), 0.55f);
        fxPlus (b, 0, b.rnd (0, 0.3f), b.rnd (0, 0.3f));
    }

    void progPsy (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, PHASEDIST, SYNC, PWM }), b.rnd (0, 0.7f), -1, 1, 0.1f);
        b.set ("sub", b.rnd (0.2f, 0.5f));
        b.set ("mono", 1);
        b.filter (b.rnd (250, 1200), b.rnd (0.25f, 0.55f), b.rnd (0.3f, 0.6f), b.rnd (0.1f, 0.4f));
        b.env (1, 0.001f, b.rnd (0.1f, 0.2f), 0, 0.05f);
        b.env (2, 0.001f, b.rnd (0.06f, 0.15f), 0, 0.05f);
        b.mod (sEnv2, tAPos, b.rnd (0.2f, 0.5f));
        b.mod (sVelocity, tCutoff, b.rnd (0.1f, 0.25f));
        b.lfo (4, 0, true, 12); b.mod (sLfo4, tFEnv, b.rnd (0.2f, 0.4f)); // the bassline opens over 8 bars
        if (i % 3 == 1) warp (b, "a", wBendUp, b.rnd (0.1f, 0.4f));
        macroPair (b, tCutoff, 0.5f, tFEnv, 0.5f);
        b.mod (sMacro3, tRes, 0.4f);
        b.fx (b.rnd (0, 0.25f), 0, 0, 0);
        fxPlus (b, 0, 0, b.rnd (0.1f, 0.35f));
    }

    void fmTones (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, HARMONIC, FM }), b.rnd (0, 0.4f), 0, 1, 0);
        b.osc ("b", BASIC, 0, b.pick ({ 0, 1, 2 }), 1, 0, b.rnd (0.3f, 0.6f));
        b.set ("bSemi", (float) b.pick ({ 0, 7, 0, 12, 5 }));
        warp (b, "a", b.pick ({ (int) wFm, (int) wFm, (int) wRing }), b.rnd (0.2f, 0.7f));
        b.env (1, b.rnd (0.001f, 0.02f), b.rnd (0.5f, 2.5f), b.rnd (0, 0.4f), b.rnd (0.3f, 1.2f));
        b.env (2, 0.001f, b.rnd (0.2f, 1.5f), b.rnd (0, 0.3f), 0.4f);
        b.mod (sEnv2, tAWarp, b.rnd (0.4f, 0.8f));
        b.mod (sVelocity, tAWarp, b.rnd (0.2f, 0.4f));
        b.filter (b.rnd (4000, 16000), 0.05f);
        if (i % 2) { b.lfo (2, 0, false, 6, 5); b.mod (sLfo2, tPitch, 0.004f); }
        macroPair (b, tAWarp, 0.6f, tBPitch, 0.2f);
        b.mod (sMacro3, tFEnv, 0.4f);
        b.fx (0, b.rnd (0, 0.3f), b.rnd (0.1f, 0.3f), b.rnd (0.2f, 0.45f), 5, 0.4f);
    }

    void warpZone (B& b, int i)
    {
        const int modes[] = { wSync, wBendUp, wBendDown, wPwm, wFm, wRing, wQuantize };
        b.osc ("a", b.pick ({ BASIC, GROWL, FOLD, SPECTRAL, VOX, FORMANT, PHASEDIST }), b.rnd (0, 1), b.pick ({ -1, 0 }), b.ri (1, 6), b.rnd (0.1f, 0.35f));
        b.osc ("b", b.pick ({ BASIC, PWM, HARMONIC }), b.rnd (0, 1), b.pick ({ -1, 0, 1 }), b.ri (1, 3), 0.2f, b.rnd (0.3f, 0.6f));
        warp (b, "a", modes[i % 7], b.rnd (0.2f, 0.6f));
        warp (b, "b", modes[(i + 3) % 7], b.rnd (0.1f, 0.5f));
        b.filter (b.rnd (1000, 9000), b.rnd (0.1f, 0.4f), 0, b.rnd (0, 0.4f));
        b.env (1, 0.003f, 0.4f, b.rnd (0.6f, 1), 0.2f);
        b.lfo (1, b.ri (0, 9), true, b.pick ({ 2, 4, 6, 8 }));
        b.lfo (3, b.ri (0, 9), true, b.pick ({ 9, 10, 11 }));
        b.mod (sLfo1, tAWarp, b.rnd (0.3f, 0.7f), b.pick ({ opLinear, opAbs, opQuantize }));
        b.mod (sLfo3, tBWarp, b.rnd (0.3f, 0.6f));
        macroPair (b, tAWarp, 0.8f, tBWarp, 0.8f);
        b.mod (sMacro3, tCutoff, 0.5f);
        b.mod (sMacro4, tLfo1Rate, 0.5f);
        b.fx (b.rnd (0, 0.4f), b.rnd (0, 0.3f), b.rnd (0.1f, 0.3f), b.rnd (0.1f, 0.35f), 4, 0.4f);
    }

    void monoLead (B& b, int)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, SYNC, FORMANT, HARMONIC }), b.rnd (0.3f, 1), 0, b.ri (1, 4), b.rnd (0.05f, 0.25f));
        b.osc ("b", b.pick ({ BASIC, PWM }), b.rnd (0, 1), b.pick ({ 0, -1 }), 1, 0.1f, b.rnd (0.3f, 0.5f));
        b.set ("bFine", b.rnd (-12, 12));
        b.set ("mono", 1); b.set ("legato", 1); b.set ("glide", b.rnd (0.05f, 0.25f));
        b.filter (b.rnd (1500, 6000), b.rnd (0.2f, 0.55f), b.rnd (0.1f, 0.4f), b.rnd (0, 0.4f));
        b.env (1, 0.003f, 0.5f, b.rnd (0.7f, 1), b.rnd (0.1f, 0.3f));
        b.env (2, 0.001f, b.rnd (0.15f, 0.5f), b.rnd (0, 0.3f), 0.2f);
        b.lfo (2, 0, false, 6, b.rnd (4.5f, 6)); b.mod (sLfo2, tPitch, b.rnd (0.004f, 0.01f));
        b.mod (sAftertouch, tCutoff, 0.4f);
        macroPair (b, tCutoff, 0.6f, tRes, 0.4f);
        b.mod (sMacro3, tDrive, 0.5f);
        b.fx (b.rnd (0.1f, 0.4f), 0, b.rnd (0.2f, 0.4f), b.rnd (0.15f, 0.35f), b.pick ({ 4, 5, 6 }), 0.45f);
    }

    void comb (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, BITS, FOLD, SPECTRAL }), b.rnd (0, 1), b.pick ({ 0, -1 }), b.ri (1, 3), 0.15f);
        if (i % 2) b.set ("noise", b.rnd (0.2f, 0.6f));
        b.set ("fType", (float) (i % 4 == 3 ? fNotch : fComb));
        b.filter (b.rnd (200, 3000), b.rnd (0.5f, 0.9f), b.rnd (-0.3f, 0.4f));
        b.set ("fKey", b.rnd (0.5f, 1));
        b.env (1, 0.001f, b.rnd (0.2f, 1.2f), b.rnd (0, 0.6f), b.rnd (0.2f, 0.8f));
        b.env (2, 0.001f, b.rnd (0.1f, 0.6f), 0, 0.2f);
        b.lfo (1, b.pick ({ 0, 1, 6 }), b.chance (0.5f), b.pick ({ 4, 6, 8, 9 }), b.rnd (0.1f, 2));
        b.mod (sLfo1, tCutoff, b.rnd (0.2f, 0.5f));
        b.mod (sEnv2, tCutoff, b.rnd (0.2f, 0.5f));
        macroPair (b, tCutoff, 0.7f, tRes, 0.5f);
        b.mod (sMacro3, tNoise, 0.5f);
        b.fx (b.rnd (0, 0.3f), b.rnd (0, 0.3f), b.rnd (0.1f, 0.35f), b.rnd (0.2f, 0.45f), 4, 0.4f);
    }

    void chord (B& b, int)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, HARMONIC, VOX }), b.rnd (0.2f, 0.9f), 0, b.ri (3, 7), b.rnd (0.2f, 0.4f));
        b.osc ("b", b.pick ({ BASIC, PWM, HARMONIC }), b.rnd (0, 1), 0, b.ri (2, 5), 0.3f, 0.6f);
        b.set ("bSemi", (float) b.pick ({ 7, 3, 4, 5, 10 })); // one key plays a fifth / minor / major / sus / seventh dyad
        b.filter (b.rnd (1200, 7000), b.rnd (0.1f, 0.4f), b.rnd (0.2f, 0.5f));
        b.env (1, b.rnd (0.001f, 0.08f), b.rnd (0.3f, 1), b.rnd (0.2f, 0.7f), b.rnd (0.2f, 0.8f));
        b.env (2, 0.001f, b.rnd (0.1f, 0.5f), 0, 0.2f);
        b.mod (sEnv2, tAPos, b.rnd (0.1f, 0.4f));
        macroPair (b, tCutoff, 0.6f, tBPitch, 0.05f);
        b.mod (sMacro3, tADetune, 0.6f);
        b.fx (0, b.rnd (0.2f, 0.5f), b.rnd (0.15f, 0.35f), b.rnd (0.25f, 0.5f), 5, 0.4f);
        fxPlus (b, 0, b.rnd (0.2f, 0.5f), b.rnd (0, 0.3f));
    }

    void raveStab (B& b, int i)
    {
        b.osc ("a", b.pick ({ BASIC, PWM, SYNC, HARMONIC, FOLD }), b.rnd (0.3f, 1), 0, b.ri (4, 8), b.rnd (0.25f, 0.5f));
        b.osc ("b", b.pick ({ BASIC, PWM }), b.rnd (0, 1), b.pick ({ 0, 1, -1 }), b.ri (2, 6), 0.3f, 0.6f);
        b.set ("bSemi", (float) b.pick ({ 7, 5, 3, 12, 0 }));
        if (i % 3 == 2) warp (b, "a", wBendDown, b.rnd (0.2f, 0.5f)); // hoover-ish
        b.filter (b.rnd (1500, 8000), b.rnd (0.1f, 0.4f), b.rnd (0.2f, 0.5f), b.rnd (0.2f, 0.6f));
        b.env (1, 0.001f, b.rnd (0.15f, 0.4f), b.rnd (0, 0.2f), b.rnd (0.1f, 0.3f));
        b.env (2, 0.001f, b.rnd (0.05f, 0.2f), 0, 0.1f);
        if (i % 3 == 2) b.mod (sEnv2, tPitch, b.rnd (0.05f, 0.15f));
        macroPair (b, tCutoff, 0.6f, tDrive, 0.5f);
        b.mod (sMacro3, tADetune, 0.6f);
        b.set ("distType", (float) b.ri (0, 6));
        b.fx (b.rnd (0.2f, 0.6f), b.rnd (0, 0.3f), b.rnd (0.15f, 0.35f), b.rnd (0.25f, 0.45f), b.pick ({ 3, 5 }), 0.4f);
        fxPlus (b, 0, b.rnd (0, 0.4f), b.rnd (0.2f, 0.5f));
    }

    void oriental (B& b, int i)
    {
        b.osc ("a", b.pick ({ FORMANT, VOX, HARMONIC, PHASEDIST, FM }), b.rnd (0, 1), 0, b.ri (1, 3), 0.1f);
        if (b.chance (0.5f)) b.osc ("b", b.pick ({ HARMONIC, FM }), b.rnd (0, 1), 1, 1, 0.1f, b.rnd (0.2f, 0.4f));
        b.set ("mono", i % 2 ? 1.0f : 0.0f); b.set ("glide", b.rnd (0.06f, 0.18f)); // slides between the maqam steps
        b.filter (b.rnd (2000, 8000), b.rnd (0.2f, 0.5f), b.rnd (0, 0.3f));
        b.env (1, b.rnd (0.003f, 0.05f), 0.6f, b.rnd (0.4f, 0.9f), b.rnd (0.2f, 0.6f));
        b.env (2, 0.001f, b.rnd (0.1f, 0.4f), 0, 0.2f);
        b.lfo (2, 0, false, 6, b.rnd (5, 7)); b.mod (sLfo2, tPitch, b.rnd (0.006f, 0.015f));
        b.mod (sEnv2, tAPos, b.rnd (0.2f, 0.5f));
        macroPair (b, tAPos, 0.6f, tPitch, 0.0417f); // MACRO 2 = a quarter-tone-ish bend
        b.mod (sMacro3, tCutoff, 0.5f);
        b.fx (b.rnd (0, 0.2f), 0, b.rnd (0.2f, 0.4f), b.rnd (0.25f, 0.5f), b.pick ({ 4, 5, 6 }), 0.45f);
    }

    struct Category
    {
        const char* name;
        int count;
        void (*build) (B&, int);
        juce::StringArray adj, noun;
    };

    const std::vector<Category>& categoryList()
    {
        static const std::vector<Category> categories {
            { "LFO TWEAK", 30, lfoTweak, { "Wobble", "Warp", "Twitch", "Spiral", "Rubber", "Liquid", "Elastic", "Stutter", "Swing", "Bouncing", "Melting", "Morphing", "Jumpy", "Tidal", "Pulsing", "Wonky" },
                                         { "Reactor", "Machine", "Engine", "Snake", "Tornado", "Mutant", "Yoyo", "Wave", "Wub", "Gremlin", "Monster", "Turbine", "Bender", "Spinner", "Wiggle", "Robot" } },
            { "BASS", 35, bass, { "Rolling", "Deep", "Acid", "Dirty", "Fat", "Rubber", "Night", "Psy", "Gritty", "Liquid", "Hollow", "Punchy", "Twisted", "Dark", "Steel", "Toxic" },
                                { "Roller", "Sub", "Growl", "Pulse", "Reese", "Bite", "Thump", "Engine", "Grind", "Stomp", "Rumble", "Snarl", "Groove", "Pump", "Knock", "Machine" } },
            { "LEAD", 35, lead, { "Golden", "Solar", "Crystal", "Neon", "Royal", "Shining", "Silver", "Epic", "Astral", "Bright", "Electric", "Heavenly", "Magic", "Cosmic", "Glowing", "Sacred" },
                                { "Lead", "Anthem", "Saw", "Hero", "Flight", "Melody", "Sky", "Spirit", "Laser", "Comet", "Arrow", "Beam", "Voice", "Crown", "Rider", "Siren" } },
            { "CRAZY LEAD", 35, crazyLead, { "Insane", "Mad", "Psycho", "Glitched", "Broken", "Feral", "Unhinged", "Alien", "Rabid", "Chaotic", "Haywire", "Fried", "Mutated", "Lunatic", "Electric", "Rogue" },
                                           { "Screamer", "Banshee", "Goblin", "Circuit", "Parrot", "Hornet", "Virus", "Chainsaw", "Gremlin", "Android", "Cyborg", "Shrieker", "Raptor", "Glitch", "Wasp", "Squealer" } },
            { "FX", 30, effects, { "Rising", "Falling", "Cosmic", "Nuclear", "Hyper", "Orbital", "Turbo", "Shatter", "Vortex", "Deep", "Laser", "Distant", "Massive", "Warp", "Static", "Solar" },
                                 { "Riser", "Drop", "Impact", "Sweep", "Zap", "Siren", "Launch", "Blast", "Swoosh", "Portal", "Storm", "Charge", "Crash", "Transition", "Pulse", "Beam" } },
            { "ARP", 30, arp, { "Psy", "Bouncy", "Glass", "Crystal", "Digital", "Tiny", "Sparkle", "Rapid", "Plucky", "Hypno", "Fractal", "Morning", "Laser", "Bubble", "Pixel", "Trance" },
                              { "Arp", "Stab", "Pluck", "Ping", "Steps", "Drops", "Chime", "Tick", "Ladder", "Rain", "Ripple", "Sequence", "Blip", "Spark", "Pattern", "Loop" } },
            { "METAL", 30, metal, { "Brutal", "Iron", "Doom", "Molten", "Savage", "Black", "Rusty", "Crushing", "Hellfire", "Shredded", "Barbed", "Scorched", "Ruthless", "Thrash", "Forged", "Wrecking" },
                                  { "Riff", "Chug", "Anvil", "Hammer", "Grinder", "Distortion", "Saw", "Fuzz", "Crusher", "Blade", "Amp", "Inferno", "Razor", "Beast", "Furnace", "Breaker" } },
            { "HARDSTYLE", 25, hardstyle, { "Raw", "Euphoric", "Hard", "Rawstyle", "Screeching", "Defqon", "Warrior", "Fierce", "Stadium", "Rebel", "Kicking", "Gated", "Pitched", "Uptempo", "Massive", "Rave" },
                                          { "Screech", "Reverse", "Kickbass", "Lead", "Anthem", "Punch", "Warrior", "Gate", "Squeal", "Saw", "Hardlead", "Drop", "Roar", "Pitch", "Hymn", "Shout" } },
            { "PAD", 25, pad, { "Warm", "Endless", "Velvet", "Floating", "Silk", "Golden", "Dreamy", "Frozen", "Soft", "Hazy", "Lush", "Distant", "Glowing", "Ocean", "Angel", "Slow" },
                 { "Pad", "Cloud", "Horizon", "Blanket", "Aura", "Choir", "Dawn", "Field", "Nebula", "Sky", "Glow", "Tide", "Haze", "Bloom", "Veil", "Wash" } },
            { "PLUCK", 25, pluck, { "Glass", "Crisp", "Bright", "Short", "Tiny", "Sharp", "Clean", "Pop", "Snappy", "Sparkle", "Wooden", "Plastic", "Liquid", "Shiny", "Metal", "Candy" },
                 { "Pluck", "Pick", "Drop", "Pop", "Ping", "Snap", "Tick", "Note", "Harp", "Koto", "Mallet", "Bubble", "Bead", "Spark", "Tap", "Pin" } },
            { "KEYS", 20, keys, { "Vintage", "Electric", "Soft", "Smooth", "Classic", "Mellow", "Tine", "Warm", "Jazz", "Velvet", "Smoky", "Dusty", "Bright", "Studio", "Late", "Soul" },
                 { "Keys", "Piano", "Rhodes", "Clav", "Organ", "Wurli", "Tines", "Upright", "Lounge", "Chords", "Stage", "Room", "Felt", "Hammer", "Bar", "Session" } },
            { "BELL", 15, bell, { "Crystal", "Temple", "Ice", "Silver", "Glass", "Golden", "Tiny", "Holy", "Frozen", "Star", "Moon", "Morning", "Bright", "Gamelan", "Toy", "Bronze" },
                 { "Bell", "Chime", "Gong", "Tone", "Ring", "Toll", "Glock", "Celesta", "Bowl", "Peal", "Clang", "Cymbal", "Carillon", "Tinkle", "Sparkle", "Halo" } },
            { "SUB 808", 15, sub808, { "Deep", "Heavy", "Trap", "Booming", "Long", "Short", "Gliding", "Dirty", "Clean", "Round", "Massive", "Low", "Thick", "Boomy", "Club", "Rumbling" },
                 { "808", "Sub", "Boom", "Kick", "Slide", "Drop", "Thump", "Rumble", "Bass", "Knock", "Pressure", "Weight", "Floor", "Quake", "Wave", "Punch" } },
            { "PSYTRANCE", 25, psytrance, { "Goa", "Full-On", "Night", "Forest", "Twilight", "Dark", "Hi-Tech", "Morning", "Acid", "Squelchy", "Rolling", "Cosmic", "Shaman", "Fractal", "Tribal", "Alien" },
                 { "Roller", "Squelch", "Zap", "Gallop", "Bassline", "Lead", "Acid", "Twister", "Laser", "Mantra", "Spiral", "Engine", "Mushroom", "Tunnel", "Ritual", "Signal" } },
            { "TECHNO", 20, techno, { "Berlin", "Warehouse", "Industrial", "Minimal", "Hypnotic", "Dark", "Peak", "Detroit", "Raw", "Acid", "Dub", "Underground", "Concrete", "Steel", "Analog", "Bunker" },
                 { "Stab", "Loop", "Pulse", "Drone", "Hit", "Chord", "Line", "Tool", "Groove", "Rumble", "Sequence", "Signal", "Tension", "Grind", "Ritual", "Machine" } },
            { "DUBSTEP", 20, dubstep, { "Filthy", "Riddim", "Heavy", "Brostep", "Wobbly", "Nasty", "Gnarly", "Deep", "Mega", "Dirty", "Robot", "Tearout", "Monster", "Grimy", "Crunchy", "Chunky" },
                 { "Wobble", "Growl", "Yoi", "Roar", "Talker", "Bass", "Monster", "Robot", "Screech", "Drop", "Wub", "Beast", "Machine", "Snarl", "Chainsaw", "Gremlin" } },
            { "DNB NEURO", 20, dnb, { "Neuro", "Liquid", "Jungle", "Rolling", "Tech", "Dark", "Twisted", "Fast", "Sharp", "Cold", "Steel", "Mutant", "Laser", "Urban", "Night", "Toxic" },
                 { "Reese", "Bass", "Hoover", "Roller", "Sweep", "Snarl", "Tear", "Mutant", "Engine", "Tunnel", "Circuit", "Grinder", "Pressure", "Shock", "Spine", "Vortex" } },
            { "FUTURE BASS", 20, futureBass, { "Pastel", "Sunny", "Kawaii", "Dreamy", "Shiny", "Candy", "Pink", "Summer", "Happy", "Emotional", "Glossy", "Lucid", "Electric", "Sweet", "Fresh", "Bubbly" },
                 { "Chord", "Supersaw", "Wave", "Pump", "Swell", "Bloom", "Feels", "Sky", "Heart", "Rush", "Glow", "Lights", "Sparkle", "Flow", "Drop", "Smile" } },
            { "SYNTHWAVE", 20, synthwave, { "Retro", "Neon", "Midnight", "Outrun", "Chrome", "Cyber", "Miami", "Analog", "Sunset", "VHS", "Arcade", "Turbo", "Laser", "Night", "Vapor", "Electric" },
                 { "Drive", "Highway", "Lead", "Bass", "Pad", "Brass", "Arp", "Cruise", "Pulse", "Dream", "Grid", "Runner", "City", "Racer", "Skyline", "Cassette" } },
            { "TRANCE", 20, trance, { "Euphoric", "Uplifting", "Anthem", "Epic", "Classic", "Vocal", "Progressive", "Psy", "Emotional", "Soaring", "Hands-Up", "Ibiza", "Festival", "Morning", "Golden", "Eternal" },
                 { "Supersaw", "Pluck", "Gate", "Lead", "Anthem", "Hoover", "Rise", "Melody", "Hymn", "Flight", "Sky", "Arp", "Theme", "Moment", "Journey", "Wings" } },
            { "AMBIENT", 20, ambient, { "Still", "Floating", "Ethereal", "Calm", "Weightless", "Tidal", "Misty", "Quiet", "Infinite", "Breathing", "Drifting", "Silent", "Slow", "Dawn", "Deep", "Soft" },
                 { "Space", "Drift", "Mist", "Breath", "Ocean", "Haze", "Light", "Meadow", "Echo", "Stream", "Air", "Garden", "Reflection", "Dream", "Snowfall", "Glacier" } },
            { "CINEMATIC", 15, cinematic, { "Epic", "Dark", "Tense", "Massive", "Ominous", "Rising", "Haunted", "Titan", "Distant", "Deep", "Heroic", "Doom", "Brooding", "Shadow", "Storm", "Sacred" },
                 { "Score", "Trailer", "Tension", "Swell", "Horizon", "Abyss", "Hit", "Brass", "Monolith", "Theme", "Dread", "Rumble", "Gate", "Empire", "Battle", "Prophecy" } },
            { "VOCAL", 15, vocal, { "Angelic", "Breathy", "Choir", "Talking", "Formant", "Ghostly", "Robotic", "Soulful", "Human", "Whisper", "Chant", "Gospel", "Tribal", "Airy", "Hollow", "Siren" },
                 { "Voice", "Choir", "Vox", "Aah", "Ooh", "Chant", "Singer", "Mantra", "Whisper", "Hymn", "Prayer", "Call", "Cry", "Talker", "Diva", "Spirit" } },
            { "BRASS", 12, brass, { "Royal", "Funky", "Soft", "Bold", "Analog", "Bright", "Warm", "Fanfare", "Stab", "Jazz", "Big", "Vintage", "Punchy", "Velvet", "Golden", "Majestic" },
                 { "Brass", "Horns", "Section", "Stab", "Fanfare", "Trumpet", "Swell", "Blast", "Call", "Hit", "Band", "Choir", "Riff", "Power", "Section", "Herald" } },
            { "STRINGS", 12, strings, { "Lush", "Analog", "Silk", "Orchestral", "Warm", "Soft", "Wide", "Vintage", "Slow", "Velvet", "Golden", "Emotional", "Grand", "Airy", "Floating", "Tender" },
                 { "Strings", "Ensemble", "Section", "Bows", "Violins", "Cellos", "Chamber", "Pad", "Swell", "Choir", "Legato", "Sweep", "Quartet", "Hall", "Orchestra", "Lines" } },
            { "LO-FI", 15, lofi, { "Dusty", "Tape", "Warm", "Sleepy", "Rainy", "Faded", "Crackly", "Hazy", "Chill", "Cozy", "Vintage", "Late", "Bedroom", "Lazy", "Mellow", "Worn" },
                 { "Keys", "Chords", "Piano", "Tape", "Rhodes", "Loop", "Memory", "Sunday", "Cafe", "Notes", "Cassette", "Study", "Night", "Window", "Dream", "Vinyl" } },
            { "HOUSE ORGAN", 12, houseOrgan, { "Classic", "Deep", "Chicago", "Garage", "Disco", "Jackin", "Soulful", "Club", "Warehouse", "Funky", "Nineties", "Piano", "Acid", "Tech", "Late", "Groovy" },
                 { "Organ", "Stab", "Bass", "Chord", "Piano", "Hit", "Groove", "Jam", "Shuffle", "Bounce", "Keys", "Line", "Vamp", "Pulse", "Move", "Session" } },
            { "GRAIN TEXTURE", 20, grainTexture, { "Granular", "Frozen", "Scattered", "Shimmer", "Swarm", "Crystal", "Broken", "Floating", "Liquid", "Dust", "Sparkling", "Pixel", "Glass", "Microscopic", "Spectral", "Fractured" },
                 { "Cloud", "Swarm", "Dust", "Rain", "Field", "Shards", "Grains", "Mist", "Particles", "Fog", "Static", "Storm", "Spray", "Glitter", "Haze", "Mosaic" } },
            { "DRONE", 12, drone, { "Endless", "Dark", "Ancient", "Infinite", "Deep", "Low", "Massive", "Drifting", "Eternal", "Haunted", "Cosmic", "Underground", "Slow", "Black", "Cavern", "Monastic" },
                 { "Drone", "Hum", "Engine", "Throne", "Void", "Cathedral", "Abyss", "Monolith", "Tunnel", "Ocean", "Core", "Field", "Rumble", "Pulse", "Planet", "Ritual" } },
            { "SEQUENCE", 20, sequence, { "Stepping", "Pulsing", "Robotic", "Hypnotic", "Looping", "Clockwork", "Digital", "Morse", "Binary", "Ticking", "Jumping", "Random", "Spiral", "Cyber", "Rolling", "Pixel" },
                 { "Sequence", "Steps", "Pattern", "Loop", "Machine", "Code", "Grid", "Ladder", "Counter", "Signal", "Ticker", "Matrix", "Runner", "Stairs", "Gears", "Pulse" } },
            { "HARD TECHNO", 25, hardTechno, { "Brutal", "Raw", "Pounding", "Relentless", "Acid", "Industrial", "Distorted", "Warehouse", "Rave", "Schranz", "Gritty", "Heavy", "Dark", "Fast", "Concrete", "Toxic" },
                 { "Stab", "Loop", "Rumble", "Hit", "Grinder", "Pulse", "Driver", "Hammer", "Tool", "Saw", "Siren", "Engine", "Rave", "Punch", "Machine", "Alarm" } },
            { "GOA", 25, goa, { "Goa", "Cosmic", "Shiva", "Ancient", "Sunrise", "Mystic", "Lysergic", "Astral", "Tribal", "Psychedelic", "Kali", "Jungle", "Sacred", "Starry", "Electric", "Spiritual" },
                 { "Squelch", "Acid", "Lead", "Zap", "Gong", "Spiral", "Mantra", "Chant", "Temple", "Twister", "Gate", "Wave", "Vision", "Eye", "Snake", "Lotus" } },
            { "TRAP", 20, trap, { "Hard", "Dark", "Icy", "Gliding", "Heavy", "Gold", "Night", "Drill", "Street", "Cold", "Booming", "Slippery", "Atlanta", "Lit", "Smooth", "Deadly" },
                 { "808", "Bell", "Pluck", "Slide", "Lead", "Key", "Boom", "Flute", "Drop", "Wave", "Chime", "Thump", "Glide", "Bounce", "Knock", "Tone" } },
            { "HYPERPOP", 20, hyperpop, { "Glitter", "Sugar", "Bubble", "Digital", "Shiny", "Chaotic", "Candy", "Cyber", "Hyper", "Crushed", "Pink", "Neon", "Glossy", "Crunchy", "Pixel", "Plastic" },
                 { "Pop", "Lead", "Blip", "Chirp", "Bounce", "Zap", "Voice", "Star", "Pill", "Heart", "Glitch", "Squeak", "Sparkle", "Bubble", "Toy", "Beam" } },
            { "MELODIC TECHNO", 25, melodicTechno, { "Hypnotic", "Deep", "Afterhours", "Emotional", "Dark", "Desert", "Analog", "Cosmic", "Floating", "Mysterious", "Lunar", "Midnight", "Distant", "Rolling", "Shadow", "Slow" },
                 { "Arp", "Pluck", "Line", "Chord", "Journey", "Sequence", "Lead", "Echo", "Pulse", "Horizon", "Signal", "Theme", "Motif", "Tide", "Path", "Orbit" } },
            { "PROG PSY", 25, progPsy, { "Rolling", "Groovy", "Deep", "Night", "Smooth", "Minimal", "Bouncy", "Tight", "Driving", "Liquid", "Clean", "Dark", "Hypnotic", "Morning", "Warm", "Tech" },
                 { "Bassline", "Roller", "Groove", "Pulse", "Line", "Bass", "Driver", "Flow", "Bounce", "Engine", "Knock", "Motion", "Stream", "Wave", "Step", "Ride" } },
            { "FM", 25, fmTones, { "Digital", "Glass", "Metallic", "Bright", "Bell", "Electric", "Crystal", "Steel", "Shimmering", "Classic", "Hollow", "Silver", "Clangy", "Icy", "Retro", "Sharp" },
                 { "Piano", "Bell", "Bass", "Tine", "Mallet", "Pluck", "Organ", "Lead", "Clang", "Marimba", "Keys", "Brass", "Pad", "Gong", "Harp", "Chime" } },
            { "WARP ZONE", 25, warpZone, { "Warped", "Bent", "Twisted", "Folded", "Ringing", "Synced", "Quantized", "Melted", "Stretched", "Mangled", "Phased", "Bending", "Morphing", "Liquid", "Wobbly", "Glitched" },
                 { "Machine", "Shape", "Engine", "Mutant", "Wave", "Signal", "Organism", "Reactor", "Form", "Spiral", "Vortex", "Prism", "Matter", "Tunnel", "Creature", "Lab" } },
            { "MONO LEAD", 20, monoLead, { "Classic", "Gliding", "Vintage", "Fat", "Singing", "Analog", "Smooth", "Bold", "Screaming", "Soaring", "Warm", "Funky", "Prog", "Sharp", "Liquid", "Silky" },
                 { "Lead", "Solo", "Line", "Voice", "Glide", "Saw", "Square", "Whistle", "Hero", "Melody", "Flute", "Cry", "Mono", "Rider", "Talker", "Sync" } },
            { "COMB", 20, comb, { "Resonant", "Metallic", "Hollow", "Tubular", "Ringing", "Plucked", "Breathy", "Plastic", "Steel", "Wooden", "Tuned", "Glass", "Tin", "Strung", "Phasing", "Brass" },
                 { "Comb", "Tube", "String", "Pipe", "Pluck", "Wire", "Chamber", "Resonator", "Harp", "Ring", "Spring", "Bottle", "Bar", "Flute", "Wind", "Echo" } },
            { "CHORD", 20, chord, { "Deep", "Lush", "Soulful", "Dreamy", "Minor", "Major", "Jazzy", "Warm", "House", "Garage", "Velvet", "Emotional", "Bright", "Soft", "Rich", "Smooth" },
                 { "Chord", "Stab", "Hit", "Dyad", "Fifth", "Sus", "Seventh", "Voicing", "Progression", "Pad", "Keys", "Swell", "Block", "Pluck", "Shot", "Wash" } },
            { "RAVE STAB", 25, raveStab, { "Oldschool", "Nineties", "Hardcore", "Breakbeat", "Jungle", "Rave", "Hoover", "Acid", "Euphoric", "Warehouse", "Piano", "Classic", "Massive", "Mentasm", "Gabber", "Strobe" },
                 { "Stab", "Hoover", "Hit", "Chord", "Siren", "Blast", "Horn", "Shot", "Riff", "Organ", "Brass", "Rush", "Alarm", "Scream", "Signal", "Dream" } },
            { "ORIENTAL", 25, oriental, { "Desert", "Golden", "Arabic", "Persian", "Ancient", "Mystic", "Silk", "Spice", "Sahara", "Bazaar", "Saffron", "Hijaz", "Nile", "Oasis", "Mediterranean", "Tribal" },
                 { "Oud", "Ney", "Lead", "Flute", "Snake", "Voice", "Melody", "Kanun", "Bouzouki", "Saz", "Dance", "Scale", "Wind", "Charm", "Caravan", "Prayer" } },
        };
        return categories;
    }

    std::vector<Preset> generate()
    {
        std::vector<Preset> out;
        int c = 0;
        for (const auto& cat : categoryList())
        {
            juce::StringArray used;
            for (int i = 0; i < cat.count; ++i)
            {
                B b (7919 * (c + 1) + 104729 * i);
                cat.build (b, i);
                b.p.name = juce::String (cat.name) + " // " + makeName (b, cat.adj, cat.noun, used);
                out.push_back (std::move (b.p));
            }
            ++c;
        }
        return out;
    }
}

const juce::StringArray& generatedCategories()
{
    static const juce::StringArray names = []
    {
        juce::StringArray n;
        for (const auto& c : categoryList())
            n.add (c.name);
        return n;
    }();
    return names;
}

Preset randomPreset (int category, juce::int64 seed)
{
    const auto& cat = categoryList()[(size_t) juce::jlimit (0, (int) categoryList().size() - 1, category)];
    B b (seed);
    cat.build (b, b.r.nextInt (1000));
    juce::StringArray used;
    b.p.name = juce::String (cat.name) + " // " + makeName (b, cat.adj, cat.noun, used) + " *";
    return b.p;
}

const std::vector<Preset>& allPresets()
{
    static const std::vector<Preset> list = []
    {
        std::vector<Preset> v;
        for (const auto& h : handPresets)
        {
            Preset p { h.name, {} };
            for (auto [id, value] : h.values)
                p.values.push_back ({ id, value });
            v.push_back (std::move (p));
        }
        for (auto& p : generate())
            v.push_back (std::move (p));
        return v;
    }();
    return list;
}
}
