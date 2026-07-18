#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/WalshOscillator.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/Invariance.h"

#include <cmath>
#include <vector>

using pdhybrid::WalshOscillator;
using Catch::Approx;
using namespace harness;

namespace {

std::vector<float> renderWalsh (double freqHz, double tilt, double oddness,
                                double sampleRate, int numSamples, int blockSize)
{
    WalshOscillator osc;
    osc.setSampleRate (sampleRate);
    osc.setFrequency (freqHz);
    osc.setTilt (tilt);
    osc.setOddness (oddness);
    osc.reset();
    return renderInBlocks ([&] (float* out, int n) { osc.processBlock (out, n); },
                           numSamples, blockSize);
}

double spectralCentroid (const Spectrum& s)
{
    double num = 0.0, den = 0.0;
    for (std::size_t b = 1; b < s.magnitude.size(); ++b)
    {
        num += s.frequencyOfBin (b) * s.magnitude[b];
        den += s.magnitude[b];
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace

TEST_CASE ("Walsh produces sound", "[oscillator][walsh]")
{
    auto buf = renderWalsh (220.0, 0.4, 0.5, 48000.0, 16384, 256);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01f);
}

TEST_CASE ("Walsh is periodic at the fundamental", "[oscillator][walsh]")
{
    const double sr = 48000.0;
    const int    n  = 16384;
    for (double freq : { 110.0, 220.0, 440.0 })
    {
        auto spec = computeSpectrum (renderWalsh (freq, 0.3, 0.5, sr, n, 512), sr);
        // A single table cycle = one period, so energy lands on harmonics of f0.
        const double ratio = spec.peakFrequency() / freq;
        REQUIRE (std::abs (ratio - std::round (ratio)) < 0.15);
        REQUIRE (std::round (ratio) >= 1.0);
    }
}

TEST_CASE ("Walsh tilt raises the brightness", "[oscillator][walsh]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;
    // Higher tilt flattens the sequency slope -> more high-sequency energy.
    const double lo = spectralCentroid (computeSpectrum (renderWalsh (freq, 0.1, 0.5, sr, n, 512), sr));
    const double hi = spectralCentroid (computeSpectrum (renderWalsh (freq, 0.9, 0.5, sr, n, 512), sr));
    REQUIRE (hi > lo * 1.3);
}

TEST_CASE ("Walsh oddness changes the tone", "[oscillator][walsh]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;
    auto a = renderWalsh (freq, 0.5, 0.1, sr, n, 512);
    auto b = renderWalsh (freq, 0.5, 0.9, sr, n, 512);
    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (a[i] - b[i]);
    REQUIRE (diff > 1.0);
}

TEST_CASE ("Walsh stays finite and bounded across its range", "[oscillator][walsh][stability]")
{
    for (double tilt : { 0.0, 0.5, 1.0 })
        for (double odd : { 0.0, 0.5, 1.0 })
        {
            auto buf = renderWalsh (330.0, tilt, odd, 48000.0, 8192, 256);
            REQUIRE_FALSE (hasBadValues (buf));
            REQUIRE (peakAbs (buf) <= 1.1f);
        }
}

TEST_CASE ("Walsh phase-mod input is a no-op at zero offset", "[oscillator][walsh][crossmod]")
{
    const double sr = 48000.0;
    auto a = renderWalsh (330.0, 0.5, 0.5, sr, 4096, 128);

    WalshOscillator osc;
    osc.setSampleRate (sr); osc.setFrequency (330.0);
    osc.setTilt (0.5); osc.setOddness (0.5); osc.reset();
    std::vector<float> b (4096);
    for (int i = 0; i < 4096; ++i) { osc.setPhaseMod (0.0); b[i] = osc.processSample(); }

    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);
}

TEST_CASE ("Walsh fold changes the tone", "[oscillator][walsh]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;
    auto renderFold = [&] (double fold)
    {
        WalshOscillator osc;
        osc.setSampleRate (sr); osc.setFrequency (freq);
        osc.setTilt (0.5); osc.setOddness (0.5); osc.setFold (fold);
        osc.reset();
        std::vector<float> b (n);
        osc.processBlock (b.data(), n);
        return b;
    };
    auto flat   = renderFold (0.0);
    auto folded = renderFold (0.9);
    REQUIRE_FALSE (hasBadValues (folded));
    REQUIRE (peakAbs (folded) <= 1.1f);
    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (flat[i] - folded[i]);
    REQUIRE (diff > 1.0);
}

TEST_CASE ("Walsh oscillator is block-size invariant", "[oscillator][walsh][invariance]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 8192;
    auto a = renderWalsh (freq, 0.5, 0.5, sr, n, 64);
    auto b = renderWalsh (freq, 0.5, 0.5, sr, n, 512);
    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);
}
