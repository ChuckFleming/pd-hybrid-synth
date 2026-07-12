#include "ModMatrix.h"

namespace pdhybrid {

void ModMatrix::clear() noexcept
{
    for (auto& r : routes_)
        r = ModRoute { };
}

void ModMatrix::setRoute (int slot, ModSource source, ModDest dest, double depth,
                          ModCurve curve) noexcept
{
    if (slot < 0 || slot >= kNumSlots)
        return;
    routes_[slot] = ModRoute { source, dest, depth, curve };
}

// Shape a bipolar source value by the slot's response curve (sign-preserving).
static inline double applyCurve (double v, ModCurve curve) noexcept
{
    if (curve == ModCurve::Linear)
        return v;

    const double a = v < 0.0 ? -v : v;
    const double sign = v < 0.0 ? -1.0 : 1.0;
    if (curve == ModCurve::Exponential)
        return sign * a * a;
    return sign * (a * a * (3.0 - 2.0 * a));   // SCurve (smoothstep)
}

void ModMatrix::evaluate (const ModSources& sources, double* out) const noexcept
{
    for (int d = 0; d < kNumDests; ++d)
        out[d] = 0.0;

    for (const auto& r : routes_)
    {
        if (r.source == ModSource::None || r.dest == ModDest::None)
            continue;
        out[static_cast<int> (r.dest)] += r.depth * applyCurve (sources[r.source], r.curve);
    }
}

} // namespace pdhybrid
