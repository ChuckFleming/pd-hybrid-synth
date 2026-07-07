#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/AnalogOscillator.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/Invariance.h"

#include <vector>
#include <cmath>

using namespace pdhybrid;
using Catch::Approx;
using namespace harness;

namespace {

std::vector<float> renderWave (AnalogWave wave, double freq, double sr, int n,
                               int blockSize, double pw = 0.5)
{
    AnalogOscillator osc;
    osc.setSampleRate (sr);
    osc.setFrequency (freq);
    osc.setWaveform (wave);
    osc.setPulseWidth (pw);
    osc.reset();
    return renderInBlocks ([&] (float* o, int m) { osc.processBlock (o, m); }, n, blockSize);
}

// Fraction of spectral energy NOT sitting on integer harmonics of f0 (aliasing).
double aliasFraction (const std::vector<float>& buf, double sr, double f0, int maxK)
{
    auto spec = computeSpectrum (buf, sr, /*hann*/ false);
    const double total = totalEnergy (spec);
    double harmonic = 0.0;
    for (int k = 1; k <= maxK && k * f0 < 0.5 * sr; ++k)
    {
        const double m = spec.magnitudeNearHz (k * f0, 1);
        harmonic += m * m;
    }
    return (total - harmonic) / (total > 0.0 ? total : 1.0);
}

} // namespace

TEST_CASE ("Analog oscillator tracks pitch for every waveform", "[analogosc]")
{
    const double sr = 48000.0, f = 440.0;
    for (auto w : { AnalogWave::Saw, AnalogWave::Square, AnalogWave::Triangle, AnalogWave::Pulse })
    {
        auto buf  = renderWave (w, f, sr, 16384, 512);
        auto spec = computeSpectrum (buf, sr);
        REQUIRE_FALSE (hasBadValues (buf));
        REQUIRE (spec.peakFrequency() == Approx (f).epsilon (0.02));
    }
}

TEST_CASE ("Saw has the full 1/n harmonic series", "[analogosc][saw]")
{
    const double sr = 48000.0;
    const int    N  = 16384;
    const double f0 = 256.0 * sr / N;   // exact bin

    auto spec = computeSpectrum (renderWave (AnalogWave::Saw, f0, sr, N, 512), sr, false);
    const double h1 = spec.magnitudeNearHz (f0);
    const double h2 = spec.magnitudeNearHz (2 * f0);
    const double h3 = spec.magnitudeNearHz (3 * f0);

    REQUIRE (h2 / h1 == Approx (0.5).margin (0.08));    // 1/2
    REQUIRE (h3 / h1 == Approx (1.0 / 3.0).margin (0.08)); // 1/3
}

TEST_CASE ("Square has odd harmonics only", "[analogosc][square]")
{
    const double sr = 48000.0;
    const int    N  = 16384;
    const double f0 = 256.0 * sr / N;

    auto spec = computeSpectrum (renderWave (AnalogWave::Square, f0, sr, N, 512), sr, false);
    const double h1 = spec.magnitudeNearHz (f0);
    const double h2 = spec.magnitudeNearHz (2 * f0);
    const double h3 = spec.magnitudeNearHz (3 * f0);

    REQUIRE (h3 / h1 == Approx (1.0 / 3.0).margin (0.08)); // odd present at 1/3
    REQUIRE (h2 / h1 < 0.03);                              // even absent
}

TEST_CASE ("PolyBLEP suppresses aliasing versus a naive saw", "[analogosc][aliasing]")
{
    const double sr = 48000.0;
    const int    N  = 16384;
    const double f0 = 2000.0 * sr / N;   // ~5859 Hz, exact bin, non-integer sr ratio

    // Naive (non-bandlimited) saw for comparison.
    std::vector<float> naive (N);
    double phase = 0.0;
    const double inc = f0 / sr;
    for (int i = 0; i < N; ++i) { naive[i] = static_cast<float> (2.0 * phase - 1.0); phase += inc; if (phase >= 1.0) phase -= 1.0; }

    const double aliasNaive = aliasFraction (naive, sr, f0, 4);
    const double aliasBlep  = aliasFraction (renderWave (AnalogWave::Saw, f0, sr, N, 512), sr, f0, 4);

    REQUIRE (aliasBlep < aliasNaive * 0.5);   // clearly better
    REQUIRE (aliasBlep < 0.10);               // and low in absolute terms
}

TEST_CASE ("Pulse width introduces even harmonics", "[analogosc][pwm]")
{
    const double sr = 48000.0;
    const int    N  = 16384;
    const double f0 = 256.0 * sr / N;

    auto even2 = [&] (double pw)
    {
        auto spec = computeSpectrum (renderWave (AnalogWave::Pulse, f0, sr, N, 512, pw), sr, false);
        return spec.magnitudeNearHz (2 * f0);
    };

    const double sym  = even2 (0.5);
    const double asym = even2 (0.30);
    REQUIRE (sym < 0.02);           // ~symmetric -> tiny 2nd harmonic
    REQUIRE (asym > sym + 0.05);    // asymmetry -> real 2nd harmonic
}

TEST_CASE ("Analog oscillator is block-size invariant", "[analogosc][invariance]")
{
    const double sr = 48000.0, f = 330.0;
    for (auto w : { AnalogWave::Saw, AnalogWave::Square, AnalogWave::Triangle, AnalogWave::Pulse })
    {
        auto a = renderWave (w, f, sr, 8192, 64, 0.4);
        auto b = renderWave (w, f, sr, 8192, 512, 0.4);
        REQUIRE (a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
            REQUIRE (a[i] == b[i]);
    }
}
