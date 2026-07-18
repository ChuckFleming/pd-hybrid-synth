#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/SynthEngine.h"
#include "dsp/Voice.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/EnvelopeProbe.h"   // sampleAt

#include <vector>
#include <cmath>
#include <algorithm>

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

struct Stereo { std::vector<float> left, right; };

Stereo renderStereo (SynthEngine& e, int n)
{
    Stereo s { std::vector<float> (n), std::vector<float> (n) };
    e.renderBlock (s.left.data(), s.right.data(), n);
    return s;
}

// The engine is stereo; most tests only need one representative channel. With
// the default centre pan both channels are identical.
std::vector<float> renderEngine (SynthEngine& e, int n)
{
    return renderStereo (e, n).left;
}

// Render in small blocks (like a real host) so block-rate modulation -- the LFO,
// mod envelope and filter envelope -- actually evolves across the render.
std::vector<float> renderChunks (SynthEngine& e, int n, int blockSize = 64)
{
    std::vector<float> l (static_cast<std::size_t> (n), 0.0f);
    std::vector<float> r (static_cast<std::size_t> (n), 0.0f);
    for (int i = 0; i < n; i += blockSize)
    {
        const int m = std::min (blockSize, n - i);
        e.renderBlock (l.data() + i, r.data() + i, m);
    }
    return l;
}

// Magnitude-weighted mean frequency -- a robust "brightness" measure.
double centroid (const std::vector<float>& buf, double sr)
{
    auto spec = computeSpectrum (buf, sr);
    double num = 0.0, den = 0.0;
    for (std::size_t b = 1; b < spec.magnitude.size(); ++b)
    {
        num += spec.frequencyOfBin (b) * spec.magnitude[b];
        den += spec.magnitude[b];
    }
    return den > 0.0 ? num / den : 0.0;
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

TEST_CASE ("Engine renders a Vector-PS oscillator at the correct pitch", "[synth][vps]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    auto p = brightSustainParams();
    p.oscAType = OscType::VPS;   // amount -> vertical, pulse width -> horizontal
    e.setParams (p);
    e.noteOn (69, 1.0f, 1);   // A4 = 440 Hz

    auto buf  = renderEngine (e, 16384);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01f);                                 // audibly non-silent
    REQUIRE (computeSpectrum (buf, sr).peakFrequency() == Approx (440.0).epsilon (0.01));
}

TEST_CASE ("Engine renders a scanned-synthesis oscillator, plucked on note-on", "[synth][scanned]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    auto p = brightSustainParams();
    p.oscAType = OscType::Scanned;    // amount -> stiffness, pulse width -> damping
    p.oscAPulseWidth = 0.2;           // light damping so the note sustains
    e.setParams (p);
    e.noteOn (69, 1.0f, 1);           // A4 -> Voice::start plucks the ring

    auto buf = renderEngine (e, 16384);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01f);                                  // the pluck sounds
    REQUIRE (std::abs (computeSpectrum (buf, sr).peakFrequency() - 440.0) < 6.0);
}

TEST_CASE ("Engine renders a VOSIM oscillator, periodic at the note", "[synth][vosim]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    auto p = brightSustainParams();
    p.oscAType = OscType::Vosim;      // amount -> formant, pulse width -> decay
    e.setParams (p);
    e.noteOn (69, 1.0f, 1);           // A4 = 440 Hz

    auto buf  = renderEngine (e, 16384);
    auto spec = computeSpectrum (buf, sr);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01f);
    // All energy on harmonics of 440 Hz (the formant peak is one of them).
    const double ratio = spec.peakFrequency() / 440.0;
    REQUIRE (std::abs (ratio - std::round (ratio)) < 0.15);
}

