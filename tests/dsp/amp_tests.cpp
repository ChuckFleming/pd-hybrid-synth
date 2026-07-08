#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/Waveshaper.h"
#include "dsp/OverdriveAmp.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>
#include <algorithm>

using namespace pdhybrid;
using Catch::Approx;
using namespace harness;

static constexpr double kTwoPi = 6.283185307179586476925287;

TEST_CASE ("Every drive type stays finite and bounded", "[amp][shaper]")
{
    for (auto c : { ShaperCurve::Tanh, ShaperCurve::Cubic, ShaperCurve::HardClip,
                    ShaperCurve::Tube, ShaperCurve::Diode, ShaperCurve::Fuzz,
                    ShaperCurve::Rectify, ShaperCurve::Wavefold, ShaperCurve::Foldback })
    {
        Waveshaper s;
        s.setCurve (c);
        s.setDrive (6.0);
        for (int i = 0; i < 400; ++i)
        {
            const double x = -5.0 + 10.0 * i / 399.0;
            const double y = s.process (static_cast<float> (x));
            REQUIRE (std::isfinite (y));
            REQUIRE (std::abs (y) <= 1.05);
        }
    }
}

TEST_CASE ("Hard clip clamps to unity", "[amp][shaper]")
{
    Waveshaper s;
    s.setCurve (ShaperCurve::HardClip);
    s.setDrive (4.0);
    REQUIRE (s.process (5.0f)  == Approx (1.0));
    REQUIRE (s.process (-5.0f) == Approx (-1.0));
}

TEST_CASE ("Bit crush quantises the output to a few levels", "[amp][crush]")
{
    OverdriveAmp amp;
    amp.setSampleRate (48000.0);
    amp.setOversampling (1);
    amp.setDcBlock (false);
    amp.setCurve (ShaperCurve::HardClip);
    amp.setDrive (1.0);
    amp.setCrushBits (2.0);   // 2 levels per polarity
    amp.reset();

    std::vector<float> vals;
    for (int i = 0; i < 2000; ++i)
        vals.push_back (amp.processSample (static_cast<float> (std::sin (kTwoPi * 100.0 * i / 48000.0))));
    std::sort (vals.begin(), vals.end());
    vals.erase (std::unique (vals.begin(), vals.end()), vals.end());
    REQUIRE (vals.size() <= 6);
}

TEST_CASE ("Waveshaper transfer curve is monotonic, bounded and odd-symmetric", "[amp][shaper]")
{
    Waveshaper s;
    s.setCurve (ShaperCurve::Tanh);
    s.setDrive (3.0);
    s.setBias (0.0);

    const int N = 400;
    double prev = -1e9;
    for (int i = 0; i < N; ++i)
    {
        const double x = -4.0 + 8.0 * i / (N - 1);
        const double y = s.process (static_cast<float> (x));

        REQUIRE (std::abs (y) <= 1.0001);                          // bounded
        REQUIRE (y >= prev - 1e-6);                                // monotonic
        REQUIRE (s.process (static_cast<float> (x))
                     == Approx (-s.process (static_cast<float> (-x))).margin (1e-6)); // odd
        prev = y;
    }
}

TEST_CASE ("Waveshaper is near-unity at low drive", "[amp][shaper]")
{
    Waveshaper s;
    s.setDrive (1.0);
    s.setBias (0.0);
    REQUIRE (s.process ( 0.01f) == Approx ( 0.01).margin (1e-4));
    REQUIRE (s.process (-0.02f) == Approx (-0.02).margin (1e-4));
}

