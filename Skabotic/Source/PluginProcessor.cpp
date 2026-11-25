#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../Common/UpdateNotice.h"

namespace
{
    struct Preset
    {
        const char* name;
        float vocode, carrier, source, note, octave, formant, react, hiss,
              pitch, ring, ringFreq, bits, crush, metal, metalFreq, drive, gate, mix;
        // Added in 1.1; the first 30 presets keep these defaults.
        float glide = 20, key = 0, scale = 0, shift = 0, stutter = 0, stutSize = 1, chop = 0, chopRate = 1, army = 0, width = 100;
    };

    // carrier: 0 SAW 1 SQUARE 2 8-BIT 3 CHORD 4 BUZZ 5 NOISE   source: 0 NOTE 1 FOLLOW 2 AUTO-TUNE 3 MIDI POLY 4 MIDI MONO
    // key: 0 C .. 11 B   scale: 0 CHROMATIC 1 MAJOR 2 MINOR 3 HARM MINOR 4 PHRYGIAN 5 PENTA MINOR   sizes/rates: 0 1/32 1/16 1/8 1/4
    const Preset presets[] {
        //  name                 voc car src note oct form react hiss  pitch ring rngF  bits crush metal metF drive gate  mix
        { "ROBOT CLASSIC",       100, 0, 0, 45,  0,  0,   20,  30,    0,    0,   60, 16,  1,    15, 220,  10, -60, 100 },
        { "TALK BOX",            100, 0, 1, 45,  0,  0,   12,  20,    0,    0,   60, 16,  1,     0, 300,  15, -60, 100 },
        { "TALK BOX MIDI",       100, 0, 3, 45,  0,  0,   12,  20,    0,    0,   60, 16,  1,     0, 300,  15, -60, 100 },
        { "VOCODER CHOIR",       100, 3, 0, 48,  0,  0,   40,  25,    0,    0,   60, 16,  1,     0, 300,   0, -60, 100 },
        { "AUTO-TUNE ROBOT",     100, 0, 2, 45,  0,  0,   10,  25,    0,    0,   60, 16,  1,     0, 300,  10, -60, 100 },
        { "CLEAN VOCODER",       100, 0, 1, 45,  0,  0,   15,  35,    0,    0,   60, 16,  1,     0, 300,   0, -70, 100 },
        { "8-BIT HERO",          100, 2, 2, 45,  0,  0,   10,  10,    0,    0,   60,  6,  4,     0, 300,   0, -60, 100 },
        { "GAME CONSOLE",         70, 2, 1, 45,  0,  0,   10,  10,    0,    0,   60,  4,  8,     0, 300,   0, -60, 100 },
        { "8-BIT DRIP",          100, 2, 3, 57,  0,  0,    8,  10,    0,    0,   60,  4,  3,     0, 300,   0, -60, 100 },
        { "GLITCH SERVO",        100, 2, 0, 57,  0,  0,    6,  15,    0,   50,    7,  3, 12,     0, 300,  20, -60, 100 },
        { "MEGA DESTROYER",      100, 0, 1, 45, -1, -7,   25,  30,  -12,   35,   45, 16,  1,    30,  90,  45, -55, 100 },
        { "GIANT MECHA",         100, 0, 0, 33, -1,-10,   30,  25,  -12,    0,   60, 16,  1,    40,  60,  35, -55, 100 },
        { "ROBO BASS VOICE",     100, 0, 0, 28, -1, -6,   25,  20,   -7,    0,   60, 16,  1,     0, 300,  40, -60, 100 },
        { "STEEL ASSASSIN",       60, 1, 1, 45,  0, -4,   20,  25,   -5,   10,   80, 16,  1,    45, 180,  30, -60, 100 },
        { "SPACE TYRANT",          0, 0, 0, 45,  0,  0,   20,  30,    0,  100,   30, 16,  1,    25, 150,  20, -60, 100 },
        { "TIN CAN",               0, 0, 0, 45,  0,  0,   20,  30,    0,    0,   60, 16,  1,    70, 700,  10, -60, 100 },
        { "METAL MOUTH",           0, 0, 0, 45,  0,  0,   20,  30,    0,    0,   60, 16,  1,    85, 220,  25, -60, 100 },
        { "CYBORG",               60, 1, 1, 45,  0,  0,   20,  25,   -3,   20,   80, 16,  1,    35, 250,  20, -60, 100 },
        { "ANDROID WHISPER",     100, 5, 0, 45,  0,  0,   15, 100,    0,    0,   60, 16,  1,    20, 400,   0, -55, 100 },
        { "WI-FI GHOST",          80, 4, 2, 45,  1,  5,   20,  30,    0,   15, 1200, 10,  3,    30,1500,   0, -60, 100 },
        { "SPACESHIP AI",         70, 3, 0, 52,  0,  3,   60,  20,    0,    0,   60, 16,  1,    25, 520,   0, -60,  85 },
        { "DEEP SPACE ORACLE",   100, 3, 0, 38, -1, -3,  120,  40,    0,    0,   60, 16,  1,    20, 110,   0, -60, 100 },
        { "ALIEN TRANSMISSION",   70, 4, 1, 45,  0,  0,   20,  30,    5,   60,  470,  7,  5,    20, 900,   0, -60, 100 },
        { "SATELLITE",            40, 4, 0, 45,  0,  0,   20,  30,    0,   30,  900,  9,  3,     0, 300,   0, -50, 100 },
        { "RADIO DROID",          50, 1, 2, 45,  1,  0,   15,  25,    0,    0,   60,  6,  2,    50,1100,  40, -60, 100 },
        { "BROKEN ROBOT",        100, 4, 0, 40,  0,  0,   20,  30,    0,   40,   13,  5,  6,     0, 300,  50, -60, 100 },
        { "CHIPMUNK BOT",        100, 1, 2, 45,  1,  9,   10,  25,   12,    0,   60,  8,  2,     0, 300,   0, -60, 100 },
        { "HELMET DISCO",        100, 0, 2, 45,  0,  0,    8,  15,    0,    0,   60, 16,  1,    10, 300,  15, -60, 100 },
        { "PSY BOT",             100, 0, 0, 40,  0,  0,    6,  20,    0,   20,  150, 16,  1,    30, 330,  30, -60, 100 },
        { "HYPER ROBOT",         100, 4, 2, 45,  0,  0,   10,  25,    0,   25,  220,  8,  2,    35, 440,  40, -60, 100 },

        // 1.1: MIDI, key-locked and sci-fi presets
        //  name                 voc car src note oct form react hiss  pitch ring rngF  bits crush metal metF drive gate  mix  glide key scl shift stut sz chop rt army
        { "ROBOT ARMY",          100, 0, 0, 45,  0,  0,   15,  25,    0,    0,   60, 16,  1,    15, 220,  15, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  80 },
        { "ROBOT ARMY MIDI",     100, 0, 3, 45,  0,  0,   15,  25,    0,    0,   60, 16,  1,    10, 220,  15, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  70 },
        { "MIDI MONO SYNTH VOX", 100, 0, 4, 45,  0,  0,   10,  20,    0,    0,   60, 16,  1,     0, 300,  20, -60, 100,   60,  0, 0,    0,   0, 1,   0, 1,   0 },
        { "MIDI LEAD BOT",       100, 1, 4, 57,  0,  2,    8,  20,    0,    0,   60, 16,  1,    10, 300,  25, -60, 100,   30,  0, 0,    0,   0, 1,   0, 1,  20 },
        { "MIDI CHOIR MACHINE",  100, 3, 3, 48,  0,  0,   50,  25,    0,    0,   60, 16,  1,     0, 300,   0, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  50 },
        { "MIDI 8-BIT MONO",     100, 2, 4, 57,  0,  0,    8,  10,    0,    0,   60,  5,  3,     0, 300,   0, -60, 100,    0,  0, 0,    0,   0, 1,   0, 1,   0 },
        { "MIDI MACHINE GOSPEL", 100, 3, 3, 48,  0,  0,   60,  25,    0,    0,   60, 16,  1,    10, 300,   0, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  70 },
        { "MIDI HELMET FUNK",    100, 0, 4, 45,  0,  0,    6,  20,    0,    0,   60, 16,  1,     0, 300,  20, -60, 100,   15,  0, 0,    0,   0, 1,   0, 1,  25 },
        { "E PHRYGIAN PSY BOT",  100, 0, 2, 40,  0,  0,    6,  20,    0,   20,  150, 16,  1,    25, 330,  30, -60, 100,    3,  4, 4,    0,   0, 1,  40, 1,   0 },
        { "A MINOR TUNE BOT",    100, 0, 2, 45,  0,  0,   10,  25,    0,    0,   60, 16,  1,     0, 300,  10, -60, 100,    5,  9, 2,    0,   0, 1,   0, 1,   0 },
        { "C MAJOR POP BOT",     100, 1, 2, 48,  0,  0,   10,  25,    0,    0,   60, 16,  1,     0, 300,   5, -60, 100,    5,  0, 1,    0,   0, 1,   0, 1,  20 },
        { "F# MINOR TRANCE BOT", 100, 0, 2, 42,  0,  0,   12,  25,    0,    0,   60, 16,  1,    10, 370,  10, -60, 100,    4,  6, 2,    0,   0, 1,  30, 1,  40 },
        { "D MINOR NEON SAMURAI",100, 0, 2, 50,  0,  0,   10,  25,    0,    0,   60, 16,  1,    20, 600,  20, -60, 100,    4,  2, 2,    0,   0, 1,   0, 1,  50 },
        { "CHIP SINGER",         100, 2, 2, 57,  0,  0,    8,  10,    0,    0,   60,  6,  2,     0, 300,   0, -60, 100,    0,  9, 5,    0,   0, 1,   0, 1,   0 },
        { "STUTTER BOT",         100, 0, 0, 45,  0,  0,   10,  25,    0,    0,   60, 16,  1,     0, 300,  15, -60, 100,   20,  0, 0,    0,  50, 1,   0, 1,   0 },
        { "RO-RO-ROBOT",         100, 1, 0, 45,  0,  0,    8,  25,    0,    0,   60, 16,  1,    10, 300,  10, -60, 100,   20,  0, 0,    0,  75, 0,   0, 1,   0 },
        { "GLITCH STORM",        100, 4, 0, 45,  0,  0,    8,  25,    0,    0,   60,  5,  6,     0, 300,  20, -60, 100,   20,  0, 0,  120,  60, 0,  40, 0,   0 },
        { "BROKEN TRANSMISSION",  80, 4, 0, 45,  0,  0,   15,  30,    0,   40,    9,  4, 10,     0, 300,   0, -60, 100,   20,  0, 0,    0,  40, 2,  30, 1,   0 },
        { "ROBOT RAPPER",        100, 1, 1, 45,  0,  0,    4,  40,    0,    0,   60, 16,  1,     0, 300,  30, -60, 100,   15,  0, 0,    0,  25, 1,   0, 1,   0 },
        { "COMPUTER SAYS",       100, 1, 0, 52,  0,  0,    4,  15,    0,    0,   60,  8,  1,     0, 300,   0, -60, 100,   20,  0, 0,    0,   0, 1,  50, 0,   0 },
        { "MORSE BEACON",         70, 4, 0, 69,  0,  0,    8,  20,    0,   20, 1000, 16,  1,     0, 300,   0, -60, 100,   20,  0, 0,    0,   0, 1,  90, 0,   0 },
        { "DUBSTEP ROBOT",       100, 0, 0, 33, -1,  0,   10,  25,    0,   25,   50, 16,  1,     0, 300,  60, -60, 100,   20,  0, 0,    0,   0, 1,  70, 2,  30 },
        { "WOBBLE MACHINE",      100, 0, 0, 40,  0,  0,   12,  25,    0,    0,   60, 16,  1,    40, 100,  30, -60, 100,   20,  0, 0,    0,   0, 1,  60, 3,   0 },
        { "FREQ SHIFT ALIEN",     30, 4, 0, 45,  0,  0,   20,  30,    0,    0,   60, 16,  1,    20, 700,   0, -60, 100,   20,  0, 0,  260,   0, 1,   0, 1,   0 },
        { "UFO LANDING",          60, 4, 0, 45,  0,  0,   20,  30,    0,   30,  600, 16,  1,    30,1300,   0, -60, 100,   20,  0, 0, -180,   0, 1,   0, 1,  50 },
        { "BARBERPOLE DROID",    100, 0, 0, 45,  0,  0,   20,  30,    0,    0,   60, 16,  1,    30, 400,  10, -60, 100,   20,  0, 0,    8,   0, 1,   0, 1,   0 },
        { "LASER BRAIN",         100, 1, 2, 45,  1,  0,   10,  25,    0,    0,   60,  7,  2,     0, 300,   0, -60, 100,    4,  0, 0,  500,   0, 1,  30, 1,   0 },
        { "TELEPORTER",           40, 4, 0, 45,  0,  0,   20,  30,    0,    0,   60, 16,  1,     0, 300,   0, -60, 100,   20,  0, 0,  700,  30, 0,   0, 1,  70 },
        { "QUANTUM GHOST",       100, 5, 0, 45,  0,  0,   15, 100,    0,    0,   60, 16,  1,     0, 300,   0, -55, 100,   20,  0, 0,  150,   0, 1,  40, 0,  80 },
        { "HAUNTED ANDROID",     100, 5, 0, 45,  0,  0,   20, 100,    0,    0,   60, 16,  1,    35, 200,   0, -55, 100,   20,  0, 0,  -40,   0, 1,   0, 1,  60 },
        { "TRANSFORMATION",      100, 0, 1, 45,  0, -8,   20,  25,  -12,    0,   60, 16,  1,    45, 120,  40, -55, 100,   20,  0, 0,  -60,   0, 1,   0, 1,  30 },
        { "WAR MACHINE",         100, 1, 1, 45, -1, -9,   20,  25,  -12,   30,   70, 16,  1,    50,  80,  55, -55, 100,   20,  0, 0,    0,   0, 1,   0, 1,  40 },
        { "BIG BOSS ROBOT",      100, 0, 0, 45, -2,-12,   25,  25,  -12,    0,   60, 16,  1,    30,  50,  60, -55, 100,   20,  0, 0,    0,   0, 1,   0, 1,  20 },
        { "SATAN.EXE",           100, 0, 0, 40, -2,-12,   25,  25,  -24,    0,   60, 16,  1,    40,  45,  70, -55, 100,   20,  0, 0,  -30,   0, 1,   0, 1,   0 },
        { "TINY ROBOT",          100, 1, 2, 45,  2, 12,   10,  25,   12,    0,   60, 10,  1,     0, 300,   0, -60, 100,    4,  0, 0,    0,   0, 1,   0, 1,   0 },
        { "ARCADE BOSS",         100, 2, 2, 45, -1,  0,    8,  10,    0,    0,   60,  3,  5,     0, 300,   0, -60, 100,    0,  0, 0,    0,   0, 1,  25, 1,   0 },
        { "CYBER CHOIR",         100, 3, 0, 50,  0,  0,   80,  30,    0,    0,   60, 16,  1,    15, 260,   0, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  90 },
        { "DIGITAL ANGEL",       100, 3, 2, 50,  1,  4,   90,  35,    0,    0,   60, 16,  1,    15,1800,   0, -60, 100,    6,  0, 0,    0,   0, 1,   0, 1,  85 },
        { "SPACE STATION PA",     50, 0, 0, 45,  0,  0,   20,  30,    0,   15, 2000,  8,  2,    40, 900,   0, -50, 100,   20,  0, 0,    0,   0, 1,   0, 1,  20 },
        { "INSANE ROBOT",        100, 4, 1, 45,  0,  0,   10,  30,    0,   50,  330,  6,  4,    60, 700,  60, -60, 100,   20,  0, 0,   90,  40, 0,   0, 1,  40 },

        // 1.2: wide-stereo presets (last column = WIDTH %)
        //  name                 voc car src note oct form react hiss  pitch ring rngF  bits crush metal metF drive gate  mix  glide key scl shift stut sz chop rt army width
        { "ULTRA WIDE ROBOT",    100, 0, 0, 45,  0,  0,   15,  25,    0,    0,   60, 16,  1,    15, 220,  15, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  40, 200 },
        { "WIDE SCREEN BOT",     100, 1, 1, 45,  0,  0,   12,  25,    0,    0,   60, 16,  1,    10, 300,  10, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  30, 170 },
        { "HOLOGRAM",             80, 4, 2, 50,  1,  4,   30,  30,    0,    0,   60, 16,  1,    20,1200,   0, -60, 100,    6,  0, 0,   12,   0, 1,   0, 1,  60, 180 },
        { "3D ANDROID",          100, 0, 1, 45,  0, -2,   15,  30,    0,   10,  120, 16,  1,    25, 350,  15, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  50, 190 },
        { "ZERO GRAVITY",        100, 3, 0, 50,  0,  2,   70,  30,    0,    0,   60, 16,  1,    10, 500,   0, -60, 100,   20,  0, 0,  -15,   0, 1,   0, 1, 100, 200 },
        { "GALAXY CHOIR",        100, 3, 3, 48,  0,  0,   90,  30,    0,    0,   60, 16,  1,    10, 260,   0, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  90, 185 },
        { "STEREO GHOST SHIP",   100, 5, 0, 45,  0,  0,   25, 100,    0,    0,   60, 16,  1,    30, 180,   0, -55, 100,   20,  0, 0,  -25,   0, 1,   0, 1,  70, 200 },
        { "SURROUND MECHA",      100, 0, 1, 45, -1, -8,   20,  25,  -12,   20,   60, 16,  1,    40,  90,  45, -55, 100,   20,  0, 0,    0,   0, 1,   0, 1,  40, 175 },
        { "TALK BOX WIDE",       100, 0, 1, 45,  0,  0,   12,  20,    0,    0,   60, 16,  1,     0, 300,  15, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  25, 165 },
        { "MIDI TALK BOX WIDE",  100, 0, 3, 45,  0,  0,   12,  20,    0,    0,   60, 16,  1,     0, 300,  15, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  30, 170 },
        { "DISCO DROID MIDI",    100, 0, 4, 45,  0,  0,    6,  20,    0,    0,   60, 16,  1,    10, 300,  20, -60, 100,   12,  0, 0,    0,   0, 1,   0, 1,  35, 160 },
        { "STEEL CHOIR MIDI",    100, 3, 3, 48,  0, -2,   40,  25,    0,    0,   60, 16,  1,    35, 200,  10, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  60, 180 },
        { "ANALOG VOCODER 70S",  100, 0, 0, 45,  0,  0,   30,  20,    0,    0,   60, 16,  1,     0, 300,   5, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  20, 140 },
        { "ROBOT MONK",          100, 3, 0, 38, -1, -5,  120,  20,    0,    0,   60, 16,  1,    20,  70,   0, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1,  60, 160 },
        { "MACHINE PRIEST",      100, 0, 0, 40, -1, -6,   60,  25,  -12,    0,   60, 16,  1,    45, 110,  10, -55, 100,   20,  0, 0,    0,   0, 1,   0, 1,  70, 170 },
        { "DROID DIVA",          100, 1, 2, 52,  1,  3,   10,  25,    0,    0,   60, 16,  1,     0, 300,   5, -60, 100,    5,  0, 1,    0,   0, 1,   0, 1,  60, 175 },
        { "TRAP ROBOT",          100, 0, 2, 45,  0,  0,    6,  25,    0,    0,   60, 16,  1,     0, 300,  15, -60, 100,   40,  9, 2,    0,   0, 1,   0, 1,  20, 150 },
        { "K-POP CYBORG",        100, 1, 2, 52,  1,  5,    8,  25,    3,    0,   60, 12,  1,     0, 300,  10, -60, 100,    5,  1, 1,    0,  20, 1,   0, 1,  40, 170 },
        { "PSY GOBLIN BOT",      100, 4, 2, 40,  0,  2,    5,  20,    0,   30,  220, 16,  1,    30, 330,  35, -60, 100,    3,  4, 4,   30,   0, 1,  50, 0,  30, 180 },
        { "HARD TECHNO BOT",     100, 1, 0, 40,  0,  0,    5,  20,    0,   20,  100, 16,  1,    35, 160,  55, -60, 100,   20,  0, 0,    0,   0, 1,  60, 1,   0, 140 },
        { "NANO SWARM",           90, 4, 0, 57,  1,  4,    8,  30,    0,    0,   60,  9,  2,     0, 300,   0, -60, 100,   20,  0, 0,  200,  40, 0,   0, 1, 100, 200 },
        { "NEURAL NET",          100, 2, 2, 57,  0,  0,    8,  10,    0,    0,   60,  6,  2,     0, 300,   0, -60, 100,    0,  0, 0,    0,  50, 0,   0, 1,  40, 180 },
        { "SIGNAL LOST",          70, 4, 0, 45,  0,  0,   15,  30,    0,   30,   11,  5,  8,     0, 300,  10, -60, 100,   20,  0, 0,  -70,  45, 1,  35, 0,  30, 170 },
        { "VHS ANDROID",         100, 1, 0, 45,  0,  0,   20,  30,    0,    0,   60, 10,  2,    10, 400,  10, -60, 100,   20,  0, 0,    3,   0, 1,   0, 1,  40, 150 },
        { "MARS RADIO",           60, 4, 0, 45,  0,  0,   20,  30,    0,   20, 1400,  8,  3,    50,1600,  30, -50, 100,   20,  0, 0,  -90,   0, 1,   0, 1,  20, 130 },
        { "BLACK HOLE",          100, 0, 0, 33, -2,-12,   40,  25,  -24,    0,   60, 16,  1,    40,  40,  50, -55, 100,   20,  0, 0,  -20,   0, 1,   0, 1,  80, 200 },
        { "CYBER ELF",           100, 1, 2, 57,  1, 10,    8,  30,   12,    0,   60, 16,  1,     0, 300,   0, -60, 100,    4,  0, 1,    0,   0, 1,   0, 1,  60, 180 },
        { "OCTAVE ARMY",         100, 3, 1, 45, -1,  0,   15,  25,    0,    0,   60, 16,  1,     0, 300,  10, -60, 100,   20,  0, 0,    0,   0, 1,   0, 1, 100, 190 },
        { "ROBOT ORACLE",        100, 3, 0, 45,  0, -3,  100,  40,    0,    0,   60, 16,  1,    25, 150,   0, -60, 100,   20,  0, 0,  -10,   0, 1,   0, 1,  80, 195 },
        { "ROBOT WHISPER WIDE",  100, 5, 0, 45,  0,  2,   15, 100,    0,    0,   60, 16,  1,    15, 700,   0, -55, 100,   20,  0, 0,    0,   0, 1,   0, 1,  50, 200 },
    };
    constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));

    float mtof (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

    juce::String noteName (float v) { return juce::MidiMessage::getMidiNoteName ((int) std::lround (v), true, true, 3); }
}

