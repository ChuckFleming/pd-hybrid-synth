#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/OscEq.h"
#include "harness/FrequencyResponse.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>

using pdhybrid::OscEq;
using Catch::Approx;
using namespace harness;

namespace {
OscEq makeEq (double sr, double lo, double mid, double hi)
{
    OscEq eq;
    eq.setSampleRate (sr);
    eq.setGains (lo, mid, hi);
    eq.reset();
    return eq;
}
}

TEST_CASE ("Osc EQ is flat (unity) at 0 dB", "[osceq]")
{
    const double sr = 48000.0;
    for (double hz : { 80.0, 200.0, 1000.0, 5000.0, 9000.0 })
    {
        auto eq = makeEq (sr, 0.0, 0.0, 0.0);
        REQUIRE (measureGainDb (eq, hz, sr) == Approx (0.0).margin (0.2));
    }
}

TEST_CASE ("Low shelf boosts the lows, not the highs", "[osceq]")
{
    const double sr = 48000.0;
    auto eq = makeEq (sr, 12.0, 0.0, 0.0);
    const double low  = measureGainDb (eq, 100.0, sr);
    auto eq2 = makeEq (sr, 12.0, 0.0, 0.0);
    const double high = measureGainDb (eq2, 8000.0, sr);

    REQUIRE (low > 8.0);            // clear low boost
    REQUIRE (high == Approx (0.0).margin (0.5));   // highs untouched
    REQUIRE (low > high + 6.0);
}

TEST_CASE ("Mid peak boosts around 1 kHz", "[osceq]")
{
    const double sr = 48000.0;
    auto e1 = makeEq (sr, 0.0, 12.0, 0.0);
    const double mid  = measureGainDb (e1, 1000.0, sr);
    auto e2 = makeEq (sr, 0.0, 12.0, 0.0);
    const double low  = measureGainDb (e2, 150.0, sr);
    auto e3 = makeEq (sr, 0.0, 12.0, 0.0);
    const double high = measureGainDb (e3, 6000.0, sr);

    REQUIRE (mid > 8.0);
    REQUIRE (mid > low + 6.0);
    REQUIRE (mid > high + 6.0);
}

TEST_CASE ("High shelf boosts the highs, not the lows", "[osceq]")
{
    const double sr = 48000.0;
    auto e1 = makeEq (sr, 0.0, 0.0, 12.0);
    const double high = measureGainDb (e1, 9000.0, sr);
    auto e2 = makeEq (sr, 0.0, 0.0, 12.0);
    const double low  = measureGainDb (e2, 150.0, sr);

    REQUIRE (high > 8.0);
    REQUIRE (low == Approx (0.0).margin (0.5));
    REQUIRE (high > low + 6.0);
}

TEST_CASE ("Negative gain cuts the band", "[osceq]")
{
    const double sr = 48000.0;
    auto eq = makeEq (sr, -12.0, 0.0, 0.0);
    REQUIRE (measureGainDb (eq, 100.0, sr) < -6.0);
}

TEST_CASE ("Osc EQ stays finite under extreme settings", "[osceq]")
{
    const double sr = 48000.0;
    auto eq = makeEq (sr, 18.0, -18.0, 18.0);

    std::vector<float> buf (8192);
    for (std::size_t i = 0; i < buf.size(); ++i)
        buf[i] = eq.processSample (static_cast<float> (0.5 * std::sin (0.05 * i)));

    REQUIRE_FALSE (hasBadValues (buf));
}
