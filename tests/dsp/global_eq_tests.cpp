#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/GlobalEq.h"
#include "harness/FrequencyResponse.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>

using pdhybrid::GlobalEq;
using Catch::Approx;
using namespace harness;

namespace {
// Builds an EQ with the four bands set to the given gains at their default
// (or supplied) frequencies, then resets the running state.
GlobalEq makeEq (double sr, double loG, double m1G, double m2G, double hiG,
                 double loF = 120.0, double m1F = 500.0,
                 double m2F = 2000.0, double hiF = 8000.0)
{
    GlobalEq eq;
    eq.setSampleRate (sr);
    eq.setBand (GlobalEq::LowShelf,  loF, loG);
    eq.setBand (GlobalEq::Mid1,      m1F, m1G);
    eq.setBand (GlobalEq::Mid2,      m2F, m2G);
    eq.setBand (GlobalEq::HighShelf, hiF, hiG);
    eq.reset();
    return eq;
}
}

TEST_CASE ("Global EQ is flat (unity) at 0 dB", "[globaleq]")
{
    const double sr = 48000.0;
    for (double hz : { 50.0, 120.0, 500.0, 2000.0, 8000.0, 15000.0 })
    {
        auto eq = makeEq (sr, 0.0, 0.0, 0.0, 0.0);
        REQUIRE (measureGainDb (eq, hz, sr) == Approx (0.0).margin (0.2));
    }
}

TEST_CASE ("Global EQ low shelf boosts the lows, not the highs", "[globaleq]")
{
    const double sr = 48000.0;
    auto e1 = makeEq (sr, 12.0, 0.0, 0.0, 0.0);
    const double low  = measureGainDb (e1, 50.0, sr);
    auto e2 = makeEq (sr, 12.0, 0.0, 0.0, 0.0);
    const double high = measureGainDb (e2, 12000.0, sr);

    REQUIRE (low > 8.0);
    REQUIRE (high == Approx (0.0).margin (0.5));
    REQUIRE (low > high + 6.0);
}

TEST_CASE ("Global EQ mid bands peak at their own centres", "[globaleq]")
{
    const double sr = 48000.0;

    // Mid1 at 500 Hz.
    auto a1 = makeEq (sr, 0.0, 12.0, 0.0, 0.0);
    const double m1 = measureGainDb (a1, 500.0, sr);
    auto a2 = makeEq (sr, 0.0, 12.0, 0.0, 0.0);
    const double m1far = measureGainDb (a2, 8000.0, sr);
    REQUIRE (m1 > 8.0);
    REQUIRE (m1 > m1far + 6.0);

    // Mid2 at 2 kHz, independent of Mid1.
    auto b1 = makeEq (sr, 0.0, 0.0, 12.0, 0.0);
    const double m2 = measureGainDb (b1, 2000.0, sr);
    auto b2 = makeEq (sr, 0.0, 0.0, 12.0, 0.0);
    const double m2low = measureGainDb (b2, 200.0, sr);
    REQUIRE (m2 > 8.0);
    REQUIRE (m2 > m2low + 6.0);
}

TEST_CASE ("Global EQ high shelf boosts the highs, not the lows", "[globaleq]")
{
    const double sr = 48000.0;
    auto e1 = makeEq (sr, 0.0, 0.0, 0.0, 12.0);
    const double high = measureGainDb (e1, 14000.0, sr);
    auto e2 = makeEq (sr, 0.0, 0.0, 0.0, 12.0);
    const double low  = measureGainDb (e2, 100.0, sr);

    REQUIRE (high > 8.0);
    REQUIRE (low == Approx (0.0).margin (0.5));
    REQUIRE (high > low + 6.0);
}

TEST_CASE ("Global EQ negative gain cuts the band", "[globaleq]")
{
    const double sr = 48000.0;
    auto eq = makeEq (sr, -12.0, 0.0, 0.0, 0.0);
    REQUIRE (measureGainDb (eq, 50.0, sr) < -6.0);
}

TEST_CASE ("Global EQ band frequency is adjustable", "[globaleq]")
{
    const double sr = 48000.0;

    // Move Mid1 up to 4 kHz: the boost should now favour 4 kHz over 500 Hz.
    auto lowCentre  = makeEq (sr, 0.0, 12.0, 0.0, 0.0, 120.0, 500.0);
    const double at500  = measureGainDb (lowCentre, 500.0, sr);

    auto highCentre = makeEq (sr, 0.0, 12.0, 0.0, 0.0, 120.0, 4000.0);
    const double at4k   = measureGainDb (highCentre, 4000.0, sr);
    auto highCentre2 = makeEq (sr, 0.0, 12.0, 0.0, 0.0, 120.0, 4000.0);
    const double at500b = measureGainDb (highCentre2, 500.0, sr);

    REQUIRE (at500 > 8.0);          // default centre boosts 500 Hz
    REQUIRE (at4k  > 8.0);          // retuned centre boosts 4 kHz
    REQUIRE (at4k  > at500b + 6.0); // and no longer boosts 500 Hz much
}

TEST_CASE ("Global EQ processes both channels identically", "[globaleq]")
{
    const double sr = 48000.0;
    GlobalEq eq;
    eq.setSampleRate (sr);
    eq.setBand (GlobalEq::LowShelf, 120.0, 9.0);
    eq.setBand (GlobalEq::HighShelf, 8000.0, -6.0);
    eq.reset();

    std::vector<float> l (2048), r (2048);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = static_cast<float> (0.3 * std::sin (0.03 * i));

    eq.processStereo (l.data(), r.data(), static_cast<int> (l.size()));

    for (std::size_t i = 0; i < l.size(); ++i)
        REQUIRE (l[i] == Approx (r[i]).margin (1e-6));
}

TEST_CASE ("Global EQ stays finite under extreme settings", "[globaleq]")
{
    const double sr = 48000.0;
    auto eq = makeEq (sr, 18.0, -18.0, 18.0, -18.0);

    std::vector<float> buf (8192);
    for (std::size_t i = 0; i < buf.size(); ++i)
        buf[i] = eq.processSample (static_cast<float> (0.5 * std::sin (0.05 * i)));

    REQUIRE_FALSE (hasBadValues (buf));
}
