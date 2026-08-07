# Chord Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a chord-organ / Orchid-style chord mode: a one-octave quality zone latches a chord type, the root zone above it sets the root, and the chord is voiced automatically.

**Architecture:** A new `ChordMode` class in `src/dsp/` — pure C++, no JUCE, no audio-thread allocation — shaped exactly like the existing `Arpeggiator`. It sits in the processor's MIDI path *ahead of* the arpeggiator and mono-bass tap-off, converting one incoming note into a set of note events. The editor gets a `ChordKeyboard` display on the VOICE tab.

**Tech Stack:** C++17, JUCE 7 (plugin layer only), Catch2 v3 for tests, CMake + Visual Studio 17 2022.

## Global Constraints

- **Build with Bash, not PowerShell.** CMake path: `/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`, generator "Visual Studio 17 2022", config Release.
- **`src/dsp/` must not include JUCE.** The offline harness compiles it standalone.
- **No audio-thread allocation.** Fixed-capacity arrays only — no `std::vector`, no `new`, in anything reachable from `processBlock`.
- **Enum and choice-parameter entries are appended, never inserted.** Presets and host automation store them by index.
- **Editor combo item lists must exactly match their APVTS choice parameter's length and order.** A shorter list silently mis-routes every entry.
- **Full verification before any commit that touches DSP:** `./build/tests/Release/pdhybrid_tests.exe` (all green), then rebuild the VST3 and run `./build/tools/pluginval.exe --strictness-level 8 --validate-in-process "build/PDHybridSynth_artefacts/Release/VST3/PD Hybrid Synth.vst3"` (must print SUCCESS).
- Spec: `docs/superpowers/specs/2026-07-26-chord-mode-design.md`.

## File Structure

| File | Responsibility |
|---|---|
| `src/dsp/ChordMode.h` (new) | Public interface, `Event`, `Voicing` enum, fixed-capacity state |
| `src/dsp/ChordMode.cpp` (new) | Quality table, zone routing, voicing engine, common-tone diffing |
| `tests/dsp/chord_mode_tests.cpp` (new) | Headless Catch2 coverage of all of the above |
| `CMakeLists.txt` | Add `ChordMode.cpp` to `pdhybrid_dsp` |
| `tests/CMakeLists.txt` | Add `chord_mode_tests.cpp` |
| `src/plugin/PluginProcessor.h/.cpp` | 6 parameters, `ChordMode` member, MIDI-chain integration, live-state atomics |
| `src/plugin/Displays.h` | New `ChordKeyboard` display class |
| `src/plugin/PluginEditor.h/.cpp` | Chord card on the VOICE tab |
| `src/plugin/PresetManager.cpp` | Two factory presets |

---

### Task 1: ChordMode skeleton and quality table

**Files:**
- Create: `src/dsp/ChordMode.h`, `src/dsp/ChordMode.cpp`
- Create: `tests/dsp/chord_mode_tests.cpp`
- Modify: `CMakeLists.txt` (add to `pdhybrid_dsp` source list, next to `Arpeggiator.cpp`)
- Modify: `tests/CMakeLists.txt` (add to `pdhybrid_tests` source list)

**Interfaces:**
- Consumes: nothing.
- Produces: `pdhybrid::ChordMode` with `static int qualityIntervals (int quality, int* out) noexcept` returning the note count and filling `out` with semitone offsets; `ChordMode::kNumQualities == 12`; `ChordMode::kMaxChordNotes == 4`.

- [ ] **Step 1: Write the failing test**

Create `tests/dsp/chord_mode_tests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "dsp/ChordMode.h"

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
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" -S . -B build > /dev/null && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head
```

Expected: FAIL — `Cannot open include file: 'dsp/ChordMode.h'`.

- [ ] **Step 3: Write minimal implementation**

`src/dsp/ChordMode.h`:

```cpp
#pragma once

namespace pdhybrid {

/**
    Chord-organ / Orchid-style chord mode.

    The keyboard splits into a one-octave "quality zone" whose twelve semitones
    latch a chord type, and a "root zone" above it whose keys set the root.
    Pressing a root emits a voiced chord; changing the latched quality re-voices
    whatever is sounding.

    Pure C++, no JUCE, fixed-capacity storage -> no audio-thread allocation.
    Shaped like Arpeggiator: it consumes note events and emits note events, and
    sits ahead of the arpeggiator and mono-bass tap-off in the processor.
*/
class ChordMode
{
public:
    static constexpr int kNumQualities    = 12;
    static constexpr int kMaxChordNotes   = 4;
    static constexpr int kQualityZoneSize = 12;   // exactly one octave
    static constexpr int kMaxEvents       = 16;   // 4 offs + 4 ons + root pair, with room

    /** `isRoot` events are for the mono-bass layer only; every other event is a
        chord note for the poly voices. Keeping them in one stream lets the
        processor route both from a single loop. */
    struct Event { bool noteOn; int note; float velocity; bool isRoot; };

    enum Voicing { VoiceLed = 0, RootPosition, Closed, Drop2, Shell };

    /** Semitone offsets of `quality` (0..11). Returns the note count. */
    static int qualityIntervals (int quality, int* out) noexcept;
};

} // namespace pdhybrid
```

`src/dsp/ChordMode.cpp`:

```cpp
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
```

In `CMakeLists.txt`, add to the `pdhybrid_dsp` source list immediately after `src/dsp/Arpeggiator.cpp`:

```cmake
    src/dsp/ChordMode.cpp
```

In `tests/CMakeLists.txt`, add to the `pdhybrid_tests` source list after `dsp/arpeggiator_tests.cpp`:

```cmake
    dsp/chord_mode_tests.cpp
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head; ./build/tests/Release/pdhybrid_tests.exe "[chord]" 2>&1 | tail -5
```

Expected: PASS, 2 test cases.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/ChordMode.h src/dsp/ChordMode.cpp tests/dsp/chord_mode_tests.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "Chord mode: quality table"
```

---

### Task 2: Zone routing and quality latching

**Files:**
- Modify: `src/dsp/ChordMode.h`, `src/dsp/ChordMode.cpp`
- Test: `tests/dsp/chord_mode_tests.cpp`

**Interfaces:**
- Consumes: `ChordMode::qualityIntervals`, `ChordMode::Event` from Task 1.
- Produces: `setEnabled(bool)`, `setSplitNote(int)`, `setQuality(int)`, `latchedQuality() const -> int`, `heldRoot() const -> int`, `handleNoteOn(int note, float vel, Event* out, int maxOut) -> int`, `handleNoteOff(int note, Event* out, int maxOut) -> int`. Chord notes are not voiced yet — Task 3 replaces the placeholder in `buildVoicing`.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/chord_mode_tests.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head -3
```

