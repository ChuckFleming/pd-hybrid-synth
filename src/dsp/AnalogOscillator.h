#pragma once

namespace pdhybrid {

enum class AnalogWave
{
    Saw = 0,
    Square,
    Triangle,
    Pulse
};

/**
    Bandlimited analog-style oscillator using PolyBLEP (polynomial band-limited
    step) to suppress the aliasing that naive saw/square edges produce. Square
    and pulse also get a pulse-width control; triangle is the integral of the
    band-limited square.

    Pure C++, no JUCE. Deterministic (drift/detune belong to the voice layer).
*/
class AnalogOscillator
{
public:
    void setSampleRate (double sampleRateHz) noexcept;
    void setFrequency  (double frequencyHz) noexcept;
    void setWaveform   (AnalogWave wave) noexcept   { wave_ = wave; }
    void setPulseWidth (double pulseWidth01) noexcept;
    void reset         () noexcept;

    float processSample () noexcept;
    void  processBlock  (float* out, int numSamples) noexcept;

private:
    double squareValue (double pulseWidth) const noexcept;

    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double inc_        = 440.0 / 44100.0;
    double phase_      = 0.0;
    double pulseWidth_ = 0.5;
    double triState_   = 0.0;
    AnalogWave wave_   = AnalogWave::Saw;
};

} // namespace pdhybrid
