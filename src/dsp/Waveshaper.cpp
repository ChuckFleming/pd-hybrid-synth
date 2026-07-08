#include "Waveshaper.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kHalfPi = 1.5707963267948966;

float Waveshaper::process (float in) const noexcept
{
    double x = static_cast<double> (in) * drive_ + bias_;
    double y;

    switch (curve_)
    {
        case ShaperCurve::Cubic:
            // Unit-slope near zero, saturates to +/- 2/3 outside [-1, 1].
            if      (x >=  1.0) y =  2.0 / 3.0;
            else if (x <= -1.0) y = -2.0 / 3.0;
            else                y = x - (x * x * x) / 3.0;
            break;

        case ShaperCurve::HardClip:
            y = std::clamp (x, -1.0, 1.0);
            break;

        case ShaperCurve::Tube:
            // Asymmetric soft clip -> even + odd harmonics (tube-like).
            y = (x >= 0.0) ? std::tanh (x) : 0.7 * std::tanh (x);
            break;

        case ShaperCurve::Diode:
            // Asymmetric diode pair: positive conducts hard, negative softly.
            y = (x >= 0.0) ? (1.0 - std::exp (-x)) : 0.25 * (std::exp (x) - 1.0);
            break;

        case ShaperCurve::Fuzz:
            // High-gain, near-square.
            y = std::clamp (std::tanh (x * 2.0) * 1.5, -1.0, 1.0);
            break;

        case ShaperCurve::Rectify:
            // Full-wave rectification -> strong octave-up (DC removed downstream).
            y = std::abs (std::tanh (x));
            break;

        case ShaperCurve::Wavefold:
            // Sine folding: as drive pushes |x| up, the sine folds back.
            y = std::sin (x * kHalfPi);
            break;

        case ShaperCurve::Foldback:
        {
            double f = x;
            int guard = 0;
            while ((f > 1.0 || f < -1.0) && guard++ < 64)
                f = (f > 1.0) ? (2.0 - f) : (-2.0 - f);
            y = f;
            break;
        }

        case ShaperCurve::Tanh:
        default:
            y = std::tanh (x);
            break;
    }

    return static_cast<float> (y);
}

} // namespace pdhybrid
