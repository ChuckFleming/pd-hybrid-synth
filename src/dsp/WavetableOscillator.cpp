#include "WavetableOscillator.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

static constexpr double kTwoPi = 6.283185307179586476925287;

namespace {

// Naive DFT of one frame. Only ever runs at load time (never on the audio
// thread), and kFrameLen is fixed, so the O(N^2) cost is a one-off.
void analyseFrame (const float* src, int len,
                   std::vector<double>& re, std::vector<double>& im)
{
    const int half = len / 2;
    re.assign (static_cast<std::size_t> (half), 0.0);
    im.assign (static_cast<std::size_t> (half), 0.0);

    for (int k = 1; k < half; ++k)
    {
        double sr = 0.0, si = 0.0;
        const double w = kTwoPi * k / len;
        // Recurrence rather than a sin/cos per sample per harmonic.
        const double c2 = 2.0 * std::cos (w);
        double cPrev = 1.0,          cCur = std::cos (w);
        double sPrev = 0.0,          sCur = std::sin (w);

        sr += src[0] * cPrev;
        si -= src[0] * sPrev;
        if (len > 1) { sr += src[1] * cCur; si -= src[1] * sCur; }

        for (int n = 2; n < len; ++n)
        {
            const double cNext = c2 * cCur - cPrev;
            const double sNext = c2 * sCur - sPrev;
            sr += src[n] * cNext;
            si -= src[n] * sNext;
            cPrev = cCur; cCur = cNext;
            sPrev = sCur; sCur = sNext;
        }
        re[static_cast<std::size_t> (k)] = sr * 2.0 / len;
        im[static_cast<std::size_t> (k)] = si * 2.0 / len;
    }
}

// Rebuilds a frame from harmonics 1..maxHarmonic.
void synthFrame (const std::vector<double>& re, const std::vector<double>& im,
                 int maxHarmonic, int len, std::vector<float>& dest)
{
    std::vector<double> acc (static_cast<std::size_t> (len), 0.0);
    const int half = static_cast<int> (re.size());
    const int top  = std::min (maxHarmonic, half - 1);

    for (int k = 1; k <= top; ++k)
    {
        const double a = re[static_cast<std::size_t> (k)];
        const double b = im[static_cast<std::size_t> (k)];
        if (std::abs (a) < 1.0e-7 && std::abs (b) < 1.0e-7)
            continue;

        const double w  = kTwoPi * k / len;
        const double c2 = 2.0 * std::cos (w);
        double cPrev = 1.0,   cCur = std::cos (w);
        double sPrev = 0.0,   sCur = std::sin (w);

        acc[0] += a * cPrev + b * sPrev;
        if (len > 1) acc[1] += a * cCur + b * sCur;
        for (int n = 2; n < len; ++n)
        {
            const double cNext = c2 * cCur - cPrev;
            const double sNext = c2 * sCur - sPrev;
            acc[static_cast<std::size_t> (n)] += a * cNext + b * sNext;
            cPrev = cCur; cCur = cNext;
            sPrev = sCur; sCur = sNext;
        }
    }

    dest.resize (static_cast<std::size_t> (len));
    for (int n = 0; n < len; ++n)
        dest[static_cast<std::size_t> (n)] = static_cast<float> (acc[static_cast<std::size_t> (n)]);
}

} // namespace

