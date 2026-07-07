#pragma once

namespace pdhybrid {

/**
    Stereo feed-forward compressor used as a global output stage.

    A peak detector reads the louder of the two channels (stereo-linked, so the
    image never wanders), a soft-knee static curve turns the level into a target
    gain reduction, and that gain is smoothed with separate attack/release times
    before being applied equally to both channels. Makeup gain follows.

    This exists to tame the large per-note level swings that high filter
    resonance produces (notes whose harmonics sit on the cutoff get boosted).
    A ratio of 1:1 is transparent, so the effect is bypassed by default.

    Pure C++, no JUCE; the offline harness measures it directly.
*/
class Compressor
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset         () noexcept;

    void setThreshold (double thresholdDb) noexcept { thresholdDb_ = thresholdDb; }
    void setRatio     (double ratio) noexcept;              // >= 1 (1 = bypass)
    void setKnee      (double kneeDb) noexcept { kneeDb_ = kneeDb < 0.0 ? 0.0 : kneeDb; }
    void setAttack    (double seconds) noexcept;
    void setRelease   (double seconds) noexcept;
    void setMakeup    (double makeupDb) noexcept { makeupDb_ = makeupDb; }

    // Processes `numSamples` of stereo audio in place.
    void processStereo (float* left, float* right, int numSamples) noexcept;

    // Current smoothed gain reduction in dB (<= 0), for metering/tests.
    double gainReductionDb() const noexcept { return gainDb_; }

private:
    double staticCurveDb (double levelDb) const noexcept;

    double sampleRate_  = 44100.0;
    double thresholdDb_ = 0.0;
    double ratio_       = 1.0;
    double kneeDb_      = 6.0;
    double makeupDb_    = 0.0;
    double attackCoef_  = 0.0;
    double releaseCoef_ = 0.0;

    double gainDb_ = 0.0;   // smoothed gain reduction (dB, <= 0)
};

} // namespace pdhybrid
