#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../Common/UpdateNotice.h"

using namespace skatom;

namespace
{
constexpr int kCtl = 32; // control-rate sub-block (macro, matrix, LFOs, env follower, coefficient updates)
const double FORMANTS[5][3] = { { 800, 1150, 2900 }, { 350, 2000, 2800 }, { 270, 2140, 2950 }, { 450, 800, 2830 }, { 325, 700, 2700 } }; // A E I O U
float dbToGain (float d) { return std::pow (10.0f, d / 20.0f); }
float smoothstep (float a, float b, float x) { const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a)); return t * t * (3 - 2 * t); }

// Mega-Macro build-up curves — identical to CURVES in state.js
float curve (int i, float x)
{
    switch (i)
    {
        case 0: return x < 0.2f ? 0 : (std::exp (4.5f * (x - 0.2f) / 0.8f) - 1) / (std::exp (4.5f) - 1);
        case 1: return std::pow (x, 1.8f);
        case 2: return std::pow (x, 2.7f);
        case 3: return x <= 0.85f ? smoothstep (0.1f, 0.85f, x) : 1 - 0.18f * (x - 0.85f) / 0.15f;
        default: return x < 0.55f ? 0 : std::pow ((x - 0.55f) / 0.45f, 2.0f);
    }
}
float lfoShape (int s, double ph, float rnd)
{
    switch (s)
    {
        case 0: return (float) std::sin (ph * kTau);
        case 1: return (float) (1 - 4 * std::abs (std::fmod (ph + 0.25, 1.0) - 0.5));
        case 2: return (float) (2 * ph - 1);
        case 3: return ph < 0.5 ? 1.0f : -1.0f;
        default: return rnd;
    }
}
juce::AudioBuffer<float> makeIR (double sr, double seconds, float bright, int seed, float shape = 2.2f, float predelay = 0, bool reverse = false)
{
    const int pre = (int) (predelay * sr), n = (int) (seconds * sr) + pre, fade = (int) (0.03 * sr);
    juce::AudioBuffer<float> ir (2, n);
    ir.clear();
    juce::Random r (seed);
    for (int c = 0; c < 2; ++c)
    {
        float lp = 0;
        for (int i = pre; i < n; ++i)
        {
            const float t = (float) (i - pre) / (n - pre);
            lp += bright * ((r.nextFloat() * 2 - 1) - lp);
            // reverse: swell up, then a 30 ms cut so it slams into the beat
            const float env = reverse ? std::pow (t, shape) * std::min (1.0f, (float) (n - i) / fade) : std::pow (1.0f - t, shape);
            ir.setSample (c, i, lp * env);
        }
    }
    return ir;
}
}

SkatomProcessor::SkatomProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SKATOM", makeLayout())
{
    loader.startThread();
    for (int i = 0; i < NUM_PARAMS; ++i)
        raw[(size_t) i] = apvts.getRawParameterValue (defs()[(size_t) i].id);
    for (auto& s : snapMsg) s.fill (-1);
    snapRT = snapMsg;
}

