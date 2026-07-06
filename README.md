# PD Hybrid Synth

A hybrid VST synthesizer combining Casio CZ-style **phase-distortion** synthesis,
traditional analog-modeled subtractive oscillators, analog + non-traditional
filters, a musical overdrive amp, and both ADSR and CZ-style multi-stage
envelopes. Built with **JUCE**, shipping **VST3 + Standalone**.

Developed **test-first**: DSP lives in a headless, JUCE-free core (`src/dsp`)
that a fast offline harness (`tests/harness`) measures via FFT/spectral analysis.

## Layout

```
src/dsp/      Headless DSP core (pure C++, no JUCE) -- the unit-test target
src/plugin/   JUCE AudioProcessor wrapper (VST3 + Standalone)
tests/harness Shared harness: FFT, spectral toolkit, signal stats, invariance
tests/dsp/    Per-module test suites
```

## Prerequisites (Windows)

Install **Visual Studio 2022 Community** with the **"Desktop development with
C++"** workload (provides MSVC + a bundled CMake + Ninja). JUCE and Catch2 are
fetched automatically by CMake -- nothing else to install.

## Build & test

```sh
# Configure (from the repo root). Use the VS generator:
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build everything (plugin + tests)
cmake --build build --config Release

# Run the DSP test suite
ctest --test-dir build -C Release --output-on-failure
```

The VST3 and Standalone builds land under
`build/PDHybridSynth_artefacts/Release/`.

## Roadmap (vertical slice first)

1. [x] Shared test harness (offline render + FFT/spectral toolkit + invariance)
2. [x] Phase-distortion oscillator + tests
3. [ ] Analog-modeled ladder filter (ZDF/TPT) + frequency-response tests
4. [ ] Overdrive amp (oversampled waveshaper) + transfer-curve/aliasing tests
5. [ ] Multi-stage envelope engine (ADSR as a 4-stage preset) + timing tests
6. [ ] Polyphony / MIDI / MPE voice allocation + tests
7. [ ] Modulation matrix
8. [ ] Non-traditional filters (phase-distortion resonator, morphing SVF, ...)
9. [ ] Hand-built GUI