TEST_CASE ("Engine renders a Walsh oscillator, periodic at the note", "[synth][walsh]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    auto p = brightSustainParams();
    p.oscAType = OscType::Walsh;      // amount -> tilt, pulse width -> oddness
    e.setParams (p);
    e.noteOn (69, 1.0f, 1);           // A4 = 440 Hz

    auto buf  = renderEngine (e, 16384);
    auto spec = computeSpectrum (buf, sr);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01f);
    const double ratio = spec.peakFrequency() / 440.0;
    REQUIRE (std::abs (ratio - std::round (ratio)) < 0.15);
}

TEST_CASE ("CZ vibrato modulates the pitch", "[synth][vibrato]")
{
    const double sr = 48000.0;
    const int    n  = 24000;
    auto render = [&] (bool on)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.vibratoOn = on; p.vibratoRate = 6.0; p.vibratoDepth = 60.0; p.vibratoWave = 0;
        e.setParams (p);
        e.noteOn (69, 1.0f, 1);
        return renderEngine (e, n);
    };
    auto off = render (false);
    auto on  = render (true);
    REQUIRE_FALSE (hasBadValues (on));
    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (on[i] - off[i]);
    REQUIRE (diff > 10.0);   // vibrato clearly wavers the pitch
}

TEST_CASE ("CZ vibrato delay postpones the onset", "[synth][vibrato]")
{
    const double sr = 48000.0;
    const int    n  = 48000;
    auto render = [&] (bool on)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.vibratoOn = on; p.vibratoRate = 6.0; p.vibratoDepth = 60.0;
        p.vibratoDelay = 0.5;   // half a second before onset
        e.setParams (p);
        e.noteOn (69, 1.0f, 1);
        return renderEngine (e, n);
    };
    auto off = render (false);
    auto on  = render (true);

    auto sumDiff = [&] (int start, int len)
    {
        double d = 0.0;
        for (int i = start; i < start + len; ++i) d += std::abs (on[i] - off[i]);
        return d;
    };
    // During the delay the two are identical; well past it they diverge.
    REQUIRE (sumDiff (2000, 18000) < 1.0e-3);
    REQUIRE (sumDiff (30000, 18000) > 10.0);
}

TEST_CASE ("Engine renders a Karplus-Strong pluck at the note pitch", "[synth][pluck]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    auto p = brightSustainParams();
    p.pluckOn = true;          // the osc mix excites a string tuned to the note
    p.pluckDecay = 0.9;
    p.pluckDamp  = 0.2;
    p.release = 2.0;           // let the ring sustain past the exciter burst
    e.setParams (p);
    e.noteOn (69, 1.0f, 1);    // A4 = 440 Hz

    auto buf  = renderEngine (e, 24000);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 0.01f);
    // The ring locks to harmonics of the tuned note.
    auto spec = computeSpectrum (buf, sr);
    const double ratio = spec.peakFrequency() / 440.0;
    REQUIRE (std::abs (ratio - std::round (ratio)) < 0.15);
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

TEST_CASE ("Key tracking opens the filter for higher notes", "[synth][filter]")
{
    const double sr = 48000.0;

    // Fraction of spectral energy above a fixed threshold -- rises as the filter
    // opens and lets more harmonics through.
    auto highEnergyFraction = [&] (double keyTrack)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.oscAType    = OscType::Saw;   // rich harmonics
        p.filterType  = FilterType::Ladder;
        p.cutoffHz    = 500.0;          // dark base cutoff
        p.resonance   = 0.0;
        p.keyTrack    = keyTrack;
        e.setParams (p);
        e.noteOn (84, 1.0f, 1);         // two octaves above the reference note

        auto spec = computeSpectrum (renderEngine (e, 16384), sr);
        const double total = totalEnergy (spec);
        const double below = energyBelowHz (spec, 1500.0);
        return total > 0.0 ? (total - below) / total : 0.0;
    };

    // Full key tracking raises the cutoff two octaves, letting the upper
    // harmonics of the note pass instead of being filtered away.
    REQUIRE (highEnergyFraction (1.0) > highEnergyFraction (0.0) * 2.0);
}

