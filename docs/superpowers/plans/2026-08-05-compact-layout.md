# Compact the PD Hybrid editor to 1280 × 800

## Context

The editor currently opens at **1348 × 1382** — taller than a 1080p screen, so it cannot be seen at once on the target display. This is not an accident; it is an explicit policy in `PluginEditor.cpp` (~line 2279):

```cpp
// Open at whatever size the *tallest* page actually needs, so nothing has to
// be scrolled to be reached.
```

The window grows to fit its tallest page. Combined with a row-based grid, that produces the wasted space visible on every tab: **each card in a row is padded to the height of the tallest card in that row.** Mixer is half empty because Mono Bass sits beside it; Pluck and Glide are half empty because Unison sits beside them. The VOICE page also leaves a whole empty column to the right of Glide.

**Goal:** VOICE (and every other page) fits inside 1280 × 800 with no scrolling, comfortably usable on 1920 × 1080 alongside a DAW window.

**Decisions already made:** target 1280 × 800; Inspector becomes a collapsible drawer; **no scrolling** — every page must fit.

## Why column-packing, not just tightening

Budget at 1280 × 800: `800 − topbar 40 − strip 110 − tab bar 26 − footer 20 − margins 12` ≈ **592 px of page content**. VOICE needs ~1000 px today.

Shrinking knobs and padding alone reaches ~700 px — still over, and it makes controls small for no structural gain. The waste is the row grid, not the cell size.

Summing VOICE's cards at their *natural* heights gives ~1122 px of card area. Packed into **three columns** that is ~374 px per column — comfortably inside 592. So the reflow is what buys the fit, and it means knobs only need to come down from 64 px to ~48 px rather than to 36 px.

## Approach

**1. Replace the fixed 6-column row grid with a 3-column shortest-column pack.**
In `PluginEditor.cpp`, `SectionPanel::layout()` (~lines 160–381) currently computes `rowY[]`/`rowHeight[]` and pads every card in a row to `rowHeight`. Replace with: maintain a running height per column, and place each card into the currently shortest column at its own natural height. `sectionHeight()` and `placeCard()` are reused unchanged — only the placement loop changes.

Card order within a column follows the existing `layout` vector order, so related cards stay adjacent. `Section::span` is reinterpreted as a *minimum column count* (1 or 2 of the 3), letting wide cards like Mono Bass still take two columns.

**2. Tighten the cell metrics** (constants at the top of `PluginEditor.cpp`, lines 5–22):

| Constant | Now | New |
|---|---|---|
| `kKnobH` / `kKnobW` | 64 | 48 |
| `kLabelH` | 16 | 13 |
| `kCellW` | 72 | 58 |
| `kHeaderH` | 24 | 19 |
| `kComboRowH` | 24 | 21 |
| `kCardPad` | 8 | 6 |
| `kGap` | 8 | 6 |
| `kMargin` | 16 | 10 |
| `kTopBar` | 48 | 40 |
| `kStripH` | 150 | 110 |
| `kFooterH` | 24 | 20 |
| `kGridCols` | 6 | 3 |

`kCellH` is derived (`kLabelH + kKnobH`) and falls 80 → 61.

### Two corrections from measuring the mockups

Both were found by measuring the rendered mockups rather than computing them, and both invalidate part of the arithmetic above.

**A knob cell is ~78 px tall, not 61.** `kKnobH` covers the rotary *and* the value text box beneath it; `kLabelH` is the caption on top. At a 48 px rotary the real cell is roughly `48 + 14 (value) + 13 (caption) + gaps ≈ 78`. Every card is therefore ~17 px per knob row taller than the table implies. The pages still fit — the mockups are built at the honest 78 px and all five clear the footer — but **do not trust the 61 px figure when sizing anything.** If `kKnobH` must include the text box, either keep 48 and accept a 78 px cell (what the mockups show, and what is verified to fit) or drop the rotary to ~34 px to actually land at 61.

**A row's height is floored by the tallest knob cell in it.** Shrinking a display next to a knob does nothing: reducing the stage-envelope graph from 100 → 64 px changed the card height by zero, because the Amt knob beside it set that row. When trimming a card, shrink the *control* next to the display, not the display.

