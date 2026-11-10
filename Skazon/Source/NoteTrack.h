#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace skazon
{
    struct TrackedNote
    {
        int pitch = 60;
        double start = 0.0, length = 0.25; // seconds
        int velocity = 100;
    };

    // Audio wave -> MIDI notes (monophonic: a lead line, a bass line or one note).
    std::vector<TrackedNote> trackNotes (const std::vector<float>& mono, double sampleRate);
}
