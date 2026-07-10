#include "OscEq.h"

namespace pdhybrid {

// Fixed band centres.
static constexpr double kLowHz  = 200.0;
static constexpr double kMidHz  = 1000.0;
static constexpr double kHighHz = 5000.0;

void OscEq::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        redesign();
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
    if (lowDb == lowDb_ && midDb == midDb_ && highDb == highDb_)
        return;   // unchanged: skip the (transcendental) redesign

    lowDb_  = lowDb;
    midDb_  = midDb;
    highDb_ = highDb;
    redesign();
}

void OscEq::redesign() noexcept
{
    // Biquad::design recomputes coefficients while preserving the running state.
    low_.design  (Biquad::Kind::LowShelf,  kLowHz,  0.707, lowDb_,  sampleRate_);
    mid_.design  (Biquad::Kind::Peaking,   kMidHz,  1.0,   midDb_,  sampleRate_);
    high_.design (Biquad::Kind::HighShelf, kHighHz, 0.707, highDb_, sampleRate_);

    // Transparent at ~0 dB on every band -> skip the biquads entirely.
    bypass_ = std::fabs (lowDb_) < 0.01 && std::fabs (midDb_) < 0.01 && std::fabs (highDb_) < 0.01;
}

float OscEq::processSample (float x) noexcept
{
    if (bypass_)
        return x;

    double y = x;
    y = low_.process (y);
    y = mid_.process (y);
    y = high_.process (y);
    return static_cast<float> (y);
}

} // namespace pdhybrid
