#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/MonoBass.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"

#include <vector>

using namespace pdhybrid;
using Catch::Approx;
using namespace harness;

namespace {
MonoBass makeBass (bool enabled = true)
{
    MonoBass b;
    b.setSampleRate (48000.0);
    b.setWaveform (AnalogWave::Saw);
    b.setOctave (0);
    b.setTuneCents (0.0);
    b.setLevel (1.0);
    b.setGlideTime (0.0);
    b.setADSR (0.001, 0.001, 1.0, 0.1);
    b.setEnabled (enabled);
    b.reset();
    b.setEnabled (enabled);
    return b;
}

// Renders a steady note and returns the settled second half for spectral analysis.
std::vector<float> renderSteady (double harmonics, int note)
{
    MonoBass b = makeBass();
    b.setHarmonics (harmonics);
    b.noteOn (note, 1.0f);

    const int n = 1 << 15;
    std::vector<float> buf (n, 0.0f);
    b.renderBlock (buf.data(), n);
    return std::vector<float> (buf.begin() + n / 2, buf.end());
}
}

TEST_CASE ("Mono bass is silent when disabled", "[monobass]")
{
    MonoBass b = makeBass (false);
    b.noteOn (48, 1.0f);

    std::vector<float> buf (512, 0.0f);
    b.renderBlock (buf.data(), 512);
    REQUIRE (peakAbs (buf) == Approx (0.0));
}

TEST_CASE ("Mono bass produces sound on note-on", "[monobass]")
{
    MonoBass b = makeBass();
    b.noteOn (45, 1.0f);
    REQUIRE (b.isActive());

    std::vector<float> buf (2048, 0.0f);
    b.renderBlock (buf.data(), 2048);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (peakAbs (buf) > 0.05f);
}

TEST_CASE ("Mono bass releases to silence after note-off", "[monobass]")
{
    MonoBass b = makeBass();
    b.noteOn (45, 1.0f);

    std::vector<float> warm (1024, 0.0f);
    b.renderBlock (warm.data(), 1024);

    b.noteOff (45);
    // Release is 0.1 s; render ~0.5 s and check the tail is quiet.
    std::vector<float> tail (24000, 0.0f);
    b.renderBlock (tail.data(), 24000);

    std::vector<float> last (tail.end() - 1024, tail.end());
    REQUIRE (peakAbs (last) < 1.0e-3f);
    REQUIRE_FALSE (b.isActive());
}

TEST_CASE ("Mono bass note priority selects the right held note", "[monobass]")
{
    MonoBass b = makeBass();

    b.setPriority (BassPriority::Last);
    b.noteOn (48, 1.0f);   REQUIRE (b.currentNote() == 48);
    b.noteOn (60, 1.0f);   REQUIRE (b.currentNote() == 60);
    b.noteOn (55, 1.0f);   REQUIRE (b.currentNote() == 55);   // most recent

    b.setPriority (BassPriority::Top);     REQUIRE (b.currentNote() == 60);
    b.setPriority (BassPriority::Bottom);  REQUIRE (b.currentNote() == 48);

    b.noteOff (48);                        REQUIRE (b.currentNote() == 55);   // new min of {60,55}
    b.setPriority (BassPriority::Last);    REQUIRE (b.currentNote() == 55);
    b.noteOff (55);                        REQUIRE (b.currentNote() == 60);
    b.noteOff (60);                        REQUIRE (b.currentNote() == -1);
}

TEST_CASE ("Mono bass all-notes-off clears the note", "[monobass]")
{
    MonoBass b = makeBass();
    b.noteOn (40, 1.0f);
    b.noteOn (52, 1.0f);
    b.allNotesOff();
    REQUIRE (b.currentNote() == -1);
}

TEST_CASE ("Mono bass harmonics knob adds harmonic content", "[monobass]")
{
    const int note = 57;                  // 220 Hz
    const double fund = 220.0;

    const auto clean  = renderSteady (0.0, note);
    const auto dirty  = renderSteady (1.0, note);

    const auto sClean = computeSpectrum (clean, 48000.0);
    const auto sDirty = computeSpectrum (dirty, 48000.0);

    const double thdClean = totalHarmonicDistortion (sClean, fund);
    const double thdDirty = totalHarmonicDistortion (sDirty, fund);

    REQUIRE_FALSE (hasBadValues (dirty));
    REQUIRE (thdDirty > thdClean);
}
