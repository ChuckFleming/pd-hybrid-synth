#include "SynthEngine.h"
#include <limits>

namespace pdhybrid {

void SynthEngine::setSampleRate (double sampleRate)
{
    sampleRate_ = sampleRate;
    for (auto& v : voices_)
        v.prepare (sampleRate);
    for (int i = 0; i < kMaxVoices; ++i)
    {
        voiceHeld_[i]     = false;
        voiceBend_[i]     = 0.0;
        voicePressure_[i] = 1.0;
        voiceTimbre_[i]   = 0.0;
        voiceNote_[i]     = -1;
        voiceId_[i]       = 0;
    }
}

int SynthEngine::allocateVoice() noexcept
{
    // 1) A free (finished) voice.
    for (int i = 0; i < kMaxVoices; ++i)
        if (! voices_[i].isActive())
            return i;

    // 2) Steal the oldest voice that is no longer held (releasing).
    int best = -1;
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (int i = 0; i < kMaxVoices; ++i)
        if (! voiceHeld_[i] && voiceAge_[i] < oldest) { oldest = voiceAge_[i]; best = i; }
    if (best >= 0)
        return best;

    // 3) Everything is held: steal the oldest overall.
    oldest = std::numeric_limits<std::uint64_t>::max();
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceAge_[i] < oldest) { oldest = voiceAge_[i]; best = i; }
    return best;
}

void SynthEngine::noteOn (int note, float velocity, int noteId)
{
    const int v = allocateVoice();

    voiceNote_[v]     = note;
    voiceId_[v]       = noteId;
    voiceHeld_[v]     = true;
    voiceAge_[v]      = ageCounter_++;
    voiceBend_[v]     = 0.0;
    voicePressure_[v] = 1.0;
    voiceTimbre_[v]   = 0.0;

    voices_[v].setParams (params_);
    voices_[v].start (note, velocity);
}

void SynthEngine::noteOff (int note, int noteId)
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceHeld_[i] && voiceNote_[i] == note && voiceId_[i] == noteId)
        {
            voices_[i].release();
            voiceHeld_[i] = false;
        }
}

void SynthEngine::allNotesOff()
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceHeld_[i])
        {
            voices_[i].release();
            voiceHeld_[i] = false;
        }
}

void SynthEngine::setNotePitchBend (int noteId, double semitones) noexcept
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceId_[i] == noteId)
            voiceBend_[i] = semitones;
}

void SynthEngine::setNotePressure (int noteId, double pressure01) noexcept
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceId_[i] == noteId)
            voicePressure_[i] = pressure01;
}

void SynthEngine::setNoteTimbre (int noteId, double timbre01) noexcept
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceId_[i] == noteId)
            voiceTimbre_[i] = timbre01;
}

void SynthEngine::renderBlock (float* out, int numSamples)
{
    for (int j = 0; j < numSamples; ++j)
        out[j] = 0.0f;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (! voices_[i].isActive())
            continue;

        voices_[i].setParams (params_);
        voices_[i].setPitchBendSemitones (voiceBend_[i]);
        voices_[i].setPressure (voicePressure_[i]);
        voices_[i].setTimbre (voiceTimbre_[i]);

        for (int j = 0; j < numSamples; ++j)
            out[j] += voices_[i].render();
    }
}

int SynthEngine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices_)
        if (v.isActive())
            ++n;
    return n;
}

} // namespace pdhybrid
