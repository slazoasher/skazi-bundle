#include "Presets.h"
#include "PresetTrims.h"
#include "Engine.h"

// 555 factory presets, built by seeded recipes (one per category) so every patch is a deliberate sound, not noise.
// Values are normalised 0..1; choices are index / (count - 1). New categories or presets go at the END (hosts store indices).
namespace skyz
{
    namespace
    {
        const char* patterns[][3] {
            { "PSY GALLOP", ".xxx.xxx.xxx.xxx", "0000000000000000" }, { "PSY GALLOP OCT", ".xxA.xxA.xxA.xxA", "000o000o000o000o" }, { "PSY TRIPLET", "xxxxxxxxxxxxxxxx", "00o00o00o00o00o0" },
            { "ROLLING 16", "AxxxAxxxAxxxAxxx", "0000000000000000" }, { "OFFBEAT", "..x...x...x...x.", "0000000000000000" }, { "OFFBEAT OCT", "..x...A...x...A.", "000000o000000000" },
            { "TECHNO STAB", "x..x..x...x..x..", "0000000000000000" }, { "TECHNO DRIVE", "x.xAx.xxx.xAx.xx", "0000000000000000" }, { "HOUSE ORGAN", "..x.x..x..x.x..x", "0000000000000000" },
            { "HOUSE BOUNCE", "x..xx..x.xx..x.x", "000o0000000o0000" }, { "BROKEN", "x..x..x...x..x..", "0000000000000000" }, { "STUTTER", "xx..xx..xxx.x.x.", "0000000000000000" },
            { "TRANCE GATE", "AxxxxxxxAxxxxxxx", "0000000000000000" }, { "ACID 1", "xAxxAx.xAxxAx.xA", "00o0000d00o00000" }, { "ACID 2", "x.Axx.Ax.xAx.Axx", "0o0000d00o000d00" },
            { "FIFTHS", "xxxxxxxxxxxxxxxx", "0707070707070707" }, { "OCTAVE JUMP", "xxxxxxxxxxxxxxxx", "0o0o0o0o0o0o0o0o" }, { "MINOR WALK", "xxxxxxxxxxxxxxxx", "0035703570357035" },
            { "MAJOR WALK", "xxxxxxxxxxxxxxxx", "0047704770477047" }, { "LONG TIES", "x---x---x-x-x---", "0000000000000000" } };
        constexpr int numPatterns = 20;

        struct Cat { const char* name; int count; const char* words[12]; };
        const Cat cats[] {
            { "PSY LEAD", 45, { "Scream", "Laser", "Spear", "Siren", "Blade", "Flare", "Beam", "Comet Tail", "Sting", "Howl", "Razor", "Shriek" } },
            { "PSY BASS", 45, { "Gallop", "Engine", "Pressure", "Core", "Thrust", "Piston", "Pulse", "Hammer", "Drive", "Motor", "Kickback", "Grind" } },
            { "ROLLING BASS", 35, { "Roller", "Spin", "Wheel", "Orbit", "Cycle", "Tumble", "Loop", "Rotor", "Turbine", "Vortex", "Drum", "Gyro" } },
            { "ACID", 30, { "Acid", "Squelch", "Burn", "Melt", "Corrode", "Fizz", "Bite", "Etch", "Venom", "Toxin", "Sour", "Sizzle" } },
            { "TECHNO STAB", 35, { "Stab", "Hit", "Chord", "Punch", "Slam", "Jab", "Strike", "Shard", "Block", "Rack", "Pillar", "Stamp" } },
            { "TECHNO RUMBLE", 30, { "Rumble", "Abyss", "Tremor", "Undertow", "Crater", "Quake", "Magma", "Cavern", "Deep Field", "Sub Space", "Fault", "Basalt" } },
            { "HOUSE ORGAN", 30, { "Organ", "Chapel", "Choir Loft", "Stops", "Drawbar", "Vox", "Hall Organ", "Bass Organ", "Pipe", "Sunday", "Gospel", "Club Organ" } },
            { "HOUSE CHORD", 30, { "Chord", "Glow", "Warm Up", "Sunset", "Lounge", "Velvet", "Bounce", "Memory", "Soul", "Disco", "Uplift", "Garage" } },
            { "DEEP PAD", 35, { "Horizon", "Drift", "Ocean", "Calm", "Halo", "Veil", "Dream", "Haze", "Bloom", "Floating", "Dusk", "Tide" } },
            { "STAR PAD", 35, { "Star Dust", "Constellation", "Galaxy", "Aurora", "Starlight", "Twinkle", "Celestial", "Zodiac", "Milky Way", "Supernova", "Cosmos", "Starfield" } },
            { "PLUCK", 35, { "Pluck", "Pick", "Drop", "Ping", "Spark", "Pop", "Flick", "Tick", "Glint", "Pin", "Snap", "Droplet" } },
            { "ARP SEQ", 40, { "Sequence", "Run", "Cascade", "Ladder", "Stairs", "Pattern", "Motion", "Chase", "Circuit", "Steps", "Spiral", "Relay" } },
            { "BELL", 25, { "Bell", "Chime", "Gong", "Glass", "Crystal", "Temple", "Tower", "Ring", "Metal", "Carillon", "Bowl", "Hammer Bell" } },
            { "KEYS", 25, { "Keys", "Piano", "Tine", "Clav", "Electric", "Rhodes Dust", "Mallet", "Felt", "Toy", "Lounge Keys", "Studio", "Upright" } },
            { "ATMOS", 30, { "Atmos", "Void", "Wind", "Fog", "Radio", "Signal", "Static", "Ghost", "Desert", "Dark Matter", "Solar Wind", "Echo" } },
            { "FX RISER", 25, { "Riser", "Launch", "Lift Off", "Ascend", "Warp", "Rocket", "Sweep", "Climb", "Countdown", "Ignition", "Boost", "Escape" } },
            { "CRAZY", 25, { "Alien", "Chaos", "Mutant", "Glitch", "Wormhole", "Anomaly", "Paradox", "Freak", "Strange", "Quantum", "Bizarre", "Weird" } },
        };
        constexpr int numCats = (int) (sizeof (cats) / sizeof (cats[0]));
        const char* bodies[] { "Andromeda", "Orion", "Vega", "Sirius", "Nova", "Pulsar", "Quasar", "Nebula", "Lunar", "Solar", "Astral", "Zenith", "Polaris", "Rigel",
                               "Altair", "Deneb", "Kepler", "Halley", "Titan", "Europa", "Io", "Callisto", "Phobos", "Saturn", "Neptune", "Pluto", "Cassini", "Lyra", "Draco", "Cygnus",
                               "Antares", "Betelgeuse", "Capella", "Hydra", "Pegasus", "Perseus", "Aquila", "Carina", "Gemini", "Sagittarius" };
        constexpr int numBodies = (int) (sizeof (bodies) / sizeof (bodies[0]));

