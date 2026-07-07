#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    The eight Casio CZ-style phase-distortion waveshapes.

    The first five are true phase-distortion waves: a linear phase ramp is passed
    through a non-linear, `amount`-driven remap (the "DCW" knob) before a sine
    lookup, so raising `amount` warps the phase and grows the harmonic series
    while the pitch stays fixed. The three resonant waves use the CZ's second
    trick -- "windowed sync": a sine running at k x the fundamental is windowed
    by a one-cycle envelope (saw / triangle / trapezoid), and here `amount`
    sweeps the resonant multiple k (i.e. the perceived resonant frequency).
*/
enum class PdWave
{
    Sawtooth = 0,   // classic movable-knee saw morph
    Square,         // twin phase plateaus -> square-ish
    Pulse,          // single asymmetric plateau -> pulse
    DoubleSine,     // sine that speeds toward two lobes
    SawPulse,       // sharpened (double-knee) saw
    ResonantI,      // windowed sync, sawtooth window
    ResonantII,     // windowed sync, triangle window
    ResonantIII     // windowed sync, trapezoid window
};

/**
    Casio CZ-style phase-distortion oscillator.

    A linear phase accumulator drives one of the eight CZ waveshapes above. The
    `amount` parameter is the DCW control: for the phase-distortion waves it is
    the distortion depth (0 = pure/tame, 1 = maximal harmonics); for the resonant
    waves it sweeps the resonant peak. Output is anti-aliased with an internal
    FIR oversampler (the aggressive waves and the resonant sync edges alias
    badly at the base rate).

    Pure C++, no JUCE dependency, fully deterministic: the offline test harness
    targets this class directly.
*/
class PhaseDistortionOscillator
{
public:
    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setAmount      (double amount01) noexcept;   // 0 = pure sine .. 1 = max distortion
    void  setWave        (PdWave wave) noexcept { wave_ = wave; }
    void  setOversampling (int factor) noexcept;       // 1, 2, 4, or 8
    void  reset          () noexcept;

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    void   updateIncrement() noexcept;
    double coreSample     () noexcept;   // one sample at the oversampled rate

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;   // per base-rate sample
    double phase_      = 0.0;   // normalised [0, 1)
    double amount_     = 0.0;
    PdWave wave_       = PdWave::Sawtooth;

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
