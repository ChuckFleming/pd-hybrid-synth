#pragma once

#include "OscillatorUnit.h"
#include "FilterUnit.h"
#include "OverdriveAmp.h"
#include "MultiStageEnvelope.h"
#include "Lfo.h"
#include "SynthParams.h"

#include <cstdint>

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

    // note-on. If glideSamples > 0 and glideFromHz > 0 the pitch slides from
    // glideFromHz to the new note over glideSamples samples.
    void start   (int note, float velocity, double glideFromHz = 0.0, double glideSamples = 0.0);
    void release ();                            // note-off -> envelope release

    bool isActive() const noexcept { return env_.isActive(); }
    int  note    () const noexcept { return note_; }

    void setPitchBendSemitones (double semitones) noexcept { pitchBend_ = semitones; }
    void setPressure           (double pressure01) noexcept { pressure_ = pressure01; }
    void setTimbre             (double timbre01)   noexcept { timbre_ = timbre01; }
    void setModWheel           (double modWheel01) noexcept { modWheel_ = modWheel01; }

    // Per-voice unison offsets (detune in cents, pan add in [-1, 1]).
    void setUnison (double detuneCents, double panOffset) noexcept
    { unisonDetuneCents_ = detuneCents; unisonPan_ = panOffset; }

    // Adds `numSamples` of this voice's output into the stereo `left`/`right`
    // buffers, panned to its stereo position.
    void renderBlock (float* left, float* right, int numSamples);

private:
    void  applyModulation() noexcept;   // evaluate the matrix and configure the DSP
    void  advanceDrift    (int numSamples) noexcept;   // step the drift random walks
    float renderOneSample() noexcept;

    OscillatorUnit            unitA_;   // oscillator slot A
    OscillatorUnit            unitB_;   // oscillator slot B
    FilterUnit                filterA_; // filter slot A
    FilterUnit                filterB_; // filter slot B
    OverdriveAmp              amp_;
    MultiStageEnvelope        env_;       // amplitude envelope
    MultiStageEnvelope        env2_;      // modulation envelope
    MultiStageEnvelope        filterEnv_;  // Filter A cutoff envelope
    MultiStageEnvelope        filter2Env_; // Filter B cutoff envelope
    Lfo                       lfo_;
    Lfo                       lfo2_;

    SynthParams params_;
    double sampleRate_ = 44100.0;

    int    note_      = -1;
    double baseFreq_  = 440.0;

    // Glide state (log-domain ramp from start to target frequency).
    double glideStartHz_  = 440.0;
    double glideTargetHz_ = 440.0;
    double glidePos_      = 1.0;    // 0..1 progress (1 = arrived)
    double glideSamples_  = 0.0;
    double pitchBend_ = 0.0;   // semitones
    double velGain_   = 1.0;
    double pressure_  = 1.0;
    double timbre_    = 0.0;
    double modWheel_  = 0.0;
    double ampMod_    = 1.0;    // amplitude modulation multiplier
    double panL_      = 0.70710678;   // equal-power pan gains (default centre)
    double panR_      = 0.70710678;
    double unisonDetuneCents_ = 0.0;  // per-voice unison detune
    double unisonPan_         = 0.0;  // per-voice unison pan offset

    std::uint32_t rng_ = 0x2545F491u;   // per-voice white-noise generator state

    // Analog drift: three independent slow random-walk values (pitch, PD amount,
    // filter cutoff), advanced at a buffer-size-independent rate.
    std::uint32_t driftRng_   = 0x9E3779B9u;
    double        driftPitch_ = 0.0;
    double        driftPd_     = 0.0;
    double        driftCut_    = 0.0;
};

} // namespace pdhybrid
