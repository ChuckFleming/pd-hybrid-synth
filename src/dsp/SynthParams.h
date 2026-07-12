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
    Pulse,
    VPS,                   // vector phaseshaping (movable 2D inflection point)
    Scanned,               // scanned synthesis (a plucked mass-spring ring)
    Vosim                  // VOSIM (bursts of decaying sin^2 pulses -> formants)
};

enum class GlideMode
{
    Off = 0,   // no portamento
    Always,    // glide from the previous note every time
    Legato     // glide only when a note is already held
};

enum class FilterRouting
{
    Single = 0,   // filter A only
    Series,       // filter A -> filter B
    Parallel      // filter A + filter B, mixed
};

// LFO frequency (Hz) for a tempo-sync division at the given BPM. `divIndex`
// selects 1/1, 1/2, 1/4, 1/8, 1/16, dotted-1/4, dotted-1/8, 1/4-triplet,
// 1/8-triplet (0..8). Factors are cycles-per-beat.
inline double syncedLfoHz (double bpm, int divIndex) noexcept
{
    static const double mult[] = { 0.25, 0.5, 1.0, 2.0, 4.0,
                                   2.0 / 3.0, 4.0 / 3.0, 1.5, 3.0 };
    if (divIndex < 0) divIndex = 0;
    if (divIndex > 8) divIndex = 8;
    return (bpm / 60.0) * mult[divIndex];
}

// Delay time (seconds) for a tempo-sync note division at the given BPM. Same
// division order as the LFO sync. Clamped to the delay's 2 s maximum.
inline double syncedDelaySeconds (double bpm, int divIndex) noexcept
{
    static const double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25,
                                    1.5, 0.75, 2.0 / 3.0, 1.0 / 3.0 };
    if (divIndex < 0) divIndex = 0;
    if (divIndex > 8) divIndex = 8;
    const double t = beats[divIndex] * 60.0 / (bpm > 1.0 ? bpm : 120.0);
    return t < 0.001 ? 0.001 : (t > 2.0 ? 2.0 : t);
}

// Microtuning: cents deviation from 12-TET for a pitch class (0 = C .. 11 = B),
// for a built-in temperament (0 = Equal, 1 = Just, 2 = Pythagorean). Equal
// returns 0 so the default is bit-identical to standard tuning.
inline double tuningCentsOffset (int scale, int pitchClass) noexcept
{
    static const double kJust[12] = { 0.0, 11.7, 3.9, 15.6, -13.7, -2.0, -10.2,
                                      2.0, 13.7, -15.6, 17.6, -11.7 };
    static const double kPyth[12] = { 0.0, 13.7, 3.9, -5.9, 7.8, -2.0, 11.7,
                                      2.0, 15.6, 5.9, -3.9, 9.8 };
    const int pc = ((pitchClass % 12) + 12) % 12;
    if (scale == 1) return kJust[pc];
    if (scale == 2) return kPyth[pc];
    return 0.0;
}

// Per-block synth settings pushed from the host/UI down to every voice.
struct SynthParams
{
    // --- Oscillator A ---
    OscType oscAType       = OscType::PhaseDistortion;
    int     oscAWave       = 0;      // PdWave index (used when type == PhaseDistortion)
    int     oscAWave2      = 0;      // 2nd PD wave for combine
    bool    oscACombine    = false;  // alternate oscAWave/oscAWave2 per cycle
    double  oscAAmount     = 0.30;   // PD DCW amount
    double  oscAPulseWidth = 0.5;
    int     oscAOctave     = 0;
    int     oscASemi       = 0;
    double  oscAFine       = 0.0;    // cents
    double  oscAEqLow = 0.0, oscAEqMid = 0.0, oscAEqHigh = 0.0;   // per-osc EQ, dB

    // --- Oscillator B ---
    OscType oscBType       = OscType::Saw;
    int     oscBWave       = 0;
    int     oscBWave2      = 0;
    bool    oscBCombine    = false;
    double  oscBAmount     = 0.30;
    double  oscBPulseWidth = 0.5;
    int     oscBOctave     = 0;
    int     oscBSemi       = 0;
    double  oscBFine       = 0.0;
    double  oscBEqLow = 0.0, oscBEqMid = 0.0, oscBEqHigh = 0.0;   // per-osc EQ, dB

    // --- Mixer (independent sum) ---
    double  oscALevel  = 1.0;
    double  oscBLevel  = 0.0;   // B silent by default -> single-osc patches unchanged
    double  noiseLevel = 0.0;
    double  ringModLevel = 0.0; // CZ-style ring mod: adds (oscA * oscB) at this level
    int     oscCrossMod  = 0;   // 0=Off, 1=Hard Sync (A masters B), 2=Phase Mod (B -> A)
    double  crossModAmount = 0.0;

    FilterType filterType   = FilterType::Ladder;
    double     cutoffHz     = 8000.0;
    double     resonance    = 0.20;
    double     filterMorph  = 0.0;   // PD-reso amount / comb damping / allpass dispersion
    double     keyTrack     = 0.0;   // 0..1 cutoff key-follow (1 = one octave per octave)
    double     filterEnvAmount = 0.0;   // filter-envelope depth in octaves (bipolar)
    double     filterEnvA = 0.01, filterEnvD = 0.20, filterEnvS = 0.0, filterEnvR = 0.30;

