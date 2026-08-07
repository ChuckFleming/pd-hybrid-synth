#include "OscillatorUnit.h"
#include <cmath>

namespace pdhybrid {

void OscillatorUnit::setSampleRate (double sampleRateHz) noexcept
{
    pd_.setSampleRate (sampleRateHz);
    pd_.setOversampling (4);          // anti-alias the aggressive CZ waves
    vps_.setSampleRate (sampleRateHz);
    vps_.setOversampling (4);         // VPS aliases like PD -> oversample too
    scanned_.setSampleRate (sampleRateHz);
    scanned_.setOversampling (4);
    vosim_.setSampleRate (sampleRateHz);
    vosim_.setOversampling (4);
    walsh_.setSampleRate (sampleRateHz);
    walsh_.setOversampling (4);
    supersaw_.setSampleRate (sampleRateHz);
    supersaw_.setOversampling (4);
    harmonic_.setSampleRate (sampleRateHz);
    harmonic_.setOversampling (4);
    paf_.setSampleRate (sampleRateHz);
    paf_.setOversampling (4);
    granular_.setSampleRate (sampleRateHz);
    granular_.setOversampling (4);
    wavetable_.setSampleRate (sampleRateHz);
    wavetable_.setOversampling (4);
    analog_.setSampleRate (sampleRateHz);
    eq_.setSampleRate (sampleRateHz);
}

void OscillatorUnit::reset() noexcept
{
    pd_.reset();
    vps_.reset();
    scanned_.reset();
    vosim_.reset();
    walsh_.reset();
    supersaw_.reset();
    harmonic_.reset();
    paf_.reset();
    granular_.reset();
    wavetable_.reset();
    analog_.reset();
    eq_.reset();
}

void OscillatorUnit::setType (OscType type) noexcept
{
    switch (type)
    {
        case OscType::Saw:      analog_.setWaveform (AnalogWave::Saw);      break;
        case OscType::Square:   analog_.setWaveform (AnalogWave::Square);   break;
        case OscType::Triangle: analog_.setWaveform (AnalogWave::Triangle); break;
        case OscType::Pulse:    analog_.setWaveform (AnalogWave::Pulse);    break;
        default: break;   // the rest are configured through the shared controls
    }

    if (type == type_)
        return;   // setParams runs every block; only a real change needs the rest

    type_ = type;

    // The shared controls only ever reached the previously selected engine, so
    // hand the newly selected one the current values before it is asked for a
    // sample. Order matters no more than it does on the normal path.
    setAmount      (amount_);
    setPulseWidth  (pulseWidth_);
    setEngineParam (engineParam_);
    setPhaseMod    (phaseMod_);
    setBaseFrequency (baseHz_);
}

void OscillatorUnit::setTuning (int octave, int semitone, double fineCents) noexcept
{
    if (octave == tuneOct_ && semitone == tuneSemi_ && fineCents == tuneFine_)
        return;   // unchanged: skip the pow() (base frequency is re-applied elsewhere)

    tuneOct_ = octave; tuneSemi_ = semitone; tuneFine_ = fineCents;
    const double semis = octave * 12.0 + semitone + fineCents / 100.0;
    tuneMul_ = std::pow (2.0, semis / 12.0);
    setBaseFrequency (baseHz_);
}

void OscillatorUnit::setBaseFrequency (double frequencyHz) noexcept
{
    baseHz_ = frequencyHz;
    const double f = frequencyHz * tuneMul_;

    // Selected engine only: this runs every control chunk, and the additive
    // engine re-derives its harmonic ceiling (and so its table) from pitch.
    switch (type_)
    {
        case OscType::PhaseDistortion: pd_.setFrequency (f);       break;
        case OscType::VPS:             vps_.setFrequency (f);      break;
        case OscType::Scanned:         scanned_.setFrequency (f);  break;
        case OscType::Vosim:           vosim_.setFrequency (f);    break;
        case OscType::Walsh:           walsh_.setFrequency (f);    break;
        case OscType::Supersaw:        supersaw_.setFrequency (f); break;
        case OscType::Harmonic:        harmonic_.setFrequency (f); break;
        case OscType::Paf:             paf_.setFrequency (f);      break;
        case OscType::Granular:        granular_.setFrequency (f); break;
        case OscType::Wavetable:       wavetable_.setFrequency (f); break;
        default:                       analog_.setFrequency (f);   break;
    }
}

float OscillatorUnit::processSample() noexcept
{
    // Only the selected engine runs. The PD engine is 4x oversampled and by far
    // the most expensive part of a voice, so running both (the old click-safe
    // approach) wasted roughly half the oscillator cost. The trade-off is a
    // possible small discontinuity if the type is switched live mid-note, which
    // is a rare, user-initiated action.
    const float raw = (type_ == OscType::PhaseDistortion) ? pd_.processSample()
                    : (type_ == OscType::VPS)             ? vps_.processSample()
                    : (type_ == OscType::Scanned)         ? scanned_.processSample()
                    : (type_ == OscType::Vosim)           ? vosim_.processSample()
                    : (type_ == OscType::Walsh)           ? walsh_.processSample()
                    : (type_ == OscType::Supersaw)        ? supersaw_.processSample()
                    : (type_ == OscType::Harmonic)        ? harmonic_.processSample()
                    : (type_ == OscType::Paf)             ? paf_.processSample()
                    : (type_ == OscType::Granular)        ? granular_.processSample()
                    : (type_ == OscType::Wavetable)       ? wavetable_.processSample()
                                                          : analog_.processSample();
    return eq_.processSample (raw);
}

} // namespace pdhybrid
