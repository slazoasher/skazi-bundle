#pragma once

// Volume trims (dB) that bring every preset to about the same loudness. Regenerate with: SkacidTest --trim Source/PresetTrims.h
// (run it 3 times: each pass starts from the previous trims)
namespace skacid { inline constexpr float presetTrims[] = {
    0.33f,    -0.99f,    -3.04f,    -2.08f,    -1.33f,    -0.56f,    -8.93f,    -7.12f,
    -5.21f,    -4.01f,    0.66f,    -1.88f,    2.69f,    -3.10f,    -5.85f,    -8.34f,
    -1.03f,    3.64f,    -3.67f,    -0.35f,    -7.12f,    -6.62f,    -0.70f,    -0.53f,
    -1.03f,    -8.63f,    -6.91f,    -3.44f,    -8.19f,    -5.75f,    -6.80f,    -10.51f,
    0.51f,    -1.73f,    -1.70f,    -6.67f,    0.11f,    -0.18f,    -0.81f,    -2.73f,
    3.03f,    -7.95f,    -7.54f,    1.74f,    -7.33f,    4.36f,    1.76f,    -3.76f,
    6.00f,    6.65f,    7.37f,    7.75f,    8.43f,    10.41f,    2.24f,    -2.71f,
    7.64f,    -6.95f,    -7.67f,    -2.30f,    -6.15f,    -0.34f,    -3.90f,    -3.42f,
    0.93f,    1.85f,    -2.10f,    4.34f,    1.53f,    6.00f,    -3.77f,    -0.40f,
    -3.39f,    1.64f,    -4.03f,    1.63f,    -2.43f,    -0.01f,    -5.15f,    -3.36f,
    1.17f,    -3.60f,    -9.17f,    -0.75f,    -3.25f,    -3.47f,    -5.65f,    -5.35f,
    -3.18f,    3.03f,    -0.59f,    -4.43f,    -1.05f,    3.19f,    1.85f,    -1.02f,
    5.65f,    -3.38f,    -4.75f,    -4.90f,    -6.27f,    0.78f,    -1.29f,    2.49f,
    5.28f,    8.31f,    7.59f,    7.37f,    7.07f,    6.00f,    -5.74f,    -0.62f,
    4.72f,    0.73f,    -7.77f,    0.24f,    0.70f,    2.46f,    0.08f,    -0.24f,
    -4.88f,    -6.84f,    -7.39f,    -4.21f,    -7.12f,    0.05f,    -2.48f,    -1.63f,
    5.44f,
}; }
