#include "ScannedOscillator.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kPi        = 3.14159265358979323846;
static constexpr double kCentering = 0.002;   // light spring pulling each mass to rest
static constexpr double kDtStep    = 0.15;    // integration timestep (slows the morph)
static constexpr double kUpdateHz  = 1500.0;  // physics updates per second (haptic rate)

void ScannedOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateIncrement();
        // Keep the physics step rate ~constant in Hz regardless of sample rate so
        // the morph speed is the same everywhere.
        updatePeriod_ = std::max (1, (int) std::lround (sampleRate_ / kUpdateHz));
    }
}

void ScannedOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    updateIncrement();
}

void ScannedOscillator::setStiffness (double stiffness01) noexcept
{
    stiffness01 = std::clamp (stiffness01, 0.0, 1.0);
    // Kept well below the symplectic-Euler stability limit for the top mode.
    stiffness_ = 0.02 + 0.38 * stiffness01;
}

void ScannedOscillator::setDamping (double damping01) noexcept
{
    damping01 = std::clamp (damping01, 0.0, 1.0);
    damping_ = damping01 * 0.05;
}

void ScannedOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void ScannedOscillator::reset() noexcept
{
    for (int i = 0; i < kNumMasses; ++i) { y_[i] = 0.0; v_[i] = 0.0; }
    phase_ = 0.0;
    updateCounter_ = 0;
    wrapped_ = false;
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

void ScannedOscillator::excite() noexcept
{
    // Raised-cosine "pluck" spanning half the ring: wide enough that the
    // fundamental dominates (so the pitch matches the note rather than its
    // octave), yet still exciting several modes so the shape morphs. Placed away
    // from the ring seam so no wrap-around is needed.
    const int center = kNumMasses / 4;
    const int half   = kNumMasses / 4;
    for (int i = 0; i < kNumMasses; ++i)
    {
        const int d = std::abs (i - center);
        y_[i] = (d < half) ? 0.5 * (1.0 + std::cos (kPi * d / half)) : 0.0;
        v_[i] = 0.0;
    }

    // Remove the DC component so the scanned wave has no offset.
    double mean = 0.0;
    for (int i = 0; i < kNumMasses; ++i) mean += y_[i];
    mean /= kNumMasses;
    double peak = 0.0;
    for (int i = 0; i < kNumMasses; ++i)
    {
        y_[i] -= mean;
        peak = std::max (peak, std::abs (y_[i]));
    }
    if (peak > 1.0e-9)
    {
        const double s = 0.7 / peak;   // headroom for constructive interference
        for (int i = 0; i < kNumMasses; ++i) y_[i] *= s;
    }

    phase_ = 0.0;
    updateCounter_ = 0;
}

void ScannedOscillator::updateIncrement() noexcept
{
    phaseInc_ = frequency_ / sampleRate_;
}

void ScannedOscillator::updatePhysics() noexcept
{
    // Symplectic (semi-implicit) Euler on the ring: update every velocity from the
    // current positions, then advance the positions. Unconditionally stable for
    // the stiffness range above.
    const double k = stiffness_, c = kCentering, d = damping_, dt = kDtStep;
    for (int i = 0; i < kNumMasses; ++i)
    {
        const int im = (i == 0) ? kNumMasses - 1 : i - 1;
        const int ip = (i == kNumMasses - 1) ? 0 : i + 1;
        const double lap = y_[im] + y_[ip] - 2.0 * y_[i];
        const double a   = k * lap - c * y_[i] - d * v_[i];
        v_[i] += a * dt;
    }
    for (int i = 0; i < kNumMasses; ++i)
        y_[i] += v_[i] * dt;
}

double ScannedOscillator::coreSample() noexcept
{
    double ph = phase_ + phaseMod_;
    ph -= std::floor (ph);

    const double pos = ph * kNumMasses;
    int i0 = (int) pos;
    if (i0 >= kNumMasses) i0 -= kNumMasses;
    const int i1 = (i0 + 1 == kNumMasses) ? 0 : i0 + 1;
    const double frac = pos - std::floor (pos);
    const double y = y_[i0] + frac * (y_[i1] - y_[i0]);

    phase_ += phaseInc_ / osFactor_;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        wrapped_ = true;
    }
    return y;
}

float ScannedOscillator::processSample() noexcept
{
    if (--updateCounter_ <= 0)
    {
        updatePhysics();
        updateCounter_ = updatePeriod_;
    }

    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void ScannedOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