bool SkatomProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == l.getMainInputChannelSet() && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void SkatomProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    glitch.prepare (sr);

    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    os->initProcessing (kCtl);
    latency = (int) std::lround (os->getLatencyInSamples());
    setLatencySamples (latency);

    for (auto* b : { &filtA, &form[0], &form[1], &form[2], &bassShelf, &bassMud, &hpf1, &hpf2, &hpfSend, &hpfSend2, &noiseBP, &dlyLP }) b->reset();
    dlyLP.set (Biquad::LP, 3500, 0.707, 0, sr);
    chorusBuf.assign ((size_t) (sr * 0.1), 0);
    for (auto& b : jetBuf) b.assign ((size_t) (sr * 0.02), 0);
    for (auto& b : dlyBuf) b.assign ((size_t) (sr * 2) + 1, 0);
    for (auto& b : dryBuf) b.assign ((size_t) latency + kCtl + 1, 0);
    chorusW = jetW = dlyW = dryW = 0; chPh[0] = chPh[1] = 0; spinPh = 0;
    duck = 1; duckHold = 0; rollGain = 1; riseProg = 0; riseDone = false;
    building = false; activeBuild = 0;
    xs = raw[xyX]->load(); ys = raw[xyY]->load();

    // two halls: a tight bright room and a huge dark wash — WASH crossfades small → huge ("the reverb opens")
    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) kCtl, 2 };
    verbS.prepare (spec); verbL.prepare (spec);
    using C = juce::dsp::Convolution;
    verbS.loadImpulseResponse (makeIR (sr, 1.4, 0.55f, 11), sr, C::Stereo::yes, C::Trim::no, C::Normalise::yes);
    pendingSr = sr; builtKey = -1; ++gen; // the loader thread (re)builds the tail IR for this sample rate
    delete pendingIR.exchange (nullptr);
    shim.prepare (sr);
    shep[0].prepare (sr); shep[1].prepare (sr); gEndless.snap (0); endlessMix = endlessOct = 0;
    tailFb.setSize (2, kCtl); tailFb.clear();
    riseArmed = false;
    lim.prepare (spec); lim.setRelease (80);
    glue.prepare (sr);
    sendS.setSize (2, kCtl); sendL.setSize (2, kCtl); dlyIn.setSize (2, kCtl); dryCopy.setSize (2, kCtl);
    monoR.setSize (1, juce::jmax (samplesPerBlock, kCtl));

    for (auto* g : { &gFormDry, &gFormWet, &gDistWet, &gDistDry, &gNoise, &gChorus, &gSend, &gSize, &gJet, &gSwallow, &gWet, &gDry, &gOut, &gMake, &gPump, &gWidth }) g->snap (0);
    pumpG = 1;
    gBloom.snap (1);
    gFormDry.snap (1); gDistWet.snap (1); gWet.snap (1); gOut.snap (1); gMake.snap (1);
}

juce::AudioBuffer<float> SkatomProcessor::makeTailIR (double sr, const TailDef& t, int type)
{
    return makeIR (sr, t.seconds, t.bright, 12 + type, t.shape, t.predelay, t.reverse);
}

void SkatomProcessor::setSnaps (const juce::String& json)
{
    SnapTable t;
    for (auto& s : t) s.fill (-1);
    const auto v = juce::JSON::parse (json);
    if (auto* arr = v.getArray())
        for (int i = 0; i < juce::jmin (16, arr->size()); ++i)
            if (auto* obj = (*arr)[i].getDynamicObject())
                for (auto& prop : obj->getProperties())
                    for (int p = 0; p < NUM_PARAMS; ++p)
                        if (isSnap (p) && prop.name.toString() == defs()[(size_t) p].id)
                            t[(size_t) i][(size_t) p] = juce::jlimit (0.0f, 1.0f, (float) prop.value);
    {
        const juce::ScopedLock l (snapLock);
        snapsJson = json;
    }
    {
        const juce::SpinLock::ScopedLockType l (snapSpin);
        snapMsg = t;
    }
    ++snapVer;
}

float SkatomProcessor::eff (int id) const
{
    const auto& p = defs()[(size_t) id];
    const float base = raw[(size_t) id]->load();
    if (p.type != Float) return matEnum[id] >= 0 ? matEnum[id] : base;
    const float off = matOff[id] + macroOff[id] + modOff[id];
    return off == 0 ? base : denorm (p, norm (p, base) + off);
}

void SkatomProcessor::fireDrop()
{
    duck = 1 - raw[dropDuck]->load();
    duckHold = (int) (0.25 * sr);
    hi = 0;
    ++tele.drops;
}

