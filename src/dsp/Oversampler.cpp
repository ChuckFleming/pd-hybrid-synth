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

    tapsPerPhase_ = tapsPerPhase;
    upBase_.assign (static_cast<std::size_t> (tapsPerPhase), 0.0);
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
        upBase_.clear();
        downState_.clear();
        return;
    }
    buildPrototype (16);
}

void Oversampler::reset() noexcept
{
    for (double& v : upBase_)    v = 0.0;
    for (double& v : downState_) v = 0.0;
    upBasePos_ = downPos_ = 0;
}

void Oversampler::firPush (std::vector<double>& state, int& pos, double in) noexcept
{
    const int L = static_cast<int> (state.size());
    state[static_cast<std::size_t> (pos)] = in;
    pos = (pos == L - 1) ? 0 : (pos + 1);
}

double Oversampler::firDot (const std::vector<double>& state, int pos,
                            const std::vector<double>& taps) noexcept
{
    const int L = static_cast<int> (taps.size());
    int idx = (pos == 0) ? (L - 1) : (pos - 1);   // most-recently written sample

    double acc = 0.0;
    for (int i = 0; i < L; ++i)                    // taps[0] * newest, taps[1] * prev, ...
    {
        acc += taps[static_cast<std::size_t> (i)] * state[static_cast<std::size_t> (idx)];
        idx = (idx == 0) ? (L - 1) : (idx - 1);
    }
    return acc;
}

void Oversampler::upsample (float x, float* highOut) noexcept
{
    if (factor_ == 1)
    {
        highOut[0] = x;
        return;
    }

    // Polyphase interpolation: the zero-stuffed input means output phase p only
    // touches taps p, p+factor, p+2*factor, ... applied to the base-rate history
    // (energy compensated by `factor`). Bit-identical to filtering the
    // zero-stuffed stream, but factor x fewer multiplies.
    upBase_[static_cast<std::size_t> (upBasePos_)] = x;

    for (int p = 0; p < factor_; ++p)
    {
        double acc = 0.0;
        int idx = upBasePos_;
        for (int m = 0; m < tapsPerPhase_; ++m)
        {
            acc += proto_[static_cast<std::size_t> (p + m * factor_)]
                 * upBase_[static_cast<std::size_t> (idx)];
            idx = (idx == 0) ? (tapsPerPhase_ - 1) : (idx - 1);
        }
        highOut[p] = static_cast<float> (acc * factor_);
    }

    upBasePos_ = (upBasePos_ == tapsPerPhase_ - 1) ? 0 : (upBasePos_ + 1);
}

float Oversampler::downsample (const float* highIn) noexcept
{
    if (factor_ == 1)
        return highIn[0];

    // Push every high-rate sample (state must stay correct), but only the
    // retained (aligned) output is used, so compute the dot product once.
    for (int j = 0; j < factor_; ++j)
        firPush (downState_, downPos_, static_cast<double> (highIn[j]));

    return static_cast<float> (firDot (downState_, downPos_, proto_));
}

} // namespace pdhybrid
