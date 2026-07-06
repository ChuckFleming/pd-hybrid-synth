#include "Waveshaper.h"
#include <cmath>

namespace pdhybrid {

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

        case ShaperCurve::Tanh:
        default:
            y = std::tanh (x);
            break;
    }

    return static_cast<float> (y);
}

} // namespace pdhybrid
