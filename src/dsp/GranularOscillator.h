#pragma once

#include "Oversampler.h"
#include <cstdint>

namespace pdhybrid {

/**
    Granular / particle oscillator: a stream of short windowed sine grains
    scattered around the note pitch.

    Grains are fired at a rate tied to the fundamental, each with its own pitch
    offset, start time and length. At zero scatter every grain is identical and
    lands exactly on the period, so the output is a clean pitched tone; as
    scatter rises the grains detune and jitter apart and the tone dissolves
    through shimmer into noise-like clouds. Grain density trades a sparse,
    stuttering texture against a dense continuous one.

    This is the only engine here that is deliberately stochastic. Its randomness
    comes from a per-voice LCG seeded on reset, so a note always renders the same
    way twice -- which matters both for the offline harness and for a synth where
    the same patch should sound the same on every press.

    Three macro controls:
      * `scatter` (the DCW "amount" knob) -- pitch/time spread of the grains.
      * `size`    (the pulse-width knob)  -- grain length as a fraction of the
        period: short is a click-y particle stream, long overlaps into a pad.
      * `density` (the per-engine "extra") -- how many grains are alive at once.
*/
class GranularOscillator
{
public:
    static constexpr int kMaxGrains = 8;

    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setScatter     (double amount01) noexcept;
    void  setSize        (double pulseWidth01) noexcept;
    void  setDensity     (double density01) noexcept;
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;
    void  reset          () noexcept;

    bool  wrapped   () const noexcept { return wrapped_; }
    void  syncReset () noexcept;

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    struct Grain
    {
        bool   active   = false;
        double phase    = 0.0;   // carrier phase, 0..1
        double inc      = 0.0;   // carrier step
        double windowPos = 0.0;  // 0..1 through the grain's life
        double windowInc = 0.0;
        double gain     = 1.0;
    };

    double nextRandom() noexcept;   // -1..1, deterministic per voice
    void   launchGrain (int slot) noexcept;
    double coreSample  () noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;
    double phase_      = 0.0;      // grain clock (fundamental x density)
    double fundPhase_  = 0.0;      // true fundamental, for wrapped()/hard sync
    double phaseMod_   = 0.0;

    double scatter_ = 0.0;
    double size_    = 0.5;
    double density_ = 0.4;

    Grain grains_[kMaxGrains];
    int   nextSlot_ = 0;
    bool  wrapped_  = false;

    std::uint32_t rng_ = 0x9E3779B9u;

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
