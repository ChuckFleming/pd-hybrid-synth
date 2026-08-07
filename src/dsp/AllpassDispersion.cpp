#include "AllpassDispersion.h"
#include <algorithm>
#include <cmath>

namespace pdhybrid {

static constexpr double kPi = 3.14159265358979323846;

void AllpassDispersion::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
        sampleRate_ = sampleRateHz;
}

void AllpassDispersion::setFrequency (double frequencyHz) noexcept
{
    // Standard first-order allpass break frequency: the coefficient that puts
    // the -90 degree phase point at fc.
    double fc = frequencyHz;
    const double nyquistGuard = 0.49 * sampleRate_;
    if (fc < 20.0)         fc = 20.0;
    if (fc > nyquistGuard) fc = nyquistGuard;

    const double t = std::tan (kPi * fc / sampleRate_);
    setCoefficient ((t - 1.0) / (t + 1.0));
}

void AllpassDispersion::setCoefficient (double a) noexcept
{
    a_ = std::clamp (a, -0.99, 0.99);
}

void AllpassDispersion::setStages (int numStages) noexcept
{
    stages_ = std::clamp (numStages, 1, kMaxStages);
}

void AllpassDispersion::setFeedback (double feedback01) noexcept
{
    feedback_ = std::clamp (feedback01, 0.0, 0.95);
}

void AllpassDispersion::reset() noexcept
{
    for (int i = 0; i < kMaxStages; ++i)
    {
        x1_[i] = 0.0;
        y1_[i] = 0.0;
    }
    fbState_ = 0.0;
}

float AllpassDispersion::processSample (float xin) noexcept
{
    double x = static_cast<double> (xin) + feedback_ * fbState_;

    for (int s = 0; s < stages_; ++s)
    {
        // First-order allpass: y = a*x + x[n-1] - a*y[n-1]  (|H(f)| == 1).
        const double y = a_ * x + x1_[s] - a_ * y1_[s];
        x1_[s] = x;
        y1_[s] = y;
        x = y;
    }

    // tanh-free soft clip keeps the resonant path bounded without colouring the
    // plain (feedback == 0) case, where fbState_ is never read.
    fbState_ = x / (1.0 + 0.3 * x * x);

    return static_cast<float> (x);
}

void AllpassDispersion::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
