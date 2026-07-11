#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/Chorus.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>

using pdhybrid::Chorus;
using Catch::Approx;
using namespace harness;

namespace {

std::vector<float> saw (int n)
{
    std::vector<float> v (static_cast<std::size_t> (n));
    double ph = 0.0;
    for (auto& s : v) { s = static_cast<float> (2.0 * ph - 1.0); ph += 0.01; if (ph >= 1.0) ph -= 1.0; }
    return v;
}

} // namespace

TEST_CASE ("Chorus alters the signal and stays finite", "[chorus]")
{
    Chorus c;
    c.setSampleRate (48000.0);
    c.setMode (2);
    c.setRate (0.8);
    c.setDepth (0.7);
    c.setMix (0.5);

    auto l = saw (8192), r = saw (8192);
    const auto dry = l;
    c.processStereo (l.data(), r.data(), static_cast<int> (l.size()));

    REQUIRE_FALSE (hasBadValues (l));
    REQUIRE_FALSE (hasBadValues (r));

    double diff = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
        diff += std::abs (l[i] - dry[i]);
    REQUIRE (diff > 1.0);                       // wet signal differs from dry
    REQUIRE (peakAbs (l) < 2.0f);               // bounded
}

TEST_CASE ("Chorus is transparent at mix 0 and silent for silence", "[chorus]")
{
    Chorus c;
    c.setSampleRate (48000.0);
    c.setMix (0.0);

    auto l = saw (2048), r = saw (2048);
    const auto dry = l;
    c.processStereo (l.data(), r.data(), static_cast<int> (l.size()));
    for (std::size_t i = 0; i < l.size(); ++i)
        REQUIRE (l[i] == Approx (dry[i]).margin (1e-6));

    Chorus c2;
    c2.setSampleRate (48000.0);
    c2.setMix (1.0);
    std::vector<float> zl (2048, 0.0f), zr (2048, 0.0f);
    c2.processStereo (zl.data(), zr.data(), 2048);
    REQUIRE (peakAbs (zl) == Approx (0.0f).margin (1e-9));
}
