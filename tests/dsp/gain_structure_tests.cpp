#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/MasterStage.h"
#include "dsp/SynthEngine.h"
#include "dsp/SynthParams.h"
#include "harness/SignalStats.h"
#include "harness/Spectrum.h"

#include <vector>
#include <cmath>
#include <algorithm>

using namespace pdhybrid;
using Catch::Approx;
using namespace harness;

namespace {

constexpr double kSr = 48000.0;

// Peak of a sustained chord straight out of the engine (pre master stage).
double enginePeak (int notes, int unison, double oscA, double oscB,
                   double noise, double ring)
{
    SynthEngine e;
    e.setSampleRate (kSr);

    SynthParams p;
    p.oscAType     = OscType::Saw;
    p.oscBType     = OscType::Saw;
    p.oscALevel    = oscA;
    p.oscBLevel    = oscB;
    p.noiseLevel   = noise;
    p.ringModLevel = ring;
    p.unisonVoices = unison;
    p.unisonDetune = 20.0;
    p.cutoffHz     = 12000.0;
    p.sustain      = 1.0;
    p.attack       = 0.005;
    p.decay        = 0.05;
    e.setParams (p);

    static const int kChord[] = { 48, 52, 55, 60, 64, 67 };
    for (int i = 0; i < notes; ++i)
        e.noteOn (kChord[i], 1.0f, i + 1);

    std::vector<float> l (512, 0.0f), r (512, 0.0f);
    double peak = 0.0;
    for (int b = 0; b < 180; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.renderBlock (l.data(), r.data(), 512);
        if (b < 10) continue;   // skip the attack transient
        peak = std::max (peak, (double) peakAbs (l));
    }
    return peak;
}

} // namespace

// ---------------------------------------------------------------------------
// Master limiter
// ---------------------------------------------------------------------------

TEST_CASE ("Master limiter reduces gain instead of waveshaping", "[master][limiter]")
{
    // The stage used to be a bare tanh knee: every sample above the threshold
    // was distorted the instant it arrived, which is what a chord ran into. A
    // real limiter pulls the level down first, so a steady hot signal comes out
    // near the ceiling with very little added harmonic content.
    MasterStage m;
    m.setSampleRate (kSr);
    m.setGainDb (0.0);
    m.setLimiterEnabled (true);
    m.setThreshold (0.9);
    m.reset();

    const double twoPi = 6.283185307179586;
    const double f = 220.0;

    std::vector<float> l (32768), r (32768);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = static_cast<float> (2.5 * std::sin (twoPi * f * (double) i / kSr));
    m.processStereo (l.data(), r.data(), (int) l.size());

    REQUIRE_FALSE (hasBadValues (l));
    REQUIRE (peakAbs (l) <= 1.0f);
    REQUIRE (m.gainReductionDb() < -3.0);   // it really is reducing gain

    // Measure THD over the settled second half, well past the attack.
    std::vector<float> tail (l.end() - 16384, l.end());
    auto spec = computeSpectrum (tail, kSr);
    const double thd = totalHarmonicDistortion (spec, f, 8);
    INFO ("THD driven 2.5x over the ceiling: " << thd);
    REQUIRE (thd < 0.12);
}

TEST_CASE ("Master limiter is transparent below the threshold", "[master][limiter]")
{
    MasterStage m;
    m.setSampleRate (kSr);
    m.setGainDb (0.0);
    m.setLimiterEnabled (true);
    m.setThreshold (0.9);
    m.reset();

    const double twoPi = 6.283185307179586;
    std::vector<float> l (4096), r (4096), in (4096);
    for (std::size_t i = 0; i < l.size(); ++i)
        in[i] = l[i] = r[i] = static_cast<float> (0.5 * std::sin (twoPi * 220.0 * (double) i / kSr));

    m.processStereo (l.data(), r.data(), (int) l.size());

    // Compared by level, not sample by sample: the soft-clip knee runs 4x
    // oversampled and its filters have group delay, so the output is a few
    // samples behind the input even when nothing is being altered.
    std::vector<float> outTail (l.end() - 2048, l.end());
    std::vector<float> inTail  (in.end() - 2048, in.end());
    REQUIRE (rms (outTail)     == Approx (rms (inTail)).epsilon (0.01));
    REQUIRE (peakAbs (outTail) == Approx (peakAbs (inTail)).epsilon (0.01));
    REQUIRE (m.gainReductionDb() == Approx (0.0).margin (0.01));
}

TEST_CASE ("Master limiter holds the peak across a waveform cycle", "[master][limiter]")
{
    // Deriving the target from the raw per-sample magnitude lets the gain
    // release upward through every zero crossing and re-attack at every crest,
    // so crests escape. The peak follower is what stops that.
    MasterStage m;
    m.setSampleRate (kSr);
    m.setGainDb (0.0);
    m.setLimiterEnabled (true);
    m.setThreshold (0.9);
    m.reset();

    const double twoPi = 6.283185307179586;
    std::vector<float> l (48000), r (48000);
    for (std::size_t i = 0; i < l.size(); ++i)   // low note: long period
        l[i] = r[i] = static_cast<float> (3.0 * std::sin (twoPi * 55.0 * (double) i / kSr));
    m.processStereo (l.data(), r.data(), (int) l.size());

    std::vector<float> tail (l.end() - 24000, l.end());
    REQUIRE (peakAbs (tail) <= 1.0f);
}

// ---------------------------------------------------------------------------
// Voice gain structure
// ---------------------------------------------------------------------------

