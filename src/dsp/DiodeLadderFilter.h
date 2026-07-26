#pragma once

namespace pdhybrid {

/**
    Diode-ladder lowpass: the squelchy, acid-flavoured cousin of the transistor
    ladder in LadderFilter.

    Two things separate it from the Moog-style ladder, and both are modelled
    here directly rather than through a circuit netlist:

      - **Spread poles.** A transistor ladder has four coincident poles; the
        diode ladder's stages load one another, so its poles sit at different
        frequencies. That softens the knee and is most of why it sounds looser
        and more vocal than the Moog.
      - **Asymmetric saturation.** The diodes conduct differently either way, so
        the resonance path clips asymmetrically and generates even harmonics --
        the "growl" as resonance climbs.

    Like LadderFilter, the zero-delay feedback loop is solved with a few Newton
    iterations, so resonance self-limits and there is no unit-delay detuning. A
    resonance-compensation term feeds part of the input back in to offset the
    bass loss that the feedback path would otherwise cause.
*/
class DiodeLadderFilter
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

    static constexpr int kStages = 4;

    double sampleRate_ = 44100.0;
    double cutoff_     = 1000.0;
    double k_          = 0.0;    // feedback amount (resonance)
    double comp_       = 0.0;    // input feed-forward that restores the low end

    double g_[kStages] = { 0.0, 0.0, 0.0, 0.0 };   // tan(pi * fc * spread / fs)
    double G_[kStages] = { 0.0, 0.0, 0.0, 0.0 };   // g / (1 + g)

    double s_[kStages] = { 0.0, 0.0, 0.0, 0.0 };   // integrator states
};

} // namespace pdhybrid
