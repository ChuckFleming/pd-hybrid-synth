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

## Status & roadmap

The synth is feature-complete: phase-distortion + analog + several non-traditional
oscillator engines, analog/non-traditional filters, oversampled overdrive amp,
ADSR + CZ-style multi-stage envelopes, full polyphony/MIDI/voice modes, a
modulation matrix, chorus/reverb/arpeggiator, and a hand-built GUI with a CRT
output scope. See [`FEATURE_ROADMAP.md`](FEATURE_ROADMAP.md) for the detailed
feature list and history.

## License

Released under the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE).

This project builds on [JUCE](https://juce.com) and Steinberg's VST3 SDK, both of
which are available under the GPLv3 for free/open-source use. Because of that,
GPLv3 is the license under which this plugin is distributed: you are free to use,
study, modify, and redistribute it, provided derivative works remain GPLv3.

VST® is a trademark of Steinberg Media Technologies GmbH.
