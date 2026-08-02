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

TEST_CASE ("Voice-led places the first chord around the played root", "[chord][voicing]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::VoiceLed);
    c.setQuality (0);                                  // maj
    const auto notes = chordNotesOn (onEvents (c, 60));
    // Centre is the root itself on the first chord: C is placed at 60, E and G
    // in whichever octave sits nearest 60.
    REQUIRE (notes == std::vector<int> { 60, 64, 67 });
}

TEST_CASE ("Voice-led keeps common tones at the same absolute pitch",
           "[chord][voicing]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::VoiceLed);

    c.setQuality (0);                                  // maj
    const auto cMaj = chordNotesOn (onEvents (c, 60));  // C E G
    offEvents (c, 60);

    c.setQuality (1);                                  // min
    const auto aMin = chordNotesOn (onEvents (c, 69));  // A C E

    // C and E are in both chords and must land on the identical MIDI note.
    for (int shared : { 60, 64 })
    {
        const bool inC = std::find (cMaj.begin(), cMaj.end(), shared) != cMaj.end();
        const bool inA = std::find (aMin.begin(), aMin.end(), shared) != aMin.end();
        INFO ("shared tone " << shared);
        REQUIRE (inC);
        REQUIRE (inA);
    }
}

TEST_CASE ("Voice-led moves less than root position", "[chord][voicing]")
{
    // Total absolute movement from one chord to the next, summed over voices.
    auto totalMovement = [] (int voicing)
    {
        auto c = makeChord();
        c.setVoicing (voicing);
        c.setQuality (0);
        auto a = chordNotesOn (onEvents (c, 60));      // C major
        offEvents (c, 60);
        auto b = chordNotesOn (onEvents (c, 65));      // F major
        int move = 0;
        const std::size_t n = a.size() < b.size() ? a.size() : b.size();
        for (std::size_t i = 0; i < n; ++i)
            move += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        return move;
    };

    INFO ("voice-led " << totalMovement (ChordMode::VoiceLed)
          << " vs root position " << totalMovement (ChordMode::RootPosition));
    REQUIRE (totalMovement (ChordMode::VoiceLed) < totalMovement (ChordMode::RootPosition));
}

TEST_CASE ("Changing quality on a held root re-voices immediately", "[chord]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::VoiceLed);
    c.setQuality (0);                       // maj
    onEvents (c, 60);                       // C major sounding

    // Press the m7 key (48 + 3) while C is still held.
    const auto evs = onEvents (c, 51);
    REQUIRE_FALSE (evs.empty());            // it re-voiced

    std::vector<int> ons, offs;
    for (const auto& e : evs)
    {
        if (e.isRoot) continue;
        (e.noteOn ? ons : offs).push_back (e.note);
    }
    // C major -> C minor 7: E leaves, Eb and Bb arrive, C and G untouched.
    REQUIRE (offs == std::vector<int> { 64 });
    REQUIRE (ons.size() == 2);
}

TEST_CASE ("Common tones are never retriggered", "[chord]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::VoiceLed);
    c.setQuality (0);                       // maj  -> C E G
    onEvents (c, 60);

    const auto evs = onEvents (c, 52);      // E of the quality zone = maj7

    // maj -> maj7 only ADDS the 7th. C, E and G must not appear at all.
    for (const auto& e : evs)
    {
        if (e.isRoot) continue;
        INFO ("event on note " << e.note);
        REQUIRE (e.note != 60);
        REQUIRE (e.note != 64);
        REQUIRE (e.note != 67);
    }
}

TEST_CASE ("refresh re-voices after an automated parameter change", "[chord]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::VoiceLed);
    c.setQuality (0);
    onEvents (c, 60);

    ChordMode::Event buf[ChordMode::kMaxEvents];
    REQUIRE (c.refresh (buf, ChordMode::kMaxEvents) == 0);   // nothing pending

    c.setQuality (3);                                        // as if from automation
    const int n = c.refresh (buf, ChordMode::kMaxEvents);
    REQUIRE (n > 0);
    REQUIRE (c.refresh (buf, ChordMode::kMaxEvents) == 0);   // consumed once only
}

TEST_CASE ("refresh does nothing when no root is held", "[chord]")
{
    auto c = makeChord();
    c.setQuality (5);
    ChordMode::Event buf[ChordMode::kMaxEvents];
    REQUIRE (c.refresh (buf, ChordMode::kMaxEvents) == 0);
}
