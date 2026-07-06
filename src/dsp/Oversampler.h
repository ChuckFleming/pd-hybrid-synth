#pragma once

#include <vector>

namespace pdhybrid {

/**
    Integer FIR oversampler (factor 1/2/4/8) built around a single windowed-sinc
    prototype (cutoff = base Nyquist), used for both interpolation and
    decimation. Streaming, sample-by-sample: `upsample` turns one base-rate
    sample into `factor` high-rate samples; process the nonlinearity on those,
    then `downsample` collapses them back to one base-rate sample.

    Reusable by any nonlinear stage that needs anti-aliasing.
*/
class Oversampler
{
public:
    void prepare (int factor);          // 1, 2, 4, or 8 (others clamp to 1)
    void reset   () noexcept;
    int  factor  () const noexcept { return factor_; }

    void  upsample   (float x, float* highOut) noexcept;   // writes `factor` samples
    float downsample (const float* highIn) noexcept;       // reads  `factor` samples

private:
    void buildPrototype (int tapsPerPhase);
    static double firStep (std::vector<double>& state,
                           const std::vector<double>& taps, double in) noexcept;

    int                 factor_ = 1;
    std::vector<double> proto_;
    std::vector<double> upState_;
    std::vector<double> downState_;
};

} // namespace pdhybrid
