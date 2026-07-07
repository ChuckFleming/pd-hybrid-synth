#pragma once

namespace pdhybrid {

enum class LfoWave
{
    Sine = 0,
    Triangle,
    Square,
    Saw
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
    void reset         () noexcept { phase_ = 0.0; }

    double value        () const noexcept;   // current output, no advance
    double processSample () noexcept;         // return current, then advance one sample
    void   advance      (int numSamples) noexcept;

private:
    double compute (double phase) const noexcept;

    double  sampleRate_ = 44100.0;
    double  frequency_  = 5.0;
    double  inc_        = 5.0 / 44100.0;
    double  phase_      = 0.0;
    LfoWave wave_       = LfoWave::Sine;
};

} // namespace pdhybrid
