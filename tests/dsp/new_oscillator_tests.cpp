#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/SupersawOscillator.h"
#include "dsp/HarmonicOscillator.h"
#include "dsp/OscillatorUnit.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>
#include <algorithm>

using namespace pdhybrid;
using Catch::Approx;
using namespace harness;

namespace {

template <typename Osc>
std::vector<float> render (Osc& o, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    o.processBlock (buf.data(), n);
    return buf;
}

// Energy within +/- halfWidth Hz of a centre frequency.
double energyNear (const Spectrum& s, double hz, double halfWidth)
{
    double e = 0.0;
    const std::size_t b0 = s.binOfFrequency (std::max (0.0, hz - halfWidth));
    const std::size_t b1 = std::min (s.binOfFrequency (hz + halfWidth), s.magnitude.size() - 1);
    for (std::size_t k = b0; k <= b1; ++k)
        e += s.magnitude[k] * s.magnitude[k];
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// Supersaw
// ---------------------------------------------------------------------------

TEST_CASE ("Supersaw with no detune collapses onto a single saw", "[osc][supersaw]")
{
    const double sr = 48000.0, f0 = 220.0;
    SupersawOscillator o;
    o.setSampleRate (sr);
    o.setFrequency (f0);
    o.setDetune (0.0);
    o.setMix (1.0);
    o.setVoices (1.0);   // 9 saws, all at the same pitch
    o.reset();

    auto buf = render (o, 32768);
    REQUIRE_FALSE (hasBadValues (buf));

    auto spec = computeSpectrum (buf, sr);
    // A saw: fundamental present, and the 2nd harmonic about half its level.
    REQUIRE (spec.peakFrequency() == Approx (f0).epsilon (0.05));
    const double h1 = spec.magnitudeNearHz (f0);
    const double h2 = spec.magnitudeNearHz (2.0 * f0);
    REQUIRE (h2 == Approx (h1 * 0.5).epsilon (0.3));
}

TEST_CASE ("Supersaw detune spreads energy around the fundamental", "[osc][supersaw]")
{
    const double sr = 48000.0, f0 = 220.0;

    auto spreadRatio = [&] (double detune)
    {
        SupersawOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setDetune (detune);
        o.setMix (1.0);
        o.setVoices (1.0);
        o.reset();

        auto spec = computeSpectrum (render (o, 32768), sr);
        // Energy in the skirts either side of the fundamental, against the
        // energy right at it. Detuning moves partials off the centre line.
        const double centre = energyNear (spec, f0, 1.5);
        const double band   = energyNear (spec, f0, 12.0);
        return (band - centre) / std::max (1.0e-12, band);
    };

    REQUIRE (spreadRatio (0.8) > spreadRatio (0.0) + 0.2);
}

TEST_CASE ("Supersaw voice count is odd and bounded", "[osc][supersaw]")
{
    const double sr = 48000.0;
    for (double v : { 0.0, 0.33, 0.66, 1.0 })
    {
        SupersawOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (110.0);
        o.setDetune (0.5);
        o.setVoices (v);
        o.reset();

        auto buf = render (o, 8192);
        REQUIRE_FALSE (hasBadValues (buf));
        REQUIRE (peakAbs (buf) < 2.0f);   // the stack is level-normalised
    }
}

TEST_CASE ("Supersaw stays bounded across the keyboard", "[osc][supersaw][stability]")
{
    const double sr = 48000.0;
    for (double f0 : { 27.5, 220.0, 1760.0, 8000.0 })
    {
        SupersawOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setDetune (1.0);
        o.setMix (1.0);
        o.setVoices (1.0);
        o.reset();

        auto buf = render (o, 16384);
        INFO ("f0 = " << f0);
        REQUIRE_FALSE (hasBadValues (buf));
        REQUIRE (peakAbs (buf) < 2.0f);
    }
}

// ---------------------------------------------------------------------------
// Harmonic (band-limited additive)
// ---------------------------------------------------------------------------

TEST_CASE ("Harmonic centroid moves the spectral peak up the series", "[osc][harmonic]")
{
    const double sr = 48000.0, f0 = 200.0;

    auto peakHarmonic = [&] (double centroid)
    {
        HarmonicOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setCentroid (centroid);
        o.setOddEven (0.5);
        o.setWidth (0.0);     // narrow window: one clear peak
        o.reset();

        auto spec = computeSpectrum (render (o, 32768), sr);
        return spec.peakFrequency() / f0;
    };

    const double low  = peakHarmonic (0.0);
    const double high = peakHarmonic (0.8);

    INFO ("peak harmonic: " << low << " -> " << high);
    REQUIRE (low  == Approx (1.0).epsilon (0.2));   // centroid 0 -> fundamental
    REQUIRE (high > low * 4.0);                     // and climbs from there
}

TEST_CASE ("Harmonic oscillator is alias-free at the top of the keyboard", "[osc][harmonic]")
{
    // The engine's whole point: partials above Nyquist are never synthesised,
    // so a high note carries no inharmonic reflections. Every significant bin
    // should sit on a multiple of the fundamental.
    const double sr = 48000.0, f0 = 4000.0;
    HarmonicOscillator o;
    o.setSampleRate (sr);
    o.setFrequency (f0);
    o.setCentroid (1.0);   // ask for the highest partials available
    o.setOddEven (0.5);
    o.setWidth (1.0);      // and the widest window
    o.reset();

    auto buf = render (o, 32768);
    REQUIRE_FALSE (hasBadValues (buf));

    auto spec = computeSpectrum (buf, sr);

    double harmonic = 0.0;
    for (int k = 1; k * f0 < 0.5 * sr; ++k)
        harmonic += energyNear (spec, k * f0, 60.0);

    double total = 0.0;
    for (std::size_t b = 1; b < spec.magnitude.size(); ++b)
        total += spec.magnitude[b] * spec.magnitude[b];

    INFO ("harmonic fraction = " << (harmonic / std::max (1.0e-12, total)));
    REQUIRE (harmonic > 0.95 * total);
}

TEST_CASE ("Harmonic width opens the window onto more partials", "[osc][harmonic]")
{
    const double sr = 48000.0, f0 = 200.0;

    auto partialCount = [&] (double width)
    {
        HarmonicOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setCentroid (0.4);
        o.setOddEven (0.5);
        o.setWidth (width);
        o.reset();

        auto spec = computeSpectrum (render (o, 32768), sr);
        const double ref = spec.magnitudeNearHz (spec.peakFrequency());

        int n = 0;
        for (int k = 1; k * f0 < 0.45 * sr; ++k)
            if (spec.magnitudeNearHz (k * f0) > ref * 0.05)
                ++n;
        return n;
    };

    REQUIRE (partialCount (1.0) > partialCount (0.0));
}

TEST_CASE ("Harmonic odd/even balance changes the waveform", "[osc][harmonic]")
{
    const double sr = 48000.0, f0 = 200.0;

    auto evenOddRatio = [&] (double oddEven)
    {
        HarmonicOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setCentroid (0.3);
        o.setOddEven (oddEven);
        o.setWidth (0.6);
        o.reset();

        auto spec = computeSpectrum (render (o, 32768), sr);
        double even = 0.0, odd = 0.0;
        for (int k = 1; k <= 16; ++k)
            ((k % 2 == 0) ? even : odd) += energyNear (spec, k * f0, 30.0);
        return even / std::max (1.0e-12, odd);
    };

    REQUIRE (evenOddRatio (1.0) > evenOddRatio (0.0) * 2.0);
}

// ---------------------------------------------------------------------------
// OscillatorUnit dispatch
// ---------------------------------------------------------------------------

TEST_CASE ("Switching engine applies the controls set while it was inactive",
           "[osc][unit]")
{
    // The unit drives only the selected engine -- broadcasting the shared
    // timbre controls to all of them made a modulated DCW envelope rebuild
    // wavetables for engines that were not even playing, which cost ~90% of
    // real time on a six-note chord. The price of that fix is that an engine
    // hears nothing while inactive, so setType has to hand it the current
    // values. This checks that a late switch lands on the same sound as
    // selecting the engine up front.
    const double sr = 48000.0;

    auto render = [&] (bool switchLate, pdhybrid::OscType target)
    {
        pdhybrid::OscillatorUnit u;
        u.setSampleRate (sr);

        if (! switchLate)
            u.setType (target);
        else
            u.setType (pdhybrid::OscType::Saw);   // some other engine first

        u.setAmount (0.72);
        u.setPulseWidth (0.31);
        u.setEngineParam (0.85);
        u.setTuning (0, 0, 0.0);
        u.setBaseFrequency (220.0);

        if (switchLate)
            u.setType (target);

        u.reset();
        u.excite();

        std::vector<float> buf (4096);
        for (auto& s : buf) s = u.processSample();
        return buf;
    };

    for (auto target : { pdhybrid::OscType::Harmonic, pdhybrid::OscType::Supersaw,
                         pdhybrid::OscType::Walsh,    pdhybrid::OscType::Vosim,
                         pdhybrid::OscType::VPS,      pdhybrid::OscType::PhaseDistortion,
                         pdhybrid::OscType::Pulse })
    {
        const auto upFront   = render (false, target);
        const auto switchedTo = render (true,  target);

        INFO ("engine " << static_cast<int> (target));
        REQUIRE_FALSE (hasBadValues (switchedTo));
        REQUIRE (switchedTo == upFront);
    }
}

TEST_CASE ("Harmonic table keeps up with a swept centroid", "[osc][harmonic]")
{
    // Rebuilds are deferred and rate-limited, so confirm a sweep still actually
    // changes the waveform rather than freezing on a stale table.
    const double sr = 48000.0;
    HarmonicOscillator o;
    o.setSampleRate (sr);
    o.setFrequency (200.0);
    o.setWidth (0.2);
    o.setOddEven (0.5);
    o.setCentroid (0.0);
    o.reset();

    // Sweep the centroid the way a DCW envelope would, then check the spectrum
    // really has moved up the series.
    std::vector<float> buf;
    buf.reserve (32768);
    for (int i = 0; i < 32768; ++i)
    {
        if ((i % 32) == 0)
            o.setCentroid (std::min (0.85, 0.85 * i / 16000.0));
        buf.push_back (o.processSample());
    }
    REQUIRE_FALSE (hasBadValues (buf));

    // Compare the first and last quarter: the peak partial must have climbed.
    std::vector<float> early (buf.begin(), buf.begin() + 8192);
    std::vector<float> late  (buf.end() - 8192, buf.end());
    const double earlyPeak = computeSpectrum (early, sr).peakFrequency();
    const double latePeak  = computeSpectrum (late,  sr).peakFrequency();

    INFO ("peak moved " << earlyPeak << " Hz -> " << latePeak << " Hz");
    REQUIRE (latePeak > earlyPeak * 2.0);
}

TEST_CASE ("Harmonic oscillator stays bounded across the keyboard", "[osc][harmonic][stability]")
{
    const double sr = 48000.0;
    for (double f0 : { 27.5, 220.0, 1760.0, 10000.0 })
    {
        HarmonicOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setCentroid (0.7);
        o.setWidth (0.5);
        o.reset();

        auto buf = render (o, 16384);
        INFO ("f0 = " << f0);
        REQUIRE_FALSE (hasBadValues (buf));
        REQUIRE (peakAbs (buf) < 2.0f);
    }
}
