#include "AllpassDispersion.h"
#include <algorithm>

namespace pdhybrid {

void AllpassDispersion::setCoefficient (double a) noexcept
{
    a_ = std::clamp (a, -0.99, 0.99);
}

void AllpassDispersion::setStages (int numStages) noexcept
{
    stages_ = std::clamp (numStages, 1, kMaxStages);
}

void AllpassDispersion::reset() noexcept
{
    for (int i = 0; i < kMaxStages; ++i)
    {
        x1_[i] = 0.0;
        y1_[i] = 0.0;
    }
}

float AllpassDispersion::processSample (float xin) noexcept
{
    double x = static_cast<double> (xin);

    for (int s = 0; s < stages_; ++s)
    {
        // First-order allpass: y = a*x + x[n-1] - a*y[n-1]  (|H(f)| == 1).
        const double y = a_ * x + x1_[s] - a_ * y1_[s];
        x1_[s] = x;
        y1_[s] = y;
        x = y;
    }

    return static_cast<float> (x);
}

void AllpassDispersion::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
