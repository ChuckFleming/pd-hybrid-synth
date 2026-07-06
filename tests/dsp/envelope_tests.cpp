#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/MultiStageEnvelope.h"
#include "harness/EnvelopeProbe.h"
#include "harness/SignalStats.h"

#include <vector>
#include <algorithm>

using pdhybrid::MultiStageEnvelope;
using pdhybrid::EnvStage;
using Catch::Approx;
using namespace harness;

TEST_CASE ("ADSR preset hits attack peak, sustain level, and finite output", "[env][adsr]")
{
    const double sr = 48000.0;
    MultiStageEnvelope env;
    env.setSampleRate (sr);
    env.setADSR (0.10, 0.10, 0.50, 0.20);
    env.reset();
    env.noteOn();

    // Render through attack + decay well into sustain.
    auto buf = render (env, sampleAt (0.5, sr));

    REQUIRE_FALSE (hasBadValues (buf));

    const float attackPeak = *std::max_element (buf.begin(), buf.end());
    REQUIRE (attackPeak == Approx (1.0f).margin (0.02));            // attack reaches full

    REQUIRE (buf[sampleAt (0.30, sr)] == Approx (0.5).margin (0.02)); // holding at sustain
    REQUIRE (env.isActive());
}

TEST_CASE ("Sustain holds indefinitely until note-off", "[env][adsr]")
{
    const double sr = 48000.0;
    MultiStageEnvelope env;
    env.setSampleRate (sr);
    env.setADSR (0.01, 0.05, 0.40, 0.10);
    env.reset();
    env.noteOn();

    auto buf = render (env, 200000);           // ~4 seconds held
    REQUIRE (buf.back() == Approx (0.40).margin (0.001));
    REQUIRE (env.isActive());
}

TEST_CASE ("Release ramps to zero and deactivates", "[env][adsr]")
{
    const double sr = 48000.0;
    MultiStageEnvelope env;
    env.setSampleRate (sr);
    env.setADSR (0.01, 0.05, 0.60, 0.10);
    env.reset();
    env.noteOn();
    render (env, sampleAt (0.20, sr));         // settle at sustain

    env.noteOff();
    auto rel = render (env, sampleAt (0.20, sr)); // release is 0.10 s -> fully done

    REQUIRE (rel.back() == Approx (0.0).margin (0.001));
    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("Note-off during attack releases from the current level", "[env][adsr]")
{
    const double sr = 48000.0;
    MultiStageEnvelope env;
    env.setSampleRate (sr);
    env.setADSR (1.0, 0.5, 0.7, 0.05);         // long attack
    env.reset();
    env.noteOn();

    render (env, sampleAt (0.10, sr));         // ~10% into a 1 s attack
    const double mid = env.level();
    REQUIRE (mid > 0.02);
    REQUIRE (mid < 0.5);                        // definitely not yet at peak

    env.noteOff();
    auto rel = render (env, sampleAt (0.10, sr));
    REQUIRE (rel.back() == Approx (0.0).margin (0.001));
}

TEST_CASE ("Multi-stage envelope hits each breakpoint level in order", "[env][multistage]")
{
    const double sr = 48000.0;
    MultiStageEnvelope env;
    env.setSampleRate (sr);
    env.setStages ({ { 0.8, 0.05, 0.0 },
                     { 0.3, 0.05, 0.0 },
                     { 1.0, 0.05, 0.0 },
                     { 0.0, 0.05, 0.0 } },
                   /*sustainIndex*/ 2);
    env.reset();
    env.noteOn();

    auto buf = render (env, sampleAt (0.30, sr));

    REQUIRE (buf[sampleAt (0.05, sr) - 1] == Approx (0.8).margin (0.02)); // end of stage 0
    REQUIRE (buf[sampleAt (0.10, sr) - 1] == Approx (0.3).margin (0.02)); // end of stage 1
    REQUIRE (buf[sampleAt (0.15, sr) - 1] == Approx (1.0).margin (0.02)); // end of stage 2
    REQUIRE (buf[sampleAt (0.25, sr)]     == Approx (1.0).margin (0.02)); // sustaining at 1.0
}

TEST_CASE ("Loop region repeats while held, then releases on note-off", "[env][loop]")
{
    const double sr = 48000.0;
    MultiStageEnvelope env;
    env.setSampleRate (sr);
    env.setStages ({ { 1.0, 0.02, 0.0 },
                     { 0.0, 0.02, 0.0 } },
                   /*sustainIndex*/ -1);
    env.setLoop (true, 0, 1);
    env.reset();
    env.noteOn();

    auto held = render (env, sampleAt (0.20, sr)); // many loop cycles
    REQUIRE (env.isActive());
    REQUIRE (*std::max_element (held.begin(), held.end()) > 0.9f);
    REQUIRE (*std::min_element (held.begin(), held.end()) < 0.1f);

    env.noteOff();
    auto after = render (env, sampleAt (0.10, sr));
    REQUIRE (after.back() == Approx (0.0).margin (0.001));
    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("Exponential curve bends away from the linear midpoint", "[env][curve]")
{
    const double sr = 48000.0;

    auto midpointValue = [&] (double curve)
    {
        MultiStageEnvelope env;
        env.setSampleRate (sr);
        env.setStages ({ { 1.0, 1.0, curve } }, /*sustainIndex*/ -1);
        env.reset();
        env.noteOn();
        auto buf = render (env, sampleAt (0.6, sr));
        return static_cast<double> (buf[sampleAt (0.5, sr)]);
    };

    REQUIRE (midpointValue (0.0) == Approx (0.5).margin (0.02));  // linear
    REQUIRE (midpointValue (3.0) > 0.70);                         // fast-approach
}

TEST_CASE ("Envelope starts from zero on note-on", "[env][timing]")
{
    const double sr = 48000.0;
    MultiStageEnvelope env;
    env.setSampleRate (sr);
    env.setADSR (0.10, 0.10, 0.50, 0.20);
    env.reset();
    env.noteOn();

    auto buf = render (env, 100);
    REQUIRE (buf.front() < 0.05f);   // begins near zero
    REQUIRE (buf.back()  > buf.front()); // and is rising
}