SkaboticAudioProcessor::SkaboticAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SKABOTIC", createParameterLayout())
{
    setCurrentProgram (0);
}

juce::AudioProcessorValueTreeState::ParameterLayout SkaboticAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto num = [&p] (const char* id, const char* name, NormalisableRange<float> range, float def, const char* unit,
                     std::function<String (float, int)> toText = {})
    {
        auto attrs = AudioParameterFloatAttributes().withLabel (unit);
        if (! toText)
            toText = [u = String (unit)] (float v, int) { return String (v, std::abs (v) >= 100.0f ? 0 : 1) + " " + u; };
        attrs = attrs.withStringFromValueFunction (std::move (toText));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, attrs));
    };
    auto logRange = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };
    auto pct = [&num] (const char* id, const char* name, float def) { num (id, name, { 0.0f, 100.0f, 0.1f }, def, "%"); };

    pct (ParamIDs::vocode, "Vocode", 100.0f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::carrier, 1 }, "Carrier",
                                                         StringArray { "SAW", "SQUARE", "8-BIT", "CHORD", "BUZZ", "NOISE" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::source, 1 }, "Source",
                                                         StringArray { "NOTE", "FOLLOW", "AUTO-TUNE", "MIDI POLY", "MIDI MONO" }, 0));
    num (ParamIDs::note, "Note", { 24.0f, 84.0f, 1.0f }, 45.0f, "", [] (float v, int) { return noteName (v); });
    num (ParamIDs::octave, "Octave", { -2.0f, 2.0f, 1.0f }, 0.0f, "oct",
         [] (float v, int) { return (v > 0 ? "+" : "") + String ((int) std::lround (v)); });
    num (ParamIDs::formant, "Formant", { -12.0f, 12.0f, 0.1f }, 0.0f, "st");
    num (ParamIDs::react, "React", logRange (1.0f, 200.0f, 30.0f), 20.0f, "ms");
    pct (ParamIDs::hiss, "Hiss", 30.0f);
    num (ParamIDs::pitch, "Pitch", { -24.0f, 24.0f, 0.1f }, 0.0f, "st");
    pct (ParamIDs::ring, "Ring", 0.0f);
    num (ParamIDs::ringFreq, "Ring Freq", logRange (1.0f, 2000.0f, 100.0f), 60.0f, "Hz");
    num (ParamIDs::bits, "Bits", { 2.0f, 16.0f, 0.1f }, 16.0f, "bit",
         [] (float v, int) { return v >= 15.95f ? String ("OFF") : String (v, 1); });
    num (ParamIDs::crush, "Crush", logRange (1.0f, 32.0f, 6.0f), 1.0f, "x",
         [] (float v, int) { return v <= 1.01f ? String ("OFF") : String (v, 1) + "x"; });
    pct (ParamIDs::metal, "Metal", 0.0f);
    num (ParamIDs::metalFreq, "Metal Freq", logRange (40.0f, 2000.0f, 300.0f), 300.0f, "Hz");
    pct (ParamIDs::drive, "Drive", 0.0f);
    num (ParamIDs::gate, "Gate", { -90.0f, -20.0f, 0.1f }, -60.0f, "dB",
         [] (float v, int) { return v <= -89.9f ? String ("OFF") : String (v, 1); });
    pct (ParamIDs::mix, "Mix", 100.0f);
    num (ParamIDs::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, "dB");

    num (ParamIDs::glide, "Glide", logRange (0.0f, 500.0f, 60.0f), 20.0f, "ms");
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::key, 1 }, "Key",
                                                         StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scale, 1 }, "Scale",
                                                         StringArray { "CHROMATIC", "MAJOR", "MINOR", "HARM MINOR", "PHRYGIAN", "PENTA MINOR" }, 0));
    // Bipolar with fine control near 0: value = sign * 1000 * u^2.
    NormalisableRange<float> shiftRange (-1000.0f, 1000.0f,
        [] (float, float, float t) { const float u = 2.0f * t - 1.0f; return std::copysign (1000.0f * u * u, u); },
        [] (float, float, float v) { return 0.5f + 0.5f * std::copysign (std::sqrt (std::abs (v) / 1000.0f), v); });
    num (ParamIDs::shift, "Freq Shift", shiftRange, 0.0f, "Hz",
         [] (float v, int) { return std::abs (v) < 0.5f ? String ("OFF") : String (v > 0 ? "+" : "") + String (v, std::abs (v) < 10 ? 1 : 0) + " Hz"; });
    const StringArray divisions { "1/32", "1/16", "1/8", "1/4" };
    pct (ParamIDs::stutter, "Stutter", 0.0f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::stutSize, 1 }, "Stutter Size", divisions, 1));
    pct (ParamIDs::chop, "Chop", 0.0f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::chopRate, 1 }, "Chop Rate", divisions, 1));
    pct (ParamIDs::army, "Army", 0.0f);
    num (ParamIDs::width, "Width", { 0.0f, 200.0f, 0.1f }, 100.0f, "%");

    return { p.begin(), p.end() };
}

