#include "VpsOscillator.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

void VpsOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateIncrement();
    }
}

void VpsOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    updateIncrement();
}

void VpsOscillator::setHorizontal (double d01) noexcept
{
    // Keep the inflection strictly inside (0,1) so neither segment collapses.
    if (d01 < 0.02) d01 = 0.02;
    if (d01 > 0.98) d01 = 0.98;
    d_ = d01;
}

void VpsOscillator::setVertical (double v) noexcept
{
    if (v < 0.0) v = 0.0;
    if (v > 8.0) v = 8.0;   // guard the phase slope (v/d) from running away
    v_ = v;
}

void VpsOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void VpsOscillator::reset() noexcept
{
    phase_ = 0.0;
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

void VpsOscillator::updateIncrement() noexcept
{
    phaseInc_ = frequency_ / sampleRate_;
}

double VpsOscillator::coreSample() noexcept
{
    double p = phase_ + phaseMod_;
    p -= std::floor (p);

    // Bend the phase through the inflection point (d_, v_), then read a cosine.
    const double f = (p < d_) ? (v_ / d_) * p
                              : v_ + ((1.0 - v_) / (1.0 - d_)) * (p - d_);
    const double y = std::cos (kTwoPi * f);

    phase_ += phaseInc_ / osFactor_;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        wrapped_ = true;
    }
    return y;
}

float VpsOscillator::processSample() noexcept
{
    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void VpsOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
