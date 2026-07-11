# Performance Analysis — PD Hybrid Synth
Date: 2026-07-10. Analysis only — **no code was changed**. Each finding lists the issue, exact location, and resolution steps. Findings are ordered by expected impact on (a) audio round-trip latency (RTT) and (b) CPU/memory footprint. The two goals are linked: the biggest RTT lever is making the DSP cheap and glitch-free enough to run reliably at small host buffer sizes (64–128 samples).

**Verification for any fix:** `cmake --build build --config Release --target pdhybrid_tests` then run `build/tests/Release/pdhybrid_tests.exe` (127 cases must stay green), rebuild VST3, run `build/tools/pluginval.exe --strictness-level 8 --validate-in-process <vst3>`. Use Bash, not PowerShell, for builds.

---

## IMPLEMENTATION STATUS (updated 2026-07-11)
All changes below verified: 127/127 tests green + pluginval strictness-8 clean.

- **DONE** — P0-1, P0-2 (audio-thread allocations) [9ed868f]
- **DONE** — P1-1c (change-guards on OscEq / LadderFilter cutoff / GlobalEq / Osc tuning) [9ed868f]
- **DONE** — P1-2 (skip mono-bass scratch when disabled) [9ed868f]
- **DONE** — P2-3 (mono-bass early-out + chunked pitch) [9ed868f]
- **DONE** — P2-4 (OscEq bypass when flat) [9ed868f]
- **DONE** — P2-5 (cache bit-crush level) [9ed868f]
- **DONE** — P2-1 (oversampler: circular buffer [622a7cd] + full polyphase [ef0dfc7])
- **DONE** — P2-6 (compressor bypass when transparent) [622a7cd]
- **DONE** — P2-2 (advance LFOs per control chunk) [5cbd5a0]
- **DONE** — P2-8 (power-of-two delay buffer, masked wrap) [6a9598b]

- **DEFERRED — P1-1a** (cache atomic param pointers in pushParams): after the P1-1c
  guards the remaining per-block cost is ~120 map lookups (~0.1–0.2% CPU). Hand-caching
  120 pointers risks silent param mis-routing that automated tests won't catch. Low
  reward vs risk; revisit only if profiling flags it.
- **DEFERRED — P1-1b** (dirty-flag / version-gated Voice::setParams): the expensive
  transcendental redesigns are already gated by P1-1c, so the remaining per-voice cost
  is a struct copy + cheap assigns. A correct dirty flag must also catch tempo changes
  and relies on synchronous parameter-listener callbacks; risk (silently unapplied
  params) outweighs the now-small benefit.
- **DEFERRED — P2-7** (fewer ladder Newton iterations): alters the saturation character
  (it is the sound); intentionally not changed.
- **DEFERRED — P3-1** (report `setLatencySamples`): the oversampler group delay varies
  with osQuality and the mixed osc/overdrive paths; reporting a wrong value is worse than
  0. Needs an offline impulse-alignment measurement first (see P3-1 below). Note this is a
  host-alignment nicety, not an RTT reduction.

---

## P0 — Audio-thread heap allocations (dropout risk; blocks small buffers)

### 1. `Voice::setParams` allocates a `std::vector` every call, every voice, every block
- **Location:** `src/dsp/Voice.cpp`, in `Voice::setParams` — the CZ block: `std::vector<EnvStage> czStages; czStages.reserve(8); ... multiEnv_.setStages(czStages, ...)` (~lines 57–62).
- **Why it matters:** `SynthEngine::renderBlock` (`src/dsp/SynthEngine.cpp` ~line 142) calls `voices_[i].setParams(params_)` for **every active voice, every block**. With 16 voices that is up to 16 heap alloc/free pairs per audio callback, plus `MultiStageEnvelope::setStages` copying the vector (`src/dsp/MultiStageEnvelope.cpp` line 22–27). Allocator contention on the audio thread causes unpredictable spikes → forces users to run larger buffers → higher RTT.
- **Fix:**
  1. Replace the local vector with a fixed `std::array<EnvStage, 8>` member (or build in-place into a preallocated member vector).
  2. Change `MultiStageEnvelope::setStages` to accept `(const EnvStage*, int count, int sustainIndex)` (or a `std::array`/span) and copy into its preallocated `stages_` without reallocation (`stages_` should be `reserve`d to max size once in `setSampleRate`/ctor).
  3. Better still, combine with P1-1 below so this code only runs when parameters actually change.