bool SkaboticAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SkaboticAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    vocoder.prepare (sr);
    tracker.prepare (sr);
    shifter.prepare (sr);
    for (auto& c : comb) c.prepare (sr);
    hilbert.reset();
    armyFx.prepare (sr);
    widthBuf.assign (1 << 12, 0.0f);
    widthPos = 0;
    widthLow = 0.0f;
    widthTapA = (int) (0.011 * sr);
    widthTapB = juce::jmin ((int) widthBuf.size() - 1, (int) (0.019 * sr));
    stutterBuf.setSize (2, (int) (sr * 1.6) + 1); // a 1/4 note at 40 BPM
    stutterBuf.clear();
    lastSlice = -1;
    repeating = false;
    chopGain = 1.0f;
    beatPos = shiftPhase = 0.0;
    dryBuffer.setSize (2, samplesPerBlock);

    for (auto* s : { &widthAmt, &armyAmt, &vocodeAmt, &ringAmt, &metalAmt, &driveAmt, &mixAmt, &outGain })
        s->reset (sr, 0.03);
    for (auto* s : { &shiftHz, &shiftRatio, &metalDelay, &ringHz })
        s->reset (sr, 0.05);

    vocodeAmt.setCurrentAndTargetValue (get (ParamIDs::vocode) * 0.01f);
    ringAmt.setCurrentAndTargetValue (get (ParamIDs::ring) * 0.01f);
    metalAmt.setCurrentAndTargetValue (get (ParamIDs::metal) * 0.01f);
    driveAmt.setCurrentAndTargetValue (get (ParamIDs::drive) * 0.01f);
    mixAmt.setCurrentAndTargetValue (get (ParamIDs::mix) * 0.01f);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (get (ParamIDs::output)));
    shiftRatio.setCurrentAndTargetValue (std::pow (2.0f, get (ParamIDs::pitch) / 12.0f));
    metalDelay.setCurrentAndTargetValue ((float) sr / get (ParamIDs::metalFreq));
    ringHz.setCurrentAndTargetValue (get (ParamIDs::ringFreq));
    shiftHz.setCurrentAndTargetValue (get (ParamIDs::shift));
    armyAmt.setCurrentAndTargetValue (get (ParamIDs::army) * 0.01f);
    widthAmt.setCurrentAndTargetValue (get (ParamIDs::width) * 0.01f);

    carrierFreq = mtof (get (ParamIDs::note));
    ringPhase = holdSample = holdCount = 0.0f;
    inEnv = gateGain = dryPow = wetPow = crushEnv = 0.0f;
    autoGain = 1.0f;
    heldNotes.fill (0);
}

