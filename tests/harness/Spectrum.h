#pragma once

#include <vector>
#include <cstddef>

namespace harness {

/**
    Magnitude spectrum plus the measurement helpers our DSP assertions are
    expressed through (fundamental detection, harmonic content, THD, aliasing
    energy). Shared by every module's tests.
*/
struct Spectrum
{
    std::vector<double> magnitude;   // linear magnitude per bin, size N/2 + 1
    double sampleRate = 0.0;
    double binHz      = 0.0;         // frequency resolution

    double      frequencyOfBin (std::size_t bin) const noexcept { return bin * binHz; }
    std::size_t binOfFrequency (double hz) const noexcept;

    // Magnitude at a frequency, taking the local peak within +/- binRadius bins
    // (robust to spectral leakage from windowing).
    double magnitudeNearHz (double hz, int binRadius = 2) const noexcept;

    // Frequency of the highest-magnitude bin, refined by parabolic interpolation.
    double peakFrequency () const noexcept;
};

// Magnitude spectrum with an optional Hann window (recommended for measurement).
Spectrum computeSpectrum (const std::vector<float>& samples, double sampleRate, bool hann = true);

// Total harmonic distortion as a ratio (sqrt(sum harmonic^2) / fundamental).
double totalHarmonicDistortion (const Spectrum& s, double fundamentalHz, int numHarmonics = 12);

// Summed squared magnitude of bins strictly below `hz` (excluding DC).
double energyBelowHz (const Spectrum& s, double hz);

// Summed squared magnitude across all bins (excluding DC).
double totalEnergy (const Spectrum& s);

} // namespace harness
