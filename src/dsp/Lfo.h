#pragma once

#include <cstdint>

namespace pdhybrid {

enum class LfoWave
{
    Sine = 0,
    Triangle,
    Square,
    Saw,            // ramp up
    RampDown,
    SampleHold,     // stepped random, held for a cycle
    SmoothRandom,   // random target, interpolated across the cycle
    Exponential     // exponential decay ramp
};

/**
    Low-frequency oscillator used as a modulation source. Bipolar output in
    [-1, 1]. `value()` reads the current sample without advancing; `advance`
    moves the phase forward -- so a voice can sample it once per control block
    and still keep it in sync sample-accurately.
*/
class Lfo
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void setFrequency  (double frequencyHz) noexcept;
    void setWaveform   (LfoWave wave) noexcept { wave_ = wave; }
    void reset         () noexcept;

    double value        () const noexcept;   // current output, no advance
    double processSample () noexcept;         // return current, then advance one sample
    void   advance      (int numSamples) noexcept;

private:
    double compute   (double phase) const noexcept;
    void   onCycleWrap() noexcept;            // resample the random generators
    double nextRandom() noexcept;             // bipolar white in [-1, 1)

    double  sampleRate_ = 44100.0;
    double  frequency_  = 5.0;
    double  inc_        = 5.0 / 44100.0;
    double  phase_      = 0.0;
    LfoWave wave_       = LfoWave::Sine;

    // Random generators for the Sample & Hold / Smooth Random waves.
    std::uint32_t rng_      = 0x1234567u;
    double        randCurr_ = 0.0;
    double        randNext_ = 0.0;
};

} // namespace pdhybrid