namespace
{
    // Nearest note of the chosen key and scale to a (fractional) MIDI note.
    float snapToScale (float midiNote, int key, int scale)
    {
        static const int masks[] { 0xfff, 0xab5, 0x5ad, 0x9ad, 0x5ab, 0x4a9 }; // bit n = semitone n above the key
        const int mask = masks[juce::jlimit (0, 5, scale)];
        const int centre = (int) std::lround (midiNote);
        float best = (float) centre, bestDist = 100.0f;
        for (int d = -3; d <= 3; ++d)
        {
            const int cand = centre + d;
            if (((mask >> (((cand - key) % 12 + 12) % 12)) & 1) != 0 && std::abs ((float) cand - midiNote) < bestDist)
            {
                bestDist = std::abs ((float) cand - midiNote);
                best = (float) cand;
            }
        }
        return best;
    }
}

void SkaboticAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (buffer.getNumChannels(), 2);
    if (n == 0 || chans == 0)
        return;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())                                 heldNotes[(size_t) m.getNoteNumber()] = ++noteCounter;
        else if (m.isNoteOff())                           heldNotes[(size_t) m.getNoteNumber()] = 0;
        else if (m.isAllNotesOff() || m.isAllSoundOff())  heldNotes.fill (0);
    }

    const auto sr = (float) sampleRate;
    const int carrierType = (int) get (ParamIDs::carrier);
    const int source = (int) get (ParamIDs::source);
    const int key = (int) get (ParamIDs::key), scale = (int) get (ParamIDs::scale);
    const float octaveMul = std::pow (2.0f, get (ParamIDs::octave));
    const float hiss = get (ParamIDs::hiss) * 0.01f;
    const float noteFreq = mtof (get (ParamIDs::note));
    const float crush = get (ParamIDs::crush);
    const float bits = get (ParamIDs::bits);
    const float quant = bits < 15.95f ? std::pow (2.0f, bits - 1.0f) : 0.0f;
    const float gateDb = get (ParamIDs::gate);
    const float gateThresh = gateDb <= -89.9f ? 0.0f : juce::Decibels::decibelsToGain (gateDb);
    const float stutAmt = get (ParamIDs::stutter) * 0.01f, chopAmt = get (ParamIDs::chop) * 0.01f;
    static constexpr double divisionBeats[] { 0.125, 0.25, 0.5, 1.0 };
    const double stutBeats = divisionBeats[juce::jlimit (0, 3, (int) get (ParamIDs::stutSize))];
    const double chopBeats = divisionBeats[juce::jlimit (0, 3, (int) get (ParamIDs::chopRate))];

    vocoder.setFormant (get (ParamIDs::formant));
    vocoder.setRelease (get (ParamIDs::react));
    vocodeAmt.setTargetValue (get (ParamIDs::vocode) * 0.01f);
    ringAmt.setTargetValue (get (ParamIDs::ring) * 0.01f);
    metalAmt.setTargetValue (get (ParamIDs::metal) * 0.01f);
    driveAmt.setTargetValue (get (ParamIDs::drive) * 0.01f);
    mixAmt.setTargetValue (get (ParamIDs::mix) * 0.01f);
    outGain.setTargetValue (juce::Decibels::decibelsToGain (get (ParamIDs::output)));
    shiftRatio.setTargetValue (std::pow (2.0f, get (ParamIDs::pitch) / 12.0f));
    metalDelay.setTargetValue (sr / get (ParamIDs::metalFreq));
    ringHz.setTargetValue (get (ParamIDs::ringFreq));
    shiftHz.setTargetValue (get (ParamIDs::shift));
    armyAmt.setTargetValue (get (ParamIDs::army) * 0.01f);
    widthAmt.setTargetValue (get (ParamIDs::width) * 0.01f);
    const float widthLowCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 250.0f / sr);

    // Host tempo for STUTTER and CHOP; free-runs at the last tempo when the host is stopped.
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (30.0, 300.0, *b);
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition()) beatPos = *q;
        }
    const double beatInc = bpm / 60.0 / sampleRate;
    const int stutLen = juce::jlimit (16, stutterBuf.getNumSamples() - 1, (int) (stutBeats * 60.0 / bpm * sampleRate));

    // MIDI POLY plays every held key; MIDI MONO plays the newest one.
    // With no keys down both fall back to following the voice.
    float midiFreqs[bot::Carrier::maxVoices];
    int numMidi = 0, monoNote = -1;
    if (source == srcMidiPoly)
    {
        for (int k = 0; k < 128 && numMidi < bot::Carrier::maxVoices; ++k)
            if (heldNotes[(size_t) k] != 0)
                midiFreqs[numMidi++] = mtof ((float) k) * octaveMul;
    }
    else if (source == srcMidiMono)
    {
        juce::uint32 newest = 0;
        for (int k = 0; k < 128; ++k)
            if (heldNotes[(size_t) k] > newest) { newest = heldNotes[(size_t) k]; monoNote = k; }
    }

    const bool follows = source == srcTrack || source == srcAutoTune
                      || ((source == srcMidiPoly || source == srcMidiMono) && numMidi == 0 && monoNote < 0);
    const float glide = 1.0f - std::exp (-1.0f / (juce::jmax (0.3f, get (ParamIDs::glide)) * 0.001f * sr));
    const float envRelease = 1.0f - std::exp (-1.0f / (0.08f * sr));
    const float gateCoef = 1.0f - std::exp (-1.0f / (0.004f * sr));
    const float chopCoef = 1.0f - std::exp (-1.0f / (0.0015f * sr));
    const float powCoef = 1.0f - std::exp (-1.0f / (0.3f * sr));
    const float agCoef = 1.0f - std::exp (-1.0f / (0.15f * sr));

    dryBuffer.setSize (2, n, false, false, true);
    for (int c = 0; c < 2; ++c)
        dryBuffer.copyFrom (c, 0, buffer, juce::jmin (c, chans - 1), 0, n);

    float* outs[2] = { buffer.getWritePointer (0), buffer.getWritePointer (chans > 1 ? 1 : 0) };
    const float* dry[2] = { dryBuffer.getReadPointer (0), dryBuffer.getReadPointer (1) };
    float* stut[2] = { stutterBuf.getWritePointer (0), stutterBuf.getWritePointer (1) };
    float peakIn = 0.0f, peakOut = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (dry[0][i] + dry[1][i]);
        const float ax = std::abs (x);
        inEnv += (ax > inEnv ? 0.3f : envRelease) * (ax - inEnv);
        peakIn = juce::jmax (peakIn, ax);
        tracker.push (x);

        const float ratio = shiftRatio.getNextValue();
        const float shifted = shifter.process (x, ratio);
        const float s = std::abs (ratio - 1.0f) > 1e-4f ? shifted : x;

        // Carrier pitch: NOTE = one fixed note for everything, FOLLOW / AUTO-TUNE = the singer's pitch.
        float target = noteFreq;
        if (monoNote >= 0)
            target = mtof ((float) monoNote);
        else if (follows && tracker.freq > 0.0f)
        {
            target = tracker.freq * ratio;
            if (source == srcAutoTune)
                target = mtof (snapToScale (12.0f * std::log2 (target / 440.0f) + 69.0f, key, scale));
        }
        carrierFreq += glide * (target * octaveMul - carrierFreq);

        const float car = numMidi > 0 ? carrierOsc.process (carrierType, midiFreqs, numMidi, sr)
                                      : carrierOsc.process (carrierType, &carrierFreq, 1, sr);
        const float voc = vocoder.process (s, car, carrierOsc.white() * hiss * 2.0f) * 4.0f;
        float y = s + vocodeAmt.getNextValue() * (voc - s);

        // Ring modulator.
        const float ra = ringAmt.getNextValue();
        ringPhase += ringHz.getNextValue() / sr;
        ringPhase -= std::floor (ringPhase);
        y += ra * (y * 1.4f * std::sin (6.2831853f * ringPhase) - y);

        // Frequency shifter: moves every partial by the same Hz, so the voice turns inharmonic and alien.
        {
            float re, im;
            hilbert.process (y, re, im);
            const float hz = shiftHz.getNextValue();
            shiftPhase += (double) hz / sampleRate;
            shiftPhase -= std::floor (shiftPhase);
            if (std::abs (hz) > 0.5f)
            {
                const float ph = 6.2831853f * (float) shiftPhase;
                y = re * std::cos (ph) + im * std::sin (ph);
            }
        }

        // 8-bit: sample-and-hold then bit depth.
        if (crush > 1.01f)
        {
            holdCount += 1.0f;
            if (holdCount >= crush) { holdCount -= crush; holdSample = y; }
            y = holdSample;
        }
        // Quantise relative to the voice level, so low bit depths crunch quiet vocals instead of muting them.
        crushEnv += (std::abs (y) > crushEnv ? 0.05f : envRelease * 0.25f) * (std::abs (y) - crushEnv);
        if (quant > 0.0f)
        {
            const float norm = juce::jmax (crushEnv * 1.5f, 1e-3f);
            y = std::round (y / norm * quant) / quant * norm;
        }

        const float da = driveAmt.getNextValue();
        if (da > 0.001f)
            y = std::tanh (y * (1.0f + da * 20.0f));

        // Army: detuned delayed copies, two per side.
        float side[2];
        armyFx.process (y, side[0], side[1]);
        const float aa = armyAmt.getNextValue();

        // Metal comb, slightly different tube per side for width.
        const float ma = metalAmt.getNextValue();
        const float md = metalDelay.getNextValue();
        const float fb = 0.6f + 0.37f * ma;
        float wet[2];
        for (int c = 0; c < 2; ++c)
        {
            const float in = y * (1.0f - 0.4f * aa) + aa * side[c] * 1.2f;
            const float tube = comb[c].process (in, md * (c == 0 ? 1.0f : 1.071f), fb) * (1.0f - fb) * 1.5f;
            wet[c] = in + ma * (tube - in);
        }

        // Stutter: at each slice boundary, maybe replay the last live slice instead of the voice.
        if (stutAmt > 0.0f)
        {
            const double slicePos = beatPos / stutBeats;
            const auto slice = (juce::int64) std::floor (slicePos);
            const float frac = (float) (slicePos - (double) slice);
            if (slice != lastSlice)
            {
                // repeats can chain ("ro-ro-ro"); the buffer keeps the last live slice
                repeating = lastSlice >= 0 && (carrierOsc.white() * 0.5f + 0.5f) < stutAmt;
                lastSlice = slice;
            }
            const int wi = juce::jlimit (0, stutLen, (int) (frac * (float) stutLen));
            if (repeating)
            {
                const float edge = juce::jmin (1.0f, juce::jmin (frac, 1.0f - frac) * (float) stutLen / 96.0f);
                for (int c = 0; c < 2; ++c)
                    wet[c] += edge * (stut[c][wi] - wet[c]);
            }
            else
                for (int c = 0; c < 2; ++c)
                    stut[c][wi] = wet[c];
        }
        else
        {
            lastSlice = -1;
            repeating = false;
        }

        // Chop: tempo-synced on/off gate.
        if (chopAmt > 0.0f)
        {
            const double phase = beatPos / chopBeats;
            chopGain += chopCoef * ((phase - std::floor (phase) < 0.5 ? 1.0f : 0.0f) - chopGain);
            const float g = 1.0f - chopAmt * (1.0f - chopGain);
            wet[0] *= g;
            wet[1] *= g;
        }
        beatPos += beatInc;

        // Width: 0% = mono, 100% = as is, above 100% adds a side made from delayed copies of the mid.
        // Only the side changes, so the mono sum stays the same; lows under ~250 Hz stay centred.
        {
            const float w = widthAmt.getNextValue();
            wetPow += powCoef * (0.5f * (wet[0] * wet[0] + wet[1] * wet[1]) - wetPow); // level match ignores WIDTH
            const float mid = 0.5f * (wet[0] + wet[1]);
            const int mask = (int) widthBuf.size() - 1;
            widthBuf[(size_t) widthPos] = mid;
            float made = widthBuf[(size_t) ((widthPos - widthTapA) & mask)] - widthBuf[(size_t) ((widthPos - widthTapB) & mask)];
            widthPos = (widthPos + 1) & mask;
            widthLow += widthLowCoef * (made - widthLow);
            made -= widthLow;
            const float sideOut = 0.5f * (wet[0] - wet[1]) * w + juce::jmax (0.0f, w - 1.0f) * 0.7f * made;
            wet[0] = mid + sideOut;
            wet[1] = mid - sideOut;
        }

        // Every preset comes out about as loud as the voice went in.
        dryPow += powCoef * (x * x - dryPow);
        if (dryPow > 1e-7f)
            autoGain += agCoef * (juce::jlimit (0.05f, 4.0f, std::sqrt (dryPow / (wetPow + 1e-12f))) - autoGain);

        gateGain += gateCoef * ((gateThresh <= 0.0f || inEnv > gateThresh ? 1.0f : 0.0f) - gateGain);

        const float mixv = mixAmt.getNextValue(), og = outGain.getNextValue();
        for (int c = 0; c < chans; ++c)
        {
            const float w = wet[c] * autoGain * gateGain;
            const float o = (dry[c][i] + mixv * (w - dry[c][i])) * og;
            outs[c][i] = o;
            peakOut = juce::jmax (peakOut, std::abs (o));
        }
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    inLevel.store (peakIn);
    outLevel.store (peakOut);
    voiceHz.store (tracker.voiced ? tracker.freq : 0.0f);
    carrierHz.store (numMidi > 0 ? midiFreqs[0] : carrierFreq);
    gateOpen.store (gateGain > 0.5f && inEnv > 1e-4f);
    for (int b = 0; b < bot::Vocoder::numBands; ++b)
        bandLevel[(size_t) b].store (vocoder.env[(size_t) b]);
}

