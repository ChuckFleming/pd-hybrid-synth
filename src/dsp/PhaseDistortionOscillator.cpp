#include "PhaseDistortionOscillator.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

void PhaseDistortionOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateIncrement();
    }
}

void PhaseDistortionOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    updateIncrement();
}

void PhaseDistortionOscillator::setAmount (double amount01) noexcept
{
    if (amount01 < 0.0) amount01 = 0.0;
    if (amount01 > 1.0) amount01 = 1.0;
    amount_ = amount01;
}

void PhaseDistortionOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void PhaseDistortionOscillator::reset() noexcept
{
    phase_ = 0.0;
    useB_  = false;
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

void PhaseDistortionOscillator::updateIncrement() noexcept
{
    phaseInc_ = frequency_ / sampleRate_;
}

// --- Phase-remap building blocks -------------------------------------------

// Two-segment CZ knee: maps [0,1) -> [0,1) with the break at `m`. As `m` slides
// from 0.5 (identity -> pure sine) toward ~0, the first half of the sine is
// compressed into an ever-shorter time and the harmonic series grows.
static inline double kneeMap (double x, double m) noexcept
{
    if (x < m)
        return 0.5 * (x / m);
    return 0.5 + 0.5 * ((x - m) / (1.0 - m));
}

// One resonant window over the cycle p in [0,1). Each window returns to 0 at
// p = 1 so the sync reset is click-free regardless of the (fractional) k.
static inline double resonantWindow (double p, PdWave wave) noexcept
{
    switch (wave)
    {
        case PdWave::ResonantI:   return 1.0 - p;                        // sawtooth (decay)
        case PdWave::ResonantII:  return 1.0 - std::fabs (2.0 * p - 1.0); // triangle (peak mid)
        case PdWave::ResonantIII:                                        // trapezoid
        {
            const double edge = 0.25;                 // rise/fall quarter-cycles
            if (p < edge)        return p / edge;
            if (p > 1.0 - edge)  return (1.0 - p) / edge;
            return 1.0;
        }
        default: return 1.0;
    }
}

double PhaseDistortionOscillator::coreSample() noexcept
{
    const double p = phase_;
    const double a = amount_;
    const PdWave w = (combine_ && useB_) ? waveB_ : wave_;   // alternate per cycle
    double y;

    switch (w)
    {
        case PdWave::Sawtooth:
        {
            const double m = 0.5 - 0.49 * a;
            y = std::sin (kTwoPi * kneeMap (p, m));
        } break;

        case PdWave::SawPulse:
        {
            // Sharper saw: apply the knee twice for a steeper first segment and
            // a brighter (more high-harmonic) spectrum than plain Sawtooth.
            const double m = 0.5 - 0.49 * a;
            y = std::sin (kTwoPi * kneeMap (kneeMap (p, m), m));
        } break;

        case PdWave::Square:
        {
            // Two phase plateaus (at the +1 and -1 sine peaks) that widen with
            // `amount`, so the tone morphs sine -> square.
            const double hf = 0.9 * a;         // total hold fraction
            const double w  = (1.0 - hf) / 3.0; // each of three transition ramps
            const double hd = hf * 0.5;         // each of two plateaus
            double g;
            if      (p < w)              g = 0.25 * (p / w);
            else if (p < w + hd)         g = 0.25;
            else if (p < 2.0 * w + hd)   g = 0.25 + 0.5 * ((p - (w + hd)) / w);
            else if (p < 2.0 * w + 2.0 * hd) g = 0.75;
            else                         g = 0.75 + 0.25 * ((p - (2.0 * w + 2.0 * hd)) / w);
            y = std::sin (kTwoPi * g);
        } break;

        case PdWave::Pulse:
        {
            // Single (top) plateau -> asymmetric, pulse-like.
            const double hold = 0.9 * a;
            const double w    = (1.0 - hold) / 3.0;
            double g;
            if      (p < w)          g = 0.25 * (p / w);
            else if (p < w + hold)   g = 0.25;
            else                     g = 0.25 + 0.75 * ((p - (w + hold)) / (1.0 - w - hold));
            y = std::sin (kTwoPi * g);
        } break;

        case PdWave::DoubleSine:
        {
            // A sine that speeds up toward two lobes as `amount` rises, kept
            // click-free by a Hann window that vanishes at the cycle edges.
            const double k   = 1.0 + a;                       // 1 -> 2 cycles
            const double win = 0.5 - 0.5 * std::cos (kTwoPi * p);
            y = win * std::sin (kTwoPi * k * p);
        } break;

        case PdWave::ResonantI:
        case PdWave::ResonantII:
        case PdWave::ResonantIII:
        {
            // Windowed sync: sine at k x fundamental, `amount` sweeps k.
            const double k = 1.0 + a * 7.0;                   // resonant multiple 1 -> 8
            y = resonantWindow (p, w) * std::sin (kTwoPi * k * p);
        } break;

        default:
            y = std::sin (kTwoPi * p);
            break;
    }

    phase_ += phaseInc_ / osFactor_;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        if (combine_)
            useB_ = ! useB_;   // swap waveforms on the next cycle (click-free at p=0)
    }

    return y;
}

float PhaseDistortionOscillator::processSample() noexcept
{
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void PhaseDistortionOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
