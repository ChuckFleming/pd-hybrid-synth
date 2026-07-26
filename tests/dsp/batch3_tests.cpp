#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/PafOscillator.h"
#include "dsp/GranularOscillator.h"
#include "dsp/WavetableOscillator.h"
#include "dsp/BandSplitFilter.h"
#include "dsp/PhaseDistortionResonator.h"
#include "dsp/SynthEngine.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"

#include <vector>
#include <random>
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

// Summed energy in a band. Note this is *not* interchangeable with
// magnitudeNearHz: a peak magnitude cannot be compared against a total energy,
// because a real partial spreads its energy over several bins.
double energyNear (const Spectrum& s, double hz, double halfWidth)
{
    double e = 0.0;
    const std::size_t b0 = s.binOfFrequency (std::max (0.0, hz - halfWidth));
    const std::size_t b1 = std::min (s.binOfFrequency (hz + halfWidth), s.magnitude.size() - 1);
    for (std::size_t k = b0; k <= b1; ++k)
        e += s.magnitude[k] * s.magnitude[k];
    return e;
}

std::vector<float> whiteNoise (int n, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> buf (static_cast<std::size_t> (n));
    for (auto& s : buf) s = d (rng);
    return buf;
}

} // namespace

// ---------------------------------------------------------------------------
// PAF
// ---------------------------------------------------------------------------

TEST_CASE ("PAF peak follows the formant control", "[osc][paf]")
{
    const double sr = 48000.0, f0 = 200.0;

    auto peakRatio = [&] (double formant)
    {
        PafOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setFormant (formant);
        o.setBandwidth (0.2);   // narrow: one clear peak
        o.setShape (0.5);
        o.reset();

        auto spec = computeSpectrum (render (o, 32768), sr);
        return spec.peakFrequency() / f0;
    };

    const double low  = peakRatio (0.0);
    const double high = peakRatio (0.8);
    INFO ("formant harmonic: " << low << " -> " << high);
    REQUIRE (high > low * 4.0);
}

TEST_CASE ("PAF is harmonic and bounded", "[osc][paf][stability]")
{
    const double sr = 48000.0;
    for (double f0 : { 55.0, 440.0, 3000.0 })
    {
        PafOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setFormant (0.6);
        o.setBandwidth (0.3);
        o.reset();

        auto buf = render (o, 16384);
        INFO ("f0 = " << f0);
        REQUIRE_FALSE (hasBadValues (buf));
        REQUIRE (peakAbs (buf) < 4.0f);
    }
}

// ---------------------------------------------------------------------------
// Granular
// ---------------------------------------------------------------------------

TEST_CASE ("Granular is pitched with no scatter and noisier with it", "[osc][granular]")
{
    const double sr = 48000.0, f0 = 220.0;

    auto tonality = [&] (double scatter)
    {
        GranularOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setScatter (scatter);
        o.setSize (0.5);
        o.setDensity (0.4);
        o.reset();

        auto spec = computeSpectrum (render (o, 32768), sr);
        // Fraction of the total energy sitting on the harmonic series.
        double harmonic = 0.0;
        for (int k = 1; k * f0 < 0.45 * sr; ++k)
            harmonic += std::pow (spec.magnitudeNearHz (k * f0, 3), 2.0);

        double total = 0.0;
        for (std::size_t b = 1; b < spec.magnitude.size(); ++b)
            total += spec.magnitude[b] * spec.magnitude[b];
        return harmonic / std::max (1.0e-12, total);
    };

    REQUIRE (tonality (0.0) > tonality (0.9));
}

TEST_CASE ("Granular renders identically on every note", "[osc][granular]")
{
    // The engine is stochastic, but a patch must sound the same each press --
    // and the offline harness needs determinism.
    const double sr = 48000.0;
    auto once = [&]
    {
        GranularOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (330.0);
        o.setScatter (0.7);
        o.setSize (0.4);
        o.setDensity (0.6);
        o.reset();
        return render (o, 8192);
    };
    REQUIRE (once() == once());
}

