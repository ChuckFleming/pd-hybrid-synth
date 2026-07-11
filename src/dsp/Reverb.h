#pragma once

#include <vector>

namespace pdhybrid {

// Damped feedback comb filter (one lowpass in the feedback path).
class RevComb
{
public:
    void  setBuffer (int n) { buf_.assign (static_cast<std::size_t> (n), 0.0f); size_ = n; idx_ = 0; store_ = 0.0f; }
    void  setDamp   (float d) noexcept { damp1_ = d; damp2_ = 1.0f - d; }
    void  setFeedback (float f) noexcept { feedback_ = f; }
    void  mute() noexcept { for (auto& s : buf_) s = 0.0f; store_ = 0.0f; }

    inline float process (float input) noexcept
    {
        float out = buf_[static_cast<std::size_t> (idx_)];
        store_ = out * damp2_ + store_ * damp1_;               // lowpass in the loop
        buf_[static_cast<std::size_t> (idx_)] = input + store_ * feedback_;
        if (++idx_ >= size_) idx_ = 0;
        return out;
    }

private:
    std::vector<float> buf_;
    int   size_ = 0, idx_ = 0;
    float feedback_ = 0.0f, damp1_ = 0.0f, damp2_ = 1.0f, store_ = 0.0f;
};

// Schroeder allpass.
class RevAllpass
{
public:
    void setBuffer (int n) { buf_.assign (static_cast<std::size_t> (n), 0.0f); size_ = n; idx_ = 0; }
    void mute() noexcept { for (auto& s : buf_) s = 0.0f; }

    inline float process (float input) noexcept
    {
        const float bufout = buf_[static_cast<std::size_t> (idx_)];
        const float out    = -input + bufout;
        buf_[static_cast<std::size_t> (idx_)] = input + bufout * feedback_;
        if (++idx_ >= size_) idx_ = 0;
        return out;
    }

private:
    std::vector<float> buf_;
    int   size_ = 0, idx_ = 0;
    float feedback_ = 0.5f;
};

/**
    Freeverb-style stereo reverb: 8 damped combs in parallel into 4 series
    allpasses per channel, with a stereo-spread offset on the right channel.
    size = decay/room, damp = HF absorption, width = stereo spread, mix = wet.
    Pure C++, no JUCE.
*/
class Reverb
{
public:
    void setSampleRate (double sampleRateHz);
    void reset() noexcept;

    void setSize  (double size01) noexcept;
    void setDamp  (double damp01) noexcept;
    void setWidth (double width01) noexcept { width_ = clamp01 (width01); }
    void setMix   (double mix01) noexcept   { mix_   = clamp01 (mix01); }

    void processStereo (float* left, float* right, int numSamples) noexcept;

    static constexpr int kNumCombs   = 8;
    static constexpr int kNumAllpass = 4;

private:
    static double clamp01 (double x) noexcept { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

    RevComb    combL_[kNumCombs],   combR_[kNumCombs];
    RevAllpass apL_[kNumAllpass],   apR_[kNumAllpass];

    double roomSize_ = 0.84;   // comb feedback
    double damp_     = 0.2;    // comb lowpass amount
    double width_    = 1.0;
    double mix_      = 0.3;
};

} // namespace pdhybrid
