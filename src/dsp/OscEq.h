#pragma once

namespace pdhybrid {

/**
    A compact 3-band tone EQ used per oscillator: a low shelf, a mid peaking
    band and a high shelf at fixed frequencies, each controlled by a single gain
    in dB. At 0 dB every band is exactly unity, so the EQ is transparent by
    default. Built from RBJ-cookbook biquads; pure C++, no JUCE.
*/
class OscEq
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset         () noexcept;

    // Band gains in dB (0 = flat).
    void setGains (double lowDb, double midDb, double highDb) noexcept;

    float processSample (float x) noexcept;

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        void  reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
        double process (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    enum class Kind { LowShelf, Peaking, HighShelf };
    static Biquad design (Kind kind, double freqHz, double q, double gainDb, double sr) noexcept;

    double sampleRate_ = 44100.0;
    double lowDb_ = 0.0, midDb_ = 0.0, highDb_ = 0.0;

    Biquad low_, mid_, high_;
};

} // namespace pdhybrid
