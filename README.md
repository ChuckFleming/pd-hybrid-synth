# PD Hybrid Synth

[![CI](https://github.com/ChuckFleming/pd-hybrid-synth/actions/workflows/ci.yml/badge.svg)](https://github.com/ChuckFleming/pd-hybrid-synth/actions/workflows/ci.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

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

## Download

Prebuilt **VST3 + Standalone** binaries for Windows, macOS, and Linux are
attached to each [GitHub Release](https://github.com/ChuckFleming/pd-hybrid-synth/releases).
Unzip and drop the `.vst3` into your host's VST3 folder, or run the standalone
app directly. (macOS builds are unsigned — right-click → Open the first time to
get past Gatekeeper.) Prefer to build it yourself? See below.

## Oscillator types

Each of the two oscillators (A and B) can run any of the engines below. To keep
the panel small, every engine reuses the same two shape knobs — **PD Amount**
(the Casio "DCW" control) and **Pulse Width** — plus a third **Engine Param**;
what they do is re-labelled per engine. The CZ-style multi-stage DCW envelope and
the mod matrix can sweep these, so every engine animates.

| Engine (dropdown name) | What it is | PD Amount / Pulse Width |
| --- | --- | --- |
| **Phase Distortion** | Casio CZ-style phase distortion. A linear phase ramp is warped through a non-linear remap before a sine read, growing the harmonic series while pitch stays fixed. Eight waveshapes: *Sawtooth, Square, Pulse, Double Sine, Saw-Pulse* (phase-distortion) and *Resonant I/II/III* (windowed-sync formants). Two waves can be alternated per cycle via **Wave Combine**. | Amount = distortion depth / resonant peak · Width = pulse plateau |
| **Saw / Square / Triangle / Pulse** | Bandlimited analog-modeled oscillators using PolyBLEP to suppress the aliasing naive edges produce. The classic subtractive starting point. | Width = pulse/square duty (triangle & saw ignore it) |
| **Vector PS** | Vector Phaseshaping (Kleimola et al. 2011) — a modern superset of CZ. A single movable 2-D inflection point bends the phase ramp before a cosine read, giving formant sweeps and hard-sync-like timbres, always click-free at the cycle boundary. | Amount = formant depth (vertical) · Width = inflection position (horizontal) |
| **Scanned** | Scanned synthesis (Verplank/Shaw/Mathews 2000). A 128-mass spring ring is "plucked" on note-on and slowly morphs its *shape* under a fixed-pitch scan — a living wavetable. An **Excite Shape** (Pluck / Impulse / Noise / Triangle) sets how it's struck. | Amount = stiffness (brightness / how fast it evolves) · Width = damping (settle time) |
| **VOSIM** | VOSIM (Kaegi & Tempelaars 1978) — "voice simulation". Bursts of decaying raised-sine (sin²) pulses place a fixed-Hz formant, reading as a vowel across the keyboard. Buzzy, vocal, speech-chip character. Engine Param = pulse count. | Amount = formant frequency · Width = burst decay |
| **Walsh** | Walsh-function synthesis (Electronotes lineage). Built from ±1 square-step functions ordered by "sequency" instead of sinusoids — a hyper-digital, gritty, chiptune-adjacent tone. Engine Param = wavefold. | Amount = spectral tilt (dark→harsh) · Width = even/odd balance |

Beyond the raw engines, the oscillator section also offers **ring modulation**,
**hard sync** and **phase modulation** between A and B, and an optional
**Karplus–Strong pluck** that uses the osc mix to excite a tuned, damped string.

## Filter types

Two independent filter slots (A and B) can be run **Single**, in **Series**, or
in **Parallel**. Each slot picks one of five types. A single **Morph** knob is
repurposed per type (noted below).

| Type | What it is | Morph knob |
| --- | --- | --- |
| **Ladder** | Moog-style 4-pole (24 dB/oct) transistor-ladder lowpass — four zero-delay-feedback stages with a tanh-saturated resonance path that self-limits and self-oscillates near the top of the resonance range. The classic warm, fat lowpass. | — |
| **State Variable** | Zero-delay-feedback 12 dB/oct state-variable filter giving lowpass, bandpass, highpass and notch. The Morph knob crossfades **LP → BP → HP** continuously. | LP → BP → HP crossfade |
| **PD Resonator** | Signature non-traditional filter: a damped quadrature resonator whose self-oscillation is *phase-distorted* rather than a pure sine. At zero it's a clean resonant bandpass; raising Morph grows a CZ-style harmonic series at multiples of the resonant frequency. | Phase-distortion amount |
| **Comb** | Tuned feedback comb / waveguide (Karplus–Strong lineage). A fractional delay line resonates at a harmonic comb; a lowpass in the feedback path gives a plucked, string-like colour. | Feedback damping |
| **Allpass** | A cascade of allpass sections: flat 0 dB magnitude at every frequency but frequency-dependent phase, so it smears/disperses transients for glassy, metallic colour without changing the spectrum's level. | Dispersion amount |

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
