#include "ModMatrix.h"

namespace pdhybrid {

void ModMatrix::clear() noexcept
{
    for (auto& r : routes_)
        r = ModRoute { };
}

void ModMatrix::setRoute (int slot, ModSource source, ModDest dest, double depth) noexcept
{
    if (slot < 0 || slot >= kNumSlots)
        return;
    routes_[slot] = ModRoute { source, dest, depth };
}

void ModMatrix::evaluate (const ModSources& sources, double* out) const noexcept
{
    for (int d = 0; d < kNumDests; ++d)
        out[d] = 0.0;

    for (const auto& r : routes_)
    {
        if (r.source == ModSource::None || r.dest == ModDest::None)
            continue;
        out[static_cast<int> (r.dest)] += r.depth * sources[r.source];
    }
}

} // namespace pdhybrid
