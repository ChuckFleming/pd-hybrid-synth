#pragma once

namespace pdhybrid {

/**
    Vocoder-style band splitter: the signal is divided into a bank of
    logarithmically spaced bandpasses whose individual levels are then re-mixed
    under one control, giving the comb-of-formants colour a vocoder's filter
    bank produces on its own -- without needing a modulator input.

    The morph knob tilts the band gains: at 0 only the low bands survive
    (a dark, chesty vowel), at 0.5 the bank is flat and near-transparent, at 1
    only the high bands do (thin and papery). Sweeping it drags a moving
    emphasis across the spectrum that sounds like a vocoder being talked
    through, which is the point.

    The cutoff shifts the whole bank up or down (the analogue of a vocoder's
    band range), and the resonance sharpens each band, deepening the notches
    between them from a gentle colour to a hollow, metallic ring.
*/
class BandSplitFilter
{
public:
    static constexpr int kNumBands = 8;

    void setSampleRate (double sampleRateHz) noexcept;
    void setFrequency  (double frequencyHz) noexcept;   // centre of the bank
    void setResonance  (double resonance01) noexcept;   // per-band Q + wet mix
    void setTilt       (double tilt01) noexcept;        // low -> high emphasis
    void reset         () noexcept;

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    void updateCoefficients() noexcept;

    struct Band
    {
        double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
        double ic1 = 0.0, ic2 = 0.0;
        double gain = 1.0;
    };

    double sampleRate_ = 44100.0;
    double centreHz_   = 1000.0;
    double resonance_  = 0.5;
    double tilt_       = 0.5;
    double wet_        = 0.5;

    Band bands_[kNumBands];
};

} // namespace pdhybrid
