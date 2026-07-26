#include "DiodeLadderFilter.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kPi = 3.14159265358979323846;

// Where each stage's pole sits relative to the nominal cutoff. The geometric
// mean is ~1, so the overall -3 dB point still tracks the cutoff knob.
static constexpr double kSpread[4] = { 0.65, 0.90, 1.25, 1.70 };

// Diode asymmetry: tanh shifted off centre, then re-zeroed so sat(0) == 0.
static constexpr double kBias = 0.25;
static const     double kBiasTanh = std::tanh (kBias);

static inline double diodeSat (double y) noexcept
{
    return std::tanh (y + kBias) - kBiasTanh;
}

static inline double diodeSatDeriv (double y) noexcept
{
    const double t = std::tanh (y + kBias);
    return 1.0 - t * t;
}

void DiodeLadderFilter::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateCoefficients();
    }
}

void DiodeLadderFilter::setCutoff (double cutoffHz) noexcept
{
    if (cutoffHz == cutoff_)
        return;   // unchanged: skip the tan() calls in updateCoefficients
    cutoff_ = cutoffHz;
    updateCoefficients();
}

void DiodeLadderFilter::setResonance (double resonance01) noexcept
{
    if (resonance01 < 0.0) resonance01 = 0.0;
    if (resonance01 > 1.0) resonance01 = 1.0;

    // The spread poles reach 180 degrees with less loop gain in hand than the
    // coincident-pole ladder, so the diode version needs more feedback to reach
    // self-oscillation.
    k_    = 6.5 * resonance01;
    comp_ = 0.5 * resonance01;
}

void DiodeLadderFilter::reset() noexcept
{
    for (auto& s : s_)
        s = 0.0;
}

void DiodeLadderFilter::updateCoefficients() noexcept
{
    const double nyquistGuard = 0.49 * sampleRate_;

    for (int i = 0; i < kStages; ++i)
    {
        double fc = cutoff_ * kSpread[i];
        if (fc < 10.0)         fc = 10.0;
        if (fc > nyquistGuard) fc = nyquistGuard;

        g_[i] = std::tan (kPi * fc / sampleRate_);
        G_[i] = g_[i] / (1.0 + g_[i]);
    }
}

// One TPT one-pole lowpass stage. Updates state `s`, returns the lowpass output.
static inline double tptStage (double x, double& s, double G) noexcept
{
    const double v = (x - s) * G;
    const double y = v + s;
    s = y + v;
    return y;
}

float DiodeLadderFilter::processSample (float xin) noexcept
{
    const double x = static_cast<double> (xin);

    // Each stage: y_i = G_i * u_i + S_i, with S_i = s_i / (1 + g_i).
    const double S1 = s_[0] / (1.0 + g_[0]);
    const double S2 = s_[1] / (1.0 + g_[1]);
    const double S3 = s_[2] / (1.0 + g_[2]);
    const double S4 = s_[3] / (1.0 + g_[3]);

    // Cascade output as an affine function of the loop input u: y4 = A*u + B.
    const double A = G_[0] * G_[1] * G_[2] * G_[3];
    const double B = G_[3] * G_[2] * G_[1] * S1
                   + G_[3] * G_[2] * S2
                   + G_[3] * S3
                   + S4;

    // Solve the zero-delay loop  y4 = A*((1 + comp*k)*x - k*sat(y4)) + B.
    const double xIn = x * (1.0 + comp_ * k_);
    double y4 = (A * xIn + B) / (1.0 + A * k_);
    for (int it = 0; it < 4; ++it)
    {
        const double F  = A * (xIn - k_ * diodeSat (y4)) + B - y4;
        const double dF = -A * k_ * diodeSatDeriv (y4) - 1.0;   // <= -1, never zero
        y4 -= F / dF;
    }

    const double u = xIn - k_ * diodeSat (y4);
    double y = u;
    for (int i = 0; i < kStages; ++i)
        y = tptStage (y, s_[i], G_[i]);

    return static_cast<float> (y);
}

void DiodeLadderFilter::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
