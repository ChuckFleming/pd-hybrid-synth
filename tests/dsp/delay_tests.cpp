#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/Delay.h"
#include "dsp/SynthParams.h"
#include "harness/SignalStats.h"

#include <vector>
#include <cmath>

using pdhybrid::Delay;
using pdhybrid::DelayMode;
using Catch::Approx;
using namespace harness;

namespace {

int argMaxAbs (const std::vector<float>& b, int from, int to)
{
    int best = from; double bestV = -1.0;
    for (int i = from; i < to && i < (int) b.size(); ++i)
        if (std::abs (b[i]) > bestV) { bestV = std::abs (b[i]); best = i; }
    return best;
}

float peakIn (const std::vector<float>& b, int from, int to)
{
    float m = 0.0f;
    for (int i = from; i < to && i < (int) b.size(); ++i)
        m = std::max (m, std::abs (b[i]));
    return m;
}

} // namespace

TEST_CASE ("Tempo-synced delay time matches the note division", "[delay][sync]")
{
    using pdhybrid::syncedDelaySeconds;
    REQUIRE (syncedDelaySeconds (120.0, 2) == Approx (0.5));   // 1/4 at 120 BPM = 0.5 s
    REQUIRE (syncedDelaySeconds (120.0, 3) == Approx (0.25));  // 1/8
    REQUIRE (syncedDelaySeconds (140.0, 2) == Approx (60.0 / 140.0));
    REQUIRE (syncedDelaySeconds (120.0, 0) <= 2.0);            // 1/1 clamped to the 2 s max
}

TEST_CASE ("Delay reproduces the input after the set time", "[delay]")
{
    const double sr = 48000.0;
    const int    d  = 480;                 // 10 ms
    Delay delay;
    delay.setSampleRate (sr);
    delay.setTimes (d / sr, d / sr);
    delay.setFeedback (0.0);
    delay.setMix (1.0);                    // fully wet
    delay.setMode (DelayMode::Stereo);
    delay.reset();

    std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
    l[0] = r[0] = 1.0f;                    // impulse
    delay.processStereo (l.data(), r.data(), (int) l.size());

    REQUIRE (argMaxAbs (l, 1, 4096) == Approx (d).margin (1));
}

TEST_CASE ("Delay feedback creates decaying repeats", "[delay]")
{
    const double sr = 48000.0;
    const int    d  = 480;
    Delay delay;
    delay.setSampleRate (sr);
    delay.setTimes (d / sr, d / sr);
    delay.setFeedback (0.5);
    delay.setMix (1.0);
    delay.setMode (DelayMode::Stereo);
    delay.reset();

    std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
    l[0] = r[0] = 1.0f;
    delay.processStereo (l.data(), r.data(), (int) l.size());

    const float e1 = peakIn (l, d - 4, d + 4);
    const float e2 = peakIn (l, 2 * d - 4, 2 * d + 4);
    const float e3 = peakIn (l, 3 * d - 4, 3 * d + 4);
    REQUIRE (e1 > e2);
    REQUIRE (e2 > e3);
    REQUIRE (e3 > 0.05f);                  // still audible repeats
    REQUIRE (e2 == Approx (e1 * 0.5f).margin (0.05));
}

TEST_CASE ("Ping-pong delay bounces echoes across channels", "[delay]")
{
    const double sr = 48000.0;
    const int    d  = 480;
    Delay delay;
    delay.setSampleRate (sr);
    delay.setTimes (d / sr, d / sr);
    delay.setFeedback (0.6);
    delay.setMix (1.0);
    delay.setMode (DelayMode::PingPong);
    delay.reset();

    std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
    l[0] = 1.0f;                           // impulse on the left only
    delay.processStereo (l.data(), r.data(), (int) l.size());

    // First repeat on the left, the bounced repeat on the right an echo later.
    REQUIRE (peakIn (l, d - 4, d + 4)         > 0.5f);
    REQUIRE (peakIn (r, d - 4, d + 4)         < 0.05f);
    REQUIRE (peakIn (r, 2 * d - 4, 2 * d + 4) > 0.2f);
    REQUIRE (peakIn (l, 2 * d - 4, 2 * d + 4) < 0.05f);
}

TEST_CASE ("Ducking attenuates the wet signal while the input is loud", "[delay][duck]")
{
    const double sr = 48000.0;
    const int    d  = 480;

    auto wetRms = [&] (double duck)
    {
        Delay delay;
        delay.setSampleRate (sr);
        delay.setTimes (d / sr, d / sr);
        delay.setFeedback (0.0);
        delay.setMix (1.0);                // wet only
        delay.setMode (DelayMode::Stereo);
        delay.setDuck (duck);
        delay.reset();

        std::vector<float> l (24000), r (24000);
        for (std::size_t i = 0; i < l.size(); ++i)   // sustained loud tone
            l[i] = r[i] = 0.8f * std::sin (6.2831853 * 300.0 * i / sr);
        delay.processStereo (l.data(), r.data(), (int) l.size());
        return rms (std::vector<float> (l.begin() + 12000, l.end()));
    };

    REQUIRE (wetRms (1.0) < wetRms (0.0) * 0.6);
}

TEST_CASE ("Delay stays finite and bounded at high feedback", "[delay]")
{
    const double sr = 48000.0;
    Delay delay;
    delay.setSampleRate (sr);
    delay.setTimes (0.01, 0.013);
    delay.setFeedback (0.95);
    delay.setMix (0.5);
    delay.setMode (DelayMode::PingPong);
    delay.reset();

    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    l[0] = r[0] = 1.0f;
    delay.processStereo (l.data(), r.data(), (int) l.size());

    REQUIRE_FALSE (hasBadValues (l));
    REQUIRE_FALSE (hasBadValues (r));
    REQUIRE (peakAbs (l) < 5.0f);
}

TEST_CASE ("Delay processWet emits echoes without the dry signal", "[delay][wet]")
{
    const double sr = 48000.0;
    auto make = [&] { Delay d; d.setSampleRate (sr); d.setTimes (0.1, 0.1);
                      d.setFeedback (0.4); d.setMix (0.5); d.setMode (DelayMode::Stereo);
                      d.reset(); return d; };

    Delay full = make();
    Delay wet  = make();

    std::vector<float> lf (24000, 0.0f), rf (24000, 0.0f);
    std::vector<float> lw (24000, 0.0f), rw (24000, 0.0f);
    lf[0] = rf[0] = 1.0f;
    lw[0] = rw[0] = 1.0f;

    full.processStereo (lf.data(), rf.data(), (int) lf.size());
    wet.processWet     (lw.data(), rw.data(), (int) lw.size());

    REQUIRE_FALSE (hasBadValues (lw));
    // Wet path has no dry impulse at sample 0 (mix 0.5 -> full keeps 0.5 there).
    REQUIRE (std::abs (lw[0]) < 1.0e-6f);
    REQUIRE (lf[0] == Approx (0.5f).margin (1e-4));
    // The first echo (~4800 samples) is identical between the two paths.
    const int e = 4800;
    REQUIRE (lw[e] == Approx (lf[e]).margin (1e-4));
    REQUIRE (peakAbs (lw) > 0.1f);   // echoes are present
}
