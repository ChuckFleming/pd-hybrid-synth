#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/SynthEngine.h"
#include "dsp/Voice.h"
#include "harness/Spectrum.h"

#include <vector>
#include <cmath>

using pdhybrid::SynthEngine;
using pdhybrid::SynthParams;
using pdhybrid::Voice;
using pdhybrid::midiNoteToHz;
using Catch::Approx;
using namespace harness;

namespace {

// Clean sustained tone so a single fundamental dominates the spectrum.
SynthParams cleanParams()
{
    SynthParams p;
    p.oscAAmount = 0.10;
    p.cutoffHz   = 12000.0;
    p.resonance  = 0.0;
    p.attack     = 0.001;
    p.decay      = 0.001;
    p.sustain    = 1.0;
    p.release    = 0.05;
    p.gain       = 0.9;
    return p;
}

void advance (SynthEngine& e, int numSamples)
{
    std::vector<float> l (numSamples), r (numSamples);
    e.renderBlock (l.data(), r.data(), numSamples);
}

double soundingHz (SynthEngine& e, double sr)
{
    std::vector<float> l (16384), r (16384);
    e.renderBlock (l.data(), r.data(), static_cast<int> (l.size()));
    return computeSpectrum (l, sr).peakFrequency();
}

} // namespace

TEST_CASE ("Polyphony parameter caps the number of active voices", "[voice][poly]")
{
    SynthEngine e;
    e.setSampleRate (48000.0);
    auto p = cleanParams();
    p.voiceMode    = 0;    // Poly
    p.unisonVoices = 1;
    p.polyphony    = 2;
    e.setParams (p);

    e.noteOn (60, 1.0f, 1);
    e.noteOn (64, 1.0f, 2);
    e.noteOn (67, 1.0f, 3);   // exceeds the 2-voice limit -> must steal
    advance (e, 64);

    REQUIRE (e.activeVoiceCount() <= 2);
}

TEST_CASE ("Mono mode keeps a single voice sounding", "[voice][mono]")
{
    SynthEngine e;
    e.setSampleRate (48000.0);
    auto p = cleanParams();
    p.voiceMode    = 1;    // Mono
    p.unisonVoices = 1;
    e.setParams (p);

    e.noteOn (60, 1.0f, 1);
    e.noteOn (67, 1.0f, 2);
    advance (e, 64);

    REQUIRE (e.activeVoiceCount() == 1);
}

TEST_CASE ("Mono note priority selects the intended held note", "[voice][mono][priority]")
{
    const double sr = 48000.0;

    auto run = [&] (int priority, int first, int second)
    {
        SynthEngine e;
        e.setSampleRate (sr);
        auto p = cleanParams();
        p.voiceMode    = 1;
        p.unisonVoices = 1;
        p.notePriority = priority;
        e.setParams (p);
        e.noteOn (first, 1.0f, 1);
        e.noteOn (second, 1.0f, 2);
        return soundingHz (e, sr);
    };

    // Bottom priority -> lowest held note sounds.
    REQUIRE (run (2, 72, 60) == Approx (midiNoteToHz (60)).epsilon (0.05));
    // Top priority -> highest held note sounds.
    REQUIRE (run (1, 60, 72) == Approx (midiNoteToHz (72)).epsilon (0.05));
    // Last priority -> most recent press sounds.
    REQUIRE (run (0, 60, 72) == Approx (midiNoteToHz (72)).epsilon (0.05));
}

TEST_CASE ("Mono priority falls back to the remaining note on release", "[voice][mono][priority]")
{
    const double sr = 48000.0;
    SynthEngine e;
    e.setSampleRate (sr);
    auto p = cleanParams();
    p.voiceMode    = 1;
    p.unisonVoices = 1;
    p.notePriority = 0;   // Last
    e.setParams (p);

    e.noteOn (60, 1.0f, 1);
    e.noteOn (72, 1.0f, 2);   // 72 sounds
    e.noteOff (72, 2);        // fall back to 60

    REQUIRE (soundingHz (e, sr) == Approx (midiNoteToHz (60)).epsilon (0.05));
}

TEST_CASE ("Sustain pedal holds a released note until pedal-up", "[voice][sustain]")
{
    SynthEngine e;
    e.setSampleRate (48000.0);
    auto p = cleanParams();
    p.voiceMode    = 0;
    p.unisonVoices = 1;
    p.release      = 0.05;
    e.setParams (p);

    e.noteOn (60, 1.0f, 1);
    e.setSustain (true);
    e.noteOff (60, 1);
    advance (e, 128);
    REQUIRE (e.activeVoiceCount() == 1);   // pedal keeps it alive

    e.setSustain (false);                  // now it releases
    for (int i = 0; i < 200; ++i)          // > release time (0.05 s @ 48k)
        advance (e, 128);
    REQUIRE (e.activeVoiceCount() == 0);
}

