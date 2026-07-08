#include "OverdriveAmp.h"
#include <cmath>

namespace pdhybrid {

float OverdriveAmp::applyDcBlock (float in) noexcept
{
    const double x = static_cast<double> (in);
    const double y = x - dcX1_ + kDcR * dcY1_;
    dcX1_ = x;
    dcY1_ = y;
    return static_cast<float> (y);
}

float OverdriveAmp::processSample (float x) noexcept
{
    float y;

    if (os_.factor() == 1)
    {
        y = shaper_.process (x);
    }
    else
    {
        float hi[8];                       // factor is capped at 8
        os_.upsample (x, hi);
        const int n = os_.factor();
        for (int j = 0; j < n; ++j)
            hi[j] = shaper_.process (hi[j]);
        y = os_.downsample (hi);
    }

    if (dcBlock_)
        y = applyDcBlock (y);

    // Bit-depth reduction (quantise to 2^(bits-1) levels).
    if (crushBits_ < 16.0)
    {
        const double levels = std::pow (2.0, crushBits_ - 1.0);
        y = static_cast<float> (std::round (y * levels) / levels);
    }

    // Sample-rate reduction (hold each sample for `downsample_` frames).
    if (downsample_ > 1)
    {
        if (dsCounter_ == 0)
            dsHeld_ = y;
        y = dsHeld_;
        if (++dsCounter_ >= downsample_)
            dsCounter_ = 0;
    }

    return y;
}

void OverdriveAmp::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
