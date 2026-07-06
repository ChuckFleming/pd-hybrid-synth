#pragma once

namespace pdhybrid {

/**
    Signature "non-traditional" filter: a resonator whose self-oscillation
    waveform is phase-distorted instead of a pure sine.

    A damped quadrature resonator (a decaying rotating phasor driven by the
    input) rings at the resonant frequency. From its two quadrature states we
    recover instantaneous phase and amplitude, then emit a *phase-distorted*
    sine of that phase. At amount 0 the output is the ordinary resonant sine
    (a bandpass); as amount rises the ring gains a harmonic series at multiples
    of the resonant frequency -- the CZ oscillator concept applied to filtering.
*/
class PhaseDistortionResonator
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void setFrequency  (double frequencyHz) noexcept;   // resonant frequency
    void setResonance  (double resonance01) noexcept;   // 0..1 -> ring length
    void setAmount     (double amount01) noexcept;      // phase distortion
    void reset         () noexcept;

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    void updateCoefficients() noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 1000.0;
    double resonance_  = 0.9;
    double amount_     = 0.0;

    double cosT_    = 1.0;
    double sinT_    = 0.0;
    double damp_    = 0.99;
    double gainComp_ = 0.01;

    double u_ = 0.0;   // quadrature states (real/imag of the decaying phasor)
    double v_ = 0.0;
};

} // namespace pdhybrid
