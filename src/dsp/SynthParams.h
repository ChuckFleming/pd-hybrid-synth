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
    OscType oscType    = OscType::PhaseDistortion;
    double  pdAmount   = 0.30;
    double  pulseWidth = 0.5;

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
