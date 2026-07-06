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
    ladder_.setSampleRate (sampleRate);
    pdReso_.setSampleRate (sampleRate);
    comb_.setSampleRate (sampleRate);
    amp_.setSampleRate (sampleRate);
    amp_.setOversampling (4);
    env_.setSampleRate (sampleRate);

    osc_.reset();
    ladder_.reset();
    pdReso_.reset();
    comb_.reset();
    allpass_.reset();
    amp_.reset();
    env_.reset();
}

void Voice::applyFilterParams() noexcept
{
    // Timbre (MPE) lifts the filter frequency up to 3 octaves.
    const double cutoff = params_.cutoffHz * std::pow (2.0, timbre_ * 3.0);
    const double res    = params_.resonance;
    const double morph  = params_.filterMorph;

    ladder_.setCutoff (cutoff);
    ladder_.setResonance (res);

    pdReso_.setFrequency (cutoff);
    pdReso_.setResonance (res);
    pdReso_.setAmount (morph);

    comb_.setFrequency (cutoff);
    comb_.setFeedback (0.5 + 0.49 * res);
    comb_.setDamping (morph);

    allpass_.setCoefficient (-0.95 + 1.9 * res);
    allpass_.setStages (2 + static_cast<int> (morph * 10.0));
}

void Voice::setParams (const SynthParams& params)
{
    params_ = params;
    osc_.setAmount (params.pdAmount);
    amp_.setDrive (params.drive);
    amp_.setBias (params.bias);
    env_.setADSR (params.attack, params.decay, params.sustain, params.release);
    applyFilterParams();
}

void Voice::updateFrequency() noexcept
{
    osc_.setFrequency (baseFreq_ * std::pow (2.0, pitchBend_ / 12.0));
}

void Voice::setPitchBendSemitones (double semitones) noexcept
{
    pitchBend_ = semitones;
    updateFrequency();
}

void Voice::setTimbre (double timbre01) noexcept
{
    timbre_ = timbre01;
    applyFilterParams();
}

void Voice::start (int note, float velocity)
{
    note_      = note;
    baseFreq_  = midiNoteToHz (note);
    velGain_   = velocity;
    pressure_  = 1.0;
    timbre_    = 0.0;
    pitchBend_ = 0.0;
    updateFrequency();
    applyFilterParams();
    env_.noteOn();   // oscillator/filter phase left running to avoid clicks
}

void Voice::release()
{
    env_.noteOff();
}

float Voice::render() noexcept
{
    double s = osc_.processSample();

    switch (params_.filterType)
    {
        case FilterType::PdResonator: s = pdReso_.processSample  (static_cast<float> (s)); break;
        case FilterType::Comb:        s = comb_.processSample    (static_cast<float> (s)); break;
        case FilterType::Allpass:     s = allpass_.processSample (static_cast<float> (s)); break;
        case FilterType::Ladder:
        default:                      s = ladder_.processSample  (static_cast<float> (s)); break;
    }

    s = amp_.processSample (static_cast<float> (s));
    const double e = env_.processSample();
    return static_cast<float> (s * e * velGain_ * pressure_ * params_.gain);
}

} // namespace pdhybrid
