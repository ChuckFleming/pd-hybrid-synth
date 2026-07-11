#include "Reverb.h"

namespace pdhybrid {

// Classic Freeverb tunings (samples @ 44.1 kHz); scaled to the real rate.
static const int kCombTuning[Reverb::kNumCombs]      = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int kAllpassTuning[Reverb::kNumAllpass] = { 556, 441, 341, 225 };
static constexpr int    kStereoSpread = 23;
static constexpr float  kFixedGain    = 0.015f;
static constexpr double kRoomScale = 0.28, kRoomOffset = 0.7;
static constexpr double kDampScale = 0.4;

void Reverb::setSampleRate (double sampleRateHz)
{
    const double sr    = sampleRateHz > 0.0 ? sampleRateHz : 44100.0;
    const double scale = sr / 44100.0;
    auto len = [scale] (int base, int extra) { return static_cast<int> ((base + extra) * scale) + 1; };

    for (int i = 0; i < kNumCombs; ++i)
    {
        combL_[i].setBuffer (len (kCombTuning[i], 0));
        combR_[i].setBuffer (len (kCombTuning[i], kStereoSpread));
    }
    for (int i = 0; i < kNumAllpass; ++i)
    {
        apL_[i].setBuffer (len (kAllpassTuning[i], 0));
        apR_[i].setBuffer (len (kAllpassTuning[i], kStereoSpread));
    }

    setSize (0.5);
    setDamp (0.5);
}

void Reverb::reset() noexcept
{
    for (int i = 0; i < kNumCombs; ++i)   { combL_[i].mute(); combR_[i].mute(); }
    for (int i = 0; i < kNumAllpass; ++i) { apL_[i].mute();   apR_[i].mute();   }
}

void Reverb::setSize (double size01) noexcept
{
    roomSize_ = clamp01 (size01) * kRoomScale + kRoomOffset;
    for (int i = 0; i < kNumCombs; ++i)
    {
        combL_[i].setFeedback (static_cast<float> (roomSize_));
        combR_[i].setFeedback (static_cast<float> (roomSize_));
    }
}

void Reverb::setDamp (double damp01) noexcept
{
    damp_ = clamp01 (damp01) * kDampScale;
    for (int i = 0; i < kNumCombs; ++i)
    {
        combL_[i].setDamp (static_cast<float> (damp_));
        combR_[i].setDamp (static_cast<float> (damp_));
    }
}

void Reverb::processStereo (float* left, float* right, int numSamples) noexcept
{
    const float dryGain = static_cast<float> (1.0 - mix_);
    const float wetGain = static_cast<float> (mix_);
    const float w1 = static_cast<float> (width_ * 0.5 + 0.5);
    const float w2 = static_cast<float> ((1.0 - width_) * 0.5);

    for (int n = 0; n < numSamples; ++n)
    {
        const float inL = left[n];
        const float inR = right[n];
        const float input = (inL + inR) * kFixedGain;

        float outL = 0.0f, outR = 0.0f;
        for (int i = 0; i < kNumCombs; ++i)     // parallel combs
        {
            outL += combL_[i].process (input);
            outR += combR_[i].process (input);
        }
        for (int i = 0; i < kNumAllpass; ++i)   // series allpasses
        {
            outL = apL_[i].process (outL);
            outR = apR_[i].process (outR);
        }

        const float wetL = outL * w1 + outR * w2;
        const float wetR = outR * w1 + outL * w2;
        left[n]  = inL * dryGain + wetL * wetGain;
        right[n] = inR * dryGain + wetR * wetGain;
    }
}

} // namespace pdhybrid
