#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace skazon
{
    // Values are in parameter units (choice = index); anything not listed loads at its default.
    // Later entries override earlier ones with the same id.
    struct Preset
    {
        juce::String name; // "CATEGORY // Name"
        std::vector<std::pair<juce::String, float>> values;
    };

    // The 24 hand-made presets, then the generated ones (seeded, so always identical; new categories only at the end).
    const std::vector<Preset>& allPresets();

    // The generated categories, and a fresh preset from one of them (the MAGIC dice).
    const juce::StringArray& generatedCategories();
    Preset randomPreset (int category, juce::int64 seed);
}
