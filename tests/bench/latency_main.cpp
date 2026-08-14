// Offline note-on -> audio latency measurement.
//
// "It feels like there is a delay after pressing a key" has several possible
// causes and they need separating before any of them can be fixed:
//
//   1. the plugin's own DSP latency  -- measured here, exactly, in samples;
//   2. the amplitude envelope attack -- also measured here, because a 10 ms
//      attack is heard as a delay even though nothing is late;
//   3. the audio buffer and driver   -- NOT measurable offline. That is the
//      host's round trip and usually dominates everything else.
//
// This tool answers 1 and 2 with numbers, so 3 can be judged by subtraction:
// whatever you feel that these do not account for is buffer and driver.
//
// A note-on is delivered at sample 0 of the first block and the output is
// scanned for the first sample crossing a set of thresholds.

#include "dsp/SynthEngine.h"
#include "dsp/SynthParams.h"
#include "dsp/MasterStage.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace pdhybrid;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 64;
constexpr int    kMaxSamples = 48000;   // one second is far past any answer here

double msOf (int samples) { return 1000.0 * samples / kSampleRate; }

struct Crossings
{
    int firstNonZero = -1;   // any output at all: the true DSP latency
    int minus60      = -1;
    int minus40      = -1;   // about where a quiet sound becomes audible
    int minus20      = -1;
    int peak         = -1;
    double peakValue = 0.0;
};

/** Renders one note and reports where the output crosses each threshold.
    `throughMaster` puts the signal through the output stage as the plugin does,
    so any latency the limiter or its oversampler adds is included. */
Crossings measure (const SynthParams& p, bool throughMaster)
{
    SynthEngine engine;
    engine.setSampleRate (kSampleRate);
    engine.setParams (p);

    MasterStage master;
    master.setSampleRate (kSampleRate);
    master.reset();

    Crossings c;
    std::vector<float> l ((std::size_t) kBlock), r ((std::size_t) kBlock);

    // Run silence through first, so nothing measured here is a cold-start
    // artefact -- the output stage ramps its gain with a one-pole, and a reset
    // ramp would otherwise be counted as latency on the first note.
    //
    // Worth recording that this made no difference: the ~15 samples the output
    // stage adds survives the warm-up, so it is real group delay from the 4x
    // oversampled soft-clip knee, not a settling transient. That matters,
    // because the plugin reports 8 samples of latency to the host and the true
    // figure through the full chain is about 16.
    for (int i = 0; i < (int) kSampleRate / kBlock; ++i)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        engine.renderBlock (l.data(), r.data(), kBlock);
        if (throughMaster)
            master.processStereo (l.data(), r.data(), kBlock);
    }

    engine.noteOn (60, 1.0f, 1);

    int n = 0;
    while (n < kMaxSamples)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        engine.renderBlock (l.data(), r.data(), kBlock);
        if (throughMaster)
            master.processStereo (l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i, ++n)
        {
            const double a = std::abs ((double) l[(std::size_t) i]);

            if (c.firstNonZero < 0 && a > 0.0)                c.firstNonZero = n;
            if (c.minus60 < 0 && a >= 0.001)                  c.minus60 = n;   // -60 dB
            if (c.minus40 < 0 && a >= 0.01)                   c.minus40 = n;   // -40 dB
            if (c.minus20 < 0 && a >= 0.1)                    c.minus20 = n;   // -20 dB
            if (a > c.peakValue) { c.peakValue = a; c.peak = n; }
        }

        // Stop once the sound is clearly established and past its peak.
        if (c.minus20 >= 0 && n > c.minus20 + (int) kSampleRate / 4)
            break;
    }
    return c;
}

void report (const char* what, const Crossings& c)
{
    auto fmt = [] (int s)
    {
        static char buf[40];
        if (s < 0) { std::snprintf (buf, sizeof buf, "%14s", "never"); return buf; }
        std::snprintf (buf, sizeof buf, "%6d sm %6.2f ms", s, msOf (s));
        return buf;
    };

    std::printf ("  %-26s\n", what);
    std::printf ("      first output   %s\n", fmt (c.firstNonZero));
    std::printf ("      -60 dB         %s\n", fmt (c.minus60));
    std::printf ("      -40 dB         %s\n", fmt (c.minus40));
    std::printf ("      -20 dB         %s\n", fmt (c.minus20));
    std::printf ("      peak %.3f     %s\n", c.peakValue, fmt (c.peak));
}

SynthParams basePatch()
{
    SynthParams p;
    p.oscAType  = OscType::Saw;   // no wavetable or grain start-up to confound it
    p.oscALevel = 1.0;
    p.oscBLevel = 0.0;
    p.cutoffHz  = 18000.0;        // filter wide open: not shaping the onset
    p.resonance = 0.0;
    p.sustain   = 1.0;
    p.decay     = 1.0;
    p.driveOn   = false;
    return p;
}

} // namespace

int main()
{
    std::printf ("\nNote-on to audio latency, %.0f Hz, %d-sample blocks\n",
                 kSampleRate, kBlock);
    std::printf ("The plugin reports 8 samples (%.2f ms) of latency to the host.\n\n",
                 msOf (8));

    std::printf ("--- the DSP alone: instant attack, no output stage ---\n");
    {
        auto p = basePatch();
        p.attack = 0.0;
        report ("attack 0 ms, engine only", measure (p, false));
    }

    std::printf ("\n--- through the output stage, as the plugin runs it ---\n");
    {
        auto p = basePatch();
        p.attack = 0.0;
        report ("attack 0 ms", measure (p, true));
    }

    std::printf ("\n--- what the amp envelope adds (this is heard as delay) ---\n");
    for (double atk : { 0.001, 0.005, 0.01, 0.02, 0.05 })
    {
        auto p = basePatch();
        p.attack = atk;
        char label[64];
        std::snprintf (label, sizeof label, "attack %.0f ms", atk * 1000.0);
        report (label, measure (p, true));
    }

    std::printf ("\n--- the default patch, for reference ---\n");
    {
        SynthParams p;               // whatever the plugin starts with
        report ("SynthParams defaults", measure (p, true));
    }

    std::printf ("\nReading this: 'first output' is the plugin's real latency. Anything\n"
                 "beyond it is the envelope ramping up, which is a patch decision, not\n"
                 "a fault. Delay you feel that these numbers do not explain is the audio\n"
                 "buffer and driver round trip -- check the block size in the host.\n\n");
    return 0;
}
