#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/FormantFilter.h"
#include "dsp/DiodeLadderFilter.h"
#include "dsp/LadderFilter.h"
#include "dsp/PhaseDistortionResonator.h"
#include "dsp/AllpassDispersion.h"
#include "dsp/CombFilter.h"
#include "dsp/FilterUnit.h"
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

// Peak frequency of the spectrum restricted to [flo, fhi].
double peakInBand (const Spectrum& s, double flo, double fhi)
{
    const std::size_t b0 = s.binOfFrequency (flo);
    const std::size_t b1 = std::min (s.binOfFrequency (fhi), s.magnitude.size() - 1);
    std::size_t best = b0;
    for (std::size_t k = b0; k <= b1; ++k)
        if (s.magnitude[k] > s.magnitude[best]) best = k;
    return s.frequencyOfBin (best);
}

} // namespace

// ---------------------------------------------------------------------------
// PD resonator: the regression that motivated the rebuild
// ---------------------------------------------------------------------------

TEST_CASE ("PD resonator passes signal at every resonance setting", "[filter][pdreso]")
{
    // The old mapping ran the pole radius to 0.9999, turning the filter into a
    // ~1.5 Hz-wide bandpass: past a light touch of resonance the signal simply
    // disappeared. The body path means output level now stays in the same
    // ballpark as the input right across the knob.
    const double sr = 48000.0;
    const auto   in = whiteNoise (16384, 21);
    const double inRms = rms (in);

    for (double res : { 0.0, 0.2, 0.5, 0.8, 1.0 })
    {
        PhaseDistortionResonator r;
        r.setSampleRate (sr);
        r.setFrequency (4000.0);   // well inside the noise band
        r.setResonance (res);
        r.setAmount (0.4);
        r.reset();

        auto buf = in;
        r.processBlock (buf.data(), static_cast<int> (buf.size()));

        REQUIRE_FALSE (hasBadValues (buf));
        INFO ("resonance = " << res << "  out/in = " << (rms (buf) / inRms));
        REQUIRE (rms (buf) > inRms * 0.1);    // never vanishes
        REQUIRE (rms (buf) < inRms * 12.0);   // and never runs away
    }
}

// ---------------------------------------------------------------------------
// Formant / vowel filter
// ---------------------------------------------------------------------------

TEST_CASE ("Formant filter places its first peak on the selected vowel", "[filter][formant]")
{
    const double sr = 48000.0;

    // A has F1 ~730 Hz, U has F1 ~300 Hz. Both should land near those with the
    // tract scaler left neutral.
    auto firstFormant = [&] (double vowel)
    {
        FormantFilter f;
        f.setSampleRate (sr);
        f.setFrequency (1000.0);   // neutral scaler
        f.setResonance (1.0);      // fully wet, sharp peaks
        f.setVowel (vowel);
        f.reset();

        auto buf = whiteNoise (32768, 13);
        f.processBlock (buf.data(), static_cast<int> (buf.size()));
        REQUIRE_FALSE (hasBadValues (buf));
        return peakInBand (computeSpectrum (buf, sr), 150.0, 1500.0);
    };

    REQUIRE (firstFormant (0.0)  == Approx (730.0).epsilon (0.2));   // A
    REQUIRE (firstFormant (1.0)  == Approx (300.0).epsilon (0.25));  // U
    REQUIRE (firstFormant (0.0)  > firstFormant (1.0));
}

TEST_CASE ("Formant cutoff scales the whole vowel", "[filter][formant]")
{
    const double sr = 48000.0;

    auto firstFormant = [&] (double tractHz)
    {
        FormantFilter f;
        f.setSampleRate (sr);
        f.setFrequency (tractHz);
        f.setResonance (1.0);
        f.setVowel (0.0);          // A
        f.reset();

        auto buf = whiteNoise (32768, 17);
        f.processBlock (buf.data(), static_cast<int> (buf.size()));
        return peakInBand (computeSpectrum (buf, sr), 100.0, 3000.0);
    };

    // Doubling the scaler should roughly double the formant frequency.
    const double low  = firstFormant (500.0);
    const double high = firstFormant (1000.0);
    REQUIRE (high == Approx (low * 2.0).epsilon (0.25));
}

