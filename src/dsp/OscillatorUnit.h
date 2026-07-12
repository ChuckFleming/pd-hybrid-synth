#pragma once

#include "PhaseDistortionOscillator.h"
#include "AnalogOscillator.h"
#include "VpsOscillator.h"
#include "OscEq.h"
#include "SynthParams.h"   // OscType

namespace pdhybrid {

/**
    One oscillator "slot": either the Casio CZ phase-distortion engine (with a
    selectable DCW waveform) or a PolyBLEP analog waveform, plus per-oscillator
    tuning (octave / semitone / fine cents). A Voice holds two of these and mixes
    them, so the two slots can be detuned, octave-stacked or set to contrasting
    engines for a thicker, more interactive sound.
*/
class OscillatorUnit
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset         () noexcept;

    void setType       (OscType type) noexcept;
    void setOversampling (int factor) noexcept  { pd_.setOversampling (factor); vps_.setOversampling (factor); }
    void setPdWave     (PdWave wave) noexcept   { pd_.setWave (wave); }
    void setPdWaveB    (PdWave wave) noexcept   { pd_.setWaveB (wave); }
    void setPdCombine  (bool on) noexcept       { pd_.setCombine (on); }

    // Cross-modulation (hard sync + phase mod). Dispatch to the active engine.
    void setPhaseMod (double offset) noexcept
    { pd_.setPhaseMod (offset); vps_.setPhaseMod (offset); analog_.setPhaseMod (offset); }
    bool wrapped     () const noexcept
    { return (type_ == OscType::PhaseDistortion) ? pd_.wrapped()
           : (type_ == OscType::VPS)             ? vps_.wrapped()
                                                 : analog_.wrapped(); }
    void syncReset   () noexcept
    { if      (type_ == OscType::PhaseDistortion) pd_.syncReset();
      else if (type_ == OscType::VPS)             vps_.syncReset();
      else                                        analog_.syncReset(); }
    void setEq         (double lowDb, double midDb, double highDb) noexcept
    { eq_.setGains (lowDb, midDb, highDb); }
    // The DCW "amount" knob doubles as the VPS vertical (formant) coordinate,
    // scaled up so the point can push past 1.0 for multi-formant sweeps.
    void setAmount     (double amount01) noexcept { pd_.setAmount (amount01); vps_.setVertical (amount01 * kVpsVMax); }
    // The pulse-width knob doubles as the VPS horizontal (inflection X).
    void setPulseWidth (double pulseWidth01) noexcept { analog_.setPulseWidth (pulseWidth01); vps_.setHorizontal (pulseWidth01); }

    // Octave (whole octaves), semitone offset, and fine detune in cents.
    void setTuning        (int octave, int semitone, double fineCents) noexcept;
    // The note pitch; the unit's own tuning is applied on top of this.
    void setBaseFrequency (double frequencyHz) noexcept;

    float processSample () noexcept;

private:
    static constexpr double kVpsVMax = 4.0;   // amount 1.0 -> VPS vertical 4.0

    PhaseDistortionOscillator pd_;
    AnalogOscillator          analog_;
    VpsOscillator             vps_;
    OscEq                     eq_;
    OscType type_    = OscType::PhaseDistortion;
    double  tuneMul_ = 1.0;
    double  baseHz_  = 440.0;

    // Cached tuning inputs so setTuning can skip the pow() when unchanged.
    int    tuneOct_  = -1000;
    int    tuneSemi_ = -1000;
    double tuneFine_ = -1.0e9;
};

} // namespace pdhybrid
