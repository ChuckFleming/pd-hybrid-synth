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

void Lfo::reset() noexcept
{
    phase_    = 0.0;
    rng_      = 0x1234567u;
    randCurr_ = nextRandom();
    randNext_ = nextRandom();
}

double Lfo::nextRandom() noexcept
{
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<double> (static_cast<std::int32_t> (rng_)) / 2147483648.0;
}

void Lfo::onCycleWrap() noexcept
{
    randCurr_ = randNext_;
    randNext_ = nextRandom();
}

double Lfo::compute (double phase) const noexcept
{
    switch (wave_)
    {
        case LfoWave::Triangle:     return 4.0 * std::fabs (phase - 0.5) - 1.0;
        case LfoWave::Square:       return (phase < 0.5) ? 1.0 : -1.0;
        case LfoWave::Saw:          return 2.0 * phase - 1.0;
        case LfoWave::RampDown:     return 1.0 - 2.0 * phase;
        case LfoWave::SampleHold:   return randCurr_;
        case LfoWave::SmoothRandom: return randCurr_ + (randNext_ - randCurr_) * phase;
        case LfoWave::Exponential:  return 2.0 * std::exp (-3.0 * phase) - 1.0;
        case LfoWave::Sine:
        default:                    return std::sin (kTwoPi * phase);
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
    if (phase_ >= 1.0) { phase_ -= 1.0; onCycleWrap(); }
    return v;
}

void Lfo::advance (int numSamples) noexcept
{
    phase_ += inc_ * numSamples;
    if (phase_ >= 1.0)
    {
        phase_ -= std::floor (phase_);
        onCycleWrap();
    }
}

} // namespace pdhybrid
