#include "SynthEngine.h"
#include <cmath>
#include <limits>

namespace pdhybrid {

double SynthEngine::noteHz (int note) const noexcept
{
    const int n = note + params_.transpose;
    const double etHz = params_.masterTuneHz * std::pow (2.0, (n - 69) / 12.0);
    return etHz * std::pow (2.0, tuningCentsOffset (params_.tuningScale, n) / 1200.0);
}

void SynthEngine::setSampleRate (double sampleRate)
{
    sampleRate_ = sampleRate;
    for (auto& v : voices_)
        v.prepare (sampleRate);
    for (int i = 0; i < kMaxVoices; ++i)
    {
        voiceHeld_[i]      = false;
        voiceSustained_[i] = false;
        voiceBend_[i]      = 0.0;
        voicePressure_[i]  = 1.0;
        voiceTimbre_[i]    = 0.0;
        voiceNote_[i]      = -1;
        voiceId_[i]        = 0;
    }
    heldStack_.reserve (128);   // avoid audio-thread reallocation on note-on
    heldStack_.clear();
    monoNote_    = -1;
    monoVoiceN_  = 0;
    sustainDown_ = false;
}

int SynthEngine::allocateVoice (int limit) noexcept
{
    if (limit < 1)            limit = 1;
    if (limit > kMaxVoices)   limit = kMaxVoices;

    // 1) A free (finished) voice.
    for (int i = 0; i < limit; ++i)
        if (! voices_[i].isActive())
            return i;

    // 2) Steal by tier (releasing < pedal-sustained < key-held), then by policy:
    //    Oldest = lowest age, Quietest = lowest envelope level.
    int    best   = 0;
    int    bestTier = 99;
    double bestMetric = std::numeric_limits<double>::max();
    for (int i = 0; i < limit; ++i)
    {
        const int    tier   = voiceHeld_[i] ? 2 : (voiceSustained_[i] ? 1 : 0);
        const double metric = (params_.stealPolicy == 1) ? voices_[i].envLevel()
                                                         : static_cast<double> (voiceAge_[i]);
        if (tier < bestTier || (tier == bestTier && metric < bestMetric))
        {
            bestTier   = tier;
            bestMetric = metric;
            best       = i;
        }
    }
    return best;
}

void SynthEngine::startVoice (int v, int note, float velocity, int noteId,
                              double spread, double fromHz, double glideSamples) noexcept
{
    voiceNote_[v]      = note;
    voiceId_[v]        = noteId;
    voiceHeld_[v]      = true;
    voiceSustained_[v] = false;
    voiceAge_[v]       = ageCounter_++;
    voiceBend_[v]      = 0.0;
    voicePressure_[v]  = 1.0;
    voiceTimbre_[v]    = 0.0;

    voices_[v].setParams (params_);
    voices_[v].setUnison (params_.unisonDetune * spread, params_.unisonWidth * spread);
    voices_[v].start (note, velocity, fromHz, glideSamples);
}

void SynthEngine::noteOn (int note, float velocity, int noteId)
{
    if (params_.voiceMode == 0) polyNoteOn (note, velocity, noteId);
    else                        monoNoteOn (note, velocity, noteId);
}

void SynthEngine::noteOff (int note, int noteId)
{
    if (params_.voiceMode == 0) polyNoteOff (note, noteId);
    else                        monoNoteOff (note, noteId);
}

void SynthEngine::polyNoteOn (int note, float velocity, int noteId)
{
    // Count currently-held voices before allocating (legato glide condition).
    int heldBefore = 0;
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceHeld_[i]) ++heldBefore;

    const bool wantGlide = (params_.glideMode == GlideMode::Always)
                        || (params_.glideMode == GlideMode::Legato && heldBefore > 0);
    double fromHz = 0.0, glideSamples = 0.0;
    if (wantGlide && lastNoteHz_ > 0.0)
    {
        fromHz       = lastNoteHz_;
        glideSamples = params_.glideTime * sampleRate_;
    }

    int lim = params_.polyphony;
    if (lim < 1) lim = 1; else if (lim > kMaxVoices) lim = kMaxVoices;

    // Unison: stack N detuned sub-voices under the same note id (clamped so the
    // stack fits inside the active polyphony).
    int n = params_.unisonVoices < 1 ? 1 : (params_.unisonVoices > 6 ? 6 : params_.unisonVoices);
    if (n > lim) n = lim;

    for (int k = 0; k < n; ++k)
    {
        const int    v      = allocateVoice (lim);
        const double spread = (n == 1) ? 0.0 : (2.0 * k / (n - 1) - 1.0);   // -1..1
        startVoice (v, note, velocity, noteId, spread, fromHz, glideSamples);
    }

    lastNoteHz_ = noteHz (note);
}

void SynthEngine::polyNoteOff (int note, int noteId)
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceHeld_[i] && voiceNote_[i] == note && voiceId_[i] == noteId)
        {
            voiceHeld_[i] = false;
            if (sustainDown_)
                voiceSustained_[i] = true;   // pedal keeps it sounding
            else
                voices_[i].release();
        }
}

void SynthEngine::removeHeld (int note, int noteId) noexcept
{
    for (auto it = heldStack_.begin(); it != heldStack_.end(); ++it)
        if (it->note == note && it->noteId == noteId)
        {
            heldStack_.erase (it);
            return;
        }
}

