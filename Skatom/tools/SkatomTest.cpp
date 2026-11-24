// Loads the built SKATOM.vst3 and drives every stage hard: output must stay finite and under the limiter ceiling,
// the macro must actually build up (HPF removes lows) and DROP must recover.
//   SkatomTest <path to SKATOM.vst3>
#include <juce_audio_processors/juce_audio_processors.h>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

struct TestPlayHead : juce::AudioPlayHead
{
    double ppq = 0; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (128.0); p.setIsPlaying (playing); p.setPpqPosition (ppq);
        return p;
    }
};

struct Host
{
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    TestPlayHead transport;

    void set (const juce::String& name, float real)
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (100) == name)
            {
                auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
                p->setValue (rp ? rp->convertTo0to1 (real) : real);
                return;
            }
        // VST3 wraps params: fall back to text lookup
        std::cout << "  ?? no parameter " << name << std::endl;
        ++failures;
    }
    void reset()
    {
        for (auto* p : plugin->getParameters()) p->setValue (p->getDefaultValue());
        plugin->releaseResources();
        plugin->prepareToPlay (sampleRate, blockSize);
    }
    // returns {peak, rms, lowRms} of the last second
    std::array<float, 3> run (double seconds, bool withKick = true, float noiseLevel = 0.2f)
    {
        juce::Random r (7);
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        const int blocks = (int) (seconds * sampleRate / blockSize), tailFrom = blocks - (int) (sampleRate / blockSize);
        float peak = 0; double ms = 0, low = 0, lp = 0; long cnt = 0; bool finite = true; long t = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++t)
            {
                const float kick = std::sin (juce::MathConstants<float>::twoPi * 55.0f * t / (float) sampleRate) * ((t % 24000) < 6000 ? 0.7f : 0.0f);
                const float v = (withKick ? kick : 0.0f) + (r.nextFloat() * 2 - 1) * noiseLevel;
                buf.setSample (0, i, v); buf.setSample (1, i, v * 0.9f);
            }
            plugin->processBlock (buf, midi);
            if (transport.playing) transport.ppq += blockSize / sampleRate * 128.0 / 60.0;
            if (b < tailFrom) continue;
            for (int i = 0; i < blockSize; ++i)
            {
                const float x = buf.getSample (0, i);
                finite &= std::isfinite (x) && std::isfinite (buf.getSample (1, i));
                peak = std::max (peak, std::abs (x));
                ms += x * x; lp += 0.005 * (x - lp); low += lp * lp; ++cnt;
            }
        }
        if (! finite) peak = 1e9f;
        return { peak, (float) std::sqrt (ms / cnt), (float) std::sqrt (low / cnt) };
    }
};
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cout << "usage: SkatomTest <SKATOM.vst3>" << std::endl; return 2; }

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, juce::String (argv[1]));
    if (found.isEmpty()) { std::cout << "could not load " << argv[1] << std::endl; return 2; }
    juce::String err;
    Host h { fm.createPluginInstance (*found[0], sampleRate, blockSize, err) };
    if (! h.plugin) { std::cout << "instantiate failed: " << err << std::endl; return 2; }
    h.plugin->enableAllBuses();
    h.plugin->setPlayHead (&h.transport);
    h.reset();

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto clean = h.run (3);
    const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / 3000.0 * 100.0;
    std::cout << "  CPU   " << juce::String (cpu, 1) << "% of one core in real time" << std::endl;
    check (cpu < 25, "CPU load is sane");
    check (clean[0] < 1e8f && clean[1] > 0.05f, "clean path passes audio (rms " + juce::String (clean[1], 3) + ")");

    h.set ("Mega Macro", 0.95f);
    auto built = h.run (3);
    check (built[0] < 1e8f, "macro 95% stays finite");
    check (built[0] <= 1.0f, "limiter holds macro 95% at the ceiling (peak " + juce::String (built[0], 3) + ")");
    check (built[2] < clean[2] * 0.5f, "macro build-up HPF strips the lows (" + juce::String (clean[2], 3) + " -> " + juce::String (built[2], 3) + ")");

    h.set ("Mega Macro", 0.0f);
    auto dropped = h.run (3);
    check (dropped[2] > clean[2] * 0.7f, "after DROP the lows are back");

    struct Case { const char* name; std::vector<std::pair<const char*, float>> p; };
    const std::vector<Case> cases {
        { "stutter 1/32", { { "Stutter", 1 }, { "Stutter Division", 5 } } },
        { "reverse", { { "Reverse", 1 } } },
        { "tape stop", { { "Tape Stop", 1 } } },
        { "freeze", { { "Freeze", 1 } } },
        { "everything maxed", { { "Freq Shift", 300 }, { "Contrast", 1 }, { "Reson Tension", 1 }, { "Filter Reso", 18 }, { "Filter Drive", 1 },
                                { "Formant Mix", 1 }, { "Dist Drive", 1 }, { "Dist Type", 1 }, { "Magic Bass", 1 }, { "Wash Noise", 1 },
                                { "Wash Detune", 1 }, { "Wash Send", 1 }, { "Phat Comp", 1 }, { "LFO1 Depth", 1 }, { "Env Depth", 1 } } },
        { "crush + glitch density", { { "Dist Type", 2 }, { "Dist Drive", 0.8f }, { "Glitch Density", 1 } } },
    };
    for (auto& c : cases)
    {
        h.reset();
        h.run (1); // buffer effects grab what's already playing — feed audio first
        for (auto& [n, v] : c.p) h.set (n, v);
        auto res = h.run (2);
        check (res[0] <= 1.0f, juce::String (c.name) + ": finite and under ceiling (peak " + juce::String (res[0], 3) + ")");
        if (juce::String (c.name) != "tape stop") // tape stop ends in silence by design
            check (res[1] > 0.01f, juce::String (c.name) + ": still makes sound (rms " + juce::String (res[1], 3) + ")");
    }

    // RISE: 1 bar at 128 BPM (~1.9 s) builds the HPF, then AUTO DROP brings the lows back
    h.reset();
    h.transport.playing = true; // RISE only advances while the DAW plays
    h.transport.ppq = 0;         // pressed on a bar line: the rise is exactly 1 bar (it always ends on a bar line)
    h.set ("Rise Bars", 0); h.set ("Rise", 1);
    h.run (1.55);                 // the HPF curve surges after ~70% of the rise
    auto rising = h.run (0.25);   // measure ~83–96% of the bar
    check (rising[2] < clean[2] * 0.5f, "RISE builds up over 1 bar (lows " + juce::String (rising[2], 3) + ")");
    auto afterRise = h.run (3);
    check (afterRise[2] > clean[2] * 0.7f, "AUTO DROP at the end of the rise brings the lows back");
    h.transport.playing = false;

    // matrix in the DSP: S06 pushes CUTOFF down; at S06 the highs go, at S01 they're back
    h.reset();
    {
        // the hosted VST3 wraps our state: <VST3PluginState><IComponent>(JUCE base64 of our XML)</IComponent>
        juce::MemoryBlock mb; h.plugin->getStateInformation (mb);
        auto outer = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        auto* comp = outer ? outer->getChildByName ("IComponent") : nullptr;
        check (comp != nullptr, "VST3 state has an IComponent block");
        if (comp != nullptr)
        {
            juce::MemoryBlock inner; inner.fromBase64Encoding (comp->getAllSubText());
            auto xml = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
            check (xml != nullptr, "inner SKATOM state is readable");
            if (xml != nullptr)
            {
                xml->setAttribute ("snaps", R"([{},{},{},{},{},{"filtCut":0.25},{},{},{},{},{},{},{},{},{},{}])");
                juce::MemoryBlock ni; juce::AudioProcessor::copyXmlToBinary (*xml, ni);
                comp->deleteAllTextElements(); comp->addTextElement (ni.toBase64Encoding());
                juce::MemoryBlock out; juce::AudioProcessor::copyXmlToBinary (*outer, out);
                h.plugin->setStateInformation (out.getData(), (int) out.getSize());
            }
        }
    }
    {
        juce::MemoryBlock mb; h.plugin->getStateInformation (mb);
        auto outer = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        juce::MemoryBlock inner; inner.fromBase64Encoding (outer->getChildByName ("IComponent")->getAllSubText());
        auto xml = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
        check (xml->getStringAttribute ("snaps").contains ("filtCut"), "snapshots survive a state round-trip");
    }
    h.set ("Matrix X", 0.375f); h.set ("Matrix Y", 0.375f);
    auto atS06 = h.run (2, false);
    h.set ("Matrix X", 0.125f); h.set ("Matrix Y", 0.125f);
    auto atS01 = h.run (2, false);
    check (atS06[1] < atS01[1] * 0.3f, "matrix: S06 pushes the cutoff down on noise (rms " + juce::String (atS06[1], 3) + " vs " + juce::String (atS01[1], 3) + ")");

    h.reset();
    h.set ("Roll Gate", 1); h.set ("Jet Flanger", 1); h.set ("Spin Pan", 1); h.set ("Wash Send", 1);
    auto fx = h.run (2);
    check (fx[0] <= 1.0f && fx[1] > 0.01f, "ROLL + JET + SPIN + full wash: finite, limited, audible");

    // BUILD modes: every build is made from the incoming sound — silence in, silence out (only NOISE CLASSIC invents sound)
    const char* modes[] = { "WASH OUT", "PITCH UP", "ROLL RUSH", "RESON DRONE", "JET SWEEP", "NOISE CLASSIC", "RANDOM" };
    for (int m = 0; m < 7; ++m)
    {
        h.reset();
        h.set ("Build Mode", (float) m / 6.0f); // choice param, normalized
        h.set ("Mega Macro", 0.95f);
        auto quiet = h.run (2, false, 0.0f); // silent from the start (a reverb tail from earlier audio would be legit)
        if (m == 5) check (quiet[1] > 0.005f, juce::String ("BUILD ") + modes[m] + ": the classic riser adds its own noise");
        else check (quiet[1] < 0.001f, juce::String ("BUILD ") + modes[m] + ": silent input stays silent (rms " + juce::String (quiet[1], 5) + ")");
        h.reset();
        h.set ("Build Mode", (float) m / 6.0f);
        h.run (0.5);
        h.set ("Mega Macro", 0.95f);
        auto loud = h.run (2);
        check (loud[0] <= 1.0f && loud[1] > 0.01f, juce::String ("BUILD ") + modes[m] + ": your sound builds, finite and limited (rms " + juce::String (loud[1], 3) + ")");
    }
    h.reset();
    h.set ("Pitch", 12); h.set ("Build Mode", 0);
    auto pitched = h.run (2);
    check (pitched[0] <= 1.0f && pitched[1] > 0.05f, "PITCH +12 st: finite and audible");

    // module power: an OFF module pins its effects neutral (matrix / macro pushes included)
    h.reset();
    h.set ("Dist Type", 2.0f / 3); h.set ("Dist Drive", 1); h.set ("Dist On", 0);
    auto distOff = h.run (2);
    check (std::abs (distOff[1] - clean[1]) < clean[1] * 0.3f, "04 OFF: crush at full drive is silenced (rms " + juce::String (distOff[1], 3) + " vs clean " + juce::String (clean[1], 3) + ")");
    h.reset();
    h.set ("Build Bus On", 0); h.set ("Mega Macro", 0.95f);
    auto busOff = h.run (3);
    check (busOff[2] > clean[2] * 0.4f && busOff[0] <= 1.0f, "05 OFF: macro build keeps the lows (no HPF, no swallow) (low " + juce::String (busOff[2], 3) + " vs clean " + juce::String (clean[2], 3) + ", peak " + juce::String (busOff[0], 3) + ")");
    // FX ORDER: all four chains run, finite and audible
    for (int o = 0; o < 4; ++o)
    {
        h.reset();
        h.set ("FX Order", (float) o / 3); h.set ("Dist Drive", 0.6f); h.set ("Filter Cutoff", 0.5f); h.set ("Glitch Density", 0.6f);
        auto ord = h.run (2);
        check (ord[0] <= 1.0f && ord[1] > 0.02f, "FX ORDER " + juce::String (o) + ": finite and audible (rms " + juce::String (ord[1], 3) + ")");
    }

    // RISE PITCH: +48 st on the macro, PITCH UP build (+12 on top, clamped at +48)
    h.reset();
    h.set ("Build Mode", 1.0f / 6); h.set ("Rise Pitch", 1.0f); h.set ("Mega Macro", 1.0f);
    auto sky = h.run (2);
    check (sky[0] <= 1.0f && sky[1] > 0.01f, "RISE PITCH +48 st: finite, limited and audible (rms " + juce::String (sky[1], 3) + ")");

    // ACELERA: PUMP ducks steady noise on the beat; ENDLESS + HIGHER + WIDTH + DRIVE at every curve stay finite and audible
    h.reset(); h.set ("Mega Macro", 0.7f);
    auto noPump = h.run (2, false);
    h.set ("Acelera Pump", 1.0f);
    auto pumped = h.run (2, false);
    check (pumped[1] < noPump[1] * 0.85f, "ACELERA PUMP ducks on the beat (rms " + juce::String (noPump[1], 3) + " -> " + juce::String (pumped[1], 3) + ")");
    for (int k = 0; k < 4; ++k)
    {
        h.reset();
        h.set ("Acelera Endless", 1.0f); h.set ("Acelera Higher", 1.0f); h.set ("Acelera Width", 1.0f); h.set ("Acelera Drive", 1.0f);
        h.set ("Acelera Pump", 1.0f); h.set ("Acelera Curve", k / 3.0f);
        float worst = 0, quietest = 1;
        for (float x : { 0.3f, 0.6f, 0.9f, 1.0f }) { h.set ("Mega Macro", x); auto a = h.run (1.5); worst = std::max (worst, a[0]); quietest = std::min (quietest, a[1]); }
        check (worst <= 1.0f && quietest > 0.005f, "ACELERA full (ENDLESS+HIGHER+WIDTH+DRIVE+PUMP) curve " + juce::String (k) + ": finite, limited, audible (peak "
                                                     + juce::String (worst, 3) + ", min rms " + juce::String (quietest, 3) + ")");
    }
    h.reset(); h.set ("Mega Macro", 0.6f);
    auto plain = h.run (2);
    h.set ("Acelera Endless", 1.0f);
    auto endless = h.run (2);
    check (std::abs (endless[1] - plain[1]) > 0.002f || std::abs (endless[2] - plain[2]) > 0.002f, "ACELERA ENDLESS changes the sound (rms " + juce::String (plain[1], 3) + " -> " + juce::String (endless[1], 3) + ")");

    // KNOB SLOTS: a dragged effect takes over the knob (REVERB only → no build HPF, lows stay); PITCH slot + everything at once stays clean
    h.reset();
    h.set ("Knob Slot 1", 3.0f / 16); h.set ("Knob Slot 1 Amount", (0.95f + 1) / 2); h.set ("Mega Macro", 0.95f);
    auto revOnly = h.run (2);
    check (revOnly[2] > built[2] * 2 && revOnly[0] <= 1.0f, "KNOB SLOT REVERB only: the build HPF is gone, lows stay (low " + juce::String (revOnly[2], 3) + " vs build " + juce::String (built[2], 3) + ")");
    h.reset();
    for (int k = 0; k < 8; ++k) { h.set ("Knob Slot " + juce::String (k + 1), (float) (k * 2 + 1) / 16); h.set ("Knob Slot " + juce::String (k + 1) + " Amount", 1.0f); }
    float worstS = 0, quietS = 1;
    for (float x : { 0.25f, 0.5f, 0.75f, 1.0f }) { h.set ("Mega Macro", x); auto a = h.run (1.5); worstS = std::max (worstS, a[0]); quietS = std::min (quietS, a[1]); }
    check (worstS <= 1.0f && quietS > 0.003f, "KNOB 8 SLOTS at +100% (FILTER..GLITCH incl. PITCH +48): finite, limited, audible (peak " + juce::String (worstS, 3) + ", min rms " + juce::String (quietS, 4) + ")");
    h.reset();
    h.set ("Knob Slot 1", 4.0f / 16); h.set ("Knob Slot 1 Amount", 1.0f); h.set ("Knob Slot 2", 16.0f / 16); h.set ("Knob Slot 2 Amount", 1.0f);
    for (float x : { 0.3f, 0.7f, 1.0f }) { h.set ("Mega Macro", x); auto a = h.run (1.5); worstS = std::max (worstS, a[0]); quietS = std::min (quietS, a[1]); }
    check (worstS <= 1.0f && quietS > 0.003f, "KNOB PITCH +48 + ENDLESS: finite, limited, audible (min rms " + juce::String (quietS, 4) + ")");

    // TAILS: every reverb tail at full build stays finite, and the feedback ones die away after the build
    const char* tails[] = { "HALL", "PLATE", "CATHEDRAL", "SHIMMER", "SKY SHIMMER", "INFINITE", "ECHO WASH", "REVERSE SWELL", "ROOM" };
    float later[9] {};
    for (int t = 0; t < 9; ++t)
    {
        h.reset();
        h.set ("Reverb Type", (float) t / 8); h.set ("Drop Duck", 0);
        h.run (0.1, false, 0.0f);                        // VST3 delivers the param change during processing
        juce::Thread::sleep (4000);                      // the tail IR is built on the plugin's loader thread (real time)
        h.run (0.3, false, 0.0f);                        // ...and swaps it in on the next blocks
        h.set ("Mega Macro", 1.0f);
        auto open = h.run (3);
        h.set ("Mega Macro", 0.0f);
        auto ring = h.run (1, false, 0.0f);   // the tail right after the release (silent input)
        auto ring2 = h.run (3, false, 0.0f);
        later[t] = ring2[1];
        std::cout << "  tail " << tails[t] << " ring rms " << juce::String (ring[1], 4) << " later " << juce::String (ring2[1], 4) << std::endl;
        h.run (8, false, 0.0f);
        auto after = h.run (2, false, 0.0f);
        check (open[0] <= 1.0f && open[1] > 0.01f && after[0] < 0.5f,
               juce::String ("TAIL ") + tails[t] + ": opens (rms " + juce::String (open[1], 3) + ") and dies away (peak after " + juce::String (after[0], 3) + ")");
    }

    check (later[2] > later[0] * 2 && later[1] < later[0] * 0.5f && later[7] < later[0] * 0.5f && later[5] > later[0] * 2 && later[3] > later[0] && later[8] < later[0] * 0.5f,
           "TAILS really differ: CATHEDRAL / INFINITE ring longer than HALL, PLATE / REVERSE SWELL stop short, SHIMMER keeps climbing, ROOM stops short");

    h.reset();
    h.set ("Bypass", 1);
    auto byp = h.run (2);
    check (std::abs (byp[1] - clean[1]) < clean[1] * 0.5f + 0.05f, "bypass passes the dry signal");

    std::cout << (failures ? "FAILED: " : "ALL PASSED") << (failures ? juce::String (failures) : juce::String()) << std::endl;
    return failures ? 1 : 0;
}
