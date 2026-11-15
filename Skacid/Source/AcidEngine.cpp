#include "AcidEngine.h"
#include <algorithm>

namespace skacid
{
namespace
{
    using juce::MathConstants;

    inline float fastTanh (float x)
    {
        x = juce::jlimit (-3.0f, 3.0f, x);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float polyBlep (double t, double dt)
    {
        if (t < dt)       { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    inline float coefFor (double seconds, double rate) { return (float) (1.0 - std::exp (-1.0 / juce::jmax (1.0e-6, seconds * rate))); }

    double stepBeats (int rate)
    {
        switch (rate)
        {
            case R8:   return 0.5;
            case R16T: return 1.0 / 6.0;
            case R32:  return 0.125;
            default:   return 0.25;
        }
    }

    // Changes some steps of a pattern. Used live (each loop, on the audio thread) and by the MUTATE button.
    template <typename Uni>
    uint32_t mutateSteps (Pattern& p, int length, float amount, Uni uni)
    {
        int pool[NumSteps]; int poolOct[NumSteps]; int n = 0;
        for (int i = 0; i < length; ++i)
            if (p[(size_t) i].gate == Note) { pool[n] = p[(size_t) i].note; poolOct[n] = p[(size_t) i].oct; ++n; }

        uint32_t mask = 0;
        for (int i = 0; i < length; ++i)
        {
            if (uni() >= amount * 0.4f) continue;
            auto& st = p[(size_t) i];
            const int op = juce::jmin (4, (int) (uni() * 5.0f));
            switch (op)
            {
                case 0: if (st.gate == Note && n > 0) { const int k = juce::jmin (n - 1, (int) (uni() * n)); st.note = pool[k]; st.oct = poolOct[k]; } break;
                case 1: st.acc = ! st.acc; break;
                case 2: st.sld = ! st.sld; break;
                case 3: st.oct = st.oct == Mid ? (uni() < 0.6f ? Up : Down) : Mid; break;
                default:
                    if (st.gate == Note && i > 0 && uni() < 0.4f) st.gate = Rest;
                    else if (st.gate != Note) { st.gate = Note; if (n > 0) st.note = pool[juce::jmin (n - 1, (int) (uni() * n))]; }
                    break;
            }
            mask |= 1u << i;
        }
        return mask;
    }
} // namespace

juce::String stepId (int step, const char* field) { return "s" + juce::String (step + 1) + "_" + field; }

const std::vector<skazi::ParamDef>& paramDefs()
{
    using P = skazi::ParamDef;
    static const std::vector<P> defs = []
    {
        std::vector<P> d = {
            { "wave", "Wave", P::Choice, 0, 1, Saw, "", 1, 1, { "SAW", "SQUARE" } },
            { "sub", "Sub Osc", P::Float, 0, 100, 0, "%", 1 },
            { "tune", "Tune", P::Float, -12, 12, 0, "st", 1 },
            { "fine", "Fine", P::Float, -50, 50, 0, "ct", 1 },

            { "cutoff", "Cutoff", P::Float, 40, 8000, 420, "Hz", 1, 0.3f },
            { "reso", "Resonance", P::Float, 0, 100, 65, "%", 1 },
            { "envMod", "Env Mod", P::Float, 0, 100, 55, "%", 1 },
            { "decay", "Decay", P::Float, 30, 2500, 320, "ms", 1, 0.4f },
            { "accent", "Accent", P::Float, 0, 100, 60, "%", 1 },
            { "fDrive", "Filter Drive", P::Float, 0, 100, 20, "%", 1 },
            { "slide", "Slide Time", P::Float, 10, 400, 60, "ms", 1, 0.5f },
            { "gateLen", "Gate Length", P::Float, 10, 100, 55, "%", 1 },

            { "lfoShape", "LFO Shape", P::Choice, 0, 4, LfoSine, "", 1, 1, { "SINE", "TRI", "SAW", "SQUARE", "S&H" } },
            { "lfoRate", "LFO Rate", P::Choice, 0, 15, 1, "", 1, 1, skazi::noteLengthNames() },
            { "lfoDepth", "LFO Depth", P::Float, 0, 100, 0, "%", 1 },
            { "lfoTarget", "LFO Target", P::Choice, 0, 3, ToCutoff, "", 1, 1, { "CUTOFF", "RESO", "PITCH", "DRIVE" } },
            { "drift", "Drift", P::Float, 0, 100, 0, "%", 1 },

            { "distType", "Distortion", P::Choice, 0, 4, Crunch, "", 1, 1, { "CRUNCH", "FUZZ", "FOLD", "RECTIFY", "CRUSH" } },
            { "distDrive", "Dist Drive", P::Float, 0, 100, 25, "%", 1 },
            { "distTone", "Dist Tone", P::Float, 0, 100, 70, "%", 1 },
            { "distMix", "Dist Mix", P::Float, 0, 100, 100, "%", 1 },

            { "dlyTime", "Delay Time", P::Choice, 0, 15, 11, "", 1, 1, skazi::noteLengthNames() },
            { "dlyFb", "Delay Feedback", P::Float, 0, 90, 35, "%", 1 },
            { "dlyMix", "Delay Mix", P::Float, 0, 100, 0, "%", 1 },
            { "revSize", "Reverb Size", P::Float, 0, 100, 40, "%", 1 },
            { "revMix", "Reverb Mix", P::Float, 0, 100, 0, "%", 1 },
            { "volume", "Volume", P::Float, -24, 6, 0, "dB", 0.1f },

            // 1.1: more filter, a full amp envelope and OTT. The defaults sound exactly like 1.0.
            { "fltType", "Filter Type", P::Choice, 0, 3, F24, "", 1, 1, { "24 dB", "18 dB", "12 dB", "BAND" } },
            { "keyTrack", "Key Track", P::Float, 0, 100, 0, "%", 1 },
            { "fAttack", "Filter Attack", P::Float, 0, 1000, 0, "ms", 1, 0.35f },
            { "ampAtk", "Amp Attack", P::Float, 1, 1000, 2, "ms", 1, 0.35f },
            { "ampDec", "Amp Decay", P::Float, 10, 5000, 1000, "ms", 1, 0.35f },
            { "ampSus", "Amp Sustain", P::Float, 0, 100, 100, "%", 1 },
            { "ampRel", "Amp Release", P::Float, 2, 3000, 6, "ms", 1, 0.35f },
            { "vca", "VCA Power", P::Float, 0, 100, 50, "%", 1 },
            { "glideOn", "Glide", P::Bool, 0, 1, 0 },
            { "glide", "Glide Time", P::Float, 5, 1000, 120, "ms", 1, 0.4f },
            { "ottDepth", "OTT Depth", P::Float, 0, 100, 0, "%", 1 },
            { "ottTime", "OTT Time", P::Float, 0, 100, 30, "%", 1 },
            { "seqKey", "Track Key", P::Choice, 0, 11, 0, "", 1, 1, { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" } },

            { "playMode", "Play Mode", P::Choice, 0, 2, ModeSeq, "", 1, 1, { "SEQ", "ARP", "MIDI" } },
            { "arpMode", "Arp Mode", P::Choice, 0, 4, ArpUp, "", 1, 1, { "UP", "DOWN", "UP/DOWN", "RANDOM", "PLAYED" } },
            { "arpOct", "Arp Octaves", P::Int, 1, 3, 1 },
            { "seqRate", "Seq Rate", P::Choice, 0, 3, R16, "", 1, 1, { "1/8", "1/16", "1/16T", "1/32" } },
            { "seqLen", "Seq Length", P::Int, 1, 16, 16 },
            { "swing", "Swing", P::Float, 0, 75, 0, "%", 1 },
            { "chance", "Chance", P::Float, 0, 100, 100, "%", 1 },
            { "mutate", "Mutate", P::Float, 0, 100, 0, "%", 1 },
            { "ratchet", "Ratchet", P::Float, 0, 100, 0, "%", 1 },
        };

        // ParamDef keeps a const char*, so the step IDs need storage that lives as long as the table
        static std::vector<std::string> ids;
        static std::vector<std::string> names;
        ids.reserve (NumSteps * 5);
        names.reserve (NumSteps * 5);
        const char* fields[] = { "note", "oct", "gate", "acc", "sld" };
        const char* labels[] = { "Note", "Octave", "Gate", "Accent", "Slide" };
        for (int i = 0; i < NumSteps; ++i)
            for (int f = 0; f < 5; ++f)
            {
                ids.push_back (stepId (i, fields[f]).toStdString());
                names.push_back ("Step " + std::to_string (i + 1) + " " + labels[f]);
                const char* id = ids.back().c_str();
                const char* nm = names.back().c_str();
                switch (f)
                {
                    case 0: d.push_back ({ id, nm, P::Int, 0, 12, 0 }); break;
                    case 1: d.push_back ({ id, nm, P::Choice, 0, 2, Mid, "", 1, 1, { "DOWN", "MID", "UP" } }); break;
                    case 2: d.push_back ({ id, nm, P::Choice, 0, 2, Note, "", 1, 1, { "REST", "NOTE", "TIE" } }); break;
                    default: d.push_back ({ id, nm, P::Bool, 0, 1, 0 }); break;
                }
            }
        return d;
    }();
    return defs;
}

// ============================================================================================ engine

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    osr = sampleRate * 2.0;

    // half-band low-pass for the 2x -> 1x step (windowed sinc, Blackman)
    double sum = 0.0;
    for (int n = 0; n < FirLen; ++n)
    {
        const double m = n - (FirLen - 1) / 2.0, x = m * 0.5;
        const double sinc = m == 0.0 ? 1.0 : std::sin (MathConstants<double>::pi * x) / (MathConstants<double>::pi * x);
        const double w = 0.42 - 0.5 * std::cos (2.0 * MathConstants<double>::pi * n / (FirLen - 1))
                       + 0.08 * std::cos (4.0 * MathConstants<double>::pi * n / (FirLen - 1));
        fir[(size_t) n] = (float) (0.5 * sinc * w);
        sum += fir[(size_t) n];
    }
    for (auto& h : fir) h = (float) (h / sum);

    const size_t dlyLen = (size_t) (sampleRate * 4.5) + 4; // a dotted half note at 40 bpm
    dlyL.assign (dlyLen, 0.0f);
    dlyR.assign (dlyLen, 0.0f);
    reverb.setSampleRate (sampleRate);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, maxBlock), 1 };
    otLow.prepare (spec);  otLow.setCutoffFrequency (160.0f);
    otHigh.prepare (spec); otHigh.setCutoffFrequency (2500.0f);
    reset();
}

void Engine::reset()
{
    phase = subPhase = 0.0;
    gate = accented = sliding = false;
    amp = fEnv = accEnv = accCap = 0.0f;
    fRising = false; ampStage = 0;
    otLow.reset(); otHigh.reset();
    for (int b = 0; b < 3; ++b) { otEnv[b] = 0.0f; otGain[b] = 1.0f; }
    y1 = y2 = y3 = y4 = hpState = 0.0f;
    toneState = crushHold = crushCount = dcIn = dcOut = 0.0f;
    firBuf.fill (0.0f);
    firPos = 0;
    std::fill (dlyL.begin(), dlyL.end(), 0.0f);
    std::fill (dlyR.begin(), dlyR.end(), 0.0f);
    dlyPos = 0;
    dlySmooth = 0.0f;
    reverb.reset();
    heldCount = 0;
    lastCount = -1;
    curIndex = -1;
    wasRunning = false;
    playStep = -1;
    cutSmooth = -1.0f;
}

void Engine::noteOn (int midiNote, bool accent, bool legato)
{
    pitchTarget = (float) midiNote;
    if (legato && gate)
    {
        sliding = true; // 303 slide: glide there, no new envelope
    }
    else
    {
        if (! portamento) pitchNow = pitchTarget;
        sliding = false;
        fRising = true;   // renderVoice jumps to the peak, or rises there over FILTER ATTACK
        ampStage = 0;
        if (accent) accEnv = 1.0f;
    }
    accented = accent;
    gate = true;
    if (rng.uni() < 0.3f) driftTarget = rng.uni() * 2.0f - 1.0f;
}

void Engine::gateOff() { gate = false; }

float Engine::decimate (float a, float b)
{
    firBuf[(size_t) firPos] = firBuf[(size_t) (firPos + FirLen)] = a;
    firPos = (firPos + 1) % FirLen;
    firBuf[(size_t) firPos] = firBuf[(size_t) (firPos + FirLen)] = b;
    firPos = (firPos + 1) % FirLen;
    float y = 0.0f;
    for (int k = 0; k < FirLen; ++k)
        y += fir[(size_t) k] * firBuf[(size_t) (firPos + k)];
    return y;
}

float Engine::renderVoice (const Settings& s, float lfo, float& second)
{
    // pitch: jump, or glide when sliding
    if (sliding)        pitchNow += (pitchTarget - pitchNow) * coefFor (s.slideMs * 0.001 * 0.45, sr);
    else if (s.glideOn) pitchNow += (pitchTarget - pitchNow) * coefFor (s.glideMs * 0.001 * 0.45, sr);
    else                pitchNow = pitchTarget;
    const float pitch = pitchNow + s.tune + s.fine * 0.01f + (s.lfoTarget == ToPitch ? lfo * 2.0f * s.lfoDepth : 0.0f);
    const double freq = 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0);

    // envelopes: accented notes use the short decay, like the original
    if (fRising)
    {
        if (s.fAttack < 0.5f) { fEnv = 1.0f; fRising = false; }
        else
        {
            fEnv += (1.05f - fEnv) * coefFor (s.fAttack * 0.001 * 0.5, sr);
            if (fEnv >= 1.0f) { fEnv = 1.0f; fRising = false; }
        }
    }
    else
        fEnv *= 1.0f - coefFor ((accented ? 0.2 : s.decay * 0.001), sr);
    accEnv *= 1.0f - coefFor (0.2, sr);
    // accent sweep capacitor: charges slower than it follows, so accents in a row stack up
    accCap += (accEnv - accCap) * (accEnv > accCap ? coefFor (0.03 + 0.09 * s.reso, sr) : coefFor (0.25, sr));
    // amp ADSR (the defaults, 2 ms attack, full sustain, 6 ms release, are the 1.0 gate)
    if (! gate)              amp += (0.0f - amp) * coefFor (s.ampRel * 0.001, sr);
    else if (ampStage == 0)
    {
        amp += (1.0f - amp) * coefFor (s.ampAtk * 0.001, sr);
        if (amp > 0.995f) ampStage = 1;
    }
    else                     amp += (s.ampSus - amp) * coefFor (s.ampDec * 0.001 * 0.4, sr);

    // cutoff in octaves; the knob part is smoothed, the envelopes are not
    const float knob = std::log2 (s.cutoff) - s.envMod * 0.8f;
    if (cutSmooth < 0.0f) cutSmooth = knob;
    cutSmooth += (knob - cutSmooth) * coefFor (0.015, sr);
    driftNow += (driftTarget - driftNow) * coefFor (0.5, sr);
    const float octs = cutSmooth + s.envMod * 4.2f * fEnv + s.accent * accCap * 2.6f
                     + (s.lfoTarget == ToCutoff ? lfo * s.lfoDepth * 3.0f : 0.0f) + driftNow * s.drift * 1.6f
                     + s.keyTrack * (pitchNow - 48.0f) / 12.0f;
    const float fc = juce::jlimit (25.0f, (float) juce::jmin (18000.0, osr * 0.42), std::exp2 (octs));
    cutoffNow.store (fc, std::memory_order_relaxed);

    float reso = s.reso + (s.lfoTarget == ToReso ? lfo * s.lfoDepth * 0.5f : 0.0f);
    float k = juce::jlimit (0.0f, 1.05f, reso) * 4.15f;
    k *= 0.55f + 0.45f * juce::jlimit (0.0f, 1.0f, (fc - 80.0f) / 600.0f); // less resonance down low, like the 303
    const float g = 1.0f - (float) std::exp (-MathConstants<double>::twoPi * fc / osr);
    const float accGain = 1.0f + (accented ? s.accent * accEnv * 1.4f : 0.0f);
    const float pre = 1.0f + s.fDrive * 3.0f;
    // VCA: accents and VCA POWER push the filter output into a soft clip before the amp envelope,
    // so quiet parts come up, accents punch and the bass stays full when the resonance is high
    const float vg = 1.0f + s.vca * 4.0f;
    const float body = (1.0f + 0.45f * k) / (1.0f + s.fDrive * 1.5f) * accGain * 0.3f * vg;
    const float post = amp * 1.3f / std::sqrt (vg);

    const float drv = juce::jlimit (0.0f, 1.0f, s.distDrive + (s.lfoTarget == ToDrive ? lfo * s.lfoDepth * 0.5f : 0.0f));
    const float dg = 1.0f + drv * drv * 40.0f;
    // heavier drive squashes harder, so scale the wet signal to stay about as loud as the clean one
    const float norm = 0.22f / juce::jmax (0.05f, std::tanh (0.22f * dg));
    const float toneC = (float) (1.0 - std::exp (-MathConstants<double>::twoPi * 600.0 * std::pow (2.0, s.distTone * 5.0) / osr));
    const double dt = freq / osr;

    float out[2];
    for (int j = 0; j < 2; ++j)
    {
        phase += dt;      if (phase >= 1.0) phase -= 1.0;
        subPhase += dt * 0.5; if (subPhase >= 1.0) subPhase -= 1.0;
        float osc;
        if (s.wave == Saw)
            osc = (float) (2.0 * phase - 1.0) - polyBlep (phase, dt);
        else
        {
            double p2 = phase + 0.5; if (p2 >= 1.0) p2 -= 1.0;
            osc = 0.85f * ((phase < 0.5 ? 1.0f : -1.0f) + polyBlep (phase, dt) - polyBlep (p2, dt));
        }
        if (s.sub > 0.0f)
        {
            double q2 = subPhase + 0.5; if (q2 >= 1.0) q2 -= 1.0;
            osc += s.sub * 0.8f * ((subPhase < 0.5 ? 1.0f : -1.0f) + polyBlep (subPhase, dt * 0.5) - polyBlep (q2, dt * 0.5));
        }

        // 4-pole ladder, saturating stages
        const float u = fastTanh (osc * pre - k * y4);
        const float t1 = fastTanh (y1), t2 = fastTanh (y2), t3 = fastTanh (y3);
        y1 += g * (u - t1);
        y2 += g * (t1 - t2);
        y3 += g * (t2 - t3);
        y4 += g * (t3 - fastTanh (y4));
        float tap;
        switch (s.fltType)
        {
            case F18:   tap = y3; break;
            case F12:   tap = y2 * 0.9f; break;
            case FBand: tap = (y2 - y4) * 2.2f; break;
            default:    tap = y4; break;
        }
        float x = fastTanh (tap * body) * post;

        // distortion
        float wet;
        switch (s.distType)
        {
            case Fuzz:    wet = juce::jlimit (-1.0f, 0.8f, x * dg + 0.15f * drv) * norm; break;
            case Fold:    wet = std::sin (x * dg * 1.3f) * norm * 0.8f; break;
            case Rectify: wet = fastTanh (dg * (0.45f * x + 0.8f * std::abs (x) - 0.2f * drv)) * norm; break;
            case Crush:
            {
                if (--crushCount <= 0.0f) { crushHold = x; crushCount = 1.0f + drv * 22.0f; }
                const float q = std::exp2 (14.0f - drv * 11.0f);
                wet = fastTanh (std::round (crushHold * q) / q * (1.0f + drv * 2.0f)) * norm;
                break;
            }
            default:      wet = fastTanh (x * dg) * norm; break;
        }
        toneState += (wet - toneState) * toneC;
        out[j] = x + (toneState - x) * s.distMix;
    }
    second = out[1];
    return out[0];
}

// ============================================================================================ sequencer

void Engine::rollMutations (const Settings& s, const Pattern& pat)
{
    live = pat;
    liveMask = s.mutate > 0.0f ? mutateSteps (live, s.seqLen, s.mutate, [this] { return rng.uni(); }) : 0u;
    mutatedMask.store (liveMask, std::memory_order_relaxed);
}

void Engine::startStep (const Settings& s, const Pattern& pat, long long count)
{
    const int len = juce::jlimit (1, NumSteps, s.seqLen);
    const int idx = (int) (((count % len) + len) % len);
    if (idx == 0 || curIndex < 0)
        rollMutations (s, pat);
    curIndex = idx;
    playStep.store (idx, std::memory_order_relaxed);

    const Step st = live[(size_t) idx];
    const Step next = live[(size_t) ((idx + 1) % len)];
    holdThrough = (st.sld && next.gate != Rest) || next.gate == Tie;
    ratchets = 1;
    lastSub = 0;

    if (st.gate == Rest) { stepPlays = false; gateOff(); return; }
    if (st.gate == Tie)
    {
        stepPlays = gate; // a tie only continues a note that is still sounding
        return;
    }
    if (rng.uni() > s.chance) { stepPlays = false; gateOff(); return; }

    const bool legato = gate; // the previous step slid into this one
    if (! legato && s.ratchet > 0.0f && rng.uni() < s.ratchet * 0.5f)
        ratchets = rng.uni() < 0.7f ? 2 : 3;
    cur = st;
    if (s.mode == ModeArp)
    {
        if (heldCount == 0) { stepPlays = false; gateOff(); return; }
        curNote = juce::jlimit (0, 127, arpNote (s) + (st.oct - Mid) * 12); // the grid gives the rhythm, the keys give the notes
    }
    else
        curNote = juce::jlimit (0, 127, 36 + s.key + st.note + (st.oct - Mid) * 12 + transpose());
    noteOn (curNote, st.acc, legato);
    stepPlays = true;
}

int Engine::arpNote (const Settings& s)
{
    // held keys (sorted, or in the order played), repeated over the octave range
    int notes[16 * 3]; int n = 0;
    int keys[16];
    for (int k = 0; k < heldCount; ++k) keys[k] = held[(size_t) k];
    if (s.arpMode != ArpPlayed) std::sort (keys, keys + heldCount);
    for (int o = 0; o < juce::jlimit (1, 3, s.arpOct); ++o)
        for (int k = 0; k < heldCount; ++k) notes[n++] = keys[k] + 12 * o;

    const long long pos = arpPos++;
    int idx;
    switch (s.arpMode)
    {
        case ArpDown:   idx = n - 1 - (int) (pos % n); break;
        case ArpUpDown: { const int period = juce::jmax (1, 2 * n - 2); const int k = (int) (pos % period); idx = k < n ? k : period - k; break; }
        case ArpRandom: idx = rng.below (n); break;
        default:        idx = (int) (pos % n); break;
    }
    return notes[juce::jlimit (0, n - 1, idx)];
}

void Engine::runSequencer (const Settings& s, const Pattern& pat, double p, bool running)
{
    if (! running)
    {
        if (wasRunning) { gateOff(); playStep = -1; lastCount = -1; curIndex = -1; wasRunning = false; }
        return;
    }
    wasRunning = true;

    const double sb = stepBeats (s.seqRate), sw = s.swing * 0.5 * sb;
    const double pair = std::floor (p / (2.0 * sb)), within = p - pair * 2.0 * sb;
    const bool odd = within >= sb + sw;
    const double ph = juce::jlimit (0.0, 0.9999, odd ? (within - sb - sw) / (sb - sw) : within / (sb + sw));
    const long long count = (long long) pair * 2 + (odd ? 1 : 0);

    if (count != lastCount)
    {
        lastCount = count;
        startStep (s, pat, count);
    }
    if (! stepPlays) return;

    const int sub = juce::jmin (ratchets - 1, (int) (ph * ratchets));
    if (sub != lastSub)
    {
        lastSub = sub;
        noteOn (curNote, cur.acc, false);
    }
    const double subPh = ph * ratchets - sub;
    const bool hold = holdThrough && sub == ratchets - 1;
    if (gate && ! hold && subPh >= s.gateLen)
        gateOff();
}

// ============================================================================================ block

void Engine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& s, const Pattern& pat,
                      const skazi::HostTime& ht)
{
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();
    buffer.clear();
    if (chans == 0 || n == 0) return;
    float* L = buffer.getWritePointer (0);
    float* R = chans > 1 ? buffer.getWritePointer (1) : nullptr;

    const double beatsPerSample = ht.bpm / 60.0 / sr;
    const double lfoBeats = skazi::noteBeats (s.lfoRate);
    const float delayTarget = (float) juce::jlimit (1.0, (double) dlyL.size() - 3.0, skazi::noteBeats (s.dlyTime) * 60.0 / ht.bpm * sr);
    if (dlySmooth <= 0.0f) dlySmooth = delayTarget;
    const float fb = s.dlyFb, dmix = s.dlyMix;
    const float dcC = coefFor (1.0 / (2.0 * MathConstants<double>::pi * 25.0), sr);

    portamento = s.glideOn;
    auto evt = midi.cbegin();
    for (int i = 0; i < n; ++i)
    {
        for (; evt != midi.cend() && (*evt).samplePosition <= i; ++evt)
        {
            const auto m = (*evt).getMessage();
            if (m.isNoteOn())
            {
                const int note = m.getNoteNumber();
                int w = 0;
                for (int k = 0; k < heldCount; ++k) if (held[(size_t) k] != note) held[(size_t) w++] = held[(size_t) k];
                heldCount = w;
                if (heldCount < (int) held.size()) held[(size_t) heldCount++] = note;
                const bool firstKey = heldCount == 1;
                lastHeld = note;
                if (firstKey) arpPos = 0;
                if (s.mode == ModeMidi)
                    noteOn (note, m.getVelocity() >= 100, heldCount > 1);
                else if (firstKey && (! ht.playing || s.mode == ModeArp))
                {
                    freeBeats = 0.0; // a held key starts the pattern from the top when the host is stopped
                    lastCount = -1;
                    curIndex = -1;
                }
            }
            else if (m.isNoteOff())
            {
                const int note = m.getNoteNumber();
                int w = 0;
                for (int k = 0; k < heldCount; ++k) if (held[(size_t) k] != note) held[(size_t) w++] = held[(size_t) k];
                heldCount = w;
                if (heldCount > 0) lastHeld = held[(size_t) heldCount - 1];
                if (s.mode == ModeMidi)
                {
                    if (heldCount == 0) gateOff();
                    else if ((int) pitchTarget == note) noteOn (lastHeld, accented, true);
                }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                heldCount = 0;
                gateOff();
            }
        }

        // clock: the host's position while it plays, otherwise our own while a key is held
        // the arp only runs while keys are held, and starts from its first step on the first key
        const bool running = s.mode == ModeSeq ? (ht.playing || heldCount > 0) : (s.mode == ModeArp && heldCount > 0);
        const bool hostClock = ht.playing && s.mode == ModeSeq;
        double beat;
        if (hostClock) beat = ht.ppq + i * beatsPerSample;
        else { beat = freeBeats; freeBeats += beatsPerSample; }
        if (s.mode != ModeMidi) runSequencer (s, pat, beat, running);
        else if (wasRunning) { wasRunning = false; playStep = -1; }

        // LFO
        double lp;
        if (ht.playing) lp = beat / lfoBeats;
        else { lfoPhase += beatsPerSample / lfoBeats; lp = lfoPhase; }
        const int cycle = (int) std::floor (lp);
        lp -= std::floor (lp);
        float lfo;
        switch (s.lfoShape)
        {
            case LfoTri:    lfo = 1.0f - 4.0f * (float) std::abs (lp - 0.5); break;
            case LfoSaw:    lfo = 1.0f - 2.0f * (float) lp; break;
            case LfoSquare: lfo = lp < 0.5 ? 1.0f : -1.0f; break;
            case LfoSH:     if (cycle != lastShCycle) { lastShCycle = cycle; shValue = rng.uni() * 2.0f - 1.0f; } lfo = shValue; break;
            default:        lfo = (float) std::sin (lp * MathConstants<double>::twoPi); break;
        }

        float b;
        const float a = renderVoice (s, lfo, b);
        float mono = decimate (a, b);
        // DC blocker (the fuzz and rectifier are lopsided on purpose)
        dcIn += (mono - dcIn) * dcC;
        mono -= dcIn;
        if (! std::isfinite (mono)) { reset(); mono = 0.0f; }
        if (s.ottDepth > 0.0f) mono = ott (mono, s);

        // ping-pong delay
        dlySmooth += (delayTarget - dlySmooth) * 0.0005f;
        const int size = (int) dlyL.size();
        float rp = (float) dlyPos - dlySmooth;
        if (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp, i1 = (i0 + 1) % size;
        const float fr = rp - (float) i0;
        const float dl = dlyL[(size_t) i0] + (dlyL[(size_t) i1] - dlyL[(size_t) i0]) * fr;
        const float dr = dlyR[(size_t) i0] + (dlyR[(size_t) i1] - dlyR[(size_t) i0]) * fr;
        dlyL[(size_t) dlyPos] = fastTanh (mono * (dmix > 0.0f ? 1.0f : 0.0f) + dr * fb);
        dlyR[(size_t) dlyPos] = fastTanh (dl * fb);
        dlyPos = (dlyPos + 1) % size;

        L[i] = mono + dl * dmix;
        if (R != nullptr) R[i] = mono + dr * dmix;
    }

    if (s.revMix > 0.0f && R != nullptr)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.68f * s.revSize;
        rp.damping = 0.45f;
        rp.wetLevel = s.revMix * 0.45f;
        rp.dryLevel = 1.0f;
        rp.width = 1.0f;
        reverb.setParameters (rp);
        reverb.processStereo (L, R, n);
    }

    const float gain = juce::Decibels::decibelsToGain (s.volume);
    float peak = 0.0f;
    for (int c = 0; c < juce::jmin (2, chans); ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            d[i] = skazi::safeClip (d[i] * gain);
            peak = juce::jmax (peak, std::abs (d[i]));
        }
    }
    for (int c = 2; c < chans; ++c) buffer.clear (c, 0, n);
    levelFollow = juce::jmax (peak, levelFollow * 0.9f);
    outLevel.store (levelFollow, std::memory_order_relaxed);
}