std::shared_ptr<WavetableOscillator::WavetableSet>
WavetableOscillator::makeWavetableSet (const float* frames, int numFrames, int frameLen)
{
    auto set = std::make_shared<WavetableSet>();
    if (frames == nullptr || numFrames <= 0 || frameLen <= 1)
        return set;

    numFrames = std::min (numFrames, kMaxFrames);
    set->numFrames = numFrames;
    set->mip.resize (static_cast<std::size_t> (numFrames));

    std::vector<double> re, im;
    std::vector<float>  resampled (static_cast<std::size_t> (kFrameLen));

    for (int f = 0; f < numFrames; ++f)
    {
        // Resample whatever frame length the caller supplied onto kFrameLen.
        const float* src = frames + static_cast<std::size_t> (f) * frameLen;
        for (int n = 0; n < kFrameLen; ++n)
        {
            const double pos = static_cast<double> (n) * frameLen / kFrameLen;
            const int    i0  = static_cast<int> (pos) % frameLen;
            const int    i1  = (i0 + 1) % frameLen;
            const double fr  = pos - std::floor (pos);
            resampled[static_cast<std::size_t> (n)] =
                static_cast<float> (src[i0] * (1.0 - fr) + src[i1] * fr);
        }

        analyseFrame (resampled.data(), kFrameLen, re, im);

        auto& levels = set->mip[static_cast<std::size_t> (f)];
        levels.resize (kNumMips);
        for (int lvl = 0; lvl < kNumMips; ++lvl)
        {
            // Level L is safe up to a pitch of (sr/2) / maxHarmonic; halving
            // the harmonic count each level buys one octave of headroom.
            const int maxH = std::max (1, (kFrameLen / 2) >> (lvl + 1));
            synthFrame (re, im, maxH, kFrameLen, levels[static_cast<std::size_t> (lvl)]);
        }
    }

    return set;
}

