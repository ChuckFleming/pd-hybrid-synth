#include "MasterStage.h"
#include <cmath>

namespace pdhybrid {

void MasterStage::setSampleRate (double sampleRateHz) noexcept
{
    sampleRate_ = sampleRateHz > 0.0 ? sampleRateHz : 44100.0;
    // ~5 ms gain smoothing, sample-rate independent.
    gainCoef_ = 1.0 - std::exp (-1.0 / (0.005 * sampleRate_));
}

void MasterStage::reset() noexcept
{
    curGain_ = targetGain_;
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
        left[i]  = static_cast<float> (softClip (left[i]  * curGain_));
        right[i] = static_cast<float> (softClip (right[i] * curGain_));
    }
}

} // namespace pdhybrid
