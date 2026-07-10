#pragma once

#include "Biquad.h"

namespace pdhybrid {

/**
    A compact 3-band tone EQ used per oscillator: a low shelf, a mid peaking
    band and a high shelf at fixed frequencies, each controlled by a single gain
    in dB. At 0 dB every band is exactly unity, so the EQ is transparent by
    default. Built from the shared RBJ-cookbook Biquad; pure C++, no JUCE.
*/
class OscEq
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset         () noexcept;

    // Band gains in dB (0 = flat).
    void setGains (double lowDb, double midDb, double highDb) noexcept;

    float processSample (float x) noexcept;

private:
    void redesign() noexcept;   // recompute coefficients + bypass flag

    double sampleRate_ = 44100.0;
    double lowDb_ = 0.0, midDb_ = 0.0, highDb_ = 0.0;
    bool   bypass_ = true;      // true when all bands are ~0 dB (the default)

    Biquad low_, mid_, high_;
};

} // namespace pdhybrid