TEST_CASE ("Formant resonance 0 stays close to dry", "[filter][formant]")
{
    const double sr = 48000.0;
    FormantFilter f;
    f.setSampleRate (sr);
    f.setFrequency (1000.0);
    f.setResonance (0.0);
    f.setVowel (0.5);
    f.reset();

    const auto in = whiteNoise (16384, 29);
    auto buf = in;
    f.processBlock (buf.data(), static_cast<int> (buf.size()));

    REQUIRE_FALSE (hasBadValues (buf));
    // 25% wet at resonance 0, so the level stays in the same region as dry.
    REQUIRE (rms (buf) == Approx (rms (in)).epsilon (0.6));
}

// ---------------------------------------------------------------------------
// Diode ladder
// ---------------------------------------------------------------------------

TEST_CASE ("Diode ladder rolls off above its cutoff", "[filter][diode]")
{
    const double sr = 48000.0;
    DiodeLadderFilter f;
    f.setSampleRate (sr);
    f.setCutoff (1000.0);
    f.setResonance (0.0);

    const double pass = measureGainDb (f, 100.0, sr);
    REQUIRE (measureGainDb (f, 4000.0,  sr) < pass - 12.0);
    REQUIRE (measureGainDb (f, 12000.0, sr) < pass - 30.0);
}

TEST_CASE ("Diode ladder resonance lifts the cutoff region", "[filter][diode]")
{
    const double sr = 48000.0;

    auto peakDb = [&] (double res)
    {
        DiodeLadderFilter f;
        f.setSampleRate (sr);
        f.setCutoff (1000.0);
        f.setResonance (res);
        return measureGainDb (f, 1000.0, sr);
    };

    REQUIRE (peakDb (0.8) > peakDb (0.0) + 6.0);
}

TEST_CASE ("Diode ladder poles are spread, not coincident", "[filter][diode]")
{
    // The defining difference from the transistor ladder: a softer knee. With
    // resonance off, measure how far the response has fallen one octave above
    // the cutoff -- the spread-pole cascade should be gentler there than four
    // coincident poles would be.
    const double sr = 48000.0;
    DiodeLadderFilter d;
    d.setSampleRate (sr);
    d.setCutoff (1000.0);
    d.setResonance (0.0);

    LadderFilter m;
    m.setSampleRate (sr);
    m.setCutoff (1000.0);
    m.setResonance (0.0);

    const double dRef = measureGainDb (d, 100.0, sr);
    const double mRef = measureGainDb (m, 100.0, sr);
    const double dDrop = dRef - measureGainDb (d, 2000.0, sr);
    const double mDrop = mRef - measureGainDb (m, 2000.0, sr);

    INFO ("diode drop " << dDrop << " dB, moog drop " << mDrop << " dB");
    REQUIRE (dDrop < mDrop);
}

TEST_CASE ("Diode ladder stays finite under fuzz", "[filter][diode][stability]")
{
    const double sr = 48000.0;
    DiodeLadderFilter f;
    f.setSampleRate (sr);

    std::mt19937 rng (5);
    std::uniform_real_distribution<double> fq (50.0, 15000.0), rs (0.0, 1.0), in (-1.0, 1.0);
    std::vector<float> out;
    out.reserve (48000);
    for (int i = 0; i < 48000; ++i)
    {
        if ((i % 32) == 0) { f.setCutoff (fq (rng)); f.setResonance (rs (rng)); }
        out.push_back (f.processSample (static_cast<float> (in (rng))));
    }
    REQUIRE_FALSE (hasBadValues (out));
    REQUIRE (peakAbs (out) < 100.0f);
}

// ---------------------------------------------------------------------------
// Repaired controls: allpass cutoff, comb feedback range
// ---------------------------------------------------------------------------

