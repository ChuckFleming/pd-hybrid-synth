# Chord Mode — Design

Date: 2026-07-26
Status: approved, ready for implementation planning

## Purpose

A chord mode modelled on old electric chord organs and the Telepathic Instruments
Orchid. The keyboard splits into two zones: a one-octave **quality zone** whose
keys latch a chord type, and a **root zone** above it whose keys set the root.
Pressing a root plays the full chord, voiced automatically.

The goal is for someone who cannot voice chords by hand to play progressions that
sound deliberate — which means the voicing engine, not the note-mapping, is the
part that has to be good.

## Behaviour

### Zones

- **Quality zone**: exactly one octave, ending one semitone below `chordSplit`.
  With the default split of C3 (60) the quality zone is C2–B2 (48–59). Its twelve
  semitones are the twelve chord types.
- **Root zone**: `chordSplit` and above. Each key sets the chord root.
- Notes below the quality zone are passed through unchanged, so the bottom of the
  keyboard still plays normally.

### Latching

Pressing a quality key **sets** the latched quality; it stays set until a
different quality key is pressed. Quality keys never sound. Roots always play a
chord — there is no "no quality selected" state, because the latch always holds a
value (default `maj`).

### One root at a time

Pressing a new root while another is held replaces it. The previous chord's notes
are released except for those the new chord also contains (see *Common tones*).

### Live re-voicing

Changing the latched quality while a root is held re-voices the sounding chord
immediately. Holding C and tapping maj → m7 → maj7 morphs the chord under the
hand. This applies however the quality changed — a quality key, or the
`chordQuality` parameter moving under host automation.

### Velocity

Every note of a chord takes the velocity of the root key that triggered it. A
re-voice caused by a quality change reuses the held root's original velocity, so
morphing a chord does not change its level.

### Common tones

When the chord changes — new root or new quality — the new voicing is diffed
against the old:

- notes present in both are **left sounding**, with no note-off/note-on pair
- notes only in the old voicing get a note-off
- notes only in the new voicing get a note-on

Envelopes on unchanged notes are therefore never restarted. This is what makes
re-voicing feel like an instrument rather than a stutter, and it is the behaviour
the voice-leading modes exist to maximise.

## Architecture

`ChordMode`, a new class in `src/dsp/`. Pure C++, no JUCE, fixed-capacity
storage, no audio-thread allocation — the same shape as `Arpeggiator`, which is
this codebase's established pattern for a note processor and which keeps the
whole thing testable headless.

It sits in the processor's MIDI path **ahead of** the arpeggiator and the
mono-bass tap-off:

```
MIDI note
   |
   v
ChordMode   -- note <  split  -->  quality zone: latch, re-voice any sounding chord
   |         -- note >= split  -->  root zone:   set root, emit voiced chord
   v
emitted events --> Arpeggiator (if on) --> poly / bass  (per arpTarget)
               \-> if arp off:  poly <- all chord notes
                                bass <- root-flagged event only
```

Each emitted event carries an `isRoot` flag. With the arp **off**, the processor
sends every event to the poly voices and only the root-flagged one to the mono
bass, giving the organ-plus-bass-pedal behaviour. With the arp **on**, the arp
pool receives the whole chord and the existing `arpTarget` (Poly / Bass / Both)
decides where the arpeggio lands. The two features compose with no special case.

### Interface

```cpp
struct Event { bool noteOn; int note; float velocity; bool isRoot; };

void setEnabled   (bool) noexcept;
void setSplitNote (int midiNote) noexcept;
void setQuality   (int index) noexcept;        // 0..11, latched
void setVoicing   (int mode) noexcept;         // 0..4
void setSpread    (double spread01) noexcept;
void setOctave    (int octaves) noexcept;      // -2..+2

int  handleNoteOn  (int note, float vel, Event* out, int maxOut) noexcept;
int  handleNoteOff (int note,            Event* out, int maxOut) noexcept;
int  flush         (Event* out, int maxOut) noexcept;   // release everything

// Read-only, for the editor's display.
int  latchedQuality() const noexcept;
int  heldRoot()       const noexcept;          // -1 when nothing held
int  voicedNotes (int* out, int maxOut) const noexcept;
```

`handleNoteOn`/`handleNoteOff` return how many events they wrote. `maxOut` of 16
is ample: the largest chord is 4 notes, so a change emits at most 4 offs + 4 ons.

## Quality table

| key | quality | intervals | | key | quality | intervals |
|-----|---------|-----------|-|-----|---------|-----------|
| C   | maj     | 0 4 7     | | F#  | m7b5    | 0 3 6 10  |
| C#  | min     | 0 3 7     | | G   | dim7    | 0 3 6 9   |
| D   | 7       | 0 4 7 10  | | G#  | aug     | 0 4 8     |
| D#  | m7      | 0 3 7 10  | | A   | sus2    | 0 2 7     |
| E   | maj7    | 0 4 7 11  | | A#  | sus4    | 0 5 7     |
| F   | 6       | 0 4 7 9   | | B   | m6      | 0 3 7 9   |

The quality index is the semitone offset from the bottom of the quality zone.

## Voicing

Common inputs: `root` (MIDI note played), the quality's interval set, and the
previous voicing (absolute MIDI notes, empty on the first chord).

Pitch classes are `pc_i = (root + interval_i) mod 12`.

### Mode 0 — Voice-Led (default)

1. **First chord of a sequence** (no voicing history): root position, i.e.
   `root + interval_i`. Centring on the played root instead — as an earlier
   draft of this spec said — places some chord tones *below* it: press C and the
   fifth lands under it, so the key you pressed is not the bass. Root position
   makes the played key audibly the root.
