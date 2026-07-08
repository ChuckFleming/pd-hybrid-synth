#pragma once

#include <cmath>

namespace pdhybrid {

/**
    A single RBJ-cookbook biquad (Direct Form I) plus coefficient designers for
    the band types used by the oscillator EQ and the global EQ. Header-only and
    JUCE-free so both can share one implementation.
*/
struct Biquad
{
    enum class Kind { LowShelf, Peaking, HighShelf };

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }

    double process (double x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    // Recomputes the coefficients (keeps the running state).
    void design (Kind kind, double freqHz, double q, double gainDb, double sr) noexcept
    {
        static constexpr double kTwoPi = 6.283185307179586476925287;

        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = kTwoPi * freqHz / sr;
        const double cosw0 = std::cos (w0);
        const double sinw0 = std::sin (w0);
        const double alpha = sinw0 / (2.0 * q);

        double nb0 = 1.0, nb1 = 0.0, nb2 = 0.0, na0 = 1.0, na1 = 0.0, na2 = 0.0;

        switch (kind)
        {
            case Kind::Peaking:
                nb0 = 1.0 + alpha * A;
                nb1 = -2.0 * cosw0;
                nb2 = 1.0 - alpha * A;
                na0 = 1.0 + alpha / A;
                na1 = -2.0 * cosw0;
                na2 = 1.0 - alpha / A;
                break;

            case Kind::LowShelf:
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                nb0 =      A * ((A + 1.0) - (A - 1.0) * cosw0 + s);
                nb1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
                nb2 =      A * ((A + 1.0) - (A - 1.0) * cosw0 - s);
                na0 =          (A + 1.0) + (A - 1.0) * cosw0 + s;
                na1 = -2.0 *   ((A - 1.0) + (A + 1.0) * cosw0);
                na2 =          (A + 1.0) + (A - 1.0) * cosw0 - s;
                break;
            }

            case Kind::HighShelf:
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                nb0 =      A * ((A + 1.0) + (A - 1.0) * cosw0 + s);
                nb1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
                nb2 =      A * ((A + 1.0) + (A - 1.0) * cosw0 - s);
                na0 =          (A + 1.0) - (A - 1.0) * cosw0 + s;
                na1 =  2.0 *   ((A - 1.0) - (A + 1.0) * cosw0);
                na2 =          (A + 1.0) - (A - 1.0) * cosw0 - s;
                break;
            }
        }

        const double inv = 1.0 / na0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv;
        a1 = na1 * inv; a2 = na2 * inv;
    }
};

} // namespace pdhybrid
