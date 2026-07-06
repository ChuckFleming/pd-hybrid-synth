#include "CombFilter.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

void CombFilter::setSampleRate (double sampleRateHz)
{
    if (sampleRateHz <= 0.0)
        return;
    sampleRate_ = sampleRateHz;

    // Longest delay corresponds to the lowest supported resonance (~20 Hz).
    const int maxDelay = static_cast<int> (sampleRate_ / 20.0) + 4;
    buffer_.assign (static_cast<std::size_t> (maxDelay), 0.0f);
    writePos_ = 0;
    lpState_  = 0.0;
}

void CombFilter::setFrequency (double frequencyHz) noexcept
{
    if (frequencyHz < 20.0)
        frequencyHz = 20.0;
    double d = sampleRate_ / frequencyHz;
    const double maxD = static_cast<double> (buffer_.size()) - 2.0;
    if (d > maxD) d = maxD;
    if (d < 1.0)  d = 1.0;
    delay_ = d;
}

void CombFilter::setFeedback (double feedback01) noexcept
{
    feedback_ = std::clamp (feedback01, 0.0, 0.995);
}

void CombFilter::setDamping (double damping01) noexcept
{
    damping_ = std::clamp (damping01, 0.0, 0.99);
}

void CombFilter::reset() noexcept
{
    std::fill (buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    lpState_  = 0.0;
}

float CombFilter::readInterpolated (double delaySamples) const noexcept
{
    const int size = static_cast<int> (buffer_.size());
    if (size == 0)
        return 0.0f;

    double readPos = writePos_ - delaySamples;
    while (readPos < 0.0) readPos += size;

    const int i0 = static_cast<int> (readPos);
    const int i1 = (i0 + 1) % size;
    const double frac = readPos - i0;
    return static_cast<float> (buffer_[static_cast<std::size_t> (i0)] * (1.0 - frac)
                             + buffer_[static_cast<std::size_t> (i1)] * frac);
}

float CombFilter::processSample (float xin) noexcept
{
    if (buffer_.empty())
        return xin;

    const double delayed = readInterpolated (delay_);

    // One-pole lowpass in the feedback path (damping).
    lpState_ = (1.0 - damping_) * delayed + damping_ * lpState_;

    const double y = static_cast<double> (xin) + feedback_ * lpState_;

    buffer_[static_cast<std::size_t> (writePos_)] = static_cast<float> (y);
    writePos_ = (writePos_ + 1) % static_cast<int> (buffer_.size());

    return static_cast<float> (y);
}

void CombFilter::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