// rise + macro curves + matrix + LFOs + envelope follower → effective values (same maths as SkatomState)
void SkatomProcessor::control (double beats, double blockBeats, bool advance, const float* L, const float* R, int m)
{
    const float dt = (float) m / (float) sr;

    // RISE: the macro rides 0 → 100% over N bars (Endless Smile style), then optionally drops itself
    if (raw[riseOn]->load() > 0.5f)
    {
        // locked to the song position while the DAW plays: progress = where the playhead is, so the build is
        // sample-exact with the track and the drop lands on a bar line (the length is rounded to whole bars)
        const double len = RISE_BARS[(int) raw[riseBars]->load()] * 4.0;
        auto anchor = [&] (double prog) {
            riseStart = beats - prog * len;
            riseEnd = std::max (beats + 1, std::ceil ((riseStart + len - 0.5) / 4) * 4); // next bar line at/after the length
            riseOnHost = hostPlaying;
        };
        if (! riseArmed) { riseArmed = true; riseDone = false; riseProg = 0; anchor (0); }
        if (hostPlaying && beats < riseLast - 0.5) { riseDone = false; anchor (0); } // cycle / jump back: rise again
        riseLast = beats;
        if (! riseDone && advance)
        {
            if (hostPlaying != riseOnHost) anchor (riseProg); // play / stop: keep the progress, re-lock the clock
            if (hostPlaying) riseProg = juce::jlimit (0.0, 1.0, (beats - riseStart) / (riseEnd - riseStart));
            else riseProg += blockBeats / len;
            if (riseProg >= 1)
            {
                riseDone = true;
                if (raw[autoDrop]->load() > 0.5f) { riseProg = 0; fireDrop(); }
                else riseProg = 1;
            }
        }
    }
    else { riseProg = 0; riseDone = false; riseArmed = false; }
    const float x = juce::jmax (raw[macro]->load(), (float) riseProg);
    macroEffNow = x;

    std::fill (std::begin (macroOff), std::end (macroOff), 0.0f);
    std::fill (std::begin (modOff), std::end (modOff), 0.0f);
    macroOff[pitch] += curve (1, x) * raw[risePitch]->load() / 72.0f; // RISE PITCH: up to +48 st in any build
    // BUILD: the build is made from the incoming sound; RANDOM picks a new mode each time a build starts
    const int bm = (int) raw[buildMode]->load();
    if (x > 0.02f && ! building) { building = true; if (bm == BUILD_RANDOM) activeBuild = rng.nextInt (5); }
    else if (x < 0.01f) building = false;
    if (bm != BUILD_RANDOM) activeBuild = bm;
    else if (activeBuild == BUILD_NOISE) activeBuild = rng.nextInt (5); // RANDOM only picks the input-made builds
    // ACELERA CURVE: how the ride is shaped (same as accShape in state.js)
    const int ak = (int) raw[accCurve]->load();
    const float xs2 = ak == 1 ? smoothstep (0, 1, x) : ak == 2 ? x * x : ak == 3 ? std::sqrt (x) : x;
    float c[5];
    for (int i = 0; i < 5; ++i) c[i] = curve (i, xs2);
    // SMILE slots: effects dragged onto the big knob ride it straight (x shaped by CURVE). Any slot in use =
    // the knob drives only the dragged effects (no BUILD routes, no auto wash / squeeze / swallow).
    float aPump = raw[accPump]->load(), aWidth = raw[accWidth]->load(), aEndless = raw[accEndless]->load();
    bool anySlot = false;
    for (int k = 0; k < SMILE_SLOTS; ++k)
    {
        const int t = (int) raw[(size_t) (smile1T + k)]->load();
        const float a = raw[(size_t) (smile1A + k)]->load();
        if (t <= 0) continue;
        anySlot = true;
        if (t == SMILE_PUMP) aPump += a;
        else if (t == SMILE_WIDTH) aWidth += a;
        else if (t == SMILE_ENDLESS) aEndless += a;
        else macroOff[SMILE_PARAM[t]] += xs2 * a;
    }
    aPump = juce::jlimit (0.0f, 1.0f, aPump); aWidth = juce::jlimit (0.0f, 1.0f, aWidth); aEndless = juce::jlimit (0.0f, 1.0f, aEndless);
    if (! anySlot)
    {
        macroOff[wash] += c[1] * 0.95f; macroOff[comp] += c[4] * 0.6f;
        for (auto& r : buildRoutes (activeBuild)) macroOff[r.param] += c[r.curve] * r.amount;
    }
    swallowNow = anySlot ? 0 : c[1] * buildSwallow (activeBuild); // the dry sinks, the reverb of your own sound takes over
    macroOff[distDrive] += c[1] * raw[accDrive]->load();
    // ACELERA PUMP: tempo ducking that deepens with the build and, with ACCEL, speeds up 1/4 → 1/8 → 1/16 → 1/32
    pumpNow = c[3] * aPump; // S-curve: deep by mid-build, not only at the very top
    pumpRate = raw[accAccel]->load() < 0.5f ? 1 : xs2 < 0.5f ? 1 : xs2 < 0.75f ? 2 : xs2 < 0.9f ? 4 : 8;
    widthNow = c[1] * aWidth; // side gain up to x2 = 200% width
    if (raw[accHigh]->load() > 0.5f) macroOff[hpf] += c[0] * 0.45f; // HIGHER: the HPF opens all the way to the top
    // ENDLESS: octaves climbed so far = ride position x (1..8 cycles); fades in over the first 15% of the ride
    endlessOct = xs2 * (1 + 7 * aEndless);
    endlessMix = aEndless * smoothstep (0.0f, 0.15f, xs2);
    bloomNow = 1 + 1.5f * c[1]; // the big tail blooms (+8 dB) as the build opens

    // falling from the red zone to the floor = the drop (works from automation, UI closed)
    hi = std::max (x, hi - dt * 0.5f);
    if (x < 0.08f && hi > 0.7f) fireDrop();

    // matrix: IDW over the 16 snapshots; un-pushed params fall back to the knob ("home") value
    if (snapVer.load() != snapVerRT)
    {
        const juce::SpinLock::ScopedTryLockType l (snapSpin);
        if (l.isLocked())
        {
            snapRT = snapMsg; snapVerRT = snapVer.load();
            for (int p = 0; p < NUM_PARAMS; ++p) { anyPush[p] = false; for (auto& s : snapRT) anyPush[p] |= s[(size_t) p] >= 0; }
        }
    }
    const float kxy = 1 - std::exp (-dt / 0.015f);
    xs += (raw[xyX]->load() - xs) * kxy; ys += (raw[xyY]->load() - ys) * kxy;
    double w[16], sum = 0; int best = 0;
    for (int i = 0; i < 16; ++i)
    {
        const double sx = (i % 4 + 0.5) / 4, sy = (i / 4 + 0.5) / 4;
        sum += w[i] = 1.0 / std::pow (std::hypot (sx - xs, sy - ys) + 1e-3, 3.0);
        if (w[i] > w[best]) best = i;
    }
    for (int p = 0; p < NUM_PARAMS; ++p)
    {
        matOff[p] = 0; matEnum[p] = -1;
        if (! anyPush[p]) continue;
        const auto& d = defs()[(size_t) p];
        if (d.type == Float)
        {
            const float home = norm (d, raw[(size_t) p]->load());
            double s = 0;
            for (int i = 0; i < 16; ++i) { const float v = snapRT[(size_t) i][(size_t) p]; s += w[i] / sum * (v >= 0 ? v : home); }
            matOff[p] = (float) s - home;
        }
        else if (snapRT[(size_t) best][(size_t) p] >= 0)
            matEnum[p] = denorm (d, snapRT[(size_t) best][(size_t) p]);
    }

    for (int k = 1; k <= 2; ++k)
    {
        const int o = k == 1 ? lfo1Rate : lfo2Rate; // rate, shape, depth, target are consecutive
        const double cyc = beats / LFO_BEATS[(int) eff (o)];
        const long n = (long) std::floor (cyc);
        if (n != shN[k]) { shN[k] = n; sh[k] = rng.nextFloat() * 2 - 1; }
        const float v = lfoShape ((int) eff (o + 1), cyc - n, sh[k]);
        modOff[MOD_TARGETS[(int) raw[(size_t) o + 3]->load()]] += v * raw[(size_t) o + 2]->load() * 0.5f;
    }

    float ms = 0;
    for (int i = 0; i < m; ++i) ms += 0.5f * (L[i] * L[i] + R[i] * R[i]);
    envMs += (ms / m - envMs) * (1 - std::exp (-dt / 0.02f));
    const float envT = juce::jlimit (0.0f, 1.0f, std::sqrt (envMs) * (1 + raw[envSens]->load() * 15));
    const float tc = (envT > env ? raw[envAtk]->load() : raw[envRel]->load()) / 1000.0f;
    env += (envT - env) * (1 - std::exp (-dt / tc));
    modOff[MOD_TARGETS[(int) raw[envTarget]->load()]] += env * raw[envDepth]->load();

    for (int i = 0; i < NUM_PARAMS; ++i) effv[i] = eff (i);
    for (auto& nz : MODULE_NEUTRAL) // switched-off modules: effects neutral, matrix/macro pushes included
        if (raw[(size_t) (mod1On + nz.module)]->load() < 0.5f) effv[nz.param] = nz.value;
    if (raw[mod5On]->load() < 0.5f) swallowNow = 0; // the swallow lives in the 05 reverb
}

void SkatomProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    double bpm = 128, ppq = freeBeats; bool playing = false, hasTransport = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hasTransport = true;
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
            if (playing) if (auto p = pos->getPpqPosition()) ppq = *p;
        }
    if (playing && ! wasPlaying) glitch.resetClock();
    wasPlaying = playing;
    hostPlaying = playing;
    freeBeats = ppq + n / sr * bpm / 60; // free-running clock keeps LFOs moving while stopped
    if (dropRequest.exchange (false)) fireDrop();
    if (auto* ir = pendingIR.exchange (nullptr)) // a new TAIL is ready: the move is cheap, the loader frees the shell
    {
        using C = juce::dsp::Convolution;
        verbL.loadImpulseResponse (std::move (*ir), sr, C::Stereo::yes, C::Trim::no, C::Normalise::yes);
        delete trashIR.exchange (ir);
    }

    // mono tracks: run the stereo engine on a duplicated channel and fold back
    float* L = buffer.getWritePointer (0);
    float* R;
    if (buffer.getNumChannels() > 1) R = buffer.getWritePointer (1);
    else
    {
        if (monoR.getNumSamples() < n) monoR.setSize (1, n, false, false, true);
        R = monoR.getWritePointer (0);
        std::copy (L, L + n, R);
    }

    process (L, R, n, ppq, bpm, playing || ! hasTransport);

    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);

    tele.ppq.store (ppq); tele.bpm.store (bpm); tele.playing.store (playing);
    tele.env.store (env); tele.rms.store (std::sqrt (rmsAcc));
    tele.mode.store (glitch.mode); tele.pos.store (glitch.pos); tele.len.store (glitch.len);
    tele.rate.store (glitch.rate); tele.glActive.store (glitch.glActive);
    tele.macroEff.store (macroEffNow); tele.activeBuild.store (activeBuild); tele.riseProg.store ((float) riseProg); tele.riseDone.store (riseDone);
}

