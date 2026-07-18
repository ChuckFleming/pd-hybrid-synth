#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    VOSIM oscillator (Kaegi & Tempelaars, Utrecht 1978) -- "VOice SIMulation".

    Each fundamental period holds a short burst of N raised-sine (sin^2) pulses
    whose amplitudes decay geometrically, followed by silence to fill out the
    period. The pulse duration T sets a *formant* at ~1/T that stays fixed in Hz
    as the pitch changes (so it reads as a vowel across the keyboard); the decay
    shapes the formant's bandwidth. The result is the buzzy, vocal / early
    speech-chip character VOSIM is known for.

    The number of pulses is clamped so the burst always fits inside one period
    (no pulse is ever cut off), which keeps the waveform click-free. A one-pole
    DC blocker removes the offset the unipolar pulses would otherwise leave.

    `formant` (from the DCW "amount" knob) sets the pulse rate / vowel, `decay`
    (from the pulse-width knob) the burst decay -- so both stay mod-matrix
    destinations and the DCW envelope can sweep the formant.

    Output is anti-aliased with the shared FIR oversampler. Pure C++, no JUCE,
    fully deterministic: the offline harness drives it.
*/
class VosimOscillator
{
public:
    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setFormant     (double amount01) noexcept;   // 0..1 -> formant Hz (log)
    void  setDecay       (double pulseWidth01) noexcept;
    void  setPulseCount  (int pulses) noexcept;        // 1..kMaxPulses per burst
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;
    void  reset          () noexcept;

    bool  wrapped   () const noexcept { return wrapped_; }
    void  syncReset () noexcept { phase_ = 0.0; }

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

    static constexpr int kMaxPulses = 8;

private:

    void   recompute      () noexcept;   // derive pulse duration / count / amps
    double coreSample     () noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;   // fundamental (pitch)
    double phaseInc_   = 440.0 / 44100.0;
    double phase_      = 0.0;
    double phaseMod_   = 0.0;

    double formantHz_  = 800.0;   // formant centre (pitch-independent)
    double decay_      = 0.75;    // geometric amplitude decay between pulses

    int    targetPulses_ = 4;      // requested pulses per burst (clamped to fit)
    double periodSec_  = 1.0 / 440.0;
    double pulseDur_   = 1.0 / 800.0;   // T
    int    numPulses_  = 4;
    double ampTable_[kMaxPulses] = { 0.0 };

    double dcPrevIn_  = 0.0;   // one-pole DC blocker state
    double dcPrevOut_ = 0.0;
    bool   wrapped_   = false;
    bool   recomputePending_ = false;   // apply pulse-structure changes at the next period boundary

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
