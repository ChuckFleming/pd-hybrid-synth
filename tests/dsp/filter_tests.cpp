#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/LadderFilter.h"
#include "harness/FrequencyResponse.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>

using pdhybrid::LadderFilter;
using Catch::Approx;
using namespace harness;

TEST_CASE ("Ladder is a lowpass: flat passband, ~-12 dB at cutoff", "[filter][ladder]")
{
    const double sr = 48000.0, fc = 1000.0;
    LadderFilter f;
    f.setSampleRate (sr);
    f.setCutoff (fc);
    f.setResonance (0.0);

    const double passband = measureGainDb (f, 50.0, sr);
    const double atCutoff = measureGainDb (f, fc,   sr);

    REQUIRE (passband == Approx (0.0).margin (0.5));       // no passband gain/loss
    REQUIRE (atCutoff - passband == Approx (-12.0).margin (1.5)); // 4 x -3 dB
}

TEST_CASE ("Ladder stopband slope is ~24 dB/oct", "[filter][ladder]")
{
    const double sr = 48000.0, fc = 1000.0;
    LadderFilter f;
    f.setSampleRate (sr);
    f.setCutoff (fc);
    f.setResonance (0.0);

    const double g4 = measureGainDb (f, 4000.0, sr);   // 2 octaves above fc
    const double g8 = measureGainDb (f, 8000.0, sr);   // 3 octaves above fc
    const double slopePerOctave = g8 - g4;

    REQUIRE (slopePerOctave < -20.0);
    REQUIRE (slopePerOctave > -30.0);
}

TEST_CASE ("Resonance boosts gain around the cutoff", "[filter][ladder]")
{
    const double sr = 48000.0, fc = 1000.0;
    LadderFilter f;
    f.setSampleRate (sr);
    f.setCutoff (fc);

    f.setResonance (0.0);
    const double flat = measureGainDb (f, fc, sr);
    f.setResonance (0.8);
    const double resonant = measureGainDb (f, fc, sr);

    REQUIRE (resonant > flat + 6.0);
}

TEST_CASE ("Ladder self-oscillates near the cutoff frequency", "[filter][ladder]")
{
    const double sr = 48000.0, fc = 1000.0;
    LadderFilter f;
    f.setSampleRate (sr);
    f.setCutoff (fc);
    f.setResonance (1.0);
    f.reset();

    // Kick the unstable equilibrium with a short noise burst, then let it ring.
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
    for (int i = 0; i < 200; ++i)   f.processSample (noise (rng));
    for (int i = 0; i < 20000; ++i) f.processSample (0.0f);   // discard transient

    const int N = 16384;
    std::vector<float> buf (N);
    for (int i = 0; i < N; ++i) buf[i] = f.processSample (0.0f);

    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (peakAbs (buf) > 0.01f);                 // genuinely oscillating

    auto spec = computeSpectrum (buf, sr);
    REQUIRE (spec.peakFrequency() == Approx (fc).epsilon (0.2));  // within 20 %
}

TEST_CASE ("Ladder stays finite under rapid modulation", "[filter][ladder][stability]")
{
    const double sr = 48000.0;
    LadderFilter f;
    f.setSampleRate (sr);

    std::mt19937 rng (7);
    std::uniform_real_distribution<double> cutoff (50.0, 15000.0), res (0.0, 1.0), in (-1.0, 1.0);

    std::vector<float> out;
    out.reserve (48000);
    for (int i = 0; i < 48000; ++i)
    {
        if ((i % 16) == 0) { f.setCutoff (cutoff (rng)); f.setResonance (res (rng)); }
        out.push_back (f.processSample (static_cast<float> (in (rng))));
    }

    REQUIRE_FALSE (hasBadValues (out));
    REQUIRE (peakAbs (out) < 100.0f);
}

TEST_CASE ("Ladder filter is block-size invariant", "[filter][invariance]")
{
    const double sr = 48000.0, fc = 1200.0;
    const double twoPi = 6.283185307179586;

    auto render = [&] (int block)
    {
        LadderFilter f;
        f.setSampleRate (sr);
        f.setCutoff (fc);
        f.setResonance (0.6);
        f.reset();

        std::vector<float> buf (8192);
        for (std::size_t i = 0; i < buf.size(); ++i)
            buf[i] = static_cast<float> (std::sin (twoPi * 220.0 * static_cast<double> (i) / sr));

        int i = 0;
        while (i < static_cast<int> (buf.size()))
        {
            const int n = std::min (block, static_cast<int> (buf.size()) - i);
            f.processBlock (buf.data() + i, n);
            i += n;
        }
        return buf;
    };

    auto a = render (64);
    auto b = render (512);

    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);
}
