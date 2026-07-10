#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/MasterStage.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>

using pdhybrid::MasterStage;
using Catch::Approx;
using namespace harness;

namespace {
MasterStage make (double gainDb, bool limiter)
{
    MasterStage m;
    m.setSampleRate (48000.0);
    m.setGainDb (gainDb);
    m.setLimiterEnabled (limiter);
    m.reset();
    return m;
}
}

TEST_CASE ("Master stage is unity at 0 dB below threshold", "[master]")
{
    auto m = make (0.0, true);
    for (int i = 0; i < 256; ++i)
    {
        const float x = 0.3f * std::sin (0.05f * i);
        REQUIRE (m.processSample (x) == Approx (x).margin (1e-5));
    }
}

TEST_CASE ("Master stage applies gain to small signals", "[master]")
{
    auto m = make (6.0206, true);           // +6.02 dB ~= x2
    // Warm up the smoothed gain.
    for (int i = 0; i < 2000; ++i) m.processSample (0.0f);

    const float y = m.processSample (0.1f);
    REQUIRE (y == Approx (0.2f).margin (0.005f));
}

TEST_CASE ("Master limiter bounds hot signals below the ceiling", "[master]")
{
    auto m = make (18.0, true);             // heavy boost -> would clip
    std::vector<float> buf (4096);
    for (std::size_t i = 0; i < buf.size(); ++i)
        buf[i] = m.processSample (0.8f * std::sin (0.02f * i));

    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (peakAbs (buf) <= 1.0f);        // never overshoots the 0 dBFS ceiling
    REQUIRE (peakAbs (buf) > 0.9f);         // but does push into the ceiling
}

TEST_CASE ("Master limiter can be bypassed", "[master]")
{
    auto on  = make (18.0, true);
    auto off = make (18.0, false);
    for (int i = 0; i < 3000; ++i) { on.processSample (0.0f); off.processSample (0.0f); }

    const float x = 0.8f;
    REQUIRE (on.processSample (x)  <= 1.0f);    // limited to the ceiling
    REQUIRE (off.processSample (x) > 4.0f);     // ~x7.9 gain, unclipped
}

TEST_CASE ("Master stereo matches mono processing", "[master]")
{
    auto stereo = make (3.0, true);
    auto mono   = make (3.0, true);

    std::vector<float> l (1024), r (1024);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = 0.5f * std::sin (0.03f * i);

    stereo.processStereo (l.data(), r.data(), static_cast<int> (l.size()));

    for (std::size_t i = 0; i < l.size(); ++i)
    {
        const float expected = mono.processSample (0.5f * std::sin (0.03f * i));
        REQUIRE (l[i] == Approx (expected).margin (1e-6));
        REQUIRE (r[i] == Approx (l[i]).margin (1e-6));
    }
}