Expected: FAIL — `'setEnabled': is not a member of 'pdhybrid::ChordMode'`.

- [ ] **Step 3: Write minimal implementation**

Add to `ChordMode.h`, inside the class after `qualityIntervals`:

```cpp
    void setEnabled   (bool on) noexcept       { enabled_ = on; }
    void setSplitNote (int midiNote) noexcept;
    void setQuality   (int index) noexcept;
    void setVoicing   (int mode) noexcept      { voicing_ = mode; dirty_ = true; }
    void setSpread    (double spread01) noexcept;
    void setOctave    (int octaves) noexcept;

    int handleNoteOn  (int note, float vel, Event* out, int maxOut) noexcept;
    int handleNoteOff (int note, Event* out, int maxOut) noexcept;

    int latchedQuality() const noexcept { return quality_; }
    int heldRoot()       const noexcept { return heldRoot_; }

private:
    /** Fills `out` with the voiced chord for `root`. Returns the note count. */
    int buildVoicing (int root, int* out) const noexcept;
    /** Diffs `newNotes` against what is sounding and emits only the changes. */
    int emitChange (const int* newNotes, int newCount, int root,
                    Event* out, int maxOut) noexcept;

    bool   enabled_ = false;
    int    split_   = 60;
    int    quality_ = 0;
    int    voicing_ = VoiceLed;
    double spread_  = 0.4;
    int    octave_  = 0;
    bool   dirty_   = false;

    int    heldRoot_  = -1;
    float  heldVel_   = 1.0f;
    int    curRoot_   = -1;                    // note currently sounding on the bass
    int    cur_[kMaxChordNotes] = { 0 };       // chord notes currently sounding
    int    curCount_ = 0;
```

Add to `ChordMode.cpp`:

```cpp
void ChordMode::setSplitNote (int midiNote) noexcept
{
    if (midiNote < 36)  midiNote = 36;
    if (midiNote > 84)  midiNote = 84;
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
```

Note the `#include <cstddef>` is not needed; `nullptr` with `newCount == 0` never dereferences `newNotes`.

- [ ] **Step 4: Run test to verify it passes**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head; ./build/tests/Release/pdhybrid_tests.exe "[chord]" 2>&1 | tail -5
```

Expected: PASS, 8 test cases.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/ChordMode.h src/dsp/ChordMode.cpp tests/dsp/chord_mode_tests.cpp
git commit -m "Chord mode: zone routing and quality latching"
```

---

### Task 3: Voice-Led placement

**Files:**
- Modify: `src/dsp/ChordMode.cpp` (replace the `buildVoicing` placeholder)
- Test: `tests/dsp/chord_mode_tests.cpp`

**Interfaces:**
- Consumes: `buildVoicing`, `cur_`, `curCount_` from Task 2.
- Produces: `ChordMode::VoiceLed` behaviour — closest-position placement around the previous voicing's centroid.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/chord_mode_tests.cpp`:

```cpp
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
```

Add `#include <algorithm>` to the test file's includes.

- [ ] **Step 2: Run test to verify it fails**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head; ./build/tests/Release/pdhybrid_tests.exe "[voicing]" 2>&1 | tail -12
```

Expected: FAIL — the placeholder `buildVoicing` ignores the voicing mode, so voice-led and root position give identical movement.

- [ ] **Step 3: Write minimal implementation**

Replace the placeholder `buildVoicing` in `ChordMode.cpp` with:

```cpp
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