const SynthEngine::HeldNote* SynthEngine::selectHeld() const noexcept
{
    if (heldStack_.empty())
        return nullptr;

    switch (params_.notePriority)
    {
        case 1:   // Top: highest note wins
        {
            const HeldNote* b = &heldStack_.front();
            for (const auto& h : heldStack_) if (h.note > b->note) b = &h;
            return b;
        }
        case 2:   // Bottom: lowest note wins
        {
            const HeldNote* b = &heldStack_.front();
            for (const auto& h : heldStack_) if (h.note < b->note) b = &h;
            return b;
        }
        case 0:
        default:
            return &heldStack_.back();   // Last: most recent press
    }
}

void SynthEngine::monoNoteOn (int note, float velocity, int noteId)
{
    removeHeld (note, noteId);                        // de-dupe repeated keys
    heldStack_.push_back ({ note, velocity, noteId });
    updateMono();
}

void SynthEngine::monoNoteOff (int note, int noteId)
{
    removeHeld (note, noteId);
    updateMono();
}

void SynthEngine::updateMono()
{
    const HeldNote* sel = selectHeld();

    // Nothing held: release the mono voices (unless the pedal holds them).
    if (sel == nullptr)
    {
        if (! sustainDown_ && monoNote_ >= 0)
        {
            for (int k = 0; k < monoVoiceN_; ++k)
            {
                const int v = monoVoices_[k];
                voiceHeld_[v] = false;
                voices_[v].release();
            }
            monoNote_   = -1;
            monoVoiceN_ = 0;
        }
        return;
    }

    // Are the mono voices currently sounding?
    bool sounding = (monoNote_ >= 0) && (monoVoiceN_ > 0);
    if (sounding)
    {
        bool anyActive = false;
        for (int k = 0; k < monoVoiceN_; ++k)
            if (voices_[monoVoices_[k]].isActive()) anyActive = true;
        sounding = anyActive;
    }

    const bool unison = (params_.voiceMode == 3);   // Unison Legato stacks voices
    int n = unison ? (params_.unisonVoices < 1 ? 1 : (params_.unisonVoices > 6 ? 6 : params_.unisonVoices))
                   : 1;
    int lim = params_.polyphony;
    if (lim < 1) lim = 1; else if (lim > kMaxVoices) lim = kMaxVoices;
    if (n > lim) n = lim;

    if (! sounding)
    {
        // Fresh start: allocate the stack and retrigger the envelopes.
        const bool wantGlide = (params_.glideMode == GlideMode::Always);
        double fromHz = 0.0, glideSamples = 0.0;
        if (wantGlide && lastNoteHz_ > 0.0)
        {
            fromHz       = lastNoteHz_;
            glideSamples = params_.glideTime * sampleRate_;
        }

        monoVoiceN_ = n;
        for (int k = 0; k < n; ++k)
        {
            const int    v      = allocateVoice (lim);
            monoVoices_[k]      = v;
            const double spread = (n == 1) ? 0.0 : (2.0 * k / (n - 1) - 1.0);
            startVoice (v, sel->note, sel->velocity, sel->noteId, spread, fromHz, glideSamples);
        }
        monoNote_   = sel->note;
        lastNoteHz_ = noteHz (sel->note);
        return;
    }

    // Already sounding: on a note change, glide (legato) or retrigger.
    if (sel->note != monoNote_)
    {
        // Mono honours the retrigger toggle; Legato / Unison-Legato are always legato.
        const bool   retrig       = (params_.voiceMode == 1) ? params_.monoRetrigger : false;
        const bool   wantGlide    = (params_.glideMode != GlideMode::Off);
        const double fromHz       = noteHz (monoNote_);
        const double glideSamples = wantGlide ? params_.glideTime * sampleRate_ : 0.0;

        for (int k = 0; k < monoVoiceN_; ++k)
        {
            const int v = monoVoices_[k];
            voiceNote_[v]      = sel->note;
            voiceId_[v]        = sel->noteId;
            voiceHeld_[v]      = true;
            voiceSustained_[v] = false;
            if (retrig)
                voices_[v].start (sel->note, sel->velocity, wantGlide ? fromHz : 0.0, glideSamples);
            else
                voices_[v].changeNote (sel->note, wantGlide ? fromHz : 0.0, glideSamples);
        }
        monoNote_   = sel->note;
        lastNoteHz_ = noteHz (sel->note);
    }
}

void SynthEngine::setSustain (bool down) noexcept
{
    if (down == sustainDown_)
        return;
    sustainDown_ = down;

    if (! down)
    {
        // Release voices that were held only by the pedal (poly mode).
        for (int i = 0; i < kMaxVoices; ++i)
            if (voiceSustained_[i] && ! voiceHeld_[i])
            {
                voices_[i].release();
                voiceSustained_[i] = false;
            }
        // Mono mode: let go of a note whose key is already up.
        if (params_.voiceMode != 0)
            updateMono();
    }
}

void SynthEngine::allNotesOff()
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceHeld_[i] || voiceSustained_[i])
        {
            voices_[i].release();
            voiceHeld_[i]      = false;
            voiceSustained_[i] = false;
        }
    heldStack_.clear();
    monoNote_   = -1;
    monoVoiceN_ = 0;
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

void SynthEngine::renderBlock (float* left, float* right, int numSamples)
{
    for (int j = 0; j < numSamples; ++j)
    {
        left[j]  = 0.0f;
        right[j] = 0.0f;
    }

    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (! voices_[i].isActive())
            continue;

        voices_[i].setParams (params_);
        voices_[i].setPitchBendSemitones (voiceBend_[i]);
        voices_[i].setPressure (voicePressure_[i]);
        voices_[i].setTimbre (voiceTimbre_[i]);
        voices_[i].setModWheel (modWheel_);
        voices_[i].renderBlock (left, right, numSamples);
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