        struct B // recipe helper
        {
            Preset pr; Rng r;
            explicit B (uint32_t seed) : r (seed * 2654435761u + 12345u) {}
            void set (int p, float x) { pr.values.push_back ({ p, juce::jlimit (0.0f, 1.0f, x) }); }
            void ch (int p, int i) { const int n = paramDefs[p].steps; set (p, n > 1 ? (float) juce::jlimit (0, n - 1, i) / (float) (n - 1) : 0.0f); }
            void lst (int p, int i, int n) { set (p, (float) juce::jlimit (0, n - 1, i) / (float) (n - 1)); }
            float u (float a, float b) { return a + r.uni() * (b - a); }
            int pick (int n) { return (int) (r.uni() * (float) n) % n; }
            bool chance (float c) { return r.uni() < c; }
            void env (int base, float a, float h, float d, float s, float rel) { set (base + 2, a); set (base + 3, h); set (base + 4, d); set (base + 5, s); set (base + 6, rel); }
            void slot (int i, int src, int dst, float amt) { ch (P_m1Src + (i - 1) * 3, src); ch (P_m1Dst + (i - 1) * 3, dst); set (P_m1Amt + (i - 1) * 3, 0.5f + amt * 0.5f); }
            void arp (int pattern, int mode, int rate, int oct, float gate)
            {
                ch (P_arpOn, 1); ch (P_aPat, pattern); ch (P_aMode, mode); lst (P_aRate, rate, numDivs); lst (P_aOct, oct, 4); set (P_aGate, gate);
                pr.lanes = lanesFromPattern (pattern);
            }
            void fx (float dly, int dlyType, int dlyDiv, float rev, int revType, float revDecay)
            {
                set (P_yMix, dly); ch (P_yType, dlyType); lst (P_yTimeL, dlyDiv, numDivs); lst (P_yTimeR, juce::jmin (16, dlyDiv + (chance (0.5f) ? 0 : 1)), numDivs);
                set (P_rMix, rev); ch (P_rType, revType); set (P_rDecay, revDecay);
            }
        };

        // B's octave knob: 7 positions, index 3 = 0
        void oct (B& b, int o) { b.lst (P_bOct, o + 3, 7); }

