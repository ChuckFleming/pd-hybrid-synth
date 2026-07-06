#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace harness {

inline double rms (const std::vector<float>& x)
{
    if (x.empty())
        return 0.0;
    double sum = 0.0;
    for (float v : x)
        sum += static_cast<double> (v) * static_cast<double> (v);
    return std::sqrt (sum / static_cast<double> (x.size()));
}

inline float peakAbs (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x)
        p = std::max (p, std::fabs (v));
    return p;
}

// True if any sample is NaN or infinite -- the cheap guard that catches a huge
// class of DSP bugs. Every module test should run this.
inline bool hasBadValues (const std::vector<float>& x)
{
    for (float v : x)
        if (! std::isfinite (v))
            return true;
    return false;
}

} // namespace harness
