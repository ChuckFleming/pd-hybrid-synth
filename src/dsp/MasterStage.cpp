#include "MasterStage.h"
#include <cmath>
#include <algorithm>

namespace pdhybrid {

void MasterStage::setSampleRate (double sampleRateHz) noexcept
{
    sampleRate_ = sampleRateHz > 0.0 ? sampleRateHz : 44100.0;
    // ~5 ms gain smoothing, sample-rate independent.
    gainCoef_ = 1.0 - std::exp (-1.0 / (0.005 * sampleRate_));
    // Limiter envelope: ~1 ms attack catches transients without audible
    // distortion, ~120 ms release lets a chord settle instead of pumping.
    limAtkCoef_ = std::exp (-1.0 / (0.001 * sampleRate_));
    limRelCoef_ = std::exp (-1.0 / (0.120 * sampleRate_));
    // Peak hold: long enough to span a low note's period so the gain does not
    // ripple, short enough to follow real dynamics.
    peakDecayCoef_ = std::exp (-1.0 / (0.050 * sampleRate_));
    osL_.prepare (kOsFactor);
    osR_.prepare (kOsFactor);
    osL_.reset();
    osR_.reset();
}

void MasterStage::reset() noexcept
{
    curGain_ = targetGain_;
    limGain_ = 1.0;
    peakEnv_ = 0.0;
    osL_.reset();
    osR_.reset();
    for (auto& s : bypassL_) s = 0.0f;
    for (auto& s : bypassR_) s = 0.0f;
    bypassPos_ = 0;
}

void MasterStage::setGainDb (double db) noexcept
{
    targetGain_ = std::pow (10.0, db / 20.0);
}

void MasterStage::setThreshold (double linear) noexcept
{
    threshold_ = linear < 0.05 ? 0.05 : (linear > 0.99 ? 0.99 : linear);
}

double MasterStage::limiterGain (double peak) noexcept
{
    // Peak follower with instant attack and a slow decay. Deriving the target
    // from a raw per-sample magnitude instead lets the gain ripple at the
    // waveform's own period -- it releases upward through every zero crossing
    // and has to re-attack at every crest, so crests escape between attacks.
    // Holding the peak keeps the gain steady across a cycle.
    peakEnv_ = std::max (peak, peakEnv_ * peakDecayCoef_);

    // Gain that would put that peak exactly on the threshold. Above it we want
    // reduction; below it, unity -- so the stage stays bit-transparent for any
    // signal that never reaches the ceiling.
    const double target = (peakEnv_ > threshold_) ? threshold_ / peakEnv_ : 1.0;

    // Attack when the required gain drops (get out of the way quickly), release
    // when it rises (come back gently).
    const double coef = (target < limGain_) ? limAtkCoef_ : limRelCoef_;
    limGain_ = target + coef * (limGain_ - target);
    return limGain_;
}

double MasterStage::gainReductionDb() const noexcept
{
    return 20.0 * std::log10 (limGain_ > 1.0e-6 ? limGain_ : 1.0e-6);
}

double MasterStage::softClip (double x) const noexcept
{
    if (! limiterOn_)
        return x;

    const double a = std::fabs (x);
    if (a <= threshold_)
        return x;

    // Asymptote just under full scale rather than exactly at it: the knee runs
    // 4x oversampled, and the downsampling filter rings slightly past whatever
    // value it is fed, which was enough to put the odd sample a hair above 1.0.
    constexpr double kCeiling = 0.99;
    const double sign = x < 0.0 ? -1.0 : 1.0;
    const double over = (a - threshold_) / (1.0 - threshold_);
    return sign * (threshold_ + (kCeiling - threshold_) * std::tanh (over));
}

float MasterStage::processSample (float x) noexcept
{
    curGain_ += gainCoef_ * (targetGain_ - curGain_);
    double y = x * curGain_;
    if (limiterOn_)
        y *= limiterGain (std::fabs (y));
    return static_cast<float> (softClip (y));
}

void MasterStage::processStereo (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        curGain_ += gainCoef_ * (targetGain_ - curGain_);
        float gl = static_cast<float> (left[i]  * curGain_);
        float gr = static_cast<float> (right[i] * curGain_);

        if (! limiterOn_)
        {
            // Transparent apart from the delay: the knee and its oversampler are
            // skipped, but the signal still goes through a matching plain delay
            // so the stage's latency is the same either way. Without this the
            // reported latency was wrong in whichever state it was not measured
            // in, and the host's delay compensation with it.
            bypassL_[(std::size_t) bypassPos_] = gl;
            bypassR_[(std::size_t) bypassPos_] = gr;
            bypassPos_ = (bypassPos_ + 1) % (kLatencySamples + 1);
            left[i]  = bypassL_[(std::size_t) bypassPos_];
            right[i] = bypassR_[(std::size_t) bypassPos_];
            continue;
        }

        // Stereo-linked peak limiting, so the image never wanders. This is what
        // turns "several voices summed past the ceiling" into level control
        // rather than the waveshaping distortion the knee alone produced.
        const double g = limiterGain (std::max (std::fabs ((double) gl),
                                                std::fabs ((double) gr)));
        gl = static_cast<float> (gl * g);
        gr = static_cast<float> (gr * g);

        // Apply the tanh knee at 4x so its harmonics don't alias back down.
        float hl[8], hr[8];
        osL_.upsample (gl, hl);
        osR_.upsample (gr, hr);
        for (int j = 0; j < kOsFactor; ++j)
        {
            hl[j] = static_cast<float> (softClip (hl[j]));
            hr[j] = static_cast<float> (softClip (hr[j]));
        }
        left[i]  = osL_.downsample (hl);
        right[i] = osR_.downsample (hr);
    }
}

} // namespace pdhybrid
