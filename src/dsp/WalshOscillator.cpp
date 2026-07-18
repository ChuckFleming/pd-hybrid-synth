#include "WalshOscillator.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace pdhybrid {

namespace {

// The sequency-ordered Walsh basis is identical for every voice, so build it once.
// Rows are the first kNumFuncs Walsh functions (values +/-1), ordered by sequency
// (ascending sign-change count) -- the Walsh analogue of ascending frequency.
struct WalshBasis
{
    signed char row[WalshOscillator::kNumFuncs][WalshOscillator::kTableLen];

    WalshBasis()
    {
        constexpr int L = WalshOscillator::kTableLen;

        // Natural-order Hadamard matrix (recursive +/-1 construction).
        static std::array<std::array<signed char, L>, L> H;
        H[0][0] = 1;
        for (int len = 1; len < L; len *= 2)
            for (int i = 0; i < len; ++i)
                for (int j = 0; j < len; ++j)
                {
                    const signed char v = H[i][j];
                    H[i][j + len]       =  v;
                    H[i + len][j]       =  v;
                    H[i + len][j + len] = (signed char) -v;
                }

        // Sequency = number of sign changes along the row.
        int seq[L];
        for (int i = 0; i < L; ++i)
        {
            int c = 0;
            for (int j = 1; j < L; ++j)
                if (H[i][j] != H[i][j - 1]) ++c;
            seq[i] = c;
        }

        // Row indices sorted by sequency give the Walsh (sequency) ordering.
        int order[L];
        for (int i = 0; i < L; ++i) order[i] = i;
        std::sort (order, order + L, [&] (int a, int b) { return seq[a] < seq[b]; });

        for (int k = 0; k < WalshOscillator::kNumFuncs; ++k)
            for (int n = 0; n < L; ++n)
                row[k][n] = H[order[k]][n];
    }
};

const WalshBasis& walshBasis()
{
    static const WalshBasis b;   // thread-safe one-time init
    return b;
}

} // namespace

void WalshOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        phaseInc_ = frequency_ / sampleRate_;
    }
}

void WalshOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    phaseInc_ = frequency_ / sampleRate_;
}

void WalshOscillator::setTilt (double amount01) noexcept
{
    tilt_ = std::clamp (amount01, 0.0, 1.0);
    if (tilt_ != tiltBuilt_ || oddness_ != oddnessBuilt_ || fold_ != foldBuilt_)
        rebuildTable();
}

void WalshOscillator::setOddness (double pulseWidth01) noexcept
{
    oddness_ = std::clamp (pulseWidth01, 0.0, 1.0);
    if (tilt_ != tiltBuilt_ || oddness_ != oddnessBuilt_ || fold_ != foldBuilt_)
        rebuildTable();
}

void WalshOscillator::setFold (double fold01) noexcept
{
    fold_ = std::clamp (fold01, 0.0, 1.0);
    if (tilt_ != tiltBuilt_ || oddness_ != oddnessBuilt_ || fold_ != foldBuilt_)
        rebuildTable();
}

void WalshOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void WalshOscillator::reset() noexcept
{
    phase_ = 0.0;
    wrapped_ = false;
    rebuildTable();
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

void WalshOscillator::rebuildTable() noexcept
{
    const auto& basis = walshBasis();

    for (int n = 0; n < kTableLen; ++n) table_[n] = 0.0;

    // Spectral slope over the sequency basis; term 0 (the DC constant) is skipped.
    const double exponent = 2.0 - 1.8 * tilt_;
    const double evenScale = 0.25 + 0.75 * (1.0 - oddness_);
    const double oddScale  = 0.25 + 0.75 * oddness_;

    for (int k = 1; k < kNumFuncs; ++k)
    {
        const double mag = std::pow ((double) k, -exponent);
        const double coef = mag * ((k % 2 == 0) ? evenScale : oddScale);
        const signed char* r = basis.row[k];
        for (int n = 0; n < kTableLen; ++n)
            table_[n] += coef * r[n];
    }

    // Normalise to unit-ish level before folding.
    double peak = 0.0;
    for (int n = 0; n < kTableLen; ++n) peak = std::max (peak, std::abs (table_[n]));
    if (peak > 1.0e-9)
    {
        const double g = 1.0 / peak;
        for (int n = 0; n < kTableLen; ++n) table_[n] *= g;
    }

    // Wavefold: drive the table past +/-1 and reflect it back. Identity at
    // fold_ = 0 (the table is within +/-1), progressively gritty as it rises.
    const double drive = 1.0 + fold_ * 4.0;
    for (int n = 0; n < kTableLen; ++n)
    {
        double x = table_[n] * drive;
        for (int it = 0; it < 4 && (x > 1.0 || x < -1.0); ++it)
        {
            if (x >  1.0) x =  2.0 - x;
            if (x < -1.0) x = -2.0 - x;
        }
        table_[n] = std::clamp (x, -1.0, 1.0) * 0.9;
    }

    tiltBuilt_ = tilt_;
    oddnessBuilt_ = oddness_;
    foldBuilt_ = fold_;
}

double WalshOscillator::coreSample() noexcept
{
    double ph = phase_ + phaseMod_;
    ph -= std::floor (ph);

    const double pos = ph * kTableLen;
    int i0 = (int) pos;
    if (i0 >= kTableLen) i0 -= kTableLen;
    const int i1 = (i0 + 1 == kTableLen) ? 0 : i0 + 1;
    const double frac = pos - std::floor (pos);
    const double y = table_[i0] + frac * (table_[i1] - table_[i0]);

    phase_ += phaseInc_ / osFactor_;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        wrapped_ = true;
    }
    return y;
}

float WalshOscillator::processSample() noexcept
{
    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void WalshOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