        // Shuffles EVERY sound parameter over a wide, still-musical range. wild 0..1 widens the odd stuff (filters, matrix, FX).
        void vary (B& b, float wild)
        {
            // star field
            b.ch (P_cons, b.pick (6)); b.set (P_aPart, b.u (0.15f, 1.0f)); b.set (P_aTilt, b.u (0.2f, 0.85f)); b.set (P_aOdd, b.u (0.1f, 0.9f));
            b.set (P_aMorph, b.u (0, 1)); b.set (P_aBlur, b.u (0, 0.3f + 0.5f * wild)); b.set (P_aShimmer, b.u (0, 0.9f));
            b.set (P_aSpread, b.chance (0.3f + 0.3f * wild) ? b.u (0.05f, 0.7f) : b.u (0, 0.05f)); b.set (P_aLvl, b.u (0.2f, 0.9f));
            b.ch (P_aOn, b.chance (0.85f) ? 1 : 0); b.ch (P_aFreeze, b.chance (0.1f) ? 1 : 0);
            // nebula
            b.ch (P_bOn, b.chance (0.88f) ? 1 : 0); b.ch (P_bWave, b.pick (7)); b.set (P_bUni, b.u (0, 0.8f)); b.set (P_bDet, b.u (0.05f, 0.6f));
            b.set (P_bPw, b.u (0, 1)); b.set (P_bFm, b.chance (0.25f + 0.3f * wild) ? b.u (0.05f, 0.7f) : 0); b.set (P_bSync, b.chance (0.15f + 0.3f * wild) ? b.u (0.1f, 0.8f) : 0);
            b.set (P_bPan, b.u (0.2f, 1)); b.set (P_bLvl, b.u (0.35f, 0.85f)); b.lst (P_bOct, 2 + b.pick (3), 7);
            { const int semis[] { 0, 0, 0, 0, 0, 7, 12, 5, -5, -12 }; b.set (P_bSemi, 0.5f + (float) semis[b.pick (10)] / 24.0f); }
            b.set (P_bFine, b.u (0.42f, 0.58f));
            // moons
            b.set (P_subLvl, b.chance (0.4f) ? b.u (0.1f, 0.45f) : 0); b.lst (P_subShape, b.pick (3), 3); b.lst (P_subOct, b.pick (2), 2);
            const bool nz = b.chance (0.35f + 0.3f * wild);
            b.set (P_noise, nz ? b.u (0.02f, 0.3f) : 0); b.lst (P_nType, b.pick (5), 5); b.set (P_nColor, b.u (0.15f, 0.85f));
            b.set (P_ring, b.chance (0.12f + 0.3f * wild) ? b.u (0.1f, 0.5f) : 0);
            b.ch (P_drift, b.pick (4));
            // filter
            static const int common[] { 0, 0, 1, 2, 3, 3, 4, 5 };
            b.ch (P_f1Type, b.chance (0.65f - 0.4f * wild) ? common[b.pick (8)] : b.pick (12));
            b.ch (P_fRoute, b.chance (0.6f) ? 0 : 1 + b.pick (2));
            {   // filter 2 gets a range that cannot cancel filter 1: HP stays low, LP stays high, BP in the middle
                const int f2 = b.chance (0.5f) ? 0 : 1 + b.pick (3);
                b.ch (P_f2Type, f2);
                b.set (P_f2Cut, f2 == 1 ? b.u (0.02f, 0.3f) : f2 == 2 ? b.u (0.35f, 0.62f) : b.u (0.62f, 0.95f));
            } b.set (P_fCut, b.u (0.35f, 0.85f)); b.set (P_fRes, b.u (0, 0.7f)); b.set (P_fDrive, b.u (0, 0.5f));
            b.set (P_fEnv, b.u (0.45f, 0.85f)); b.set (P_fKey, b.u (0.2f, 0.9f));
            // envelopes
            b.env (P_ampLoop, b.u (0, 0.3f), b.chance (0.15f) ? b.u (0, 0.4f) : 0, b.u (0.15f, 0.6f), b.u (0, 1), b.u (0.1f, 0.55f)); b.ch (P_ampCurve, b.pick (4));
            b.env (P_fltLoop, b.u (0, 0.35f), 0, b.u (0.1f, 0.55f), b.u (0, 0.75f), b.u (0.1f, 0.5f)); b.ch (P_fltCurve, b.pick (4)); b.ch (P_fltLoop, b.chance (0.1f) ? 1 : 0);
            b.env (P_modLoop, b.u (0, 0.6f), 0, b.u (0.1f, 0.6f), b.u (0, 1), b.u (0.1f, 0.6f)); b.ch (P_modCurve, b.pick (4)); b.ch (P_modLoop, b.chance (0.25f) ? 1 : 0);
            b.set (P_ampVel, b.u (0.1f, 0.7f)); b.set (P_fltVel, b.u (0, 0.8f));
            // orbits
            for (int l = 0; l < 4; ++l)
            {
                const int L = P_lfo1Shape + l * 6;
                b.ch (L, b.pick (10)); b.lst (L + 1, 3 + b.pick (13), numDivs); b.set (L + 2, b.u (0.2f, 1)); b.set (L + 3, b.u (0, 1));
                b.ch (L + 4, b.chance (0.85f) ? 1 : 0); b.ch (L + 5, b.chance (0.5f) ? 1 : 0);
            }
            // matrix: 1..4 routings (more when wild) to targets that always sound musical
            static const int srcs[] { sLfo1, sLfo2, sLfo3, sLfo4, sModEnv, sFltEnv, sVel, sNote, sRandom, sWheel, sLfo1, sLfo2 };
            static const int dsts[] { tMorph, tTilt, tOdd, tBlur, tBPw, tBDet, tCut, tCut, tRes, tDrive, tWidth, tShimmer, tF2Cut, tBFm, tSpread, tPartials };
            const int slots = 1 + b.pick (wild > 0.5f ? 8 : 4);
            for (int s = 1; s <= 16; ++s)
            {
                if (s <= slots)
                {
                    const int d = dsts[b.pick (16)];
                    // cutoff / filter-2 routings stay musical: a big negative push would close the filter to silence
                    b.slot (s, srcs[b.pick (12)], d, d == tCut ? b.u (-0.15f, 0.35f) : d == tF2Cut ? b.u (-0.12f, 0.12f) : b.u (-0.6f, 0.6f));
                }
                else b.slot (s, 0, 0, 0);
            }
            if (b.chance (0.25f)) b.slot (slots + 1, sLfo1 + b.pick (4), tPitch, b.chance (0.5f) ? b.u (0.004f, 0.02f) : 1.0f / 12.0f * (b.chance (0.5f) ? 1.0f : 0.0f) + 0.004f);
            b.set (P_mwAmt, b.u (0.1f, 0.6f)); b.set (P_atAmt, b.u (0, 0.6f)); b.set (P_velAmt, b.u (0.3f, 0.9f));
            // FX
            b.ch (P_onDRIVE, b.chance (0.55f) ? 1 : 0); b.ch (P_xType, b.pick (11)); b.set (P_xDrive, b.u (0, 0.45f)); b.set (P_xTone, b.u (0.4f, 1)); b.set (P_xBias, b.u (0.3f, 0.7f)); b.set (P_xPre, b.u (0, 0.3f));
            b.ch (P_onCHORUS, b.chance (0.5f) ? 1 : 0); b.ch (P_cType, b.pick (5)); b.set (P_cMix, b.u (0.1f, 0.5f)); b.set (P_cRate, b.u (0.1f, 0.6f)); b.set (P_cDepth, b.u (0.2f, 0.8f)); b.set (P_cFb, b.u (0, 0.4f));
            b.ch (P_onPHASER, b.chance (0.25f) ? 1 : 0); b.ch (P_pType, b.pick (5)); b.set (P_pMix, b.u (0.2f, 0.6f)); b.set (P_pRate, b.u (0.05f, 0.5f)); b.set (P_pDepth, b.u (0.3f, 0.9f)); b.set (P_pFb, b.u (0.2f, 0.8f)); b.set (P_pFreq, b.u (0.2f, 0.7f));
            b.ch (P_onDELAY, b.chance (0.75f) ? 1 : 0); b.ch (P_yType, b.pick (7)); { const int d = 3 + b.pick (10); b.lst (P_yTimeL, d, numDivs); b.lst (P_yTimeR, juce::jmin (16, d + b.pick (3)), numDivs); }
            b.set (P_yFb, b.u (0.2f, 0.65f)); b.set (P_yMix, b.u (0.08f, 0.35f)); b.set (P_yMod, b.u (0, 0.4f)); b.set (P_ySat, b.u (0, 0.5f)); b.set (P_yWidth, b.u (0.4f, 1)); b.set (P_yLo, b.u (0.05f, 0.4f)); b.set (P_yHi, b.u (0.4f, 0.95f)); b.set (P_yDuck, b.u (0, 0.5f));
            b.ch (P_onREVERB, b.chance (0.85f) ? 1 : 0); b.ch (P_rType, b.pick (11)); b.set (P_rSize, b.u (0.3f, 1)); b.set (P_rDecay, b.u (0.15f, 0.7f)); b.set (P_rMix, b.u (0.08f, 0.45f));
            b.set (P_rDamp, b.u (0.1f, 0.8f)); b.set (P_rMod, b.u (0, 0.8f)); b.set (P_rPre, b.u (0, 0.4f)); b.set (P_rWidth, b.u (0.3f, 0.8f)); b.set (P_rShim, b.chance (0.2f) ? b.u (0.2f, 0.7f) : 0); b.set (P_rDuck, b.u (0, 0.5f));
            b.ch (P_onGATE, b.chance (0.08f + 0.15f * wild) ? 1 : 0); b.lst (P_gRate, 1 + b.pick (2), 4); b.set (P_gDepth, b.u (0.5f, 1));
            for (int g = 0; g < 16; ++g) b.ch (P_g0 + g, b.chance (0.65f) ? 1 : 0);
            b.ch (P_onDYNAMICS, b.chance (0.5f) ? 1 : 0); b.ch (P_dType, b.pick (6)); b.set (P_dDepth, b.u (0.15f, 0.6f)); b.set (P_dTime, b.u (0.2f, 0.8f));
            b.ch (P_onEQ, b.chance (0.4f) ? 1 : 0); b.set (P_qLo, b.u (0.35f, 0.7f)); b.set (P_qMid, b.u (0.35f, 0.65f)); b.set (P_qHi, b.u (0.35f, 0.7f)); b.set (P_qMidF, b.u (0.2f, 0.8f)); b.set (P_qTilt, b.u (0.35f, 0.65f));
            b.ch (P_onSTEREO, b.chance (0.3f) ? 1 : 0); b.set (P_sWidth, b.u (0.4f, 0.9f)); b.set (P_sPan, b.chance (0.3f) ? b.u (0.1f, 0.6f) : 0); b.set (P_sMonoF, b.u (0, 0.5f));
            b.ch (P_onLOFI, b.chance (0.08f + 0.25f * wild) ? 1 : 0); b.ch (P_lType, b.pick (5)); b.set (P_lMix, b.u (0.2f, 0.55f)); b.set (P_lBits, b.u (0.45f, 0.9f)); b.set (P_lRate, b.u (0.5f, 1)); b.set (P_lWow, b.u (0, 0.5f));
            b.ch (P_arpOn, 0);
            b.ch (P_mono, 0); b.ch (P_legato, 0); b.set (P_glide, 0);
        }

