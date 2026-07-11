#include "Voice.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kPi = 3.14159265358979323846;

double midiNoteToHz (int note) noexcept
{
    return 440.0 * std::pow (2.0, (note - 69) / 12.0);
}

double Voice::noteHz (int note) const noexcept
{
    return params_.masterTuneHz * std::pow (2.0, (note + params_.transpose - 69) / 12.0);
}

void Voice::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    unitA_.setSampleRate (sampleRate);
    unitB_.setSampleRate (sampleRate);
    filterA_.setSampleRate (sampleRate);
    filterB_.setSampleRate (sampleRate);
    amp_.setSampleRate (sampleRate);
    amp_.setOversampling (4);
    env_.setSampleRate (sampleRate);
    env2_.setSampleRate (sampleRate);
    filterEnv_.setSampleRate (sampleRate);
    filter2Env_.setSampleRate (sampleRate);
    multiEnv_.setSampleRate (sampleRate);
    pitchEnv_.setSampleRate (sampleRate);
    lfo_.setSampleRate (sampleRate);
    lfo2_.setSampleRate (sampleRate);

    unitA_.reset();
    unitB_.reset();
    filterA_.reset();
    filterB_.reset();
    amp_.reset();
    env_.reset();
    env2_.reset();
    filterEnv_.reset();
    filter2Env_.reset();
    multiEnv_.reset();
    pitchEnv_.reset();
    lfo_.reset();
    lfo2_.reset();
}

void Voice::setParams (const SynthParams& params)
{
    params_ = params;
    amp_.setBias (params.bias);
    amp_.setCurve (static_cast<ShaperCurve> (params.driveType));
    amp_.setCrushBits (params.crushBits);
    amp_.setDownsample (static_cast<int> (params.downsample));
    env_.setADSR (params.attack, params.decay, params.sustain, params.release);
    env2_.setADSR (params.modEnvA, params.modEnvD, params.modEnvS, params.modEnvR);
    filterEnv_.setADSR (params.filterEnvA, params.filterEnvD, params.filterEnvS, params.filterEnvR);
    filter2Env_.setADSR (params.filter2EnvA, params.filter2EnvD, params.filter2EnvS, params.filter2EnvR);

    // CZ multi-stage envelope: 8 {level,time} stages, one sustain stage. Built
    // into a preallocated member array so no heap allocation happens per block.
    for (int i = 0; i < 8; ++i)
        czStages_[i] = { params.czLevel[i], params.czRate[i], 0.0 };
    multiEnv_.setStages (czStages_.data(), 8, std::clamp (params.czSustain - 1, 0, 7));

    // CZ pitch (DCO) envelope: same 8-stage structure, drives pitch.
    for (int i = 0; i < 8; ++i)
        pitchStages_[i] = { params.pitchEnvLevel[i], params.pitchEnvRate[i], 0.0 };
    pitchEnv_.setStages (pitchStages_.data(), 8, std::clamp (params.pitchEnvSustain - 1, 0, 7));

    lfo_.setFrequency (params.lfoRate);
    lfo_.setWaveform (static_cast<LfoWave> (params.lfoWave));
    lfo_.setFadeIn (params.lfoFade);
    lfo_.setPhaseOffset (params.lfoPhase);
    lfo2_.setFrequency (params.lfo2Rate);
    lfo2_.setWaveform (static_cast<LfoWave> (params.lfo2Wave));
    lfo2_.setFadeIn (params.lfo2Fade);
    lfo2_.setPhaseOffset (params.lfo2Phase);

    unitA_.setType   (params.oscAType);
    unitA_.setPdWave (static_cast<PdWave> (params.oscAWave));
    unitA_.setPdWaveB (static_cast<PdWave> (params.oscAWave2));
    unitA_.setPdCombine (params.oscACombine);
    unitA_.setTuning (params.oscAOctave, params.oscASemi, params.oscAFine);
    unitA_.setEq     (params.oscAEqLow, params.oscAEqMid, params.oscAEqHigh);

    unitB_.setType   (params.oscBType);
    unitB_.setPdWave (static_cast<PdWave> (params.oscBWave));
    unitB_.setPdWaveB (static_cast<PdWave> (params.oscBWave2));
    unitB_.setPdCombine (params.oscBCombine);
    unitB_.setTuning (params.oscBOctave, params.oscBSemi, params.oscBFine);
    unitB_.setEq     (params.oscBEqLow, params.oscBEqMid, params.oscBEqHigh);

    filterA_.setType (params.filterType);
    filterB_.setType (params.filter2Type);

    // Oversampling changes rebuild the internal FIR state, so only apply on a
    // real change (setParams runs every block).
    if (params.oscOversampling != oversampling_)
    {
        oversampling_ = params.oscOversampling;
        unitA_.setOversampling (oversampling_);
        unitB_.setOversampling (oversampling_);
        amp_.setOversampling (oversampling_);
    }
}

