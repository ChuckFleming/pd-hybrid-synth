#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/MasterStage.h"
#include "harness/SignalStats.h"
#include "harness/Spectrum.h"

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

TEST_CASE ("Master stereo channels track together and preserve level", "[master]")
{
    auto m = make (0.0, true);   // unity gain, limiter on

    std::vector<float> l (4096), r (4096), in (4096);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = in[i] = 0.5f * std::sin (0.03f * i);   // below threshold

    m.processStereo (l.data(), r.data(), static_cast<int> (l.size()));

    for (std::size_t i = 0; i < l.size(); ++i)
        REQUIRE (l[i] == Approx (r[i]).margin (1e-6));        // both channels identical
    // Below threshold the oversampled knee is transparent apart from its FIR
    // group delay, so the level is preserved.
    REQUIRE (rms (l) == Approx (rms (in)).epsilon (0.05));
}

TEST_CASE ("Master soft-clip does not alias when driven hard", "[master][aliasing]")
{
    const double sr = 48000.0;
    const int    n  = 16384;
    const int    k0 = 1707;               // fundamental bin (~5 kHz), harmonics fold
    const double f0 = k0 * sr / n;

    auto m = make (0.0, true);            // 0 dB, limiter on
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
        l[i] = r[i] = static_cast<float> (1.3 * std::sin (2.0 * 3.14159265358979 * f0 * i / sr));
    m.processStereo (l.data(), r.data(), n);

    // Energy off the harmonic grid of f0 is aliasing from the clip.
    auto spec = computeSpectrum (l, sr, /*hann=*/false);
    double harmonic = 0.0, alias = 0.0;
    for (std::size_t b = 1; b < spec.magnitude.size(); ++b)
    {
        const double e = spec.magnitude[b] * spec.magnitude[b];
        if (b % static_cast<std::size_t> (k0) == 0) harmonic += e;
        else                                        alias    += e;
    }
    REQUIRE (alias / (harmonic + alias) < 0.02);
}