        // arp with lanes that are the pattern plus random melodic moves, slides and ratchets
        void arpLine (B& b, int pattern, int mode, int rateIdx, int octIdx, float melody)
        {
            b.arp (pattern, mode, rateIdx, octIdx, b.u (0.35f, 0.8f));
            static const int steps[] { 0, 0, 0, 3, 5, 7, 10, 12, -12, 7, 12, -5 };
            for (int s = 0; s < 32; ++s)
            {
                if (b.r.uni() < melody) b.pr.lanes.v[Lanes::pitch][s] = 0.5f + (float) steps[b.pick (12)] / 48.0f;
                b.pr.lanes.v[Lanes::vel][s] = juce::jlimit (0.3f, 1.0f, b.pr.lanes.v[Lanes::vel][s] + b.u (-0.2f, 0.2f));
                if (b.r.uni() < 0.06f * melody) b.pr.lanes.v[Lanes::slide][s] = 1;
                if (b.r.uni() < 0.04f) b.pr.lanes.v[Lanes::rat][s] = 1;
            }
            b.ch (P_aDir, b.chance (0.8f) ? 0 : b.pick (5)); b.set (P_aSwing, b.chance (0.3f) ? b.u (0.05f, 0.4f) : 0);
            b.ch (P_aScale, b.chance (0.5f) ? 3 : 1); b.ch (P_aRoot, b.pick (12));
        }

