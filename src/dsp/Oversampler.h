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
    // Circular-buffer FIR helpers: `firPush` writes one sample and advances the
    // index; `firDot` computes the dot product with the most-recent sample as
    // taps[0]. Splitting them lets decimation push `factor` samples but compute
    // only the one retained output.
    static void   firPush (std::vector<double>& state, int& pos, double in) noexcept;
    static double firDot  (const std::vector<double>& state, int pos,
                           const std::vector<double>& taps) noexcept;

    int                 factor_ = 1;
    int                 tapsPerPhase_ = 0;
    std::vector<double> proto_;
    std::vector<double> upBase_;      // base-rate history for polyphase interpolation
    std::vector<double> downState_;   // high-rate history for decimation
    int                 upBasePos_ = 0;
    int                 downPos_   = 0;
};

} // namespace pdhybrid