### 2. `MonoBass::noteOn` can allocate on the audio thread
- **Location:** `src/dsp/MonoBass.cpp`, `noteOn` — `held_.push_back(note)`; `held_` is a plain `std::vector<int>` (`src/dsp/MonoBass.h`).
- **Why:** MIDI note-ons are handled on the audio thread (`PluginProcessor::handleMidiMessage`). First growths of `held_` allocate.
- **Fix:** `held_.reserve(128)` in `MonoBass::setSampleRate` (or `reset`). One line, zero risk.

---

## P1 — Redundant per-block work (steady-state CPU waste)

### 1. Full parameter push + coefficient redesign runs every block even when nothing changed
This is the **largest steady-state CPU issue**. Three layers compound:

- **(a) `PluginProcessor::pushParams`** (`src/plugin/PluginProcessor.cpp`) performs ~120 `apvts.getRawParameterValue("id")->load()` calls per block. Each is a string-keyed hash-map lookup in JUCE. 
  - **Fix:** In the processor constructor, cache each `std::atomic<float>*` once (e.g., a struct of pointers, `p.cutoff = apvts.getRawParameterValue("cutoff")` stored as members) and read the atomics directly in `pushParams`. Straight refactor, no behavior change.
- **(b) `SynthEngine::renderBlock`** (`src/dsp/SynthEngine.cpp` ~line 142) calls `voices_[i].setParams(params_)` per active voice per block. `Voice::setParams` (`src/dsp/Voice.cpp` lines 45–93) then does, per voice per block: a full `SynthParams` struct copy (several hundred bytes incl. the ModMatrix), 5× `setADSR`, `setStages` (see P0-1), 2× `Lfo::setFrequency`, 2× `OscillatorUnit::setTuning` (each a `std::pow`), and 2× `OscillatorUnit::setEq` → `OscEq::setGains` → **6 `Biquad::design` calls** (each with `pow/sin/cos`) — all even when no knob moved.
  - **Fix:** Add a monotonically increasing `uint64_t paramsVersion_` to `SynthEngine`, bumped in `SynthEngine::setParams`. Each `Voice` stores `lastSeenVersion_`; `Voice::setParams` becomes a cheap early-out (`if (version == lastSeenVersion_) return;`) with the full push only on change. Note `Voice::setParams` is also called in `SynthEngine::noteOn` — keep that unconditional (fresh voice) or version-checked, either works.
- **(c) `PluginProcessor::pushParams` redesigns all 4 Global-EQ bands every block** — `globalEq.setBand(...)` ×4, each running `Biquad::design` for L+R (8 designs/block, each with `pow/sqrt/sin/cos`), and then `applyGlobalModulation` (`src/plugin/PluginProcessor.cpp`) redesigns the high shelf **again** every block even with no `GlobalEqGain` route active.
  - **Fix:** In `GlobalEq::setBand` (`src/dsp/GlobalEq.cpp`), early-out when `freqHz == freq_[band] && gainDb == gain_[band]`. In `applyGlobalModulation`, skip the `setBand` call when the modulation contribution is 0 (e.g., `std::abs(md(GlobalEqGain)) < 1e-9` and gain equals base). Same compare-before-design guard is worth adding to `OscEq::setGains` and `LadderFilter::setCutoff` (which calls `tan` per call; it is invoked per control chunk — see P2-2).

### 2. Mono-bass scratch work runs even when the bass is disabled
- **Location:** `src/plugin/PluginProcessor.cpp`, `renderSegment` — zero-fills `scratchBass` and then adds it into `scratchL/scratchR` unconditionally; `MonoBass::renderBlock` no-ops when disabled but the zero-fill + add loops still execute.
- **Fix:** Wrap the entire scratchBass zero/render/add block in `if (monoBass.enabled())` (add a const accessor use; it already exists: `monoBass.enabled()`).

---

## P2 — Per-sample hot-path costs (the real CPU floor)

