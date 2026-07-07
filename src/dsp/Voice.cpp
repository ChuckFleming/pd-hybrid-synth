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
    unitA_.setSampleRate (sampleRate);
    unitB_.setSampleRate (sampleRate);
    ladder_.setSampleRate (sampleRate);
    svf_.setSampleRate (sampleRate);
    pdReso_.setSampleRate (sampleRate);
    comb_.setSampleRate (sampleRate);
    amp_.setSampleRate (sampleRate);
    amp_.setOversampling (4);
    env_.setSampleRate (sampleRate);
    env2_.setSampleRate (sampleRate);
    filterEnv_.setSampleRate (sampleRate);
    lfo_.setSampleRate (sampleRate);

    unitA_.reset();
    unitB_.reset();
    ladder_.reset();
    svf_.reset();
    pdReso_.reset();
    comb_.reset();
    allpass_.reset();
    amp_.reset();
    env_.reset();
    env2_.reset();
    filterEnv_.reset();
    lfo_.reset();
}

void Voice::setParams (const SynthParams& params)
{
    params_ = params;
    amp_.setBias (params.bias);
    env_.setADSR (params.attack, params.decay, params.sustain, params.release);
    env2_.setADSR (params.modEnvA, params.modEnvD, params.modEnvS, params.modEnvR);
    filterEnv_.setADSR (params.filterEnvA, params.filterEnvD, params.filterEnvS, params.filterEnvR);
    lfo_.setFrequency (params.lfoRate);
    lfo_.setWaveform (static_cast<LfoWave> (params.lfoWave));

    unitA_.setType   (params.oscAType);
    unitA_.setPdWave (static_cast<PdWave> (params.oscAWave));
    unitA_.setTuning (params.oscAOctave, params.oscASemi, params.oscAFine);

    unitB_.setType   (params.oscBType);
    unitB_.setPdWave (static_cast<PdWave> (params.oscBWave));
    unitB_.setTuning (params.oscBOctave, params.oscBSemi, params.oscBFine);
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

    // Pitch (matrix in semitones, +/-24 at full depth). Each unit applies its
    // own octave/semi/fine tuning on top of this note pitch.
    const double semis = pitchBend_ + md (ModDest::Pitch) * 24.0;
    const double freq  = baseFreq_ * std::pow (2.0, semis / 12.0);
    unitA_.setBaseFrequency (freq);
    unitB_.setBaseFrequency (freq);

    // PD amount and pulse width are modulated equally on both slots.
    const double pdMod = md (ModDest::PdAmount);
    const double pwMod = md (ModDest::PulseWidth) * 0.45;
    unitA_.setAmount     (std::clamp (params_.oscAAmount + pdMod, 0.0, 1.0));
    unitB_.setAmount     (std::clamp (params_.oscBAmount + pdMod, 0.0, 1.0));
    unitA_.setPulseWidth (std::clamp (params_.oscAPulseWidth + pwMod, 0.05, 0.95));
    unitB_.setPulseWidth (std::clamp (params_.oscBPulseWidth + pwMod, 0.05, 0.95));

    // Cutoff: timbre (MPE), matrix, key tracking and the filter envelope all
    // shift the cutoff in octaves. Key tracking follows the note relative to
    // middle C; the filter envelope depth is bipolar (in octaves).
    const double keyOct  = params_.keyTrack * (note_ - 60) / 12.0;
    const double fenvOct = params_.filterEnvAmount * filterEnv_.level();
    const double cutoff  = params_.cutoffHz
                         * std::pow (2.0, timbre_ * 3.0 + md (ModDest::Cutoff) * 4.0
                                          + keyOct + fenvOct);
    const double res    = std::clamp (params_.resonance + md (ModDest::Resonance), 0.0, 1.0);
    const double morph  = std::clamp (params_.filterMorph + md (ModDest::Morph), 0.0, 1.0);

    ladder_.setCutoff (cutoff);
    ladder_.setResonance (res);
    svf_.setCutoff (cutoff);
    svf_.setResonance (res);
    svf_.setMorph (morph);            // morph knob sweeps LP -> BP -> HP
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
    filterEnv_.noteOn();
    env_.noteOn();
    applyModulation();
}

void Voice::release()
{
    env_.noteOff();
    env2_.noteOff();
    filterEnv_.noteOff();
}

float Voice::renderOneSample() noexcept
{
    // White noise via a cheap LCG, mapped to [-1, 1).
    rng_ = rng_ * 1664525u + 1013904223u;
    const double noise = static_cast<double> (static_cast<std::int32_t> (rng_))
                       / 2147483648.0;

    double s = unitA_.processSample() * params_.oscALevel
             + unitB_.processSample() * params_.oscBLevel
             + noise                  * params_.noiseLevel;

    switch (params_.filterType)
    {
        case FilterType::StateVariable: s = svf_.processSample   (static_cast<float> (s)); break;
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
        filterEnv_.processSample();
    }
}

} // namespace pdhybrid
