#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/Reverb.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>

using pdhybrid::Reverb;
using Catch::Approx;
using namespace harness;

TEST_CASE ("Reverb produces a decaying tail from an impulse", "[reverb]")
{
    Reverb rv;
    rv.setSampleRate (48000.0);
    rv.setSize (0.5);
    rv.setDamp (0.5);
    rv.setMix (1.0);

    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    l[0] = r[0] = 1.0f;                     // impulse
    rv.processStereo (l.data(), r.data(), static_cast<int> (l.size()));

    REQUIRE_FALSE (hasBadValues (l));

    auto energy = [&] (int a, int b)
    {
        double e = 0.0;
        for (int i = a; i < b; ++i) e += l[i] * l[i];
        return e;
    };
    const double early = energy (0, 12000);
    const double late  = energy (36000, 48000);
    REQUIRE (early > 0.0);          // there is a tail
    REQUIRE (late < early);         // and it decays
}

TEST_CASE ("Reverb stays stable at maximum size", "[reverb]")
{
    Reverb rv;
    rv.setSampleRate (48000.0);
    rv.setSize (1.0);
    rv.setDamp (0.0);
    rv.setMix (1.0);

    std::vector<float> l (4096), r (4096);
    for (int block = 0; block < 200; ++block)   // ~17 s of white noise
    {
        for (int i = 0; i < 4096; ++i)
        {
            const float v = ((block * 4096 + i) % 97) / 48.5f - 1.0f;
            l[i] = v; r[i] = v;
        }
        rv.processStereo (l.data(), r.data(), 4096);
        REQUIRE_FALSE (hasBadValues (l));
        REQUIRE (peakAbs (l) < 8.0f);   // bounded, no runaway feedback
    }
}

TEST_CASE ("Reverb is transparent at mix 0", "[reverb]")
{
    Reverb rv;
    rv.setSampleRate (48000.0);
    rv.setMix (0.0);

    std::vector<float> l (1024), r (1024);
    for (int i = 0; i < 1024; ++i) { l[i] = 0.3f * std::sin (0.05f * i); r[i] = l[i]; }
    const auto dry = l;
    rv.processStereo (l.data(), r.data(), 1024);
    for (std::size_t i = 0; i < l.size(); ++i)
        REQUIRE (l[i] == Approx (dry[i]).margin (1e-6));
}
