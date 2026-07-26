#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    Additive "harmonic sweep" oscillator: a bank of sine partials whose levels
    follow a movable, adjustable-width window over the harmonic series.

    Sliding the window up the series sweeps a formant-like peak through the
    tone -- organ-ish when narrow and low, vocal when narrow and high, and a
    full bright spectrum when opened wide. It is the counterweight to the rest
    of the engine list: every other oscillator here generates its brightness by
    distorting or stepping a waveform, and so aliases by nature, whereas this
    one is band-limited by construction (partials above Nyquist are simply not
    synthesised), giving a clean, glassy tone that stays clean at the top of the
    keyboard.

    Three macro controls:
      * `centroid` (the DCW "amount" knob) -- which harmonic the window sits on.
      * `oddEven`  (the pulse-width knob)  -- balance between odd and even
        partials: odd-only is hollow and clarinet-like, even-heavy is brighter.
      * `width`    (the per-engine "extra") -- how many neighbouring harmonics
        the window includes, from a near-pure sine to the full series.
*/
class HarmonicOscillator
{
public:
    // 512 float entries (2 kB) rather than a wider double table: a Voice holds
    // two oscillator slots and a SynthEngine sixteen voices, and those are
    // routinely stack-allocated, so the per-instance footprint has to stay small.
    // 512 points carries harmonics well past the kMaxHarmonic ceiling anyway.
    static constexpr int kTableLen  = 512;    // samples per cycle (power of two)
    static constexpr int kMaxHarmonic = 64;

    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setCentroid    (double amount01) noexcept;      // window centre
    void  setOddEven     (double pulseWidth01) noexcept;  // odd/even balance
    void  setWidth       (double width01) noexcept;       // window width
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;
    void  reset          () noexcept;

    bool  wrapped   () const noexcept { return wrapped_; }
    void  syncReset () noexcept { phase_ = 0.0; }

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    // The table is rebuilt lazily and no more often than kRebuildInterval
    // samples. Four setters feed it and all of them can move every control
    // chunk (drift, the DCW envelope, a modulated centroid), so rebuilding
    // eagerly inside each setter meant several full rebuilds per chunk per
    // voice. ~94 Hz is far finer than the ear resolves on a timbre sweep.
    static constexpr int kRebuildInterval = 512;

    void   markDirty   () noexcept { dirty_ = true; }
    void   rebuildTable() noexcept;
    void   serviceRebuild (int elapsedSamples) noexcept;
    double coreSample  () noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;
    double phase_      = 0.0;
    double phaseMod_   = 0.0;

    double centroid_ = 0.0;
    double oddEven_  = 0.5;
    double width_    = 0.4;

    // Last values the table was built for, so the rebuild only runs on a real
    // change. The harmonic ceiling is part of it: it falls as the note rises,
    // and the table has to be rebuilt when it does.
    double centroidBuilt_ = -1.0;
    double oddEvenBuilt_  = -1.0;
    double widthBuilt_    = -1.0;
    int    ceilingBuilt_  = -1;
    int    ceiling_       = kMaxHarmonic;

    bool dirty_ = true;          // a control moved; table not yet caught up
    bool everBuilt_ = false;     // first build is never deferred
    int  sinceRebuild_ = 0;      // samples since the last rebuild

    float  table_[kTableLen] = { 0.0f };
    bool   wrapped_ = false;

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
