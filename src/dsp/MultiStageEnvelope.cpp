#include "MultiStageEnvelope.h"
#include <cmath>

namespace pdhybrid {

// Interpolate from a to b as progress p goes 0->1. curve == 0 is linear;
// curve > 0 gives a fast-approach exponential shape (still hits a at 0 and b at 1).
static inline double interp (double a, double b, double p, double curve) noexcept
{
    const double f = (curve == 0.0)
                       ? p
                       : (1.0 - std::exp (-curve * p)) / (1.0 - std::exp (-curve));
    return a + (b - a) * f;
}

void MultiStageEnvelope::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
        sampleRate_ = sampleRateHz;

    stages_.reserve (8);   // avoid audio-thread reallocation in setStages
}

void MultiStageEnvelope::setStages (const std::vector<EnvStage>& stages, int sustainIndex)
{
    setStages (stages.data(), static_cast<int> (stages.size()), sustainIndex);
}

void MultiStageEnvelope::setStages (const EnvStage* stages, int count, int sustainIndex) noexcept
{
    stages_.assign (stages, stages + count);   // reuses capacity once reserved
    sustainIndex_ = sustainIndex;
    loopEnabled_  = false;
}

void MultiStageEnvelope::setLoop (bool enabled, int startIndex, int endIndex) noexcept
{
    loopEnabled_ = enabled;
    loopStart_   = startIndex;
    loopEnd_     = endIndex;
}

void MultiStageEnvelope::setADSR (double attack, double decay, double sustain,
                                  double release, double curve)
{
    if (stages_.size() != 3)
        stages_.resize (3);   // one-time allocation; in-place thereafter

    stages_[0] = { 1.0,     attack,  curve };   // attack  -> full
    stages_[1] = { sustain, decay,   curve };   // decay   -> sustain level
    stages_[2] = { 0.0,     release, curve };   // release -> silence

    sustainIndex_ = 1;        // hold after the decay stage
    loopEnabled_  = false;
}

void MultiStageEnvelope::startStage (int index) noexcept
{
    curStage_        = index;
    stageStartLevel_ = output_;
    phase_           = 0.0;
    sustaining_      = false;

    const double samples = stages_[static_cast<std::size_t> (index)].timeSeconds * sampleRate_;
    phaseInc_ = (samples > 1.0) ? (1.0 / samples) : 1.0;   // instant if <= 1 sample
}

void MultiStageEnvelope::advanceStage () noexcept
{
    if (! releasing_ && noteHeld_)
    {
        if (loopEnabled_ && curStage_ == loopEnd_) { startStage (loopStart_); return; }
        if (curStage_ == sustainIndex_)            { sustaining_ = true;      return; }
    }

    if (curStage_ + 1 < static_cast<int> (stages_.size()))
    {
        startStage (curStage_ + 1);
    }
    else
    {
        active_     = false;
        sustaining_ = false;
    }
}

void MultiStageEnvelope::noteOn () noexcept
{
    if (stages_.empty()) { active_ = false; return; }

    active_     = true;
    noteHeld_   = true;
    releasing_  = false;
    sustaining_ = false;
    startStage (0);
}

void MultiStageEnvelope::noteOff () noexcept
{
    noteHeld_ = false;
    if (! active_)
        return;

    releasing_  = true;
    sustaining_ = false;

    // Release = the stages after the sustain point; with no sustain point, ramp
    // through the final stage to its end level.
    int rel = (sustainIndex_ < 0) ? (static_cast<int> (stages_.size()) - 1)
                                   : (sustainIndex_ + 1);

    if (rel >= 0 && rel < static_cast<int> (stages_.size()))
        startStage (rel);
    else
        active_ = false;
}

void MultiStageEnvelope::reset () noexcept
{
    active_     = false;
    noteHeld_   = false;
    releasing_  = false;
    sustaining_ = false;
    curStage_   = 0;
    phase_      = 0.0;
    output_     = 0.0;
}

float MultiStageEnvelope::processSample () noexcept
{
    if (! active_ || sustaining_)
        return static_cast<float> (output_);

    phase_ += phaseInc_;
    const double p = (phase_ >= 1.0) ? 1.0 : phase_;

    const EnvStage& s = stages_[static_cast<std::size_t> (curStage_)];
    output_ = interp (stageStartLevel_, s.level, p, s.curve);

    if (phase_ >= 1.0)
    {
        output_ = s.level;      // snap exactly to the breakpoint
        advanceStage();
    }

    return static_cast<float> (output_);
}

} // namespace pdhybrid