void SkaboticAudioProcessor::randomise()
{
    auto& r = random;
    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    auto chance = [&r] (float p) { return r.nextFloat() < p; };
    auto range = [&r] (float lo, float hi) { return lo + r.nextFloat() * (hi - lo); };
    auto logRange = [&r] (float lo, float hi) { return lo * std::pow (hi / lo, r.nextFloat()); };

    set (ParamIDs::vocode, chance (0.75f) ? range (60.0f, 100.0f) : range (0.0f, 60.0f));
    set (ParamIDs::carrier, (float) r.nextInt (6));
    set (ParamIDs::source, (float) r.nextInt (3)); // never MIDI: random should sound without keys
    set (ParamIDs::note, (float) r.nextInt ({ 33, 60 }));
    set (ParamIDs::octave, chance (0.6f) ? 0.0f : (float) r.nextInt ({ -1, 2 }));
    set (ParamIDs::formant, chance (0.4f) ? 0.0f : range (-10.0f, 10.0f));
    set (ParamIDs::react, logRange (4.0f, 120.0f));
    set (ParamIDs::hiss, range (5.0f, 60.0f));
    set (ParamIDs::pitch, chance (0.5f) ? 0.0f : (float) r.nextInt ({ -12, 13 }));
    set (ParamIDs::ring, chance (0.5f) ? 0.0f : range (10.0f, 80.0f));
    set (ParamIDs::ringFreq, logRange (5.0f, 1500.0f));
    set (ParamIDs::bits, chance (0.5f) ? 16.0f : range (3.0f, 10.0f));
    set (ParamIDs::crush, chance (0.5f) ? 1.0f : logRange (2.0f, 12.0f));
    set (ParamIDs::metal, chance (0.4f) ? 0.0f : range (10.0f, 70.0f));
    set (ParamIDs::metalFreq, logRange (60.0f, 1500.0f));
    set (ParamIDs::drive, chance (0.4f) ? 0.0f : range (5.0f, 60.0f));
    set (ParamIDs::glide, logRange (2.0f, 120.0f));
    set (ParamIDs::key, (float) r.nextInt (12));
    set (ParamIDs::scale, (float) r.nextInt (6));
    set (ParamIDs::shift, chance (0.7f) ? 0.0f : (chance (0.5f) ? 1.0f : -1.0f) * logRange (5.0f, 700.0f));
    set (ParamIDs::stutter, chance (0.7f) ? 0.0f : range (20.0f, 70.0f));
    set (ParamIDs::stutSize, (float) r.nextInt (4));
    set (ParamIDs::chop, chance (0.7f) ? 0.0f : range (20.0f, 80.0f));
    set (ParamIDs::chopRate, (float) r.nextInt (4));
    set (ParamIDs::army, chance (0.5f) ? 0.0f : range (20.0f, 90.0f));
    set (ParamIDs::width, chance (0.3f) ? 100.0f : range (100.0f, 190.0f));
    randomised = true;
}

