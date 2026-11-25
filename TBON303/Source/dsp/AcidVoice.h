#pragma once

// The monophonic 303 voice: oscillator -> pre drive -> diode ladder -> filter 2
// -> post drive, plus a clean sub added after the distortion. Accent and slide are
// wired into the envelopes the way the hardware does it, not as plain velocity/glide.

#include "Distortion.h"
#include "Filters.h"
#include "Oscillator.h"
#include <algorithm>
#include <cmath>

namespace s303
{
enum class Filter2Type { Off = 0, Ladder24, Formant };

struct VoiceParams
{
    float wave = 0.0f, tuneSemis = 0.0f, fineCents = 0.0f, drift = 0.3f;
    float subLevel = 0.0f;
    int subWave = 0, subOctaves = 1;

    float cutoffHz = 500.0f, resonance = 0.6f, envMod = 0.5f, decaySec = 0.6f;
    float accent = 0.6f, slideSec = 0.06f;
    float ampDecaySec = 2.5f; // VCA decay while the gate is held
    float ampSustain = 0.0f;  // level the VCA decays to (0 = the stock 303 fade-out)
    float ampReleaseSec = 0.004f;
    float filterAttackSec = 0.0f;  // 0 = the stock 303 instant filter envelope
    float filterSustain = 0.0f;
    float filterReleaseSec = 0.3f; // only the sustained part of the filter envelope releases at this rate

    // Circuit mods (in the spirit of the classic 303 hardware mods)
    float filterTrack = 0.0f;     // 0..1: cutoff follows the note pitch
    float accentDecaySec = 0.2f;  // filter decay on accented notes (stock 303: fixed ~0.2 s)
    float softAttackSec = 0.003f; // VCA attack on normal notes; accents always snap
    float sweepSpeed = 1.0f;      // accent sweep charge speed multiplier
    float pulseWidth = 0.0f;      // 0..1, square wave duty
    float oscBLevel = 0.0f;       // second oscillator, same wave, own tuning
    float oscBSemis = 0.0f, oscBFine = 0.0f;
    int unisonVoices = 1;         // 1..7 detuned copies of the main oscillator
    float unisonDetune = 0.3f;    // 0..1 = up to +-50 cents at the edges
    int filterModel = 0;          // 0 = diode (303), 1 = transistor ladder
    float noise = 0.0f;           // analog hiss into the filter

    Filter2Type f2Type = Filter2Type::Off;
    float f2CutoffHz = 2000.0f, f2Resonance = 0.3f, f2Vowel = 0.0f, f2Mix = 1.0f;

