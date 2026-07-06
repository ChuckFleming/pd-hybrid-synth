#pragma once

#include <vector>

namespace pdhybrid {

/**
    Tuned feedback comb / waveguide filter (Karplus-Strong lineage). A fractional
    delay line with feedback resonates at integer multiples of frequency =
    sampleRate / delay, giving a harmonic comb. A one-pole lowpass in the
    feedback path ("damping") rolls off the higher resonances for a plucked,
    string-like character.
*/
class CombFilter
{
public:
    void setSampleRate (double sampleRateHz);
    void setFrequency  (double frequencyHz) noexcept;   // resonance spacing
    void setFeedback   (double feedback01) noexcept;    // 0..~0.99 (ring time)
    void setDamping    (double damping01) noexcept;     // feedback lowpass
    void reset         () noexcept;

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    float readInterpolated (double delaySamples) const noexcept;

    double sampleRate_ = 44100.0;
    double delay_      = 100.0;
    double feedback_   = 0.9;
    double damping_    = 0.2;
    double lpState_    = 0.0;

    std::vector<float> buffer_;
    int                writePos_ = 0;
};

} // namespace pdhybrid
