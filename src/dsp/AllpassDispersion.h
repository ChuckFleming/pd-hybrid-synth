#pragma once

namespace pdhybrid {

/**
    Allpass dispersion filter: a cascade of first-order allpass sections. Each
    section has unity magnitude at every frequency but a frequency-dependent
    phase, so the cascade smears phase (disperses transients, adds glassy/
    metallic colour) without altering the magnitude spectrum. The defining,
    testable property is a flat 0 dB magnitude response.
*/
class AllpassDispersion
{
public:
    static constexpr int kMaxStages = 16;

    void setCoefficient (double a) noexcept;     // -0.99..0.99 (dispersion amount)
    void setStages      (int numStages) noexcept;
    void reset          () noexcept;

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    double a_      = 0.7;
    int    stages_ = 4;
    double x1_[kMaxStages] = { 0.0 };
    double y1_[kMaxStages] = { 0.0 };
};

} // namespace pdhybrid
