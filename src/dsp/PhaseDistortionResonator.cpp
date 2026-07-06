#include "PhaseDistortionResonator.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kPi    = 3.14159265358979323846;
static constexpr double kTwoPi = 6.283185307179586476925287;

// Same two-segment phase map as the PD oscillator.
static inline double distortPhase (double p, double amount) noexcept
{
    const double m = 0.5 - 0.49 * amount;
    if (p < m)
        return 0.5 * (p / m);
    return 0.5 + 0.5 * ((p - m) / (1.0 - m));
}

void PhaseDistortionResonator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateCoefficients();
    }
}

void PhaseDistortionResonator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    updateCoefficients();
}

void PhaseDistortionResonator::setResonance (double resonance01) noexcept
{
    if (resonance01 < 0.0) resonance01 = 0.0;
    if (resonance01 > 1.0) resonance01 = 1.0;
    resonance_ = resonance01;
    updateCoefficients();
}

void PhaseDistortionResonator::setAmount (double amount01) noexcept
{
    if (amount01 < 0.0) amount01 = 0.0;
    if (amount01 > 1.0) amount01 = 1.0;
    amount_ = amount01;
}

void PhaseDistortionResonator::reset() noexcept
{
    u_ = 0.0;
    v_ = 0.0;
}

void PhaseDistortionResonator::updateCoefficients() noexcept
{
    double fc = frequency_;
    const double nyquistGuard = 0.49 * sampleRate_;
    if (fc < 5.0)          fc = 5.0;
    if (fc > nyquistGuard) fc = nyquistGuard;

    const double theta = kTwoPi * fc / sampleRate_;
    cosT_ = std::cos (theta);
    sinT_ = std::sin (theta);

    // Pole radius from resonance: 0 -> 0.9 (short ring), 1 -> ~0.9999 (long).
    damp_ = 1.0 - std::pow (10.0, -(1.0 + 3.0 * resonance_));
    gainComp_ = 1.0 - damp_;   // normalise steady-state resonant gain to ~1
}

float PhaseDistortionResonator::processSample (float xin) noexcept
{
    const double x = static_cast<double> (xin);

    // Rotate the damped phasor and inject the input into the real part.
    const double un = damp_ * (u_ * cosT_ - v_ * sinT_) + x;
    const double vn = damp_ * (u_ * sinT_ + v_ * cosT_);
    u_ = un;
    v_ = vn;

    const double amp   = std::sqrt (u_ * u_ + v_ * v_);
    double       phase = std::atan2 (v_, u_) / kTwoPi;   // -0.5 .. 0.5
    if (phase < 0.0) phase += 1.0;                        // 0 .. 1

    const double warped = distortPhase (phase, amount_);
    const double y = amp * std::sin (kTwoPi * warped) * gainComp_;
    return static_cast<float> (y);
}

void PhaseDistortionResonator::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
