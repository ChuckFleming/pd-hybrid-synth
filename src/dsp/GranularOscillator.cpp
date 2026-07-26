#include "GranularOscillator.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

namespace {

// Up to kMaxGrains grains are alive at once and each is oversampled, so this
// engine asks for ~32 sines per output sample -- by far the most of any engine
// here. A shared table keeps the worst-case block comfortably inside budget;
// at 4096 points with interpolation the error is far below the noise floor of
// a grain cloud.
constexpr int kSinTableLen = 4096;

const float* sineTable()
{
    static const std::vector<float> table = []
    {
        std::vector<float> t (kSinTableLen + 1);
        for (int i = 0; i <= kSinTableLen; ++i)
            t[static_cast<std::size_t> (i)] =
                static_cast<float> (std::sin (kTwoPi * i / kSinTableLen));
        return t;
    }();
    return table.data();
}

// `ph` in 0..1.
inline double fastSin (double ph) noexcept
{
    const double pos = ph * kSinTableLen;
    const int    i0  = static_cast<int> (pos) & (kSinTableLen - 1);
    const double fr  = pos - std::floor (pos);
    const float* t   = sineTable();
    return t[i0] + fr * (t[i0 + 1] - t[i0]);
}

} // namespace

void GranularOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        phaseInc_ = frequency_ / sampleRate_;
    }
}

void GranularOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    phaseInc_ = frequency_ / sampleRate_;
}

void GranularOscillator::setScatter (double amount01) noexcept
{
    scatter_ = std::clamp (amount01, 0.0, 1.0);
}

void GranularOscillator::setSize (double pulseWidth01) noexcept
{
    size_ = std::clamp (pulseWidth01, 0.0, 1.0);
}

void GranularOscillator::setDensity (double density01) noexcept
{
    density_ = std::clamp (density01, 0.0, 1.0);
}

void GranularOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void GranularOscillator::reset() noexcept
{
    phase_ = 0.0;
    fundPhase_ = 0.0;
    wrapped_ = false;
    nextSlot_ = 0;
    // Fixed seed: the same patch must render identically on every note-on, and
    // the offline harness needs the engine to be deterministic.
    rng_ = 0x9E3779B9u;
    for (auto& g : grains_)
        g = Grain {};

    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

void GranularOscillator::syncReset() noexcept
{
    phase_ = 0.0;
    fundPhase_ = 0.0;
}

double GranularOscillator::nextRandom() noexcept
{
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<double> (static_cast<std::int32_t> (rng_)) / 2147483648.0;
}

void GranularOscillator::launchGrain (int slot) noexcept
{
    auto& g = grains_[slot];

    // Pitch scatter: up to +/- an octave at full, biased small so moderate
    // settings shimmer rather than smear.
    const double cents = nextRandom() * scatter_ * scatter_ * 1200.0;
    const double f = frequency_ * std::pow (2.0, cents / 1200.0);

    // Grain length as a multiple of the period. Longer grains overlap, which is
    // what turns the particle stream into a pad.
    const double periods = 0.25 + size_ * 3.75;
    const double lenSamples = std::max (8.0, periods * sampleRate_
                                             / std::max (1.0, frequency_));

    g.active    = true;
    g.phase     = 0.0;
    g.inc       = f / sampleRate_;
    g.windowPos = 0.0;
    g.windowInc = 1.0 / lenSamples;
    // Amplitude scatter too, so grains do not all punch equally.
    g.gain      = 1.0 - 0.5 * scatter_ * std::abs (nextRandom());
}

double GranularOscillator::coreSample() noexcept
{
    // Grain clock: fire on the fundamental's period, subdivided by density so
    // higher density overlaps more grains within the same pitch.
    const int perPeriod = 1 + static_cast<int> (density_ * (kMaxGrains - 1) + 0.5);
    const double tick = phaseInc_ / osFactor_ * perPeriod;

    phase_ += tick;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        launchGrain (nextSlot_);
        nextSlot_ = (nextSlot_ + 1) % kMaxGrains;
    }

    // The fundamental still defines the cycle for hard sync, independent of the
    // grain clock above.
    fundPhase_ += phaseInc_ / osFactor_;
    if (fundPhase_ >= 1.0)
    {
        fundPhase_ -= 1.0;
        wrapped_ = true;
    }

    double out = 0.0;
    for (auto& g : grains_)
    {
        if (! g.active)
            continue;

        // Raised-cosine grain envelope: starts and ends at exactly zero, so no
        // grain can click however short it is.
        const double w = 0.5 * (1.0 - fastSin (g.windowPos + 0.25));   // cos via sin

        double ph = g.phase + phaseMod_;
        ph -= std::floor (ph);
        out += fastSin (ph) * w * g.gain;

        g.phase += g.inc / osFactor_;
        if (g.phase >= 1.0) g.phase -= 1.0;

        g.windowPos += g.windowInc / osFactor_;
        if (g.windowPos >= 1.0)
            g.active = false;
    }

    // Normalise by the number of grains that can overlap, so density does not
    // double as a volume control.
    return out / std::sqrt (static_cast<double> (perPeriod)) * 0.6;
}

float GranularOscillator::processSample() noexcept
{
    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void GranularOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
