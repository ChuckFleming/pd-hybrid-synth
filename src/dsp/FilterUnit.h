#pragma once

#include "LadderFilter.h"
#include "StateVariableFilter.h"
#include "PhaseDistortionResonator.h"
#include "CombFilter.h"
#include "AllpassDispersion.h"
#include "SynthParams.h"   // FilterType

namespace pdhybrid {

/**
    One filter "slot": holds all five filter engines and routes the signal
    through the currently selected one. A Voice owns two of these so they can be
    run singly, in series (A -> B) or in parallel (A + B), each with its own
    type, cutoff, resonance and morph.
*/
class FilterUnit
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset         () noexcept;

    void setType (FilterType type) noexcept { type_ = type; }

    // Applies the (already modulated) cutoff / resonance / morph to whichever
    // filter type is selected.
    void configure (double cutoffHz, double resonance, double morph) noexcept;

    float processSample (float x) noexcept;

private:
    FilterType type_ = FilterType::Ladder;

    LadderFilter             ladder_;
    StateVariableFilter      svf_;
    PhaseDistortionResonator pdReso_;
    CombFilter               comb_;
    AllpassDispersion        allpass_;
};

} // namespace pdhybrid
