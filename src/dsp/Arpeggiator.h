#pragma once

#include <array>
#include <cstdint>

namespace pdhybrid {

/**
    Note arpeggiator. The host feeds it held notes; each block it generates
    note-on/off events at sample-accurate step boundaries derived from the step
    length (in samples). Modes: Up, Down, UpDown, Random, As Played. Octave range
    1-4, adjustable gate length and latch.

    Pure C++, no JUCE. Fixed-capacity storage -> no audio-thread allocation.
*/
class Arpeggiator
{
public:
    enum Mode { Up = 0, Down, UpDown, Random, AsPlayed };

    struct Event { int pos; bool noteOn; int note; float velocity; };

    void reset() noexcept;

    void setStepSamples (double samples) noexcept { stepSamples_ = samples > 1.0 ? samples : 1.0; }
    void setMode        (int mode) noexcept       { mode_ = mode; }
    void setOctaves     (int octaves) noexcept    { octaves_ = octaves < 1 ? 1 : (octaves > 4 ? 4 : octaves); }
    void setGate        (double gate01) noexcept  { gate_ = gate01 < 0.05 ? 0.05 : (gate01 > 1.0 ? 1.0 : gate01); }
    void setLatch       (bool latch) noexcept;

    void noteOn  (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void clear   () noexcept;                       // drop all held notes

    int  heldCount() const noexcept { return poolCount_; }

    // Emit up to maxOut events for a block of numSamples, in time order.
    int generate (int numSamples, Event* out, int maxOut) noexcept;

private:
    struct Held { int note; float velocity; };

    void buildSequence() noexcept;                  // expand pool -> ordered note list

    std::array<Held, 32> pool_ { };
    int   poolCount_ = 0;
    int   physicalHeld_ = 0;                        // keys physically down (for latch)

    std::array<int, 128> seq_ { };                  // expanded note sequence
    std::array<float, 128> seqVel_ { };
    int   seqLen_ = 0;
    int   seqIndex_ = 0;
    bool  upDownRising_ = true;

    double stepSamples_ = 22050.0;
    double stepPhase_   = 0.0;      // samples until the next step
    int    mode_    = Up;
    int    octaves_ = 1;
    double gate_    = 0.5;
    bool   latch_   = false;

    bool   noteSounding_ = false;
    int    curNote_ = -1;
    double offPhase_ = 0.0;         // samples until the current note's gate-off
    std::uint32_t rng_ = 0x1234567u;
};

} // namespace pdhybrid
