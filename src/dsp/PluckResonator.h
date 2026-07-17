#pragma once

#include "CombFilter.h"
#include "AllpassDispersion.h"

namespace pdhybrid {

/**
    A per-voice Karplus-Strong pluck: the oscillator mix is injected as a short
    exciter burst into a tuned feedback comb (the "string"), which then rings and
    decays on its own -- turning any oscillator (notably the CZ phase-distortion
    waves) into the excitation of a plucked string / mallet / tine. An optional
    allpass dispersion stage stretches the partials for a glassy, inharmonic,
    bell-like colour.

    Reuses the existing CombFilter (waveguide) and AllpassDispersion. The exciter
    is scaled by (1 - feedback) so the comb's resonant gain (1 / (1 - feedback))
    is cancelled and the output stays bounded for any decay / burst setting.

    Pure C++, no JUCE, deterministic: the offline harness drives it directly.
*/
class PluckResonator
{
public:
    void  setSampleRate (double sampleRateHz);
    void  setFrequency  (double frequencyHz) noexcept;   // string tuning
    void  setDecay      (double decay01) noexcept;       // ring time (comb feedback)
    void  setDamping    (double damping01) noexcept;     // tone (feedback lowpass)
    void  setDispersion (double dispersion01) noexcept;  // inharmonic stretch
    void  setBurstMs    (double milliseconds) noexcept;  // exciter injection window
    void  reset         () noexcept;
    void  trigger       () noexcept;                     // note-on: (re)start the burst

    float processSample (float exciter) noexcept;

private:
    void updateBurstSamples() noexcept;

    CombFilter        comb_;
    AllpassDispersion allpass_;

    double sampleRate_   = 44100.0;
    double feedback_     = 0.95;   // mirror of the comb feedback (for exciter scaling)
    double dispersion_   = 0.0;
    double burstMs_      = 20.0;
    int    burstSamples_ = 960;
    int    burstLeft_    = 0;
    int    fadeSamples_  = 96;
};

} // namespace pdhybrid
