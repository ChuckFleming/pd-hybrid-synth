#include "PhaseDistortionOscillator.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

void PhaseDistortionOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateIncrement();
    }
}

void PhaseDistortionOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    updateIncrement();
}

void PhaseDistortionOscillator::setAmount (double amount01) noexcept
{
    if (amount01 < 0.0) amount01 = 0.0;
    if (amount01 > 1.0) amount01 = 1.0;
    amount_ = amount01;
}

void PhaseDistortionOscillator::reset() noexcept
{
    phase_ = 0.0;
}

void PhaseDistortionOscillator::updateIncrement() noexcept
{
    phaseInc_ = frequency_ / sampleRate_;
}

// Two-segment CZ-style phase map.
// The knee position `m` slides from 0.5 (identity -> pure sine) toward ~0.01
// (heavy distortion). [0, m] maps to the first half of the sine, [m, 1] to the
// second half, so as m shrinks the first half-cycle happens ever faster.
static inline double distortPhase (double p, double amount) noexcept
{
    const double m = 0.5 - 0.49 * amount;
    if (p < m)
        return 0.5 * (p / m);
    return 0.5 + 0.5 * ((p - m) / (1.0 - m));
}

float PhaseDistortionOscillator::processSample() noexcept
{
    const double warped = distortPhase (phase_, amount_);
    const double y      = std::sin (kTwoPi * warped);

    phase_ += phaseInc_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;

    return static_cast<float> (y);
}

void PhaseDistortionOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