void Voice::applyModulation() noexcept
{
    ModSources src;
    src[ModSource::ModEnv]    = env2_.level();
    src[ModSource::Lfo]       = lfo_.value();
    src[ModSource::Lfo2]      = lfo2_.value();
    src[ModSource::Velocity]  = velGain_;
    src[ModSource::Pressure]  = pressure_;
    src[ModSource::Timbre]    = timbre_;
    src[ModSource::PitchBend] = pitchBend_ / 12.0;
    src[ModSource::KeyTrack]  = (note_ - 60) / 48.0;
    src[ModSource::ModWheel]  = modWheel_;
    src[ModSource::MultiEnv]  = multiEnv_.level();
    src[ModSource::AmpEnv]     = env_.level();
    src[ModSource::FilterEnvA] = filterEnv_.level();
    src[ModSource::FilterEnvB] = filter2Env_.level();
    src[ModSource::Random]     = randomMod_;
    src[ModSource::Macro1]     = params_.macro1;
    src[ModSource::Macro2]     = params_.macro2;
    // GlobalLfo stays 0 here; it drives the global dests in the processor.

    double mod[ModMatrix::kNumDests];
    params_.modMatrix.evaluate (src, mod);

    auto md = [&] (ModDest d) { return mod[static_cast<int> (d)]; };

    // Analog drift: apply the current random-walk values (advanced per block by
    // advanceDrift) to pitch, PD amount and filter cutoff for an "unstable
    // analog" feel driven entirely by the single drift knob.
    const double driftSemis  = params_.drift * driftPitch_ * 2.0;    // +/- 2 semitones
    const double driftPdAmt  = params_.drift * driftPd_    * 0.6;    // +/- 0.6 DCW
    const double driftCutOct = params_.drift * driftCut_   * 1.0;    // +/- 1 octave

    // CZ pitch (DCO) envelope: bipolar around 0.5, scaled to semitones.
    const double pitchEnvSemis = params_.pitchEnvAmount * (pitchEnv_.level() - 0.5) * 2.0;

    // CZ noise pitch modulation: chaotic per-chunk pitch grit.
    double noiseSemis = 0.0;
    if (params_.noiseModDepth > 1.0e-6)
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        const double nz = static_cast<double> (static_cast<std::int32_t> (rng_)) / 2147483648.0;
        noiseSemis = params_.noiseModDepth * nz * 12.0;   // +/- 12 semitones at full
    }

    // Pitch (matrix in semitones, +/-24 at full depth). Each unit applies its
    // own octave/semi/fine tuning on top of this note pitch.
    const double semis = pitchBend_ + md (ModDest::Pitch) * 24.0 + driftSemis
                       + pitchEnvSemis + noiseSemis
                       + unisonDetuneCents_ / 100.0
                       + md (ModDest::Detune) * 0.5;   // +/- 50 cents at full depth
    const double freq  = baseFreq_ * std::pow (2.0, semis / 12.0);
    unitA_.setBaseFrequency (freq);
    unitB_.setBaseFrequency (freq);

    // PD amount and pulse width are modulated equally on both slots.
    const double pdMod = md (ModDest::PdAmount) + driftPdAmt;
    const double pwMod = md (ModDest::PulseWidth) * 0.45;
    unitA_.setAmount     (std::clamp (params_.oscAAmount + pdMod, 0.0, 1.0));
    unitB_.setAmount     (std::clamp (params_.oscBAmount + pdMod, 0.0, 1.0));
    unitA_.setPulseWidth (std::clamp (params_.oscAPulseWidth + pwMod, 0.05, 0.95));
    unitB_.setPulseWidth (std::clamp (params_.oscBPulseWidth + pwMod, 0.05, 0.95));

    // Cutoff: timbre (MPE), matrix, key tracking and the filter envelope all
    // shift the cutoff in octaves. Key tracking follows the note relative to
    // middle C; the filter envelope depth is bipolar (in octaves).
    const double keyOct   = params_.keyTrack * (note_ - 60) / 12.0;
    const double czOct    = params_.czAmount * multiEnv_.level();   // CZ multi-stage -> cutoff
    const double sharedOct = timbre_ * 3.0 + keyOct + driftCutOct + czOct;
    // Velocity scaling of the filter-envelope depth.
    const double velFilt  = (1.0 - params_.filterVelSens) + params_.filterVelSens * velGain_;
    const double octA     = sharedOct + md (ModDest::Cutoff) * 4.0
                          + params_.filterEnvAmount  * filterEnv_.level() * velFilt;
    const double octB     = sharedOct + md (ModDest::Filter2Cutoff) * 4.0
                          + params_.filter2EnvAmount * filter2Env_.level() * velFilt;
    const double resMod   = md (ModDest::Resonance);
    const double morphMod = md (ModDest::Morph);

    filterA_.configure (params_.cutoffHz * std::pow (2.0, octA),
                        std::clamp (params_.resonance + resMod, 0.0, 1.0),
                        std::clamp (params_.filterMorph + morphMod, 0.0, 1.0));
    filterB_.configure (params_.filter2Cutoff * std::pow (2.0, octB),
                        std::clamp (params_.filter2Res + resMod, 0.0, 1.0),
                        std::clamp (params_.filter2Morph + morphMod, 0.0, 1.0));

    amp_.setDrive (params_.drive * std::pow (2.0, md (ModDest::Drive) * 2.0));

    ampMod_ = std::clamp (1.0 + md (ModDest::Amplitude), 0.0, 4.0);

    // Velocity scaling of the amp level (0 sens = velocity ignored).
    velAmp_ = (1.0 - params_.ampVelSens) + params_.ampVelSens * velGain_;

    // Mixer levels after matrix modulation.
    oscALevelMod_  = std::clamp (params_.oscALevel + md (ModDest::OscALevel), 0.0, 1.0);
    oscBLevelMod_  = std::clamp (params_.oscBLevel + md (ModDest::OscBLevel), 0.0, 1.0);
    noiseLevelMod_ = std::clamp (params_.noiseLevel + md (ModDest::NoiseLevel), 0.0, 1.0);

    // LFO rates can be modulated (+/- 2 octaves); no change when unrouted.
    lfo_.setFrequency  (params_.lfoRate  * std::pow (2.0, md (ModDest::LfoRate)  * 2.0));
    lfo2_.setFrequency (params_.lfo2Rate * std::pow (2.0, md (ModDest::Lfo2Rate) * 2.0));

    // Stereo position: master pan plus keyboard-position spread and matrix pan,
    // then equal-power constant-power law (centre = -3 dB each side).
    const double pan   = std::clamp (params_.pan
                                     + params_.panSpread * (note_ - 60) / 24.0
                                     + unisonPan_ + md (ModDest::Pan), -1.0, 1.0);
    const double angle = (pan + 1.0) * 0.25 * kPi;   // 0..pi/2
    panL_ = std::cos (angle);
    panR_ = std::sin (angle);
}

