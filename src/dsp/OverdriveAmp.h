#pragma once

#include "Oversampler.h"
#include "Waveshaper.h"

namespace pdhybrid {

/**
    Musical overdrive: oversampled waveshaping (anti-aliased) followed by a DC
    blocker (removes the offset that bias/asymmetry introduces). Composes the
    reusable Oversampler and Waveshaper.
*/
class OverdriveAmp
{
public:
    void setSampleRate  (double sampleRateHz) noexcept { sampleRate_ = sampleRateHz; }
    void setOversampling (int factor) noexcept         { os_.prepare (factor); }
    void setDrive       (double drive) noexcept        { shaper_.setDrive (drive); }
    void setBias        (double bias) noexcept         { shaper_.setBias (bias); }
    void setCurve       (ShaperCurve c) noexcept       { shaper_.setCurve (c); }
    void setDcBlock     (bool on) noexcept             { dcBlock_ = on; }

    void reset() noexcept
    {
        os_.reset();
        dcX1_ = dcY1_ = 0.0;
    }

    float processSample (float x) noexcept;
    void  processBlock  (float* buffer, int numSamples) noexcept;

private:
    float applyDcBlock (float in) noexcept;

    Oversampler os_;
    Waveshaper  shaper_;
    double      sampleRate_ = 44100.0;

    bool   dcBlock_ = true;
    double dcX1_ = 0.0, dcY1_ = 0.0;
    static constexpr double kDcR = 0.9995;
};

} // namespace pdhybrid
