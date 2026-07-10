#pragma once

#include <vector>

namespace pdhybrid {

struct EnvStage
{
    double level       = 0.0;   // target level reached at the end of the stage
    double timeSeconds = 0.0;   // time to travel from the previous level to `level`
    double curve       = 0.0;   // 0 = linear; >0 = fast-approach (exponential-ish)
};

/**
    General multi-stage envelope generator (Casio CZ-style): a list of stages,
    one optional sustain point, and an optional loop region. It progresses stage
    to stage; if a stage is the sustain point it holds there until note-off, then
    plays the remaining ("release") stages. A loop region repeats while the note
    is held.

    ADSR is expressed as a 4-stage preset (setADSR) over this same engine, so
    there is a single code path.

    Pure C++, no JUCE. The timing harness targets it directly.
*/
class MultiStageEnvelope
{
public:
    void setSampleRate (double sampleRateHz) noexcept;

    // General configuration. sustainIndex = stage to hold on after completing
    // (-1 = no sustain / one-shot).
    void setStages (const std::vector<EnvStage>& stages, int sustainIndex);
    // Allocation-free overload for the audio thread (copies into the preallocated
    // stage buffer; reuses capacity when count <= the reserved size).
    void setStages (const EnvStage* stages, int count, int sustainIndex) noexcept;
    void setLoop   (bool enabled, int startIndex, int endIndex) noexcept;

    // ADSR preset: attack->1, decay->sustain, hold, release->0.
    void setADSR (double attack, double decay, double sustain, double release,
                  double curve = 0.0);

    void noteOn () noexcept;
    void noteOff() noexcept;
    void reset  () noexcept;

    float  processSample () noexcept;
    bool   isActive () const noexcept { return active_; }
    double level    () const noexcept { return output_; }

private:
    void startStage   (int index) noexcept;
    void advanceStage () noexcept;

    double sampleRate_ = 44100.0;

    std::vector<EnvStage> stages_;
    int  sustainIndex_ = -1;
    bool loopEnabled_  = false;
    int  loopStart_    = 0;
    int  loopEnd_      = 0;

    int    curStage_        = 0;
    double phase_           = 0.0;
    double phaseInc_        = 0.0;
    double stageStartLevel_ = 0.0;
    double output_          = 0.0;

    bool active_     = false;
    bool noteHeld_   = false;
    bool releasing_  = false;
    bool sustaining_ = false;
};

} // namespace pdhybrid