2. **Every chord after**: place each pitch class in the octave nearest the
   previous voicing's centre,
   `note_i = pc_i + 12 * round((centre - pc_i) / 12.0)`, then sort and dedupe.
3. The centre is carried forward as the mean of the voicing just produced.

No explicit common-tone logic is needed: a pitch class already sounding near the
centre resolves to the same absolute note, so common tones hold automatically.

**The voicing centre outlives the sounding notes.** Releasing a chord and playing
the next one is still a progression, so history persists across note-offs rather
than resetting to root position every time a key is lifted. Only a `flush` — mode
toggled, or the split moved — clears it.

### Mode 1 — Root Position

Notes are `root + interval_i` directly, using the played root's own octave. The
chord follows the hand up and down the keyboard. This is the authentic chord
organ behaviour.

### Mode 2 — Closed

Fixed register, no history. With `base = 54` (an octave centred on C4), each
pitch class is placed as `base + ((pc_i - base) mod 12)`, then sorted. Always
lands in the same octave. `chordOctave` is deliberately *not* used here — it is
applied once at the end, for every mode.

### Mode 3 — Drop-2

Compute the Closed voicing, then lower the second-from-top note by 12 semitones
and re-sort. For a 3-note chord this is the middle note.

### Mode 4 — Shell

Drop interval 7 (the fifth) from the interval set **when the chord has four or
more notes**; triads are left intact, having nothing useful to drop. The reduced
set is then placed as Voice-Led. Saves one voice per seventh chord.

### Spread (all modes)

With the voicing sorted ascending as `v[0..n-1]` and `k = round(spread * (n-1))`:
lift `v[1] .. v[k]` by 12 semitones, then re-sort. `spread = 0` leaves the
voicing closed; `spread = 1` raises every voice above the bass by an octave.

### Octave

`chordOctave` transposes the finished voicing by whole octaves. Applied last.

### Range clamp

Any note falling outside MIDI 0–127 after all of the above is transposed by whole
octaves until it fits.

## Parameters

All appended to the APVTS, so existing presets are unaffected.

| id | type | range | default |
|----|------|-------|---------|
| `chordOn`      | bool   | —              | off        |
| `chordSplit`   | int    | 36–84          | 60 (C3)    |
| `chordQuality` | choice | 12 entries     | maj        |
| `chordVoicing` | choice | 5 entries      | Voice-Led  |
| `chordSpread`  | float  | 0–1            | 0.4        |
| `chordOctave`  | int    | −2…+2          | 0          |

`chordQuality` is a real parameter rather than hidden state, so the latched chord
saves with the preset and can be automated — which gives chord progressions from
a host automation lane.

## UI

One wide card on the **VOICE** tab, following the existing card conventions.

- **Title strip:** `ON` toggle.
- **Display:** a new `ChordKeyboard` class in `Displays.h`, alongside the other
  live displays. Draws a real piano keyboard — seven white keys per octave, black
  keys only between C–D, D–E, F–G, G–A and A–B, each black key 62% of a white
  key's width and 62% of its height, centred on the white-key boundary. Shows:
  - quality names printed on the twelve quality-zone keys
  - an amber split marker on the zone boundary
  - the latched quality lit green
  - the held root lit amber (the colour this UI already uses for "modulation
    lands here")
  - the chord name and the resulting voiced notes spelled out beneath
- **Controls:** `Voicing` combo, then `Split`, `Spread`, `Octave` knobs.

The display needs the live held root and voiced notes, which only the audio
thread knows. The processor publishes them through relaxed atomics and the editor
polls on its existing timer — the same mechanism the modulation meters use.

## Edge cases

- **Split moved while notes held** — flush the chord layer (all sounding chord
  notes released), following the precedent already set for arp-target changes.
  Otherwise a note can hang on the wrong side of the boundary.
- **Chord mode toggled off while notes held** — same flush.
- **Root released** — release the whole chord.
- **Quality key released** — nothing happens; the latch persists by design.
- **Note below the quality zone** — passed through unchanged.
- **Voice budget** — a 4-note chord with unison at 6 wants 24 voices against a
  16-voice budget. The existing `if (n > lim) n = lim` clamp prevents anything
  breaking, but chords and wide unison will steal voices from each other. Not
  fixed here; documented so it is a known trade-off rather than a surprise.

## Testing

`ChordMode` is pure C++, so its coverage is headless Catch2:

- quality table intervals match the table above
- split routing: below the split latches a quality and emits **nothing**; at or
  above the split emits the chord; below the quality zone passes through
- Voice-Led: consecutive chords share common tones **at identical absolute pitch**
- common tones emit no note-off/note-on pair — the assertion that protects the
  "does not retrigger" behaviour
- each voicing mode produces its documented shape (fixtures per mode)
- spread and octave are monotonic; spread 0 is closed
- all output stays within MIDI 0–127
- deterministic, no allocation, event counts bounded by `maxOut`
- moving the split or disabling chord mode flushes held notes

Integration coverage: chord + arp compose (arp pool receives chord notes); bass
receives only the root with the arp off. A benchmark line for chord × unison
against the voice budget.

## Out of scope

- Multiple simultaneous roots / polychords — one root at a time, by decision.
- User-assignable quality slots — the twelve types are fixed.
- Strum or arpeggiate-within-chord timing — the existing arpeggiator covers this.
- Inversion control as a separate parameter — the voicing modes cover it.
