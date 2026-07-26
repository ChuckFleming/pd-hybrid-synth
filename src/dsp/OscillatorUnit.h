#pragma once

#include "PhaseDistortionOscillator.h"
#include "AnalogOscillator.h"
#include "VpsOscillator.h"
#include "ScannedOscillator.h"
#include "VosimOscillator.h"
#include "WalshOscillator.h"
#include "SupersawOscillator.h"
#include "HarmonicOscillator.h"
#include "OscEq.h"
#include "SynthParams.h"   // OscType

namespace pdhybrid {

/**
    One oscillator "slot": either the Casio CZ phase-distortion engine (with a
    selectable DCW waveform) or a PolyBLEP analog waveform, plus per-oscillator
    tuning (octave / semitone / fine cents). A Voice holds two of these and mixes
    them, so the two slots can be detuned, octave-stacked or set to contrasting
    engines for a thicker, more interactive sound.

    Every control setter drives **only the selected engine**. Broadcasting them
    to all engines is much more expensive than it looks: the wavetable engines
    rebuild their table when their inputs move, so drift or a DCW envelope --
    which change the shared timbre knobs every control chunk -- had every voice
    rebuilding tables for engines it was not even using. The latest value of
    each shared control is cached here and re-applied by setType, so switching
    engine still lands on a correctly configured oscillator.
*/
class OscillatorUnit
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset         () noexcept;

    void setType       (OscType type) noexcept;
    void setOversampling (int factor) noexcept  { pd_.setOversampling (factor); vps_.setOversampling (factor); scanned_.setOversampling (factor); vosim_.setOversampling (factor); walsh_.setOversampling (factor); supersaw_.setOversampling (factor); harmonic_.setOversampling (factor); }
    void setPdWave     (PdWave wave) noexcept   { pd_.setWave (wave); }
    void setPdWaveB    (PdWave wave) noexcept   { pd_.setWaveB (wave); }
    void setPdCombine  (bool on) noexcept       { pd_.setCombine (on); }

    // Re-pluck the scanned-synthesis ring (no-op for the other engines).
    void excite        () noexcept              { scanned_.excite(); }