        // The first arp step played must sound, or a short key tap stays silent. Runs after a preset is picked, so the factory list is unchanged.
        // Not for the PSY / ROLLING basses: their gallop starts on a rest on purpose. Presets keep the default 16 steps, offset 0.
        void startOnHit (Preset& p, int category)
        {
            if (category == 1 || category == 2) return;
            auto last = [&p] (int id) { for (auto it = p.values.rbegin(); it != p.values.rend(); ++it) if (it->first == id) return it->second; return paramDefs[id].def; };
            if (last (P_arpOn) < 0.5f) return;
            auto hit = [&] (int s) { const int g = (int) std::lround (p.lanes.v[Lanes::gate][(s % 32 + 32) % 32]); return g == 1 || g == 2; };
            int dir = (int) std::lround (last (P_aDir) * 4.0f);
            if (dir >= 3) // random / walk can start on any step: keep them only when every step sounds
            {
                bool all = true;
                for (int s = 0; s < 16; ++s) all = all && hit (s);
                if (all) return;
                dir = 0; p.values.push_back ({ P_aDir, 0.0f });
            }
            const int start = dir == 1 ? 15 : 0, way = dir == 1 ? -1 : 1;
            int k = 0;
            while (k < 16 && ! hit (start + way * k)) ++k;
            if (k == 0 || k == 16) return;
            for (auto& lane : p.lanes.v) // move the first hit onto the start step; lanes are 32 long, the pattern repeats every 16
                if (dir == 1) std::rotate (std::begin (lane), std::end (lane) - k, std::end (lane));
                else std::rotate (std::begin (lane), std::begin (lane) + k, std::end (lane));
        }

        void fxOff (B& b) { for (int p : { P_onCHORUS, P_onPHASER, P_onGATE, P_onSTEREO, P_onLOFI }) b.ch (p, 0); }

