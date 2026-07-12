# PD Hybrid Synth — handoff notes

JUCE C++ **Casio CZ-style phase-distortion synth** (VST3 + Standalone, Windows). Feature-complete: v5 + all of v6, a performance pass, and a UI overhaul are done and committed on branch `master`.

## Build & verify (use the Bash tool, NOT PowerShell — AV blocks PS for builds)
CMake: `/e/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`
```
"$CMAKE" --build build --config Release --target <T>     # T = pdhybrid_tests | PDHybridSynth_VST3 | PDHybridSynth_Standalone
./build/tests/Release/pdhybrid_tests.exe                 # 153 cases — keep green
VST3="$(cygpath -w "$PWD/build/PDHybridSynth_artefacts/Release/VST3/PD Hybrid Synth.vst3")"
./build/tools/pluginval.exe --strictness-level 8 --validate-in-process "$VST3"   # must say SUCCESS
```
**Gotcha:** kill the running Standalone (`taskkill //IM "PD Hybrid Synth.exe" //F`) BEFORE rebuilding it — otherwise the exe is locked and the link fails *silently* (grep for `error C` misses linker errors).

## Architecture
- `src/dsp/` — pure C++ DSP, no JUCE, exercised by `tests/dsp/*.cpp` (Catch2 + `tests/harness/`). Key: `Voice`, `SynthEngine` (allocation/voice modes), `SynthParams.h` (per-block param struct), oscillators/filters/FX classes.
- `src/plugin/PluginProcessor.{h,cpp}` — params (`createLayout`), MIDI (`handleMidiMessage`), master FX chain + arp in `processBlock`, `pushParams`.
- `src/plugin/PluginEditor.{h,cpp}` + `SynthLookAndFeel.h` — tabbed card editor.

## Adding a parameter (the recipe)
1. `createLayout()` in PluginProcessor.cpp  2. read in `pushParams()` into a `SynthParams` field  3. consume in Voice/engine/FX stage  4. editor: `addKnob`/`addCombo` in `buildSections()` + add to a Page in the `layout` vector.
**ComboBox gotcha (caused a real bug):** an editor combo's item list must match its APVTS choice param length/order EXACTLY (JUCE maps by index/(count-1)).
New DSP `.cpp` → add to `pdhybrid_dsp` in root `CMakeLists.txt` AND its test to `tests/CMakeLists.txt`.

## Perf rules (see PERFORMANCE_ANALYSIS.md STATUS block)
No audio-thread allocation; guard transcendental redesigns behind change-checks; prefer 32-sample control-rate work; skip transparent stages.

## Status / where to look
- **FEATURE_ROADMAP.md** — per-feature spec + STATUS block (all v6 done, commit hashes). Only future extensions left: file-based microtuning (.scl/MTS-ESP) behind the `noteHz`/`tuningCentsOffset` seam.
- **PERFORMANCE_ANALYSIS.md** — perf audit + STATUS (safe fixes done; P1-1a/P1-1b/P2-7/P3-1 deferred by design).
- **UI** = "CZ Terminal" theme (black + green-phosphor monospace, wireframe knobs) on an 8-pt **column grid**: cards snap to justified columns with aligned knob/value baselines; a `Section::stackId` groups narrow strips side-by-side in one column (Glide|Unison|Stereo). Editor layout lives in `SectionPanel::layout`.
- Auto-memory (`~/.claude/.../memory/MEMORY.md` + linked files) has the same project history if this file is missing.

## Conventions
Commit per feature; end messages with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Only commit/push when asked.
