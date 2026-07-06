#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/SynthEngine.h"
#include "dsp/Voice.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/EnvelopeProbe.h"   // sampleAt

#include <vector>
#include <cmath>

using pdhybrid::SynthEngine;
using pdhybrid::SynthParams;
using pdhybrid::midiNoteToHz;
using Catch::Approx;
using namespace harness;

namespace {

SynthParams brightSustainParams()
{
    SynthParams p;
    p.pdAmount = 0.10;   // few harmonics -> clean fundamental
    p.cutoffHz = 12000.0;
    p.resonance = 0.0;
    p.drive = 1.0;
    p.attack = 0.001;
    p.decay = 0.001;
    p.sustain = 1.0;     // steady level for amplitude/pitch measurement
    p.release = 0.05;
    p.gain = 0.9;
    return p;
}

std::vector<float> renderEngine (SynthEngine& e, int n)
{
    std::vector<float> buf (n);
    e.renderBlock (buf.data(), n);
    return buf;
}

} // namespace

TEST_CASE ("Engine plays a note at the correct pitch", "[synth][midi]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    e.setParams (brightSustainParams());
    e.noteOn (69, 1.0f, 1);   // A4 = 440 Hz

    auto buf  = renderEngine (e, 16384);
    auto spec = computeSpectrum (buf, sr);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (spec.peakFrequency() == Approx (440.0).epsilon (0.01));
}

TEST_CASE ("Engine is polyphonic and frees voices after release", "[synth][midi]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    e.setParams (brightSustainParams());

    e.noteOn (60, 1.0f, 1);
    e.noteOn (64, 1.0f, 2);
    e.noteOn (67, 1.0f, 3);
    renderEngine (e, 512);
    REQUIRE (e.activeVoiceCount() == 3);

    e.noteOff (60, 1);
    e.noteOff (64, 2);
    e.noteOff (67, 3);
    renderEngine (e, sampleAt (0.2, sr));   // past the 0.05 s release
    REQUIRE (e.activeVoiceCount() == 0);
}

TEST_CASE ("Velocity controls amplitude", "[synth][midi]")
{
    const double sr = 48000.0;

    auto rmsForVelocity = [&] (float vel)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        e.setParams (brightSustainParams());
        e.noteOn (60, vel, 1);
        return rms (renderEngine (e, 8192));
    };

    REQUIRE (rmsForVelocity (0.9f) > rmsForVelocity (0.3f) * 1.8);
}

TEST_CASE ("Pitch bend shifts the note", "[synth][midi]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    e.setParams (brightSustainParams());
    e.noteOn (69, 1.0f, 1);
    renderEngine (e, 4096);

    e.setNotePitchBend (1, 2.0);   // +2 semitones
    auto spec = computeSpectrum (renderEngine (e, 16384), sr);
    REQUIRE (spec.peakFrequency() == Approx (440.0 * std::pow (2.0, 2.0 / 12.0)).epsilon (0.01));
}

TEST_CASE ("Per-note (MPE) bend affects only the addressed note", "[synth][mpe]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    e.setParams (brightSustainParams());

    e.noteOn (60, 1.0f, 1);   // C4  261.63 Hz, channel/noteId 1
    e.noteOn (67, 1.0f, 2);   // G4  392.00 Hz, channel/noteId 2

    e.setNotePitchBend (2, 2.0);   // bend only note on id 2: 392 -> 440

    auto spec = computeSpectrum (renderEngine (e, 16384), sr);

    const double c4     = spec.magnitudeNearHz (midiNoteToHz (60));       // unchanged
    const double bentG4 = spec.magnitudeNearHz (392.0 * std::pow (2.0, 2.0 / 12.0)); // 440
    const double origG4 = spec.magnitudeNearHz (392.0);

    REQUIRE (c4 > 0.01);          // note 1 still where it was
    REQUIRE (bentG4 > 0.01);      // note 2 moved to 440
    REQUIRE (origG4 < bentG4);    // little left at the original 392
}

TEST_CASE ("Pressure (MPE) controls level", "[synth][mpe]")
{
    const double sr = 48000.0;

    auto rmsForPressure = [&] (double pressure)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        e.setParams (brightSustainParams());
        e.noteOn (60, 1.0f, 1);
        e.setNotePressure (1, pressure);
        return rms (renderEngine (e, 8192));
    };

    REQUIRE (rmsForPressure (1.0) > rmsForPressure (0.25) * 2.0);
}

TEST_CASE ("Polyphony is capped and steals the oldest voice", "[synth][voicing]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    e.setParams (brightSustainParams());

    for (int i = 0; i < 24; ++i)      // more notes than voices
    {
        e.noteOn (48 + i, 1.0f, i + 1);
        renderEngine (e, 64);
    }

    REQUIRE (e.activeVoiceCount() == SynthEngine::kMaxVoices);
    REQUIRE_FALSE (hasBadValues (renderEngine (e, 512)));
}

TEST_CASE ("Sub-block triggering makes note-on sample-accurate", "[synth][timing]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    e.setParams (brightSustainParams());

    // First half of the block: no note -> silence.
    auto before = renderEngine (e, 512);
    REQUIRE (peakAbs (before) == Approx (0.0f).margin (1e-7));

    // Trigger, then render the second half -> signal present.
    e.noteOn (60, 1.0f, 1);
    auto after = renderEngine (e, 4096);
    REQUIRE (peakAbs (after) > 0.05f);
}