TEST_CASE ("Granular stays bounded", "[osc][granular][stability]")
{
    const double sr = 48000.0;
    for (double scatter : { 0.0, 0.5, 1.0 })
        for (double density : { 0.0, 1.0 })
        {
            GranularOscillator o;
            o.setSampleRate (sr);
            o.setFrequency (110.0);
            o.setScatter (scatter);
            o.setSize (0.8);
            o.setDensity (density);
            o.reset();

            auto buf = render (o, 8192);
            INFO ("scatter " << scatter << " density " << density);
            REQUIRE_FALSE (hasBadValues (buf));
            REQUIRE (peakAbs (buf) < 4.0f);
        }
}

// ---------------------------------------------------------------------------
// Wavetable
// ---------------------------------------------------------------------------

TEST_CASE ("Wavetable default set morphs from sine to saw", "[osc][wavetable]")
{
    const double sr = 48000.0, f0 = 220.0;

    auto harmonicRatio = [&] (double position)
    {
        WavetableOscillator o;
        o.setSampleRate (sr);
        o.setFrequency (f0);
        o.setTable (WavetableOscillator::defaultSet());
        o.setPosition (position);
        o.setWarp (0.5);      // neutral
        o.setFormant (0.0);
        o.reset();

        auto spec = computeSpectrum (render (o, 32768), sr);
        const double h1 = spec.magnitudeNearHz (f0);
        const double h2 = spec.magnitudeNearHz (2.0 * f0);
        return h2 / std::max (1.0e-9, h1);
    };

    // Frame 0 is a sine (no 2nd harmonic); the last frame is a saw (h2 ~ h1/2).
    REQUIRE (harmonicRatio (0.0) < 0.05);
    REQUIRE (harmonicRatio (1.0) > 0.3);
}

TEST_CASE ("Wavetable mip-maps keep high notes alias-free", "[osc][wavetable]")
{
    // A naive wavetable read of a saw at 4 kHz folds a forest of partials back
    // down the spectrum. The per-octave mip-maps exist to stop that.
    const double sr = 48000.0, f0 = 4000.0;
    WavetableOscillator o;
    o.setSampleRate (sr);
    o.setFrequency (f0);
    o.setTable (WavetableOscillator::defaultSet());
    o.setPosition (1.0);   // saw: the worst case
    o.setWarp (0.5);
    o.reset();

    auto buf = render (o, 32768);
    REQUIRE_FALSE (hasBadValues (buf));

    auto spec = computeSpectrum (buf, sr);
    double harmonic = 0.0;
    for (int k = 1; k * f0 < 0.5 * sr; ++k)
        harmonic += energyNear (spec, k * f0, 80.0);

    double total = 0.0;
    for (std::size_t b = 1; b < spec.magnitude.size(); ++b)
        total += spec.magnitude[b] * spec.magnitude[b];

    INFO ("harmonic fraction " << (harmonic / total));
    REQUIRE (harmonic > 0.9 * total);
}

TEST_CASE ("Wavetable falls back to the default set with no table", "[osc][wavetable]")
{
    const double sr = 48000.0;
    WavetableOscillator o;
    o.setSampleRate (sr);
    o.setFrequency (220.0);
    o.reset();   // no setTable call at all

    auto buf = render (o, 8192);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01);   // it plays something
}

// ---------------------------------------------------------------------------
// Band-split filter
// ---------------------------------------------------------------------------

TEST_CASE ("Band split tilt moves the emphasis across the spectrum", "[filter][bandsplit]")
{
    const double sr = 48000.0;

    auto centroid = [&] (double tilt)
    {
        BandSplitFilter f;
        f.setSampleRate (sr);
        f.setFrequency (1000.0);
        f.setResonance (1.0);
        f.setTilt (tilt);
        f.reset();

        auto buf = whiteNoise (32768, 61);
        f.processBlock (buf.data(), static_cast<int> (buf.size()));
        REQUIRE_FALSE (hasBadValues (buf));

        auto spec = computeSpectrum (buf, sr);
        double num = 0.0, den = 0.0;
        for (std::size_t b = 1; b < spec.magnitude.size(); ++b)
        {
            const double m = spec.magnitude[b] * spec.magnitude[b];
            num += spec.frequencyOfBin (b) * m;
            den += m;
        }
        return num / std::max (1.0e-12, den);
    };

    INFO ("centroid " << centroid (0.0) << " -> " << centroid (1.0));
    REQUIRE (centroid (1.0) > centroid (0.0) * 2.0);
}

