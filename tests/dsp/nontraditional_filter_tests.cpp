#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/PhaseDistortionResonator.h"
#include "dsp/CombFilter.h"
#include "dsp/AllpassDispersion.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/FrequencyResponse.h"

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>

using namespace pdhybrid;
using Catch::Approx;
using namespace harness;

namespace {

std::vector<float> whiteNoise (int n, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> buf (n);
    for (auto& s : buf) s = d (rng);
    return buf;
}

double bandEnergy (const Spectrum& s, double flo, double fhi)
{
    double e = 0.0;
    const std::size_t b0 = s.binOfFrequency (flo);
    const std::size_t b1 = s.binOfFrequency (fhi);
    for (std::size_t k = b0; k <= b1 && k < s.magnitude.size(); ++k)
        e += s.magnitude[k] * s.magnitude[k];
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// Phase-distortion resonator
// ---------------------------------------------------------------------------

TEST_CASE ("PD resonator peak tracks the resonant frequency", "[filter][pdreso]")
{
    const double sr = 48000.0;
    for (double fc : { 800.0, 1500.0 })
    {
        PhaseDistortionResonator r;
        r.setSampleRate (sr);
        r.setFrequency (fc);
        r.setResonance (0.9);
        r.setAmount (0.0);
        r.reset();

        auto buf = whiteNoise (32768, 3);
        r.processBlock (buf.data(), static_cast<int> (buf.size()));

        auto spec = computeSpectrum (buf, sr);
        REQUIRE_FALSE (hasBadValues (buf));
        REQUIRE (spec.peakFrequency() == Approx (fc).epsilon (0.15));
    }
}

TEST_CASE ("Phase distortion adds harmonics to the resonance", "[filter][pdreso]")
{
    const double sr = 48000.0, fc = 1000.0;

    auto harmonicRatio = [&] (double amount)
    {
        PhaseDistortionResonator r;
        r.setSampleRate (sr);
        r.setFrequency (fc);
        r.setResonance (0.9);
        r.setAmount (amount);
        r.reset();

        auto buf = whiteNoise (32768, 7);
        r.processBlock (buf.data(), static_cast<int> (buf.size()));
        auto spec = computeSpectrum (buf, sr);

        const double fund = bandEnergy (spec, fc - 150,     fc + 150);
        const double harm = bandEnergy (spec, 2 * fc - 150, 2 * fc + 150);
        return harm / (fund > 0.0 ? fund : 1.0);
    };

    const double clean     = harmonicRatio (0.0);
    const double distorted = harmonicRatio (0.85);
    REQUIRE (distorted > clean * 3.0);
}

TEST_CASE ("PD resonator stays finite under fuzz", "[filter][pdreso][stability]")
{
    const double sr = 48000.0;
    PhaseDistortionResonator r;
    r.setSampleRate (sr);

    std::mt19937 rng (11);
    std::uniform_real_distribution<double> fq (100.0, 12000.0), rs (0.0, 1.0),
                                           am (0.0, 1.0), in (-1.0, 1.0);
    std::vector<float> out;
    out.reserve (48000);
    for (int i = 0; i < 48000; ++i)
    {
        if ((i % 32) == 0) { r.setFrequency (fq (rng)); r.setResonance (rs (rng)); r.setAmount (am (rng)); }
        out.push_back (r.processSample (static_cast<float> (in (rng))));
    }
    REQUIRE_FALSE (hasBadValues (out));
    REQUIRE (peakAbs (out) < 100.0f);
}

// ---------------------------------------------------------------------------
// Comb / waveguide
// ---------------------------------------------------------------------------

TEST_CASE ("Comb resonates at integer multiples of its frequency", "[filter][comb]")
{
    const double sr = 48000.0, f0 = 500.0;
    CombFilter c;
    c.setSampleRate (sr);
    c.setFrequency (f0);
    c.setFeedback (0.9);
    c.setDamping (0.1);
    c.reset();

    auto buf = whiteNoise (32768, 5);
    c.processBlock (buf.data(), static_cast<int> (buf.size()));
    auto spec = computeSpectrum (buf, sr);

    const double atF0    = bandEnergy (spec, f0 - 25,      f0 + 25);
    const double at2F0   = bandEnergy (spec, 2 * f0 - 25,  2 * f0 + 25);
    const double between = bandEnergy (spec, 1.5 * f0 - 25, 1.5 * f0 + 25);

    REQUIRE (atF0  > between * 2.0);   // peak at fundamental
    REQUIRE (at2F0 > between * 2.0);   // and at the 2nd harmonic
}

TEST_CASE ("Comb resonance tracks its frequency setting", "[filter][comb]")
{
    const double sr = 48000.0, f0 = 300.0;
    CombFilter c;
    c.setSampleRate (sr);
    c.setFrequency (f0);
    c.setFeedback (0.9);
    c.setDamping (0.1);
    c.reset();

    auto buf = whiteNoise (32768, 9);
    c.processBlock (buf.data(), static_cast<int> (buf.size()));
    auto spec = computeSpectrum (buf, sr);

    const double atF0    = bandEnergy (spec, f0 - 20,       f0 + 20);
    const double between = bandEnergy (spec, 1.5 * f0 - 20, 1.5 * f0 + 20);
    REQUIRE (atF0 > between * 2.0);
}

// ---------------------------------------------------------------------------
// Allpass dispersion
// ---------------------------------------------------------------------------

TEST_CASE ("Allpass dispersion has flat unity magnitude", "[filter][allpass]")
{
    const double sr = 48000.0;
    AllpassDispersion ap;
    ap.setCoefficient (0.7);
    ap.setStages (4);

    for (double hz : { 100.0, 500.0, 1000.0, 4000.0, 10000.0 })
        REQUIRE (measureGainDb (ap, hz, sr) == Approx (0.0).margin (0.1));
}

TEST_CASE ("Non-traditional filters are block-size invariant", "[filter][invariance]")
{
    const double sr = 48000.0;
    const double twoPi = 6.283185307179586;

    auto input = [&] (std::size_t n)
    {
        std::vector<float> in (n);
        for (std::size_t i = 0; i < n; ++i)
            in[i] = static_cast<float> (std::sin (twoPi * 220.0 * static_cast<double> (i) / sr));
        return in;
    };

    auto runInBlocks = [&] (auto& filt, std::vector<float> buf, int block)
    {
        int i = 0;
        while (i < static_cast<int> (buf.size()))
        {
            const int n = std::min (block, static_cast<int> (buf.size()) - i);
            filt.processBlock (buf.data() + i, n);
            i += n;
        }
        return buf;
    };

    SECTION ("PD resonator")
    {
        PhaseDistortionResonator a, b;
        for (auto* r : { &a, &b }) { r->setSampleRate (sr); r->setFrequency (900.0); r->setResonance (0.7); r->setAmount (0.5); r->reset(); }
        REQUIRE (runInBlocks (a, input (8192), 64) == runInBlocks (b, input (8192), 512));
    }
    SECTION ("Comb")
    {
        CombFilter a, b;
        for (auto* c : { &a, &b }) { c->setSampleRate (sr); c->setFrequency (440.0); c->setFeedback (0.85); c->setDamping (0.2); c->reset(); }
        REQUIRE (runInBlocks (a, input (8192), 64) == runInBlocks (b, input (8192), 512));
    }
    SECTION ("Allpass")
    {
        AllpassDispersion a, b;
        for (auto* p : { &a, &b }) { p->setCoefficient (0.6); p->setStages (6); p->reset(); }
        REQUIRE (runInBlocks (a, input (8192), 64) == runInBlocks (b, input (8192), 512));
    }
}