        // Category character on top of vary(): only what defines the category is pinned, with wide ranges and several variants.
        Preset recipe (int c, uint32_t seed)
        {
            B b (seed + (uint32_t) c * 7919u);
            vary (b, c == 16 ? 1.0f : 0.35f);
            const int variant = b.pick (4);
            switch (c)
            {
                case 0: // PSY LEAD: supersaw / FM scream / sync lead / star lead
                    b.env (P_ampLoop, b.u (0, 0.08f), 0, b.u (0.3f, 0.5f), b.u (0.5f, 0.9f), b.u (0.2f, 0.4f));
                    if (variant == 0) { b.ch (P_bOn, 1); b.ch (P_bWave, 4); b.set (P_bUni, b.u (0.4f, 0.75f)); b.set (P_bDet, b.u (0.3f, 0.6f)); }
                    if (variant == 1) { b.ch (P_bOn, 1); b.ch (P_bWave, 3); b.set (P_bFm, b.u (0.3f, 0.8f)); b.ch (P_aOn, 1); b.set (P_aLvl, b.u (0.4f, 0.8f)); }
                    if (variant == 2) { b.ch (P_bOn, 1); b.ch (P_bWave, b.pick (2)); b.set (P_bSync, b.u (0.25f, 0.75f)); b.slot (16, sModEnv, tBPw, 0.5f); }
                    if (variant == 3) { b.ch (P_aOn, 1); b.set (P_aLvl, b.u (0.75f, 0.95f)); b.set (P_aTilt, b.u (0.65f, 0.85f)); b.set (P_bLvl, b.u (0.1f, 0.3f)); }
                    b.set (P_fCut, b.u (0.5f, 0.8f));
                    if (b.chance (0.5f)) { b.ch (P_mono, 1); b.ch (P_legato, 1); b.set (P_glide, b.u (0.15f, 0.4f)); }
                    b.ch (P_onDELAY, 1); b.set (P_yMix, b.u (0.12f, 0.3f));
                    break;
                case 1: case 2: // PSY BASS / ROLLING BASS: mono, low, short, the arp rolls
                {
                    b.ch (P_mono, 1); b.ch (P_retrig, 1); b.ch (P_bOn, 1);
                    static const int waves[] { 0, 1, 5, 2, 0, 6 };
                    b.ch (P_bWave, waves[b.pick (6)]); b.set (P_bUni, b.u (0, 0.1f)); b.lst (P_bOct, b.chance (0.75f) ? 3 : 2, 7); // at the key or one octave down: lower is sub-audio b.set (P_bSemi, 0.5f); b.set (P_bLvl, 0.75f);
                    b.set (P_aLvl, b.u (0, 0.35f)); b.set (P_aSpread, b.u (0, 0.04f)); b.set (P_subLvl, b.u (0.15f, 0.5f)); b.set (P_noise, 0); b.set (P_ring, 0);
                    b.ch (P_f1Type, c == 2 ? (b.chance (0.5f) ? 2 : b.pick (2)) : b.pick (4)); b.ch (P_fRoute, 0); b.ch (P_f2Type, 0);
                    b.set (P_fCut, b.u (0.26f, 0.5f)); b.set (P_fRes, b.u (0, 0.5f)); b.set (P_fEnv, b.u (0.65f, 0.86f));
                    b.env (P_ampLoop, 0, 0, b.u (0.15f, 0.32f), b.u (0, 0.4f), b.u (0.03f, 0.1f)); b.ch (P_ampLoop, 0);
                    b.env (P_fltLoop, 0, 0, b.u (0.1f, 0.26f), b.u (0, 0.2f), 0.1f); b.ch (P_fltLoop, 0);
                    static const int psy[] { 0, 1, 2 }, roll[] { 3, 2, 7, 1, 13 };
                    arpLine (b, c == 1 ? psy[b.pick (3)] : roll[b.pick (5)], 9, 3, 0, c == 1 ? 0.0f : 0.15f);
                    b.ch (P_aScale, 0); b.set (P_aAcc, b.u (0.4f, 0.9f)); b.set (P_aSwing, 0);
                    fxOff (b); b.ch (P_onREVERB, b.chance (0.3f) ? 1 : 0); b.set (P_rMix, b.u (0.03f, 0.1f)); b.ch (P_onDELAY, b.chance (0.25f) ? 1 : 0); b.set (P_yMix, b.u (0.03f, 0.1f));
                    b.slot (1, b.chance (0.5f) ? sLfo1 : sModEnv, tCut, b.u (-0.15f, 0.2f));
                    break;
                }
                case 3: // ACID
                    b.ch (P_mono, 1); b.ch (P_legato, 1); b.ch (P_bOn, 1); b.ch (P_bWave, b.chance (0.6f) ? 0 : 1); b.set (P_bUni, 0); b.lst (P_bOct, b.chance (0.7f) ? 3 : 2, 7); b.set (P_bSemi, 0.5f);
                    b.set (P_aLvl, b.u (0, 0.2f));
                    b.ch (P_f1Type, b.chance (0.7f) ? 2 : 0); b.set (P_fCut, b.u (0.25f, 0.5f)); b.set (P_fRes, b.u (0.55f, 0.9f)); b.set (P_fEnv, b.u (0.7f, 0.9f)); b.set (P_fDrive, b.u (0.1f, 0.5f));
                    b.env (P_ampLoop, 0, 0, 0.3f, b.u (0.4f, 0.8f), 0.1f); b.ch (P_ampLoop, 0); b.env (P_fltLoop, 0, 0, b.u (0.15f, 0.32f), 0, 0.15f); b.ch (P_fltLoop, 0);
                    arpLine (b, b.chance (0.5f) ? 13 : 14, 9, 3, 0, 0.35f); b.set (P_aGlide, b.u (0.1f, 0.35f));
                    b.ch (P_onDRIVE, 1); b.ch (P_xType, b.chance (0.5f) ? 7 : 2 + b.pick (2)); b.set (P_xDrive, b.u (0.15f, 0.5f));
                    break;
                case 4: // TECHNO STAB
                    b.env (P_ampLoop, 0, 0, b.u (0.2f, 0.4f), b.u (0, 0.15f), b.u (0.15f, 0.35f)); b.ch (P_ampLoop, 0);
                    b.env (P_fltLoop, 0, 0, b.u (0.15f, 0.35f), b.u (0, 0.2f), 0.2f);
                    b.set (P_fCut, b.u (0.3f, 0.6f)); b.set (P_fEnv, b.u (0.65f, 0.85f));
                    if (b.chance (0.3f)) arpLine (b, b.chance (0.5f) ? 6 : 10, 6, 3 + 3 * b.pick (2), 0, 0.0f);
                    break;
                case 5: // TECHNO RUMBLE
                    b.ch (P_bOn, 1); b.ch (P_bWave, b.pick (4)); b.lst (P_bOct, 2, 7); b.set (P_aSpread, b.u (0.1f, 0.7f)); b.set (P_noise, b.u (0.03f, 0.25f));
                    b.ch (P_f1Type, b.chance (0.5f) ? 1 : b.pick (12)); b.set (P_fCut, b.u (0.18f, 0.42f)); b.set (P_fRes, b.u (0.1f, 0.6f));
                    b.env (P_ampLoop, b.u (0.05f, 0.35f), 0, 0.5f, b.u (0.7f, 1), b.u (0.4f, 0.65f)); b.ch (P_ampLoop, 0);
                    b.slot (1, sLfo3, tCut, b.u (0.1f, 0.35f));
                    b.ch (P_onREVERB, 1); b.ch (P_rType, b.chance (0.5f) ? 4 : 9); b.set (P_rMix, b.u (0.25f, 0.55f)); b.set (P_rDecay, b.u (0.4f, 0.75f));
                    if (b.chance (0.5f)) { b.ch (P_onDYNAMICS, 1); b.ch (P_dType, 3 + b.pick (2)); b.set (P_dDepth, b.u (0.3f, 0.7f)); }
                    break;
                case 6: // HOUSE ORGAN
                    b.ch (P_aOn, 1); b.ch (P_cons, b.pick (3)); b.set (P_aMorph, b.u (0.5f, 1)); b.set (P_aPart, b.u (0.12f, 0.5f)); b.set (P_aSpread, b.u (0, 0.03f)); b.set (P_aLvl, b.u (0.7f, 0.95f)); b.set (P_aShimmer, b.u (0, 0.15f));
                    b.ch (P_bWave, b.chance (0.5f) ? 6 : 1); b.set (P_bLvl, b.u (0.1f, 0.4f)); b.set (P_bUni, b.u (0, 0.15f));
                    b.ch (P_f1Type, 3); b.set (P_fCut, b.u (0.6f, 0.9f)); b.set (P_fEnv, b.u (0.5f, 0.62f));
                    b.env (P_ampLoop, 0, 0, b.u (0.2f, 0.4f), b.u (0.3f, 0.85f), b.u (0.1f, 0.25f)); b.ch (P_ampLoop, 0);
                    b.ch (P_onCHORUS, 1); b.ch (P_cType, b.chance (0.6f) ? 4 : b.pick (4)); b.set (P_cMix, b.u (0.25f, 0.6f));
                    if (b.chance (0.35f)) arpLine (b, b.chance (0.5f) ? 8 : 4, 6, 3, 0, 0.0f);
                    break;
                case 7: // HOUSE CHORD
                    b.ch (P_bOn, 1); b.ch (P_bWave, b.chance (0.5f) ? 0 : 4 + b.pick (2)); b.set (P_bUni, b.u (0.1f, 0.4f));
                    b.set (P_fCut, b.u (0.38f, 0.65f)); b.set (P_fEnv, b.u (0.58f, 0.75f));
                    b.env (P_ampLoop, b.u (0, 0.05f), 0, b.u (0.25f, 0.45f), b.u (0.1f, 0.5f), b.u (0.2f, 0.4f)); b.ch (P_ampLoop, 0);
                    if (b.chance (0.5f)) { b.ch (P_onDYNAMICS, 1); b.ch (P_dType, 3); b.set (P_dDepth, b.u (0.3f, 0.65f)); }
                    if (b.chance (0.3f)) arpLine (b, 9, 6, 3, 0, 0.0f);
                    break;
                case 8: // DEEP PAD
                    b.env (P_ampLoop, b.u (0.4f, 0.65f), 0, 0.6f, b.u (0.7f, 1), b.u (0.5f, 0.8f)); b.ch (P_ampLoop, 0);
                    b.set (P_fCut, b.u (0.35f, 0.65f)); b.set (P_fRes, b.u (0, 0.35f));
                    b.ch (P_onREVERB, 1); b.set (P_rMix, b.u (0.3f, 0.55f)); b.set (P_rDecay, b.u (0.45f, 0.75f));
                    break;
                case 9: // STAR PAD
                    b.ch (P_aOn, 1); b.set (P_aLvl, b.u (0.7f, 0.95f)); b.set (P_aShimmer, b.u (0.5f, 1)); b.set (P_aMorph, b.u (0.4f, 1)); b.set (P_bLvl, b.u (0.05f, 0.35f));
                    b.env (P_ampLoop, b.u (0.35f, 0.65f), 0, 0.6f, b.u (0.75f, 1), b.u (0.55f, 0.8f)); b.ch (P_ampLoop, 0);
                    b.set (P_fCut, b.u (0.55f, 0.85f));
                    b.ch (P_onREVERB, 1); b.ch (P_rType, b.chance (0.5f) ? 6 : 10 - b.pick (2) * 10); b.set (P_rMix, b.u (0.35f, 0.6f)); b.set (P_rDecay, b.u (0.45f, 0.75f));
                    break;
                case 10: // PLUCK
                    b.env (P_ampLoop, 0, 0, b.u (0.15f, 0.35f), 0, b.u (0.15f, 0.35f)); b.ch (P_ampLoop, 0);
                    b.env (P_fltLoop, 0, 0, b.u (0.1f, 0.28f), 0, 0.2f); b.ch (P_fltLoop, 0);
                    b.set (P_fCut, b.u (0.25f, 0.55f)); b.set (P_fEnv, b.u (0.72f, 0.92f));
                    break;
                case 11: // ARP SEQ
                {
                    static const int pats[] { 0, 2, 3, 4, 5, 7, 9, 10, 11, 12, 15, 16, 17, 18, 19 };
                    b.env (P_ampLoop, 0, 0, b.u (0.2f, 0.4f), b.u (0, 0.4f), b.u (0.15f, 0.35f)); b.ch (P_ampLoop, 0);
                    b.set (P_fCut, b.u (0.35f, 0.7f)); b.set (P_fEnv, b.u (0.6f, 0.85f));
                    static const int rates[] { 3, 3, 3, 4, 6, 1 };
                    arpLine (b, pats[b.pick (15)], b.pick (12), rates[b.pick (6)], b.pick (3), b.u (0.2f, 0.7f));
                    break;
                }
                case 12: // BELL
                    b.ch (P_aOn, 1); b.set (P_aSpread, b.u (0.25f, 0.85f)); b.set (P_aTilt, b.u (0.55f, 0.85f)); b.set (P_aLvl, b.u (0.7f, 0.95f));
                    if (b.chance (0.5f)) { b.ch (P_bWave, 3); b.set (P_bFm, b.u (0.2f, 0.7f)); }
                    b.env (P_ampLoop, 0, 0, b.u (0.4f, 0.65f), 0, b.u (0.4f, 0.65f)); b.ch (P_ampLoop, 0);
                    b.set (P_fCut, b.u (0.6f, 0.95f));
                    break;
                case 13: // KEYS
                    b.ch (P_aOn, 1); b.set (P_aPart, b.u (0.15f, 0.55f)); b.set (P_aSpread, b.u (0, 0.12f));
                    b.ch (P_bWave, b.chance (0.5f) ? 2 : (b.chance (0.5f) ? 6 : 3)); b.set (P_bUni, b.u (0, 0.15f));
                    b.env (P_ampLoop, 0, 0, b.u (0.35f, 0.55f), b.u (0.1f, 0.5f), b.u (0.2f, 0.35f)); b.ch (P_ampLoop, 0);
                    b.set (P_fCut, b.u (0.5f, 0.8f));
                    if (b.chance (0.4f)) b.slot (16, sLfo1, tVolume, b.u (0.05f, 0.2f));
                    break;
                case 14: // ATMOS
                    b.set (P_noise, b.u (0.15f, 0.45f)); b.lst (P_nType, 2 + b.pick (3), 5); b.set (P_aBlur, b.u (0.4f, 0.9f)); b.set (P_aShimmer, b.u (0.5f, 1));
                    b.env (P_ampLoop, b.u (0.45f, 0.75f), 0, 0.6f, b.u (0.7f, 1), b.u (0.6f, 0.85f)); b.ch (P_ampLoop, 0);
                    b.ch (P_onREVERB, 1); b.ch (P_rType, b.chance (0.5f) ? 9 : 10); b.set (P_rMix, b.u (0.45f, 0.7f)); b.set (P_rDecay, b.u (0.55f, 0.85f));
                    b.ch (P_onDELAY, 1); b.ch (P_yType, 5 + b.pick (2));
                    break;
                case 15: // FX RISER
                    b.set (P_noise, b.u (0.1f, 0.4f));
                    b.env (P_ampLoop, b.u (0.55f, 0.8f), 0, 0.6f, 1, b.u (0.4f, 0.6f)); b.ch (P_ampLoop, 0);
                    b.env (P_modLoop, b.u (0.65f, 0.88f), 0, 0.6f, 1, 0.5f); b.ch (P_modLoop, 0);
                    b.slot (14, sModEnv, tCut, b.u (0.4f, 0.8f)); b.slot (15, sModEnv, tPitch, b.u (0.1f, 0.6f)); b.slot (16, sModEnv, tWidth, 0.4f);
                    b.set (P_fCut, b.u (0.15f, 0.4f));
                    b.ch (P_onREVERB, 1); b.set (P_rMix, b.u (0.35f, 0.6f));
                    break;
                default: // CRAZY: vary(1.0) already went wild; add exotic filters and an arp sometimes
                    b.ch (P_f1Type, 6 + b.pick (6));
                    if (b.chance (0.4f)) arpLine (b, b.pick (numPatterns), b.pick (12), 1 + b.pick (8), b.pick (4), 0.8f);
                    break;
            }
            const auto bOn = std::find_if (b.pr.values.rbegin(), b.pr.values.rend(), [] (const std::pair<int, float>& v) { return v.first == P_bOn; });
            if (bOn == b.pr.values.rend() || bOn->second < 0.5f) b.ch (P_aOn, 1); // never both oscillators off
            return b.pr;
        }

