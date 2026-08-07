#include "BandSplitFilter.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kPi = 3.14159265358979323846;

void BandSplitFilter::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateCoefficients();
    }
}

void BandSplitFilter::setFrequency (double frequencyHz) noexcept
{
    centreHz_ = std::clamp (frequencyHz, 60.0, 8000.0);
    updateCoefficients();
}

void BandSplitFilter::setResonance (double resonance01) noexcept
{
    resonance_ = std::clamp (resonance01, 0.0, 1.0);
    updateCoefficients();
}

void BandSplitFilter::setTilt (double tilt01) noexcept
{
    tilt_ = std::clamp (tilt01, 0.0, 1.0);
    updateCoefficients();
}

void BandSplitFilter::reset() noexcept
{
    for (auto& b : bands_)
    {
        b.ic1 = 0.0;
        b.ic2 = 0.0;
    }
}

void BandSplitFilter::updateCoefficients() noexcept
{
    // Bands span three octaves either side of the centre, logarithmically --
    // the same spacing a vocoder's filter bank uses.
    const double nyquistGuard = 0.45 * sampleRate_;
    const double q   = 2.0 + 10.0 * resonance_;
    wet_ = 0.3 + 0.7 * resonance_;

    // Tilt selects which part of the bank survives. The emphasis is a soft
    // window over band index so sweeping it drags a peak across the spectrum
    // rather than switching bands on and off.
    const double focus = tilt_ * (kNumBands - 1);
    const double width = 1.5 + 2.5 * (1.0 - std::abs (tilt_ - 0.5) * 2.0);

    double sum = 0.0;
    for (int i = 0; i < kNumBands; ++i)
    {
        const double oct = -3.0 + 6.0 * i / (kNumBands - 1);
        double hz = centreHz_ * std::pow (2.0, oct);
        hz = std::clamp (hz, 20.0, nyquistGuard);

        auto& b = bands_[i];
        b.g  = std::tan (kPi * hz / sampleRate_);
        b.k  = 1.0 / q;
        b.a1 = 1.0 / (1.0 + b.g * (b.g + b.k));
        b.a2 = b.g * b.a1;
        b.a3 = b.g * b.a2;

        const double d = (i - focus) / width;
        const double w = std::exp (-0.5 * d * d);
        // Bandpass peak gain is 1/k, so fold k back in to keep levels sane.
        b.gain = w * b.k;
        sum += w;
    }

    // Keep the summed output roughly level-independent of how many bands the
    // tilt currently favours.
    const double norm = sum > 1.0e-9 ? 1.0 / std::sqrt (sum) : 1.0;
    for (auto& b : bands_)
        b.gain *= norm;
}

float BandSplitFilter::processSample (float xin) noexcept
{
    const double v0 = static_cast<double> (xin);

    double wet = 0.0;
    for (auto& b : bands_)
    {
        const double v3 = v0 - b.ic2;
        const double v1 = b.a1 * b.ic1 + b.a2 * v3;
        const double v2 = b.ic2 + b.a2 * b.ic1 + b.a3 * v3;
        b.ic1 = 2.0 * v1 - b.ic1;
        b.ic2 = 2.0 * v2 - b.ic2;

        wet += v1 * b.gain;   // v1 is the bandpass output
    }

    return static_cast<float> ((1.0 - wet_) * v0 + wet_ * wet * 3.0);
}

void BandSplitFilter::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