void Voice::advanceDrift (int numSamples) noexcept
{
    // Pull each random walk toward a fresh target with a time-constant-based
    // coefficient, so the wander speed is the same regardless of block size.
    auto nextRand = [&]
    {
        driftRng_ = driftRng_ * 1664525u + 1013904223u;
        return static_cast<double> (static_cast<std::int32_t> (driftRng_)) / 2147483648.0;
    };
    const double tau  = 0.2;   // ~200 ms wander time constant
    const double coef = 1.0 - std::exp (-static_cast<double> (numSamples)
                                        / (sampleRate_ * tau));
    driftPitch_ += coef * (nextRand() - driftPitch_);
    driftPd_    += coef * (nextRand() - driftPd_);
    driftCut_   += coef * (nextRand() - driftCut_);
}

void Voice::start (int note, float velocity, double glideFromHz, double glideSamples)
{
    note_          = note;
    glideTargetHz_ = noteHz (note);
    if (glideFromHz > 0.0 && glideSamples > 0.5)
    {
        glideStartHz_ = glideFromHz;
        glideSamples_ = glideSamples;
        glidePos_     = 0.0;
        baseFreq_     = glideFromHz;
    }
    else
    {
        glideStartHz_ = glideTargetHz_;
        glideSamples_ = 0.0;
        glidePos_     = 1.0;
        baseFreq_     = glideTargetHz_;
    }
    velGain_   = velocity;
    pressure_  = 1.0;
    timbre_    = 0.0;
    pitchBend_ = 0.0;
    rng_ = rng_ * 1664525u + 1013904223u;   // fresh per-note sample & hold value
    randomMod_ = static_cast<double> (static_cast<std::int32_t> (rng_)) / 2147483648.0;
    if (params_.lfoRetrig)  lfo_.reset();
    if (params_.lfo2Retrig) lfo2_.reset();
    lfo_.trigger();
    lfo2_.trigger();
    env2_.noteOn();
    filterEnv_.noteOn();
    filter2Env_.noteOn();
    multiEnv_.noteOn();
    pitchEnv_.noteOn();
    env_.noteOn();
    applyModulation();
}

