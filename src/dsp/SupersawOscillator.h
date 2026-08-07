#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    Supersaw: a stack of detuned sawtooth oscillators in a single slot (the
    JP-8000 idea). One centre saw stays at pitch and the rest are spread either
    side of it, so the beating between them thickens a single note the way a
    unison stack does -- but per oscillator slot, leaving the synth's own unison
    free to widen the result further.

    Each saw is PolyBLEP band-limited, so the stack stays clean without leaning
    entirely on the oversampler.

    Three macro controls:
      * `detune` (the DCW "amount" knob) -- spread in cents, 0 collapses the
        stack back onto a single saw.
      * `mix`    (the pulse-width knob)  -- level of the detuned saws against
        the centre one.
      * `voices` (the per-engine "extra") -- how many saws are in the stack,
        3 to 9 in odd steps so there is always a centre.
*/
class SupersawOscillator
{
public:
    static constexpr int kMaxVoices = 9;

    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setDetune      (double amount01) noexcept;      // stack spread
    void  setMix         (double pulseWidth01) noexcept;  // side-saw level
    void  setVoices      (double voices01) noexcept;      // 3..9, odd
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;
    void  reset          () noexcept;

    bool  wrapped   () const noexcept { return wrapped_; }
    void  syncReset () noexcept;

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    void   updateIncrements() noexcept;
    void   resetPhases     () noexcept;
    double coreSample      () noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseMod_   = 0.0;

    double detune_ = 0.3;
    double mix_    = 0.7;
    int    voices_ = 7;

    double phase_[kMaxVoices] = { 0.0 };
    double inc_  [kMaxVoices] = { 0.0 };
    double gain_ [kMaxVoices] = { 0.0 };
    double norm_ = 1.0;

    bool wrapped_ = false;   // tracks the centre saw, which owns the pitch

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
