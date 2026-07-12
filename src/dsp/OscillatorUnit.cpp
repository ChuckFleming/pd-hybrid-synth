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
    analog_.setSampleRate (sampleRateHz);
    eq_.setSampleRate (sampleRateHz);
}

void OscillatorUnit::reset() noexcept
{
    pd_.reset();
    vps_.reset();
    scanned_.reset();
    vosim_.reset();
    analog_.reset();
    eq_.reset();
}

void OscillatorUnit::setType (OscType type) noexcept
{
    type_ = type;
    switch (type)
    {
        case OscType::Saw:      analog_.setWaveform (AnalogWave::Saw);      break;
        case OscType::Square:   analog_.setWaveform (AnalogWave::Square);   break;
        case OscType::Triangle: analog_.setWaveform (AnalogWave::Triangle); break;
        case OscType::Pulse:    analog_.setWaveform (AnalogWave::Pulse);    break;
        case OscType::VPS:                                                  break;   // configured via amount/pulseWidth
        case OscType::Scanned:                                              break;   // configured via amount/pulseWidth + excite()
        case OscType::Vosim:                                                break;   // configured via amount/pulseWidth
        case OscType::PhaseDistortion: default: break;
    }
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
    pd_.setFrequency (f);
    vps_.setFrequency (f);
    scanned_.setFrequency (f);
    vosim_.setFrequency (f);
    analog_.setFrequency (f);
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
                                                          : analog_.processSample();
    return eq_.processSample (raw);
}

} // namespace pdhybrid
