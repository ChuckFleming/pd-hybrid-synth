#pragma once

namespace pdhybrid {

/**
    Allpass dispersion filter: a cascade of first-order allpass sections. Each
    section has unity magnitude at every frequency but a frequency-dependent
    phase, so the cascade smears phase (disperses transients, adds glassy/
    metallic colour) without altering the magnitude spectrum. The defining,
    testable property is a flat 0 dB magnitude response.

    The break frequency (where each section's phase passes -90 degrees) can be
    set directly with setFrequency, which is what makes the cascade sweepable;
    setCoefficient remains for callers that want to dial the raw coefficient.

    An optional resonant feedback path (default off, so the flat-magnitude
    property is preserved unless it is asked for) turns the cascade into the
    core of a phaser once a caller mixes the dry signal back in.
*/
class AllpassDispersion
{
public:
    static constexpr int kMaxStages = 16;

    void setSampleRate  (double sampleRateHz) noexcept;
    void setFrequency   (double frequencyHz) noexcept;   // sweeps the phase break point
    void setCoefficient (double a) noexcept;             // -0.99..0.99 (dispersion amount)
    void setStages      (int numStages) noexcept;
    void setFeedback    (double feedback01) noexcept;    // 0 = pure allpass (flat)
    void reset          () noexcept;

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    double sampleRate_ = 44100.0;
    double a_          = 0.7;
    int    stages_     = 4;
    double feedback_   = 0.0;
    double fbState_    = 0.0;
    double x1_[kMaxStages] = { 0.0 };
    double y1_[kMaxStages] = { 0.0 };
};

} // namespace pdhybrid
