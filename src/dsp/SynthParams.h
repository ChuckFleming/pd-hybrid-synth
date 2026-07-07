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

enum class GlideMode
{
    Off = 0,   // no portamento
    Always,    // glide from the previous note every time
    Legato     // glide only when a note is already held
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
    double     keyTrack     = 0.0;   // 0..1 cutoff key-follow (1 = one octave per octave)
    double     filterEnvAmount = 0.0;   // filter-envelope depth in octaves (bipolar)
    double     filterEnvA = 0.01, filterEnvD = 0.20, filterEnvS = 0.0, filterEnvR = 0.30;

    double drive     = 1.0;
    double bias      = 0.0;

    double attack  = 0.01;
    double decay   = 0.10;
    double sustain = 0.70;
    double release = 0.20;

    double gain    = 0.80;

    // Stereo placement: master pan plus a per-voice spread by keyboard position
    // (low notes left, high notes right) for a wider sound.
    double pan       = 0.0;    // -1 = hard left .. +1 = hard right
    double panSpread = 0.0;    // 0..1 amount of keyboard-position spread
    double drift     = 0.0;    // 0..1 analog-style slow pitch + PD-amount wander

    // Glide / portamento.
    GlideMode glideMode  = GlideMode::Off;
    double    glideTime  = 0.10;   // seconds to slide to the new note
    double    glideCurve = 1.0;    // ramp exponent (1 = linear, <1 fast-in, >1 slow-in)

    // Modulation
    double    modEnvA = 0.01, modEnvD = 0.20, modEnvS = 0.0, modEnvR = 0.30;
    double    lfoRate = 5.0;
    int       lfoWave = 0;
    double    lfo2Rate = 0.5;
    int       lfo2Wave = 0;
    ModMatrix modMatrix;
};

} // namespace pdhybrid