void Voice::changeNote (int note, double glideFromHz, double glideSamples)
{
    // Legato: retune (optionally gliding) but leave every envelope/LFO running.
    note_          = note;
    glideTargetHz_ = noteHz (note);
    if (glideFromHz > 0.0 && glideSamples > 0.5)
    {
        glideStartHz_ = glideFromHz;
        glideSamples_ = glideSamples;
        glidePos_     = 0.0;
        baseFreq_     = glideFromHz;
    }
    else
    {
        glideStartHz_ = glideTargetHz_;
        glideSamples_ = 0.0;
        glidePos_     = 1.0;
        baseFreq_     = glideTargetHz_;
    }
    applyModulation();
}

void Voice::release()
{
    env_.noteOff();
    env2_.noteOff();
    filterEnv_.noteOff();
    filter2Env_.noteOff();
    multiEnv_.noteOff();
    pitchEnv_.noteOff();
}

float Voice::renderOneSample() noexcept
{
    // Osc A + Osc B with optional cross-modulation. Osc B runs when it is mixed,
    // ring-modulated, or cross-modulating (hard sync / phase mod).
    const double ring  = params_.ringModLevel;
    const int    cross = params_.oscCrossMod;
    const bool   needB = (oscBLevelMod_ > 1.0e-5) || (ring > 1.0e-5) || (cross != 0);

    double sA, sB = 0.0;
    if (cross == 2)          // Phase Mod: Osc B modulates Osc A's phase
    {
        sB = unitB_.processSample();
        unitA_.setPhaseMod (sB * params_.crossModAmount * 0.5);
        sA = unitA_.processSample();
    }
    else if (cross == 1)     // Hard Sync: Osc A masters Osc B (reset on A's wrap)
    {
        unitA_.setPhaseMod (0.0);
        sA = unitA_.processSample();
        if (unitA_.wrapped())
            unitB_.syncReset();
        sB = unitB_.processSample();
    }
    else                     // Off
    {
        unitA_.setPhaseMod (0.0);
        sA = unitA_.processSample();
        if (needB)
            sB = unitB_.processSample();
    }

    double s = sA * oscALevelMod_ + sB * oscBLevelMod_;
    if (ring > 1.0e-5)
        s += sA * sB * ring;   // CZ line ring modulation

    if (noiseLevelMod_ > 1.0e-5)
    {
        rng_ = rng_ * 1664525u + 1013904223u;   // cheap white-noise LCG in [-1, 1)
        s += (static_cast<double> (static_cast<std::int32_t> (rng_)) / 2147483648.0)
             * noiseLevelMod_;
    }

    // Overdrive can sit before the filter (pre) or after it (post, default).
    if (params_.driveOn && params_.drivePos == 1)
        s = amp_.processSample (static_cast<float> (s));

    const float in = static_cast<float> (s);
    switch (params_.filterRouting)
    {
        case FilterRouting::Series:
            s = filterB_.processSample (filterA_.processSample (in));
            break;
        case FilterRouting::Parallel:
            s = 0.5f * (filterA_.processSample (in) + filterB_.processSample (in));
            break;
        case FilterRouting::Single:
        default:
            s = filterA_.processSample (in);
            break;
    }

    if (params_.driveOn && params_.drivePos == 0)
        s = amp_.processSample (static_cast<float> (s));
    const double e = env_.processSample();
    return static_cast<float> (s * e * velAmp_ * pressure_ * ampMod_ * params_.gain);
}

