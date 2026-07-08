#include "OscEq.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

// Fixed band centres.
static constexpr double kLowHz  = 200.0;
static constexpr double kMidHz  = 1000.0;
static constexpr double kHighHz = 5000.0;

OscEq::Biquad OscEq::design (Kind kind, double freqHz, double q, double gainDb, double sr) noexcept
{
    Biquad bq;

    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = kTwoPi * freqHz / sr;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    const double alpha = sinw0 / (2.0 * q);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (kind)
    {
        case Kind::Peaking:
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cosw0;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cosw0;
            a2 = 1.0 - alpha / A;
            break;

        case Kind::LowShelf:
        {
            const double s = 2.0 * std::sqrt (A) * alpha;
            b0 =      A * ((A + 1.0) - (A - 1.0) * cosw0 + s);
            b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
            b2 =      A * ((A + 1.0) - (A - 1.0) * cosw0 - s);
            a0 =          (A + 1.0) + (A - 1.0) * cosw0 + s;
            a1 = -2.0 *   ((A - 1.0) + (A + 1.0) * cosw0);
            a2 =          (A + 1.0) + (A - 1.0) * cosw0 - s;
            break;
        }

        case Kind::HighShelf:
        {
            const double s = 2.0 * std::sqrt (A) * alpha;
            b0 =      A * ((A + 1.0) + (A - 1.0) * cosw0 + s);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
            b2 =      A * ((A + 1.0) + (A - 1.0) * cosw0 - s);
            a0 =          (A + 1.0) - (A - 1.0) * cosw0 + s;
            a1 =  2.0 *   ((A - 1.0) - (A + 1.0) * cosw0);
            a2 =          (A + 1.0) - (A - 1.0) * cosw0 - s;
            break;
        }
    }

    const double inv = 1.0 / a0;
    bq.b0 = b0 * inv;
    bq.b1 = b1 * inv;
    bq.b2 = b2 * inv;
    bq.a1 = a1 * inv;
    bq.a2 = a2 * inv;
    return bq;
}

void OscEq::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        setGains (lowDb_, midDb_, highDb_);
    }
}

void OscEq::reset() noexcept
{
    low_.reset();
    mid_.reset();
    high_.reset();
}

void OscEq::setGains (double lowDb, double midDb, double highDb) noexcept
{
    lowDb_  = lowDb;
    midDb_  = midDb;
    highDb_ = highDb;

    // Preserve the running state; only recompute the coefficients.
    const auto keep = [] (Biquad& dst, const Biquad& src)
    {
        dst.b0 = src.b0; dst.b1 = src.b1; dst.b2 = src.b2;
        dst.a1 = src.a1; dst.a2 = src.a2;
    };
    keep (low_,  design (Kind::LowShelf,  kLowHz,  0.707, lowDb,  sampleRate_));
    keep (mid_,  design (Kind::Peaking,   kMidHz,  1.0,   midDb,  sampleRate_));
    keep (high_, design (Kind::HighShelf, kHighHz, 0.707, highDb, sampleRate_));
}

float OscEq::processSample (float x) noexcept
{
    double y = x;
    y = low_.process (y);
    y = mid_.process (y);
    y = high_.process (y);
    return static_cast<float> (y);
}

} // namespace pdhybrid
