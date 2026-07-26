#include "FilterUnit.h"

namespace pdhybrid {

void FilterUnit::setSampleRate (double sampleRateHz) noexcept
{
    ladder_.setSampleRate (sampleRateHz);
    svf_.setSampleRate (sampleRateHz);
    pdReso_.setSampleRate (sampleRateHz);
    comb_.setSampleRate (sampleRateHz);
    allpass_.setSampleRate (sampleRateHz);
    formant_.setSampleRate (sampleRateHz);
    diode_.setSampleRate (sampleRateHz);
}

void FilterUnit::reset() noexcept
{
    ladder_.reset();
    svf_.reset();
    pdReso_.reset();
    comb_.reset();
    allpass_.reset();
    formant_.reset();
    diode_.reset();
}

void FilterUnit::configure (double cutoffHz, double resonance, double morph) noexcept
{
    switch (type_)
    {
        case FilterType::StateVariable:
            svf_.setCutoff (cutoffHz);
            svf_.setResonance (resonance);
            svf_.setMorph (morph);
            break;
        case FilterType::PdResonator:
            pdReso_.setFrequency (cutoffHz);
            pdReso_.setResonance (resonance);
            pdReso_.setAmount (morph);
            break;
        case FilterType::Comb:
            comb_.setFrequency (cutoffHz);
            // Full knob travel: the old 0.5 + 0.49*res floor meant the comb
            // always rang and only half the resonance range did anything.
            comb_.setFeedback (0.995 * resonance);
            comb_.setDamping (morph);
            break;
        case FilterType::Allpass:
            // Cutoff sweeps the phase break point (it used to be ignored
            // entirely, leaving the knob dead), resonance feeds the cascade
            // back on itself and morph sets the number of stages -- i.e. how
            // many notches the dry mix in processSample carves out.
            allpass_.setFrequency (cutoffHz);
            allpass_.setFeedback (0.9 * resonance);
            allpass_.setStages (2 + static_cast<int> (morph * 10.0));
            break;
        case FilterType::Formant:
            formant_.setFrequency (cutoffHz);
            formant_.setResonance (resonance);
            formant_.setVowel (morph);
            break;
        case FilterType::DiodeLadder:
            diode_.setCutoff (cutoffHz);
            diode_.setResonance (resonance);
            break;
        case FilterType::Ladder:
        default:
            ladder_.setCutoff (cutoffHz);
            ladder_.setResonance (resonance);
            break;
    }
}

float FilterUnit::processSample (float x) noexcept
{
    switch (type_)
    {
        case FilterType::StateVariable: return svf_.processSample (x);
        case FilterType::PdResonator:   return pdReso_.processSample (x);
        case FilterType::Comb:          return comb_.processSample (x);
        case FilterType::Allpass:
            // The cascade alone is flat by definition, so on its own it was
            // inaudible as a filter. Summing it with the dry signal turns the
            // phase shift into real notches -- a phaser.
            return 0.5f * (x + allpass_.processSample (x));
        case FilterType::Formant:       return formant_.processSample (x);
        case FilterType::DiodeLadder:   return diode_.processSample (x);
        case FilterType::Ladder:
        default:                        return ladder_.processSample (x);
    }
}

} // namespace pdhybrid