    DistType preType = DistType::Tube;
    float preDrive = 0.0f;
    DistType postType = DistType::Tube;
    float postDrive = 0.0f, postToneHz = 8000.0f, postMix = 1.0f;
};

class AcidVoice
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        osc.prepare (sr);
        oscB.prepare (sr);
        for (auto& u : uni)
            u.prepare (sr);
        ladder.prepare (sr);
        drift.prepare (sr, 0x1234567u);
        diode.prepare (sr);
        moog.prepare (sr);
        formant.prepare (sr);
        preDist.prepare (sr);
        postDist.prepare (sr);
        reset();
    }

    void reset()
    {
        osc.reset();
        oscB.reset();
        for (int k = 0; k < kMaxUnison - 1; ++k)
        {
            uni[k].reset();
            uni[k].setPhase (0.137 + 0.271 * k); // spread start phases, so unison doesn't start as one wave
        }
        ladder.reset();
        diode.reset();
        moog.reset();
        formant.reset();
        preDist.reset();
        postDist.reset();
        gate = false;
        amp = 0.0;
        attacking = false;
        filterEnv = 0.0;
        filterAttacking = false;
        filterTransient = 0.0;
        filterHeld = 0.0;
        accentCap = 0.0;
        accentGain = 1.0;
        logCutoff = std::log2 (500.0);
        toneState = 0.0;
        sliding = false;
    }

    /**
        slide = true means "this note is reached by sliding from the previous one":
        pitch glides, and neither envelope retriggers (the gate stays high).
    */
    void noteOn (int midiNote, bool accented, bool slide)
    {
        targetPitch = midiNote;
        accentOn = accented;

        if (slide && gate)
        {
            sliding = true;
            return;
        }

        sliding = false;
        pitch = midiNote;
        gate = true;
        attacking = true;
        filterAttacking = true;
        filterHeld = 0.0;
        noteCents = drift.randomCents (lastDriftAmount);
    }

    void noteOff() { gate = false; }

    // Wheels. Bend is in semitones; the mod wheel (0..1) opens the cutoff and/or adds vibrato.
    void setBend (double semitones) { bendTarget = semitones; }
    void setModWheel (double amount, int destination, double depth)
    {
        modWheel = std::clamp (amount, 0.0, 1.0);
        modDest = destination;
        modDepth = depth;
    }
    bool isActive() const { return gate || amp > 1.0e-5; }

    float render (const VoiceParams& p)
    {
        lastDriftAmount = p.drift;

        // --- pitch: constant-time RC glide, like the 303's slide capacitor
        const double slideTau = std::max (0.005, (double) p.slideSec);
        pitch += (targetPitch - pitch) * (1.0 - std::exp (-1.0 / (slideTau * sr)));
        if (sliding && std::abs (targetPitch - pitch) < 0.001)
            sliding = false;

        bendNow += (bendTarget - bendNow) * (1.0 - std::exp (-1.0 / (0.004 * sr)));
        const double wheel = modWheel * modDepth;
        double vibrato = 0.0;
        if (modDest != 0 && wheel > 0.0)
        {
            vibPhase += 5.5 / sr;
            vibPhase -= std::floor (vibPhase);
            vibrato = std::sin (vibPhase * kTwoPi) * 0.6 * wheel; // up to +-0.6 semitones
        }

        const double cents = p.fineCents + noteCents + drift.next (p.drift);
        const double note = pitch + p.tuneSemis + cents / 100.0 + bendNow + vibrato;
        const double freq = 440.0 * std::exp2 ((note - 69.0) / 12.0);

        float mainOsc = 0.0f, subOsc = 0.0f;
        osc.process (freq, p.wave, p.pulseWidth, p.subWave, p.subOctaves, mainOsc, subOsc);

        // Unison: extra detuned copies, alternating up / down, summed at the same RMS.
        const int voices = std::clamp (p.unisonVoices, 1, kMaxUnison);
        if (voices > 1)
        {
            float sum = mainOsc, unused = 0.0f;
            const double half = std::max (1.0, (voices - 1) * 0.5);
            for (int k = 1; k < voices; ++k)
            {
                const double side = (k & 1) ? 1.0 : -1.0;
                const double cent = side * p.unisonDetune * 50.0 * ((k + 1) / 2) / half;
                float o = 0.0f;
                uni[k - 1].process (freq * std::exp2 (cent / 1200.0), p.wave, p.pulseWidth, 0, 1, o, unused);
                sum += o;
            }
            mainOsc = sum / std::sqrt ((float) voices);
        }

        if (p.oscBLevel > 0.0001f)
        {
            float b = 0.0f, unused = 0.0f;
            oscB.process (freq * std::exp2 ((p.oscBSemis + p.oscBFine / 100.0) / 12.0), p.wave, p.pulseWidth, 0, 1, b, unused);
            mainOsc += b * p.oscBLevel;
        }
        if (p.noise > 0.0001f)
            mainOsc += hiss.next() * p.noise * 0.12f;

        // --- filter envelope (MEG). Accents use their own (normally very short) decay.
        // The envelope is a decaying part plus a sustained part: while the gate is held it
        // decays towards SUSTAIN; after the gate the sustained part fades at the RELEASE rate
        // while the decaying part keeps its own rate. SUSTAIN 0 is the stock 303 envelope.
        const double decay = accentOn ? std::max (0.03, (double) p.accentDecaySec)
                                      : std::max (0.05, (double) p.decaySec);
        if (filterAttacking && p.filterAttackSec < 0.0005f)
        {
            filterTransient = 1.0; // instant attack: decays from this very sample, like the stock 303
            filterAttacking = false;
        }
        if (filterAttacking)
        {
            filterTransient += 1.0 / (p.filterAttackSec * sr);
            if (filterTransient >= 1.0)
            {
                filterTransient = 1.0;
                filterAttacking = false;
            }
            filterEnv = filterTransient;
        }
        else
        {
            filterTransient *= std::exp (-4.0 / (decay * sr));
            const double sus = std::clamp ((double) p.filterSustain, 0.0, 1.0);
            if (gate)
                filterHeld = sus * (1.0 - filterTransient);
            else
                filterHeld *= std::exp (-4.0 / (std::max (0.005, (double) p.filterReleaseSec) * sr));
            filterEnv = filterTransient + filterHeld;
        }

        // Accent sweep: accented envelope charges a cap through a resonance-dependent
        // RC. Back-to-back accents don't let it discharge, so the cutoff climbs.
        const double accIn = accentOn ? filterEnv * p.accent : 0.0;
        const double accTau = (0.012 + 0.11 * p.resonance) / std::max (0.1, (double) p.sweepSpeed);
        accentCap += (accIn - accentCap) * (1.0 - std::exp (-1.0 / (accTau * sr)));

        const double targetLog = std::log2 (std::max (20.0f, p.cutoffHz))
                               + p.envMod * 6.0 * filterEnv   // up to 6 octaves of sweep
                               + accentCap * 4.5
                               + p.filterTrack * (pitch - 48.0) / 12.0
                               + (modDest != 1 ? wheel * 4.0 : 0.0); // mod wheel: up to 4 octaves brighter

        // Slides also smooth the cutoff between the two notes; otherwise only de-zip.
        const double cutTau = sliding ? slideTau * 0.6 : 0.0015;
        logCutoff += (targetLog - logCutoff) * (1.0 - std::exp (-1.0 / (cutTau * sr)));
        const double cutoff = std::exp2 (logCutoff);

        // --- audio path
        float x = preDist.process (mainOsc, p.preType, p.preDrive);
        x = p.filterModel == 1 ? ladder.process (x, cutoff, p.resonance * 0.95) // unity gain lands within ~3 dB of the diode
                               : diode.process (x, cutoff, p.resonance);

        if (p.f2Type == Filter2Type::Ladder24)
        {
            const float f2 = moog.process (x, p.f2CutoffHz, p.f2Resonance);
            x += (f2 - x) * p.f2Mix;
        }
        else if (p.f2Type == Filter2Type::Formant)
        {
            const double shift = std::clamp (p.f2CutoffHz / 1000.0, 0.4, 2.5);
            const float f2 = formant.process (x, p.f2Vowel, shift, p.f2Resonance);
            x += (f2 - x) * p.f2Mix;
        }

        if (p.postDrive > 0.0001f)
        {
            const float driven = postDist.process (x, p.postType, p.postDrive);
            const double tc = 1.0 - std::exp (-kTwoPi * std::min ((double) p.postToneHz, sr * 0.45) / sr);
            toneState += (driven - toneState) * tc;
            x += ((float) toneState - x) * p.postMix;
        }

        // --- VCA. Fast attack, slow decay while the gate is held, quick release.
        if (gate)
        {
            if (attacking)
            {
                const double attack = accentOn ? 0.001 : std::max (0.0005, (double) p.softAttackSec);
                amp += 1.0 / (attack * sr);
                if (amp >= 1.0)
                {
                    amp = 1.0;
                    attacking = false;
                }
            }
            else
            {
                const double sus = std::clamp ((double) p.ampSustain, 0.0, 1.0);
                amp = sus + (amp - sus) * std::exp (-1.0 / (std::max (0.05, (double) p.ampDecaySec) * sr));
            }
        }
        else
        {
            attacking = false;
            amp *= std::exp (-1.0 / (std::max (0.001, (double) p.ampReleaseSec) * sr));
        }

        const double targetAccentGain = accentOn ? 1.0 + 0.9 * p.accent : 1.0;
        accentGain += (targetAccentGain - accentGain) * (1.0 - std::exp (-1.0 / (0.003 * sr)));

        // The sub joins after the drive so the low end stays clean and mono-solid.
        const double out = (x * accentGain + subOsc * p.subLevel * 0.8) * amp;
        return (float) (out * 0.5);
    }

    double getFilterEnv() const { return filterEnv; }

private:
    double sr = 44100.0;
    static constexpr int kMaxUnison = 7;
    AcidOscillator osc, oscB;
    AcidOscillator uni[kMaxUnison - 1];
    MoogLadder ladder;
    Drift drift;
    Noise hiss;
    DiodeLadder diode;
    MoogLadder moog;
    FormantFilter formant;
    Distortion preDist, postDist;

    bool gate = false, attacking = false, accentOn = false, sliding = false, filterAttacking = false;
    double pitch = 48.0, targetPitch = 48.0, noteCents = 0.0;
    double amp = 0.0, filterEnv = 0.0, filterTransient = 0.0, filterHeld = 0.0, accentCap = 0.0, accentGain = 1.0;
    double logCutoff = 9.0, toneState = 0.0;
    double bendTarget = 0.0, bendNow = 0.0, modWheel = 0.0, modDepth = 1.0, vibPhase = 0.0;
    int modDest = 0; // 0 = cutoff, 1 = vibrato, 2 = both
    float lastDriftAmount = 0.3f;
};
} // namespace s303
