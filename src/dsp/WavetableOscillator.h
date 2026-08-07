#pragma once

#include "Oversampler.h"
#include <memory>
#include <vector>

namespace pdhybrid {

/**
    Wavetable oscillator with morphing between frames and per-octave mip-maps.

    A table is a series of single-cycle frames; the position knob sweeps between
    them, cross-fading so the morph is continuous. Every frame is analysed once
    at load time into a set of band-limited mip-maps (one per octave, each with
    the partials that would alias at that pitch removed), so playing the same
    table at the top of the keyboard cannot alias -- the usual failure of naive
    wavetable playback.

    Loading is deliberately not part of this class: the DSP core stays free of
    JUCE and of file I/O. A caller hands over already-decoded sample data with
    `setTable`, and the plugin layer does the WAV reading. `WavetableSet` is
    shared (std::shared_ptr) so every voice references one copy of the analysed
    tables rather than each holding its own megabyte of mip-maps.

    With no table loaded it synthesises a built-in default set (sine through
    saw), so the engine is playable the moment it is selected.

    Three macro controls:
      * `position` (the DCW "amount" knob) -- morph across the frames.
      * `warp`     (the pulse-width knob)  -- phase-distorts the read position
        within a frame, bending the waveform without changing the table.
      * `formant`  (the per-engine "extra") -- reads the frame faster than the
        fundamental and re-triggers each period, shifting the spectral envelope
        independently of pitch.
*/
class WavetableOscillator
{
public:
    static constexpr int kFrameLen   = 2048;   // samples per single-cycle frame
    static constexpr int kMaxFrames  = 256;
    static constexpr int kNumMips    = 10;     // one per octave from the fundamental

    /** Analysed table data, shared by every voice. Build with makeWavetableSet. */
    struct WavetableSet
    {
        int numFrames = 0;
        // mip[frame][level] -> kFrameLen band-limited samples.
        std::vector<std::vector<std::vector<float>>> mip;
    };

    /** Builds mip-maps from raw single-cycle frames (each kFrameLen long). */
    static std::shared_ptr<WavetableSet> makeWavetableSet (const float* frames,
                                                           int numFrames,
                                                           int frameLen);

    /** The built-in fallback set: sine, triangle, square-ish, saw. */
    static std::shared_ptr<WavetableSet> defaultSet();

    void  setSampleRate  (double sampleRateHz) noexcept;
    void  setFrequency   (double frequencyHz) noexcept;
    void  setTable       (std::shared_ptr<WavetableSet> table) noexcept;
    void  setPosition    (double amount01) noexcept;      // frame morph
    void  setWarp        (double pulseWidth01) noexcept;  // intra-frame phase warp
    void  setFormant     (double formant01) noexcept;     // spectral shift
    void  setPhaseMod    (double offset) noexcept { phaseMod_ = offset; }
    void  setOversampling (int factor) noexcept;
    void  reset          () noexcept;

    bool  wrapped   () const noexcept { return wrapped_; }
    void  syncReset () noexcept { phase_ = 0.0; formantPhase_ = 0.0; }

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    double coreSample() noexcept;
    float  readFrame (int frame, int level, double ph) const noexcept;

    std::shared_ptr<WavetableSet> table_;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 440.0 / 44100.0;
    double phase_      = 0.0;
    double phaseMod_   = 0.0;
    double formantPhase_ = 0.0;

    double position_ = 0.0;
    double warp_     = 0.5;
    double formant_  = 0.0;

    int    mipLevel_ = 0;     // chosen from the pitch, so playback stays clean
    bool   wrapped_  = false;

    Oversampler os_;
    int         osFactor_ = 4;
};

} // namespace pdhybrid
