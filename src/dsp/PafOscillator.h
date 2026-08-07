#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    Phase-Aligned Formant oscillator (Puckette's PAF).

    A carrier sine sits at the harmonic of the fundamental nearest the formant
    frequency, and a bell-shaped window -- driven by the same phasor, so the two
    stay phase-locked -- multiplies it once per period. The result is a single
    clean resonant peak that glides continuously as the formant is swept, with
    none of the stepping you get from switching harmonics, and no aliasing skirt
    because the carrier is locked to the harmonic series.

    Where VOSIM builds a formant from bursts of squared sine pulses and the
    Formant *filter* imposes vowel peaks on whatever it is fed, PAF synthesises
    one directly and is far smoother under modulation -- it is the engine to
    reach for when the formant itself is the thing being played.

    Three macro controls:
      * `formant`   (the DCW "amount" knob) -- centre frequency of the peak, as
        a multiple of the fundamental.
      * `bandwidth` (the pulse-width knob)  -- width of the bell window: narrow
        is a pure whistle, wide approaches a buzzy pulse.
      * `shape`     (the per-engine "extra") -- blends the carrier between the
        nearest harmonic and the one above, which is what lets the formant move
        continuously rather than in steps.
*/
class PafOscillator
{
public:
    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setFormant     (double amount01) noexcept;      // peak position
    void  setBandwidth   (double pulseWidth01) noexcept;  // peak width
    void  setShape       (double shape01) noexcept;       // carrier blend
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;
    void  reset          () noexcept;

    bool  wrapped   () const noexcept { return wrapped_; }
    void  syncReset () noexcept { phase_ = 0.0; }

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    double coreSample() noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;
    double phase_      = 0.0;
    double phaseMod_   = 0.0;

    double formant_   = 0.3;
    double bandwidth_ = 0.4;
    double shape_     = 0.5;

    bool wrapped_ = false;

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
