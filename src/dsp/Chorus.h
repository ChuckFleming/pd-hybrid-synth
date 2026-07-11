#pragma once

#include <vector>

namespace pdhybrid {

/**
    Stereo chorus / ensemble. Two LFO-modulated fractional delay voices with the
    left/right taps in quadrature for width. Mode I = one slow voice, II = a
    faster/deeper voice, I+II = both (the classic CZ/Juno-style ensemble).

    Fractional (linearly interpolated) delay lines on a power-of-two buffer;
    pure C++, no JUCE.
*/
class Chorus
{
public:
    void setSampleRate (double sampleRateHz);
    void reset         () noexcept;

    void setRate  (double hz) noexcept    { rateHz_ = hz; }
    void setDepth (double depth01) noexcept { depth_ = clamp01 (depth01); }
    void setMix   (double mix01) noexcept   { mix_   = clamp01 (mix01); }
    void setMode  (int mode) noexcept       { mode_  = mode; }   // 0=I, 1=II, 2=I+II

    void processStereo (float* left, float* right, int numSamples) noexcept;

private:
    static double clamp01 (double x) noexcept { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
    float readFrac (const std::vector<float>& buf, double delaySamples) const noexcept;

    double sampleRate_ = 44100.0;
    std::vector<float> bufL_, bufR_;
    int    size_  = 1;    // power of two
    int    mask_  = 0;
    int    write_ = 0;

    double phaseA_ = 0.0;
    double phaseB_ = 1.5707963267948966;   // offset so I+II voices don't align
    double rateHz_ = 0.5;
    double depth_  = 0.5;
    double mix_    = 0.5;
    int    mode_   = 2;

    static constexpr double kBaseMs  = 11.0;   // centre delay
    static constexpr double kDepthMs = 5.0;    // max modulation swing
    static constexpr double kTwoPi   = 6.283185307179586;
    static constexpr double kHalfPi  = 1.5707963267948966;
};

} // namespace pdhybrid
