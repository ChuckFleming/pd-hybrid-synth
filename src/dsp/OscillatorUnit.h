#pragma once

#include "PhaseDistortionOscillator.h"
#include "AnalogOscillator.h"
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
    void setPdWave     (PdWave wave) noexcept   { pd_.setWave (wave); }
    void setAmount     (double amount01) noexcept { pd_.setAmount (amount01); }
    void setPulseWidth (double pulseWidth01) noexcept { analog_.setPulseWidth (pulseWidth01); }

    // Octave (whole octaves), semitone offset, and fine detune in cents.
    void setTuning        (int octave, int semitone, double fineCents) noexcept;
    // The note pitch; the unit's own tuning is applied on top of this.
    void setBaseFrequency (double frequencyHz) noexcept;

    float processSample () noexcept;

private:
    PhaseDistortionOscillator pd_;
    AnalogOscillator          analog_;
    OscType type_    = OscType::PhaseDistortion;
    double  tuneMul_ = 1.0;
    double  baseHz_  = 440.0;
};

} // namespace pdhybrid
