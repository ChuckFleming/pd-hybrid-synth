#include "PafOscillator.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

void PafOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        phaseInc_ = frequency_ / sampleRate_;
    }
}

void PafOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    phaseInc_ = frequency_ / sampleRate_;
}

void PafOscillator::setFormant (double amount01) noexcept
{
    formant_ = std::clamp (amount01, 0.0, 1.0);
}

void PafOscillator::setBandwidth (double pulseWidth01) noexcept
{
    bandwidth_ = std::clamp (pulseWidth01, 0.0, 1.0);
}

void PafOscillator::setShape (double shape01) noexcept
{
    shape_ = std::clamp (shape01, 0.0, 1.0);
}

void PafOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void PafOscillator::reset() noexcept
{
    phase_ = 0.0;
    wrapped_ = false;
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

double PafOscillator::coreSample() noexcept
{
    double ph = phase_ + phaseMod_;
    ph -= std::floor (ph);

    // Formant centre as a multiple of the fundamental, exponential so the low
    // end -- where the ear hears the vowel change -- gets most of the knob.
    const double centre = std::pow (2.0, formant_ * 5.0);        // 1 .. 32

    // Split into the harmonic below and the fraction toward the next one. The
    // carrier is a blend of the two, which is what makes a formant sweep
    // continuous instead of stepping from harmonic to harmonic.
    const double kFloor = std::floor (centre);
    const double frac   = centre - kFloor;
    // `shape` biases that blend, so the same formant position can read as
    // cleanly locked (0 or 1) or as the fuller two-harmonic sound (0.5).
    const double blend  = std::clamp (frac + (shape_ - 0.5) * 0.5, 0.0, 1.0);

    const double carrierLo = std::cos (kTwoPi * kFloor * ph);
    const double carrierHi = std::cos (kTwoPi * (kFloor + 1.0) * ph);
    const double carrier   = (1.0 - blend) * carrierLo + blend * carrierHi;

    // Bell window, phase-locked to the same phasor. cos gives a raised cosine
    // over the period; raising it to a power narrows the bell without ever
    // introducing a discontinuity, so the spectrum stays a clean single peak.
    const double halfCos = 0.5 * (1.0 + std::cos (kTwoPi * (ph - 0.5)));
    const double sharp   = 1.0 + (1.0 - bandwidth_) * 24.0;      // 1 .. 25
    const double window  = std::pow (halfCos, sharp);

    phase_ += phaseInc_ / osFactor_;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        wrapped_ = true;
    }

    // Narrow windows carry much less energy per period; compensate so sweeping
    // the bandwidth does not double as a volume control.
    const double norm = std::sqrt (sharp);
    return carrier * window * norm * 0.7;
}

float PafOscillator::processSample() noexcept
{
    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void PafOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
