#include "GlobalEq.h"

namespace pdhybrid {

Biquad::Kind GlobalEq::kindFor (int band) noexcept
{
    switch (band)
    {
        case LowShelf:  return Biquad::Kind::LowShelf;
        case HighShelf: return Biquad::Kind::HighShelf;
        default:        return Biquad::Kind::Peaking;
    }
}

double GlobalEq::qFor (int band) noexcept
{
    return (band == LowShelf || band == HighShelf) ? 0.707 : 1.0;
}

void GlobalEq::design (int band) noexcept
{
    const auto kind = kindFor (band);
    const auto q    = qFor (band);
    l_[band].design (kind, freq_[band], q, gain_[band], sampleRate_);
    r_[band].design (kind, freq_[band], q, gain_[band], sampleRate_);
}

void GlobalEq::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        for (int b = 0; b < kNumBands; ++b)
            design (b);
    }
}

void GlobalEq::reset() noexcept
{
    for (int b = 0; b < kNumBands; ++b)
    {
        l_[b].reset();
        r_[b].reset();
    }
}

void GlobalEq::setBand (int band, double freqHz, double gainDb) noexcept
{
    if (band < 0 || band >= kNumBands)
        return;

    freq_[band] = freqHz;
    gain_[band] = gainDb;
    design (band);   // keeps the running state
}

float GlobalEq::processSample (float x) noexcept
{
    double y = x;
    for (int b = 0; b < kNumBands; ++b)
        y = l_[b].process (y);
    return static_cast<float> (y);
}

void GlobalEq::processStereo (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        double yl = left[i];
        double yr = right[i];
        for (int b = 0; b < kNumBands; ++b)
        {
            yl = l_[b].process (yl);
            yr = r_[b].process (yr);
        }
        left[i]  = static_cast<float> (yl);
        right[i] = static_cast<float> (yr);
    }
}

} // namespace pdhybrid
