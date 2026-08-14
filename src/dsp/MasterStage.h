#pragma once

#include "Oversampler.h"

namespace pdhybrid {

/**
    Final master output stage: a smoothed output gain, then a peak limiter, then
    a soft-clip safety net.

    The limiter is a feed-forward peak limiter with an envelope: it measures the
    stereo-linked peak, works out the gain that would bring it to the threshold,
    and smooths that with a fast attack and a musical release. This is the part
    that does the level control. Before it existed the stage was only the tanh
    knee below -- a static waveshaper with no time constants, which distorts
    every sample above the threshold the instant it arrives. That is audible as
    clipping, and it is what a chord or a wide unison patch ran into, because
    voices sum and the engine can arrive several times over the ceiling.

    The tanh knee is kept as the last line of defence: a feed-forward limiter
    has no lookahead, so a sharp transient can still poke through for the
    length of the attack. The knee is applied 4x oversampled so that when it
    does engage, the harmonics it adds do not fold back down as aliased crackle.

    Gain is ramped (one-pole) to avoid zipper noise when the level control moves.

    Pure C++, no JUCE; measured directly by the offline harness.
*/
class MasterStage
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset () noexcept;

    void setGainDb          (double db) noexcept;                 // target output gain
    void setLimiterEnabled  (bool on) noexcept { limiterOn_ = on; }
    void setThreshold       (double linear) noexcept;             // knee start (0..1)

    float processSample (float x) noexcept;                       // mono (tests)
    void  processStereo (float* left, float* right, int numSamples) noexcept;

    /** Group delay this stage adds, in samples, whatever the limiter is doing.
        The 4x knee oversampler delays by this much and used to be bypassed
        entirely with the limiter off -- so the stage's latency changed with a
        control, and no single number reported to the host could be right. With
        the limiter off the signal now goes through a matching plain delay
        instead, which costs almost nothing and makes this constant true.

        Independent of sample rate: the FIR is a fixed number of taps. */
    static constexpr int kLatencySamples = 15;

    /** Current limiter gain reduction in dB (<= 0), for metering. */
    double gainReductionDb() const noexcept;

private:
    double softClip (double x) const noexcept;
    // Updates the limiter envelope for one sample's stereo-linked peak and
    // returns the gain to apply.
    double limiterGain (double peak) noexcept;

    double sampleRate_ = 44100.0;
    double targetGain_ = 1.0;
    double curGain_    = 1.0;
    double gainCoef_   = 1.0;      // one-pole smoothing coefficient
    bool   limiterOn_  = true;
    double threshold_  = 0.9;

    double limGain_    = 1.0;      // smoothed limiter gain (<= 1)
    double limAtkCoef_ = 0.0;
    double limRelCoef_ = 0.0;
    double peakEnv_      = 0.0;   // held peak driving the limiter
    double peakDecayCoef_ = 0.0;

    Oversampler osL_, osR_;        // 4x anti-alias the soft-clip knee
    static constexpr int kOsFactor = 4;

    // Matching delay for the limiter-off path, so this stage's latency does not
    // depend on whether the limiter is engaged. Fixed capacity: no allocation.
    float bypassL_[kLatencySamples + 1] = { 0.0f };
    float bypassR_[kLatencySamples + 1] = { 0.0f };
    int   bypassPos_ = 0;
};

} // namespace pdhybrid
