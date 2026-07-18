#include "MasterStage.h"
#include <cmath>

namespace pdhybrid {

void MasterStage::setSampleRate (double sampleRateHz) noexcept
{
    sampleRate_ = sampleRateHz > 0.0 ? sampleRateHz : 44100.0;
    // ~5 ms gain smoothing, sample-rate independent.
    gainCoef_ = 1.0 - std::exp (-1.0 / (0.005 * sampleRate_));
    osL_.prepare (kOsFactor);
    osR_.prepare (kOsFactor);
    osL_.reset();
    osR_.reset();
}

void MasterStage::reset() noexcept
{
    curGain_ = targetGain_;
    osL_.reset();
    osR_.reset();
}

void MasterStage::setGainDb (double db) noexcept
{
    targetGain_ = std::pow (10.0, db / 20.0);
}

void MasterStage::setThreshold (double linear) noexcept
{
    threshold_ = linear < 0.05 ? 0.05 : (linear > 0.99 ? 0.99 : linear);
}

double MasterStage::softClip (double x) const noexcept
{
    if (! limiterOn_)
        return x;

    const double a = std::fabs (x);
    if (a <= threshold_)
        return x;

    const double sign = x < 0.0 ? -1.0 : 1.0;
    const double over = (a - threshold_) / (1.0 - threshold_);
    return sign * (threshold_ + (1.0 - threshold_) * std::tanh (over));
}

float MasterStage::processSample (float x) noexcept
{
    curGain_ += gainCoef_ * (targetGain_ - curGain_);
    return static_cast<float> (softClip (x * curGain_));
}

void MasterStage::processStereo (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        curGain_ += gainCoef_ * (targetGain_ - curGain_);
        const float gl = static_cast<float> (left[i]  * curGain_);
        const float gr = static_cast<float> (right[i] * curGain_);

        if (! limiterOn_)   // transparent: no knee, no oversampling latency
        {
            left[i] = gl; right[i] = gr;
            continue;
        }

        // Apply the tanh knee at 4x so its harmonics don't alias back down.
        float hl[8], hr[8];
        osL_.upsample (gl, hl);
        osR_.upsample (gr, hr);
        for (int j = 0; j < kOsFactor; ++j)
        {
            hl[j] = static_cast<float> (softClip (hl[j]));
            hr[j] = static_cast<float> (softClip (hr[j]));
        }
        left[i]  = osL_.downsample (hl);
        right[i] = osR_.downsample (hr);
    }
}

} // namespace pdhybrid
