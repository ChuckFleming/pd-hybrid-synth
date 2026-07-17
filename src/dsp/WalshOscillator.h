#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    Walsh-function-synthesis oscillator (Hutchins / Electronotes lineage, 1970s).

    Instead of summing sinusoids, the waveform is built from Walsh functions -- an
    orthogonal basis of +/-1 square-step functions ordered by "sequency" (their
    number of sign changes, the Walsh analogue of frequency). Summing a weighted
    set of them yields a hyper-digital, gritty, chiptune-adjacent tone made
    entirely of hard steps. The step edges alias by design; a light oversample +
    table interpolation tempers the worst of it while keeping the character.

    Two macro controls drive it (rather than 32 raw coefficients):
      * `tilt`   (from the DCW "amount" knob) -- the spectral slope over the
        sequency basis: low = only low-sequency terms (dark, simple), high = a
        flat spread up to high sequency (bright, harsh).
      * `oddness`(from the pulse-width knob) -- the balance between even- and
        odd-index Walsh terms, which shifts the waveform's symmetry / timbre.

    Both are therefore mod-matrix destinations and the DCW envelope can sweep the
    tilt. Pure C++, no JUCE, fully deterministic: the offline harness drives it.
*/
class WalshOscillator
{
public:
    static constexpr int kTableLen = 64;   // samples per cycle (power of two)
    static constexpr int kNumFuncs = 32;   // sequency-ordered Walsh terms used

    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setTilt        (double amount01) noexcept;      // spectral slope
    void  setOddness     (double pulseWidth01) noexcept;  // even/odd balance
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;
    void  reset          () noexcept;

    bool  wrapped   () const noexcept { return wrapped_; }
    void  syncReset () noexcept { phase_ = 0.0; }

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    void   rebuildTable() noexcept;
    double coreSample  () noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;
    double phase_      = 0.0;
    double phaseMod_   = 0.0;

    double tilt_    = 0.3;
    double oddness_ = 0.5;
    double tiltBuilt_    = -1.0;   // last values the table was built for (rebuild guard)
    double oddnessBuilt_ = -1.0;

    double table_[kTableLen] = { 0.0 };
    bool   wrapped_ = false;

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