TEST_CASE ("Filter envelope sweeps the cutoff over time", "[synth][filter][env]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);

    auto p = brightSustainParams();
    p.oscAType        = OscType::Saw;
    p.filterType      = FilterType::Ladder;
    p.cutoffHz        = 400.0;      // dark base
    p.resonance       = 0.0;
    p.filterEnvAmount = 5.0;        // +5 octaves at full envelope
    p.filterEnvA      = 0.001;      // snap open
    p.filterEnvD      = 0.30;       // then slowly close
    p.filterEnvS      = 0.0;
    p.sustain         = 1.0;        // hold the note so we hear the sweep
    p.release         = 0.05;
    e.setParams (p);
    e.noteOn (48, 1.0f, 1);         // low note so key pitch doesn't dominate

    const double early = centroid (renderChunks (e, 4096), sr);   // envelope open
    renderChunks (e, 24000);                                      // let it decay to sustain 0
    const double late  = centroid (renderChunks (e, 4096), sr);   // envelope closed

    REQUIRE (early > late * 1.3);   // the tone gets darker as the filter env falls
}

TEST_CASE ("Filter B has its own envelope, independent of Filter A", "[synth][filter][env]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);

    auto p = brightSustainParams();
    p.oscAType        = OscType::Saw;
    p.filterRouting   = pdhybrid::FilterRouting::Series;   // A -> B
    p.filterType      = FilterType::Ladder;  p.cutoffHz     = 16000.0; p.resonance = 0.0;
    p.filterEnvAmount = 0.0;                                // Filter A: wide open, no env
    p.filter2Type     = FilterType::Ladder;  p.filter2Cutoff = 400.0;  p.filter2Res = 0.0;
    p.filter2EnvAmount = 5.0;                               // Filter B: its own env sweep
    p.filter2EnvA = 0.001; p.filter2EnvD = 0.30; p.filter2EnvS = 0.0;
    p.sustain = 1.0;
    e.setParams (p);
    e.noteOn (48, 1.0f, 1);

    const double early = centroid (renderChunks (e, 4096), sr);   // B env open
    renderChunks (e, 24000);                                      // let B env decay
    const double late  = centroid (renderChunks (e, 4096), sr);   // B env closed

    REQUIRE (early > late * 1.3);   // Filter B's own envelope shapes the tone
}

TEST_CASE ("CZ multi-stage envelope sweeps the filter cutoff", "[synth][multi]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);

    auto p = brightSustainParams();
    p.oscAType   = OscType::Saw;
    p.filterType = FilterType::Ladder;  p.cutoffHz = 300.0;  p.resonance = 0.0;
    p.czAmount   = 5.0;      // strong routing to cutoff; default shape decays 1.0 -> 0.5 sustain
    p.sustain    = 1.0;
    e.setParams (p);
    e.noteOn (48, 1.0f, 1);

    const double early = centroid (renderChunks (e, 4096), sr);   // high early stages
    renderChunks (e, 24000);                                      // settle to the sustain stage
    const double late  = centroid (renderChunks (e, 4096), sr);

    REQUIRE (early > late * 1.2);   // the multi-stage shape opens then settles the filter
}

TEST_CASE ("Mod matrix can route a new source to a new destination", "[synth][mod]")
{
    const double sr = 48000.0;

    auto oscBEnergy = [&] (double macro)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.oscAType = OscType::Saw; p.oscALevel = 0.5;
        p.oscBType = OscType::Saw; p.oscBLevel = 0.0; p.oscBSemi = 7;  // a fifth up
        p.cutoffHz = 12000.0;
        p.macro1 = macro;
        p.modMatrix.setRoute (0, ModSource::Macro1, ModDest::OscBLevel, 1.0);
        e.setParams (p);
        e.noteOn (57, 1.0f, 1);   // A3 = 220 Hz; Osc B ~ 329.6 Hz
        return computeSpectrum (renderEngine (e, 16384), sr).magnitudeNearHz (329.6);
    };

    REQUIRE (oscBEnergy (1.0) > oscBEnergy (0.0) * 4.0);   // macro -> Osc B level brings it in
}

