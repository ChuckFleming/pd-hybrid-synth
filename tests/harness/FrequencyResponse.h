#pragma once

#include <vector>
#include <cmath>
#include <cstddef>

namespace harness {

struct FreqPoint
{
    double hz;
    double gainDb;
};

/**
    Measures the steady-state gain (dB) of a filter at one frequency by driving
    it with a small-amplitude sine, discarding the transient, then comparing
    output RMS to input RMS. Small amplitude keeps mildly nonlinear filters
    (e.g. tanh feedback) in their near-linear regime for a meaningful response.

    `Filter` must expose `void reset()` and `float processSample(float)`.
    Configure the filter (cutoff/resonance) before calling.
*/
template <typename Filter>
double measureGainDb (Filter& filter, double hz, double sampleRate,
                      double amplitude = 0.05,
                      int settleCycles = 120, int measureCycles = 60)
{
    filter.reset();

    const double twoPi = 6.283185307179586476925287;
    const double w     = twoPi * hz / sampleRate;

    const long settle = static_cast<long> (std::ceil (settleCycles  * sampleRate / hz));
    const long meas   = static_cast<long> (std::ceil (measureCycles * sampleRate / hz));

    double phase = 0.0;
    for (long i = 0; i < settle; ++i)
    {
        const float x = static_cast<float> (amplitude * std::sin (phase));
        filter.processSample (x);
        phase += w;
        if (phase >= twoPi) phase -= twoPi;
    }

    double sumOut = 0.0;
    for (long i = 0; i < meas; ++i)
    {
        const float  x = static_cast<float> (amplitude * std::sin (phase));
        const double y = filter.processSample (x);
        sumOut += y * y;
        phase += w;
        if (phase >= twoPi) phase -= twoPi;
    }

    const double outRms = std::sqrt (sumOut / static_cast<double> (meas));
    const double inRms  = amplitude / std::sqrt (2.0);
    double gain = outRms / (inRms > 1e-15 ? inRms : 1e-15);
    if (gain < 1e-9) gain = 1e-9;
    return 20.0 * std::log10 (gain);
}

// Log-spaced magnitude sweep. Filter is re-measured (and reset) per point.
template <typename Filter>
std::vector<FreqPoint> sweepResponse (Filter& filter, double sampleRate,
                                      double fLow, double fHigh, int numPoints,
                                      double amplitude = 0.05)
{
    std::vector<FreqPoint> pts;
    pts.reserve (static_cast<std::size_t> (numPoints));
    for (int i = 0; i < numPoints; ++i)
    {
        const double t  = (numPoints <= 1) ? 0.0 : static_cast<double> (i) / (numPoints - 1);
        const double hz = fLow * std::pow (fHigh / fLow, t);
        pts.push_back ({ hz, measureGainDb (filter, hz, sampleRate, amplitude) });
    }
    return pts;
}

// First frequency (interpolated in log-freq) where the response drops `dropDb`
// below `passbandDb`. Returns -1 if never crossed.
inline double findRolloffHz (const std::vector<FreqPoint>& pts, double passbandDb, double dropDb = 3.0)
{
    const double target = passbandDb - dropDb;
    for (std::size_t i = 1; i < pts.size(); ++i)
    {
        if (pts[i].gainDb <= target && pts[i - 1].gainDb > target)
        {
            const double lf0 = std::log (pts[i - 1].hz);
            const double lf1 = std::log (pts[i].hz);
            const double g0  = pts[i - 1].gainDb;
            const double g1  = pts[i].gainDb;
            const double frac = (target - g0) / (g1 - g0);
            return std::exp (lf0 + frac * (lf1 - lf0));
        }
    }
    return -1.0;
}

} // namespace harness