TEST_CASE ("Ring modulation adds sum/difference sidebands", "[voice][ringmod]")
{
    const double sr = 48000.0;

    auto render = [&] (double ring)
    {
        Voice v;
        v.prepare (sr);
        SynthParams p = cleanParams();
        p.oscAType   = pdhybrid::OscType::Triangle;   // simple, mostly-fundamental tones
        p.oscBType   = pdhybrid::OscType::Triangle;
        p.oscBLevel  = 0.0;         // B not mixed directly...
        p.oscBSemi   = 7;           // ...B a fifth above A
        p.ringModLevel = ring;
        p.cutoffHz   = 18000.0;
        v.setParams (p);
        v.start (57, 1.0f);         // A3 = 220 Hz
        std::vector<float> l (16384), r (16384);
        v.renderBlock (l.data(), r.data(), static_cast<int> (l.size()));
        return l;
    };

    const auto dry = render (0.0);
    const auto wet = render (0.8);

    // Ring mod must change the signal, and add energy at |fB - fA| that the dry
    // (A-only) signal does not have. fA=220, fB=220*2^(7/12)=~329.6, diff ~110 Hz.
    double diff = 0.0;
    for (std::size_t i = 0; i < dry.size(); ++i)
        diff += std::abs (wet[i] - dry[i]);
    REQUIRE (diff > 1.0);

    auto sDry = computeSpectrum (dry, sr);
    auto sWet = computeSpectrum (wet, sr);
    const double fDiff = 220.0 * (std::pow (2.0, 7.0 / 12.0) - 1.0);   // ~109.6 Hz
    REQUIRE (sWet.magnitudeNearHz (fDiff) > sDry.magnitudeNearHz (fDiff) * 4.0);
}

TEST_CASE ("Drive position (pre vs post filter) changes the tone", "[voice][drivepos]")
{
    const double sr = 48000.0;

    auto render = [&] (int pos)
    {
        Voice v;
        v.prepare (sr);
        SynthParams p = cleanParams();
        p.driveOn   = true;
        p.drive     = 8.0;
        p.driveType = 2;          // hard clip -> position clearly matters
        p.cutoffHz  = 1500.0;     // a low cutoff so pre/post filtering diverge
        p.resonance = 0.3;
        p.drivePos  = pos;
        v.setParams (p);
        v.start (60, 1.0f);
        std::vector<float> l (4096), r (4096);
        v.renderBlock (l.data(), r.data(), static_cast<int> (l.size()));
        return l;
    };

    const auto post = render (0);
    const auto pre  = render (1);

    double diff = 0.0;
    for (std::size_t i = 0; i < post.size(); ++i)
        diff += std::abs (post[i] - pre[i]);
    REQUIRE (diff > 1.0);   // the two orderings are audibly different
}

TEST_CASE ("Pitch envelope shifts the note pitch", "[voice][pitchenv]")
{
    const double sr = 48000.0;

    // Hold the pitch envelope at a constant level so the pitch shift is steady
    // and the spectrum has a clean peak.
    auto peakHz = [&] (double amount, double level)
    {
        Voice v;
        v.prepare (sr);
        SynthParams p = cleanParams();
        p.oscAType        = pdhybrid::OscType::Triangle;
        p.cutoffHz        = 18000.0;
        p.pitchEnvAmount  = amount;
        p.pitchEnvSustain = 1;        // sustain on stage 1 -> constant offset
        for (int i = 0; i < 8; ++i) { p.pitchEnvLevel[i] = level; p.pitchEnvRate[i] = 0.001; }
        v.setParams (p);
        v.start (57, 1.0f);           // A3 = 220 Hz
        std::vector<float> l (16384), r (16384);
        v.renderBlock (l.data(), r.data(), static_cast<int> (l.size()));
        return computeSpectrum (l, sr).peakFrequency();
    };

    const double base = midiNoteToHz (57);
    // Level 0.75 with amount 12 -> +12 * (0.75-0.5)*2 = +6 semitones.
    REQUIRE (peakHz (0.0,  0.75) == Approx (base).epsilon (0.05));                       // amount 0 -> off
    REQUIRE (peakHz (12.0, 0.50) == Approx (base).epsilon (0.05));                       // level 0.5 -> no offset
    REQUIRE (peakHz (12.0, 0.75) == Approx (base * std::pow (2.0, 6.0 / 12.0)).epsilon (0.05));
}

TEST_CASE ("Legato changeNote retunes without retriggering the envelope", "[voice][legato]")
{
    Voice v;
    v.prepare (48000.0);
    auto p = cleanParams();
    v.setParams (p);

    v.start (60, 1.0f);
    std::vector<float> l (4096), r (4096);
    v.renderBlock (l.data(), r.data(), static_cast<int> (l.size()));   // reach sustain

    const double before = v.envLevel();
    REQUIRE (before > 0.5);            // sustaining

    v.changeNote (72);
    REQUIRE (v.note() == 72);
    REQUIRE (v.envLevel() == Approx (before));   // envelope untouched (no restart from 0)
}