TEST_CASE ("Master pan places a voice in the stereo field", "[synth][stereo][pan]")
{
    const double sr = 48000.0;

    auto energies = [&] (double pan)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.pan = pan;
        e.setParams (p);
        e.noteOn (60, 1.0f, 1);
        auto s = renderStereo (e, 8192);
        return std::pair<double, double> { rms (s.left), rms (s.right) };
    };

    auto [cl, cr] = energies (0.0);
    REQUIRE (cl == Approx (cr).epsilon (0.01));     // centre: balanced

    auto [ll, lr] = energies (-1.0);
    REQUIRE (ll > lr * 10.0);                        // hard left

    auto [rl, rr] = energies (1.0);
    REQUIRE (rr > rl * 10.0);                        // hard right
}

TEST_CASE ("Pan spread widens low and high notes to opposite sides", "[synth][stereo][pan]")
{
    const double sr = 48000.0;

    auto balance = [&] (int note)   // right RMS minus left RMS
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.panSpread = 1.0;          // full keyboard spread
        e.setParams (p);
        e.noteOn (note, 1.0f, 1);
        auto s = renderStereo (e, 8192);
        return rms (s.right) - rms (s.left);
    };

    REQUIRE (balance (36) < 0.0);   // low note leans left
    REQUIRE (balance (84) > 0.0);   // high note leans right
}

TEST_CASE ("Analog drift wanders the voice audibly without breaking pitch", "[synth][drift]")
{
    const double sr = 48000.0;

    auto renderWith = [&] (double drift, int blockSize)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.drift = drift;
        e.setParams (p);
        e.noteOn (69, 1.0f, 1);                 // A4 = 440 Hz
        return renderChunks (e, 16384, blockSize);
    };

    auto dry = renderWith (0.0, 64);
    auto wet = renderWith (1.0, 64);
    REQUIRE_FALSE (hasBadValues (wet));

    // Drift makes the output diverge clearly from the perfectly static version...
    double sumSq = 0.0;
    for (std::size_t i = 0; i < wet.size(); ++i)
        sumSq += (wet[i] - dry[i]) * (wet[i] - dry[i]);
    REQUIRE (std::sqrt (sumSq / wet.size()) > 5e-3);

    // ...but stays recognisable: the dominant pitch still sits near A4 even
    // though the drift is now deep (+/- 2 semitones at full).
    auto spec = computeSpectrum (wet, sr);
    REQUIRE (spec.peakFrequency() == Approx (440.0).epsilon (0.15));

    // The wander speed is buffer-size independent: the RMS of a large-block
    // render is close to a small-block render (both drift at the same rate).
    const double rmsSmall = rms (wet);
    const double rmsLarge = rms (renderWith (1.0, 512));
    REQUIRE (rmsLarge == Approx (rmsSmall).epsilon (0.25));
}

TEST_CASE ("Glide slides the pitch from the previous note to the target", "[synth][glide]")
{
    const double sr = 48000.0;
    pdhybrid::Voice v;
    v.prepare (sr);

    SynthParams p;
    p.oscAType = OscType::Saw;
    p.oscALevel = 1.0;
    p.sustain = 1.0;
    p.glideCurve = 1.0;
    v.setParams (p);

    // Slide from A3 (220 Hz) up to A4 (440 Hz) over 0.2 s.
    v.start (69, 1.0f, 220.0, 0.2 * sr);

    const int n = 24000;
    std::vector<float> l (n, 0.0f), r (n, 0.0f);
    for (int i = 0; i < n; i += 64)
        v.renderBlock (l.data() + i, r.data() + i, std::min (64, n - i));

    auto slice = [&] (int from, int len)
    {
        return std::vector<float> (l.begin() + from, l.begin() + from + len);
    };
    const double early = computeSpectrum (slice (0, 4096), sr).peakFrequency();
    const double late  = computeSpectrum (slice (n - 8192, 8192), sr).peakFrequency();

    REQUIRE (early > 200.0);          // starts near the previous note
    REQUIRE (early < late * 0.85);    // still climbing early on
    REQUIRE (late == Approx (440.0).epsilon (0.03));   // arrives at the target
}