**3b. Full-width bands are required, not optional.** The MOD page's stage-envelope card carries 18 controls per bank (R1–R8, L1–L8, Amt, Sus Pt) across three banks. Sixteen knobs in a row need ~464 px; a column is 415 px. That card cannot fit in one column at any knob size, so the packer **must** support a card that spans the full content width and takes its own band. A card whose `span` equals the column count gets a full-width row to itself, and the remaining cards pack into columns beneath it. Watch the JUCE equivalent of the CSS bug this exposed: the band must be stretched to the content width, not sized to its own content, or its knob row wraps and the page overflows.

**3c. Add the chord readout.** A 415 × 76 px display well below the Chord card on VOICE: the chord name at ~29 px, the sounding notes, and a one-octave strip lighting the sounding pitch classes. Fed entirely by the existing `pdtheme`-adjacent `ChordNamer::name()` and `PDHybridAudioProcessor::soundingNotes()` — no DSP work. Idle state shows a dim dash rather than emptying, so it does not pull the eye on every note-off.

### Measured clearance (footer to lowest card)

| Tab | Slack |
|---|---|
| VOICE | 14 px (with the chord readout) |
| SHAPE | 220 px |
| MOD | 35 px (worst case, NUMERIC expanded) |
| OUT | 183 px |
| GLOBAL | 197 px |

VOICE and MOD are the binding pages. Anything that grows a card on those two must be checked against these numbers.

**3. Make the Inspector a collapsible drawer.**
`kInspW` (line 1365) is a permanent 236 px reserved in `resized()` (line 2733). Make it hidden by default with a toggle in the top bar; when open it overlays the right-hand side rather than shrinking the page. Reclaims 236 px of width on every page.

**4. Replace the grow-to-fit sizing with a fixed default size.**
The block at ~line 2277 becomes `setSize (1280, 800)` with `setResizeLimits (1100, 700, 2600, 1600)`. The existing `ScrollPanel` viewport stays as a last-resort safety net for very small window sizes, but no page should reach it at the default size.

**5. Shrink the oversized custom displays** so they stop driving card height:
`kStripCurveH` 42 → 34, `kEnvGraphH` 118 → 88, `kEnvSelH` 32 → 26, `kEnvKnobH` 62 → 50, the osc cycle preview `customH` 56 → 42, and the chord keyboard height in the Chord card.

## Step 1 — mockup first (approve before any C++)

Build `docs/design/voice-1280.html` showing the VOICE page at exactly 1280 × 800 in the Panel 1985 skin, reusing the panel CSS already in `docs/design/panel-1985.html`. It must show all eight real cards (Osc A, Osc B, Mixer, Mono Bass, Pluck, Unison/Drift, Glide, Chord) with their real controls at the new metrics, packed into three columns, with the Inspector collapsed and a visible 1280 × 800 boundary so the fit is verifiable by eye.

Publish it as an Artifact for review. **No C++ changes until that is approved** — the mockup is cheap to iterate, the layout code is not.

## Files to modify

- `src/plugin/PluginEditor.cpp` — metric constants (5–22), `SectionPanel::layout()` (160–381), `kInspW` (1365), Inspector placement in `resized()` (~2733), window sizing (~2277), display-height constants (441–446)
- `src/plugin/PluginEditor.h` — Inspector toggle button member
- `src/plugin/Displays.h` — internal padding of `ChordKeyboard`, `EnvelopeCurve`, `WaveCyclePreview` where it forces a minimum height

No DSP file changes. No parameter changes. `src/theme/` and the theme layer are untouched.

## Verification

1. `cmake --build build --config Release` clean, then `./build/tests/Release/pdhybrid_tests.exe` — expect 302 test cases passing (layout is untested, but nothing here may break DSP).
2. Launch the standalone and confirm the window opens at exactly 1280 × 800.
3. **Visit all five tabs and confirm none scrolls** — this is the acceptance criterion. Any page whose content exceeds the viewport is a failure, not a nitpick.
4. Confirm in both themes (Panel 1985 and Phosphor Mk II) — tighter padding is where a light skin's contrast problems would show.
5. Resize to the minimum (1100 × 700) and confirm nothing overlaps or clips.
6. `./build/tools/pluginval.exe --strictness-level 8 --validate-in-process --validate "build/PDHybridSynth_artefacts/Release/VST3/PD Hybrid Synth.vst3"` — expect SUCCESS.
7. Load a pre-existing preset and confirm it still loads and sounds identical.

**Note:** screen capture is currently blocked by Bitdefender on this machine, so I cannot take the verification screenshots myself. Steps 2–5 will need you to look at the built plugin, or for the block to be lifted.
