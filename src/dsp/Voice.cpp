#include "Voice.h"
#include <cmath>

namespace pdhybrid {

double midiNoteToHz (int note) noexcept
{
    return 440.0 * std::pow (2.0, (note - 69) / 12.0);
}

void Voice::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    osc_.setSampleRate (sampleRate);
    filter_.setSampleRate (sampleRate);
    amp_.setSampleRate (sampleRate);
    amp_.setOversampling (4);
    env_.setSampleRate (sampleRate);
    osc_.reset();
    filter_.reset();
    amp_.reset();
    env_.reset();
}

void Voice::setParams (const SynthParams& params)
{
    params_ = params;
    osc_.setAmount (params.pdAmount);
    filter_.setResonance (params.resonance);
    amp_.setDrive (params.drive);
    amp_.setBias (params.bias);
    env_.setADSR (params.attack, params.decay, params.sustain, params.release);
    updateCutoff();
}

void Voice::updateFrequency() noexcept
{
    const double freq = baseFreq_ * std::pow (2.0, pitchBend_ / 12.0);
    osc_.setFrequency (freq);
}

void Voice::updateCutoff() noexcept
{
    // Timbre lifts the cutoff up to 3 octaves above the base setting.
    filter_.setCutoff (params_.cutoffHz * std::pow (2.0, timbre_ * 3.0));
}

void Voice::setPitchBendSemitones (double semitones) noexcept
{
    pitchBend_ = semitones;
    updateFrequency();
}

void Voice::setTimbre (double timbre01) noexcept
{
    timbre_ = timbre01;
    updateCutoff();
}

void Voice::start (int note, float velocity)
{
    note_     = note;
    baseFreq_ = midiNoteToHz (note);
    velGain_  = velocity;
    pressure_ = 1.0;
    timbre_   = 0.0;
    pitchBend_ = 0.0;
    updateFrequency();
    updateCutoff();
    env_.noteOn();   // oscillator/filter phase left running to avoid clicks
}

void Voice::release()
{
    env_.noteOff();
}

float Voice::render() noexcept
{
    double s = osc_.processSample();
    s = filter_.processSample (static_cast<float> (s));
    s = amp_.processSample (static_cast<float> (s));
    const double e = env_.processSample();
    return static_cast<float> (s * e * velGain_ * pressure_ * params_.gain);
}

} // namespace pdhybrid