TEST_CASE ("Series filter routing rolls off the highs more steeply", "[synth][filter][routing]")
{
    const double sr = 48000.0;
    using pdhybrid::FilterRouting;

    auto highFraction = [&] (FilterRouting routing)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = brightSustainParams();
        p.oscAType      = OscType::Saw;
        p.filterType    = FilterType::Ladder;   p.cutoffHz     = 700.0; p.resonance = 0.0;
        p.filter2Type   = FilterType::Ladder;   p.filter2Cutoff = 700.0; p.filter2Res = 0.0;
        p.filterRouting = routing;
        e.setParams (p);
        e.noteOn (60, 1.0f, 1);

        auto spec = computeSpectrum (renderEngine (e, 16384), sr);
        const double total = totalEnergy (spec);
        return total > 0.0 ? (total - energyBelowHz (spec, 2000.0)) / total : 0.0;
    };

    // Two lowpasses in series are steeper than one -> less energy up high.
    REQUIRE (highFraction (FilterRouting::Series) < highFraction (FilterRouting::Single) * 0.7);
}

TEST_CASE ("Parallel filter routing stays clean and audible", "[synth][filter][routing]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    auto p = brightSustainParams();
    p.oscAType      = OscType::Saw;
    p.filterType    = FilterType::Ladder;      p.cutoffHz     = 600.0;
    p.filter2Type   = FilterType::StateVariable; p.filter2Cutoff = 4000.0; p.filter2Morph = 1.0;
    p.filterRouting = pdhybrid::FilterRouting::Parallel;
    e.setParams (p);
    e.noteOn (60, 1.0f, 1);

    auto buf = renderEngine (e, 16384);
    REQUIRE_FALSE (hasBadValues (buf));
    REQUIRE (rms (buf) > 1e-4);
    REQUIRE (peakAbs (buf) < 10.0f);
}

TEST_CASE ("Unison stacks detuned voices and widens the field", "[synth][unison]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);

    auto p = brightSustainParams();
    p.oscAType     = OscType::Saw;
    p.unisonDetune = 20.0;   // cents
    p.unisonWidth  = 1.0;

    // Single voice reference.
    p.unisonVoices = 1;
    e.setParams (p);
    e.noteOn (57, 1.0f, 1);  // A3 = 220 Hz
    REQUIRE (e.activeVoiceCount() == 1);
    const double singleRms = rms (renderStereo (e, 16384).left);

    // Five-voice unison stack under one note id.
    SynthEngine e2;
    e2.setSampleRate (sr);
    p.unisonVoices = 5;
    e2.setParams (p);
    e2.noteOn (57, 1.0f, 1);
    REQUIRE (e2.activeVoiceCount() == 5);        // a whole stack of sub-voices

    auto s = renderStereo (e2, 16384);
    REQUIRE_FALSE (hasBadValues (s.left));

    // Stacking several voices adds level over the single voice...
    REQUIRE (rms (s.left) > singleRms * 1.5);

    // ...and full width pushes the outer voices to opposite sides.
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = 0; i < s.left.size(); ++i)
    {
        diff += (s.left[i] - s.right[i]) * (s.left[i] - s.right[i]);
        ref  += s.left[i] * s.left[i];
    }
    REQUIRE (diff > ref * 0.01);                 // a genuinely stereo result
}
