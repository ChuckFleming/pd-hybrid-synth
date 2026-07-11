#include "Delay.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static inline double timeToCoef (double seconds, double sr) noexcept
{
    if (seconds <= 0.0) return 0.0;
    return std::exp (-1.0 / (seconds * sr));
}

void Delay::setSampleRate (double sampleRateHz)
{
    if (sampleRateHz <= 0.0)
        return;

    sampleRate_ = sampleRateHz;
    const int needed = static_cast<int> (kMaxDelaySeconds * sampleRate_) + 4;
    size_ = 1;
    while (size_ < needed)          // round up to a power of two for masked wrap
        size_ <<= 1;
    mask_ = size_ - 1;
    bufL_.assign (static_cast<std::size_t> (size_), 0.0f);
    bufR_.assign (static_cast<std::size_t> (size_), 0.0f);
    write_ = 0;
    duckAtk_ = timeToCoef (0.005, sampleRate_);
    duckRel_ = timeToCoef (0.15, sampleRate_);
}

void Delay::reset() noexcept
{
    std::fill (bufL_.begin(), bufL_.end(), 0.0f);
    std::fill (bufR_.begin(), bufR_.end(), 0.0f);
    write_    = 0;
    duckEnv_  = 0.0;
}

void Delay::setTimes (double leftSeconds, double rightSeconds) noexcept
{
    const double maxD = static_cast<double> (size_ - 2);
    delayL_ = std::clamp (leftSeconds  * sampleRate_, 1.0, maxD);
    delayR_ = std::clamp (rightSeconds * sampleRate_, 1.0, maxD);
}

void Delay::setFeedback (double feedback01) noexcept
{
    feedback_ = std::clamp (feedback01, 0.0, 0.95);
}

void Delay::setMix (double mix01) noexcept
{
    mix_ = std::clamp (mix01, 0.0, 1.0);
}

void Delay::setDuck (double amount01) noexcept
{
    duckAmount_ = std::clamp (amount01, 0.0, 1.0);
}

// Linearly interpolated read `delaySamples` behind the write head.
float Delay::readFrac (const std::vector<float>& buf, double delaySamples) const noexcept
{
    double readPos = static_cast<double> (write_) - delaySamples;
    if (readPos < 0.0)              // delaySamples < size_, so one add suffices
        readPos += size_;

    const int    i0   = static_cast<int> (readPos);
    const double frac = readPos - i0;
    const int    i1   = (i0 + 1) & mask_;
    return static_cast<float> (buf[static_cast<std::size_t> (i0 & mask_)] * (1.0 - frac)
                             + buf[static_cast<std::size_t> (i1)] * frac);
}

void Delay::processStereo (float* left, float* right, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        const double dryL = left[n];
        const double dryR = right[n];

        // Ducking follower on the dry input (fast attack, slower release).
        const double inLevel = std::max (std::abs (dryL), std::abs (dryR));
        const double coef    = (inLevel > duckEnv_) ? duckAtk_ : duckRel_;
        duckEnv_ = inLevel + coef * (duckEnv_ - inLevel);
        const double duckGain = std::clamp (1.0 - duckAmount_ * std::min (1.0, duckEnv_ * 4.0),
                                            0.0, 1.0);

        const double timeR = (mode_ == DelayMode::Mono) ? delayL_ : delayR_;
        const double echoL = readFrac (bufL_, delayL_);
        const double echoR = readFrac (bufR_, timeR);

        // Feedback routing: ping-pong crosses the channels.
        double wL, wR;
        if (mode_ == DelayMode::PingPong)
        {
            wL = dryL + feedback_ * echoR;
            wR = dryR + feedback_ * echoL;
        }
        else
        {
            wL = dryL + feedback_ * echoL;
            wR = dryR + feedback_ * echoR;
        }

        bufL_[static_cast<std::size_t> (write_)] = static_cast<float> (wL);
        bufR_[static_cast<std::size_t> (write_)] = static_cast<float> (wR);
        write_ = (write_ + 1) & mask_;

        left[n]  = static_cast<float> (dryL * (1.0 - mix_) + echoL * mix_ * duckGain);
        right[n] = static_cast<float> (dryR * (1.0 - mix_) + echoR * mix_ * duckGain);
    }
}

} // namespace pdhybrid