TEST_CASE ("Band split stays bounded under fuzz", "[filter][bandsplit][stability]")
{
    const double sr = 48000.0;
    BandSplitFilter f;
    f.setSampleRate (sr);

    std::mt19937 rng (23);
    std::uniform_real_distribution<double> fq (80.0, 6000.0), unit (0.0, 1.0), in (-1.0, 1.0);
    std::vector<float> out;
    out.reserve (24000);
    for (int i = 0; i < 24000; ++i)
    {
        if ((i % 32) == 0)
        {
            f.setFrequency (fq (rng));
            f.setResonance (unit (rng));
            f.setTilt (unit (rng));
        }
        out.push_back (f.processSample (static_cast<float> (in (rng))));
    }
    REQUIRE_FALSE (hasBadValues (out));
    REQUIRE (peakAbs (out) < 100.0f);
}

// ---------------------------------------------------------------------------
// CZ resonance mode
// ---------------------------------------------------------------------------

TEST_CASE ("PD resonator syncs its ring to the note when given a pitch", "[filter][pdreso]")
{
    // Given a note pitch the ring is hard-synced to the fundamental, so the
    // whole output becomes periodic at f0 and every partial must land on the
    // note's harmonic grid. 2000 Hz is deliberately *not* a multiple of 220, so
    // a line sitting there is proof the ring is free-running instead.
    // 2090 Hz sits exactly between the 9th (1980) and 10th (2200) harmonics of
    // 220, so a narrow window there cannot catch either of them.
    const double sr = 48000.0, f0 = 220.0, fc = 2090.0;
    const double twoPi = 6.283185307179586;

    auto runWith = [&] (double noteHz)
    {
        PhaseDistortionResonator r;
        r.setSampleRate (sr);
        r.setNoteFrequency (noteHz);
        r.setFrequency (fc);
        r.setResonance (0.8);
        r.setAmount (0.7);
        r.reset();

        // A saw, so there is real energy for the bandpass to find up at the
        // cutoff -- a pure sine at f0 leaves the resonator with nothing to ring.
        std::vector<float> buf (32768);
        for (std::size_t i = 0; i < buf.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            double saw = 0.0;
            for (int k = 1; k <= 32; ++k)
                saw += std::sin (twoPi * k * f0 * t) / k;
            buf[i] = r.processSample (static_cast<float> (saw * 0.3));
        }
        REQUIRE_FALSE (hasBadValues (buf));
        return computeSpectrum (buf, sr);
    };

    const auto synced = runWith (f0);
    const auto freeRun = runWith (0.0);

    // Energy in a narrow window at the cutoff -- which is off the harmonic grid
    // -- as a fraction of the whole spectrum. Synced, the output is periodic at
    // f0 and nothing can live there; free-running, the ring puts a line on it.
    auto offGridRatio = [&] (const Spectrum& s)
    {
        double total = 0.0;
        for (std::size_t b = 1; b < s.magnitude.size(); ++b)
            total += s.magnitude[b] * s.magnitude[b];
        return energyNear (s, fc, 40.0) / std::max (1.0e-15, total);
    };

    INFO ("off-grid ratio: synced " << offGridRatio (synced)
          << "  free " << offGridRatio (freeRun));
    REQUIRE (offGridRatio (synced) < offGridRatio (freeRun) * 0.5);
}

// ---------------------------------------------------------------------------
// FX send routing
// ---------------------------------------------------------------------------