float Engine::ott (float x, const Settings& s)
{
    const float att = std::exp (-1.0f / (float) ((0.001 + 0.03 * s.ottTime) * sr));
    const float rel = std::exp (-1.0f / (float) ((0.03 + 0.4 * s.ottTime) * sr));
    const float gSm = std::exp (-1.0f / (float) (0.003 * sr));
    float band[3], rest;
    otLow.processSample (0, x, band[0], rest);
    otHigh.processSample (0, rest, band[1], band[2]);
    float wet = 0.0f;
    for (int b = 0; b < 3; ++b)
    {
        const float lv = std::abs (band[b]);
        otEnv[b] = lv > otEnv[b] ? att * otEnv[b] + (1 - att) * lv : rel * otEnv[b] + (1 - rel) * lv;
        const float db = juce::Decibels::gainToDecibels (otEnv[b], -100.0f);
        float gdb = 0.0f;
        if (db > -14.0f) gdb -= (db + 14.0f) * 0.75f;                                           // loud: pushed down
        else if (db > -75.0f && db < -34.0f) gdb += juce::jmin (18.0f, (-34.0f - db) * 0.6f);   // quiet: pulled up
        otGain[b] = gSm * otGain[b] + (1 - gSm) * juce::Decibels::decibelsToGain (gdb + 4.0f);
        wet += band[b] * otGain[b];
    }
    return x + (wet - x) * s.ottDepth;
}

