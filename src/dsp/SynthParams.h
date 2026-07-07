#pragma once

#include "ModMatrix.h"

namespace pdhybrid {

enum class FilterType
{
    Ladder = 0,     // analog-modeled 4-pole ladder
    StateVariable,  // morphable TPT state-variable (LP/BP/HP)
    PdResonator,    // phase-distortion resonator
    Comb,           // tuned feedback comb / waveguide
    Allpass         // allpass dispersion
};

enum class OscType
{
    PhaseDistortion = 0,   // Casio CZ-style PD
    Saw,                   // analog PolyBLEP waveforms
    Square,
    Triangle,
    Pulse
};

// Per-block synth settings pushed from the host/UI down to every voice.
struct SynthParams
{
    // --- Oscillator A ---
    OscType oscAType       = OscType::PhaseDistortion;
    int     oscAWave       = 0;      // PdWave index (used when type == PhaseDistortion)
    double  oscAAmount     = 0.30;   // PD DCW amount
    double  oscAPulseWidth = 0.5;
    int     oscAOctave     = 0;
    int     oscASemi       = 0;
    double  oscAFine       = 0.0;    // cents

    // --- Oscillator B ---
    OscType oscBType       = OscType::Saw;
    int     oscBWave       = 0;
    double  oscBAmount     = 0.30;
    double  oscBPulseWidth = 0.5;
    int     oscBOctave     = 0;
    int     oscBSemi       = 0;
    double  oscBFine       = 0.0;

    // --- Mixer (independent sum) ---
    double  oscALevel  = 1.0;
    double  oscBLevel  = 0.0;   // B silent by default -> single-osc patches unchanged
    double  noiseLevel = 0.0;

    FilterType filterType   = FilterType::Ladder;
    double     cutoffHz     = 8000.0;
    double     resonance    = 0.20;
    double     filterMorph  = 0.0;   // PD-reso amount / comb damping / allpass dispersion

    double drive     = 1.0;
    double bias      = 0.0;

    double attack  = 0.01;
    double decay   = 0.10;
    double sustain = 0.70;
    double release = 0.20;

    double gain    = 0.80;

    // Modulation
    double    modEnvA = 0.01, modEnvD = 0.20, modEnvS = 0.0, modEnvR = 0.30;
    double    lfoRate = 5.0;
    int       lfoWave = 0;
    ModMatrix modMatrix;
};

} // namespace pdhybrid
