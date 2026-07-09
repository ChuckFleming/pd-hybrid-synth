#pragma once

#include "AnalogOscillator.h"
#include "Waveshaper.h"
#include "MultiStageEnvelope.h"
#include <vector>

namespace pdhybrid {

// Which held note the monophonic bass tracks when several are down.
enum class BassPriority { Last = 0, Top, Bottom };

/**
    Monophonic sub-bass layer: one analog oscillator plus a sub an octave below,
    driven through a wavefolder then a saturator ("harmonics"), shaped by its own
    amp ADSR. It keeps its own held-note stack and picks the sounding note by
    Last/Top/Bottom priority, gliding between notes and only retriggering the
    envelope when starting from silence. Summed at centre into the mix pre-FX.

    Headless and JUCE-free; fed note events by the processor.
*/
class MonoBass
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void reset         () noexcept;

    // --- Configuration ---
    void setEnabled  (bool on) noexcept          { enabled_ = on; }
    void setWaveform (AnalogWave wave) noexcept  { main_.setWaveform (wave); sub_.setWaveform (wave); }
    void setOctave   (int octave) noexcept       { octave_ = octave; retune(); }
    void setTuneCents(double cents) noexcept     { tuneCents_ = cents; retune(); }
    void setHarmonics(double amount01) noexcept;  // 0 = clean .. 1 = folded + saturated
    void setLevel    (double level01) noexcept   { level_ = level01; }
    void setGlideTime(double seconds) noexcept   { glideTime_ = seconds < 0.0 ? 0.0 : seconds; }
    void setPriority (BassPriority p) noexcept   { priority_ = p; updateTarget (false); }
    void setADSR     (double a, double d, double s, double r) noexcept { env_.setADSR (a, d, s, r); }

    // --- Note events (from the processor) ---
    void noteOn     (int note, float velocity) noexcept;
    void noteOff    (int note) noexcept;
    void allNotesOff() noexcept;

    bool enabled     () const noexcept { return enabled_; }
    bool isActive    () const noexcept { return env_.isActive(); }
    int  currentNote () const noexcept { return curNote_; }   // -1 = none (for tests)

    // Adds this block of mono bass into `mono` (centre). No-op when disabled.
    void renderBlock (float* mono, int numSamples) noexcept;

private:
    static double noteHz (int note) noexcept;
    void   retune () noexcept;
    int    selectNote () const noexcept;                 // -1 if nothing held
    void   updateTarget (bool allowRetrigger) noexcept;  // pick note by priority
    double glideCoef () const noexcept;

    AnalogOscillator   main_, sub_;
    Waveshaper         fold_, sat_;
    MultiStageEnvelope env_;

    std::vector<int> held_;             // held notes in press order

    double sampleRate_ = 44100.0;
    bool   enabled_    = false;
    int    octave_     = -1;            // default one octave below the played note
    double tuneCents_  = 0.0;
    double tuneMul_    = 1.0;
    double harmonics_  = 0.0;
    double level_      = 0.8;
    double glideTime_  = 0.05;
    BassPriority priority_ = BassPriority::Last;

    int    curNote_    = -1;
    float  velGain_    = 1.0f;
    float  pendingVel_ = 1.0f;
    double curLogHz_   = 0.0;           // current (glided) pitch, log Hz
    double targetHz_   = 55.0;
};

} // namespace pdhybrid
