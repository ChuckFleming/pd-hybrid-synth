#pragma once

namespace pdhybrid {

// Per-block synth settings pushed from the host/UI down to every voice.
struct SynthParams
{
    double pdAmount  = 0.30;
    double cutoffHz  = 8000.0;
    double resonance = 0.20;
    double drive     = 1.0;
    double bias      = 0.0;

    double attack  = 0.01;
    double decay   = 0.10;
    double sustain = 0.70;
    double release = 0.20;

    double gain    = 0.80;
};

} // namespace pdhybrid
