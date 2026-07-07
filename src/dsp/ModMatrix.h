#pragma once

namespace pdhybrid {

enum class ModSource : int
{
    None = 0,
    ModEnv,
    Lfo,
    Velocity,
    Pressure,
    Timbre,
    PitchBend,
    KeyTrack,
    ModWheel,
    Lfo2,
    Count
};

enum class ModDest : int
{
    None = 0,
    Pitch,        // semitones
    PdAmount,
    PulseWidth,
    Cutoff,       // octaves
    Resonance,
    Morph,
    Drive,        // octaves of gain
    Amplitude,
    Count
};

struct ModRoute
{
    ModSource source = ModSource::None;
    ModDest   dest   = ModDest::None;
    double    depth  = 0.0;
};

// Current values of every modulation source for one voice.
struct ModSources
{
    double v[static_cast<int> (ModSource::Count)] = { 0.0 };

    double& operator[] (ModSource s) noexcept       { return v[static_cast<int> (s)]; }
    double  operator[] (ModSource s) const noexcept { return v[static_cast<int> (s)]; }
};

/**
    Fixed-slot modulation matrix: each slot routes a source to a destination
    with a signed depth. `evaluate` sums every slot's contribution into a
    per-destination array. Pure data + arithmetic, trivially testable.
*/
class ModMatrix
{
public:
    static constexpr int kNumSlots  = 6;
    static constexpr int kNumDests  = static_cast<int> (ModDest::Count);

    void clear() noexcept;
    void setRoute (int slot, ModSource source, ModDest dest, double depth) noexcept;
    ModRoute route (int slot) const noexcept { return routes_[slot]; }

    // Accumulates depth*source per destination into out[kNumDests].
    void evaluate (const ModSources& sources, double* out) const noexcept;

private:
    ModRoute routes_[kNumSlots] { };
};

} // namespace pdhybrid