    // Second filter + routing. Filter A reuses the fields above.
    FilterRouting filterRouting = FilterRouting::Single;
    FilterType    filter2Type   = FilterType::Ladder;
    double        filter2Cutoff = 8000.0;
    double        filter2Res    = 0.20;
    double        filter2Morph  = 0.0;
    double        filter2EnvAmount = 0.0;   // Filter B env depth (octaves, bipolar)
    double        filter2EnvA = 0.01, filter2EnvD = 0.20, filter2EnvS = 0.0, filter2EnvR = 0.30;

    bool   driveOn   = true;   // overdrive stage bypass when false
    double drive     = 1.0;
    double bias      = 0.0;
    int    driveType = 0;      // ShaperCurve index
    double crushBits = 16.0;   // bit-depth reduction (>= 16 = off)
    double downsample = 1.0;   // sample-rate reduction factor (1 = off)
    int    drivePos  = 0;      // 0 = post-filter (default), 1 = pre-filter

    // Velocity sensitivity: 0 = ignore velocity, 1 = full velocity scaling.
    double ampVelSens    = 1.0;   // amp level scaled by velocity (1 = classic behaviour)
    double filterVelSens = 0.0;   // filter-envelope depth scaled by velocity
    double noiseModDepth = 0.0;   // CZ noise pitch modulation (0..1)

    double attack  = 0.01;
    double decay   = 0.10;
    double sustain = 0.70;
    double release = 0.20;

    double gain    = 0.80;

    int    oscOversampling = 4;   // PD oscillator + overdrive oversampling (1/2/4)

    // v6.0: Voice allocation & pitch bend
    int    polyphony = 16;        // active voices (1-16)
    int    voiceMode = 0;         // 0=Poly, 1=Mono, 2=Legato, 3=Unison-Legato
    int    notePriority = 0;      // 0=Last, 1=Top, 2=Bottom (mono mode fallback)
    int    stealPolicy = 0;       // 0=Oldest, 1=Quietest
    bool   monoRetrigger = true;  // true=retrig on note change, false=true legato
    double pitchBendRange = 2.0;  // semitones for MIDI bend (1-24)
    double masterTuneHz = 440.0;  // A4 reference (415-465)
    int    transpose    = 0;      // semitone transpose (-24..24)
    int    tuningScale  = 0;      // 0=Equal, 1=Just, 2=Pythagorean

    // Stereo placement: master pan plus a per-voice spread by keyboard position
    // (low notes left, high notes right) for a wider sound.
    double pan       = 0.0;    // -1 = hard left .. +1 = hard right
    double panSpread = 0.0;    // 0..1 amount of keyboard-position spread
    double drift     = 0.0;    // 0..1 analog-style slow pitch + PD-amount wander

    // Unison: stack several detuned, pan-spread sub-voices per note.
    int    unisonVoices = 1;    // 1..6 (1 = off)
    double unisonDetune = 15.0; // max detune in cents
    double unisonWidth  = 0.5;  // 0..1 stereo spread of the stack

    // Glide / portamento.
    GlideMode glideMode  = GlideMode::Off;
    double    glideTime  = 0.10;   // seconds to slide to the new note
    double    glideCurve = 1.0;    // ramp exponent (1 = linear, <1 fast-in, >1 slow-in)

    // Modulation
    double    modEnvA = 0.01, modEnvD = 0.20, modEnvS = 0.0, modEnvR = 0.30;
    double    lfoRate = 5.0;
    int       lfoWave = 0;
    double    lfoFade = 0.0;      // fade-in seconds (0 = instant)
    double    lfoPhase = 0.0;     // 0..1 start phase offset
    bool      lfoRetrig = true;   // reset phase on note-on
    double    lfo2Rate = 0.5;
    int       lfo2Wave = 0;
    double    lfo2Fade = 0.0;
    double    lfo2Phase = 0.0;
    bool      lfo2Retrig = true;

    // CZ-style 8-stage multi-envelope (rate/level per stage + one sustain stage).
    // Routed to the filter cutoff via czAmount (octaves, bipolar) so it acts as a
    // multi-stage filter envelope; also available as ModSource::MultiEnv.
    double czAmount  = 0.0;                 // octaves, bipolar (0 = off)
    int    czSustain = 5;                   // 1-based sustain stage
    double czRate[8]  = { 0.02, 0.15, 0.10, 0.30, 0.50, 0.40, 0.60, 0.50 };  // seconds
    double czLevel[8] = { 1.00, 0.80, 0.60, 0.50, 0.50, 0.30, 0.15, 0.00 };  // 0..1

    // CZ-style 8-stage pitch (DCO) envelope. Levels are bipolar around 0.5
    // (0.5 = no offset); pitchEnvAmount scales the deviation in semitones.
    double pitchEnvAmount = 0.0;             // semitones (bipolar, 0 = off)
    int    pitchEnvSustain = 8;              // 1-based sustain stage
    double pitchEnvRate[8]  = { 0.02, 0.20, 0.30, 0.40, 0.50, 0.50, 0.50, 0.50 };
    double pitchEnvLevel[8] = { 0.50, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50 };

    // CZ-style 8-stage DCW (wave-depth) envelope. Same bipolar-around-0.5
    // convention; dcwEnvAmount scales the deviation added to both oscs' PD amount.
    double dcwEnvAmount = 0.0;
    int    dcwEnvSustain = 8;
    double dcwEnvRate[8]  = { 0.02, 0.20, 0.30, 0.40, 0.50, 0.50, 0.50, 0.50 };
    double dcwEnvLevel[8] = { 0.50, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50 };

    // Macro sources (also usable as global mod sources).
    double macro1 = 0.0, macro2 = 0.0;

    ModMatrix modMatrix;
};

} // namespace pdhybrid
