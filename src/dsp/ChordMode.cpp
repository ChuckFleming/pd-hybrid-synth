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

namespace {

// Insertion sort: at most four elements, and it keeps the class allocation-free.
void sortAscending (int* v, int n) noexcept
{
    for (int i = 1; i < n; ++i)
    {
        const int key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; --j; }
        v[j + 1] = key;
    }
}

} // namespace

int ChordMode::buildVoicing (int root, int* out) noexcept
{
    int iv[kMaxChordNotes];
    int n = qualityIntervals (quality_, iv);

    // Shell: drop the fifth, but only where something useful is left -- a triad
    // reduced to root and third is not a chord worth playing.
    if (voicing_ == Shell && n >= 4)
    {
        int m = 0;
        for (int i = 0; i < n; ++i)
            if (iv[i] != 7) iv[m++] = iv[i];
        n = m;
    }

    if (voicing_ == Closed || voicing_ == Drop2)
    {
        // Fixed register, no history: the octave starting at 54, centred on C4.
        // chordOctave is deliberately not used here -- it is applied once at the
        // end, for every mode.
        constexpr int kBase = 54;
        for (int i = 0; i < n; ++i)
        {
            const int pc = ((root + iv[i]) % 12 + 12) % 12;
            out[i] = kBase + ((pc - kBase) % 12 + 12) % 12;
        }
        sortAscending (out, n);

        if (voicing_ == Drop2 && n >= 2)
        {
            out[n - 2] -= 12;
            sortAscending (out, n);
        }
    }
    // Root position, and also the very *first* chord of a voice-led sequence:
    // with no history there is nothing to lead from, and centring on the played
    // root would place some chord tones below it -- press C and the fifth lands
    // under it, so the key you pressed is not the bass. Starting in root
    // position makes the played key audibly the root; every chord after leads
    // from it.
    else if (voicing_ == RootPosition || ! hasHistory_)
    {
        for (int i = 0; i < n; ++i)
            out[i] = root + iv[i];
    }
    else
    {
        // Closest-position placement: put every pitch class in whichever octave
        // sits nearest the previous voicing's centre. Common tones fall out of
        // this for free -- a pitch class already sounding near the centre
        // resolves to the same absolute note, so no explicit common-tone logic
        // is needed.
        for (int i = 0; i < n; ++i)
        {
            const int pc = ((root + iv[i]) % 12 + 12) % 12;
            // Round half away from zero without <cmath>: the operand is small.
            const double k   = (lastCentre_ - pc) / 12.0;
            const int    oct = static_cast<int> (k >= 0.0 ? k + 0.5 : k - 0.5);
            out[i] = pc + 12 * oct;
        }
        sortAscending (out, n);
    }

    // ---- shared tail: applies to every voicing mode ----

    // Spread: lift the k voices above the bass by an octave. k = 0 leaves the
    // voicing closed; k = n-1 raises everything except the bass.
    {
        const double kf = spread_ * (n - 1);
        const int    k  = static_cast<int> (kf + 0.5);
        for (int i = 1; i <= k && i < n; ++i)
            out[i] += 12;
        sortAscending (out, n);
    }

    for (int i = 0; i < n; ++i)
        out[i] += 12 * octave_;

    // Fold anything that fell off either end back by whole octaves, so the
    // chord keeps its pitch classes rather than being clipped against a wall.
    for (int i = 0; i < n; ++i)
    {
        while (out[i] < 0)   out[i] += 12;
        while (out[i] > 127) out[i] -= 12;
    }
    sortAscending (out, n);

    // Folding can collide two voices onto one note; drop the duplicate.
    int m = 0;
    for (int i = 0; i < n; ++i)
        if (i == 0 || out[i] != out[i - 1])
            out[m++] = out[i];
    n = m;

    // Carry the centre forward for the next chord.
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += out[i];
    lastCentre_ = n > 0 ? sum / n : lastCentre_;
    hasHistory_ = true;

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

int ChordMode::flush (Event* out, int maxOut) noexcept
{
    heldRoot_ = -1;
    dirty_    = false;
    // A flush means the keyboard layout changed under the player, so there is
    // no progression to keep leading from: start the next chord fresh.
    hasHistory_ = false;
    return emitChange (nullptr, 0, -1, out, maxOut);
}

int ChordMode::voicedNotes (int* out, int maxOut) const noexcept
{
    int n = 0;
    for (int i = 0; i < curCount_ && n < maxOut; ++i)
        out[n++] = cur_[i];
    return n;
}

int ChordMode::revoice (Event* out, int maxOut) noexcept
{
    dirty_ = false;
    if (heldRoot_ < 0)
        return 0;

    int nv[kMaxChordNotes];
    const int nc = buildVoicing (heldRoot_, nv);
    return emitChange (nv, nc, heldRoot_, out, maxOut);
}

int ChordMode::refresh (Event* out, int maxOut) noexcept
{
    if (! dirty_)
        return 0;
    return revoice (out, maxOut);
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
        const int q = note - (split_ - kQualityZoneSize);
        if (q != quality_)
        {
            setQuality (q);
            // Tell the caller to push this back into its parameter: the
            // parameter is the latch, and it is re-applied every block.
            qualityFromKey_ = true;
        }
        // A quality key re-voices whatever is sounding, right now. `refresh`
        // covers the same change arriving from host automation instead.
        return revoice (out, maxOut);
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
