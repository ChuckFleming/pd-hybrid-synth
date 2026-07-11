# Feature Roadmap — PD Hybrid Synth
Date: 2026-07-11. This is a **specification document — no code has been changed**. It lists the next generation of features (v6.x) with concrete implementation guidance grounded in this codebase. It is written to be handed to a developer (or model) with no prior context on this project.

User-stated priority: **voice modes & allocation control, including changeable polyphony** (§1). Emphasis otherwise is spread evenly across CZ authenticity (§2), modern FX (§3), performance features (§4), and sound-design depth (§5).

---

## 0. How this repo works — read before implementing anything

**Architecture.** DSP lives in `src/dsp/` as pure C++ (no JUCE) so the offline test harness can drive it. The JUCE wrapper is `src/plugin/PluginProcessor.{h,cpp}` (parameters, MIDI, master FX chain) and `src/plugin/PluginEditor.{h,cpp}` (tabbed card-based editor). Per-voice DSP is `src/dsp/Voice.{h,cpp}`, voice allocation is `src/dsp/SynthEngine.{h,cpp}`, the per-block parameter snapshot is `src/dsp/SynthParams.h`.

**The parameter-wiring recipe** (follow it for every new parameter):
1. Add the parameter in `PluginProcessor::createLayout()` (`src/plugin/PluginProcessor.cpp`) — use the existing formatter lambdas (`pct`, `hz`, `db`, `sec`, `cnt`, `oct`, `rate`, …).
2. Read it in `PluginProcessor::pushParams()` into a `SynthParams` field (add the field to `src/dsp/SynthParams.h`) — or, for master-bus stages, configure the stage directly (see how `delay`/`compressor`/`globalEq`/`master` are set there).
3. Consume it in `Voice` / `SynthEngine` / the processor stage.
4. Add the control to the editor: a knob via `addKnob("paramId", "Label")` or combo via `addCombo("paramId", {...})` inside `PDHybridEditor::buildSections()` (`src/plugin/PluginEditor.cpp`), assign it to a Section card, and add the card to a tab page in the `layout` vector in the constructor.

**⚠ ComboBox gotcha (caused a real shipped bug):** every editor combo's item list must match the APVTS choice parameter's length and order **exactly**. JUCE maps by `index/(itemCount-1)`; a shorter list silently mis-routes every selection. When you add a `ModSource`/`ModDest` or any choice value, update `PluginProcessor.cpp` *and* the matching list in `PluginEditor.cpp`.

**Testing.** Catch2 tests in `tests/dsp/*.cpp`; register new files in `tests/CMakeLists.txt`. New DSP `.cpp` files must be added to the `pdhybrid_dsp` target in the root `CMakeLists.txt`. Harness helpers: `tests/harness/FrequencyResponse.h` (gain sweeps), `tests/harness/Spectrum.h` (FFT, THD, aliasing energy), `tests/harness/SignalStats.h` (`peakAbs`, `rms`, `hasBadValues`). Current suite: **127 cases — keep them all green**.

