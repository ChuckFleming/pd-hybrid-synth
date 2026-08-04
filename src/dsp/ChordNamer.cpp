#include "ChordNamer.h"

namespace pdhybrid {

namespace {

// Sharps throughout. Spelling a chord properly needs a key, which the plugin
// does not have -- Db major and C# major are the same keys on the keyboard, and
// guessing wrong is worse than being consistent.
const char* const kPitchNames[12] = { "C", "C#", "D", "D#", "E", "F",
                                      "F#", "G", "G#", "A", "A#", "B" };

struct Formula { int mask; const char* suffix; };

// Ordered commonest-first: ties are broken by table order, so triads must come
// before the extensions that contain them.
const Formula kFormulas[] = {
    { 0b000010010001, "" },        // 0 4 7      major
    { 0b000010001001, "m" },       // 0 3 7      minor
    { 0b000010000101, "sus2" },    // 0 2 7
    { 0b000010100001, "sus4" },    // 0 5 7
    { 0b000001001001, "dim" },     // 0 3 6
    { 0b000100010001, "aug" },     // 0 4 8
    { 0b010010010001, "7" },       // 0 4 7 10   dominant 7
    { 0b100010010001, "maj7" },    // 0 4 7 11
    { 0b010010001001, "m7" },      // 0 3 7 10
    { 0b100010001001, "mMaj7" },   // 0 3 7 11
    { 0b001010010001, "6" },       // 0 4 7 9
    { 0b001010001001, "m6" },      // 0 3 7 9
    { 0b010001001001, "m7b5" },    // 0 3 6 10   half-diminished
    { 0b001001001001, "dim7" },    // 0 3 6 9
    { 0b010010100001, "7sus4" },   // 0 5 7 10
    { 0b000010010101, "add9" },    // 0 2 4 7
    { 0b010010010101, "9" },       // 0 2 4 7 10
    { 0b100010010101, "maj9" },    // 0 2 4 7 11
    { 0b010010001101, "m9" },      // 0 2 3 7 10
};
constexpr int kNumFormulas = static_cast<int> (sizeof (kFormulas) / sizeof (kFormulas[0]));

// Intervals for the two-note case, indexed by semitone distance (0..11).
const char* const kIntervalNames[12] = {
    "oct", "m2", "M2", "m3", "M3", "P4", "tritone", "P5", "m6", "M6", "m7", "M7"
};

/** Appends `src` to `out`, respecting `maxOut` (which includes the NUL).
    Returns the new length. */
int append (char* out, int len, int maxOut, const char* src) noexcept
{
    if (maxOut <= 0) return len;
    while (*src != '\0' && len < maxOut - 1)
        out[len++] = *src++;
    out[len] = '\0';
    return len;
}

int appendInt (char* out, int len, int maxOut, int value) noexcept
{
    char tmp[8];
    int n = 0;
    bool neg = value < 0;
    if (neg) value = -value;
    do { tmp[n++] = static_cast<char> ('0' + value % 10); value /= 10; }
    while (value > 0 && n < 7);
    if (neg && n < 7) tmp[n++] = '-';

    for (int i = n - 1; i >= 0; --i)
        if (len < maxOut - 1) out[len++] = tmp[i];
    if (maxOut > 0) out[len] = '\0';
    return len;
}

int pitchClass (int midiNote) noexcept { return ((midiNote % 12) + 12) % 12; }

} // namespace

int ChordNamer::rotate (int mask, int semitones) noexcept
{
    // Rotate right within 12 bits, so the given pitch class lands on bit 0.
    const int s = ((semitones % 12) + 12) % 12;
    return ((mask >> s) | (mask << (12 - s))) & 0xfff;
}

int ChordNamer::noteName (int midiNote, char* out, int maxOut) noexcept
{
    if (out == nullptr || maxOut <= 0) return 0;
    out[0] = '\0';
    if (midiNote < 0 || midiNote > 127) return 0;

    int len = append (out, 0, maxOut, kPitchNames[pitchClass (midiNote)]);
    // Middle C (60) is C4, the convention JUCE and most DAWs display.
    return appendInt (out, len, maxOut, midiNote / 12 - 1);
}

int ChordNamer::name (const int* notes, int count, char* out, int maxOut) noexcept
{
    if (out == nullptr || maxOut <= 0) return 0;
    out[0] = '\0';
    if (notes == nullptr || count <= 0) return 0;

    // Lowest sounding note: it is the bass, and it breaks ties between roots.
    int lowest = 128;
    int mask   = 0;
    for (int i = 0; i < count; ++i)
    {
        const int n = notes[i];
        if (n < 0 || n > 127) continue;
        if (n < lowest) lowest = n;
        mask |= 1 << pitchClass (n);
    }
    if (lowest > 127) return 0;

    // Count distinct pitch classes -- an octave-doubled note is still one note.
    int distinct = 0;
    for (int b = 0; b < 12; ++b)
        if (mask & (1 << b)) ++distinct;

    if (distinct == 1)
        return noteName (lowest, out, maxOut);

    if (distinct == 2)
    {
        // Two notes are an interval, not a chord: name both ends and the gap.
        int other = -1;
        for (int b = 0; b < 12; ++b)
            if ((mask & (1 << b)) && b != pitchClass (lowest)) { other = b; break; }

        int len = append (out, 0, maxOut, kPitchNames[pitchClass (lowest)]);
        len = append (out, len, maxOut, "-");
        len = append (out, len, maxOut, kPitchNames[other]);
        len = append (out, len, maxOut, " ");
        return append (out, len, maxOut,
                       kIntervalNames[((other - pitchClass (lowest)) % 12 + 12) % 12]);
    }

    // Three or more: try every sounding pitch class as the root and keep the
    // best match. One set of pitch classes often spells several chords -- C E G
    // A is both C6 and Am7 -- so the choice needs a stated rule rather than
    // whichever root happens to be scanned first:
    //   1. a root that is also the bass beats one that is not, because that is
    //      the chord the ear hears;
    //   2. otherwise the commonest formula wins, which is table order.
    const int bassPc = pitchClass (lowest);
    int bestRoot = -1, bestFormula = -1, bestScore = 0x7fffffff;

    for (int root = 0; root < 12; ++root)
    {
        if (! (mask & (1 << root))) continue;      // the root must be sounding

        const int rotated = rotate (mask, root);
        for (int f = 0; f < kNumFormulas; ++f)
        {
            if (kFormulas[f].mask != rotated) continue;

            const int score = (root == bassPc ? 0 : 1000) + f;
            if (score < bestScore)
            {
                bestScore = score; bestRoot = root; bestFormula = f;
            }
            break;                                 // one formula per root
        }
    }

    if (bestRoot < 0)
    {
        // No formula matches. Spelling out the notes is more use than "?" --
        // the player can still see what is sounding.
        int len = 0;
        for (int b = 0; b < 12; ++b)
            if (mask & (1 << b))
            {
                if (len > 0) len = append (out, len, maxOut, " ");
                len = append (out, len, maxOut, kPitchNames[b]);
            }
        return len;
    }

    int len = append (out, 0, maxOut, kPitchNames[bestRoot]);
    len = append (out, len, maxOut, kFormulas[bestFormula].suffix);
    if (bassPc != bestRoot)                        // an inversion: name the bass
    {
        len = append (out, len, maxOut, "/");
        len = append (out, len, maxOut, kPitchNames[bassPc]);
    }
    return len;
}

} // namespace pdhybrid