TEST_CASE ("Allpass frequency control actually moves the filter", "[filter][allpass]")
{
    // The cutoff knob used to be wired to nothing for this type. setFrequency
    // must change the output while leaving the magnitude flat.
    const double sr = 48000.0;
    const auto   in = whiteNoise (4096, 33);

    auto run = [&] (double hz)
    {
        AllpassDispersion ap;
        ap.setSampleRate (sr);
        ap.setFrequency (hz);
        ap.setStages (4);
        ap.reset();
        auto buf = in;
        ap.processBlock (buf.data(), static_cast<int> (buf.size()));
        return buf;
    };

    REQUIRE (run (300.0) != run (5000.0));

    // Still a pure allpass: unity magnitude everywhere.
    AllpassDispersion ap;
    ap.setSampleRate (sr);
    ap.setFrequency (1200.0);
    ap.setStages (4);
    for (double hz : { 100.0, 1000.0, 8000.0 })
        REQUIRE (measureGainDb (ap, hz, sr) == Approx (0.0).margin (0.1));
}

TEST_CASE ("Allpass feedback defaults to off so the cascade stays flat", "[filter][allpass]")
{
    const double sr = 48000.0;
    AllpassDispersion ap;
    ap.setSampleRate (sr);
    ap.setCoefficient (0.7);
    ap.setStages (6);
    // No setFeedback call: the flat-magnitude contract must hold.
    for (double hz : { 200.0, 2000.0, 9000.0 })
        REQUIRE (measureGainDb (ap, hz, sr) == Approx (0.0).margin (0.1));
}

TEST_CASE ("Comb feedback covers its full range", "[filter][comb]")
{
    // The FilterUnit mapping used to floor feedback at 0.5, so half the
    // resonance knob did nothing and the comb always rang. Zero feedback must
    // now be a clean pass-through.
    const double sr = 48000.0;
    CombFilter c;
    c.setSampleRate (sr);
    c.setFrequency (440.0);
    c.setFeedback (0.0);
    c.setDamping (0.0);
    c.reset();

    const auto in = whiteNoise (2048, 41);
    auto buf = in;
    c.processBlock (buf.data(), static_cast<int> (buf.size()));
    REQUIRE (buf == in);
}

TEST_CASE ("Allpass filter type is audible through FilterUnit", "[filter][allpass][unit]")
{
    // On its own the cascade is flat, so the slot mixes the dry signal back in
    // to turn the phase shift into real notches. Without that the Allpass type
    // was inaudible as a filter.
    const double sr = 48000.0;
    FilterUnit u;
    u.setSampleRate (sr);
    u.setType (FilterType::Allpass);
    u.configure (1000.0, 0.0, 0.5);
    u.reset();

    auto sweep = sweepResponse (u, sr, 100.0, 10000.0, 40);
    double lo = 1.0e9, hi = -1.0e9;
    for (const auto& p : sweep) { lo = std::min (lo, p.gainDb); hi = std::max (hi, p.gainDb); }

    INFO ("allpass-through-unit range: " << lo << " .. " << hi << " dB");
    REQUIRE (hi - lo > 6.0);   // notches present, not a flat wire
}

TEST_CASE ("Every filter type is stable through FilterUnit", "[filter][unit][stability]")
{
    const double sr = 48000.0;
    for (auto ft : { FilterType::Ladder, FilterType::StateVariable, FilterType::PdResonator,
                     FilterType::Comb, FilterType::Allpass, FilterType::Formant,
                     FilterType::DiodeLadder })
    {
        FilterUnit u;
        u.setSampleRate (sr);
        u.setType (ft);
        u.reset();

        std::mt19937 rng (7);
        std::uniform_real_distribution<double> fq (50.0, 12000.0), unit (0.0, 1.0), in (-1.0, 1.0);
        std::vector<float> out;
        out.reserve (24000);
        for (int i = 0; i < 24000; ++i)
        {
            if ((i % 32) == 0) u.configure (fq (rng), unit (rng), unit (rng));
            out.push_back (u.processSample (static_cast<float> (in (rng))));
        }
        INFO ("filter type " << static_cast<int> (ft));
        REQUIRE_FALSE (hasBadValues (out));
        REQUIRE (peakAbs (out) < 100.0f);
    }
}
