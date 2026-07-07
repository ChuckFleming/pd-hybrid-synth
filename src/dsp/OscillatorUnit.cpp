#include "OscillatorUnit.h"
#include <cmath>

namespace pdhybrid {

void OscillatorUnit::setSampleRate (double sampleRateHz) noexcept
{
    pd_.setSampleRate (sampleRateHz);
    pd_.setOversampling (4);          // anti-alias the aggressive CZ waves
    analog_.setSampleRate (sampleRateHz);
}

void OscillatorUnit::reset() noexcept
{
    pd_.reset();
    analog_.reset();
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
        case OscType::PhaseDistortion: default: break;
    }
}

void OscillatorUnit::setTuning (int octave, int semitone, double fineCents) noexcept
{
    const double semis = octave * 12.0 + semitone + fineCents / 100.0;
    tuneMul_ = std::pow (2.0, semis / 12.0);
    setBaseFrequency (baseHz_);
}

void OscillatorUnit::setBaseFrequency (double frequencyHz) noexcept
{
    baseHz_ = frequencyHz;
    const double f = frequencyHz * tuneMul_;
    pd_.setFrequency (f);
    analog_.setFrequency (f);
}

float OscillatorUnit::processSample() noexcept
{
    // Both engines run so switching is click-safe; only the selected one is read.
    const float pd     = pd_.processSample();
    const float analog = analog_.processSample();
    return (type_ == OscType::PhaseDistortion) ? pd : analog;
}

} // namespace pdhybrid
