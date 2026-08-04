#include <catch2/catch_test_macros.hpp>

#include "dsp/ChordNamer.h"

#include <cstring>
#include <initializer_list>
#include <string>

using namespace pdhybrid;

namespace {

std::string nameOf (std::initializer_list<int> notes)
{
    char buf[ChordNamer::kMaxName] = { 0 };
    ChordNamer::name (notes.begin(), static_cast<int> (notes.size()),
                      buf, ChordNamer::kMaxName);
    return buf;
}

std::string noteOf (int midi)
{
    char buf[ChordNamer::kMaxName] = { 0 };
    ChordNamer::noteName (midi, buf, ChordNamer::kMaxName);
    return buf;
}

} // namespace

TEST_CASE ("Single notes are named with their octave", "[namer]")
{
    REQUIRE (noteOf (60) == "C4");     // middle C
    REQUIRE (noteOf (61) == "C#4");
    REQUIRE (noteOf (59) == "B3");
    REQUIRE (noteOf (0)  == "C-1");
    REQUIRE (noteOf (127) == "G9");

    REQUIRE (nameOf ({ 60 }) == "C4");
    // An octave is still one note.
    REQUIRE (nameOf ({ 60, 72 }) == "C4");
}

TEST_CASE ("Two notes are named as an interval", "[namer]")
{
    REQUIRE (nameOf ({ 60, 67 }) == "C-G P5");
    REQUIRE (nameOf ({ 60, 63 }) == "C-D# m3");
    REQUIRE (nameOf ({ 60, 66 }) == "C-F# tritone");
}

TEST_CASE ("Triads are named", "[namer]")
{
    REQUIRE (nameOf ({ 60, 64, 67 }) == "C");
    REQUIRE (nameOf ({ 60, 63, 67 }) == "Cm");
    REQUIRE (nameOf ({ 60, 63, 66 }) == "Cdim");
    REQUIRE (nameOf ({ 60, 64, 68 }) == "Caug");
    REQUIRE (nameOf ({ 60, 62, 67 }) == "Csus2");
    REQUIRE (nameOf ({ 60, 65, 67 }) == "Csus4");

    // Order must not matter, and doubling must not either.
    REQUIRE (nameOf ({ 67, 60, 64 }) == "C");
    REQUIRE (nameOf ({ 60, 64, 67, 72, 76 }) == "C");
}

TEST_CASE ("Sevenths and extensions are named", "[namer]")
{
    REQUIRE (nameOf ({ 60, 64, 67, 70 }) == "C7");
    REQUIRE (nameOf ({ 60, 64, 67, 71 }) == "Cmaj7");
    REQUIRE (nameOf ({ 60, 63, 67, 70 }) == "Cm7");
    REQUIRE (nameOf ({ 60, 63, 67, 71 }) == "CmMaj7");
    REQUIRE (nameOf ({ 60, 64, 67, 69 }) == "C6");
    REQUIRE (nameOf ({ 60, 63, 67, 69 }) == "Cm6");
    REQUIRE (nameOf ({ 60, 63, 66, 70 }) == "Cm7b5");
    REQUIRE (nameOf ({ 60, 63, 66, 69 }) == "Cdim7");
    REQUIRE (nameOf ({ 60, 65, 67, 70 }) == "C7sus4");
    REQUIRE (nameOf ({ 60, 62, 64, 67 }) == "Cadd9");
    REQUIRE (nameOf ({ 60, 62, 64, 67, 70 }) == "C9");
    REQUIRE (nameOf ({ 60, 62, 64, 67, 71 }) == "Cmaj9");
    REQUIRE (nameOf ({ 60, 62, 63, 67, 70 }) == "Cm9");
}

TEST_CASE ("Inversions are named as slash chords", "[namer]")
{
    // First inversion of C major: E in the bass.
    REQUIRE (nameOf ({ 64, 67, 72 }) == "C/E");
    // Second inversion.
    REQUIRE (nameOf ({ 67, 72, 76 }) == "C/G");
    // Am7 with the seventh in the bass.
    REQUIRE (nameOf ({ 67, 69, 72, 76 }) == "Am7/G");
}

TEST_CASE ("A chord in root position is not named as a slash chord", "[namer]")
{
    // The bass is tried as the root first, so this must be Am7 and not C6/A --
    // both spell the same pitch classes.
    REQUIRE (nameOf ({ 57, 60, 64, 67 }) == "Am7");
    // ...and the same notes with C in the bass are C6.
    REQUIRE (nameOf ({ 60, 64, 67, 69 }) == "C6");
}

TEST_CASE ("Unrecognised sets fall back to listing the notes", "[namer]")
{
    // A chromatic cluster matches no formula.
    const auto s = nameOf ({ 60, 61, 62 });
    REQUIRE (s == "C C# D");
}

TEST_CASE ("Edge cases are handled without writing past the buffer", "[namer]")
{
    char buf[ChordNamer::kMaxName];

    // Empty set.
    std::memset (buf, 0x7f, sizeof (buf));
    REQUIRE (ChordNamer::name (nullptr, 0, buf, ChordNamer::kMaxName) == 0);
    REQUIRE (buf[0] == '\0');

    const int notes[] = { 60, 64, 67 };
    REQUIRE (ChordNamer::name (notes, 0, buf, ChordNamer::kMaxName) == 0);

    // Out-of-range notes are skipped, not trusted.
    const int bad[] = { -5, 200, 60, 64, 67 };
    char b2[ChordNamer::kMaxName] = { 0 };
    ChordNamer::name (bad, 5, b2, ChordNamer::kMaxName);
    REQUIRE (std::string (b2) == "C");

    // A tiny buffer must still be terminated and never overrun.
    char tiny[4];
    std::memset (tiny, 0x7f, sizeof (tiny));
    const int len = ChordNamer::name (notes, 3, tiny, 4);
    REQUIRE (len < 4);
    REQUIRE (tiny[len] == '\0');
}

TEST_CASE ("Every formula in the table round-trips from every root", "[namer]")
{
    // Each named chord, transposed through all twelve roots, must come back
    // with that root's name -- this catches a rotation bug in one place rather
    // than needing a case per chord.
    struct Case { const char* suffix; std::initializer_list<int> iv; };
    const Case cases[] = {
        { "",      { 0, 4, 7 } },      { "m",    { 0, 3, 7 } },
        { "dim",   { 0, 3, 6 } },      { "aug",  { 0, 4, 8 } },
        { "7",     { 0, 4, 7, 10 } },  { "maj7", { 0, 4, 7, 11 } },
        { "m7",    { 0, 3, 7, 10 } },  { "dim7", { 0, 3, 6, 9 } },
    };
    const char* pc[12] = { "C", "C#", "D", "D#", "E", "F",
                           "F#", "G", "G#", "A", "A#", "B" };

    for (const auto& c : cases)
        for (int root = 0; root < 12; ++root)
        {
            int notes[8]; int n = 0;
            for (int iv : c.iv) notes[n++] = 60 + root + iv;

            char buf[ChordNamer::kMaxName] = { 0 };
            ChordNamer::name (notes, n, buf, ChordNamer::kMaxName);

            const std::string expected = std::string (pc[root]) + c.suffix;
            INFO ("root " << root << " suffix '" << c.suffix << "' got '" << buf << "'");
            REQUIRE (std::string (buf) == expected);
        }
}
