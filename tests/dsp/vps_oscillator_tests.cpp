#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/VpsOscillator.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/Invariance.h"

#include <cmath>
#include <vector>

using pdhybrid::VpsOscillator;
using Catch::Approx;
using namespace harness;

namespace {

std::vector<float> renderVps (double freqHz, double d, double v, double sampleRate,
                              int numSamples, int blockSize)
{
    VpsOscillator osc;
    osc.setSampleRate (sampleRate);
    osc.setFrequency (freqHz);
    osc.setHorizontal (d);
    osc.setVertical (v);
    osc.reset();
    return renderInBlocks ([&] (float* out, int n) { osc.processBlock (out, n); },
                           numSamples, blockSize);
}

// Magnitude-weighted mean frequency (spectral centroid) -- a robust "brightness"
// measure that tracks the moving VPS formant.
double spectralCentroid (const Spectrum& s)
{
    double num = 0.0, den = 0.0;
    for (std::size_t b = 1; b < s.magnitude.size(); ++b)   // skip DC
    {
        num += s.frequencyOfBin (b) * s.magnitude[b];
        den += s.magnitude[b];
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace

TEST_CASE ("VPS at the neutral point (d=0.5, v=0.5) is a near-pure tone", "[oscillator][vps]")
{
    const double sr = 48000.0, freq = 440.0;
    const int    n  = 16384;

    auto buf = renderVps (freq, 0.5, 0.5, sr, n, 512);
    REQUIRE_FALSE (hasBadValues (buf));

    // f(p) = p -> a plain cosine, so harmonic content is negligible.
    auto spec = computeSpectrum (buf, sr);
    REQUIRE (totalHarmonicDistortion (spec, freq) < 0.02);   // < 2% THD
}

TEST_CASE ("VPS fundamental tracks the set frequency", "[oscillator][vps]")
{
    const double sr = 48000.0;
    const int    n  = 16384;

    // Mild bend so the fundamental stays the dominant partial.
    for (double freq : { 110.0, 220.0, 440.0, 880.0 })
    {
        auto spec = computeSpectrum (renderVps (freq, 0.5, 0.65, sr, n, 512), sr);
        REQUIRE (std::abs (spec.peakFrequency() - freq) < 1.0);   // within 1 Hz
    }
}

TEST_CASE ("VPS vertical coordinate sweeps the formant brighter", "[oscillator][vps]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;

    // Pushing the inflection point higher packs more cosine cycles per period,
    // moving the spectral centroid (the formant peak) up.
    const double lo = spectralCentroid (computeSpectrum (renderVps (freq, 0.5, 0.55, sr, n, 512), sr));
    const double hi = spectralCentroid (computeSpectrum (renderVps (freq, 0.5, 2.5,  sr, n, 512), sr));
    REQUIRE (hi > lo * 1.5);
}

TEST_CASE ("VPS horizontal coordinate changes the tone", "[oscillator][vps]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;

    auto a = renderVps (freq, 0.30, 1.5, sr, n, 512);
    auto b = renderVps (freq, 0.70, 1.5, sr, n, 512);

    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (a[i] - b[i]);
    REQUIRE (diff > 1.0);   // moving the inflection X audibly changes the wave
}

TEST_CASE ("VPS stays finite and bounded across the point range", "[oscillator][vps]")
{
    for (double d : { 0.1, 0.3, 0.5, 0.7, 0.9 })
        for (double v : { 0.0, 0.5, 1.0, 2.0, 4.0 })
        {
            auto buf = renderVps (220.0, d, v, 48000.0, 8192, 256);
            REQUIRE_FALSE (hasBadValues (buf));
            // Core cosine is bounded by 1; the decimation FIR adds intersample
            // ringing on the steep phase-shaping edges. At the most aggressive
            // corner (small d, large v -> phase slope ~40) that overshoot reaches
            // ~1.09 -- benign per-oscillator ringing the master limiter catches.
            REQUIRE (peakAbs (buf) <= 1.10f);
        }
}

TEST_CASE ("VPS oversampling keeps the aliasing floor low on a bright formant", "[oscillator][vps][aliasing]")
{
    // Exact-bin fundamental so harmonics land on bins and any aliases fold
    // off-grid (same technique as the PD/analog aliasing tests).
    const double sr = 48000.0;
    const int    n  = 16384;
    const int    k0 = 683;                     // fundamental bin (~2000 Hz)
    const double f0 = k0 * sr / n;

    auto buf  = renderVps (f0, 0.5, 2.5, sr, n, 512);
    auto spec = computeSpectrum (buf, sr, /*hann=*/false);

    double harmonic = 0.0, alias = 0.0;
    for (std::size_t b = 1; b < spec.magnitude.size(); ++b)
    {
        const double e = spec.magnitude[b] * spec.magnitude[b];
        if (b % static_cast<std::size_t> (k0) == 0) harmonic += e;
        else                                        alias    += e;
    }
    REQUIRE (alias / (harmonic + alias) < 0.05);
}

TEST_CASE ("VPS phase-mod input is a no-op at zero offset", "[oscillator][vps][crossmod]")
{
    const double sr = 48000.0;
    auto a = renderVps (330.0, 0.5, 1.2, sr, 4096, 128);

    VpsOscillator osc;
    osc.setSampleRate (sr); osc.setFrequency (330.0);
    osc.setHorizontal (0.5); osc.setVertical (1.2); osc.reset();
    std::vector<float> b (4096);
    for (int i = 0; i < 4096; ++i) { osc.setPhaseMod (0.0); b[i] = osc.processSample(); }

    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);   // bit-identical when PM offset is 0
}

TEST_CASE ("VPS hard sync locks the slave to the master period", "[oscillator][vps][crossmod]")
{
    const double sr = 48000.0;
    const double fMaster = 220.0, fSlave = 337.0;

    VpsOscillator master, slave;
    for (auto* o : { &master, &slave })
    {
        o->setSampleRate (sr); o->setHorizontal (0.5); o->setVertical (1.0); o->reset();
    }
    master.setFrequency (fMaster);
    slave.setFrequency (fSlave);

    const int n = 16384;
    std::vector<float> synced (n);
    for (int i = 0; i < n; ++i)
    {
        (void) master.processSample();
        if (master.wrapped())
            slave.syncReset();
        synced[i] = slave.processSample();
    }

    REQUIRE_FALSE (hasBadValues (synced));
    auto spec = computeSpectrum (synced, sr);
    const double ratio = spec.peakFrequency() / fMaster;
    REQUIRE (std::abs (ratio - std::round (ratio)) < 0.12);   // peak is a master harmonic
    REQUIRE (std::round (ratio) >= 1.0);
}

TEST_CASE ("VPS oscillator is block-size invariant", "[oscillator][vps][invariance]")
{
    const double sr = 48000.0, freq = 330.0;
    const int    n  = 8192;

    auto a = renderVps (freq, 0.4, 1.6, sr, n, 64);
    auto b = renderVps (freq, 0.4, 1.6, sr, n, 512);

    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);   // bit-identical
}
