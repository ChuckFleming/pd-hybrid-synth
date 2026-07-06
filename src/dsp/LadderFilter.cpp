#include "LadderFilter.h"
#include <cmath>

namespace pdhybrid {

static constexpr double kPi = 3.14159265358979323846;

void LadderFilter::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateCoefficients();
    }
}

void LadderFilter::setCutoff (double cutoffHz) noexcept
{
    cutoff_ = cutoffHz;
    updateCoefficients();
}

void LadderFilter::setResonance (double resonance01) noexcept
{
    if (resonance01 < 0.0) resonance01 = 0.0;
    if (resonance01 > 1.0) resonance01 = 1.0;
    // Map to feedback k; k ~= 4 is the self-oscillation threshold, so max
    // resonance pushes just past it.
    k_ = 4.4 * resonance01;
}

void LadderFilter::reset() noexcept
{
    s1_ = s2_ = s3_ = s4_ = 0.0;
}

void LadderFilter::updateCoefficients() noexcept
{
    double fc = cutoff_;
    const double nyquistGuard = 0.49 * sampleRate_;
    if (fc < 10.0)            fc = 10.0;
    if (fc > nyquistGuard)    fc = nyquistGuard;

    g_ = std::tan (kPi * fc / sampleRate_);
    G_ = g_ / (1.0 + g_);
}

// One TPT one-pole lowpass stage. Updates state `s`, returns the lowpass output.
static inline double tptStage (double x, double& s, double G) noexcept
{
    const double v = (x - s) * G;
    const double y = v + s;
    s = y + v;
    return y;
}

float LadderFilter::processSample (float xin) noexcept
{
    const double x = static_cast<double> (xin);
    const double G = G_;
    const double invOnePlusG = 1.0 / (1.0 + g_);

    // Contribution of each stage's state, independent of this sample's input:
    //   y_i = G * x_in + S_i,   S_i = s_i / (1 + g)
    const double S1 = s1_ * invOnePlusG;
    const double S2 = s2_ * invOnePlusG;
    const double S3 = s3_ * invOnePlusG;
    const double S4 = s4_ * invOnePlusG;

    // Ladder output as an affine function of the loop input u:  y4 = A*u + B.
    const double A = G * G * G * G;
    const double B = G * G * G * S1 + G * G * S2 + G * S3 + S4;

    // Solve the zero-delay feedback loop  y4 = A*(x - k*tanh(y4)) + B.
    // Start from the linear-feedback solution, refine with Newton.
    double y4 = (A * x + B) / (1.0 + A * k_);
    for (int it = 0; it < 4; ++it)
    {
        const double th = std::tanh (y4);
        const double F  = A * (x - k_ * th) + B - y4;
        const double dF = -A * k_ * (1.0 - th * th) - 1.0;   // always <= -1, never zero
        y4 -= F / dF;
    }

    // Run the stages with the resolved loop input to advance the states.
    const double u  = x - k_ * std::tanh (y4);
    const double y1 = tptStage (u,  s1_, G);
    const double y2 = tptStage (y1, s2_, G);
    const double y3 = tptStage (y2, s3_, G);
    const double y4o = tptStage (y3, s4_, G);

    return static_cast<float> (y4o);
}

void LadderFilter::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