void SkatomProcessor::process (float* L, float* R, int n, double ppq, double bpm, bool advance)
{
    const double sr2 = sr * 2, beatPerSample = bpm / 60 / sr;
    for (int s0 = 0; s0 < n; s0 += kCtl)
    {
        const int m = std::min (kCtl, n - s0);
        float* l = L + s0; float* r = R + s0;
        const double beats0 = ppq + s0 * beatPerSample;
        control (beats0, m * beatPerSample, advance, l, r, m);
        const float* e = effv;

        // dry tap, delayed by the oversampler latency
        for (int i = 0; i < m; ++i)
        {
            const int sz = (int) dryBuf[0].size(), rd = (dryW - latency + sz) % sz;
            dryBuf[0][(size_t) dryW] = l[i]; dryBuf[1][(size_t) dryW] = r[i];
            dryCopy.setSample (0, i, dryBuf[0][(size_t) rd]); dryCopy.setSample (1, i, dryBuf[1][(size_t) rd]);
            dryW = (dryW + 1) % sz;
        }

        // 01–02 glitch/spectral and 03–04 filter/dist, in the FX ORDER chosen
        const int order = (int) e[fxOrder];
        const bool glitchLast = order >= 2, distFirst = order == 1 || order == 3;
        auto& gp = glitch.p;
        gp.bpm = bpm; gp.div = (int) e[stutDiv]; gp.stutter = e[stutter] > 0.5f; gp.reverse = e[reverse] > 0.5f; gp.tape = e[tapeStop] > 0.5f;
        gp.freeze = e[freeze] > 0.5f; gp.tapeTime = e[tapeTime]; gp.glitch = e[glitchAmt]; gp.shift = e[freqShift];
        gp.contrast = e[contrast]; gp.resonFreq = e[resonFreq]; gp.tension = e[resonTension]; gp.pitch = e[pitch];
        if (! glitchLast) glitch.process (l, r, m);

        // 03–04 at 2x: (drive → filter A → formant) and (distortion) in either order → magic bass
        static const Biquad::Kind fk[] = { Biquad::LP, Biquad::BP, Biquad::HP };
        filtA.set (fk[(int) e[filtType]], e[filtCut], e[filtRes], 0, sr2);
        const float vw = e[vowel]; const int i0 = std::min (3, (int) vw); const float fr = vw - i0;
        for (int j = 0; j < 3; ++j) form[j].set (Biquad::BP, FORMANTS[i0][j] + (FORMANTS[i0 + 1][j] - FORMANTS[i0][j]) * fr, 8, 0, sr2);
        bassShelf.set (Biquad::LowShelf, 90, 0.707, e[magicBass] * 10, sr2);
        bassMud.set (Biquad::Peak, 280, 1, -e[magicBass] * 5, sr2);
        const float fm = e[formantMix], dd = e[distDrive], dm = e[distMix], fdr = e[filtDrive];
        const int dtype = (int) e[distType];
        gFormDry.next (1 - fm); gFormWet.next (fm * 4); gDistWet.next (dm / (1 + dd * 1.2f)); gDistDry.next (1 - dm);
        {
            float* chans[2] = { l, r };
            juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) m);
            auto up = os->processSamplesUp (blk);
            const int mu = (int) up.getNumSamples();
            for (int c = 0; c < 2; ++c)
            {
                float* u = up.getChannelPointer ((size_t) c);
                for (int i = 0; i < mu; ++i)
                {
                    const float f = (float) i / mu;
                    auto filt = [&] (float x) {
                        if (fdr > 0.0005f) x = tube (x, fdr);
                        x = filtA.process (x, c);
                        return gFormDry.at (f) * x + gFormWet.at (f) * (form[0].process (x, c) + 0.5f * form[1].process (x, c) + 0.25f * form[2].process (x, c));
                    };
                    auto dist = [&] (float x) { return (dd > 0.0005f ? distShape (dtype, x, dd) : x) * gDistWet.at (f) + x * gDistDry.at (f); };
                    const float y = distFirst ? filt (dist (u[i])) : dist (filt (u[i]));
                    u[i] = bassMud.process (bassShelf.process (y, c), c);
                }
            }
            os->processSamplesDown (blk);
        }
        if (glitchLast) glitch.process (l, r, m);

        // ACELERA ENDLESS: two copies an octave apart rise 0 → +24 st and wrap; a sin window mutes each one at the
        // wrap, so the ear only hears pitch going up (Shepard). Runs into the 05 bus, so HPF + reverb ride on it too.
        gEndless.next (endlessMix);
        if (endlessMix > 0.0005f || gEndless.a > 0.0005f)
        {
            float gk[2];
            for (int k = 0; k < 2; ++k)
            {
                const float pos = std::fmod ((endlessOct + k) * 0.5f, 1.0f);
                shep[k].target = 24.0 * pos;
                gk[k] = std::sin (juce::MathConstants<float>::pi * pos) * 0.75f;
            }
            for (int i = 0; i < m; ++i)
            {
                const float mx = gEndless.at ((float) i / m);
                double al = 0, ar = 0;
                for (int k = 0; k < 2; ++k)
                {
                    double vl = l[i], vr = r[i];
                    shep[k].process (vl, vr);
                    al += vl * gk[k]; ar += vr * gk[k];
                }
                l[i] = (float) (l[i] * (1 - 0.6f * mx) + al * mx); r[i] = (float) (r[i] * (1 - 0.6f * mx) + ar * mx);
            }
        }

        // 05 build-up bus: HPF → detune + noise → JET → [send tap] → ROLL gate → SPIN pan
        hpf1.set (Biquad::HP, e[hpf], 0.707, 0, sr); hpf2.set (Biquad::HP, e[hpf], 0.9, 0, sr);
        // the reverb keeps its body: never below the dry HPF (no sub in the tail), but an 8 kHz dry HPF only takes ~700 Hz off the tail
        const float fSend = std::min (e[hpf], 300 + 0.05f * e[hpf]);
        hpfSend.set (Biquad::HP, fSend, 0.707, 0, sr); hpfSend2.set (Biquad::HP, fSend, 0.9, 0, sr);
        const float nz = e[noise], de = e[detune], jt = e[jet], rl = e[roll], sp = e[spin];
        noiseBP.set (Biquad::BP, 400 * std::pow (2.0f, nz * 4.5f), 1.2, 0, sr);
        gNoise.next (std::pow (nz, 1.5f) * 0.3f); gChorus.next (de * 0.8f); gSend.next (e[wash] * 1.3f);
        gSize.next (juce::jlimit (0.0f, 1.0f, e[wash] * 1.4f)); gBloom.next (bloomNow); gJet.next (jt); gSwallow.next (swallowNow);
        const double chDepth = 0.0004 + de * 0.004, chInc[2] = { 0.63 / sr, 0.91 / sr };
        const int CB = (int) chorusBuf.size(), JB = (int) jetBuf[0].size();
        const double rollDiv = std::pow (2.0, std::floor (rl * 3.999)); // gates per beat: 1/4 → 1/32 as ROLL rises
        const float rollDepth = juce::jmin (1.0f, rl * 2.5f), rollK = 1 - std::exp (-1.0f / (0.0015f * (float) sr));
        const double spinInc = 0.25 * std::pow (2.0, sp * 5.0) * beatPerSample; // 1 turn per bar → 8 per beat
        const auto& tail = TAILS[juce::jlimit (0, NUM_TAILS - 1, (int) e[tailType])];
        shim.target = tail.semis;
        const float echoSend = tail.echo ? 1.6f : 0.0f; // only ECHO WASH echoes; every other type is pure reverb
        for (int i = 0; i < m; ++i)
        {
            const float f = (float) i / m;
            const double beat = beats0 + i * beatPerSample;
            double fbL = 0, fbR = 0; // SHIMMER / INFINITE: last block's big-reverb return fed back in (pitched), soft-clipped
            if (tail.fb > 0)
            {
                fbL = tailFb.getSample (0, i); fbR = tailFb.getSample (1, i);
                shim.process (fbL, fbR);
                fbL = std::tanh (fbL * tail.fb); fbR = std::tanh (fbR * tail.fb);
            }
            float b[2] = { hpf2.process (hpf1.process (l[i], 0), 0), hpf2.process (hpf1.process (r[i], 1), 1) };
            const float body[2] = { hpfSend2.process (hpfSend.process (l[i], 0), 0) - b[0], hpfSend2.process (hpfSend.process (r[i], 1), 1) - b[1] }; // what the dry HPF took away
            chorusBuf[(size_t) chorusW] = b[0] + b[1];
            const float jm = gJet.at (f);
            for (int c = 0; c < 2; ++c)
            {
                chPh[c] += chInc[c]; chPh[c] -= std::floor (chPh[c]);
                const double d = (0.012 + chDepth * std::sin (kTau * chPh[c])) * sr;
                double rp = chorusW - d; if (rp < 0) rp += CB;
                const int r0 = (int) rp, r1 = (r0 + 1) % CB; const float fr2 = (float) (rp - r0);
                const float ch = chorusBuf[(size_t) r0] + (chorusBuf[(size_t) r1] - chorusBuf[(size_t) r0]) * fr2;
                b[c] += ch * gChorus.at (f) + noiseBP.process (rng.nextFloat() * 2 - 1, c) * gNoise.at (f);

                if (jm > 0.001f) // JET: through-zero-ish flanger, one sweep per bar, R a quarter behind
                {
                    const double jd = (0.0006 + 0.0044 * (0.5 + 0.5 * std::sin (kTau * (beat / 4 + c * 0.25)))) * sr;
                    double jp = jetW - jd; if (jp < 0) jp += JB;
                    const int j0 = (int) jp, j1 = (j0 + 1) % JB; const float jf = (float) (jp - j0);
                    const float dl = jetBuf[c][(size_t) j0] + (jetBuf[c][(size_t) j1] - jetBuf[c][(size_t) j0]) * jf;
                    jetBuf[c][(size_t) jetW] = b[c] + 0.75f * jm * dl;
                    b[c] = (b[c] + jm * dl) / (1 + 0.5f * jm);
                }
                else jetBuf[c][(size_t) jetW] = b[c];

                const float snd = (b[c] + body[c]) * gSend.at (f), sz = gSize.at (f);
                sendS.setSample (c, i, snd * (1 - sz)); sendL.setSample (c, i, snd * sz + (float) (c ? fbR : fbL)); dlyIn.setSample (c, i, snd * echoSend);
            }
            chorusW = (chorusW + 1) % CB; jetW = (jetW + 1) % JB;

            // ROLL: tempo gate accelerating with the build (the reverb tail fills the gaps)
            const bool open = rollDepth < 0.001f || std::fmod (beat * rollDiv, 1.0) < 0.5;
            rollGain += ((open ? 1.0f : 1 - rollDepth) - rollGain) * rollK;
            // SPIN: equal-power auto-pan speeding up
            float gl = 1, gr = 1;
            if (sp > 0.001f)
            {
                spinPh += spinInc; spinPh -= std::floor (spinPh);
                const float pan = sp * (float) std::sin (kTau * spinPh), a = (pan + 1) * juce::MathConstants<float>::pi / 4;
                gl = std::cos (a) * juce::MathConstants<float>::sqrt2; gr = std::sin (a) * juce::MathConstants<float>::sqrt2;
            }
            const float keep = rollGain * (1 - gSwallow.at (f));
            l[i] = b[0] * keep * gl; r[i] = b[1] * keep * gr;
        }
        {
            juce::dsp::AudioBlock<float> bs (sendS.getArrayOfWritePointers(), 2, (size_t) m), bl (sendL.getArrayOfWritePointers(), 2, (size_t) m);
            verbS.process (juce::dsp::ProcessContextReplacing<float> (bs));
            verbL.process (juce::dsp::ProcessContextReplacing<float> (bl));
            for (int c = 0; c < 2; ++c) tailFb.copyFrom (c, 0, sendL, c, 0, m);
        }
        const float dlyFb = tail.echo ? 0.78f : 0.45f;
        const int DB = (int) dlyBuf[0].size();
        const int dlyLen = juce::jlimit (1, DB - 1, (int) (0.75 * 60 / bpm * sr));
        const float duckRec = 1 - std::exp (-1.0f / (0.35f * (float) sr));
        for (int i = 0; i < m; ++i)
        {
            if (duckHold > 0) --duckHold; else duck += (1 - duck) * duckRec;
            const int rd = (dlyW - dlyLen + DB) % DB;
            for (int c = 0; c < 2; ++c) // sendS/sendL now hold the reverb returns; dlyIn holds the dry send
            {
                const float y = dlyLP.process (dlyBuf[c][(size_t) rd], c);
                dlyBuf[c][(size_t) dlyW] = dlyIn.getSample (c, i) + dlyFb * y;
                (c ? r : l)[i] += duck * (sendS.getSample (c, i) + sendL.getSample (c, i) * gBloom.at ((float) i / m) + y);
            }
            dlyW = (dlyW + 1) % DB;
        }

        // ACELERA: tempo pump (sidechain shape: 4% dip, recover by 70%) + width opening, on the whole wet incl. reverb
        gPump.next (pumpNow); gWidth.next (widthNow);
        if (pumpNow > 0.0005f || pumpG < 0.9999f || widthNow > 0.0005f || gWidth.a > 0.0005f)
        {
            const float pk = 1 - std::exp (-1.0f / (0.0015f * (float) sr)); // 1.5 ms glide hides rate switches
            for (int i = 0; i < m; ++i)
            {
                const float f = (float) i / m;
                const float ph = (float) std::fmod ((beats0 + i * beatPerSample) * pumpRate, 1.0);
                const float shape = ph < 0.04f ? ph / 0.04f : 1 - smoothstep (0.04f, 0.7f, ph);
                pumpG += ((1 - gPump.at (f) * shape) - pumpG) * pk;
                const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * (1 + gWidth.at (f));
                l[i] = (mid + side) * pumpG; r[i] = (mid - side) * pumpG;
            }
        }

        // 06 master dynamics
        const float cp = e[comp];
        gMake.next (dbToGain (cp * 8));
        float grMax = 0;
        for (int i = 0; i < m; ++i)
        {
            const float g = glue.gain (std::max (std::abs (l[i]), std::abs (r[i])), -cp * 36, 1 + cp * 9) * gMake.at ((float) i / m);
            l[i] *= g; r[i] *= g;
            grMax = std::max (grMax, (float) glue.grDb);
        }
        if (e[limiter] > 0.5f)
        {
            lim.setThreshold (e[ceiling]);
            float* chans[2] = { l, r };
            juce::dsp::AudioBlock<float> lb (chans, 2, (size_t) m);
            lim.process (juce::dsp::ProcessContextReplacing<float> (lb));
        }

        const bool bp = e[bypass] > 0.5f; const float dw = e[dryWet];
        gWet.next (bp ? 0 : dw); gDry.next (bp ? 1 : 1 - dw); gOut.next (dbToGain (e[output]));
        int sw = scopeWrite.load();
        for (int i = 0; i < m; ++i)
        {
            const float f = (float) i / m, go = gOut.at (f);
            l[i] = (l[i] * gWet.at (f) + dryCopy.getSample (0, i) * gDry.at (f)) * go;
            r[i] = (r[i] * gWet.at (f) + dryCopy.getSample (1, i) * gDry.at (f)) * go;
            rmsAcc += (0.5f * (l[i] * l[i] + r[i] * r[i]) - rmsAcc) * 0.0005f;
            scope[sw] = 0.5f * (l[i] + r[i]); sw = (sw + 1) % scopeSize;
        }
        scopeWrite.store (sw);
        tele.gr.store (-grMax);
    }
}

void SkatomProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("snaps", getSnaps(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void SkatomProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto st = juce::ValueTree::fromXml (*xml);
        if (! st.hasType (apvts.state.getType())) return;
        setSnaps (st.getProperty ("snaps").toString());
        apvts.replaceState (st);
    }
}

juce::AudioProcessorEditor* SkatomProcessor::createEditor() { return skazi::withUpdateNotice (new SkatomEditor (*this)); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkatomProcessor(); }
