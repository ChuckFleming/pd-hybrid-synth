#include "StateVariableFilter.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kPi = 3.14159265358979323846;

void StateVariableFilter::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateCoefficients();
    }
}

void StateVariableFilter::setCutoff (double cutoffHz) noexcept
{
    cutoff_ = cutoffHz;
    updateCoefficients();
}

void StateVariableFilter::setResonance (double resonance01) noexcept
{
    if (resonance01 < 0.0) resonance01 = 0.0;
    if (resonance01 > 1.0) resonance01 = 1.0;
    resonance_ = resonance01;
    updateCoefficients();
}

void StateVariableFilter::reset() noexcept
{
    ic1_ = 0.0;
    ic2_ = 0.0;
}

void StateVariableFilter::updateCoefficients() noexcept
{
    double fc = cutoff_;
    const double nyquistGuard = 0.49 * sampleRate_;
    if (fc < 10.0)         fc = 10.0;
    if (fc > nyquistGuard) fc = nyquistGuard;

    g_ = std::tan (kPi * fc / sampleRate_);
    k_ = 2.0 * (1.0 - 0.98 * resonance_);   // damping: r=0 -> 2 (Q~0.5), r=1 -> 0.04 (high Q)
    if (k_ < 0.02) k_ = 0.02;

    a1_ = 1.0 / (1.0 + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;
}

float StateVariableFilter::processSample (float xin) noexcept
{
    const double v0 = static_cast<double> (xin);

    const double v3 = v0 - ic2_;
    const double v1 = a1_ * ic1_ + a2_ * v3;
    const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;

    ic1_ = 2.0 * v1 - ic1_;
    ic2_ = 2.0 * v2 - ic2_;

    const double lp = v2;
    const double bp = v1;
    const double hp = v0 - k_ * v1 - v2;
    const double notch = v0 - k_ * v1;   // lp + hp

    double out;
    if (useMorph_)
    {
        // Crossfade LP -> BP -> HP as morph goes 0 -> 0.5 -> 1.
        if (morph_ < 0.5)
        {
            const double t = morph_ * 2.0;
            out = (1.0 - t) * lp + t * bp;
        }
        else
        {
            const double t = (morph_ - 0.5) * 2.0;
            out = (1.0 - t) * bp + t * hp;
        }
    }
    else
    {
        switch (mode_)
        {
            case SvfMode::Bandpass: out = bp;    break;
            case SvfMode::Highpass: out = hp;    break;
            case SvfMode::Notch:    out = notch; break;
            case SvfMode::Lowpass:
            default:                out = lp;    break;
        }
    }

    return static_cast<float> (out);
}

void StateVariableFilter::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