int ChordMode::buildVoicing (int root, int* out) const noexcept
{
    int iv[kMaxChordNotes];
    const int n = qualityIntervals (quality_, iv);

    if (voicing_ == RootPosition)
    {
        for (int i = 0; i < n; ++i)
            out[i] = root + iv[i];
        return n;
    }

    // Closest-position placement: put every pitch class in whichever octave
    // sits nearest the previous voicing's centroid. Common tones fall out of
    // this for free -- a pitch class already sounding near the centre resolves
    // to the same absolute note, so no explicit common-tone logic is needed.
    double centre = static_cast<double> (root);
    if (curCount_ > 0)
    {
        double sum = 0.0;
        for (int i = 0; i < curCount_; ++i) sum += cur_[i];
        centre = sum / curCount_;
    }

    for (int i = 0; i < n; ++i)
    {
        const int pc = ((root + iv[i]) % 12 + 12) % 12;
        // round-half-away-from-zero without <cmath>: the operand is small.
        const double k = (centre - pc) / 12.0;
        const int    oct = static_cast<int> (k >= 0.0 ? k + 0.5 : k - 0.5);
        out[i] = pc + 12 * oct;
    }
    sortAscending (out, n);
    return n;
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head; ./build/tests/Release/pdhybrid_tests.exe "[chord]" 2>&1 | tail -5
```

Expected: PASS, 11 test cases.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/ChordMode.cpp tests/dsp/chord_mode_tests.cpp
git commit -m "Chord mode: voice-led placement"
```

---

### Task 4: Live re-voicing without retriggering common tones

**Files:**
- Modify: `src/dsp/ChordMode.h`, `src/dsp/ChordMode.cpp`
- Test: `tests/dsp/chord_mode_tests.cpp`

**Interfaces:**
- Consumes: `emitChange`, `dirty_`, `heldRoot_` from Task 2.
- Produces: `int refresh (Event* out, int maxOut) noexcept` — emits any re-voice owed after a parameter change; call once per block. Quality-zone key presses re-voice immediately inside `handleNoteOn`.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/chord_mode_tests.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head -3
```

Expected: FAIL — `'refresh': is not a member of 'pdhybrid::ChordMode'`.

- [ ] **Step 3: Write minimal implementation**

In `ChordMode.h`, add to the public section:

```cpp
    /** Emits any re-voice owed after a parameter change (quality, voicing,
        spread, octave). Call once per block; returns 0 when nothing is due. */
    int refresh (Event* out, int maxOut) noexcept;
```

and to the private section:

```cpp
    int revoice (Event* out, int maxOut) noexcept;
```

In `ChordMode.cpp`, add:

```cpp
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
```

and change the quality-zone branch of `handleNoteOn` from:

```cpp
        setQuality (note - (split_ - kQualityZoneSize));
        return 0;
```

to:

```cpp
        setQuality (note - (split_ - kQualityZoneSize));
        // A quality key re-voices whatever is sounding, right now. `refresh`
        // covers the same change arriving from host automation instead.
        return revoice (out, maxOut);
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head; ./build/tests/Release/pdhybrid_tests.exe "[chord]" 2>&1 | tail -5
```

Expected: PASS, 15 test cases.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/ChordMode.h src/dsp/ChordMode.cpp tests/dsp/chord_mode_tests.cpp
git commit -m "Chord mode: live re-voicing, common tones held"
```

---

### Task 5: Remaining voicing modes

**Files:**
- Modify: `src/dsp/ChordMode.cpp` (`buildVoicing`)
- Test: `tests/dsp/chord_mode_tests.cpp`

**Interfaces:**
- Consumes: `buildVoicing`, `sortAscending` from Task 3.
- Produces: `Closed`, `Drop2` and `Shell` behaviour.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/chord_mode_tests.cpp`:

```cpp
TEST_CASE ("Closed voicing stays in one fixed octave", "[chord][voicing]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::Closed);
    c.setQuality (0);

    // Same chord played three octaves apart must come out identically: Closed
    // has no memory and no dependence on the played octave.
    const auto low  = chordNotesOn (onEvents (c, 60));
    offEvents (c, 60);
    const auto high = chordNotesOn (onEvents (c, 84));
    offEvents (c, 84);
    REQUIRE (low == high);

    // And it sits inside the octave starting at 54.
    for (int nte : low) { REQUIRE (nte >= 54); REQUIRE (nte <= 65); }
}

TEST_CASE ("Drop-2 lowers the second voice from the top", "[chord][voicing]")
{
    auto c = makeChord();
    c.setQuality (4);                       // maj7, four notes

    c.setVoicing (ChordMode::Closed);
    const auto closed = chordNotesOn (onEvents (c, 60));
    offEvents (c, 60);

    c.setVoicing (ChordMode::Drop2);
    const auto drop = chordNotesOn (onEvents (c, 60));

    REQUIRE (closed.size() == 4);
    REQUIRE (drop.size() == 4);
    // The dropped voice is an octave below where it sat in the closed voicing.
    REQUIRE (drop.front() == closed[closed.size() - 2] - 12);
    // Wider overall.
    REQUIRE (drop.back() - drop.front() > closed.back() - closed.front());
}

TEST_CASE ("Shell drops the fifth from four-note chords only", "[chord][voicing]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::Shell);

    c.setQuality (4);                       // maj7 -> root, 3rd, 7th
    const auto seventh = chordNotesOn (onEvents (c, 60));
    offEvents (c, 60);
    REQUIRE (seventh.size() == 3);
    for (int nte : seventh) REQUIRE (nte % 12 != 7);   // no G

    c.setQuality (0);                       // maj triad is left intact
    const auto triad = chordNotesOn (onEvents (c, 60));
    REQUIRE (triad.size() == 3);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && ./build/tests/Release/pdhybrid_tests.exe "[voicing]" 2>&1 | tail -12
```

Expected: FAIL — `Closed` currently falls through to the voice-led branch, so the two octaves differ.

- [ ] **Step 3: Write minimal implementation**

In `buildVoicing`, insert the Shell reduction immediately after `qualityIntervals`, and the Closed/Drop2 branch before the voice-led fallback:

```cpp
    int iv[kMaxChordNotes];
    int n = qualityIntervals (quality_, iv);

    // Shell: drop the fifth, but only where there is something left worth
    // playing -- a triad reduced to root and third is not a useful chord.
    if (voicing_ == Shell && n >= 4)
    {
        int m = 0;
        for (int i = 0; i < n; ++i)
            if (iv[i] != 7) iv[m++] = iv[i];
        n = m;
    }

    if (voicing_ == RootPosition)
    {
        for (int i = 0; i < n; ++i)
            out[i] = root + iv[i];
        return n;
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
        return n;
    }
```

(The voice-led block from Task 3 follows unchanged, using `n` rather than a
newly-declared `const int n`.)

- [ ] **Step 4: Run test to verify it passes**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head; ./build/tests/Release/pdhybrid_tests.exe "[chord]" 2>&1 | tail -5
```

Expected: PASS, 18 test cases.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/ChordMode.cpp tests/dsp/chord_mode_tests.cpp
git commit -m "Chord mode: closed, drop-2 and shell voicings"
```

---

### Task 6: Spread, octave and range clamp

**Files:**
- Modify: `src/dsp/ChordMode.cpp` (`buildVoicing` tail)
- Test: `tests/dsp/chord_mode_tests.cpp`

**Interfaces:**
- Consumes: `buildVoicing` from Tasks 3 and 5.
- Produces: spread, octave transpose and MIDI-range clamping applied to every voicing mode.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/chord_mode_tests.cpp`:

```cpp
namespace {

int chordWidth (ChordMode& c, int root)
{
    const auto v = chordNotesOn (onEvents (c, root));
    offEvents (c, root);
    if (v.empty()) return 0;
    return v.back() - v.front();
}

} // namespace

TEST_CASE ("Spread widens the voicing monotonically", "[chord][voicing]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::VoiceLed);
    c.setQuality (4);                       // maj7, four notes

    c.setSpread (0.0);
    const int closed = chordWidth (c, 60);
    c.setSpread (0.5);
    const int mid = chordWidth (c, 60);
    c.setSpread (1.0);
    const int wide = chordWidth (c, 60);

    INFO ("widths " << closed << " " << mid << " " << wide);
    REQUIRE (mid  >= closed);
    REQUIRE (wide >  closed);
}

TEST_CASE ("Octave transposes the whole voicing", "[chord][voicing]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::RootPosition);
    c.setQuality (0);
    c.setSpread (0.0);

    c.setOctave (0);
    const auto base = chordNotesOn (onEvents (c, 60));
    offEvents (c, 60);

    c.setOctave (1);
    const auto up = chordNotesOn (onEvents (c, 60));

    REQUIRE (base.size() == up.size());
    for (std::size_t i = 0; i < base.size(); ++i)
        REQUIRE (up[i] == base[i] + 12);
}

TEST_CASE ("Every voiced note stays inside MIDI range", "[chord][voicing]")
{
    for (int voicing = 0; voicing <= ChordMode::Shell; ++voicing)
        for (int quality = 0; quality < ChordMode::kNumQualities; ++quality)
            for (int octave : { -2, 0, 2 })
                for (double spread : { 0.0, 1.0 })
                    for (int root : { 36, 60, 84 })
                    {
                        auto c = makeChord();
                        c.setVoicing (voicing);
                        c.setQuality (quality);
                        c.setOctave (octave);
                        c.setSpread (spread);
                        const auto v = chordNotesOn (onEvents (c, root));
                        INFO ("voicing " << voicing << " quality " << quality
                              << " octave " << octave << " root " << root);
                        REQUIRE_FALSE (v.empty());
                        for (int nte : v) { REQUIRE (nte >= 0); REQUIRE (nte <= 127); }
                    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && ./build/tests/Release/pdhybrid_tests.exe "[voicing]" 2>&1 | tail -12
```

Expected: FAIL — spread and octave are stored but never applied, so all three widths are equal.

- [ ] **Step 3: Write minimal implementation**

Replace each `return n;` in `buildVoicing` with `break;` inside a `do { ... } while (false);`, or more simply restructure so all three branches fall through to a shared tail. Add this tail as the last thing `buildVoicing` does:

```cpp
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
    // chord keeps its pitch classes rather than being clipped to a wall.
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
    return m;
```

To make the three branches fall through, change `return n;` in the RootPosition branch to a plain assignment and use `else if` chaining:

```cpp
    if (voicing_ == RootPosition)
    {
        for (int i = 0; i < n; ++i)
            out[i] = root + iv[i];
    }
    else if (voicing_ == Closed || voicing_ == Drop2)
    {
        // ... as in Task 5, but without the trailing `return n;`
    }
    else
    {
        // ... voice-led block from Task 3, without the trailing `return n;`
    }
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head; ./build/tests/Release/pdhybrid_tests.exe "[chord]" 2>&1 | tail -5
```

Expected: PASS, 21 test cases.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/ChordMode.cpp tests/dsp/chord_mode_tests.cpp
git commit -m "Chord mode: spread, octave and range clamping"
```

---

### Task 7: Flush and edge cases

**Files:**
- Modify: `src/dsp/ChordMode.h`, `src/dsp/ChordMode.cpp`
- Test: `tests/dsp/chord_mode_tests.cpp`

**Interfaces:**
- Consumes: `emitChange`, `heldRoot_`, `curRoot_` from Task 2.
- Produces: `int flush (Event* out, int maxOut) noexcept` releasing everything sounding; `int voicedNotes (int* out, int maxOut) const noexcept` for the display.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/chord_mode_tests.cpp`:

```cpp
TEST_CASE ("flush releases everything sounding", "[chord]")
{
    auto c = makeChord();
    onEvents (c, 60);

    ChordMode::Event buf[ChordMode::kMaxEvents];
    const int n = c.flush (buf, ChordMode::kMaxEvents);
    REQUIRE (n > 0);
    for (int i = 0; i < n; ++i)
        REQUIRE_FALSE (buf[i].noteOn);          // offs only

    REQUIRE (c.heldRoot() == -1);
    REQUIRE (c.flush (buf, ChordMode::kMaxEvents) == 0);   // idempotent
}

TEST_CASE ("voicedNotes reports what is sounding", "[chord]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::RootPosition);
    c.setQuality (0);
    onEvents (c, 60);

    int notes[ChordMode::kMaxChordNotes] = { 0 };
    const int n = c.voicedNotes (notes, ChordMode::kMaxChordNotes);
    REQUIRE (n == 3);
    REQUIRE (notes[0] == 60);
    REQUIRE (notes[1] == 64);
    REQUIRE (notes[2] == 67);
}

TEST_CASE ("A second root replaces the first", "[chord]")
{
    auto c = makeChord();
    c.setVoicing (ChordMode::RootPosition);
    onEvents (c, 60);
    onEvents (c, 65);
    REQUIRE (c.heldRoot() == 65);

    // Releasing the abandoned root must not silence the chord that replaced it.
    const auto evs = offEvents (c, 60);
    REQUIRE (evs.empty());
    REQUIRE (c.heldRoot() == 65);
}

TEST_CASE ("Output never exceeds maxOut", "[chord]")
{
    auto c = makeChord();
    c.setQuality (4);
    ChordMode::Event tiny[2];
    const int n = c.handleNoteOn (60, 1.0f, tiny, 2);
    REQUIRE (n <= 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head -3
```

Expected: FAIL — `'flush': is not a member of 'pdhybrid::ChordMode'`.

- [ ] **Step 3: Write minimal implementation**

In `ChordMode.h` public section:

```cpp
    /** Releases everything sounding. Used when the split moves or chord mode is
        switched off, either of which would otherwise strand a held note. */
    int flush (Event* out, int maxOut) noexcept;

    /** Copies the currently sounding chord notes out. Returns the count. */
    int voicedNotes (int* out, int maxOut) const noexcept;
```

In `ChordMode.cpp`:

```cpp
int ChordMode::flush (Event* out, int maxOut) noexcept
{
    heldRoot_ = -1;
    dirty_    = false;
    return emitChange (nullptr, 0, -1, out, maxOut);
}

int ChordMode::voicedNotes (int* out, int maxOut) const noexcept
{
    int n = 0;
    for (int i = 0; i < curCount_ && n < maxOut; ++i)
        out[n++] = cur_[i];
    return n;
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target pdhybrid_tests 2>&1 | grep -Ei "error" | head; ./build/tests/Release/pdhybrid_tests.exe 2>&1 | tail -5
```

Expected: PASS — the full suite, 25 chord cases plus everything already there.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/ChordMode.h src/dsp/ChordMode.cpp tests/dsp/chord_mode_tests.cpp
git commit -m "Chord mode: flush, voicedNotes and edge cases"
```

---

### Task 8: Parameters

**Files:**
- Modify: `src/plugin/PluginProcessor.cpp` (`createLayout`, near the Arpeggiator block)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: APVTS parameter ids `chordOn`, `chordSplit`, `chordQuality`, `chordVoicing`, `chordSpread`, `chordOctave`.

- [ ] **Step 1: Write the failing test**

There is no unit test for parameter existence; the gate is that the plugin builds and pluginval's parameter fuzz passes. Skip to Step 2.

- [ ] **Step 2: Confirm the parameters do not exist yet**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && grep -c "chordOn" src/plugin/PluginProcessor.cpp
```

Expected: `0`.

- [ ] **Step 3: Write the implementation**

In `createLayout`, immediately before the `// --- Arpeggiator ---` block:

```cpp
    // --- Chord mode ---
    // A one-octave quality zone latches a chord type; the root zone above it
    // sets the root. chordQuality is a real parameter rather than hidden state,
    // so the latched chord saves with the preset and a host can automate it.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "chordOn", 1 }, "Chord Mode", false));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "chordSplit", 1 }, "Chord Split", 36, 84, 60));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "chordQuality", 1 }, "Chord Quality",
        juce::StringArray { "maj", "min", "7", "m7", "maj7", "6",
                            "m7b5", "dim7", "aug", "sus2", "sus4", "m6" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "chordVoicing", 1 }, "Chord Voicing",
        juce::StringArray { "Voice-Led", "Root Position", "Closed",
                            "Drop-2", "Shell" }, 0));
    pf ("chordSpread", "Chord Spread", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct);
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "chordOctave", 1 }, "Chord Octave", -2, 2, 0));
```

- [ ] **Step 4: Build and verify**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target PDHybridSynth_All 2>&1 | grep -Ei "error C|error LNK" | head; ./build/tools/pluginval.exe --strictness-level 8 --validate-in-process "build/PDHybridSynth_artefacts/Release/VST3/PD Hybrid Synth.vst3" 2>&1 | grep -E "SUCCESS|FAILED" | tail -1
```

Expected: no errors, `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add src/plugin/PluginProcessor.cpp
git commit -m "Chord mode: parameters"
```

---

### Task 9: Processor integration

**Files:**
- Modify: `src/plugin/PluginProcessor.h` (member, live-state atomics, accessors)
- Modify: `src/plugin/PluginProcessor.cpp` (`pushParams`, `handleMidiMessage`, `processBlock`)

**Interfaces:**
- Consumes: the whole `ChordMode` interface from Tasks 1–7; the parameters from Task 8.
- Produces: `chordHeldRoot() const -> int`, `chordVoicedNotes (int* out, int maxOut) const -> int` on the processor, for the editor display.

- [ ] **Step 1: Write the failing test**

Processor integration has no headless test (it needs JUCE). The gate is pluginval plus a manual check in the standalone. Skip to Step 2.

- [ ] **Step 2: Confirm the current behaviour**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && grep -n "arp_.noteOn\|handleMidiMessage (msg" src/plugin/PluginProcessor.cpp | head
```

Expected: the arp currently receives raw MIDI note numbers — chord mode has to intercept before that.

- [ ] **Step 3: Write the implementation**

In `PluginProcessor.h`, add the include and members:

```cpp
#include "dsp/ChordMode.h"
```

```cpp
    /** Root currently held in chord mode, or -1. For the editor's display. */
    int chordHeldRoot() const noexcept { return chordRoot_.load (std::memory_order_relaxed); }
    /** Copies the sounding chord out for the display. Returns the count. */
    int chordVoicedNotes (int* out, int maxOut) const noexcept;
```

```cpp
    pdhybrid::ChordMode chord_;
    bool                chordOn_ = false, chordWasOn_ = false;
    int                 chordSplitCached_ = 60;
    // Live chord state, written on the audio thread and read by the editor.
    std::atomic<int>    chordRoot_ { -1 };
    std::atomic<int>    chordNoteCount_ { 0 };
    std::atomic<int>    chordNotes_[pdhybrid::ChordMode::kMaxChordNotes] { };
```

In `PluginProcessor.cpp` `pushParams`, next to the arp block:

```cpp
    chordOn_ = apvts.getRawParameterValue ("chordOn")->load() > 0.5f;
    const int chordSplit = static_cast<int> (apvts.getRawParameterValue ("chordSplit")->load());
    chord_.setEnabled   (chordOn_);
    chord_.setSplitNote (chordSplit);
    chord_.setQuality   (static_cast<int> (apvts.getRawParameterValue ("chordQuality")->load()));
    chord_.setVoicing   (static_cast<int> (apvts.getRawParameterValue ("chordVoicing")->load()));
    chord_.setSpread    (apvts.getRawParameterValue ("chordSpread")->load());
    chord_.setOctave    (static_cast<int> (apvts.getRawParameterValue ("chordOctave")->load()));
    chordSplitCached_ = chordSplit;
```

Add the accessor:

```cpp
int PDHybridAudioProcessor::chordVoicedNotes (int* out, int maxOut) const noexcept
{
    const int n = juce::jmin (maxOut, chordNoteCount_.load (std::memory_order_relaxed));
    for (int i = 0; i < n; ++i)
        out[i] = chordNotes_[i].load (std::memory_order_relaxed);
    return n;
}
```

and a publisher called after any chord event, next to `publishModLevels`:

```cpp
void PDHybridAudioProcessor::publishChordState() noexcept
{
    int notes[pdhybrid::ChordMode::kMaxChordNotes];
    const int n = chord_.voicedNotes (notes, pdhybrid::ChordMode::kMaxChordNotes);
    for (int i = 0; i < n; ++i)
        chordNotes_[i].store (notes[i], std::memory_order_relaxed);
    chordNoteCount_.store (n, std::memory_order_relaxed);
    chordRoot_.store (chord_.heldRoot(), std::memory_order_relaxed);
}
```

(declare `void publishChordState() noexcept;` in the private section of the header.)

Replace the body of `handleMidiMessage`'s note handling so chord mode runs first. Change the note-on / note-off branches to:

```cpp
    if (msg.isNoteOn())
    {
        float vel = msg.getFloatVelocity();
        switch (velCurve_)
        {
            case 1: vel = std::sqrt (vel);       break;   // Soft
            case 2: vel = vel * vel;             break;   // Hard
            case 3: vel = 1.0f;                  break;   // Fixed
            default:                             break;   // Linear
        }

        pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
        const int n = chord_.handleNoteOn (msg.getNoteNumber(), vel,
                                           ev, pdhybrid::ChordMode::kMaxEvents);
        dispatchChordEvents (ev, n, channel, toPoly, toBass);
        publishChordState();
    }
    else if (msg.isNoteOff())
    {
        pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
        const int n = chord_.handleNoteOff (msg.getNoteNumber(),
                                            ev, pdhybrid::ChordMode::kMaxEvents);
        dispatchChordEvents (ev, n, channel, toPoly, toBass);
        publishChordState();
    }
```

and add the dispatcher (declare it private in the header as
`void dispatchChordEvents (const pdhybrid::ChordMode::Event* ev, int n, int channel, bool toPoly, bool toBass);`):

```cpp
void PDHybridAudioProcessor::dispatchChordEvents (const pdhybrid::ChordMode::Event* ev,
                                                  int n, int channel,
                                                  bool toPoly, bool toBass)
{
    // isRoot events are the bass layer's; the rest are chord notes for the
    // poly voices. With chord mode off, ChordMode passes the played note
    // through as a single isRoot event, so this routes it to both.
    for (int i = 0; i < n; ++i)
    {
        const auto& e = ev[i];
        const bool wantPoly = toPoly && (! e.isRoot || ! chordOn_);
        const bool wantBass = toBass && (e.isRoot || ! chordOn_);

        if (e.noteOn)
        {
            if (wantPoly) engine.noteOn (e.note, e.velocity, channel);
            if (wantBass) monoBass.noteOn (e.note, e.velocity);
        }
        else
        {
            if (wantPoly) engine.noteOff (e.note, channel);
            if (wantBass) monoBass.noteOff (e.note);
        }
    }
}
```

In `processBlock`, add a flush when chord mode is toggled or the split moves, alongside the existing arp flush:

```cpp
    // Flush when chord mode is switched, or the split moves under held notes:
    // either would otherwise strand a note on the wrong side of the boundary.
    static int lastSplit = 60;
    if (chordOn_ != chordWasOn_ || chordSplitCached_ != lastSplit)
    {
        pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
        const int n = chord_.flush (ev, pdhybrid::ChordMode::kMaxEvents);
        dispatchChordEvents (ev, n, 1, true, true);
        engine.allNotesOff();
        monoBass.allNotesOff();
        publishChordState();
    }
    chordWasOn_ = chordOn_;
    lastSplit   = chordSplitCached_;
```

Replace `static int lastSplit` with a member `int chordLastSplit_ = 60;` — a function-local static is shared across plugin instances and would break multi-instance use.

Also drain `refresh` once per block, before rendering, so automated parameter changes re-voice:

```cpp
    {
        pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
        const int n = chord_.refresh (ev, pdhybrid::ChordMode::kMaxEvents);
        if (n > 0) { dispatchChordEvents (ev, n, 1, true, true); publishChordState(); }
    }
```

Finally, feed the arp from chord output: in the `arpOn_` branch of `processBlock`, the held-note loop currently calls `arp_.noteOn (msg.getNoteNumber(), ...)`. Change it to run the message through `chord_` first and push every non-root event into the arp pool:

```cpp
            if (msg.isNoteOn() || msg.isNoteOff())
            {
                pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
                const int n = msg.isNoteOn()
                    ? chord_.handleNoteOn (msg.getNoteNumber(), msg.getFloatVelocity(),
                                           ev, pdhybrid::ChordMode::kMaxEvents)
                    : chord_.handleNoteOff (msg.getNoteNumber(),
                                            ev, pdhybrid::ChordMode::kMaxEvents);
                publishChordState();

                for (int i = 0; i < n; ++i)
                {
                    if (chordOn_ && ev[i].isRoot) continue;   // bass event, not for the arp
                    if (ev[i].noteOn) arp_.noteOn  (ev[i].note, ev[i].velocity);
                    else              arp_.noteOff (ev[i].note);
                }

                if (! arpDrivesPoly || ! arpDrivesBass)
                    handleMidiMessage (msg, ! arpDrivesPoly, ! arpDrivesBass);
            }
            else
            {
                handleMidiMessage (msg);
            }
```

- [ ] **Step 4: Build and verify**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target PDHybridSynth_All 2>&1 | grep -Ei "error C|error LNK" | head; ./build/tests/Release/pdhybrid_tests.exe 2>&1 | tail -3; ./build/tools/pluginval.exe --strictness-level 8 --validate-in-process "build/PDHybridSynth_artefacts/Release/VST3/PD Hybrid Synth.vst3" 2>&1 | grep -E "SUCCESS|FAILED" | tail -1
```

Expected: no build errors, all tests pass, `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add src/plugin/PluginProcessor.h src/plugin/PluginProcessor.cpp
git commit -m "Chord mode: processor integration ahead of arp and bass"
```

---

### Task 10: ChordKeyboard display

**Files:**
- Modify: `src/plugin/Displays.h` (new class at the end, before the closing `} // namespace pdui`)

**Interfaces:**
- Consumes: `PDHybridAudioProcessor::chordHeldRoot()`, `chordVoicedNotes()` from Task 9; the `chordSplit` / `chordQuality` parameters from Task 8.
- Produces: `pdui::ChordKeyboard` with `attach (juce::AudioProcessorValueTreeState&, std::function<int()> heldRoot, std::function<int(int*,int)> voiced)`.

- [ ] **Step 1: Confirm the geometry rules before coding**

Real piano layout, and the reason the first mockup was rejected:

- seven white keys per octave, all equal width
- black keys **only** between C–D, D–E, F–G, G–A, A–B — there is no black key between E–F or B–C
- each black key is 62% of a white key's width and 62% of its height
- each black key is centred on the boundary between its two white keys

- [ ] **Step 2: Write the implementation**

Append to `Displays.h`, before the namespace close:

```cpp
//==============================================================================
/** The chord-mode keyboard: quality zone, root zone, and what is sounding.

    Draws a real piano layout -- seven white keys per octave with black keys only
    between C-D, D-E, F-G, G-A and A-B, each 62% of a white key's width and 62%
    of its height, centred on the white-key boundary. Anything else reads as
    "not a keyboard" immediately. */
class ChordKeyboard : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kOctaves   = 3;
    static constexpr int kWhitePerOct = 7;
    static constexpr int kNumWhite  = kOctaves * kWhitePerOct;

    ChordKeyboard() { setInterceptsMouseClicks (false, false); }
    ~ChordKeyboard() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s,
                 std::function<int()> heldRoot,
                 std::function<int (int*, int)> voiced)
    {
        apvts_ = &s;
        heldRoot_ = std::move (heldRoot);
        voiced_   = std::move (voiced);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "KEYBOARD");
        if (apvts_ == nullptr) return;

        static const char* kQualityNames[12] = { "maj", "min", "7", "m7", "maj7", "6",
                                                 "m7b5", "dim7", "aug", "sus2", "sus4", "m6" };
        // Semitone offset of each white key within an octave, and which
        // white-key boundaries carry a black key.
        static const int kWhiteSemi[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int kBlackAfter[5] = { 0, 1, 3, 4, 5 };   // C,D,F,G,A
        static const int kBlackSemi[5]  = { 1, 3, 6, 8, 10 };

        const int split   = juce::roundToInt (raw ("chordSplit"));
        const int quality = juce::roundToInt (raw ("chordQuality"));
        const int zoneLow = split - 12;
        const int root    = heldRoot_ ? heldRoot_() : -1;

        int sounding[8] = { 0 };
        const int nSounding = voiced_ ? voiced_ (sounding, 8) : 0;
        auto isSounding = [&] (int midi)
        {
            for (int i = 0; i < nSounding; ++i) if (sounding[i] == midi) return true;
            return false;
        };

        // Draw from the octave containing the quality zone upward.
        const int firstMidi = (zoneLow / 12) * 12;
        const float w = in.getWidth() / (float) kNumWhite;
        const float h = in.getHeight();

        g.setFont (monoF (7.5f));

        // --- white keys ---
        for (int i = 0; i < kNumWhite; ++i)
        {
            const int midi = firstMidi + (i / 7) * 12 + kWhiteSemi[i % 7];
            const juce::Rectangle<float> r (in.getX() + i * w, in.getY(), w, h);

            const bool inZone = (midi >= zoneLow && midi < split);
            const bool isSel  = inZone && (midi - zoneLow) == quality;
            const bool isRoot = (midi == root);

            g.setColour (isSel  ? juce::Colour (0xff1d5c3c)
                       : isRoot ? juce::Colour (0xff3a2a0c)
                       : inZone ? juce::Colour (0xff0b2618)
                                : juce::Colour (0xff07130d));
            g.fillRect (r);
            g.setColour (isSel ? kTrace : (isRoot ? kAmber : kEdge));
            g.drawRect (r, 1.0f);

            if (isSounding (midi) && ! isRoot)
            {
                g.setColour (kTrace.withAlpha (0.35f));
                g.fillRect (r.reduced (2.0f));
            }

            if (inZone)
            {
                g.setColour (isSel ? juce::Colour (0xffbdf5d6) : kDim);
                g.drawText (kQualityNames[midi - zoneLow],
                            r.withTrimmedBottom (2.0f), juce::Justification::centredBottom);
            }
        }

        // --- black keys ---
        const float bw = w * 0.62f, bh = h * 0.62f;
        for (int oct = 0; oct < kOctaves; ++oct)
            for (int b = 0; b < 5; ++b)
            {
                const int midi = firstMidi + oct * 12 + kBlackSemi[b];
                const float boundary = in.getX() + (oct * 7 + kBlackAfter[b] + 1) * w;
                const juce::Rectangle<float> r (boundary - bw * 0.5f, in.getY(), bw, bh);

                const bool inZone = (midi >= zoneLow && midi < split);
                const bool isSel  = inZone && (midi - zoneLow) == quality;
                const bool isRoot = (midi == root);

                g.setColour (isSel  ? juce::Colour (0xff1d5c3c)
                           : isRoot ? juce::Colour (0xff3a2a0c)
                           : inZone ? juce::Colour (0xff061a10)
                                    : juce::Colour (0xff010402));
                g.fillRect (r);
                g.setColour (isSel ? kTrace : (isRoot ? kAmber : kEdge));
                g.drawRect (r, 1.0f);

                if (isSounding (midi) && ! isRoot)
                {
                    g.setColour (kTrace.withAlpha (0.35f));
                    g.fillRect (r.reduced (1.5f));
                }
            }

        // --- split marker ---
        {
            const int idx = splitWhiteIndex (firstMidi, split);
            if (idx >= 0 && idx <= kNumWhite)
            {
                g.setColour (kAmber);
                g.fillRect (in.getX() + idx * w - 1.0f, in.getY() - 2.0f, 2.0f, h + 4.0f);
            }
        }
    }

private:
    // How many white keys sit below `split`, counting from `firstMidi`.
    static int splitWhiteIndex (int firstMidi, int split)
    {
        static const int kWhiteSemi[7] = { 0, 2, 4, 5, 7, 9, 11 };
        int idx = 0;
        for (int i = 0; i < kNumWhite; ++i)
        {
            const int midi = firstMidi + (i / 7) * 12 + kWhiteSemi[i % 7];
            if (midi < split) idx = i + 1;
        }
        return idx;
    }

    float raw (const juce::String& id) const
    {
        auto* p = apvts_->getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    void timerCallback() override { repaint(); }

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    std::function<int()> heldRoot_;
    std::function<int (int*, int)> voiced_;
};
```

- [ ] **Step 3: Build to verify it compiles**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target PDHybridSynth_All 2>&1 | grep -Ei "error C|error LNK" | head
```

Expected: no output. (The class is not instantiated yet, so this only proves it compiles.)

- [ ] **Step 4: Commit**

```bash
git add src/plugin/Displays.h
git commit -m "Chord mode: keyboard display"
```

---

### Task 11: Chord card on the VOICE tab

**Files:**
- Modify: `src/plugin/PluginEditor.h` (Section + display members)
- Modify: `src/plugin/PluginEditor.cpp` (card construction, page registration)

**Interfaces:**
- Consumes: `pdui::ChordKeyboard` from Task 10; the parameters from Task 8; `proc.chordHeldRoot()` / `proc.chordVoicedNotes()` from Task 9.
- Produces: the visible chord card.

- [ ] **Step 1: Add the members**

In `PluginEditor.h`, extend the Voice-page section list and add the display:

```cpp
    Section oscA, oscB, mixer, chordSec;                                 // Voice page
```

```cpp
    pdui::ChordKeyboard chordKeys;
```

- [ ] **Step 2: Build the card**

In `PluginEditor.cpp`, next to the other Voice-page cards (after the `mixer` block):

```cpp
    chordKeys.attach (proc.apvts,
                      [this] { return proc.chordHeldRoot(); },
                      [this] (int* out, int maxOut) { return proc.chordVoicedNotes (out, maxOut); });
    chordSec.title   = "Chord";
    chordSec.cols    = 3;
    chordSec.span    = 3;          // wide: the keyboard needs the room
    chordSec.custom  = &chordKeys;
    chordSec.customH = 76;
    chordSec.toggles = { &addToggle ("chordOn", "ON") };
    chordSec.combos  = { &addCombo ("chordVoicing", { "Voice-Led", "Root Position",
                                                      "Closed", "Drop-2", "Shell" }) };
    chordSec.knobs   = { &addKnob ("chordSplit", "Split", 0),
                         &addKnob ("chordSpread", "Spread"),
                         &addKnob ("chordOctave", "Octave", 0) };
```

The combo list must match `chordVoicing`'s five choices exactly, in order — a
shorter list silently mis-routes every entry.

Register the card on the Voice page:

```cpp
                        { &oscA, &oscB, &mixer, &chordSec, &bassSec, &pluckSec, &unison, &glideSec },
```

- [ ] **Step 3: Build and look at it**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target PDHybridSynth_All 2>&1 | grep -Ei "error C|error LNK" | head
```

Then launch the standalone and confirm on the VOICE tab: the keyboard has the
E–F and B–C gaps, the quality zone is tinted with its twelve names, the amber
split marker sits on the zone boundary, and turning `ON` plus playing a root
lights the held key amber.

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "build/PDHybridSynth_artefacts/Release/Standalone/PD Hybrid Synth.exe"
```

- [ ] **Step 4: Verify**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && ./build/tests/Release/pdhybrid_tests.exe 2>&1 | tail -3; ./build/tools/pluginval.exe --strictness-level 8 --validate-in-process "build/PDHybridSynth_artefacts/Release/VST3/PD Hybrid Synth.vst3" 2>&1 | grep -E "SUCCESS|FAILED" | tail -1
```

Expected: all tests pass, `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add src/plugin/PluginEditor.h src/plugin/PluginEditor.cpp
git commit -m "Chord mode: card on the VOICE tab"
```

---

### Task 12: Factory presets and final verification

**Files:**
- Modify: `src/plugin/PresetManager.cpp`
- Modify: `tests/bench/bench_main.cpp` (voice-budget line)

**Interfaces:**
- Consumes: everything.
- Produces: two factory presets, and a benchmark line covering chord × unison.

- [ ] **Step 1: Add the presets**

In `createFactoryPresetsIfNeeded`, alongside the other groups:

```cpp
    // --- Chord mode ---
    ensure ("Chord/Organ Comp", { {"chordOn",1}, {"chordSplit",60}, {"chordVoicing",0},
        {"chordSpread",0.3f}, {"oscAType",0}, {"cutoff",6000}, {"attack",0.01f},
        {"decay",0.3f}, {"sustain",0.85f}, {"release",0.3f}, {"bassOn",1},
        {"bassLevel",0.7f}, {"chorusOn",1} });
    ensure ("Chord/Arp Progression", { {"chordOn",1}, {"chordSplit",60}, {"chordVoicing",3},
        {"chordSpread",0.6f}, {"arpOn",1}, {"arpRate",4}, {"arpTarget",0},
        {"oscAType",9}, {"cutoff",7000}, {"sustain",0.8f}, {"release",0.4f},
        {"delayMix",0.25f}, {"reverbOn",1} });
```

- [ ] **Step 2: Add the benchmark line**

In `bench_main.cpp`, in the gain-structure section, add a chord-shaped case so the
voice budget stays visible:

```cpp
    std::printf ("  chord-shaped load (4 notes at once)\n");
    for (int uni : { 1, 3, 6 })
    {
        const auto lv = measureConfig (4, uni, 1.0, 0.0, 0.0, 0.0);
        std::printf ("    4-note chord x %d unison = %2d voices   peak %6.2f   rms %6.3f\n",
                     uni, 4 * uni, lv.peak, lv.rms);
    }
```

- [ ] **Step 3: Full verification**

```bash
cd "D:/Claude Code/PD Hybrid VST Idea" && "/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target PDHybridSynth_All pdhybrid_tests pdhybrid_bench 2>&1 | grep -Ei "error C|error LNK" | head
./build/tests/Release/pdhybrid_tests.exe 2>&1 | tail -3
./build/tests/Release/pdhybrid_bench.exe 6 2>&1 | sed -n '/chord-shaped/,+4p'
./build/tools/pluginval.exe --strictness-level 8 --validate-in-process "build/PDHybridSynth_artefacts/Release/VST3/PD Hybrid Synth.vst3" 2>&1 | grep -E "SUCCESS|FAILED" | tail -1
```

Expected: no build errors, all tests pass, benchmark prints the chord rows with
no case over the block budget, `SUCCESS`.

- [ ] **Step 4: Manual check in the standalone**

Launch the standalone and confirm, with `ON` lit:

- pressing a quality-zone key makes no sound but lights that key green
- pressing a root plays the chord and lights the root amber
- tapping a different quality while holding the root morphs the chord without
  restarting the notes it has in common
- with the mono bass on, the bass follows the chord root
- with the arp on, the arp runs over the chord notes

- [ ] **Step 5: Commit**

```bash
git add src/plugin/PresetManager.cpp tests/bench/bench_main.cpp
git commit -m "Chord mode: factory presets and voice-budget benchmark"
```

---

## Self-Review

**Spec coverage:** Zones (T2) · latching (T2) · one root at a time (T7) · live
re-voicing (T4) · common tones (T4) · velocity (T2, `heldVel_`) · architecture and
`isRoot` routing (T9) · quality table (T1) · all five voicing modes (T3, T5) ·
spread / octave / clamp (T6) · parameters (T8) · UI (T10, T11) · edge cases
(T7, T9) · testing (throughout) · benchmark (T12). No gaps.

**Placeholder scan:** none — every step carries the code it needs.

**Type consistency:** `Event { bool noteOn; int note; float velocity; bool isRoot; }`
is used identically in T2, T4, T7, T9. `buildVoicing (int root, int* out) const`
and `emitChange (const int*, int, int, Event*, int)` keep the same signatures from
T2 through T6. `kMaxEvents`, `kMaxChordNotes`, `kNumQualities` are used
consistently. `refresh` / `revoice` / `flush` / `voicedNotes` are each defined once
and referenced with matching names.

**Two things a reviewer should watch:**

1. **Task 6 restructures `buildVoicing`'s control flow** from early `return n;` in
   each branch to if/else-if/else falling through to a shared tail. If the branches
   from Tasks 3 and 5 are left with their `return` statements, spread and octave
   silently never apply — and the tests in Task 6 are what catch it.
2. **Task 9 replaces a function-local `static int lastSplit`** with the member
   `chordLastSplit_`. A static there is shared across plugin instances, so two
   instances in one session would flush each other.