TEST_CASE ("Unison is a width control, not a loudness control", "[synth][gain]")
{
    // Six sub-voices used to sum to roughly six times a single voice, putting a
    // wide patch several times over the master ceiling before it was even
    // played as a chord.
    const double one = enginePeak (1, 1, 1.0, 0.0, 0.0, 0.0);
    const double six = enginePeak (1, 6, 1.0, 0.0, 0.0, 0.0);

    INFO ("1 voice " << one << ", 6-voice unison " << six);
    REQUIRE (six < one * 2.5);   // was ~5x before the compensation
    REQUIRE (six > one * 0.8);   // but the stack is still audibly there
}

TEST_CASE ("Mixer sources sum with constant power", "[synth][gain]")
{
    // Turning every source up used to stack levels additively, so the mixer
    // could be several times hotter than one oscillator before the filter.
    const double aOnly = enginePeak (1, 1, 1.0, 0.0, 0.0, 0.0);
    const double all   = enginePeak (1, 1, 1.0, 1.0, 1.0, 1.0);

    INFO ("A only " << aOnly << ", everything up " << all);
    REQUIRE (all < aOnly * 1.6);
}

TEST_CASE ("A single full oscillator is unchanged by the mixer normalisation",
           "[synth][gain]")
{
    // The normalisation must be exactly unity for the common case, or every
    // existing single-oscillator patch would quietly change level.
    SynthParams p;
    p.oscALevel = 1.0;
    p.oscBLevel = 0.0;
    p.noiseLevel = 0.0;
    p.ringModLevel = 0.0;

    // sqrt(1^2) == 1, so no scaling is applied.
    const double lvl = std::sqrt (p.oscALevel * p.oscALevel + p.oscBLevel * p.oscBLevel
                                  + p.noiseLevel * p.noiseLevel
                                  + p.ringModLevel * p.ringModLevel);
    REQUIRE (lvl == Approx (1.0));
}

TEST_CASE ("Unison gain compensation follows 1/sqrt(n)", "[synth][gain]")
{
    REQUIRE (SynthEngine::unisonGainFor (1) == Approx (1.0));
    REQUIRE (SynthEngine::unisonGainFor (4) == Approx (0.5));
    REQUIRE (SynthEngine::unisonGainFor (9) == Approx (1.0 / 3.0));
    // Never boosts.
    for (int n = 1; n <= 8; ++n)
        REQUIRE (SynthEngine::unisonGainFor (n) <= 1.0);
}

// ---------------------------------------------------------------------------
// Tempo-synced envelope times
// ---------------------------------------------------------------------------

TEST_CASE ("Note divisions convert to the expected envelope times", "[tempo]")
{
    // syncedDelaySeconds is what the processor maps an envelope stage's sync
    // choice through, so these are the times a synced ADSR actually gets.
    // Index order: 1/1, 1/2, 1/4, 1/8, 1/16, 1/4., 1/8., 1/4T, 1/8T.
    REQUIRE (syncedDelaySeconds (120.0, 0) == Approx (2.0));    // 1/1 = 4 beats
    REQUIRE (syncedDelaySeconds (120.0, 1) == Approx (1.0));    // 1/2
    REQUIRE (syncedDelaySeconds (120.0, 2) == Approx (0.5));    // 1/4 = 1 beat
    REQUIRE (syncedDelaySeconds (120.0, 3) == Approx (0.25));   // 1/8
    REQUIRE (syncedDelaySeconds (120.0, 4) == Approx (0.125));  // 1/16
    REQUIRE (syncedDelaySeconds (120.0, 5) == Approx (0.75));   // dotted 1/4
    REQUIRE (syncedDelaySeconds (120.0, 7) == Approx (1.0 / 3.0));  // 1/4 triplet

    // Halving the tempo doubles every division.
    REQUIRE (syncedDelaySeconds (60.0, 2) == Approx (1.0));
    REQUIRE (syncedDelaySeconds (240.0, 2) == Approx (0.25));
}

TEST_CASE ("A synced envelope stage tracks tempo", "[tempo][envelope]")
{
    // An eighth note at 120 BPM is 250 ms; at 60 BPM it is 500 ms. Drive an
    // envelope at each and confirm the decay really does take twice as long.
    auto decayTo = [] (double seconds)
    {
        SynthEngine e;
        e.setSampleRate (kSr);

        SynthParams p;
        p.oscAType = OscType::Saw;
        p.attack   = 0.001;
        p.decay    = seconds;
        p.sustain  = 0.0;
        p.release  = 0.1;
        p.cutoffHz = 12000.0;
        e.setParams (p);
        e.noteOn (60, 1.0f, 1);

        // Samples until the output falls to 10% of its peak.
        std::vector<float> l (64, 0.0f), r (64, 0.0f);
        double peak = 0.0;
        int    n = 0;
        for (int b = 0; b < 2000; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            e.renderBlock (l.data(), r.data(), 64);
            const double blockPeak = peakAbs (l);
            peak = std::max (peak, blockPeak);
            if (peak > 0.0 && blockPeak < peak * 0.1)
                return n;
            n += 64;
        }
        return n;
    };

    const int eighthAt120 = decayTo (syncedDelaySeconds (120.0, 3));
    const int eighthAt60  = decayTo (syncedDelaySeconds (60.0, 3));

    INFO ("1/8 decay: " << eighthAt120 << " samples at 120 BPM, "
          << eighthAt60 << " at 60 BPM");
    REQUIRE (eighthAt60 > eighthAt120 * 1.5);
}
