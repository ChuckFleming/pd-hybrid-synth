#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    Vector Phaseshaping oscillator (Kleimola, Lazzarini, Timoney, Valimaki 2011) --
    a modern generalisation of Casio phase distortion.

    A linear phase ramp p in [0,1) is bent by a single movable inflection point
    (d, v) before a cosine read:

        f(p) = (p < d) ? (v/d) * p
                       : v + ((1 - v) / (1 - d)) * (p - d)
        y    = cos(2*pi * f(p))

    Because f(0) = 0 and f(1) = 1 for any (d, v), the waveform is continuous at
    the cycle boundary (click-free), while the slope break at p = d and the
    multiple cosine cycles packed in when v > 1 give formant sweeps and
    hard-sync-like timbres -- a strict superset of the classic CZ knee.

    `d` (horizontal) is driven by the per-oscillator pulse-width control and `v`
    (vertical / formant depth) by the DCW "amount" control, so both are already
    mod-matrix destinations and the DCW envelope sweeps the formant for free.

    Output is anti-aliased with the same internal FIR oversampler the CZ engine
    uses. Pure C++, no JUCE, fully deterministic: the offline harness drives it.
*/
class VpsOscillator
{
public:
    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setHorizontal  (double d01) noexcept;   // inflection X, kept inside (0,1)
    void  setVertical    (double v) noexcept;     // inflection Y / formant depth, >= 0
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;  // 1, 2, 4, or 8
    void  reset          () noexcept;

    bool  wrapped   () const noexcept { return wrapped_; }   // base phase wrapped last sample
    void  syncReset () noexcept { phase_ = 0.0; }            // hard-sync slave reset

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    void   updateIncrement() noexcept;
    double coreSample     () noexcept;   // one sample at the oversampled rate

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;   // per base-rate sample
    double phase_      = 0.0;   // normalised [0, 1)
    double d_          = 0.5;   // inflection X, clamped to (0,1)
    double v_          = 0.5;   // inflection Y (0.5 with d=0.5 -> pure cosine)
    double phaseMod_   = 0.0;
    bool   wrapped_    = false;

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
