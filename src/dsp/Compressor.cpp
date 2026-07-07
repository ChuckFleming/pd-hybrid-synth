#include "Compressor.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static inline double timeToCoef (double seconds, double sr) noexcept
{
    if (seconds <= 0.0)
        return 0.0;
    return std::exp (-1.0 / (seconds * sr));
}

void Compressor::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        setAttack (0.005);
        setRelease (0.10);
    }
}

void Compressor::reset() noexcept
{
    gainDb_ = 0.0;
}

void Compressor::setRatio (double ratio) noexcept
{
    ratio_ = ratio < 1.0 ? 1.0 : ratio;
}

void Compressor::setAttack (double seconds) noexcept
{
    attackCoef_ = timeToCoef (seconds, sampleRate_);
}

void Compressor::setRelease (double seconds) noexcept
{
    releaseCoef_ = timeToCoef (seconds, sampleRate_);
}

// Soft-knee static curve: returns the gain change in dB (<= 0) for an input
// level in dB. The knee smooths the transition around the threshold.
double Compressor::staticCurveDb (double levelDb) const noexcept
{
    const double slope = 1.0 / ratio_ - 1.0;   // 0 at ratio 1, -1 as ratio -> inf
    const double over  = levelDb - thresholdDb_;

    if (kneeDb_ <= 0.0)
        return over > 0.0 ? slope * over : 0.0;

    if (2.0 * over < -kneeDb_)
        return 0.0;
    if (2.0 * over <= kneeDb_)
    {
        const double x = over + kneeDb_ * 0.5;
        return slope * x * x / (2.0 * kneeDb_);
    }
    return slope * over;
}

void Compressor::processStereo (float* left, float* right, int numSamples) noexcept
{
    const double makeupLin = std::pow (10.0, makeupDb_ / 20.0);

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = left[i];
        const double r = right[i];

        // Stereo-linked peak detector.
        const double level   = std::max (std::abs (l), std::abs (r));
        const double levelDb = 20.0 * std::log10 (level + 1.0e-9);

        const double targetDb = staticCurveDb (levelDb);   // <= 0

        // Smooth the gain: attack when reduction deepens, release when it eases.
        const double coef = (targetDb < gainDb_) ? attackCoef_ : releaseCoef_;
        gainDb_ = targetDb + coef * (gainDb_ - targetDb);

        const double gainLin = std::pow (10.0, gainDb_ / 20.0) * makeupLin;
        left[i]  = static_cast<float> (l * gainLin);
        right[i] = static_cast<float> (r * gainLin);
    }
}

} // namespace pdhybrid
