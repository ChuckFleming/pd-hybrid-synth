#pragma once

namespace pdhybrid {

/**
    Reverse chord / note lookup: turns a set of sounding MIDI notes into a name
    a player recognises -- "C4", "C-G 5th", "Cmaj7", "Dm7/A".

    Pure C++, no JUCE, no allocation and no <string>: names are written into a
    caller-supplied buffer. Everything is static, so the editor can call it
    straight from paint() without owning an instance.

    Naming is a judgement call, not a fact -- the same pitch classes spell more
    than one chord (C-Eb-Gb-A is dim7 from any of its four notes). The rules
    used here, in order:
      - an exact match against the formula table wins over a partial one;
      - among exact matches, the one rooted on the lowest sounding note wins,
        because that is the one the ear hears;
      - failing that, the earlier entry in the table wins, and the table is
        ordered commonest-first.
*/
class ChordNamer
{
public:
    /** Longest name this can produce, including the terminator. */
    static constexpr int kMaxName = 32;

    /** Names one note with its octave, e.g. "C#4". Middle C (MIDI 60) is C4.
        Returns the length written. */
    static int noteName (int midiNote, char* out, int maxOut) noexcept;

    /** Names the set of sounding notes. `notes` need not be sorted and may
        contain duplicates. Writes a NUL-terminated name and returns its length;
        writes an empty string for an empty set. */
    static int name (const int* notes, int count, char* out, int maxOut) noexcept;

private:
    /** Pitch-class set as a 12-bit mask, rotated so the root is bit 0. */
    static int rotate (int mask, int semitones) noexcept;
};

} // namespace pdhybrid
