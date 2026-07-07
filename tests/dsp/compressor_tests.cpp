#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/Compressor.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>

using pdhybrid::Compressor;
using Catch::Approx;
using namespace harness;

namespace {

constexpr double kTwoPi = 6.283185307179586;

// A stereo pair of sine buffers at the given per-channel amplitudes.
struct StereoSine { std::vector<float> l, r; };

StereoSine makeStereoSine (double ampL, double ampR, double freq, double sr, int n)
{
    StereoSine s { std::vector<float> (n), std::vector<float> (n) };
    for (int i = 0; i < n; ++i)
    {
        const double ph = kTwoPi * freq * i / sr;
        s.l[i] = static_cast<float> (ampL * std::sin (ph));
        s.r[i] = static_cast<float> (ampR * std::sin (ph));
    }
    return s;
}

double dB (double linear) { return 20.0 * std::log10 (linear + 1.0e-12); }

// Peak of the second half (past the attack transient).
float settledPeak (const std::vector<float>& b)
{
    return peakAbs (std::vector<float> (b.begin() + b.size() / 2, b.end()));
}

} // namespace

TEST_CASE ("Compressor below threshold is transparent", "[compressor]")
{
    const double sr = 48000.0;
    Compressor c;
    c.setSampleRate (sr);
    c.setThreshold (-20.0);
    c.setRatio (4.0);
    c.setKnee (0.0);
    c.reset();

    auto s = makeStereoSine (0.05, 0.05, 200.0, sr, 48000);   // -26 dB, below -20 dB
    c.processStereo (s.l.data(), s.r.data(), (int) s.l.size());

    REQUIRE (settledPeak (s.l) == Approx (0.05f).epsilon (0.03));
}

TEST_CASE ("Compressor reduces gain above threshold at ~1/ratio", "[compressor]")
{
    const double sr = 48000.0;
    Compressor c;
    c.setSampleRate (sr);
    c.setThreshold (-20.0);
    c.setRatio (4.0);
    c.setKnee (0.0);
    c.setAttack (0.002);
    c.setRelease (0.05);
    c.reset();

    const double amp = 0.5;                       // -6 dB in
    auto s = makeStereoSine (amp, amp, 200.0, sr, 48000);
    c.processStereo (s.l.data(), s.r.data(), (int) s.l.size());

    // Expected steady-state output: threshold + (in - threshold) / ratio.
    const double inDb       = dB (amp);
    const double expectedDb = -20.0 + (inDb - (-20.0)) / 4.0;
    REQUIRE (dB (settledPeak (s.l)) == Approx (expectedDb).margin (1.0));
    REQUIRE (c.gainReductionDb() < -1.0);         // it is actually compressing
}

TEST_CASE ("Compressor is stereo-linked (same gain on both channels)", "[compressor]")
{
    const double sr = 48000.0;
    Compressor c;
    c.setSampleRate (sr);
    c.setThreshold (-24.0);
    c.setRatio (6.0);
    c.setKnee (0.0);
    c.setAttack (0.002);
    c.setRelease (0.05);
    c.reset();

    const double ampL = 0.5, ampR = 0.1;          // very different levels
    auto s = makeStereoSine (ampL, ampR, 200.0, sr, 48000);
    c.processStereo (s.l.data(), s.r.data(), (int) s.l.size());

    // The applied gain (out/in) must be identical on both channels.
    const double gainL = settledPeak (s.l) / ampL;
    const double gainR = settledPeak (s.r) / ampR;
    REQUIRE (gainR == Approx (gainL).epsilon (0.02));
}

TEST_CASE ("Compressor ratio 1 is a true bypass", "[compressor]")
{
    const double sr = 48000.0;
    Compressor c;
    c.setSampleRate (sr);
    c.setThreshold (-40.0);
    c.setRatio (1.0);
    c.setMakeup (0.0);
    c.reset();

    auto s = makeStereoSine (0.7, 0.7, 200.0, sr, 8192);
    c.processStereo (s.l.data(), s.r.data(), (int) s.l.size());

    REQUIRE (settledPeak (s.l) == Approx (0.7f).epsilon (0.001));
}

TEST_CASE ("Compressor stays finite and bounded under extreme input", "[compressor]")
{
    const double sr = 48000.0;
    Compressor c;
    c.setSampleRate (sr);
    c.setThreshold (-30.0);
    c.setRatio (20.0);
    c.setMakeup (0.0);
    c.reset();

    auto s = makeStereoSine (10.0, 10.0, 200.0, sr, 16384);   // very hot
    c.processStereo (s.l.data(), s.r.data(), (int) s.l.size());

    REQUIRE_FALSE (hasBadValues (s.l));
    REQUIRE (settledPeak (s.l) < 2.0f);   // heavily tamed
}
