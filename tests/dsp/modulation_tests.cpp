#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/Lfo.h"
#include "dsp/ModMatrix.h"
#include "dsp/SynthParams.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>

using namespace pdhybrid;
using Catch::Approx;
using namespace harness;

// ---------------------------------------------------------------------------
// LFO
// ---------------------------------------------------------------------------

TEST_CASE ("LFO runs at the set frequency", "[mod][lfo]")
{
    const double sr = 48000.0, rate = 5.0;
    Lfo lfo;
    lfo.setSampleRate (sr);
    lfo.setFrequency (rate);
    lfo.setWaveform (LfoWave::Sine);
    lfo.reset();

    const int N = 1 << 18;   // ~5.5 s -> fine frequency resolution
    std::vector<float> buf (N);
    for (int i = 0; i < N; ++i)
        buf[i] = static_cast<float> (lfo.processSample());

    auto spec = computeSpectrum (buf, sr);
    REQUIRE (spec.peakFrequency() == Approx (rate).epsilon (0.05));
}

TEST_CASE ("LFO output stays in [-1, 1] for every waveform", "[mod][lfo]")
{
    const double sr = 48000.0;
    for (auto w : { LfoWave::Sine, LfoWave::Triangle, LfoWave::Square, LfoWave::Saw,
                    LfoWave::RampDown, LfoWave::SampleHold, LfoWave::SmoothRandom,
                    LfoWave::Exponential })
    {
        Lfo lfo;
        lfo.setSampleRate (sr);
        lfo.setFrequency (3.0);
        lfo.setWaveform (w);
        lfo.reset();

        std::vector<float> buf (20000);
        for (auto& s : buf) s = static_cast<float> (lfo.processSample());
        REQUIRE_FALSE (hasBadValues (buf));
        REQUIRE (peakAbs (buf) <= 1.0001f);
    }
}

TEST_CASE ("Sample & hold LFO holds a constant within each cycle", "[mod][lfo]")
{
    const double sr = 48000.0;
    const double rate = 10.0;                    // 4800-sample cycle
    Lfo lfo;
    lfo.setSampleRate (sr);
    lfo.setFrequency (rate);
    lfo.setWaveform (LfoWave::SampleHold);
    lfo.reset();

    std::vector<double> v (14400);
    for (auto& s : v) s = lfo.processSample();

    // Constant across the first cycle, then it may step to a new value.
    for (int i = 1; i < 4700; ++i)
        REQUIRE (v[i] == Approx (v[0]));

    // At least two distinct held levels appear over three cycles.
    bool changed = false;
    for (int i = 4900; i < 14400; ++i)
        if (std::abs (v[i] - v[0]) > 1e-6) { changed = true; break; }
    REQUIRE (changed);
}

TEST_CASE ("Tempo-synced LFO rate matches the note division", "[mod][lfo][sync]")
{
    // At 120 BPM one beat is 0.5 s.
    REQUIRE (syncedLfoHz (120.0, 2) == Approx (2.0));    // 1/4 = one cycle per beat
    REQUIRE (syncedLfoHz (120.0, 3) == Approx (4.0));    // 1/8 = twice per beat
    REQUIRE (syncedLfoHz (120.0, 0) == Approx (0.5));    // 1/1 = once per 4 beats
    REQUIRE (syncedLfoHz (120.0, 7) == Approx (3.0));    // 1/4 triplet
    // Rate scales linearly with tempo.
    REQUIRE (syncedLfoHz (140.0, 2) == Approx (140.0 / 60.0));
}

TEST_CASE ("LFO advance matches sample-by-sample stepping", "[mod][lfo]")
{
    const double sr = 48000.0;
    Lfo a, b;
    for (auto* l : { &a, &b }) { l->setSampleRate (sr); l->setFrequency (7.0); l->reset(); }

    for (int i = 0; i < 1000; ++i) a.processSample();
    b.advance (1000);
    REQUIRE (a.value() == Approx (b.value()).margin (1e-9));
}

// ---------------------------------------------------------------------------
// Modulation matrix
// ---------------------------------------------------------------------------

TEST_CASE ("A single route scales a source into its destination", "[mod][matrix]")
{
    ModMatrix m;
    m.clear();
    m.setRoute (0, ModSource::Velocity, ModDest::Cutoff, 0.5);

    ModSources src;
    src[ModSource::Velocity] = 1.0;

    double out[ModMatrix::kNumDests];
    m.evaluate (src, out);

    REQUIRE (out[static_cast<int> (ModDest::Cutoff)] == Approx (0.5));
    REQUIRE (out[static_cast<int> (ModDest::Pitch)]  == Approx (0.0));
}

TEST_CASE ("Multiple routes to one destination sum; None routes are ignored", "[mod][matrix]")
{
    ModMatrix m;
    m.clear();
    m.setRoute (0, ModSource::Lfo,      ModDest::Pitch, 0.25);
    m.setRoute (1, ModSource::ModEnv,   ModDest::Pitch, 1.0);
    m.setRoute (2, ModSource::None,     ModDest::Pitch, 5.0);   // ignored
    m.setRoute (3, ModSource::Pressure, ModDest::None,  9.0);   // ignored

    ModSources src;
    src[ModSource::Lfo]    = -1.0;   // -0.25
    src[ModSource::ModEnv] =  0.8;   // +0.8

    double out[ModMatrix::kNumDests];
    m.evaluate (src, out);

    REQUIRE (out[static_cast<int> (ModDest::Pitch)] == Approx (-0.25 + 0.8));
}

TEST_CASE ("Route depth respects sign", "[mod][matrix]")
{
    ModMatrix m;
    m.clear();
    m.setRoute (0, ModSource::ModWheel, ModDest::Drive, -0.75);

    ModSources src;
    src[ModSource::ModWheel] = 1.0;

    double out[ModMatrix::kNumDests];
    m.evaluate (src, out);
    REQUIRE (out[static_cast<int> (ModDest::Drive)] == Approx (-0.75));
}
