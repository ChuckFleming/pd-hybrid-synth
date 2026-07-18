#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/ScannedOscillator.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/Invariance.h"

#include <cmath>
#include <vector>
#include <numeric>

using pdhybrid::ScannedOscillator;
using Catch::Approx;
using namespace harness;

namespace {

std::vector<float> renderScanned (double freqHz, double stiffness, double damping,
                                  double sampleRate, int numSamples, int blockSize,
                                  bool exciteFirst = true)
{
    ScannedOscillator osc;
    osc.setSampleRate (sampleRate);
    osc.setFrequency (freqHz);
    osc.setStiffness (stiffness);
    osc.setDamping (damping);
    osc.reset();
    if (exciteFirst) osc.excite();
    return renderInBlocks ([&] (float* out, int n) { osc.processBlock (out, n); },
                           numSamples, blockSize);
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

// L1 distance between two sum-normalised magnitude spectra (0 = identical, up
// to 2 = no overlap): a scale-independent "how different is the timbre" measure.
double spectralDistance (const std::vector<float>& a, const std::vector<float>& b, double sr)
{
    auto sa = computeSpectrum (a, sr);
    auto sb = computeSpectrum (b, sr);
    const double na = std::accumulate (sa.magnitude.begin(), sa.magnitude.end(), 0.0);
    const double nb = std::accumulate (sb.magnitude.begin(), sb.magnitude.end(), 0.0);
    if (na <= 0.0 || nb <= 0.0) return 0.0;
    double d = 0.0;
    const std::size_t n = std::min (sa.magnitude.size(), sb.magnitude.size());
    for (std::size_t i = 0; i < n; ++i)
        d += std::abs (sa.magnitude[i] / na - sb.magnitude[i] / nb);
    return d;
}

} // namespace

TEST_CASE ("Scanned ring is silent until it is plucked", "[oscillator][scanned]")
{
    auto buf = renderScanned (220.0, 0.5, 0.3, 48000.0, 8192, 256, /*exciteFirst=*/false);
    REQUIRE (rms (buf) < 1.0e-6f);
}

TEST_CASE ("Scanned oscillator sounds after a pluck", "[oscillator][scanned]")
{
    auto buf = renderScanned (220.0, 0.5, 0.2, 48000.0, 16384, 256);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01f);
}

TEST_CASE ("Scanned oscillator plays at the scan frequency", "[oscillator][scanned]")
{
    const double sr = 48000.0;
    const int    n  = 16384;
    for (double freq : { 110.0, 220.0, 440.0 })
    {
        // Low damping so the tone holds steady over the measurement window. The
        // peak lands on the fundamental (not the octave), within one FFT bin
        // (48000/16384 ~ 2.9 Hz).
        auto spec = computeSpectrum (renderScanned (freq, 0.4, 0.05, sr, n, 512), sr);
        REQUIRE (std::abs (spec.peakFrequency() - freq) < 4.0);
    }
}

TEST_CASE ("Scanned timbre evolves over the note", "[oscillator][scanned]")
{
    const double sr = 48000.0;
    const int    n  = 48000;   // ~1 s
    // Low damping so the modes keep morphing the shape.
    auto buf = renderScanned (220.0, 0.5, 0.05, sr, n, 256);
    REQUIRE_FALSE (hasBadValues (buf));

    auto early = slice (buf, 2000, 8192);
    auto late  = slice (buf, n - 8192, 8192);
    // The shape is a living wavetable; its spectrum should move noticeably.
    REQUIRE (spectralDistance (early, late, sr) > 0.15);
}

TEST_CASE ("Scanned damping bleeds energy away", "[oscillator][scanned]")
{
    const double sr = 48000.0;
    const int    n  = 48000;
    auto buf = renderScanned (220.0, 0.4, 0.95, sr, n, 256);   // heavy damping
    REQUIRE_FALSE (hasBadValues (buf));

    const float early = rms (slice (buf, 2000, 8192));
    const float late  = rms (slice (buf, n - 8192, 8192));
    REQUIRE (late < early * 0.5f);   // clearly decaying
}

TEST_CASE ("Scanned stiffness controls brightness", "[oscillator][scanned]")
{
    const double sr = 48000.0;
    const int    n  = 8192;   // early window, before much decay
    const double lo = spectralCentroid (computeSpectrum (renderScanned (220.0, 0.05, 0.1, sr, n, 256), sr));
    const double hi = spectralCentroid (computeSpectrum (renderScanned (220.0, 0.95, 0.1, sr, n, 256), sr));
    REQUIRE (hi > lo * 1.1);
}

TEST_CASE ("Scanned oscillator stays finite and bounded across its range", "[oscillator][scanned][stability]")
{
    for (double stiff : { 0.0, 0.5, 1.0 })
        for (double damp : { 0.0, 0.5, 1.0 })
        {
            auto buf = renderScanned (330.0, stiff, damp, 48000.0, 96000, 512);   // 2 s
            REQUIRE_FALSE (hasBadValues (buf));
            REQUIRE (peakAbs (buf) <= 1.2f);
        }
}

TEST_CASE ("Scanned phase-mod input is a no-op at zero offset", "[oscillator][scanned][crossmod]")
{
    const double sr = 48000.0;
    auto a = renderScanned (330.0, 0.5, 0.2, sr, 4096, 128);

    ScannedOscillator osc;
    osc.setSampleRate (sr); osc.setFrequency (330.0);
    osc.setStiffness (0.5); osc.setDamping (0.2); osc.reset(); osc.excite();
    std::vector<float> b (4096);
    for (int i = 0; i < 4096; ++i) { osc.setPhaseMod (0.0); b[i] = osc.processSample(); }

    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);
}

TEST_CASE ("Scanned excite shape changes the attack timbre", "[oscillator][scanned]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;
    auto renderShape = [&] (int shape)
    {
        ScannedOscillator osc;
        osc.setSampleRate (sr); osc.setFrequency (freq);
        osc.setStiffness (0.5); osc.setDamping (0.1); osc.setExciteShape (shape);
        osc.reset(); osc.excite();
        std::vector<float> b (n);
        osc.processBlock (b.data(), n);
        return b;
    };
    auto pluck  = renderShape (0);
    auto noise  = renderShape (2);
    REQUIRE_FALSE (hasBadValues (noise));
    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (pluck[i] - noise[i]);
    REQUIRE (diff > 1.0);
}

TEST_CASE ("Scanned morph rate changes the evolution", "[oscillator][scanned]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 24000;
    auto renderRate = [&] (double rate)
    {
        ScannedOscillator osc;
        osc.setSampleRate (sr); osc.setFrequency (freq);
        osc.setStiffness (0.5); osc.setDamping (0.03); osc.setMorphRate (rate);
        osc.reset(); osc.excite();
        return renderInBlocks ([&] (float* o, int m) { osc.processBlock (o, m); }, n, 256);
    };
    // The physics update rate is a real, audible parameter: slow vs fast morph
    // drive the ring to different states, so the output differs.
    auto slow = renderRate (0.05);
    auto fast = renderRate (0.95);
    REQUIRE_FALSE (hasBadValues (fast));
    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (slow[i] - fast[i]);
    REQUIRE (diff > 1.0);
}

TEST_CASE ("Scanned oscillator is block-size invariant", "[oscillator][scanned][invariance]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 8192;

    auto a = renderScanned (freq, 0.5, 0.2, sr, n, 64);
    auto b = renderScanned (freq, 0.5, 0.2, sr, n, 512);

    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);
}
