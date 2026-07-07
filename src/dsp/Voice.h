#pragma once

#include "PhaseDistortionOscillator.h"
#include "AnalogOscillator.h"
#include "LadderFilter.h"
#include "PhaseDistortionResonator.h"
#include "CombFilter.h"
#include "AllpassDispersion.h"
#include "OverdriveAmp.h"
#include "MultiStageEnvelope.h"
#include "Lfo.h"
#include "SynthParams.h"

namespace pdhybrid {

// Convert a MIDI note number to frequency (A4 = note 69 = 440 Hz).
double midiNoteToHz (int note) noexcept;

/**
    A single synth voice: oscillator (PD or analog) -> selectable filter ->
    overdrive -> amp envelope. Each control block, a modulation matrix combines
    the sources (mod envelope, LFO, velocity, MPE pressure/timbre, pitch bend,
    key tracking, mod wheel) and applies the result to pitch, PD amount, pulse
    width, filter cutoff/resonance/morph, drive and amplitude.
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

    void setPitchBendSemitones (double semitones) noexcept { pitchBend_ = semitones; }
    void setPressure           (double pressure01) noexcept { pressure_ = pressure01; }
    void setTimbre             (double timbre01)   noexcept { timbre_ = timbre01; }
    void setModWheel           (double modWheel01) noexcept { modWheel_ = modWheel01; }

    // Adds `numSamples` of this voice's output into `out`.
    void renderBlock (float* out, int numSamples);

private:
    void  applyModulation() noexcept;   // evaluate the matrix and configure the DSP
    float renderOneSample() noexcept;

    PhaseDistortionOscillator osc_;
    AnalogOscillator          analogOsc_;
    LadderFilter              ladder_;
    PhaseDistortionResonator  pdReso_;
    CombFilter                comb_;
    AllpassDispersion         allpass_;
    OverdriveAmp              amp_;
    MultiStageEnvelope        env_;    // amplitude envelope
    MultiStageEnvelope        env2_;   // modulation envelope
    Lfo                       lfo_;

    SynthParams params_;
    double sampleRate_ = 44100.0;

    int    note_      = -1;
    double baseFreq_  = 440.0;
    double pitchBend_ = 0.0;   // semitones
    double velGain_   = 1.0;
    double pressure_  = 1.0;
    double timbre_    = 0.0;
    double modWheel_  = 0.0;
    double ampMod_    = 1.0;    // amplitude modulation multiplier
};

} // namespace pdhybrid
