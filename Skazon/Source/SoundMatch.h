#pragma once

#include "Engine.h"
#include "NoteTrack.h"

namespace skazon
{
    // SOUND MATCH: turns a recording (one note or a whole mono line) into a SKAZON patch plus its MIDI.
    // The line is tracked into notes first; the longest, loudest note is the one the timbre comes from.
    // Pitch (YIN), 16 pitch-synchronous single cycles across the note -> USER wavetable scanned by the
    // mod envelope, amp ADSR from the level curve, vibrato -> LFO 1 on pitch, stereo width -> unison,
    // aperiodic energy -> noise.
    struct MatchResult
    {
        bool ok = false;
        juce::String error, summary;
        std::shared_ptr<const UserTable> table;
        std::vector<std::pair<juce::String, float>> values; // patch, in parameter units
        float f0 = 0;                                        // detected pitch, Hz
        int midiNote = 60;
        float attack = 0, vibratoHz = 0, vibratoCents = 0, width = 0;
        std::vector<TrackedNote> notes; // the whole recording as MIDI (seconds from the file start)
        double heroStart = 0, heroEnd = 0; // the note the timbre was taken from, seconds
    };

    MatchResult matchSound (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name);
}
