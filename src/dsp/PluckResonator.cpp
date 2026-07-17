#include "PluckResonator.h"
#include <algorithm>
#include <cmath>

namespace pdhybrid {

void PluckResonator::setSampleRate (double sampleRateHz)
{
    if (sampleRateHz <= 0.0)
        return;
    sampleRate_ = sampleRateHz;
    comb_.setSampleRate (sampleRateHz);
    updateBurstSamples();
    reset();
}

void PluckResonator::setFrequency (double frequencyHz) noexcept
{
    comb_.setFrequency (frequencyHz);
}

void PluckResonator::setDecay (double decay01) noexcept
{
    decay01 = std::clamp (decay01, 0.0, 1.0);
    feedback_ = 0.85 + 0.145 * decay01;   // 0.85 .. 0.995 (short .. long ring)
    comb_.setFeedback (feedback_);
}

void PluckResonator::setDamping (double damping01) noexcept
{
    damping01 = std::clamp (damping01, 0.0, 1.0);
    comb_.setDamping (0.1 + 0.85 * damping01);   // brighter/longer .. darker/shorter
}

void PluckResonator::setDispersion (double dispersion01) noexcept
{
    dispersion_ = std::clamp (dispersion01, 0.0, 1.0);
    allpass_.setStages (6);
    allpass_.setCoefficient (dispersion_ * 0.7);
}

void PluckResonator::setBurstMs (double milliseconds) noexcept
{
    burstMs_ = std::clamp (milliseconds, 0.5, 50.0);
    updateBurstSamples();
}

void PluckResonator::updateBurstSamples() noexcept
{
    burstSamples_ = std::max (1, (int) (burstMs_ * 0.001 * sampleRate_));
    fadeSamples_  = std::max (1, std::min (burstSamples_ / 2, (int) (0.002 * sampleRate_)));
}

void PluckResonator::reset() noexcept
{
    comb_.reset();
    allpass_.reset();
    burstLeft_ = 0;
}

void PluckResonator::trigger() noexcept
{
    comb_.reset();       // clear the string so each note is a fresh pluck
    allpass_.reset();
    burstLeft_ = burstSamples_;
}

float PluckResonator::processSample (float exciter) noexcept
{
    double in = 0.0;
    if (burstLeft_ > 0)
    {
        // Fade the exciter in and out so injecting/removing it is click-free, and
        // scale by (1 - feedback) so the comb can't resonate above unity gain.
        const int elapsed = burstSamples_ - burstLeft_;
        double g = 1.0;
        if (elapsed    < fadeSamples_) g = std::min (g, (double) elapsed    / fadeSamples_);
        if (burstLeft_ < fadeSamples_) g = std::min (g, (double) burstLeft_ / fadeSamples_);
        in = exciter * g * (1.0 - feedback_);
        --burstLeft_;
    }

    float out = comb_.processSample (static_cast<float> (in));
    if (dispersion_ > 1.0e-4)
        out = allpass_.processSample (out);
    return out;
}

} // namespace pdhybrid
