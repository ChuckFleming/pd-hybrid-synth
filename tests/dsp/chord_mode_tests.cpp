#include <catch2/catch_test_macros.hpp>

#include "dsp/ChordMode.h"

#include <algorithm>
#include <vector>

using pdhybrid::ChordMode;

namespace {

std::vector<int> intervalsOf (int quality)
{
    int buf[ChordMode::kMaxChordNotes] = { 0 };
    const int n = ChordMode::qualityIntervals (quality, buf);
    return std::vector<int> (buf, buf + n);
}

} // namespace

TEST_CASE ("Quality table matches the spec", "[chord]")
{
    REQUIRE (intervalsOf (0)  == std::vector<int> { 0, 4, 7 });        // maj
    REQUIRE (intervalsOf (1)  == std::vector<int> { 0, 3, 7 });        // min
    REQUIRE (intervalsOf (2)  == std::vector<int> { 0, 4, 7, 10 });    // 7
    REQUIRE (intervalsOf (3)  == std::vector<int> { 0, 3, 7, 10 });    // m7
    REQUIRE (intervalsOf (4)  == std::vector<int> { 0, 4, 7, 11 });    // maj7
    REQUIRE (intervalsOf (5)  == std::vector<int> { 0, 4, 7, 9 });     // 6
    REQUIRE (intervalsOf (6)  == std::vector<int> { 0, 3, 6, 10 });    // m7b5
    REQUIRE (intervalsOf (7)  == std::vector<int> { 0, 3, 6, 9 });     // dim7
    REQUIRE (intervalsOf (8)  == std::vector<int> { 0, 4, 8 });        // aug
    REQUIRE (intervalsOf (9)  == std::vector<int> { 0, 2, 7 });        // sus2
    REQUIRE (intervalsOf (10) == std::vector<int> { 0, 5, 7 });        // sus4
    REQUIRE (intervalsOf (11) == std::vector<int> { 0, 3, 7, 9 });     // m6
}

TEST_CASE ("Quality index is clamped, never out of bounds", "[chord]")
{
    int buf[ChordMode::kMaxChordNotes] = { 0 };
    REQUIRE (ChordMode::qualityIntervals (-5, buf) > 0);
    REQUIRE (ChordMode::qualityIntervals (99, buf) > 0);
}

namespace {

// Convenience: run one note-on and return the events it produced.
std::vector<ChordMode::Event> onEvents (ChordMode& c, int note, float vel = 1.0f)
{
    ChordMode::Event buf[ChordMode::kMaxEvents];
    const int n = c.handleNoteOn (note, vel, buf, ChordMode::kMaxEvents);
    return std::vector<ChordMode::Event> (buf, buf + n);
}

std::vector<ChordMode::Event> offEvents (ChordMode& c, int note)
{
    ChordMode::Event buf[ChordMode::kMaxEvents];
    const int n = c.handleNoteOff (note, buf, ChordMode::kMaxEvents);
    return std::vector<ChordMode::Event> (buf, buf + n);
}

// The poly-bound chord notes (isRoot events belong to the bass layer).
std::vector<int> chordNotesOn (const std::vector<ChordMode::Event>& evs)
{
    std::vector<int> v;
    for (const auto& e : evs) if (e.noteOn && ! e.isRoot) v.push_back (e.note);
    return v;
}

ChordMode makeChord()
{
    ChordMode c;
    c.setEnabled (true);
    c.setSplitNote (60);        // quality zone 48..59, root zone 60+
    c.setVoicing (ChordMode::RootPosition);   // predictable for routing tests
    c.setSpread (0.0);
    c.setOctave (0);
    c.setQuality (0);           // maj
    return c;
}

} // namespace

TEST_CASE ("Disabled chord mode passes notes straight through", "[chord]")
{
    ChordMode c;
    c.setEnabled (false);
    const auto evs = onEvents (c, 60);
    REQUIRE (evs.size() == 1);
    REQUIRE (evs[0].noteOn);
    REQUIRE (evs[0].note == 60);
}

TEST_CASE ("Quality-zone keys latch and make no sound", "[chord]")
{
    auto c = makeChord();
    // D# of the quality octave = 48 + 3 = m7.
    const auto evs = onEvents (c, 51);
    REQUIRE (evs.empty());
    REQUIRE (c.latchedQuality() == 3);
    REQUIRE (c.heldRoot() == -1);

    // Releasing a quality key does nothing: the latch persists by design.
    REQUIRE (offEvents (c, 51).empty());
    REQUIRE (c.latchedQuality() == 3);
}

TEST_CASE ("Root-zone keys play the latched chord", "[chord]")
{
    auto c = makeChord();
    c.setQuality (1);                        // min
    const auto evs = onEvents (c, 60);       // C4
    REQUIRE (chordNotesOn (evs) == std::vector<int> { 60, 63, 67 });
    REQUIRE (c.heldRoot() == 60);
}

TEST_CASE ("Notes below the quality zone pass through", "[chord]")
{
    auto c = makeChord();
    const auto evs = onEvents (c, 40);       // below 48
    REQUIRE (evs.size() == 1);
    REQUIRE (evs[0].note == 40);
    REQUIRE (c.heldRoot() == -1);
}

TEST_CASE ("Releasing the root releases the chord", "[chord]")
{
    auto c = makeChord();
    onEvents (c, 60);
    const auto evs = offEvents (c, 60);
    std::vector<int> offs;
    for (const auto& e : evs) if (! e.noteOn && ! e.isRoot) offs.push_back (e.note);
    REQUIRE (offs == std::vector<int> { 60, 64, 67 });
    REQUIRE (c.heldRoot() == -1);
}

TEST_CASE ("The bass layer gets a root-flagged event", "[chord]")
{
    auto c = makeChord();
    const auto evs = onEvents (c, 60);
    int rootOns = 0, rootNote = -1;
    for (const auto& e : evs) if (e.noteOn && e.isRoot) { ++rootOns; rootNote = e.note; }
    REQUIRE (rootOns == 1);
    REQUIRE (rootNote == 60);
}
