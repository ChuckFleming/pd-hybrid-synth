#include "FormantFilter.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kPi = 3.14159265358979323846;

namespace {

// First three formants of the five cardinal vowels (Hz) and their relative
// levels. Ordered A - E - I - O - U so a rising morph sweeps a natural,
// continuous vowel arc rather than jumping between unrelated shapes.
constexpr double kFormantHz[FormantFilter::kNumVowels][FormantFilter::kNumFormants] =
{
    { 730.0, 1090.0, 2440.0 },   // A  (father)
    { 530.0, 1840.0, 2480.0 },   // E  (bed)
    { 270.0, 2290.0, 3010.0 },   // I  (see)
    { 570.0,  840.0, 2410.0 },   // O  (thought)
    { 300.0,  870.0, 2240.0 }    // U  (boot)
};

constexpr double kFormantGain[FormantFilter::kNumFormants] = { 1.0, 0.55, 0.30 };

} // namespace

void FormantFilter::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        updateCoefficients();
    }
}

void FormantFilter::setFrequency (double frequencyHz) noexcept
{
    // 1 kHz is the neutral tract length; the useful range is a couple of
    // octaves either side before the vowel stops reading as a voice.
    scale_ = std::clamp (frequencyHz / 1000.0, 0.25, 4.0);
    updateCoefficients();
}

void FormantFilter::setResonance (double resonance01) noexcept
{
    resonance_ = std::clamp (resonance01, 0.0, 1.0);
    updateCoefficients();
}

void FormantFilter::setVowel (double vowel01) noexcept
{
    vowel_ = std::clamp (vowel01, 0.0, 1.0);
    updateCoefficients();
}

void FormantFilter::reset() noexcept
{
    for (auto& b : bands_)
    {
        b.ic1 = 0.0;
        b.ic2 = 0.0;
    }
}

void FormantFilter::updateCoefficients() noexcept
{
    // Interpolate between the two vowels the morph currently sits between.
    const double pos  = vowel_ * (kNumVowels - 1);
    const int    i0   = std::min (static_cast<int> (pos), kNumVowels - 2);
    const double frac = pos - i0;

    // Q from 4 (soft colouring) to 22 (sharply vowelled).
    const double q = 4.0 + 18.0 * resonance_;
    wet_ = 0.25 + 0.75 * resonance_;

    const double nyquistGuard = 0.45 * sampleRate_;

    for (int f = 0; f < kNumFormants; ++f)
    {
        double hz = (1.0 - frac) * kFormantHz[i0][f] + frac * kFormantHz[i0 + 1][f];
        hz *= scale_;
        hz = std::clamp (hz, 20.0, nyquistGuard);

        auto& b = bands_[f];
        b.g = std::tan (kPi * hz / sampleRate_);
        b.k = 1.0 / q;
        b.a1 = 1.0 / (1.0 + b.g * (b.g + b.k));
        b.a2 = b.g * b.a1;
        b.a3 = b.g * b.a2;
        // Bandpass peak gain is 1/k, so normalise back to the intended level.
        b.gain = kFormantGain[f] * b.k;
    }
}

float FormantFilter::processSample (float xin) noexcept
{
    const double v0 = static_cast<double> (xin);

    double wet = 0.0;
    for (auto& b : bands_)
    {
        const double v3 = v0 - b.ic2;
        const double v1 = b.a1 * b.ic1 + b.a2 * v3;
        const double v2 = b.ic2 + b.a2 * b.ic1 + b.a3 * v3;
        b.ic1 = 2.0 * v1 - b.ic1;
        b.ic2 = 2.0 * v2 - b.ic2;

        wet += v1 * b.gain;   // v1 is the bandpass output
    }

    return static_cast<float> ((1.0 - wet_) * v0 + wet_ * wet * 2.0);
}

void FormantFilter::processBlock (float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = processSample (buffer[i]);
}

} // namespace pdhybrid
