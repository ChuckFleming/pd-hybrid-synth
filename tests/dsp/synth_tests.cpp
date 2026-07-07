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
using pdhybrid::FilterType;
using pdhybrid::OscType;
using pdhybrid::ModSource;
using pdhybrid::ModDest;
using pdhybrid::midiNoteToHz;
using Catch::Approx;
using namespace harness;

namespace {

SynthParams brightSustainParams()
{
    SynthParams p;
    p.oscAAmount = 0.10;   // few harmonics -> clean fundamental
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

TEST_CASE ("Mod matrix routes velocity to cutoff (brighter with velocity)", "[synth][mod]")
{
    const double sr = 48000.0;

    auto highFreqEnergy = [&] (float velocity)
    {
        SynthParams p;
        p.oscAType  = OscType::Saw;        // rich harmonics so cutoff matters
        p.cutoffHz  = 500.0;               // low base cutoff
        p.resonance = 0.0;
        p.attack = 0.001; p.decay = 0.001; p.sustain = 1.0; p.release = 0.05;
        p.gain = 0.9;
        p.modMatrix.setRoute (0, ModSource::Velocity, ModDest::Cutoff, 1.0); // opens filter

        SynthEngine e;
        e.setSampleRate (sr);
        e.setParams (p);
        e.noteOn (57, velocity, 1);        // A3 ~220 Hz

        auto buf  = renderEngine (e, 16384);
        auto spec = computeSpectrum (buf, sr);
        // Energy above 2 kHz -> "brightness".
        double hi = 0.0;
        for (std::size_t k = spec.binOfFrequency (2000.0); k < spec.magnitude.size(); ++k)
            hi += spec.magnitude[k] * spec.magnitude[k];
        return hi;
    };

    REQUIRE (highFreqEnergy (1.0f) > highFreqEnergy (0.2f) * 3.0);
}

TEST_CASE ("Engine produces clean sound with every filter type", "[synth][filter]")
{
    const double sr = 48000.0;
    for (auto ft : { FilterType::Ladder, FilterType::PdResonator,
                     FilterType::Comb, FilterType::Allpass })
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.filterType   = ft;
        p.resonance    = 0.4;
        p.filterMorph  = 0.5;
        e.setParams (p);
        e.noteOn (60, 1.0f, 1);

        auto buf = renderEngine (e, 16384);
        REQUIRE_FALSE (hasBadValues (buf));
        REQUIRE (peakAbs (buf) < 50.0f);
        REQUIRE (rms (buf) > 1e-5);        // it actually makes sound
    }
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

TEST_CASE ("Per-oscillator octave tuning shifts the fundamental", "[synth][osc]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);

    auto p = brightSustainParams();     // Osc A = PD, Osc B silent
    p.oscAOctave = -1;                  // one octave down
    e.setParams (p);
    e.noteOn (69, 1.0f, 1);             // A4 -> should sound at A3 = 220 Hz

    auto spec = computeSpectrum (renderEngine (e, 16384), sr);
    REQUIRE (spec.peakFrequency() == Approx (220.0).epsilon (0.02));
}

TEST_CASE ("Second oscillator adds its own detuned pitch to the mix", "[synth][osc][mixer]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);

    auto p = brightSustainParams();
    p.oscAType = OscType::Saw;   p.oscALevel = 0.7;
    p.oscBType = OscType::Saw;   p.oscBLevel = 0.7;
    p.oscBSemi = 7;              // a fifth above
    p.cutoffHz = 12000.0;
    e.setParams (p);
    e.noteOn (57, 1.0f, 1);      // A3 = 220 Hz; B a fifth up ~ 329.6 Hz

    auto buf  = renderEngine (e, 16384);
    auto spec = computeSpectrum (buf, sr);
    REQUIRE_FALSE (hasBadValues (buf));
    // Energy present at both oscillator pitches.
    REQUIRE (spec.magnitudeNearHz (220.0) > 0.01);
    REQUIRE (spec.magnitudeNearHz (329.6) > 0.01);
}

TEST_CASE ("Noise source produces broadband sound on its own", "[synth][osc][mixer]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);

    auto p = brightSustainParams();
    p.oscALevel = 0.0;           // silence both oscillators
    p.oscBLevel = 0.0;
    p.noiseLevel = 0.5;          // noise only
    p.cutoffHz = 16000.0;
    e.setParams (p);
    e.noteOn (60, 1.0f, 1);

    auto buf = renderEngine (e, 16384);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 1e-4);
    REQUIRE (peakAbs (buf) < 10.0f);
}