TEST_CASE ("Symmetric shaping yields odd harmonics; bias adds even harmonics", "[amp][shaper]")
{
    const double sr = 48000.0;
    const int    N  = 32768;
    const double f0 = 64.0 * sr / N;   // exact FFT bin -> no leakage

    auto renderSpectrum = [&] (double bias)
    {
        Waveshaper s;
        s.setCurve (ShaperCurve::Tanh);
        s.setDrive (5.0);
        s.setBias (bias);
        std::vector<float> buf (N);
        for (int i = 0; i < N; ++i)
            buf[i] = s.process (static_cast<float> (0.8 * std::sin (kTwoPi * f0 * i / sr)));
        return computeSpectrum (buf, sr, /*hann*/ false);
    };

    auto sym = renderSpectrum (0.0);
    const double odd3  = sym.magnitudeNearHz (3 * f0);
    const double even2 = sym.magnitudeNearHz (2 * f0);
    REQUIRE (odd3 > 0.0);
    REQUIRE (even2 < 0.02 * odd3);          // evens essentially absent

    auto biased = renderSpectrum (0.6);
    const double even2b = biased.magnitudeNearHz (2 * f0);
    const double odd3b  = biased.magnitudeNearHz (3 * f0);
    REQUIRE (even2b > 0.2 * odd3b);         // bias introduces a real even harmonic
}

TEST_CASE ("Oversampling suppresses aliasing on a hot high tone", "[amp][oversampling]")
{
    const double sr = 48000.0;
    const int    N  = 32768;
    // Exact FFT bin (no leakage) but sr/f0 is non-integer, so aliased harmonics
    // fold onto bins that are NOT genuine harmonics -> they're measurable.
    const double f0 = 2000.0 * sr / N;   // = 2929.6875 Hz

    auto aliasRatio = [&] (int factor)
    {
        OverdriveAmp amp;
        amp.setSampleRate (sr);
        amp.setOversampling (factor);
        amp.setCurve (ShaperCurve::Tanh);
        amp.setDrive (8.0);
        amp.setBias (0.0);
        amp.reset();

        std::vector<float> buf (N);
        for (int i = 0; i < N; ++i)
            buf[i] = amp.processSample (static_cast<float> (std::sin (kTwoPi * f0 * i / sr)));

        auto spec = computeSpectrum (buf, sr, /*hann*/ false);
        const double total = totalEnergy (spec);

        double harmonic = 0.0;   // energy at genuine (odd) harmonics of f0
        for (int k = 1; k * f0 < 0.5 * sr; k += 2)
        {
            const double m = spec.magnitudeNearHz (k * f0, 1);
            harmonic += m * m;
        }
        const double alias = total - harmonic;
        return alias / (total > 0.0 ? total : 1.0);
    };

    const double r1 = aliasRatio (1);
    const double r8 = aliasRatio (8);

    REQUIRE (r8 < r1);        // oversampling clearly helps
    REQUIRE (r8 < 0.02);      // and leaves little aliasing
}

TEST_CASE ("DC blocker removes bias-induced offset", "[amp][dc]")
{
    const double sr = 48000.0, f0 = 220.0;
    const int    N  = 16384;

    OverdriveAmp amp;
    amp.setSampleRate (sr);
    amp.setOversampling (4);
    amp.setCurve (ShaperCurve::Tanh);
    amp.setDrive (4.0);
    amp.setBias (0.8);
    amp.setDcBlock (true);
    amp.reset();

    std::vector<float> buf (N);
    for (int i = 0; i < N; ++i)
        buf[i] = amp.processSample (static_cast<float> (0.7 * std::sin (kTwoPi * f0 * i / sr)));

    const int start = 2000;   // skip DC-blocker + FIR settling
    double mean = 0.0;
    for (int i = start; i < N; ++i) mean += buf[i];
    mean /= (N - start);

    REQUIRE (std::abs (mean) < 0.01);
}

TEST_CASE ("Overdrive is block-size invariant", "[amp][invariance]")
{
    const double sr = 48000.0, f0 = 330.0;

    auto render = [&] (int block)
    {
        OverdriveAmp amp;
        amp.setSampleRate (sr);
        amp.setOversampling (4);
        amp.setDrive (6.0);
        amp.reset();

        std::vector<float> buf (8192);
        for (std::size_t i = 0; i < buf.size(); ++i)
            buf[i] = static_cast<float> (std::sin (kTwoPi * f0 * static_cast<double> (i) / sr));

        int i = 0;
        while (i < static_cast<int> (buf.size()))
        {
            const int n = std::min (block, static_cast<int> (buf.size()) - i);
            amp.processBlock (buf.data() + i, n);
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
