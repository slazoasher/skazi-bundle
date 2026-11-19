#pragma once

#include "Arp.h"
#include <vector>

namespace skyz
{
    struct Preset
    {
        juce::String name;                         // "CATEGORY // Name"
        std::vector<std::pair<int, float>> values; // normalised; everything else stays at its default
        Lanes lanes;
        float trimDb = 0;
    };

    const juce::StringArray& presetCategories();
    const std::vector<Preset>& factoryPresets();               // 555, built once
    Preset generatePreset (int category, uint32_t seed);      // RANDOM button: a fresh one in a category
    Lanes lanesFromPattern (int pattern);                     // same table as PATTERNS in web/index.html
}
