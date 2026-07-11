#include "Chorus.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

void Chorus::setSampleRate (double sampleRateHz)
{
    sampleRate_ = sampleRateHz > 0.0 ? sampleRateHz : 44100.0;

    const int needed = static_cast<int> (0.05 * sampleRate_) + 4;   // 50 ms max
    size_ = 1;
    while (size_ < needed)
        size_ <<= 1;
    mask_ = size_ - 1;

    bufL_.assign (static_cast<std::size_t> (size_), 0.0f);
    bufR_.assign (static_cast<std::size_t> (size_), 0.0f);
    write_  = 0;
    phaseA_ = 0.0;
    phaseB_ = kHalfPi;
}

void Chorus::reset() noexcept
{
    std::fill (bufL_.begin(), bufL_.end(), 0.0f);
    std::fill (bufR_.begin(), bufR_.end(), 0.0f);
    write_ = 0;
}

float Chorus::readFrac (const std::vector<float>& buf, double delaySamples) const noexcept
{
    double readPos = static_cast<double> (write_) - delaySamples;
    if (readPos < 0.0)
        readPos += size_;

    const int    i0   = static_cast<int> (readPos);
    const double frac = readPos - i0;
    const int    i1   = (i0 + 1) & mask_;
    return static_cast<float> (buf[static_cast<std::size_t> (i0 & mask_)] * (1.0 - frac)
                             + buf[static_cast<std::size_t> (i1)] * frac);
}

void Chorus::processStereo (float* left, float* right, int numSamples) noexcept
{
    const double baseS = kBaseMs  * 0.001 * sampleRate_;
    const double depS  = kDepthMs * 0.001 * sampleRate_ * depth_;
    const double incA  = kTwoPi * rateHz_          / sampleRate_;
    const double incB  = kTwoPi * (rateHz_ * 1.31) / sampleRate_;   // 2nd voice slightly faster

    const bool   useA = (mode_ == 0 || mode_ == 2);
    const bool   useB = (mode_ == 1 || mode_ == 2);
    const double norm = (mode_ == 2) ? 0.5 : 1.0;   // keep level even when both voices run

    for (int i = 0; i < numSamples; ++i)
    {
        const double dryL = left[i];
        const double dryR = right[i];

        bufL_[static_cast<std::size_t> (write_)] = static_cast<float> (dryL);
        bufR_[static_cast<std::size_t> (write_)] = static_cast<float> (dryR);

        double wetL = 0.0, wetR = 0.0;
        if (useA)
        {
            wetL += readFrac (bufL_, baseS + depS * (0.5 + 0.5 * std::sin (phaseA_)));
            wetR += readFrac (bufR_, baseS + depS * (0.5 + 0.5 * std::sin (phaseA_ + kHalfPi)));
        }
        if (useB)
        {
            wetL += readFrac (bufL_, baseS + depS * (0.5 + 0.5 * std::sin (phaseB_)));
            wetR += readFrac (bufR_, baseS + depS * (0.5 + 0.5 * std::sin (phaseB_ + kHalfPi)));
        }
        wetL *= norm;
        wetR *= norm;

        left[i]  = static_cast<float> (dryL * (1.0 - mix_) + wetL * mix_);
        right[i] = static_cast<float> (dryR * (1.0 - mix_) + wetR * mix_);

        write_ = (write_ + 1) & mask_;
        phaseA_ += incA; if (phaseA_ >= kTwoPi) phaseA_ -= kTwoPi;
        phaseB_ += incB; if (phaseB_ >= kTwoPi) phaseB_ -= kTwoPi;
    }
}

} // namespace pdhybrid
