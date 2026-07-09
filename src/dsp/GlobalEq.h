#pragma once

#include "Biquad.h"

namespace pdhybrid {

/**
    Stereo 4-band master EQ: a low shelf, two peaking mid bands and a high
    shelf. Every band has an adjustable corner/centre frequency and a gain in
    dB; at 0 dB each band is exactly unity, so the EQ is transparent by default.
    Built from the shared RBJ-cookbook Biquad; pure C++, no JUCE.

    `processSample` runs the left-channel chain (used by the offline frequency
    harness); `processStereo` runs both channels for the plugin's master bus.
*/
class GlobalEq
{
public:
    static constexpr int kNumBands = 4;
    enum Band { LowShelf = 0, Mid1, Mid2, HighShelf };

    void setSampleRate (double sampleRateHz) noexcept;
    void reset         () noexcept;

    // band in [0, kNumBands): frequency in Hz, gain in dB (0 = flat).
    void setBand (int band, double freqHz, double gainDb) noexcept;

    float processSample (float x) noexcept;                              // left chain
    void  processStereo (float* left, float* right, int numSamples) noexcept;

private:
    void design (int band) noexcept;
    static Biquad::Kind kindFor (int band) noexcept;
    static double        qFor    (int band) noexcept;

    double sampleRate_ = 44100.0;
    double freq_[kNumBands] { 120.0, 500.0, 2000.0, 8000.0 };
    double gain_[kNumBands] { 0.0, 0.0, 0.0, 0.0 };

    Biquad l_[kNumBands], r_[kNumBands];
};

} // namespace pdhybrid