int SkaboticAudioProcessor::getNumPrograms() { return numPresets; }

const juce::String SkaboticAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void SkaboticAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPresets))
        return;

    currentPreset = index;
    randomised = false;
    const auto& p = presets[index];
    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set (ParamIDs::vocode, p.vocode);
    set (ParamIDs::carrier, p.carrier);
    set (ParamIDs::source, p.source);
    set (ParamIDs::note, p.note);
    set (ParamIDs::octave, p.octave);
    set (ParamIDs::formant, p.formant);
    set (ParamIDs::react, p.react);
    set (ParamIDs::hiss, p.hiss);
    set (ParamIDs::pitch, p.pitch);
    set (ParamIDs::ring, p.ring);
    set (ParamIDs::ringFreq, p.ringFreq);
    set (ParamIDs::bits, p.bits);
    set (ParamIDs::crush, p.crush);
    set (ParamIDs::metal, p.metal);
    set (ParamIDs::metalFreq, p.metalFreq);
    set (ParamIDs::drive, p.drive);
    set (ParamIDs::gate, p.gate);
    set (ParamIDs::mix, p.mix);
    set (ParamIDs::glide, p.glide);
    set (ParamIDs::key, p.key);
    set (ParamIDs::scale, p.scale);
    set (ParamIDs::shift, p.shift);
    set (ParamIDs::stutter, p.stutter);
    set (ParamIDs::stutSize, p.stutSize);
    set (ParamIDs::chop, p.chop);
    set (ParamIDs::chopRate, p.chopRate);
    set (ParamIDs::army, p.army);
    set (ParamIDs::width, p.width);
}

void SkaboticAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SkaboticAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentPreset = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("preset", 0));
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* SkaboticAudioProcessor::createEditor() { return skazi::withUpdateNotice (new SkaboticAudioProcessorEditor (*this)); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkaboticAudioProcessor(); }
