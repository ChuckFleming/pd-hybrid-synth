#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/PluckResonator.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"

#include <cmath>
#include <vector>
#include <cstdint>

using pdhybrid::PluckResonator;
using Catch::Approx;
using namespace harness;

namespace {

// Drive the resonator with a white-noise exciter (the resonator gates it to the
// burst window internally). If `trigger` is false the burst never fires.
std::vector<float> renderPluck (double freq, double decay, double damp, double disp,
                                double burstMs, double sr, int n, bool trigger = true)
{
    PluckResonator p;
    p.setSampleRate (sr);
    p.setFrequency (freq);
    p.setDecay (decay);
    p.setDamping (damp);
    p.setDispersion (disp);
    p.setBurstMs (burstMs);
    p.reset();
    if (trigger) p.trigger();

    std::uint32_t rng = 0x1234567u;
    std::vector<float> out (n);
    for (int i = 0; i < n; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        const float exc = static_cast<float> (static_cast<std::int32_t> (rng) / 2147483648.0);
        out[i] = p.processSample (exc);
    }
    return out;
}

std::vector<float> slice (const std::vector<float>& v, int start, int len)
{
    return { v.begin() + start, v.begin() + start + len };
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

TEST_CASE ("Pluck is silent until triggered", "[pluck]")
{
    auto buf = renderPluck (220.0, 0.8, 0.3, 0.0, 20.0, 48000.0, 8192, /*trigger=*/false);
    REQUIRE (rms (buf) < 1.0e-6f);
}

TEST_CASE ("Pluck sounds and rings on past the exciter burst", "[pluck]")
{
    const double sr = 48000.0;
    auto buf = renderPluck (220.0, 0.85, 0.2, 0.0, 20.0, sr, 24000);
    REQUIRE_FALSE (hasBadValues (buf));
    // White noise only couples to the comb's harmonic resonances, so this is a
    // conservative level (a real tuned osc exciter drives it far harder -- see
    // the engine integration test).
    REQUIRE (rms (buf) > 0.003f);
    // 20 ms burst ~ 960 samples; well past it there should still be a ring.
    REQUIRE (rms (slice (buf, 3000, 8000)) > 0.002f);
}

TEST_CASE ("Pluck rings at the tuned frequency", "[pluck]")
{
    const double sr = 48000.0;
    const int    n  = 32768;
    for (double freq : { 110.0, 220.0, 440.0 })
    {
        auto buf  = renderPluck (freq, 0.9, 0.15, 0.0, 15.0, sr, n);
        // Analyse the free ring, after the exciter burst has ended.
        auto spec = computeSpectrum (slice (buf, 2000, 16384), sr);
        const double ratio = spec.peakFrequency() / freq;
        REQUIRE (std::abs (ratio - std::round (ratio)) < 0.12);
        REQUIRE (std::round (ratio) >= 1.0);
    }
}

TEST_CASE ("Pluck decay controls the ring time", "[pluck]")
{
    const double sr = 48000.0;
    const int    n  = 32000;
    auto shortRing = renderPluck (220.0, 0.2, 0.3, 0.0, 20.0, sr, n);
    auto longRing  = renderPluck (220.0, 1.0, 0.3, 0.0, 20.0, sr, n);
    // Late in the note, a longer decay retains far more energy.
    REQUIRE (rms (slice (longRing, 20000, 8000)) > rms (slice (shortRing, 20000, 8000)) * 2.0f);
}

TEST_CASE ("Pluck damping darkens the ring", "[pluck]")
{
    const double sr = 48000.0;
    const int    n  = 16384;
    // Analyse the free ring (after the ~720-sample burst), not the broadband
    // exciter itself, so this measures the string's tone rather than the noise.
    auto ring = [&] (double damp)
    {
        auto buf = renderPluck (220.0, 0.9, damp, 0.0, 15.0, sr, n);
        return spectralCentroid (computeSpectrum (slice (buf, 800, 8000), sr));
    };
    REQUIRE (ring (0.95) < ring (0.05));   // more damping -> darker ring
}

TEST_CASE ("Pluck dispersion changes the tone", "[pluck]")
{
    const double sr = 48000.0;
    const int    n  = 16384;
    auto a = renderPluck (220.0, 0.9, 0.2, 0.0, 15.0, sr, n);
    auto b = renderPluck (220.0, 0.9, 0.2, 0.9, 15.0, sr, n);
    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (a[i] - b[i]);
    REQUIRE (diff > 1.0);
}

TEST_CASE ("Pluck stays finite and bounded across its range", "[pluck][stability]")
{
    for (double decay : { 0.0, 0.5, 1.0 })
        for (double damp : { 0.0, 0.5, 1.0 })
            for (double disp : { 0.0, 1.0 })
            {
                auto buf = renderPluck (330.0, decay, damp, disp, 50.0, 48000.0, 16384);
                REQUIRE_FALSE (hasBadValues (buf));
                REQUIRE (peakAbs (buf) <= 1.3f);
            }
}
