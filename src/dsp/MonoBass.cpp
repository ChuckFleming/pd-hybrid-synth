#include "MonoBass.h"
#include "SynthParams.h"   // tuningCentsOffset
#include <algorithm>
#include <cmath>

namespace pdhybrid {

double MonoBass::noteHz (int note) const noexcept
{
    const int n = note + transpose_;
    const double etHz = masterTuneHz_ * std::pow (2.0, (n - 69) / 12.0);
    return etHz * std::pow (2.0, tuningCentsOffset (tuningScale_, n) / 1200.0);
}

void MonoBass::setSampleRate (double sampleRateHz) noexcept
{
    sampleRate_ = sampleRateHz;
    main_.setSampleRate (sampleRateHz);
    sub_.setSampleRate (sampleRateHz);
    env_.setSampleRate (sampleRateHz);
    held_.reserve (128);   // avoid audio-thread reallocation on note-on
    setHarmonics (harmonics_);
}

void MonoBass::reset() noexcept
{
    main_.reset();
    sub_.reset();
    env_.reset();
    held_.clear();
    curNote_  = -1;
    targetHz_ = 55.0;
    curLogHz_ = std::log (targetHz_);
}

void MonoBass::setHarmonics (double amount01) noexcept
{
    harmonics_ = std::clamp (amount01, 0.0, 1.0);
    fold_.setCurve (ShaperCurve::Wavefold);
    fold_.setDrive (1.0 + harmonics_ * 3.0);
    sat_.setCurve (ShaperCurve::Tanh);
    sat_.setDrive (1.0 + harmonics_ * 3.0);
}

void MonoBass::retune() noexcept
{
    tuneMul_ = std::pow (2.0, octave_ + tuneCents_ / 1200.0);
}

double MonoBass::glideCoef() const noexcept
{
    if (glideTime_ <= 0.0)
        return 1.0;
    return 1.0 - std::exp (-1.0 / (glideTime_ * sampleRate_));
}

int MonoBass::selectNote() const noexcept
{
    if (held_.empty())
        return -1;

    switch (priority_)
    {
        case BassPriority::Top:    return *std::max_element (held_.begin(), held_.end());
        case BassPriority::Bottom: return *std::min_element (held_.begin(), held_.end());
        case BassPriority::Last:
        default:                   return held_.back();
    }
}

void MonoBass::updateTarget (bool allowRetrigger) noexcept
{
    const int sel = selectNote();

    if (sel < 0)                        // nothing held -> release
    {
        if (curNote_ >= 0)
        {
            env_.noteOff();
            curNote_ = -1;
        }
        return;
    }

    const bool silent = (curNote_ < 0) || ! env_.isActive();

    if (sel != curNote_ || silent)
    {
        targetHz_ = noteHz (sel);
        if (silent || glideTime_ <= 0.0)      // jump when starting fresh
            curLogHz_ = std::log (targetHz_);
        curNote_ = sel;

        if (silent && allowRetrigger)
        {
            velGain_ = pendingVel_;
            env_.noteOn();
        }
    }
}

void MonoBass::noteOn (int note, float velocity) noexcept
{
    held_.erase (std::remove (held_.begin(), held_.end(), note), held_.end());
    held_.push_back (note);
    pendingVel_ = velocity;
    updateTarget (true);
}

void MonoBass::noteOff (int note) noexcept
{
    held_.erase (std::remove (held_.begin(), held_.end(), note), held_.end());
    updateTarget (false);
}

void MonoBass::allNotesOff() noexcept
{
    held_.clear();
    updateTarget (false);
}

void MonoBass::renderBlock (float* mono, int numSamples) noexcept
{
    if (! enabled_ || ! env_.isActive())
        return;

    const double coef      = glideCoef();
    const double tgtLog     = std::log (targetHz_);
    const double chunkDecay = 1.0 - coef;         // per-sample one-pole retention
    constexpr int kCtrl = 32;

    for (int done = 0; done < numSamples; )
    {
        const int chunk = std::min (kCtrl, numSamples - done);

        // Advance the glide across the whole chunk in closed form, then set the
        // oscillator frequencies once (per-sample setFrequency was the cost).
        curLogHz_ = tgtLog + (curLogHz_ - tgtLog) * std::pow (chunkDecay, chunk);
        const double hz = std::exp (curLogHz_) * tuneMul_;
        main_.setFrequency (hz);
        sub_.setFrequency (hz * 0.5);

        for (int i = 0; i < chunk; ++i)
        {
            double s = main_.processSample() + 0.5 * sub_.processSample();
            s *= 0.6;                                   // headroom before shaping
            s = fold_.process (static_cast<float> (s));
            s = sat_.process (static_cast<float> (s));

            const double e = env_.processSample();
            mono[done + i] += static_cast<float> (s * e * velGain_ * level_);
        }

        done += chunk;
    }
}

} // namespace pdhybrid
