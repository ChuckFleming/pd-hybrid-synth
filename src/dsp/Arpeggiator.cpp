#include "Arpeggiator.h"

namespace pdhybrid {

void Arpeggiator::reset() noexcept
{
    poolCount_    = 0;
    physicalHeld_ = 0;
    seqLen_       = 0;
    seqIndex_     = 0;
    upDownRising_ = true;
    stepPhase_    = 0.0;
    noteSounding_ = false;
    curNote_      = -1;
    offPhase_     = 0.0;
}

void Arpeggiator::clear() noexcept
{
    poolCount_    = 0;
    physicalHeld_ = 0;
    seqLen_       = 0;
}

void Arpeggiator::setLatch (bool latch) noexcept
{
    latch_ = latch;
    if (! latch_ && physicalHeld_ == 0)
        poolCount_ = 0;   // dropping latch with no keys down clears the chord
}

void Arpeggiator::noteOn (int note, float velocity) noexcept
{
    if (latch_ && physicalHeld_ == 0)
        poolCount_ = 0;   // first key after release starts a fresh latched chord

    ++physicalHeld_;

    for (int i = 0; i < poolCount_; ++i)
        if (pool_[static_cast<std::size_t> (i)].note == note)
        {
            pool_[static_cast<std::size_t> (i)].velocity = velocity;
            return;
        }
    if (poolCount_ < static_cast<int> (pool_.size()))
        pool_[static_cast<std::size_t> (poolCount_++)] = { note, velocity };
}

void Arpeggiator::noteOff (int note) noexcept
{
    if (physicalHeld_ > 0)
        --physicalHeld_;

    if (latch_)
        return;   // latched notes stay in the pool until a fresh press

    for (int i = 0; i < poolCount_; ++i)
        if (pool_[static_cast<std::size_t> (i)].note == note)
        {
            for (int j = i; j < poolCount_ - 1; ++j)
                pool_[static_cast<std::size_t> (j)] = pool_[static_cast<std::size_t> (j + 1)];
            --poolCount_;
            return;
        }
}

void Arpeggiator::buildSequence() noexcept
{
    seqLen_ = 0;
    if (poolCount_ == 0)
        return;

    std::array<int, 32>   order { };
    std::array<float, 32> ovel  { };
    const int m = poolCount_;
    for (int i = 0; i < m; ++i)
    {
        order[static_cast<std::size_t> (i)] = pool_[static_cast<std::size_t> (i)].note;
        ovel[static_cast<std::size_t> (i)]  = pool_[static_cast<std::size_t> (i)].velocity;
    }

    if (mode_ != AsPlayed)   // pitch-sorted for Up/Down/UpDown/Random
    {
        for (int i = 1; i < m; ++i)
        {
            const int   kn = order[static_cast<std::size_t> (i)];
            const float kv = ovel[static_cast<std::size_t> (i)];
            int j = i - 1;
            while (j >= 0 && order[static_cast<std::size_t> (j)] > kn)
            {
                order[static_cast<std::size_t> (j + 1)] = order[static_cast<std::size_t> (j)];
                ovel[static_cast<std::size_t> (j + 1)]  = ovel[static_cast<std::size_t> (j)];
                --j;
            }
            order[static_cast<std::size_t> (j + 1)] = kn;
            ovel[static_cast<std::size_t> (j + 1)]  = kv;
        }
    }

    for (int oct = 0; oct < octaves_; ++oct)
        for (int i = 0; i < m && seqLen_ < 128; ++i)
        {
            seq_[static_cast<std::size_t> (seqLen_)]    = order[static_cast<std::size_t> (i)] + 12 * oct;
            seqVel_[static_cast<std::size_t> (seqLen_)] = ovel[static_cast<std::size_t> (i)];
            ++seqLen_;
        }
}

int Arpeggiator::generate (int numSamples, Event* out, int maxOut) noexcept
{
    buildSequence();

    auto nextIndex = [&] () -> int
    {
        if (seqLen_ <= 0) return -1;
        int idx;
        switch (mode_)
        {
            case Down:
                idx = (seqLen_ - 1) - (seqIndex_ % seqLen_);
                break;
            case UpDown:
                if (seqLen_ == 1) { idx = 0; }
                else { const int period = 2 * seqLen_ - 2;
                       const int p = seqIndex_ % period;
                       idx = (p < seqLen_) ? p : (period - p); }
                break;
            case Random:
                rng_ = rng_ * 1664525u + 1013904223u;
                idx = static_cast<int> ((rng_ >> 8) % static_cast<unsigned> (seqLen_));
                break;
            case Up:
            case AsPlayed:
            default:
                idx = seqIndex_ % seqLen_;
                break;
        }
        if (++seqIndex_ >= 1000000) seqIndex_ = seqIndex_ % (seqLen_ > 0 ? seqLen_ : 1);
        return idx;
    };

    int count = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        if (noteSounding_ && offPhase_ <= 0.0)
        {
            if (count < maxOut) out[count++] = { i, false, curNote_, 0.0f };
            noteSounding_ = false;
        }

        if (seqLen_ > 0 && stepPhase_ <= 0.0)
        {
            if (noteSounding_)   // gate ~1.0: close the previous note first
            {
                if (count < maxOut) out[count++] = { i, false, curNote_, 0.0f };
                noteSounding_ = false;
            }
            const int idx = nextIndex();
            if (idx >= 0)
            {
                curNote_ = seq_[static_cast<std::size_t> (idx)];
                const float vel = seqVel_[static_cast<std::size_t> (idx)];
                if (count < maxOut) out[count++] = { i, true, curNote_, vel };
                noteSounding_ = true;
                offPhase_ = stepSamples_ * gate_;
            }
            stepPhase_ += stepSamples_;
        }

        if (seqLen_ > 0) stepPhase_ -= 1.0;
        else             stepPhase_ = 0.0;   // ready to fire the instant a note arrives
        if (noteSounding_) offPhase_ -= 1.0;

        if (count >= maxOut) break;
    }
    return count;
}

} // namespace pdhybrid