// ============================================================================================ pattern tools

namespace patterns
{
Pattern generate (juce::Random& r, int style)
{
    struct Recipe { std::vector<int> notes; float density, acc, sld, up, down, tie; };
    static const Recipe recipes[] = {
        { { 0, 0, 3, 5, 7, 10, 12 },     0.75f, 0.30f, 0.20f, 0.15f, 0.10f, 0.05f }, // Classic
        { { 0, 0, 0, 0, 12, 7, 10 },     0.92f, 0.25f, 0.12f, 0.20f, 0.05f, 0.00f }, // Rolling
        { { 0, 0, 1, 3, 7, 12 },         0.70f, 0.45f, 0.35f, 0.15f, 0.15f, 0.05f }, // Squelch
        { { 0, 3, 5, 7, 10, 12 },        0.85f, 0.35f, 0.15f, 0.30f, 0.05f, 0.00f }, // Rave
        { { 0, 0, 7, 5, 3 },             0.45f, 0.20f, 0.30f, 0.05f, 0.20f, 0.15f }, // Dub
        { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }, 0.65f, 0.40f, 0.30f, 0.25f, 0.25f, 0.08f }, // Mutant
    };
    const auto& rc = recipes[juce::jlimit (0, 5, style)];
    Pattern p;
    for (int i = 0; i < NumSteps; ++i)
    {
        auto& st = p[(size_t) i];
        const bool onBeat = i % 4 == 0;
        const float dens = onBeat ? juce::jmin (1.0f, rc.density + 0.2f) : rc.density;
        if (i > 0 && r.nextFloat() < rc.tie && p[(size_t) i - 1].gate != Rest) st.gate = Tie;
        else st.gate = (i == 0 || r.nextFloat() < dens) ? Note : Rest;
        st.note = i == 0 ? 0 : rc.notes[(size_t) r.nextInt ((int) rc.notes.size())];
        const float o = r.nextFloat();
        st.oct = i == 0 ? Mid : (o < rc.up ? Up : (o < rc.up + rc.down ? Down : Mid));
        st.acc = r.nextFloat() < (onBeat ? rc.acc * 1.4f : rc.acc);
        st.sld = r.nextFloat() < rc.sld;
    }
    return p;
}

Pattern mutate (const Pattern& p, juce::Random& r, float amount)
{
    Pattern out = p;
    mutateSteps (out, NumSteps, amount, [&r] { return r.nextFloat(); });
    return out;
}

Pattern shift (const Pattern& p, int direction, int length)
{
    length = juce::jlimit (1, NumSteps, length);
    Pattern out = p;
    for (int i = 0; i < length; ++i)
        out[(size_t) ((i + direction + length * 2) % length)] = p[(size_t) i];
    return out;
}
} // namespace patterns
} // namespace skacid
