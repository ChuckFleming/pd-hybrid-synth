#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    Final master output stage: a smoothed output gain followed by an optional
    soft limiter. The limiter is transparent below its threshold and applies a
    smooth tanh knee above it, so the output asymptotes to a 1.0 ceiling instead
    of hard-clipping. The knee is applied 4x oversampled so that clipping bright,
    loud material (e.g. a dense chord of the pulsed VOSIM engine) does not fold
    its added harmonics down as aliased crackle. Gain is ramped (one-pole) to
    avoid zipper noise when the level control moves.

    Pure C++, no JUCE; measured directly by the offline harness.
*/
class MasterStage
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset () noexcept;

    void setGainDb          (double db) noexcept;                 // target output gain
    void setLimiterEnabled  (bool on) noexcept { limiterOn_ = on; }
    void setThreshold       (double linear) noexcept;             // knee start (0..1)

    float processSample (float x) noexcept;                       // mono (tests)
    void  processStereo (float* left, float* right, int numSamples) noexcept;

private:
    double softClip (double x) const noexcept;

    double sampleRate_ = 44100.0;
    double targetGain_ = 1.0;
    double curGain_    = 1.0;
    double gainCoef_   = 1.0;      // one-pole smoothing coefficient
    bool   limiterOn_  = true;
    double threshold_  = 0.9;

    Oversampler osL_, osR_;        // 4x anti-alias the soft-clip knee
    static constexpr int kOsFactor = 4;
};

} // namespace pdhybrid
