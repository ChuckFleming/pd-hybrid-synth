#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/StateVariableFilter.h"
#include "harness/FrequencyResponse.h"
#include "harness/SignalStats.h"

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>

using pdhybrid::StateVariableFilter;
using pdhybrid::SvfMode;
using Catch::Approx;
using namespace harness;

namespace {

StateVariableFilter makeSvf (double sr, double fc, double res, SvfMode mode)
{
    StateVariableFilter f;
    f.setSampleRate (sr);
    f.setCutoff (fc);
    f.setResonance (res);
    f.setMode (mode);
    return f;
}

} // namespace

TEST_CASE ("SVF lowpass: flat passband, ~12 dB/oct rolloff", "[svf]")
{
    const double sr = 48000.0, fc = 1000.0;
    auto f = makeSvf (sr, fc, 0.3, SvfMode::Lowpass);

    const double pass  = measureGainDb (f, 100.0,  sr);
    const double g4    = measureGainDb (f, 4000.0, sr);
    const double g8    = measureGainDb (f, 8000.0, sr);

    REQUIRE (pass == Approx (0.0).margin (0.5));
    const double slope = g8 - g4;                    // one octave
    REQUIRE (slope < -9.0);
    REQUIRE (slope > -15.0);
}

TEST_CASE ("SVF highpass passes highs and rejects lows", "[svf]")
{
    const double sr = 48000.0, fc = 1000.0;
    auto f = makeSvf (sr, fc, 0.3, SvfMode::Highpass);

    REQUIRE (measureGainDb (f, 12000.0, sr) == Approx (0.0).margin (0.7));
    REQUIRE (measureGainDb (f, 100.0,   sr) < -20.0);
}

TEST_CASE ("SVF bandpass peaks at cutoff", "[svf]")
{
    const double sr = 48000.0, fc = 1000.0;
    auto f = makeSvf (sr, fc, 0.5, SvfMode::Bandpass);

    const double atFc  = measureGainDb (f, fc,     sr);
    const double below = measureGainDb (f, fc / 4, sr);
    const double above = measureGainDb (f, fc * 4, sr);

    REQUIRE (atFc > below + 6.0);
    REQUIRE (atFc > above + 6.0);
}

TEST_CASE ("SVF notch rejects the cutoff frequency", "[svf]")
{
    const double sr = 48000.0, fc = 1000.0;
    auto f = makeSvf (sr, fc, 0.3, SvfMode::Notch);

    const double atFc  = measureGainDb (f, fc,     sr);
    const double below = measureGainDb (f, fc / 8, sr);
    const double above = measureGainDb (f, fc * 8, sr);

    REQUIRE (below == Approx (0.0).margin (1.0));
    REQUIRE (above == Approx (0.0).margin (1.0));
    REQUIRE (atFc < below - 20.0);
}

TEST_CASE ("SVF morph sweeps from lowpass to highpass", "[svf][morph]")
{
    const double sr = 48000.0, fc = 1000.0;

    StateVariableFilter lpEnd;
    lpEnd.setSampleRate (sr); lpEnd.setCutoff (fc); lpEnd.setResonance (0.2); lpEnd.setMorph (0.0);
    REQUIRE (measureGainDb (lpEnd, fc * 4, sr) < measureGainDb (lpEnd, fc / 4, sr) - 10.0);

    StateVariableFilter hpEnd;
    hpEnd.setSampleRate (sr); hpEnd.setCutoff (fc); hpEnd.setResonance (0.2); hpEnd.setMorph (1.0);
    REQUIRE (measureGainDb (hpEnd, fc / 4, sr) < measureGainDb (hpEnd, fc * 4, sr) - 10.0);
}

TEST_CASE ("SVF resonance raises the peak", "[svf]")
{
    const double sr = 48000.0, fc = 1000.0;
    auto lowQ  = makeSvf (sr, fc, 0.1, SvfMode::Bandpass);
    auto highQ = makeSvf (sr, fc, 0.9, SvfMode::Bandpass);
    REQUIRE (measureGainDb (highQ, fc, sr) > measureGainDb (lowQ, fc, sr) + 6.0);
}

TEST_CASE ("SVF stays finite under fuzz and is block-size invariant", "[svf][stability][invariance]")
{
    const double sr = 48000.0;
    const double twoPi = 6.283185307179586;

    SECTION ("fuzz")
    {
        StateVariableFilter f;
        f.setSampleRate (sr);
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> fc (30.0, 16000.0), r (0.0, 1.0), in (-1.0, 1.0);
        std::vector<float> out;
        for (int i = 0; i < 48000; ++i)
        {
            if ((i % 16) == 0) { f.setCutoff (fc (rng)); f.setResonance (r (rng)); }
            out.push_back (f.processSample (static_cast<float> (in (rng))));
        }
        REQUIRE_FALSE (hasBadValues (out));
        REQUIRE (peakAbs (out) < 100.0f);
    }

    SECTION ("block-size invariance")
    {
        auto render = [&] (int block)
        {
            StateVariableFilter f;
            f.setSampleRate (sr); f.setCutoff (1200.0); f.setResonance (0.6); f.setMode (SvfMode::Lowpass); f.reset();
            std::vector<float> buf (8192);
            for (std::size_t i = 0; i < buf.size(); ++i)
                buf[i] = static_cast<float> (std::sin (twoPi * 220.0 * static_cast<double> (i) / sr));
            int i = 0;
            while (i < static_cast<int> (buf.size()))
            {
                const int n = std::min (block, static_cast<int> (buf.size()) - i);
                f.processBlock (buf.data() + i, n);
                i += n;
            }
            return buf;
        };
        auto a = render (64), b = render (512);
        for (std::size_t i = 0; i < a.size(); ++i)
            REQUIRE (a[i] == b[i]);
    }
}
