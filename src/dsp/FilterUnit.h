#pragma once

#include "LadderFilter.h"
#include "StateVariableFilter.h"
#include "PhaseDistortionResonator.h"
#include "CombFilter.h"
#include "AllpassDispersion.h"
#include "FormantFilter.h"
#include "DiodeLadderFilter.h"
#include "BandSplitFilter.h"
#include "SynthParams.h"   // FilterType

namespace pdhybrid {

/**
    One filter "slot": holds all eight filter engines and routes the signal
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
    // filter type is selected. `noteHz` is the pitch of the note being filtered
    // (0 = unknown); only the PD resonator uses it, to sync its ring to the
    // fundamental the way the CZ's resonant waves do.
    void configure (double cutoffHz, double resonance, double morph,
                    double noteHz = 0.0) noexcept;

    float processSample (float x) noexcept;

private:
    FilterType type_ = FilterType::Ladder;

    LadderFilter             ladder_;
    StateVariableFilter      svf_;
    PhaseDistortionResonator pdReso_;
    CombFilter               comb_;
    AllpassDispersion        allpass_;
    FormantFilter            formant_;
    DiodeLadderFilter        diode_;
    BandSplitFilter          bandSplit_;
};

} // namespace pdhybrid
