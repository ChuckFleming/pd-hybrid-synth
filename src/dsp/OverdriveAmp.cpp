#include "OverdriveAmp.h"

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

    return dcBlock_ ? applyDcBlock (y) : y;
}

void OverdriveAmp::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
