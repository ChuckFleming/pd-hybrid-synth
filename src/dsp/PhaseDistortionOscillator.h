#pragma once

namespace pdhybrid {

/**
    Casio CZ-style phase-distortion oscillator.

    A linear phase accumulator is passed through a nonlinear, movable-knee
    two-segment phase map before a sine lookup. At amount == 0 the map is the
    identity, so the output is a pure sine. As `amount` rises the knee slides
    toward zero, compressing the first half-cycle and stretching the second --
    this is the classic PD "sawtooth" morph and it introduces a growing
    harmonic series.

    Pure C++, no JUCE dependency, fully deterministic: the offline test harness
    targets this class directly.
*/
class PhaseDistortionOscillator
{
public:
    void  setSampleRate (double sampleRateHz) noexcept;
    void  setFrequency  (double frequencyHz) noexcept;
    void  setAmount     (double amount01) noexcept;   // 0 = pure sine .. 1 = max distortion
    void  reset         () noexcept;

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    void updateIncrement() noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;
    double phase_      = 0.0;   // normalised [0, 1)
    double amount_     = 0.0;
};

} // namespace pdhybrid
