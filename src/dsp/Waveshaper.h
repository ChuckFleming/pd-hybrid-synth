#pragma once

namespace pdhybrid {

enum class ShaperCurve
{
    Tanh,      // symmetric soft clip (odd harmonics)
    Cubic,     // symmetric cubic soft clip (odd harmonics)
    HardClip,  // brick-wall clip (bright, odd)
    Tube,      // asymmetric soft clip (even + odd)
    Diode,     // asymmetric diode-pair (buzzy, even)
    Fuzz,      // high-gain near-square
    Rectify,   // full-wave rectifier (octave up)
    Wavefold,  // sine wavefolder (metallic)
    Foldback   // triangle foldback
};

/**
    Memoryless waveshaper: applies input drive and an optional bias, then a
    saturating transfer curve. Bias shifts the operating point so a symmetric
    input produces even harmonics (tube-like). Separated out so its static
    transfer curve and harmonic content are trivially testable in isolation.
*/
class Waveshaper
{
public:
    void setDrive (double drive) noexcept  { drive_ = drive < 0.0 ? 0.0 : drive; }
    void setBias  (double bias)  noexcept  { bias_  = bias; }
    void setCurve (ShaperCurve c) noexcept { curve_ = c; }

    float process (float x) const noexcept;

private:
    double      drive_ = 1.0;
    double      bias_  = 0.0;
    ShaperCurve curve_ = ShaperCurve::Tanh;
};

} // namespace pdhybrid