        // Distance between two presets over every sound parameter (0 = identical, 1 = opposite)
        std::vector<float> fullVector (const Preset& p)
        {
            std::vector<float> v (NUM_PARAMS);
            for (int i = 0; i < NUM_PARAMS; ++i) v[(size_t) i] = paramDefs[i].def;
            for (auto& [id, x] : p.values) v[(size_t) id] = x;
            return v;
        }
        float distance (const std::vector<float>& a, const std::vector<float>& b)
        {
            float d = 0; int n = 0;
            for (int i = 0; i < NUM_PARAMS; ++i) { if (i == P_uiEnv || i == P_uiLfo) continue; d += std::abs (a[(size_t) i] - b[(size_t) i]); ++n; }
            return d / (float) n;
        }

        juce::String nameFor (int c, int i, uint32_t seed, juce::StringArray& used)
        {
            Rng r (seed * 7u + 1u + (uint32_t) c * 31u);
            for (int tries = 0; tries < 40; ++tries)
            {
                const juce::String n = juce::String (bodies[r.next() % numBodies]) + " " + cats[c].words[r.next() % 12];
                if (! used.contains (n)) { used.add (n); return juce::String (cats[c].name) + " // " + n; }
            }
            return juce::String (cats[c].name) + " // " + cats[c].words[i % 12] + " " + juce::String (i + 1);
        }
    }

