#include "Lfo.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

void Lfo::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        inc_ = frequency_ / sampleRate_;
    }
}

void Lfo::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    inc_ = frequency_ / sampleRate_;
}

double Lfo::compute (double phase) const noexcept
{
    switch (wave_)
    {
        case LfoWave::Triangle: return 4.0 * std::fabs (phase - 0.5) - 1.0;
        case LfoWave::Square:   return (phase < 0.5) ? 1.0 : -1.0;
        case LfoWave::Saw:      return 2.0 * phase - 1.0;
        case LfoWave::Sine:
        default:                return std::sin (kTwoPi * phase);
    }
}

double Lfo::value() const noexcept
{
    return compute (phase_);
}

double Lfo::processSample() noexcept
{
    const double v = compute (phase_);
    phase_ += inc_;
    if (phase_ >= 1.0) phase_ -= 1.0;
    return v;
}

void Lfo::advance (int numSamples) noexcept
{
    phase_ += inc_ * numSamples;
    phase_ -= std::floor (phase_);
}

} // namespace pdhybrid