**Verification per feature:** build + run `build/tests/Release/pdhybrid_tests.exe`; rebuild VST3; run `build/tools/pluginval.exe --strictness-level 8 --validate-in-process "<path to .vst3>"`. Build with **Bash** (the repo's convention on this machine), not PowerShell. CMake path used here: `/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`, generator "Visual Studio 17 2022", config Release.

**Performance rules** (see `PERFORMANCE_ANALYSIS.md`, STATUS block):
- **Never allocate on the audio thread** (reserve in `prepare`/`setSampleRate`).
- Guard transcendental coefficient redesigns behind changed-value checks (pattern: `OscEq::setGains`, `GlobalEq::setBand`, `LadderFilter::setCutoff`).
- Prefer 32-sample control-rate evaluation over per-sample (pattern: `Voice::renderBlock`, `MonoBass::renderBlock`).
- Skip work when a stage is transparent (pattern: `OscEq` bypass, `Compressor` early-out, `monoBass.enabled()` gate).

---

## 1. Voice modes & polyphony — USER PRIORITY, implement first

Today `SynthEngine` (`src/dsp/SynthEngine.{h,cpp}`) is always 16-voice poly; `kMaxVoices = 16` is both the array size and the effective polyphony. Unison (up to 6 sub-voices per note) allocates from the same pool. There is no mono/legato mode (only the separate `MonoBass` layer is monophonic).

### 1a. Changeable polyphony
- **Param:** `polyphony` (AudioParameterInt, 1–16, default 16).
- **Engine:** keep `kMaxVoices = 16` as the array size. Add `int activePolyphony_ = 16;` + setter. In `allocateVoice()` only consider indices `< activePolyphony_`. Additionally, in `noteOn`, after allocation count *held notes* (not voices — a 6-unison note is one note) and steal the oldest held note's whole stack when the count exceeds a sensible note limit (`polyphony / unisonVoices`, min 1) so unison doesn't silently eat the pool.
- **SynthParams:** `int polyphony = 16;` read in `pushParams`, pushed via `engine.setParams` → engine copies it to `activePolyphony_` in `setParams`.
- **Tests:** with polyphony=2, playing 3 notes leaves ≤2 sounding stacks; with polyphony=16 behavior unchanged.

### 1b. Voice mode (Poly / Mono / Legato / Unison-Legato)
- **Param:** `voiceMode` (choice {"Poly", "Mono", "Legato", "Unison Legato"}, default Poly).
- **Engine changes** (`SynthEngine::noteOn`/`noteOff`):
  - Maintain a held-note stack (press order) exactly like `MonoBass::held_` (`src/dsp/MonoBass.cpp` — reuse this proven logic; either extract a small `HeldNoteStack` helper into a new header or replicate it). **Reserve capacity in `setSampleRate`** (no audio-thread allocation).
  - **Mono:** one stack (1 or N-unison voices) total; every note-on retriggers envelopes (`Voice::start`) at the new pitch, glide per `glideMode`. Note-off falls back to the next held note per note priority.
  - **Legato:** same, but when a note is already held, change pitch **without retriggering envelopes**. Needs a new `Voice::changeNote(int note, double glideFromHz, double glideSamples)` that updates `note_`, `glideTargetHz_`/glide state and calls `applyModulation()` but does **not** call `env_.noteOn()` etc. (contrast with `Voice::start`, `src/dsp/Voice.cpp`, which retriggers everything).
  - **Unison-Legato:** legato behavior with the unison stack (all stack voices `changeNote` together).
- **Note priority param:** `notePriority` (choice {"Last", "Top", "Bottom"}) used on note-off fallback — same selection logic as `MonoBass::selectNote`.
- **Retrigger param:** `monoRetrigger` (bool, default on) — distinguishes Mono (retrig) from "true legato on fallback": when off, the fallback note-change also uses `changeNote`.
- **Steal policy param:** `stealPolicy` (choice {"Oldest", "Quietest"}). Current logic (oldest, prefer released — `SynthEngine::allocateVoice`) stays "Oldest"; "Quietest" picks the active voice with the lowest `env_.level()` (add a `double envLevel() const` accessor to `Voice` that returns `env_.level()`).
- **Editor:** new "Voice" card on the Oscillators tab: combos voiceMode/notePriority/stealPolicy + polyphony knob (0 decimals) + retrigger toggle combo ({"Retrig Off","Retrig On"}).
- **Tests (SynthEngine-level, `tests/dsp/synth_tests.cpp` style):** legato note-change does not restart the amp envelope (render, change note mid-note, assert no attack dip); mono mode never has >1 sounding note stack; priority fallback picks correct note; quietest-steal steals the released voice first.
- **Interaction notes:** glide (`glideMode` Legato) already exists and composes naturally; `MonoBass` keeps its own independent mono logic — do not merge.

---

## 2. CZ authenticity features

Background: the original Casio CZ voice had, per line (osc), **three independent 8-stage envelopes** — DCO (pitch), DCW (waveshape depth), DCA (amp) — plus line ring/noise modulation, wave combination, and (CZ-3000/5000) chorus. We currently have one CZ-style 8-stage env (`multiEnv_`, routed to filter cutoff + mod matrix), classic ADSRs elsewhere, and no ring/noise mod.

### 2a. 8-stage pitch envelope (DCO envelope) — biggest missing CZ element
- Add a **fourth** `MultiStageEnvelope` instance to `Voice` (`pitchEnv_`), configured exactly like `multiEnv_` (8 rate/level stages + sustain index — see the `czRate`/`czLevel` param pattern in `createLayout` and the stage-building loop in `Voice::setParams`, which now uses the preallocated `czStages_` array — copy that pattern with a second array).
- **Params:** `pitchEnvRate1..8`, `pitchEnvLevel1..8`, `pitchEnvSustain` (Int 1–8), `pitchEnvAmount` (float, ±48 semitones, default 0, formatter `cnt`-like but semis). Levels are 0..1 but interpreted bipolar: `(level - 0.5) * 2 * amount` semis, so 0.5 = no offset (document this in the param tooltip/name).
- **Routing:** add `pitchEnvAmount * (pitchEnv_.level() - 0.5) * 2.0` into the `semis` sum in `Voice::applyModulation` (the block that already sums pitchBend/drift/detune).
- Trigger in `Voice::start` (`pitchEnv_.noteOn()`), release in `Voice::release`, advance per-sample alongside the other envelopes in `renderBlock`'s inner loop (or per-chunk if envelope `advance(n)` has been added by then).
- Also add `ModSource::PitchEnv` (remember the ComboBox gotcha — update `kSrcNames` in **both** PluginProcessor.cpp and PluginEditor.cpp).
- **Editor:** new "Pitch Env (CZ)" card on the Envelopes tab, 8 columns like the existing Multi-Stage card.
- **Tests:** with a rising pitch env, dominant frequency early in the note < later (or use two renders + `Spectrum::peakFrequency` on windows).

### 2b. Per-osc DCW (waveshape) envelope
Two tiers — implement the cheap one first:
- **Cheap:** per-osc *amount* params (`oscACzDcw`, `oscBCzDcw`, bipolar ±1) that route the **existing** `multiEnv_` into each osc's PD amount in `applyModulation` (add `multiEnv_.level() * amount` per unit before the clamp). Zero new DSP.
- **Full (later):** a dedicated second 8-stage env (`dcwEnv_`) with its own rate/level params, replacing the cheap routing. Same construction pattern as 2a.

### 2c. Ring modulation (CZ line ring-mod)
- **Param:** `ringModLevel` (0..1, default 0).
- **Implementation:** in `Voice::renderOneSample` (`src/dsp/Voice.cpp`), Osc B's sample is currently computed only when `oscBLevelMod_ > 1e-5`. Restructure: compute `sB` when `oscBLevelMod_ > tiny || ringLevel > tiny`; then `s += sA * sB * ringLevel` (classic ring product). Skip entirely at 0 (perf pattern). Ring level should be modulatable later — add `ModDest::RingMod` only if trivial, else defer.
- **Test:** ring of two sines at f1, f2 puts energy at f1±f2 (use `Spectrum::magnitudeNearHz`).

### 2d. Noise modulation
- **Param:** `noiseModDepth` (0..1). CZ "noise" line modulated pitch chaotically.
- **Implementation:** in `applyModulation` (control-rate is fine and cheap): add `noiseModDepth * noise * kScale` semis to the pitch sum, where `noise` is a fresh LCG value per control chunk (reuse the `rng_` LCG idiom already in `Voice`). kScale ≈ 12 semis at full depth. This gives the characteristic CZ noise-pitch grit without per-sample cost.

### 2e. Wave combine (alternating waveforms per cycle)
- CZ DCOs could alternate two waveforms on successive cycles.
- **Params per osc:** `oscAWave2` (same choice list as `oscAWave`), `oscACombine` (bool). Same for B.
- **Implementation:** in `PhaseDistortionOscillator` (`src/dsp/PhaseDistortionOscillator.{h,cpp}`): store `waveB_` + `combine_`; the phase accumulator wraps in `coreSample`/the phase-increment site — on each wrap, if `combine_`, toggle which wave renders the next cycle. Keep the toggle at the *core* (oversampled) cycle boundary so there is no discontinuity.
- **Test:** combined saw+square spectrum contains both waves' signatures; period doubles (energy at f/2 partials appears) — assert energy near f/2 with combine on, absent with it off.

### 2f. Pitch-bend range parameter (quick win)
- `pitchBendRangeSemis` is a **hardcoded member** (`= 2.0`) in `src/plugin/PluginProcessor.h`, used in `handleMidiMessage`. Make it a param `bendRange` (Int 1–24, default 2), read in `pushParams`. Editor: add to the new Voice card (§1) or Glide card.

---

## 3. Modern FX

Current master chain in `processBlock` (`src/plugin/PluginProcessor.cpp`): voices → global-mod → **compressor → delay → globalEq → master(level+limiter)**, each gated by an `*On` param except master. Target chain: **compressor → chorus → delay → reverb → globalEq → master**. A user-reorderable FX matrix is explicitly **out of scope** (high complexity, low payoff).

### 3a. Chorus / Ensemble (also a CZ-3000/5000 authenticity item)
- **New DSP:** `src/dsp/Chorus.{h,cpp}` — pure C++, no JUCE. Stereo: per channel 2 modulated fractional-delay taps (~5–25 ms base, LFO depth ~2–6 ms), LFOs in quadrature between channels for width. Reuse the interpolation + power-of-two + mask buffer pattern from `Delay::readFrac` (`src/dsp/Delay.cpp`). Reuse `pdhybrid::Lfo` for the mod LFOs (sine).
- **API mirror `Delay`:** `setSampleRate/reset/setRate(Hz)/setDepth(0..1)/setMix(0..1)/setMode(I, II, I+II)` + `processStereo(float*, float*, int)`. Mode I = 1 tap slow, II = 1 tap faster/deeper, I+II = both (CZ chorus lineage).
- **Params:** `chorusOn` (bool, default off), `chorusMode` (choice), `chorusRate` (0.05–5 Hz), `chorusDepth` (pct), `chorusMix` (pct). Gate in `processBlock` like `delayOn`.
- **Editor:** "Chorus" card on FX tab.
- **Tests:** output ≠ input when on (RMS diff), silence-in → silence-out, no NaN under extremes (`hasBadValues`), L≠R (decorrelation) with width, bypass at mix 0.

### 3b. Reverb — largest new DSP item
- **New DSP:** `src/dsp/Reverb.{h,cpp}` — Freeverb topology: per channel 8 parallel feedback combs (with one-pole damping in the loop) + 4 series allpasses; right channel uses +23-sample stereo-spread offsets on all delay lengths. Do **not** reuse `CombFilter`/`AllpassDispersion` (they are tuned/filter-oriented); implement the classic fixed-length integer delay lines directly (power-of-two + mask buffers, preallocated in `setSampleRate` — scale the classic 44.1 kHz lengths by `sr/44100`).
- **API:** `setSampleRate/reset/setSize(0..1)/setDamp(0..1)/setWidth(0..1)/setMix(0..1)` + `processStereo`.
- **Params:** `reverbOn` (default off), `reverbSize`, `reverbDamp`, `reverbWidth`, `reverbMix`. Insert after delay, before globalEq. Update `getTailLengthSeconds` in `PluginProcessor.h` (currently returns the delay max; return e.g. `max(delayTail, 8.0)` when reverb on — a static conservative value is acceptable).
- **Tests:** impulse response decays monotonically to < -60 dB within a bounded time at size=0.5; larger size → longer T60 (compare energy after N samples); stability at size=1/damp=0 for 10 s of noise (`hasBadValues`, bounded peak); mix=0 is bit-transparent.

### 3c. Phaser (lower priority)
- 4–8 first-order allpasses per channel with LFO-swept coefficient + feedback. `AllpassDispersion` (`src/dsp/AllpassDispersion.{h,cpp}`) is structurally close — but it has a fixed coefficient; either extend it with per-block coefficient set (cheap, control-rate sweep) or write a small dedicated `Phaser` class. Params: `phaserOn/Rate/Depth/Feedback/Mix`. Only after 3a/3b.

---

## 4. Performance features

### 4a. Sustain pedal (CC64) — **functional gap, fix first**
`PluginProcessor::handleMidiMessage` currently ignores CC64 entirely, so a sustain pedal does nothing.
- **Engine:** add `SynthEngine::setSustainPedal(bool down)`. State: `bool pedalDown_` + per-voice `voiceSustained_[kMaxVoices]` array (like `voiceHeld_`). In `noteOff`: if pedal down, set `voiceSustained_[i] = true` instead of releasing (still clear `voiceHeld_`). On pedal-up: release every voice with `voiceSustained_` set and clear the flags. `allNotesOff` clears sustained too. Voice-steal should still prefer non-held over sustained (`allocateVoice` order: free → released-and-unsustained → sustained → held).
- **Processor:** in `handleMidiMessage`, `else if (msg.isController() && msg.getControllerNumber() == 64) engine.setSustainPedal(msg.getControllerValue() >= 64);` and mirror to `MonoBass` (add the same pedal concept to its held-note logic: pedal holds `curNote_` alive after note-off).
- **Tests:** note-on, pedal down, note-off → voice still active; pedal up → releases; steal order respected.

### 4b. Arpeggiator
- **Placement:** processor-level, *before* the MIDI event loop in `processBlock`. Maintain a held-note set fed by incoming note-ons/offs (arp intercepts them; when `arpOn`, real notes go to the arp's pool instead of `engine.noteOn` directly). Each block, compute step boundaries in samples from host tempo (`getPlayHead()->getPosition()->getBpm()` — pattern already in `pushParams`) and the division table `syncedDelaySeconds`/`syncedLfoHz` in `src/dsp/SynthParams.h`; emit note-on/off pairs at exact sample offsets by inserting into the block's processing (call `engine.noteOn/noteOff` between `renderSegment` splits, same as real MIDI — simplest: build a temporary `juce::MidiBuffer` merging real non-note events + generated arp notes, then run the existing loop unchanged).
- **State to keep:** current step index, phase in samples (persist across blocks; reset when transport jumps or pool empties), gate-off pending list. All fixed-size arrays — no allocation.
- **Params:** `arpOn`, `arpMode` (choice {Up, Down, UpDown, Random, As Played}), `arpOctaves` (Int 1–4), `arpRate` (choice: the `kSyncNames` division list — sync-only keeps it simple; free-Hz optional later), `arpGate` (10–100 %), `arpLatch` (bool: pool persists after keys released until all keys re-pressed).
- **Editor:** "Arpeggiator" card on the Modulation tab (or a new "Perform" tab if crowded).
- **Tests:** headless: feed held notes, render N blocks at fixed BPM, assert `engine.activeVoiceCount()` toggles at expected sample intervals and note order matches mode. (Engine is directly drivable — see `tests/dsp/synth_tests.cpp`.)

### 4c. Velocity curve
- **Param:** `velCurve` (choice {Linear, Soft, Hard, Fixed}). Apply in `handleMidiMessage` before `engine.noteOn`: Soft = `sqrt(v)`, Hard = `v*v`, Fixed = 1.0. One-line transform + tests.

### 4d. Master tune + transpose
- **Params:** `masterTune` (415–465 Hz, default 440, formatter `hz`-like with 1 decimal), `transpose` (Int ±24 semis).
- **Implementation:** `midiNoteToHz` (`src/dsp/Voice.cpp`) hardcodes 440. Add `double tuneA4 = 440; int transpose = 0;` to `SynthParams`; compute frequency as `tuneA4 * 2^((note + transpose - 69)/12)`. Call sites: `Voice::start` (via `SynthEngine::noteOn`'s `midiNoteToHz(note)` for glide memory) and `MonoBass::noteHz` — thread the values through both (MonoBass needs setters).

### 4e. MPE configuration (documentation + params only)
Per-note expression already works (channel-as-noteId: bend/pressure/CC74 per channel — see `handleMidiMessage`). Add: `bendRange` (§2f) for the per-note bend, and optionally `mpeMasterBend` (Int, default 2) applied to channel-1 messages if zone-aware handling is added. Low urgency; document the existing mechanism in the code comments when touched.

### 4f. Microtuning (stretch)
Isolate all pitch lookup behind one function (already nearly true: `midiNoteToHz`). Replace with a `TuningTable` (128 doubles, defaults to 12-TET from §4d params). Then .scl/.kbm file loading (parse on the message thread, swap the table atomically) or MTS-ESP client (external lib — evaluate licensing) can be added without touching DSP. Do §4d first; it creates the seam.

---

## 5. Sound-design depth

### 5a. Oscillator cross-modulation
- **Param:** `oscCrossMod` (choice {Off, Ring, Hard Sync, Phase Mod}). Ring = §2c (same underlying multiply; if both are implemented, unify: the choice selects the algorithm, `ringModLevel` becomes the generic cross-mod amount `crossModAmount`).
- **Hard Sync:** reset osc B's phase when osc A's phase wraps. Needs `OscillatorUnit`/oscillators to expose a `bool wrappedThisSample()` from A and `void resetPhase()` on B — both trivial for `AnalogOscillator` (`phase_`); for the PD osc do the reset at the base-rate boundary (accept slight softness, or reset inside the oversampled core for accuracy). Expect aliasing → recommend osQuality 4x note.
- **Phase Mod:** `B_output * amount` added to A's phase each sample (classic PM). Feed via a new `OscillatorUnit::setPhaseMod(double)` consumed in the PD/analog phase accumulators.
- **Tests:** hard sync at fA≠fB produces spectrum locked to fA harmonics; PM at amount 0 is bit-identical to off.

### 5b. Envelope velocity sensitivity
- **Params:** `ampVelSens`, `filterVelSens` (0..1, default: amp currently hard-wired to full velocity).
- **Implementation:** `Voice::renderOneSample` multiplies by `velGain_` unconditionally; change to `(1 - sens) + sens * velGain_` (computed once in `applyModulation`). Same idea scaling `filterEnvAmount` contribution by velocity.

### 5c. LFO upgrades
In `src/dsp/Lfo.{h,cpp}`:
- **Fade-in:** `setFadeIn(seconds)`; a 0→1 ramp multiplier started by a new `trigger()` call (called from `Voice::start`); `value()` returns `raw * fade`.
- **Retrigger mode:** param per LFO {Free, Retrig}: Retrig calls `lfo.reset()` in `Voice::start` (current behavior — it *always* resets today, see `Voice::start`; "Free" = stop resetting, keep a shared running phase — simplest correct approach: keep per-voice LFOs but seed phase from a `SynthEngine`-owned master phase on start).
- **Phase offset:** `setPhaseOffset(0..1)` added into `compute()`.
- **Params:** `lfoFade`, `lfoRetrig`, `lfoPhase` (+ lfo2 equivalents). Tests: fade envelope shape; free-running phase continuity across notes.

### 5d. New mod destinations
Add to `ModDest` (`src/dsp/ModMatrix.h`) + **both** name lists (gotcha!): `Lfo1Rate`, `Lfo2Rate`, `NoiseLevel`, `EnvRateScale` (scales amp-env stage rates — apply as a multiplier when calling `env_.setADSR`, control-rate). Wire each in `Voice::applyModulation`. Keep the per-destination cost zero-when-unrouted (the matrix already sums only active slots).

### 5e. Drive position
- **Param:** `drivePos` (choice {Post Filter, Pre Filter}, default Post = current).
- **Implementation:** `Voice::renderOneSample` — branch: Pre applies `amp_.processSample` to the mixed osc signal *before* the filter-routing switch, Post is current placement. Keep the `driveOn` gate around both.

### 5f. Mod-slot response curve
- **Param per slot:** `mod1Curve..mod10Curve` (choice {Linear, Exp, S-Curve}) — 10 new params.
- **Implementation:** in `ModMatrix::evaluate` (`src/dsp/ModMatrix.cpp`), transform the source value before `depth *`: Exp = `sign(v)*v*v`, S-Curve = `v*(1.5 - 0.5*v*v)` (cheap smoothstep-like). Add curve to `ModRoute` + `setRoute` signature. Only if the matrix UI has room — consider deferring to the UI overhaul.

---

## 6. Workflow / UI (pointers, lower priority)
- **Preset manager** (`src/plugin/PresetManager.{h,cpp}`): add `deletePreset(name)` (File::deleteFile) + a Delete button with confirm; distinguish Save (overwrite current) vs Save As (always prompt); optional subfolder categories (scan recursively, prefix combo items "Folder/Name").
- **A/B compare:** two `juce::ValueTree` snapshots in the editor + toggle button (`apvts.copyState()`/`replaceState` — same calls the presets use).
- **Randomize patch:** button randomizing normalized values of a curated param subset (exclude master level/limiter/polyphony); per-section randomize later.
- **Panic button:** editor button → `engine.allNotesOff()` + `monoBass.allNotesOff()` (both exist; needs a small thread-safe path, e.g. an atomic flag read in `processBlock`).
- **UI aesthetic overhaul** is tracked separately (user finds the current editor functional but ugly): knob rendering `SynthLookAndFeel::drawRotarySlider`, card styling `PDHybridEditor::SectionPanel::paint`, palette/fonts/spacing.

---

## 7. Suggested milestones

| Milestone | Features | Size | Key files |
|---|---|---|---|
| **v6.0** | §1 voice modes/polyphony; §4a sustain pedal; §2f bend range; §4c velocity curve | S–M each, high value | SynthEngine, Voice, PluginProcessor, SynthParams |
| **v6.1** | §3a chorus; §2a pitch envelope; §2c ring mod; §4d master tune/transpose | M | new Chorus.{h,cpp}, Voice, PluginProcessor |
| **v6.2** | §4b arpeggiator; §3b reverb | L each | new Reverb.{h,cpp}, PluginProcessor |
| **v6.3** | §5a cross-mod/sync; §5c LFO upgrades; §2e wave combine; §2b DCW routing; §5b/§5e | M | oscillators, Lfo, Voice |
| **v6.4** | §4f microtuning; §4e MPE config; §5d/§5f mod extensions; §6 workflow | M–L, stretch | TuningTable, ModMatrix, PresetManager |

Per-item definition of done: tests written first and green (full suite stays green), pluginval strictness-8 clean, editor control wired (combo lists matched!), preset compatibility preserved (new params must have backward-compatible defaults — old presets load with the feature off/neutral), commit per feature with the established message style.