### 1. `Oversampler::firStep` uses an O(L) shift register and a non-polyphase FIR — the single hottest loop in the plugin
- **Location:** `src/dsp/Oversampler.cpp` lines 65–104 (`firStep`, `upsample`, `downsample`).
- **Cost today:** At 4× the prototype is `16 taps/phase × 4 = 64` taps. Every high-rate sample does a 64-element **shift copy** plus a 64-MAC dot product. Per *base-rate* sample that is 4 × (64+64) × 2 (up + down) ≈ **1024 ops**, and there are **two** oversampled stages per voice (PD oscillator core and `OverdriveAmp`). With 6-voice unison chords this dominates the profile.
- **Fix (in order of payoff):**
  1. **Circular buffer instead of shifting:** keep a write index, index taps modulo L (use power-of-two size + mask). Removes the O(L) copy — ~2× on this loop.
  2. **True polyphase decomposition:** for upsampling, the input is zero-stuffed, so only every `factor`-th tap sees a non-zero sample — each output phase needs only `tapsPerPhase` (16) MACs, not 64. Same for decimation (compute only the retained output phase). This is a ~`factor`× reduction on top of (1). Combined: ~8× fewer ops in these FIRs.
  3. Optional: since `PhaseDistortionOscillator` generates its core at the high rate directly (no upsampling of an input signal — check `processSample`: it zero-stuffs only in `OverdriveAmp`'s path), verify whether the oscillator even needs the *up* FIR; generating N core samples and decimating may allow deleting half the work there.
  4. Keep the existing `[oversampling]` test green and add a spectral test comparing pre/post-refactor output (they should match within float tolerance if the math is equivalent).

### 2. Voice inner loop advances 7 modulation sources per sample although they are only *read* every 32 samples
- **Location:** `src/dsp/Voice.cpp`, `renderBlock` inner loop — per sample calls `lfo_.processSample(); lfo2_.processSample(); env2_.processSample(); filterEnv_.processSample(); filter2Env_.processSample(); multiEnv_.processSample();` while their values are consumed only by `applyModulation()` once per 32-sample control chunk. Only `env_` (amp envelope, inside `renderOneSample`) truly needs per-sample stepping.
- **Fix:** `Lfo` already has `advance(int)` — replace the two per-sample LFO calls with one `advance(chunk)` per control chunk. Add an equivalent `advance(int numSamples)` to `MultiStageEnvelope` (phase math is linear per stage: advance `phase_ += phaseInc_ * n` with stage-boundary handling in a small loop) and advance the four non-amp envelopes per chunk. Saves ~6 virtual-free but branchy calls/sample/voice; meaningful at 16 voices.
- **Caution:** envelope stage boundaries must land on the same sample as before within one chunk; the existing envelope timing tests (`tests/dsp/envelope_tests.cpp`) plus `[synth]` tests guard this.

### 3. `MonoBass::renderBlock` does per-sample `exp` + 2 divisions even when pitch is settled, and renders while silent
- **Location:** `src/dsp/MonoBass.cpp`, `renderBlock` — per sample: `curLogHz_ += coef * (...)`, `std::exp`, `main_.setFrequency(hz)` and `sub_.setFrequency(hz*0.5)` (each `AnalogOscillator::setFrequency` recomputes `inc_ = f/sr`, a division). Also, when the envelope has finished (`!env_.isActive()`) but `enabled_` is true, the full osc + wavefold + tanh chain still runs producing zeros.
- **Fix:**
  1. Early-out at the top: `if (!enabled_ || !env_.isActive()) return;`.
  2. Hoist glide/pitch to 32-sample control chunks (mirror `Voice::renderBlock`'s structure): compute `hz` once per chunk, call `setFrequency` once per chunk. When `|tgtLog - curLogHz_| < 1e-6`, snap and skip the exp entirely.

### 4. `OscEq` always runs 3 biquads/sample/oscillator even when flat (the default)
- **Location:** `src/dsp/OscEq.cpp` `processSample`; called from `OscillatorUnit::processSample` (`src/dsp/OscillatorUnit.cpp` line 58) for both units of every voice.
- **Fix:** In `OscEq::setGains`, set a `bypass_ = (|low|+|mid|+|high| < 0.01 dB)` flag; `processSample` returns input immediately when bypassed. Default patches are flat → saves 6 biquads/sample/voice for most sounds. (State is preserved; re-engage is click-free at these gains.)

### 5. `OverdriveAmp` computes `std::pow(2, crushBits-1)` per sample when bit-crush is active
- **Location:** `src/dsp/OverdriveAmp.cpp` line ~39 (inside `processSample`).
- **Fix:** cache `crushLevels_` in `setCrushBits` and reuse. One-line hoist.

### 6. `Compressor` does `log10` + `pow` per sample on the master bus
- **Location:** `src/dsp/Compressor.cpp` lines 75 and 83 (`levelDb = 20*log10(...)`, `gainLin = pow(10, gainDb/20) * makeupLin`).
- **Fix options:** (a) update the static-curve/gain conversion at a 8–16 sample control interval with one-pole smoothing between updates (the detector itself stays per-sample); or (b) substitute fast approximations (`std::exp2/std::log2`-based, or a polynomial approx). Also hoist `makeupLin` (line 66) out of `processStereo`'s loop if it is currently computed per call — it only changes with `setMakeup`.

### 7. `LadderFilter` runs a 4-iteration Newton solve with `tanh` each iteration (≈5 `tanh`/sample)
- **Location:** `src/dsp/LadderFilter.cpp` `processSample` (lines 57+).
- **Fix (optional, lower priority — it is the sound):** reduce to 2 iterations (usually converged) or use a rational `tanh` approximation for the inner iterations, keeping one exact `tanh` for the final evaluation. Guard with the existing `[filter]` frequency-response tests; verify self-oscillation behavior at high resonance manually.

### 8. `Delay::readFrac` uses `%` and a `while` wrap per read
- **Location:** `src/dsp/Delay.cpp` lines 58–68.
- **Fix (minor):** round the buffer size up to a power of two and mask, or replace `%` with a conditional subtract. Two reads/sample on the master bus — small but free to fix.

---

## P3 — Latency (RTT) specifics

### 1. Oversampler group delay is not reported to the host
- **Issue:** the two FIR stages are linear-phase; at 4× each contributes ≈ `(64-1)/2 = 31.5` high-rate samples ≈ **7.9 base samples**; oscillator + overdrive in series ≈ ~16 samples (~0.33 ms @ 48 kHz). The plugin never calls `setLatencySamples`, so hosts cannot delay-compensate. For a synth this does not affect perceived key-to-sound RTT much, but it does misalign the rendered audio against other tracks.
- **Fix:** measure the actual impulse alignment offline (the harness can render an impulse through a voice) and report it via `setLatencySamples` in `prepareToPlay`, updating it when `osQuality` changes (host is notified via `updateHostDisplay`/latency change). At `osQuality = 1x` report 0.
- **Note:** the `MasterStage` limiter is deliberately zero-lookahead (tanh knee, no delay) — keep it that way; do **not** add a lookahead limiter if RTT is the priority.

### 2. Standalone RTT is dominated by the audio-device configuration, not the DSP
- **Issue:** the JUCE standalone defaults to shared-mode WASAPI with a conservative buffer. Users judge "RTT" mostly from this.
- **Fix:** in the standalone wrapper setup (CMake `juce_add_plugin` options / `StandalonePluginHolder` settings), prefer smaller default buffer (128) and surface the device panel prominently; document ASIO or WASAPI-Exclusive for low latency. The CPU fixes above (P0–P2) are what make 64–128-sample buffers reliably glitch-free — that is the real RTT win.

---

## P4 — Structural ideas (bigger refactors, evaluate after P0–P2)

1. **Unison cost scales linearly** (`SynthEngine::noteOn` stacks up to 6 full voices per note, each with 2 oscillators, 2 filters, oversampled overdrive, 5 envelopes, 2 LFOs). Consider a lighter unison mode: share filters/envelopes per note and duplicate only oscillators with detune/pan. Large CPU saving for the "Fat Saw"-style patches, but a real architecture change — do last.
2. **`SynthParams` by-value copies**: `SynthEngine::setParams` copies the struct per block (fine, once) but `Voice::setParams` copies it again per voice (`params_ = params;`). With P1-1's version gate this cost mostly disappears; alternatively hold a `const SynthParams*` in voices since the engine's copy outlives the block.
3. **Per-sample `FilterUnit` switch dispatch** (`src/dsp/FilterUnit.cpp` line 54): a per-sample `switch` on filter type. Cheap in practice (predictable branch); only worth converting to a per-block function pointer if profiling shows it.

---

## Explicitly checked and fine (no action)
- `ScopedNoDenormals` present in `processBlock`; `scratchL/scratchR` preallocated in `prepareToPlay` (resize path only for larger-than-prepared blocks).
- No timers/animation in the editor; repaint volume is low; `TooltipWindow` idle.
- Voice stealing is O(voices) array scans — fine at 16.
- `Biquad`/filters use doubles; denormals guarded by the noDenormals scope.
- MIDI-split segmented rendering (`renderSegment`) is correct and cheap.
- 32-sample control-rate modulation (recent change) is a good latency/quality tradeoff; keep.

## Suggested order of execution
1. P0-1, P0-2 (allocations) — small diffs, immediate stability at small buffers.
2. P1-1a (cached param pointers), P1-1b (version-gated `Voice::setParams`), P1-1c (compare-before-design in `GlobalEq::setBand`/`OscEq::setGains`), P1-2 (mono-bass guard).
3. P2-1 (oversampler polyphase + circular buffer) — biggest CPU item, needs care + spectral regression tests.
4. P2-2 … P2-8 in listed order.
5. P3-1 (report latency), P3-2 (standalone device defaults).
6. Re-profile; only then consider P4.
