#include "Spectrum.h"
#include "Fft.h"

#include <cmath>
#include <complex>
#include <algorithm>

namespace harness {

static constexpr double kPi = 3.14159265358979323846;

Spectrum computeSpectrum (const std::vector<float>& samples, double sampleRate, bool hann)
{
    const std::size_t count = samples.size();
    const std::size_t n     = nextPowerOfTwo (count == 0 ? 1 : count);

    std::vector<std::complex<double>> buf (n, std::complex<double> (0.0, 0.0));
    double windowSum = 0.0;

    for (std::size_t i = 0; i < count; ++i)
    {
        double w = 1.0;
        if (hann && count > 1)
            w = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i)
                                      / static_cast<double> (count - 1));
        buf[i] = std::complex<double> (static_cast<double> (samples[i]) * w, 0.0);
        windowSum += w;
    }

    fft (buf);

    Spectrum s;
    s.sampleRate = sampleRate;
    s.binHz      = sampleRate / static_cast<double> (n);

    const std::size_t half = n / 2 + 1;
    s.magnitude.resize (half);

    // Normalise so a full-scale sine reads ~1.0 regardless of window / FFT size.
    const double norm = (windowSum > 0.0) ? (2.0 / windowSum) : (2.0 / static_cast<double> (n));
    for (std::size_t k = 0; k < half; ++k)
        s.magnitude[k] = std::abs (buf[k]) * norm;

    return s;
}

std::size_t Spectrum::binOfFrequency (double hz) const noexcept
{
    if (binHz <= 0.0 || magnitude.empty())
        return 0;
    long b = std::lround (hz / binHz);
    if (b < 0) b = 0;
    if (static_cast<std::size_t> (b) >= magnitude.size())
        b = static_cast<long> (magnitude.size()) - 1;
    return static_cast<std::size_t> (b);
}

double Spectrum::magnitudeNearHz (double hz, int binRadius) const noexcept
{
    if (magnitude.empty())
        return 0.0;
    const long center = static_cast<long> (binOfFrequency (hz));
    double m = 0.0;
    for (long k = center - binRadius; k <= center + binRadius; ++k)
        if (k >= 1 && static_cast<std::size_t> (k) < magnitude.size())
            m = std::max (m, magnitude[static_cast<std::size_t> (k)]);
    return m;
}

double Spectrum::peakFrequency () const noexcept
{
    if (magnitude.size() < 3)
        return 0.0;

    std::size_t best = 1;
    double      bestVal = -1.0;
    for (std::size_t k = 1; k + 1 < magnitude.size(); ++k)
        if (magnitude[k] > bestVal) { bestVal = magnitude[k]; best = k; }

    // Parabolic interpolation of the peak for sub-bin accuracy.
    const double a = magnitude[best - 1];
    const double b = magnitude[best];
    const double c = magnitude[best + 1];
    const double denom = a - 2.0 * b + c;
    const double delta = (denom != 0.0) ? 0.5 * (a - c) / denom : 0.0;
    return (static_cast<double> (best) + delta) * binHz;
}

double totalHarmonicDistortion (const Spectrum& s, double fundamentalHz, int numHarmonics)
{
    const double fundamental = s.magnitudeNearHz (fundamentalHz);
    if (fundamental <= 0.0)
        return 0.0;

    double sumSq = 0.0;
    for (int h = 2; h <= numHarmonics; ++h)
    {
        const double m = s.magnitudeNearHz (fundamentalHz * h);
        sumSq += m * m;
    }
    return std::sqrt (sumSq) / fundamental;
}

double energyBelowHz (const Spectrum& s, double hz)
{
    const std::size_t limit = s.binOfFrequency (hz);
    double e = 0.0;
    for (std::size_t k = 1; k < limit && k < s.magnitude.size(); ++k)
        e += s.magnitude[k] * s.magnitude[k];
    return e;
}

double totalEnergy (const Spectrum& s)
{
    double e = 0.0;
    for (std::size_t k = 1; k < s.magnitude.size(); ++k)
        e += s.magnitude[k] * s.magnitude[k];
    return e;
}

} // namespace harness
