#include <catch2/catch_test_macros.hpp>

#include "dsp/PhaseDistortionOscillator.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/Invariance.h"

#include <cmath>
#include <vector>

using pdhybrid::PhaseDistortionOscillator;
using namespace harness;

namespace {

std::vector<float> renderOsc (double freqHz, double amount, double sampleRate,
                              int numSamples, int blockSize)
{
    PhaseDistortionOscillator osc;
    osc.setSampleRate (sampleRate);
    osc.setFrequency (freqHz);
    osc.setAmount (amount);
    osc.reset();
    return renderInBlocks ([&] (float* out, int n) { osc.processBlock (out, n); },
                           numSamples, blockSize);
}

} // namespace

TEST_CASE ("PD oscillator at amount 0 is a near-pure sine", "[oscillator][pd]")
{
    const double sr = 48000.0, freq = 440.0;
    const int    n  = 16384;

    auto buf = renderOsc (freq, 0.0, sr, n, 512);
    REQUIRE_FALSE (hasBadValues (buf));

    auto spec = computeSpectrum (buf, sr);
    REQUIRE (totalHarmonicDistortion (spec, freq) < 0.02); // < 2% THD
}

TEST_CASE ("PD oscillator fundamental tracks the set frequency", "[oscillator][pd]")
{
    const double sr = 48000.0;
    const int    n  = 16384;

    for (double freq : { 110.0, 220.0, 440.0, 880.0 })
    {
        auto spec     = computeSpectrum (renderOsc (freq, 0.3, sr, n, 512), sr);
        auto measured = spec.peakFrequency();
        REQUIRE (std::abs (measured - freq) < 1.0); // within 1 Hz
    }
}

TEST_CASE ("PD distortion amount increases harmonic content", "[oscillator][pd]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;

    auto thdAt = [&] (double amount)
    {
        return totalHarmonicDistortion (computeSpectrum (renderOsc (freq, amount, sr, n, 512), sr),
                                        freq);
    };

    const double t0 = thdAt (0.0);
    const double t1 = thdAt (0.40);
    const double t2 = thdAt (0.85);

    REQUIRE (t0 < t1);
    REQUIRE (t1 < t2);
}

TEST_CASE ("PD oscillator output stays bounded at full distortion", "[oscillator][pd]")
{
    auto buf = renderOsc (440.0, 1.0, 48000.0, 8192, 128);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (peakAbs (buf) <= 1.001f);
}

TEST_CASE ("PD oscillator is block-size invariant", "[oscillator][invariance]")
{
    const double sr = 48000.0, freq = 330.0, amount = 0.6;
    const int    n  = 8192;

    auto a = renderOsc (freq, amount, sr, n, 64);
    auto b = renderOsc (freq, amount, sr, n, 512);

    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]); // bit-identical
}