std::shared_ptr<WavetableOscillator::WavetableSet> WavetableOscillator::defaultSet()
{
    // Built once and shared: sine -> triangle -> square -> saw, so the engine
    // has something musical to play before any file is loaded.
    static std::shared_ptr<WavetableSet> cached = []
    {
        constexpr int kFrames = 4;
        std::vector<float> raw (static_cast<std::size_t> (kFrames * kFrameLen));

        for (int n = 0; n < kFrameLen; ++n)
        {
            const double t = static_cast<double> (n) / kFrameLen;
            double sine = std::sin (kTwoPi * t);

            // Band-limited by construction: sum a modest harmonic series.
            double tri = 0.0, sqr = 0.0, saw = 0.0;
            for (int k = 1; k <= 63; ++k)
            {
                const double s = std::sin (kTwoPi * k * t);
                saw += s / k;
                if (k % 2 == 1)
                {
                    sqr += s / k;
                    const double sign = ((k - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
                    tri += sign * std::sin (kTwoPi * k * t) / (k * k);
                }
            }

            raw[static_cast<std::size_t> (0 * kFrameLen + n)] = static_cast<float> (sine);
            raw[static_cast<std::size_t> (1 * kFrameLen + n)] = static_cast<float> (tri * 0.81);
            raw[static_cast<std::size_t> (2 * kFrameLen + n)] = static_cast<float> (sqr * 1.27);
            raw[static_cast<std::size_t> (3 * kFrameLen + n)] = static_cast<float> (saw * 0.63);
        }

        return makeWavetableSet (raw.data(), kFrames, kFrameLen);
    }();

    return cached;
}

void WavetableOscillator::setSampleRate (double sampleRateHz) noexcept
{
    if (sampleRateHz > 0.0)
    {
        sampleRate_ = sampleRateHz;
        setFrequency (frequency_);
    }
}

void WavetableOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency_ = frequencyHz;
    phaseInc_ = frequency_ / sampleRate_;

    // Pick the mip level whose harmonic ceiling still fits below Nyquist at
    // this pitch. One level per octave, matching how the set was built.
    const double ratio = std::max (1.0e-6, frequency_ * 2.0 / sampleRate_);
    int lvl = static_cast<int> (std::floor (std::log2 (ratio * (kFrameLen / 2)))) + 1;
    mipLevel_ = std::clamp (lvl, 0, kNumMips - 1);
}

void WavetableOscillator::setTable (std::shared_ptr<WavetableSet> table) noexcept
{
    table_ = std::move (table);
}

void WavetableOscillator::setPosition (double amount01) noexcept
{
    position_ = std::clamp (amount01, 0.0, 1.0);
}

void WavetableOscillator::setWarp (double pulseWidth01) noexcept
{
    warp_ = std::clamp (pulseWidth01, 0.0, 1.0);
}

void WavetableOscillator::setFormant (double formant01) noexcept
{
    formant_ = std::clamp (formant01, 0.0, 1.0);
}

void WavetableOscillator::setOversampling (int factor) noexcept
{
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    osFactor_ = factor;
    os_.prepare (factor);
    os_.reset();
}

void WavetableOscillator::reset() noexcept
{
    phase_ = 0.0;
    formantPhase_ = 0.0;
    wrapped_ = false;
    if (table_ == nullptr)
        table_ = defaultSet();
    if (os_.factor() != osFactor_)
        os_.prepare (osFactor_);
    os_.reset();
}

float WavetableOscillator::readFrame (int frame, int level, double ph) const noexcept
{
    const auto& data = table_->mip[static_cast<std::size_t> (frame)][static_cast<std::size_t> (level)];
    if (data.empty())
        return 0.0f;

    const double pos = ph * kFrameLen;
    int i0 = static_cast<int> (pos);
    if (i0 >= kFrameLen) i0 -= kFrameLen;
    if (i0 < 0) i0 = 0;
    const int i1 = (i0 + 1 == kFrameLen) ? 0 : i0 + 1;
    const double fr = pos - std::floor (pos);
    return static_cast<float> (data[static_cast<std::size_t> (i0)] * (1.0 - fr)
                             + data[static_cast<std::size_t> (i1)] * fr);
}

double WavetableOscillator::coreSample() noexcept
{
    if (table_ == nullptr || table_->numFrames <= 0)
        return 0.0;

    double ph = phase_ + phaseMod_;
    ph -= std::floor (ph);

    // Intra-frame phase warp: the same two-segment map the PD engine uses, so
    // the knob bends the stored waveform rather than selecting a new one.
    if (std::abs (warp_ - 0.5) > 1.0e-6)
    {
        const double amt = (warp_ - 0.5) * 2.0;              // -1 .. 1
        const double m   = 0.5 - 0.49 * std::abs (amt);
        const double p   = (amt >= 0.0) ? ph : 1.0 - ph;
        ph = (p < m) ? 0.5 * (p / m)
                     : 0.5 + 0.5 * ((p - m) / (1.0 - m));
    }

    // Formant shift: read the frame faster, restarting each fundamental period
    // so the pitch is unchanged but the spectral envelope moves up.
    double readPh = ph;
    if (formant_ > 1.0e-6)
    {
        const double mult = 1.0 + formant_ * 3.0;
        readPh = formantPhase_ * mult;
        readPh -= std::floor (readPh);
    }

    // Morph between adjacent frames.
    const int    n     = table_->numFrames;
    const double fpos  = position_ * (n - 1);
    const int    f0    = std::clamp (static_cast<int> (fpos), 0, n - 1);
    const int    f1    = std::min (f0 + 1, n - 1);
    const double blend = fpos - f0;

    const double a = readFrame (f0, mipLevel_, readPh);
    const double b = readFrame (f1, mipLevel_, readPh);
    const double y = (1.0 - blend) * a + blend * b;

    const double step = phaseInc_ / osFactor_;
    phase_ += step;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        wrapped_ = true;
        formantPhase_ = 0.0;   // re-trigger the formant read each period
    }
    formantPhase_ += step;
    if (formantPhase_ >= 1.0)
        formantPhase_ -= 1.0;

    return y;
}

float WavetableOscillator::processSample() noexcept
{
    wrapped_ = false;
    float high[8];
    for (int j = 0; j < osFactor_; ++j)
        high[j] = static_cast<float> (coreSample());
    return os_.downsample (high);
}

void WavetableOscillator::processBlock (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample();
}

} // namespace pdhybrid
