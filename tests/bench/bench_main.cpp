// Offline CPU benchmark for the synth voice stack.
//
// Renders a sustained chord through a real SynthEngine and reports the cost as
// a percentage of real time, per oscillator engine and per filter type. This is
// the harness used to chase glitching: anything above ~100% cannot keep up, and
// a spike confined to one engine points straight at that engine's code.

#include "dsp/SynthEngine.h"
#include "dsp/SynthParams.h"
#include "dsp/MasterStage.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace pdhybrid;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 128;      // small buffer: the glitch-prone case
constexpr double kSeconds    = 4.0;

// One block's real-time budget. Anything slower than this is a dropout.
constexpr double kBudgetMs = 1000.0 * kBlock / kSampleRate;

struct Result
{
    double percentRealTime;
    double blockWorstMs;
    int    blocksOverBudget;   // a single cold-start outlier is not a glitch;
                               // a recurring overrun is.
};

// `retriggerBlocks` > 0 re-strikes the chord on that interval, which is what
// exercises the note-on path (the case reported as glitching).
Result runChord (const SynthParams& base, int numNotes, int retriggerBlocks = 0)
{
    SynthEngine engine;
    engine.setSampleRate (kSampleRate);
    engine.setParams (base);

    // A six-note chord, the case that was reported as glitching.
    static const int kChord[] = { 48, 52, 55, 60, 64, 67 };
    auto strike = [&]
    {
        for (int i = 0; i < numNotes; ++i)
            engine.noteOn (kChord[i], 0.9f, i + 1);
    };
    auto release = [&]
    {
        for (int i = 0; i < numNotes; ++i)
            engine.noteOff (kChord[i], i + 1);
    };
    strike();

    std::vector<float> left  (kBlock, 0.0f);
    std::vector<float> right (kBlock, 0.0f);

    const int totalBlocks = static_cast<int> (kSeconds * kSampleRate / kBlock);
    double worstMs = 0.0;
    int    over    = 0;

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int b = 0; b < totalBlocks; ++b)
    {
        if (retriggerBlocks > 0 && b > 0 && (b % retriggerBlocks) == 0)
        {
            release();
            strike();
        }

        std::fill (left.begin(),  left.end(),  0.0f);
        std::fill (right.begin(), right.end(), 0.0f);

        const auto b0 = std::chrono::high_resolution_clock::now();
        engine.renderBlock (left.data(), right.data(), kBlock);
        const auto b1 = std::chrono::high_resolution_clock::now();

        const double ms = std::chrono::duration<double, std::milli> (b1 - b0).count();
        if (ms > worstMs) worstMs = ms;
        if (b > 2 && ms > kBudgetMs) ++over;   // skip cold-start blocks
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double elapsedS = std::chrono::duration<double> (t1 - t0).count();
    return { 100.0 * elapsedS / kSeconds, worstMs, over };
}

SynthParams baseParams()
{
    SynthParams p;
    p.oscALevel = 1.0;
    p.oscBLevel = 0.0;
    p.sustain   = 0.9;
    p.attack    = 0.01;
    p.decay     = 0.2;
    p.cutoffHz  = 8000.0;
    return p;
}

const char* kOscNames[] = { "PhaseDistortion", "Saw", "Square", "Triangle", "Pulse",
                            "VPS", "Scanned", "VOSIM", "Walsh", "Supersaw", "Harmonic",
                            "PAF", "Granular", "Wavetable" };
const char* kFiltNames[] = { "Ladder", "StateVariable", "PdResonator", "Comb",
                             "Allpass", "Formant", "DiodeLadder", "BandSplit" };

} // namespace

// Peak / RMS of a sustained chord. Crackling is usually clipping, so a filter
// or engine whose peak dwarfs the others is a level bug, not a CPU bug.
struct Level { double peak; double rms; };

Level measureLevel (const SynthParams& base, int numNotes)
{
    SynthEngine engine;
    engine.setSampleRate (kSampleRate);
    engine.setParams (base);

    static const int kChord[] = { 48, 52, 55, 60, 64, 67 };
    for (int i = 0; i < numNotes; ++i)
        engine.noteOn (kChord[i], 0.9f, i + 1);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    double peak = 0.0, sumSq = 0.0;
    long   n = 0;

    const int blocks = static_cast<int> (2.0 * kSampleRate / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        engine.renderBlock (l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            const double v = std::abs (l[i]);
            peak = std::max (peak, v);
            sumSq += v * v;
            ++n;
        }
    }
    return { peak, std::sqrt (sumSq / std::max (1L, n)) };
}

