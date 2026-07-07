#include "Voice.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

double midiNoteToHz (int note) noexcept
{
    return 440.0 * std::pow (2.0, (note - 69) / 12.0);
}

void Voice::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    osc_.setSampleRate (sampleRate);
    analogOsc_.setSampleRate (sampleRate);
    ladder_.setSampleRate (sampleRate);
    pdReso_.setSampleRate (sampleRate);
    comb_.setSampleRate (sampleRate);
    amp_.setSampleRate (sampleRate);
    amp_.setOversampling (4);
    env_.setSampleRate (sampleRate);
    env2_.setSampleRate (sampleRate);
    lfo_.setSampleRate (sampleRate);

    osc_.reset();
    analogOsc_.reset();
    ladder_.reset();
    pdReso_.reset();
    comb_.reset();
    allpass_.reset();
    amp_.reset();
    env_.reset();
    env2_.reset();
    lfo_.reset();
}

void Voice::setParams (const SynthParams& params)
{
    params_ = params;
    amp_.setBias (params.bias);
    env_.setADSR (params.attack, params.decay, params.sustain, params.release);
    env2_.setADSR (params.modEnvA, params.modEnvD, params.modEnvS, params.modEnvR);
    lfo_.setFrequency (params.lfoRate);
    lfo_.setWaveform (static_cast<LfoWave> (params.lfoWave));

    switch (params.oscType)
    {
        case OscType::Saw:      analogOsc_.setWaveform (AnalogWave::Saw);      break;
        case OscType::Square:   analogOsc_.setWaveform (AnalogWave::Square);   break;
        case OscType::Triangle: analogOsc_.setWaveform (AnalogWave::Triangle); break;
        case OscType::Pulse:    analogOsc_.setWaveform (AnalogWave::Pulse);    break;
        case OscType::PhaseDistortion: default: break;
    }
}

void Voice::applyModulation() noexcept
{
    ModSources src;
    src[ModSource::ModEnv]    = env2_.level();
    src[ModSource::Lfo]       = lfo_.value();
    src[ModSource::Velocity]  = velGain_;
    src[ModSource::Pressure]  = pressure_;
    src[ModSource::Timbre]    = timbre_;
    src[ModSource::PitchBend] = pitchBend_ / 12.0;
    src[ModSource::KeyTrack]  = (note_ - 60) / 48.0;
    src[ModSource::ModWheel]  = modWheel_;

    double mod[ModMatrix::kNumDests];
    params_.modMatrix.evaluate (src, mod);

    auto md = [&] (ModDest d) { return mod[static_cast<int> (d)]; };

    // Pitch (matrix in semitones, +/-24 at full depth).
    const double semis = pitchBend_ + md (ModDest::Pitch) * 24.0;
    const double freq  = baseFreq_ * std::pow (2.0, semis / 12.0);
    osc_.setFrequency (freq);
    analogOsc_.setFrequency (freq);

    osc_.setAmount (std::clamp (params_.pdAmount + md (ModDest::PdAmount), 0.0, 1.0));
    analogOsc_.setPulseWidth (std::clamp (params_.pulseWidth + md (ModDest::PulseWidth) * 0.45, 0.05, 0.95));

    // Cutoff: timbre (MPE) and matrix both in octaves.
    const double cutoff = params_.cutoffHz * std::pow (2.0, timbre_ * 3.0 + md (ModDest::Cutoff) * 4.0);
    const double res    = std::clamp (params_.resonance + md (ModDest::Resonance), 0.0, 1.0);
    const double morph  = std::clamp (params_.filterMorph + md (ModDest::Morph), 0.0, 1.0);

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

    amp_.setDrive (params_.drive * std::pow (2.0, md (ModDest::Drive) * 2.0));

    ampMod_ = std::clamp (1.0 + md (ModDest::Amplitude), 0.0, 4.0);
}

void Voice::start (int note, float velocity)
{
    note_      = note;
    baseFreq_  = midiNoteToHz (note);
    velGain_   = velocity;
    pressure_  = 1.0;
    timbre_    = 0.0;
    pitchBend_ = 0.0;
    lfo_.reset();
    env2_.noteOn();
    env_.noteOn();
    applyModulation();
}

void Voice::release()
{
    env_.noteOff();
    env2_.noteOff();
}

float Voice::renderOneSample() noexcept
{
    double s = (params_.oscType == OscType::PhaseDistortion)
                   ? osc_.processSample()
                   : analogOsc_.processSample();

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
    return static_cast<float> (s * e * velGain_ * pressure_ * ampMod_ * params_.gain);
}

void Voice::renderBlock (float* out, int numSamples)
{
    applyModulation();   // control-rate: evaluate the matrix once per block

    for (int i = 0; i < numSamples; ++i)
    {
        out[i] += renderOneSample();
        lfo_.processSample();    // advance modulation sources in sync
        env2_.processSample();
    }
}

} // namespace pdhybrid