    const juce::StringArray& presetCategories()
    {
        static const juce::StringArray c = [] { juce::StringArray s; for (auto& x : cats) s.add (x.name); return s; }();
        return c;
    }

    Lanes lanesFromPattern (int pattern)
    {
        Lanes l;
        if (pattern < 0 || pattern >= numPatterns) return l;
        for (int s = 0; s < 32; ++s)
        {
            const char g = patterns[pattern][1][s % 16], t = patterns[pattern][2][s % 16];
            l.v[Lanes::gate][s] = g == '.' ? 0.0f : g == 'A' ? 2.0f : g == '-' ? 3.0f : 1.0f;
            l.v[Lanes::pitch][s] = 0.5f + (float) (t == 'o' ? 12 : t == 'd' ? -12 : t - '0') / 48.0f;
            l.v[Lanes::vel][s] = g == 'A' ? 1.0f : 0.75f;
        }
        return l;
    }

    Preset generatePreset (int category, uint32_t seed)
    {
        category = juce::jlimit (0, numCats - 1, category);
        auto p = recipe (category, seed);
        startOnHit (p, category);
        juce::StringArray used;
        p.name = nameFor (category, 0, seed, used);
        return p;
    }

    const std::vector<Preset>& factoryPresets()
    {
        static const std::vector<Preset> all = []
        {
            std::vector<Preset> v;
            juce::StringArray used;
            for (int c = 0; c < numCats; ++c)
            {
                std::vector<std::vector<float>> taken;
                for (int i = 0; i < cats[c].count; ++i)
                {
                    // 16 candidates; keep the one farthest from every preset already in this category
                    uint32_t seed = 0; Preset p; float best = -1;
                    for (int t = 0; t < 16; ++t)
                    {
                        const uint32_t s = (uint32_t) (c * 100000 + i * 100 + t + 1);
                        auto cand = recipe (c, s);
                        const auto vec = fullVector (cand);
                        float nearest = 1e9f;
                        for (auto& o : taken) nearest = juce::jmin (nearest, distance (vec, o));
                        if (nearest > best) { best = nearest; p = std::move (cand); seed = s; }
                    }
                    taken.push_back (fullVector (p));
                    startOnHit (p, c);
                    p.name = nameFor (c, i, seed, used);
                    const int idx = (int) v.size();
                    p.trimDb = idx < (int) (sizeof (presetTrims) / sizeof (presetTrims[0])) ? presetTrims[idx] : 0.0f;
                    v.push_back (std::move (p));
                }
            }
            return v;
        }();
        return all;
    }
}
