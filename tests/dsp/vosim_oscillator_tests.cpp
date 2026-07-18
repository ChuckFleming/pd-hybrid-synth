#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/VosimOscillator.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/Invariance.h"

#include <cmath>
#include <vector>
#include <numeric>

using pdhybrid::VosimOscillator;
using Catch::Approx;
using namespace harness;

namespace {

std::vector<float> renderVosim (double freqHz, double formant01, double decay01,
                                double sampleRate, int numSamples, int blockSize)
{
    VosimOscillator osc;
    osc.setSampleRate (sampleRate);
    osc.setFrequency (freqHz);
    osc.setFormant (formant01);
    osc.setDecay (decay01);
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

TEST_CASE ("VOSIM produces sound", "[oscillator][vosim]")
{
    auto buf = renderVosim (220.0, 0.4, 0.6, 48000.0, 16384, 256);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01f);
}

static float maxStep (const std::vector<float>& v)
{
    float m = 0.0f;
    for (std::size_t i = 1; i < v.size(); ++i)
        m = std::max (m, std::abs (v[i] - v[i - 1]));
    return m;
}

TEST_CASE ("VOSIM stays click-free while the formant is modulated", "[oscillator][vosim]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n = 48000, block = 64;

    VosimOscillator osc;
    osc.setSampleRate (sr); osc.setFrequency (freq); osc.setDecay (0.7);
    osc.setFormant (0.35); osc.reset();

    std::vector<float> held, moved;
    for (int i = 0; i < n; ++i) held.push_back (osc.processSample());   // formant held

    osc.setFormant (0.35); osc.reset();
    for (int i = 0; i < n; i += block)                                  // formant wandered
    {
        const double f = 0.35 + 0.15 * std::sin (2.0 * 3.14159265 * 3.0 * i / n);
        osc.setFormant (f);
        for (int k = 0; k < block && i + k < n; ++k) moved.push_back (osc.processSample());
    }

    const float heldStep  = maxStep (held);
    const float movedStep = maxStep (moved);
    CAPTURE (heldStep, movedStep);
    // Modulating the formant must not introduce jumps far larger than the held tone.
    REQUIRE (movedStep < heldStep * 2.0f + 0.05f);
}

TEST_CASE ("VOSIM is periodic at the fundamental", "[oscillator][vosim]")
{
    const double sr = 48000.0;
    const int    n  = 16384;
    for (double freq : { 110.0, 220.0, 440.0 })
    {
        auto spec = computeSpectrum (renderVosim (freq, 0.5, 0.7, sr, n, 512), sr);
        // The burst repeats every fundamental period, so all energy lands on
        // harmonics of f0 -- the (formant-dominated) peak is one of them.
        const double ratio = spec.peakFrequency() / freq;
        REQUIRE (std::abs (ratio - std::round (ratio)) < 0.15);
        REQUIRE (std::round (ratio) >= 1.0);
        REQUIRE (spec.magnitudeNearHz (freq) > 0.0);   // fundamental present
    }
}

TEST_CASE ("VOSIM formant control shifts the spectrum up", "[oscillator][vosim]")
{
    const double sr = 48000.0, freq = 150.0;
    const int    n  = 16384;
    const double lo = spectralCentroid (computeSpectrum (renderVosim (freq, 0.2, 0.7, sr, n, 512), sr));
    const double hi = spectralCentroid (computeSpectrum (renderVosim (freq, 0.8, 0.7, sr, n, 512), sr));
    REQUIRE (hi > lo * 1.3);
}

TEST_CASE ("VOSIM decay control changes the tone", "[oscillator][vosim]")
{
    const double sr = 48000.0, freq = 150.0;
    const int    n  = 16384;
    auto a = renderVosim (freq, 0.5, 0.1, sr, n, 512);
    auto b = renderVosim (freq, 0.5, 0.9, sr, n, 512);
    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (a[i] - b[i]);
    REQUIRE (diff > 1.0);
}

TEST_CASE ("VOSIM output has no DC offset", "[oscillator][vosim]")
{
    auto buf = renderVosim (220.0, 0.4, 0.8, 48000.0, 16384, 256);
    // Skip the DC-blocker settling transient, then check the mean is ~0.
    double mean = std::accumulate (buf.begin() + 4000, buf.end(), 0.0) / (buf.size() - 4000);
    REQUIRE (std::abs (mean) < 0.01);
}

TEST_CASE ("VOSIM stays finite and bounded across its range", "[oscillator][vosim][stability]")
{
    for (double formant : { 0.0, 0.5, 1.0 })
        for (double decay : { 0.0, 0.5, 1.0 })
        {
            auto buf = renderVosim (330.0, formant, decay, 48000.0, 8192, 256);
            REQUIRE_FALSE (hasBadValues (buf));
            REQUIRE (peakAbs (buf) <= 1.1f);
        }
}

TEST_CASE ("VOSIM phase-mod input is a no-op at zero offset", "[oscillator][vosim][crossmod]")
{
    const double sr = 48000.0;
    auto a = renderVosim (330.0, 0.5, 0.6, sr, 4096, 128);

    VosimOscillator osc;
    osc.setSampleRate (sr); osc.setFrequency (330.0);
    osc.setFormant (0.5); osc.setDecay (0.6); osc.reset();
    std::vector<float> b (4096);
    for (int i = 0; i < 4096; ++i) { osc.setPhaseMod (0.0); b[i] = osc.processSample(); }

    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);
}

TEST_CASE ("VOSIM pulse count changes the tone", "[oscillator][vosim]")
{
    const double sr = 48000.0, freq = 150.0;
    const int    n  = 16384;
    auto renderN = [&] (int pulses)
    {
        VosimOscillator osc;
        osc.setSampleRate (sr); osc.setFrequency (freq);
        osc.setFormant (0.5); osc.setDecay (0.85); osc.setPulseCount (pulses);
        osc.reset();
        std::vector<float> b (n);
        osc.processBlock (b.data(), n);
        return b;
    };
    auto few  = renderN (1);
    auto many = renderN (6);
    REQUIRE_FALSE (hasBadValues (many));
    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (few[i] - many[i]);
    REQUIRE (diff > 1.0);   // pulse count audibly reshapes the burst
}

TEST_CASE ("VOSIM oscillator is block-size invariant", "[oscillator][vosim][invariance]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 8192;
    auto a = renderVosim (freq, 0.5, 0.6, sr, n, 64);
    auto b = renderVosim (freq, 0.5, 0.6, sr, n, 512);
    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);
}
