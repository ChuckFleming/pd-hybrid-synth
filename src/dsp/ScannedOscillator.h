#pragma once

#include "Oversampler.h"
#include <cstdint>

namespace pdhybrid {

/**
    Scanned-synthesis oscillator (Verplank, Shaw & Mathews, 2000).

    A ring of N masses is connected to its neighbours by springs and to rest by a
    light centering spring, with damping. On note-on the ring is "plucked" with a
    localised bump, which excites many spatial modes; because each mode oscillates
    at its own (slow) frequency the ring's *shape* slowly morphs over the note --
    a living wavetable. The audio output is produced by scanning around the ring
    at the note frequency, so pitch is set by the scan while the timbre evolves
    organically underneath it.

    The dynamics are integrated with a symplectic (semi-implicit) Euler step at a
    fixed ~haptic rate (a few hundred updates/sec, independent of the audio rate),
    which keeps the morph musical and the scheme unconditionally stable for the
    stiffness range used here.

    `stiffness` (from the DCW "amount" knob) sets the neighbour coupling -> how
    bright and how fast the shape evolves; `damping` (from the pulse-width knob)
    sets how quickly the pluck settles. Both are therefore mod-matrix
    destinations and the DCW envelope can sweep the stiffness.

    Output is anti-aliased with the shared FIR oversampler. Pure C++, no JUCE,
    fully deterministic: the offline harness drives it.
*/
class ScannedOscillator
{
public:
    static constexpr int kNumMasses = 128;

    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setStiffness   (double stiffness01) noexcept;   // neighbour coupling
    void  setDamping     (double damping01) noexcept;     // energy bleed
    void  setMorphRate   (double rate01) noexcept;        // physics/haptic update rate
    void  setExciteShape (int shape) noexcept { exciteShape_ = shape; }  // 0=pluck 1=impulse 2=noise 3=triangle
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;
    void  reset          () noexcept;                     // zero the ring + scan
    void  excite         () noexcept;                     // pluck (call on note-on)

    bool  wrapped   () const noexcept { return wrapped_; }   // scan wrapped last sample
    void  syncReset () noexcept { phase_ = 0.0; }            // hard-sync slave reset

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    void   updateIncrement() noexcept;
    void   updatePhysics  () noexcept;   // one symplectic Euler step over the ring
    double coreSample     () noexcept;   // one scan read at the oversampled rate

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;
    double phase_      = 0.0;
    double phaseMod_   = 0.0;

    double y_[kNumMasses] = { 0.0 };   // mass positions (the wavetable)
    double v_[kNumMasses] = { 0.0 };   // mass velocities

    double stiffness_ = 0.15;   // neighbour spring constant (scaled from the knob)
    double damping_   = 0.01;   // velocity damping
    double updateHz_  = 1500.0; // physics updates per second (morph speed)
    int    exciteShape_ = 0;    // 0=pluck 1=impulse 2=noise 3=triangle

    std::uint32_t rng_ = 0x853c49e6u;   // noise excitation

    int    updatePeriod_  = 32;   // audio samples between physics steps (~haptic rate)
    int    updateCounter_ = 0;
    bool   wrapped_       = false;

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