    // Cross-modulation (hard sync + phase mod). Called per sample on the
    // cross-mod path, so it too goes only to the engine that is running.
    void setPhaseMod (double offset) noexcept
    {
        phaseMod_ = offset;
        switch (type_)
        {
            case OscType::PhaseDistortion: pd_.setPhaseMod (offset);       break;
            case OscType::VPS:             vps_.setPhaseMod (offset);      break;
            case OscType::Scanned:         scanned_.setPhaseMod (offset);  break;
            case OscType::Vosim:           vosim_.setPhaseMod (offset);    break;
            case OscType::Walsh:           walsh_.setPhaseMod (offset);    break;
            case OscType::Supersaw:        supersaw_.setPhaseMod (offset); break;
            case OscType::Harmonic:        harmonic_.setPhaseMod (offset); break;
            default:                       analog_.setPhaseMod (offset);   break;
        }
    }
    bool wrapped     () const noexcept
    { return (type_ == OscType::PhaseDistortion) ? pd_.wrapped()
           : (type_ == OscType::VPS)             ? vps_.wrapped()
           : (type_ == OscType::Scanned)         ? scanned_.wrapped()
           : (type_ == OscType::Vosim)           ? vosim_.wrapped()
           : (type_ == OscType::Walsh)           ? walsh_.wrapped()
           : (type_ == OscType::Supersaw)        ? supersaw_.wrapped()
           : (type_ == OscType::Harmonic)        ? harmonic_.wrapped()
                                                 : analog_.wrapped(); }
    void syncReset   () noexcept
    { if      (type_ == OscType::PhaseDistortion) pd_.syncReset();
      else if (type_ == OscType::VPS)             vps_.syncReset();
      else if (type_ == OscType::Scanned)         scanned_.syncReset();
      else if (type_ == OscType::Vosim)           vosim_.syncReset();
      else if (type_ == OscType::Walsh)           walsh_.syncReset();
      else if (type_ == OscType::Supersaw)        supersaw_.syncReset();
      else if (type_ == OscType::Harmonic)        harmonic_.syncReset();
      else                                        analog_.syncReset(); }
    void setEq         (double lowDb, double midDb, double highDb) noexcept
    { eq_.setGains (lowDb, midDb, highDb); }
    // The DCW "amount" knob doubles as the VPS vertical (formant) coordinate, the
    // scanned-string stiffness and the VOSIM formant; the pulse-width knob as the
    // VPS horizontal (inflection X), the scanned-string damping and the VOSIM
    // decay. So both stay mod-matrix destinations and the DCW envelope sweeps
    // them, whatever the engine.
    void setAmount     (double amount01) noexcept
    {
        amount_ = amount01;
        switch (type_)
        {
            case OscType::PhaseDistortion: pd_.setAmount (amount01);                break;
            case OscType::VPS:             vps_.setVertical (amount01 * kVpsVMax);  break;
            case OscType::Scanned:         scanned_.setStiffness (amount01);        break;
            case OscType::Vosim:           vosim_.setFormant (amount01);            break;
            case OscType::Walsh:           walsh_.setTilt (amount01);               break;
            case OscType::Supersaw:        supersaw_.setDetune (amount01);          break;
            case OscType::Harmonic:        harmonic_.setCentroid (amount01);        break;
            default: break;   // the analog waveforms ignore the amount knob
        }
    }
    void setPulseWidth (double pulseWidth01) noexcept
    {
        pulseWidth_ = pulseWidth01;
        switch (type_)
        {
            case OscType::VPS:             vps_.setHorizontal (pulseWidth01);   break;
            case OscType::Scanned:         scanned_.setDamping (pulseWidth01);  break;
            case OscType::Vosim:           vosim_.setDecay (pulseWidth01);      break;
            case OscType::Walsh:           walsh_.setOddness (pulseWidth01);    break;
            case OscType::Supersaw:        supersaw_.setMix (pulseWidth01);     break;
            case OscType::Harmonic:        harmonic_.setOddEven (pulseWidth01); break;
            case OscType::PhaseDistortion: break;   // PD ignores pulse width
            default:                       analog_.setPulseWidth (pulseWidth01); break;
        }
    }

    // A third per-engine "extra" control, meaning VOSIM pulse count / Scanned
    // morph rate / Walsh fold / supersaw voice count / harmonic window width
    // depending on the active engine (unused by the analog and PD waveforms).
    void setEngineParam (double value01) noexcept
    {
        engineParam_ = value01;
        switch (type_)
        {
            case OscType::Vosim:    vosim_.setPulseCount (1 + (int) (value01 * 7.0 + 0.5)); break;
            case OscType::Scanned:  scanned_.setMorphRate (value01); break;
            case OscType::Walsh:    walsh_.setFold (value01);        break;
            case OscType::Supersaw: supersaw_.setVoices (value01);   break;
            case OscType::Harmonic: harmonic_.setWidth (value01);    break;
            default: break;
        }
    }
    // Scanned excitation shape (0=pluck 1=impulse 2=noise 3=triangle).
    void setExcite (int shape) noexcept { exciteShape_ = shape; scanned_.setExciteShape (shape); }

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
    ScannedOscillator         scanned_;
    VosimOscillator           vosim_;
    WalshOscillator           walsh_;
    SupersawOscillator        supersaw_;
    HarmonicOscillator        harmonic_;
    OscEq                     eq_;
    OscType type_    = OscType::PhaseDistortion;
    double  tuneMul_ = 1.0;
    double  baseHz_  = 440.0;

    // Latest value of each shared control, so setType can bring a newly
    // selected engine up to date (it was never given these while inactive).
    double amount_      = 0.30;
    double pulseWidth_  = 0.50;
    double engineParam_ = 0.40;
    double phaseMod_    = 0.0;
    int    exciteShape_ = 0;

    // Cached tuning inputs so setTuning can skip the pow() when unchanged.
    int    tuneOct_  = -1000;
    int    tuneSemi_ = -1000;
    double tuneFine_ = -1.0e9;
};

} // namespace pdhybrid
