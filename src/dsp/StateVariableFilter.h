#pragma once

namespace pdhybrid {

enum class SvfMode
{
    Lowpass = 0,
    Bandpass,
    Highpass,
    Notch
};

/**
    Topology-preserving-transform (zero-delay-feedback) state-variable filter.
    Produces lowpass, bandpass, highpass and notch simultaneously (12 dB/oct),
    is cheap and unconditionally stable. Either pick a discrete mode or enable
    a morph knob that crossfades LP -> BP -> HP.
*/
class StateVariableFilter
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void setCutoff     (double cutoffHz) noexcept;
    void setResonance  (double resonance01) noexcept;   // 0..1 -> Q
    void setMode       (SvfMode mode) noexcept   { mode_ = mode; useMorph_ = false; }
    void setMorph      (double morph01) noexcept { morph_ = morph01; useMorph_ = true; }
    void reset         () noexcept;

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    void updateCoefficients() noexcept;

    double sampleRate_ = 44100.0;
    double cutoff_     = 1000.0;
    double resonance_  = 0.2;

    double g_  = 0.0, k_ = 1.4;
    double a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double ic1_ = 0.0, ic2_ = 0.0;

    SvfMode mode_    = SvfMode::Lowpass;
    bool    useMorph_ = false;
    double  morph_   = 0.0;
};

} // namespace pdhybrid
