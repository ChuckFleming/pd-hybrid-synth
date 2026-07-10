#include "Oversampler.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kPi = 3.14159265358979323846;

static inline double sinc (double x) noexcept
{
    if (std::fabs (x) < 1e-12)
        return 1.0;
    const double px = kPi * x;
    return std::sin (px) / px;
}

void Oversampler::buildPrototype (int tapsPerPhase)
{
    const int    L      = tapsPerPhase * factor_;
    const double fc     = 0.5 / static_cast<double> (factor_);   // cutoff at high rate
    const double center = (L - 1) / 2.0;

    proto_.assign (static_cast<std::size_t> (L), 0.0);

    double sum = 0.0;
    for (int i = 0; i < L; ++i)
    {
        // Blackman window.
        const double w = 0.42
                       - 0.5  * std::cos (2.0 * kPi * i / (L - 1))
                       + 0.08 * std::cos (4.0 * kPi * i / (L - 1));
        const double h = 2.0 * fc * sinc (2.0 * fc * (i - center)) * w;
        proto_[static_cast<std::size_t> (i)] = h;
        sum += h;
    }
    for (double& c : proto_)     // normalise to unity DC gain
        c /= sum;

    upState_.assign (static_cast<std::size_t> (L), 0.0);
    downState_.assign (static_cast<std::size_t> (L), 0.0);
}

void Oversampler::prepare (int factor)
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 1;

    factor_ = factor;

    if (factor_ == 1)
    {
        proto_.clear();
        upState_.clear();
        downState_.clear();
        return;
    }
    buildPrototype (16);
}

void Oversampler::reset() noexcept
{
    for (double& v : upState_)   v = 0.0;
    for (double& v : downState_) v = 0.0;
    upPos_ = downPos_ = 0;
}

double Oversampler::firStep (std::vector<double>& state, int& pos,
                             const std::vector<double>& taps, double in) noexcept
{
    const int L = static_cast<int> (taps.size());
    state[static_cast<std::size_t> (pos)] = in;   // newest sample at pos

    double acc = 0.0;
    int idx = pos;
    for (int i = 0; i < L; ++i)                    // taps[0] * newest, taps[1] * prev, ...
    {
        acc += taps[static_cast<std::size_t> (i)] * state[static_cast<std::size_t> (idx)];
        idx = (idx == 0) ? (L - 1) : (idx - 1);
    }

    pos = (pos == L - 1) ? 0 : (pos + 1);
    return acc;
}

void Oversampler::upsample (float x, float* highOut) noexcept
{
    if (factor_ == 1)
    {
        highOut[0] = x;
        return;
    }

    // Zero-stuff (energy compensated by `factor`), then interpolation lowpass.
    for (int j = 0; j < factor_; ++j)
    {
        const double in = (j == 0) ? static_cast<double> (x) * factor_ : 0.0;
        highOut[j] = static_cast<float> (firStep (upState_, upPos_, proto_, in));
    }
}

float Oversampler::downsample (const float* highIn) noexcept
{
    if (factor_ == 1)
        return highIn[0];

    double y = 0.0;
    for (int j = 0; j < factor_; ++j)
        y = firStep (downState_, downPos_, proto_, static_cast<double> (highIn[j]));
    return static_cast<float> (y);   // aligned (last-phase) sample
}

} // namespace pdhybrid