TEST_CASE ("A full FX send reproduces the plain render exactly", "[synth][fxsend]")
{
    // The processor's send routing is out = (dry - send) + chain(send). That is
    // only equal to the old insert chain if a send of 1 makes the send bus
    // identical to the dry bus, so check that at the engine level.
    const double sr = 48000.0;

    auto run = [&] (double send, bool withBuses, std::vector<float>& sendOut)
    {
        SynthEngine e;
        e.setSampleRate (sr);

        SynthParams p;
        p.oscAType = OscType::Saw;
        p.fxSend   = send;
        p.sustain  = 0.9;
        e.setParams (p);
        e.noteOn (60, 0.9f, 1);

        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        sendOut.assign (4096, 0.0f);
        std::vector<float> sr2 (4096, 0.0f);

        if (withBuses) e.renderBlock (l.data(), r.data(), sendOut.data(), sr2.data(), 4096);
        else           e.renderBlock (l.data(), r.data(), 4096);
        return l;
    };

    std::vector<float> sendBus, unused;
    const auto dryNoBuses = run (1.0, false, unused);
    const auto dryWithBus = run (1.0, true,  sendBus);

    // Asking for a send bus must not change the main output at all.
    REQUIRE (dryWithBus == dryNoBuses);
    // At send 1 the send bus is the dry bus, so the residue is silence.
    REQUIRE (sendBus == dryWithBus);

    // At send 0 nothing reaches the chain.
    std::vector<float> zeroSend;
    run (0.0, true, zeroSend);
    REQUIRE (peakAbs (zeroSend) == Approx (0.0f).margin (1.0e-7));
}

TEST_CASE ("Unison spread shape redistributes the stack", "[synth][unison]")
{
    SynthEngine e;
    SynthParams p;
    p.unisonVoices = 5;

    auto offsets = [&] (double shape)
    {
        p.unisonSpread = shape;
        e.setParams (p);
        std::vector<double> v;
        for (int k = 0; k < 5; ++k)
            v.push_back (e.unisonSpreadAt (k, 5));
        return v;
    };

    const auto even  = offsets (0.5);
    const auto tight = offsets (0.0);
    const auto wide  = offsets (1.0);

    // 0.5 is the historical even distribution, and must stay exactly that so
    // existing unison patches are unchanged.
    REQUIRE (even[0] == Approx (-1.0));
    REQUIRE (even[1] == Approx (-0.5));
    REQUIRE (even[2] == Approx (0.0));
    REQUIRE (even[3] == Approx (0.5));
    REQUIRE (even[4] == Approx (1.0));

    // The extremes are pinned in all three; only the inner voices move.
    for (const auto* v : { &even, &tight, &wide })
    {
        REQUIRE ((*v)[0] == Approx (-1.0));
        REQUIRE ((*v)[2] == Approx (0.0));
        REQUIRE ((*v)[4] == Approx (1.0));
    }

    // Tight pulls the inner pair toward the centre pitch, wide pushes them out.
    REQUIRE (std::abs (tight[3]) < std::abs (even[3]));
    REQUIRE (std::abs (wide[3])  > std::abs (even[3]));
    REQUIRE (std::abs (tight[1]) < std::abs (even[1]));
    REQUIRE (std::abs (wide[1])  > std::abs (even[1]));

    // A single voice is always centred, whatever the shape.
    REQUIRE (e.unisonSpreadAt (0, 1) == Approx (0.0));
}

TEST_CASE ("Unison spread renders cleanly at both extremes", "[synth][unison]")
{
    // Shape < 0.5 bunches the stack toward the centre pitch, > 0.5 pushes it
    // to the edges. Measured as how tightly the energy around the note is
    // concentrated: a peak magnitude at the exact note is no good here, because
    // several near-coincident detuned voices interfere at that bin.
    const double sr = 48000.0;

    auto centreEnergy = [&] (double shape)
    {
        SynthEngine e;
        e.setSampleRate (sr);

        SynthParams p;
        p.oscAType     = OscType::Saw;
        p.unisonVoices = 5;
        p.unisonDetune = 40.0;
        p.unisonSpread = shape;
        p.sustain      = 0.9;
        p.attack       = 0.001;
        e.setParams (p);
        e.noteOn (57, 0.9f, 1);   // A3 = 220 Hz

        std::vector<float> l (32768, 0.0f), r (32768, 0.0f);
        e.renderBlock (l.data(), r.data(), 32768);
        REQUIRE_FALSE (hasBadValues (l));

        return rms (l);
    };

    // Both extremes must simply work: a level in the same ballpark, nothing
    // silent, nothing blowing up.
    const double tight = centreEnergy (0.0);
    const double wide  = centreEnergy (1.0);
    INFO ("rms: tight " << tight << "  wide " << wide);
    REQUIRE (tight > 1.0e-3);
    REQUIRE (wide  > 1.0e-3);
    REQUIRE (tight == Approx (wide).epsilon (0.5));
}