void Voice::renderBlock (float* left, float* right, int numSamples)
{
    advanceDrift (numSamples);

    // Re-evaluate glide + modulation every kCtrl samples rather than once per
    // buffer. This keeps cutoff / pitch / amp / pan modulation smooth (no
    // block-rate zipper) and follows fast envelopes/LFOs closely, independent of
    // the host's buffer size.
    constexpr int kCtrl = 32;

    for (int done = 0; done < numSamples; )
    {
        const int chunk = std::min (kCtrl, numSamples - done);

        if (glidePos_ < 1.0 && glideSamples_ > 0.0)
        {
            glidePos_ = std::min (1.0, glidePos_ + chunk / glideSamples_);
            const double exp  = params_.glideCurve > 0.0 ? params_.glideCurve : 1.0;
            const double t    = std::pow (glidePos_, exp);
            const double logHz = std::log (glideStartHz_)
                               + t * (std::log (glideTargetHz_) - std::log (glideStartHz_));
            baseFreq_ = std::exp (logHz);
        }

        applyModulation();

        for (int i = 0; i < chunk; ++i)
        {
            const float s = renderOneSample();
            left[done + i]  += static_cast<float> (s * panL_);
            right[done + i] += static_cast<float> (s * panR_);
            // Envelopes advance per sample (cheap when sustaining, and their
            // shapes are short-lived); they are only read at the next chunk.
            env2_.processSample();
            filterEnv_.processSample();
            filter2Env_.processSample();
            multiEnv_.processSample();
            pitchEnv_.processSample();
        }

        // LFO values are read only once per control chunk, so advance them by
        // the whole chunk instead of computing a waveform every sample. advance()
        // is exactly equivalent to per-sample stepping (see modulation tests).
        lfo_.advance (chunk);
        lfo2_.advance (chunk);

        done += chunk;
    }
}

} // namespace pdhybrid