// Engine peak with an arbitrary chord / unison / mixer setup. Used to map the
// gain structure: where does the level actually come from as a patch gets wide?
Level measureConfig (int notes, int unison, double oscA, double oscB,
                     double noise, double ring)
{
    SynthEngine engine;
    engine.setSampleRate (kSampleRate);

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
    engine.setParams (p);

    static const int kChord[] = { 48, 52, 55, 60, 64, 67 };
    for (int i = 0; i < notes; ++i)
        engine.noteOn (kChord[i], 1.0f, i + 1);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    double peak = 0.0, sumSq = 0.0;
    long   n = 0;
    const int blocks = static_cast<int> (2.0 * kSampleRate / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        engine.renderBlock (l.data(), r.data(), kBlock);
        // Skip the attack transient.
        if (b < 20) continue;
        for (int i = 0; i < kBlock; ++i)
        {
            const double v = std::abs (l[i]);
            peak = std::max (peak, v);
            sumSq += v * v;
            ++n;
        }
    }
    return { peak, std::sqrt (sumSq / std::max (1L, n)) };
}

// Engine -> MasterStage, reporting the final peak and how hard the limiter had
// to work. Distortion here is measured as the harmonic content the stage adds
// to a signal that was clean going in.
struct MasterResult { double peak; double grDb; };

MasterResult measureThroughMaster (int notes, int unison)
{
    SynthEngine engine;
    engine.setSampleRate (kSampleRate);

    SynthParams p;
    p.oscAType     = OscType::Saw;
    p.unisonVoices = unison;
    p.unisonDetune = 20.0;
    p.cutoffHz     = 12000.0;
    p.sustain      = 1.0;
    p.attack       = 0.005;
    p.decay        = 0.05;
    engine.setParams (p);

    MasterStage master;
    master.setSampleRate (kSampleRate);
    master.setGainDb (0.0);
    master.setLimiterEnabled (true);
    master.setThreshold (0.9);
    master.reset();

    static const int kChord[] = { 48, 52, 55, 60, 64, 67 };
    for (int i = 0; i < notes; ++i)
        engine.noteOn (kChord[i], 1.0f, i + 1);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    double peak = 0.0, worstGr = 0.0;
    const int blocks = static_cast<int> (2.0 * kSampleRate / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        engine.renderBlock (l.data(), r.data(), kBlock);
        master.processStereo (l.data(), r.data(), kBlock);
        if (b < 20) continue;
        for (int i = 0; i < kBlock; ++i)
            peak = std::max (peak, std::abs ((double) l[i]));
        worstGr = std::min (worstGr, master.gainReductionDb());
    }
    return { peak, worstGr };
}

void report (const char* name, const Result& r)
{
    std::printf ("  %-16s %6.1f %% rt   worst %6.2f ms   over budget: %d\n",
                 name, r.percentRealTime, r.blockWorstMs, r.blocksOverBudget);
}

