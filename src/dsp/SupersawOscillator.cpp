#include "SupersawOscillator.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

namespace {

// PolyBLEP residual: subtracts the aliasing step at a saw's discontinuity.
inline double polyBlep (double t, double dt) noexcept
{
    if (dt <= 0.0)
        return 0.0;

    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

} // namespace

void SupersawOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateIncrements();
    }
}

void SupersawOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    updateIncrements();
}

void SupersawOscillator::setDetune (double amount01) noexcept
{
    detune_ = std::clamp (amount01, 0.0, 1.0);
    updateIncrements();
}

void SupersawOscillator::setMix (double pulseWidth01) noexcept
{
    mix_ = std::clamp (pulseWidth01, 0.0, 1.0);
    updateIncrements();
}

void SupersawOscillator::setVoices (double voices01) noexcept
{
    // 3, 5, 7 or 9 -- always odd so there is a centre saw at the true pitch.
    const int steps = static_cast<int> (std::clamp (voices01, 0.0, 1.0) * 3.0 + 0.5);
    const int v = 3 + 2 * steps;
    if (v != voices_)
    {
        voices_ = v;
        updateIncrements();
    }
}

void SupersawOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void SupersawOscillator::reset() noexcept
{
    resetPhases();
    wrapped_ = false;
    updateIncrements();
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

void SupersawOscillator::syncReset() noexcept
{
    resetPhases();
}

void SupersawOscillator::resetPhases() noexcept
{
    // All saws start coherent. Spreading the start phases evenly instead is
    // tempting -- it softens the onset -- but N saws at one frequency spaced
    // 1/N of a cycle apart cancel every harmonic that is not a multiple of N,
    // so a zero-detune stack would ring an octave-and-a-bit up instead of
    // sounding like the single saw it is meant to be. The detune spread is what
    // pulls them apart; from there they drift on their own.
    for (int i = 0; i < kMaxVoices; ++i)
        phase_[i] = 0.0;
}

void SupersawOscillator::updateIncrements() noexcept
{
    const int    half     = voices_ / 2;             // saws either side of centre
    const double maxCents = 50.0 * detune_;          // up to a quarter tone out
    const double nyquist  = 0.49 * sampleRate_;

    double sum = 0.0;
    for (int i = 0; i < voices_; ++i)
    {
        const int    offset = i - half;              // -half .. +half
        const double cents  = half > 0 ? (static_cast<double> (offset) / half) * maxCents
                                       : 0.0;
        double f = frequency_ * std::pow (2.0, cents / 1200.0);
        f = std::clamp (f, 0.0, nyquist);

        inc_[i]  = f / sampleRate_;
        gain_[i] = (offset == 0) ? 1.0 : mix_;
        sum += gain_[i];
    }
    for (int i = voices_; i < kMaxVoices; ++i)
    {
        inc_[i]  = 0.0;
        gain_[i] = 0.0;
    }

    norm_ = sum > 1.0e-9 ? 1.0 / sum : 1.0;
}

double SupersawOscillator::coreSample() noexcept
{
    double out = 0.0;
    const int half = voices_ / 2;

    for (int i = 0; i < voices_; ++i)
    {
        const double dt = inc_[i] / osFactor_;

        double ph = phase_[i] + phaseMod_;
        ph -= std::floor (ph);

        // Naive saw in -1..1, minus the PolyBLEP correction at the wrap.
        double s = 2.0 * ph - 1.0;
        s -= polyBlep (ph, dt);

        out += s * gain_[i];

        phase_[i] += dt;
        if (phase_[i] >= 1.0)
        {
            phase_[i] -= 1.0;
            if (i == half)
                wrapped_ = true;   // the centre saw defines the cycle for hard sync
        }
    }

    return out * norm_;
}

float SupersawOscillator::processSample() noexcept
{
    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void SupersawOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
