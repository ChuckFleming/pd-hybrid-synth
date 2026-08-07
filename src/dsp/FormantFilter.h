#pragma once

namespace pdhybrid {

/**
    Vowel / formant filter: three parallel resonant bandpasses placed at the
    first three formants of a sung vowel, mixed at their natural relative
    levels. Morphing sweeps continuously through A -> E -> I -> O -> U, so a
    slow LFO or envelope on the morph gives a talking filter.

    The cutoff acts as a vocal-tract length scaler (every formant shifts with
    it, which keeps the vowel identity while moving the voice from a big chest
    to a small head), and the resonance sharpens the peaks and lifts the wet
    mix, so 0 leaves the signal dry and 1 is a fully vowelled voice.
*/
class FormantFilter
{
public:
    static constexpr int kNumFormants = 3;
    static constexpr int kNumVowels   = 5;   // A E I O U

    void setSampleRate (double sampleRateHz) noexcept;
    void setFrequency  (double frequencyHz) noexcept;   // tract scaler (1000 Hz = neutral)
    void setResonance  (double resonance01) noexcept;   // peak Q + wet mix
    void setVowel      (double vowel01) noexcept;       // 0 = A .. 1 = U
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
    double scale_      = 1.0;    // tract scaler derived from the cutoff
    double resonance_  = 0.5;
    double vowel_      = 0.0;
    double wet_        = 0.5;

    Band bands_[kNumFormants];
};

} // namespace pdhybrid
