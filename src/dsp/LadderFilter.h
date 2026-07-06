#pragma once

namespace pdhybrid {

/**
    Moog-style 4-pole transistor-ladder lowpass, built from four TPT
    (topology-preserving transform / zero-delay-feedback) one-pole stages with a
    global tanh-saturated resonance feedback path.

    The instantaneous feedback loop y4 = A*(x - k*tanh(y4)) + B is solved with a
    few Newton iterations each sample, so the filter is truly zero-delay (well
    tuned), the resonance self-limits via the tanh (bounded, self-oscillating
    near k = 4), and there is no unit-delay detuning.

    Pure C++, no JUCE. The frequency-response harness targets it directly.
*/
class LadderFilter
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void setCutoff     (double cutoffHz) noexcept;
    void setResonance  (double resonance01) noexcept;   // 0..1 ( >~0.9 self-oscillates )
    void reset         () noexcept;

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    void updateCoefficients() noexcept;

    double sampleRate_ = 44100.0;
    double cutoff_     = 1000.0;
    double k_          = 0.0;    // feedback amount (resonance)

    double g_ = 0.0;            // tan(pi * fc / fs)
    double G_ = 0.0;            // g / (1 + g)

    double s1_ = 0.0, s2_ = 0.0, s3_ = 0.0, s4_ = 0.0;  // integrator states
};

} // namespace pdhybrid
