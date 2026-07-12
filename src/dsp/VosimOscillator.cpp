#include "VosimOscillator.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kPi   = 3.14159265358979323846;
static constexpr double kDcR  = 0.999;   // DC-blocker pole (~7.6 Hz, settles fast per note)

void VosimOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        phaseInc_ = frequency_ / sampleRate_;
        recompute();
    }
}

void VosimOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    phaseInc_ = frequency_ / sampleRate_;
    recompute();
}

void VosimOscillator::setFormant (double amount01) noexcept
{
    amount01 = std::clamp (amount01, 0.0, 1.0);
    // Log map across a vocal formant range: ~200 Hz .. ~4.5 kHz.
    formantHz_ = 200.0 * std::pow (2.0, amount01 * 4.5);
    recompute();
}

void VosimOscillator::setDecay (double pulseWidth01) noexcept
{
    pulseWidth01 = std::clamp (pulseWidth01, 0.0, 1.0);
    decay_ = 0.5 + 0.49 * pulseWidth01;   // 0.5 (broad) .. ~0.99 (narrow formant)
    recompute();
}

void VosimOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void VosimOscillator::reset() noexcept
{
    phase_ = 0.0;
    dcPrevIn_ = 0.0;
    dcPrevOut_ = 0.0;
    wrapped_ = false;
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

void VosimOscillator::recompute() noexcept
{
    periodSec_ = 1.0 / std::max (frequency_, 1.0e-6);
    pulseDur_  = 1.0 / std::max (formantHz_, 1.0e-6);
    // A pulse can't be longer than the period (formant >= fundamental).
    if (pulseDur_ > periodSec_) pulseDur_ = periodSec_;

    const int maxFit = std::max (1, (int) std::floor (periodSec_ / pulseDur_));
    numPulses_ = std::clamp (kTargetPulses, 1, std::min (maxFit, kMaxPulses));

    double a = 1.0;
    for (int i = 0; i < numPulses_; ++i) { ampTable_[i] = a; a *= decay_; }
}

double VosimOscillator::coreSample() noexcept
{
    double ph = phase_ + phaseMod_;
    ph -= std::floor (ph);

    const double t   = ph * periodSec_;          // seconds into the period
    const int    idx = (int) (t / pulseDur_);    // which pulse (or past the burst)
    double y = 0.0;
    if (idx < numPulses_)
    {
        const double local = (t - idx * pulseDur_) / pulseDur_;   // [0,1)
        const double s = std::sin (kPi * local);
        y = ampTable_[idx] * s * s;                               // sin^2 pulse
    }

    phase_ += phaseInc_ / osFactor_;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        wrapped_ = true;
    }
    return y;
}

float VosimOscillator::processSample() noexcept
{
    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    const double raw = os_.downsample (high);

    // One-pole DC blocker (the unipolar pulses carry a large offset).
    const double out = raw - dcPrevIn_ + kDcR * dcPrevOut_;
    dcPrevIn_  = raw;
    dcPrevOut_ = out;
    return static_cast<float> (out);
}

void VosimOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
