#include "FilterUnit.h"

namespace pdhybrid {

void FilterUnit::setSampleRate (double sampleRateHz) noexcept
{
    ladder_.setSampleRate (sampleRateHz);
    svf_.setSampleRate (sampleRateHz);
    pdReso_.setSampleRate (sampleRateHz);
    comb_.setSampleRate (sampleRateHz);
    // AllpassDispersion is sample-rate independent (pure phase network).
}

void FilterUnit::reset() noexcept
{
    ladder_.reset();
    svf_.reset();
    pdReso_.reset();
    comb_.reset();
    allpass_.reset();
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
            comb_.setFeedback (0.5 + 0.49 * resonance);
            comb_.setDamping (morph);
            break;
        case FilterType::Allpass:
            allpass_.setCoefficient (-0.95 + 1.9 * resonance);
            allpass_.setStages (2 + static_cast<int> (morph * 10.0));
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
        case FilterType::Allpass:       return allpass_.processSample (x);
        case FilterType::Ladder:
        default:                        return ladder_.processSample (x);
    }
}

} // namespace pdhybrid
