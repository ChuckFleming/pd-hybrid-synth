#include "ChordMode.h"

namespace pdhybrid {

int ChordMode::qualityIntervals (int quality, int* out) noexcept
{
    // Index is the semitone offset from the bottom of the quality zone, so the
    // twelve types land on C, C#, D ... B of that octave. -1 pads the shorter
    // (three-note) chords.
    static const signed char kTable[kNumQualities][kMaxChordNotes] = {
        { 0, 4, 7, -1 },   // C   maj
        { 0, 3, 7, -1 },   // C#  min
        { 0, 4, 7, 10 },   // D   7
        { 0, 3, 7, 10 },   // D#  m7
        { 0, 4, 7, 11 },   // E   maj7
        { 0, 4, 7,  9 },   // F   6
        { 0, 3, 6, 10 },   // F#  m7b5
        { 0, 3, 6,  9 },   // G   dim7
        { 0, 4, 8, -1 },   // G#  aug
        { 0, 2, 7, -1 },   // A   sus2
        { 0, 5, 7, -1 },   // A#  sus4
        { 0, 3, 7,  9 },   // B   m6
    };

    if (quality < 0) quality = 0;
    if (quality >= kNumQualities) quality = kNumQualities - 1;

    int n = 0;
    for (int i = 0; i < kMaxChordNotes; ++i)
        if (kTable[quality][i] >= 0)
            out[n++] = kTable[quality][i];
    return n;
}

void ChordMode::setSplitNote (int midiNote) noexcept
{
    if (midiNote < 36) midiNote = 36;
    if (midiNote > 84) midiNote = 84;
    split_ = midiNote;
}

void ChordMode::setQuality (int index) noexcept
{
    if (index < 0) index = 0;
    if (index >= kNumQualities) index = kNumQualities - 1;
    if (index != quality_) { quality_ = index; dirty_ = true; }
}

void ChordMode::setSpread (double spread01) noexcept
{
    if (spread01 < 0.0) spread01 = 0.0;
    if (spread01 > 1.0) spread01 = 1.0;
    if (spread01 != spread_) { spread_ = spread01; dirty_ = true; }
}

void ChordMode::setOctave (int octaves) noexcept
{
    if (octaves < -2) octaves = -2;
    if (octaves >  2) octaves =  2;
    if (octaves != octave_) { octave_ = octaves; dirty_ = true; }
}

// Placeholder until Task 3: root position, no spread or octave yet.
int ChordMode::buildVoicing (int root, int* out) const noexcept
{
    int iv[kMaxChordNotes];
    const int n = qualityIntervals (quality_, iv);
    for (int i = 0; i < n; ++i)
        out[i] = root + iv[i];
    return n;
}

int ChordMode::emitChange (const int* newNotes, int newCount, int root,
                           Event* out, int maxOut) noexcept
{
    int n = 0;

    // Notes leaving.
    for (int i = 0; i < curCount_; ++i)
    {
        bool kept = false;
        for (int j = 0; j < newCount; ++j)
            if (newNotes[j] == cur_[i]) { kept = true; break; }
        if (! kept && n < maxOut)
            out[n++] = { false, cur_[i], 0.0f, false };
    }

    // Notes entering.
    for (int j = 0; j < newCount; ++j)
    {
        bool held = false;
        for (int i = 0; i < curCount_; ++i)
            if (cur_[i] == newNotes[j]) { held = true; break; }
        if (! held && n < maxOut)
            out[n++] = { true, newNotes[j], heldVel_, false };
    }

    // The bass layer follows the chord root, not the voicing.
    if (curRoot_ != root)
    {
        if (curRoot_ >= 0 && n < maxOut) out[n++] = { false, curRoot_, 0.0f, true };
        if (root    >= 0 && n < maxOut) out[n++] = { true,  root, heldVel_, true };
        curRoot_ = root;
    }

    for (int i = 0; i < newCount; ++i) cur_[i] = newNotes[i];
    curCount_ = newCount;
    return n;
}

int ChordMode::handleNoteOn (int note, float vel, Event* out, int maxOut) noexcept
{
    if (! enabled_)
    {
        if (maxOut < 1) return 0;
        out[0] = { true, note, vel, true };
        return 1;
    }

    if (note >= split_)                       // root zone
    {
        heldRoot_ = note;
        heldVel_  = vel;
        int nv[kMaxChordNotes];
        const int nc = buildVoicing (note, nv);
        return emitChange (nv, nc, note, out, maxOut);
    }

    if (note >= split_ - kQualityZoneSize)    // quality zone: latch, never sounds
    {
        setQuality (note - (split_ - kQualityZoneSize));
        return 0;
    }

    if (maxOut < 1) return 0;                 // below the zones: pass through
    out[0] = { true, note, vel, true };
    return 1;
}

int ChordMode::handleNoteOff (int note, Event* out, int maxOut) noexcept
{
    if (! enabled_)
    {
        if (maxOut < 1) return 0;
        out[0] = { false, note, 0.0f, true };
        return 1;
    }

    if (note >= split_)
    {
        if (note != heldRoot_) return 0;      // a stale root: already replaced
        heldRoot_ = -1;
        return emitChange (nullptr, 0, -1, out, maxOut);
    }

    if (note >= split_ - kQualityZoneSize)
        return 0;                             // quality keys latch; release does nothing

    if (maxOut < 1) return 0;
    out[0] = { false, note, 0.0f, true };
    return 1;
}

} // namespace pdhybrid
