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

    // Biquad::design recomputes coefficients while preserving the running state.
    low_.design  (Biquad::Kind::LowShelf,  kLowHz,  0.707, lowDb,  sampleRate_);
    mid_.design  (Biquad::Kind::Peaking,   kMidHz,  1.0,   midDb,  sampleRate_);
    high_.design (Biquad::Kind::HighShelf, kHighHz, 0.707, highDb, sampleRate_);
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
