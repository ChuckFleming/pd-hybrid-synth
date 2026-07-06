#pragma once

#include "PhaseDistortionOscillator.h"
#include "LadderFilter.h"
#include "OverdriveAmp.h"
#include "MultiStageEnvelope.h"
#include "SynthParams.h"

namespace pdhybrid {

// Convert a MIDI note number to frequency (A4 = note 69 = 440 Hz).
double midiNoteToHz (int note) noexcept;

/**
    A single synth voice: PD oscillator -> ladder filter -> overdrive -> amp
    envelope. Expression inputs (pitch bend in semitones, pressure, timbre)
    modulate pitch, level, and filter cutoff respectively.
*/
class Voice
{
public:
    void prepare   (double sampleRate);
    void setParams (const SynthParams& params);

    void start   (int note, float velocity);   // note-on
    void release ();                            // note-off -> envelope release

    bool isActive() const noexcept { return env_.isActive(); }
    int  note    () const noexcept { return note_; }

    void setPitchBendSemitones (double semitones) noexcept;
    void setPressure           (double pressure01) noexcept { pressure_ = pressure01; }
    void setTimbre             (double timbre01)   noexcept;

    float render() noexcept;

private:
    void updateFrequency() noexcept;
    void updateCutoff()    noexcept;

    PhaseDistortionOscillator osc_;
    LadderFilter              filter_;
    OverdriveAmp              amp_;
    MultiStageEnvelope        env_;

    SynthParams params_;
    double sampleRate_ = 44100.0;

    int    note_      = -1;
    double baseFreq_  = 440.0;
    double pitchBend_ = 0.0;   // semitones
    double velGain_   = 1.0;
    double pressure_  = 1.0;   // 0..1
    double timbre_    = 0.0;   // 0..1
};

} // namespace pdhybrid
