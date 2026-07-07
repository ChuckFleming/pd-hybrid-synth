#include "AnalogOscillator.h"

namespace pdhybrid {

// PolyBLEP residual: corrects a unit step's aliasing near a discontinuity.
// t is the phase (0..1), dt the per-sample phase increment.
static inline double polyBLEP (double t, double dt) noexcept
{
    if (t < dt)                 // just after a discontinuity
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)           // just before the next one
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

void AnalogOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        inc_ = frequency_ / sampleRate_;
    }
}

void AnalogOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    inc_ = frequency_ / sampleRate_;
}

void AnalogOscillator::setPulseWidth (double pulseWidth01) noexcept
{
    if (pulseWidth01 < 0.05) pulseWidth01 = 0.05;
    if (pulseWidth01 > 0.95) pulseWidth01 = 0.95;
    pulseWidth_ = pulseWidth01;
}

void AnalogOscillator::reset() noexcept
{
    phase_    = 0.0;
    triState_ = 0.0;
}

double AnalogOscillator::squareValue (double pulseWidth) const noexcept
{
    double v = (phase_ < pulseWidth) ? 1.0 : -1.0;
    v += polyBLEP (phase_, inc_);                          // rising edge at 0
    double t = phase_ - pulseWidth;
    if (t < 0.0) t += 1.0;
    v -= polyBLEP (t, inc_);                               // falling edge at pw
    return v;
}

float AnalogOscillator::processSample() noexcept
{
    const double dt = inc_;
    double out = 0.0;

    switch (wave_)
    {
        case AnalogWave::Saw:
            out = (2.0 * phase_ - 1.0) - polyBLEP (phase_, dt);
            break;

        case AnalogWave::Square:
            out = squareValue (0.5);
            break;

        case AnalogWave::Pulse:
            out = squareValue (pulseWidth_);
            break;

        case AnalogWave::Triangle:
        {
            const double sq = squareValue (0.5);
            // Leaky integration of the band-limited square yields a triangle.
            triState_ = 0.9995 * triState_ + 4.0 * dt * sq;
            out = triState_;
            break;
        }
    }

    phase_ += inc_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;

    return static_cast<float> (out);
}

void AnalogOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