int main (int argc, char** argv)
{
    const int notes = (argc > 1) ? std::atoi (argv[1]) : 6;
    std::printf ("Chord of %d notes, %d-sample blocks @ %.0f Hz (budget %.2f ms/block)\n\n",
                 notes, kBlock, kSampleRate, kBudgetMs);

    std::printf ("--- Oscillator engines, sustained (Ladder filter) ---\n");
    for (int t = 0; t <= static_cast<int> (OscType::Wavetable); ++t)
    {
        auto p = baseParams();
        p.oscAType   = static_cast<OscType> (t);
        p.filterType = FilterType::Ladder;
        p.cutoffHz   = 8000.0;
        report (kOscNames[t], runChord (p, notes));
    }

    std::printf ("\n--- Filter types, sustained (Saw oscillator) ---\n");
    for (int t = 0; t <= static_cast<int> (FilterType::BandSplit); ++t)
    {
        auto p = baseParams();
        p.oscAType   = OscType::Saw;
        p.filterType = static_cast<FilterType> (t);
        p.cutoffHz   = 2000.0;
        p.resonance  = 0.6;
        report (kFiltNames[t], runChord (p, notes));
    }

    // Re-striking the chord ~10x/second: this is the reported failure case.
    std::printf ("\n--- Repeated chord strikes (note-on cost) ---\n");
    for (int t = 0; t <= static_cast<int> (OscType::Wavetable); ++t)
    {
        auto p = baseParams();
        p.oscAType   = static_cast<OscType> (t);
        p.filterType = FilterType::Ladder;
        p.cutoffHz   = 8000.0;
        report (kOscNames[t], runChord (p, notes, 37));
    }

    // Worst case for the table-building engines: every harmonic active, and
    // continuous pitch movement so the tables keep being invalidated.
    std::printf ("\n--- Vibrato + widest timbre (table rebuild pressure) ---\n");
    for (int t : { static_cast<int> (OscType::Saw),
                   static_cast<int> (OscType::Walsh),
                   static_cast<int> (OscType::Supersaw),
                   static_cast<int> (OscType::Wavetable) })
    {
        auto p = baseParams();
        p.oscAType     = static_cast<OscType> (t);
        p.filterType   = FilterType::Ladder;
        p.cutoffHz     = 8000.0;
        p.oscAAmount   = 0.95;   // Harmonic: centroid near the top of the series
        p.oscAEngine   = 1.0;    // Harmonic: widest window / Walsh: full fold
        p.vibratoOn    = true;
        p.vibratoDepth = 50.0;
        p.vibratoRate  = 6.0;
        report (kOscNames[t], runChord (p, notes));
    }

    // Drift / DCW envelope move the shared timbre knobs every control chunk.
    // OscillatorUnit dispatches those to *every* engine, so a table-building
    // engine can be rebuilding constantly even when it is not the selected one.
    std::printf ("\n--- Modulated timbre (drift on) — note the *unselected* engines ---\n");
    for (int t = 0; t <= static_cast<int> (OscType::Wavetable); ++t)
    {
        auto p = baseParams();
        p.oscAType   = static_cast<OscType> (t);
        p.filterType = FilterType::Ladder;
        p.cutoffHz   = 8000.0;
        p.drift      = 0.5;
        report (kOscNames[t], runChord (p, notes));
    }

    std::printf ("\n--- Modulated timbre (DCW envelope sweeping the amount) ---\n");
    for (int t = 0; t <= static_cast<int> (OscType::Wavetable); ++t)
    {
        auto p = baseParams();
        p.oscAType     = static_cast<OscType> (t);
        p.filterType   = FilterType::Ladder;
        p.cutoffHz     = 8000.0;
        p.dcwEnvAmount = 0.8;
        for (int i = 0; i < 8; ++i) { p.dcwEnvRate[i] = 0.25; p.dcwEnvLevel[i] = (i % 2) ? 0.9 : 0.1; }
        report (kOscNames[t], runChord (p, notes));
    }

    // Where the level actually comes from. The master limiter asymptotes to 1.0,
    // so anything arriving well above that is squashed into the top of its knee
    // -- audible as "clipping" even though nothing hard-clips.
    std::printf ("\n--- GAIN STRUCTURE: engine peak (limiter ceiling is 1.0) ---\n");
    std::printf ("  notes x unison (osc A only)\n");
    for (int notes6 : { 1, 3, 6 })
        for (int uni : { 1, 3, 6 })
        {
            const auto lv = measureConfig (notes6, uni, 1.0, 0.0, 0.0, 0.0);
            std::printf ("    %d note(s) x %d unison = %2d voices   peak %6.2f   rms %6.3f%s\n",
                         notes6, uni, notes6 * uni, lv.peak, lv.rms,
                         lv.peak > 1.0 ? "   <-- over ceiling" : "");
        }

    // Chord mode turns one key into three or four voices, so the voice budget
    // is spent much faster than the note count suggests.
    std::printf ("  chord-shaped load (4 notes at once)\n");
    for (int uni : { 1, 3, 6 })
    {
        const auto lv = measureConfig (4, uni, 1.0, 0.0, 0.0, 0.0);
        std::printf ("    4-note chord x %d unison = %2d voices   peak %6.2f   rms %6.3f%s\n",
                     uni, 4 * uni, lv.peak, lv.rms,
                     lv.peak > 1.0 ? "   <-- over ceiling" : "");
    }

    std::printf ("  mixer sources (1 note, no unison)\n");
    {
        struct { const char* name; double a, b, n, r; } cases[] = {
            { "A only",            1.0, 0.0, 0.0, 0.0 },
            { "A + B",             1.0, 1.0, 0.0, 0.0 },
            { "A + B + noise",     1.0, 1.0, 1.0, 0.0 },
            { "A + B + noise+ring",1.0, 1.0, 1.0, 1.0 },
        };
        for (const auto& c : cases)
        {
            const auto lv = measureConfig (1, 1, c.a, c.b, c.n, c.r);
            std::printf ("    %-20s peak %6.2f   rms %6.3f%s\n",
                         c.name, lv.peak, lv.rms,
                         lv.peak > 1.0 ? "   <-- over ceiling" : "");
        }
    }

    std::printf ("  through the master stage (limiter ceiling 0.9 + soft-clip net)\n");
    for (int notes6 : { 1, 3, 6 })
        for (int uni : { 1, 6 })
        {
            const auto m = measureThroughMaster (notes6, uni);
            std::printf ("    %d note(s) x %d unison   out peak %5.2f   limiter GR %5.1f dB%s\n",
                         notes6, uni, m.peak, m.grDb,
                         m.peak > 1.001 ? "   <-- ESCAPED THE CEILING" : "");
        }

    std::printf ("\n--- Output level by filter type (Saw osc, before any FX) ---\n");
    for (double res : { 0.3, 0.7, 1.0 })
    {
        std::printf ("  resonance %.1f\n", res);
        for (int t = 0; t <= static_cast<int> (FilterType::BandSplit); ++t)
        {
            auto p = baseParams();
            p.oscAType   = OscType::Saw;
            p.filterType = static_cast<FilterType> (t);
            p.cutoffHz   = 2000.0;
            p.resonance  = res;
            p.filterMorph = 0.5;

            const auto lv = measureLevel (p, notes);
            std::printf ("    %-16s peak %8.2f   rms %7.3f%s\n",
                         kFiltNames[t], lv.peak, lv.rms,
                         lv.peak > 4.0 ? "   <-- CLIPPING" : "");
        }
    }

    return 0;
}
