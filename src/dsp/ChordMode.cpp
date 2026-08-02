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

} // namespace pdhybrid
