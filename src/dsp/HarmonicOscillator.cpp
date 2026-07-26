#include "HarmonicOscillator.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

void HarmonicOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        phaseInc_ = frequency_ / sampleRate_;
        rebuildIfNeeded();
    }
}

void HarmonicOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    phaseInc_ = frequency_ / sampleRate_;

    // Highest partial that still fits below Nyquist. This is what keeps the
    // engine alias-free: the table is rebuilt whenever the ceiling moves.
    const double top = 0.45 * sampleRate_;
    const int    c   = frequency_ > 1.0 ? static_cast<int> (top / frequency_) : kMaxHarmonic;
    ceiling_ = std::clamp (c, 1, kMaxHarmonic);
    rebuildIfNeeded();
}

void HarmonicOscillator::setCentroid (double amount01) noexcept
{
    centroid_ = std::clamp (amount01, 0.0, 1.0);
    rebuildIfNeeded();
}

void HarmonicOscillator::setOddEven (double pulseWidth01) noexcept
{
    oddEven_ = std::clamp (pulseWidth01, 0.0, 1.0);
    rebuildIfNeeded();
}

void HarmonicOscillator::setWidth (double width01) noexcept
{
    width_ = std::clamp (width01, 0.0, 1.0);
    rebuildIfNeeded();
}

void HarmonicOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void HarmonicOscillator::reset() noexcept
{
    phase_ = 0.0;
    wrapped_ = false;
    rebuildTable();
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

void HarmonicOscillator::rebuildIfNeeded() noexcept
{
    if (centroid_ != centroidBuilt_ || oddEven_ != oddEvenBuilt_
        || width_ != widthBuilt_ || ceiling_ != ceilingBuilt_)
        rebuildTable();
}

void HarmonicOscillator::rebuildTable() noexcept
{
    double acc[kTableLen] = { 0.0 };

    // Window centre in harmonic numbers, exponential so the low harmonics --
    // where the ear hears the biggest timbral change -- get most of the knob.
    const double centre = std::pow (2.0, centroid_ * 5.0);          // 1 .. 32
    // Width in harmonics: narrow enough for a near-sine, wide enough to cover
    // the whole series.
    const double sigma  = 0.5 + width_ * width_ * 24.0;

    const double evenScale = 0.15 + 0.85 * oddEven_;
    const double oddScale  = 0.15 + 0.85 * (1.0 - oddEven_);

    double amp[kMaxHarmonic + 1] = { 0.0 };
    for (int k = 1; k <= ceiling_; ++k)
    {
        const double d = (static_cast<double> (k) - centre) / sigma;
        double a = std::exp (-0.5 * d * d);
        a *= (k % 2 == 0) ? evenScale : oddScale;
        // Gentle 1/sqrt(k) tilt: without it a high, wide window is piercing.
        a /= std::sqrt (static_cast<double> (k));
        amp[k] = a;
    }

    for (int k = 1; k <= ceiling_; ++k)
    {
        if (amp[k] < 1.0e-4)
            continue;
        const double w = kTwoPi * k / kTableLen;
        for (int n = 0; n < kTableLen; ++n)
            acc[n] += amp[k] * std::sin (w * n);
    }

    double peak = 0.0;
    for (int n = 0; n < kTableLen; ++n)
        peak = std::max (peak, std::abs (acc[n]));
    const double g = peak > 1.0e-9 ? 0.9 / peak : 1.0;
    for (int n = 0; n < kTableLen; ++n)
        table_[n] = static_cast<float> (acc[n] * g);

    centroidBuilt_ = centroid_;
    oddEvenBuilt_  = oddEven_;
    widthBuilt_    = width_;
    ceilingBuilt_  = ceiling_;
}

double HarmonicOscillator::coreSample() noexcept
{
    double ph = phase_ + phaseMod_;
    ph -= std::floor (ph);

    const double pos = ph * kTableLen;
    int i0 = static_cast<int> (pos);
    if (i0 >= kTableLen) i0 -= kTableLen;
    const int i1 = (i0 + 1 == kTableLen) ? 0 : i0 + 1;
    const double frac = pos - std::floor (pos);
    const double y = table_[i0] + frac * (table_[i1] - table_[i0]);

    phase_ += phaseInc_ / osFactor_;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        wrapped_ = true;
    }
    return y;
}

float HarmonicOscillator::processSample() noexcept
{
    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void HarmonicOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
