#include "PhaseDistortionResonator.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kPi    = 3.14159265358979323846;
static constexpr double kTwoPi = 6.283185307179586476925287;

// Same two-segment phase map as the PD oscillator.
static inline double distortPhase (double p, double amount) noexcept
{
    const double m = 0.5 - 0.49 * amount;
    if (p < m)
        return 0.5 * (p / m);
    return 0.5 + 0.5 * ((p - m) / (1.0 - m));
}

void PhaseDistortionResonator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateCoefficients();
    }
}

void PhaseDistortionResonator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    updateCoefficients();
}

void PhaseDistortionResonator::setResonance (double resonance01) noexcept
{
    resonance_ = std::clamp (resonance01, 0.0, 1.0);
    updateCoefficients();
}

void PhaseDistortionResonator::setAmount (double amount01) noexcept
{
    amount_ = std::clamp (amount01, 0.0, 1.0);
    updateCoefficients();
}

void PhaseDistortionResonator::setNoteFrequency (double noteHz) noexcept
{
    noteHz_ = noteHz > 0.0 ? noteHz : 0.0;
    updateCoefficients();
}

void PhaseDistortionResonator::reset() noexcept
{
    ic1_ = 0.0;
    ic2_ = 0.0;
    phase_ = 0.0;
    env_   = 0.0;
    fundPhase_ = 0.0;
}

void PhaseDistortionResonator::updateCoefficients() noexcept
{
    double fc = frequency_;
    const double nyquistGuard = 0.49 * sampleRate_;
    if (fc < 10.0)         fc = 10.0;
    if (fc > nyquistGuard) fc = nyquistGuard;

    g_ = std::tan (kPi * fc / sampleRate_);

    // Musical damping, matching StateVariableFilter: res 0 -> Q ~0.5, res 1 ->
    // Q ~25. The old mapping ran the pole radius to 0.9999 (a 1.5 Hz-wide
    // bandpass), which is why anything past a light touch of resonance made the
    // signal vanish.
    k_ = 2.0 * (1.0 - 0.98 * resonance_);
    if (k_ < 0.04) k_ = 0.04;

    a1_ = 1.0 / (1.0 + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;

    // The ring rides on top of the body, so its depth follows the resonance.
    ringGain_ = resonance_;

    phaseInc_ = fc / sampleRate_;

    // CZ sync mode: the ring runs at a whole-ish multiple of the fundamental
    // so the formant tracks the keyboard, and the fundamental drives the
    // once-per-period window.
    if (noteHz_ > 0.0)
    {
        fundInc_ = noteHz_ / sampleRate_;
    }
    else
    {
        fundInc_ = 0.0;
    }

    // Fade the warp out between fs/24 and fs/8: the distorted sine carries a
    // few dozen harmonics and the filter stage is not oversampled.
    const double aaLo = sampleRate_ / 24.0, aaHi = sampleRate_ / 8.0;
    const double aa = fc <= aaLo ? 1.0
                    : fc >= aaHi ? 0.0
                                 : (aaHi - fc) / (aaHi - aaLo);
    effAmount_ = amount_ * aa;

    // Window follower: quick attack, release slow enough not to flutter at the
    // resonant period but still to track note dynamics.
    const double atkS = 0.001;
    const double relS = std::max (0.005, 8.0 / fc);
    envAtk_ = std::exp (-1.0 / (atkS * sampleRate_));
    envRel_ = std::exp (-1.0 / (relS * sampleRate_));
}

float PhaseDistortionResonator::processSample (float xin) noexcept
{
    const double v0 = static_cast<double> (xin);

    // --- TPT state-variable core: body (lowpass) + excitation (bandpass) ---
    const double v3 = v0 - ic2_;
    const double v1 = a1_ * ic1_ + a2_ * v3;
    const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = 2.0 * v1 - ic1_;
    ic2_ = 2.0 * v2 - ic2_;

    const double lp = v2;
    // The bandpass carries its full 1/k peak gain, and that gain *is* the
    // resonant emphasis -- normalising it away (as an earlier revision did)
    // leaves the ring buried under the body and the filter sounds like a plain
    // lowpass. Matches the peak height an ordinary resonant SVF would give.
    const double bp = v1;

    const double mag = std::abs (bp);
    env_ = mag > env_ ? envAtk_ * env_ + (1.0 - envAtk_) * mag
                      : envRel_ * env_ + (1.0 - envRel_) * mag;

    double ring;
    if (fundInc_ > 0.0)
    {
        // --- CZ resonance: hard sync + once-per-period window ---
        // The fundamental's phase both resets the ring (so the formant sits at
        // a fixed ratio above the note and the sync edge buzzes) and provides
        // the falling window that makes each period decay into the next reset.
        fundPhase_ += fundInc_;
        if (fundPhase_ >= 1.0)
        {
            fundPhase_ -= 1.0;
            phase_ = 0.0;              // hard sync
        }

        phase_ += phaseInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;

        const double warped = distortPhase (phase_, effAmount_);
        const double window = 1.0 - fundPhase_;   // CZ's falling saw window
        ring = env_ * window * std::sin (kTwoPi * warped);
    }
    else
    {
        // --- Free-running ring at the resonant frequency ---
        // Deliberately not re-synced to the bandpass. Locking to its zero
        // crossings is clean while the input is periodic, but a broadband input
        // crosses zero irregularly, the phase jitters, and the warp then yields
        // noise instead of a harmonic series -- the failure the original design
        // had. The envelope is what ties the ring to the signal.
        phase_ += phaseInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;

        const double warped = distortPhase (phase_, effAmount_);
        ring = env_ * std::sin (kTwoPi * warped);
    }

    return static_cast<float> (lp + ringGain_ * ring);
}

void PhaseDistortionResonator::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
