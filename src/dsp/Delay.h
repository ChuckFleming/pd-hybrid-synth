#pragma once

#include <vector>

namespace pdhybrid {

enum class DelayMode
{
    Mono = 0,   // both channels share the left time
    Stereo,     // independent left/right times
    PingPong    // echoes bounce across the stereo field
};

/**
    Stereo delay with feedback, wet/dry mix, three routing modes and optional
    ducking. Ducking runs an envelope follower on the dry input and pulls the
    wet level down while the input is loud, so the echoes bloom in the gaps
    instead of muddying the notes.

    Fractional (linearly interpolated) delay lines; pure C++, no JUCE.
*/
class Delay
{
public:
    void setSampleRate (double sampleRateHz);
    void reset         () noexcept;

    void setTimes    (double leftSeconds, double rightSeconds) noexcept;
    void setFeedback (double feedback01) noexcept;   // 0 .. ~0.95
    void setMix      (double mix01) noexcept;         // 0 dry .. 1 wet
    void setMode     (DelayMode mode) noexcept { mode_ = mode; }
    void setDuck     (double amount01) noexcept;      // 0 = off

    void processStereo (float* left, float* right, int numSamples) noexcept;
    // Wet-only (echoes without the dry signal) for parallel FX routing.
    void processWet    (float* left, float* right, int numSamples) noexcept;

    static constexpr double kMaxDelaySeconds = 2.0;

private:
    void  processImpl (float* left, float* right, int numSamples, bool wetOnly) noexcept;
    float readFrac (const std::vector<float>& buf, double delaySamples) const noexcept;

    double sampleRate_ = 44100.0;
    std::vector<float> bufL_, bufR_;
    int    size_    = 1;    // power of two
    int    mask_    = 0;    // size_ - 1, for cheap wrap
    int    write_   = 0;
    double delayL_  = 0.25 * 44100.0;
    double delayR_  = 0.25 * 44100.0;
    double feedback_ = 0.30;
    double mix_      = 0.25;
    DelayMode mode_  = DelayMode::Stereo;

    double duckAmount_ = 0.0;
    double duckEnv_    = 0.0;
    double duckAtk_    = 0.0;
    double duckRel_    = 0.0;
};

} // namespace pdhybrid
