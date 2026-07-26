#pragma once

namespace pdhybrid {

/**
    Signature "non-traditional" filter: a resonant lowpass whose resonance peak
    is a *phase-distorted* sine rather than a pure one.

    Structure (body + ring), which is how the CZ's resonant waves are built:

      - A TPT state-variable filter at the cutoff supplies the **body** (its
        lowpass output) and the **excitation** (its bandpass output). The body
        means the filter always passes signal, whatever the resonance.
      - A free-running phase accumulator at the cutoff supplies the **ring**,
        windowed by the smoothed envelope of that bandpass. Driving the warp
        from a steadily rotating phase is what makes the harmonics come out
        clean: reading instantaneous phase off the resonator states (atan2), or
        re-syncing to their zero crossings, jitters with a broadband input and
        yields noise instead of a harmonic series.
      - The windowed, phase-distorted sine is added on top of the body with a
        depth set by the resonance.

    So amount 0 behaves as an ordinary resonant lowpass, and raising the amount
    grows a harmonic series on the resonant peak -- the CZ oscillator concept
    applied to filtering. The warp is faded out as the cutoff approaches Nyquist
    (the filters run at base rate, unlike the oscillators) to keep it clean.
*/
class PhaseDistortionResonator
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void setFrequency  (double frequencyHz) noexcept;   // resonant frequency
    void setResonance  (double resonance01) noexcept;   // 0..1 -> Q + ring depth
    void setAmount     (double amount01) noexcept;      // phase distortion
    void reset         () noexcept;

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    void updateCoefficients() noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 1000.0;
    double resonance_  = 0.2;
    double amount_     = 0.0;

    // TPT state-variable coefficients (shared shape with StateVariableFilter).
    double g_ = 0.0, k_ = 2.0, a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double ic1_ = 0.0, ic2_ = 0.0;

    double ringGain_ = 0.0;   // depth of the phase-distorted peak
    double effAmount_ = 0.0;  // amount after the anti-alias fade
    double phaseInc_ = 0.0;   // ring accumulator step, cycles/sample
    double envAtk_   = 0.0;   // envelope-follower coefficients
    double envRel_   = 0.0;

    double phase_    = 0.0;   // ring phase, 0..1
    double env_      = 0.0;   // smoothed bandpass magnitude
};

} // namespace pdhybrid
