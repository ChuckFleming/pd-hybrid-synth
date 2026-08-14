#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout PDHybridAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Human-friendly value formatting: each unit gets a string-from-value plus a
    // matching value-from-string so typed edits round-trip. `pf` pushes a float
    // parameter with the given formatter attributes.
    using Attr = juce::AudioParameterFloatAttributes;
    auto sv = [] (std::function<juce::String(float)> toStr,
                  std::function<float(const juce::String&)> fromStr)
    {
        return Attr().withStringFromValueFunction ([toStr] (float v, int) { return toStr (v); })
                     .withValueFromStringFunction ([fromStr] (const juce::String& t) { return fromStr (t); });
    };
    auto pct = sv ([] (float x) { return juce::String (juce::roundToInt (x * 100.0f)) + " %"; },
                   [] (const juce::String& t) { return t.getFloatValue() * 0.01f; });
    auto hz  = sv ([] (float x) { return x >= 1000.0f ? juce::String (x / 1000.0f, 1) + " kHz"
                                                      : juce::String (juce::roundToInt (x)) + " Hz"; },
                   [] (const juce::String& t) { float f = t.getFloatValue();
                                                return t.containsIgnoreCase ("k") ? f * 1000.0f : f; });
    auto db  = sv ([] (float x) { int d = juce::roundToInt (x);
                                  return juce::String (d > 0 ? "+" : "") + juce::String (d) + " dB"; },
                   [] (const juce::String& t) { return t.getFloatValue(); });
    auto sec = sv ([] (float x) { return x < 1.0f ? juce::String (juce::roundToInt (x * 1000.0f)) + " ms"
                                                  : juce::String (x, 2) + " s"; },
                   [] (const juce::String& t) { float f = t.getFloatValue();
                                                return t.containsIgnoreCase ("ms") ? f * 0.001f : f; });
    auto cnt = sv ([] (float x) { int d = juce::roundToInt (x);
                                  return juce::String (d > 0 ? "+" : "") + juce::String (d) + " ct"; },
                   [] (const juce::String& t) { return t.getFloatValue(); });
    auto oct = sv ([] (float x) { return juce::String (x > 0 ? "+" : "") + juce::String (x, 1) + " oct"; },
                   [] (const juce::String& t) { return t.getFloatValue(); });
    auto rate = sv ([] (float x) { return juce::String (x, 2) + " Hz"; },
                    [] (const juce::String& t) { return t.getFloatValue(); });
    // Integer, no unit: the knob is already labelled BPM and the value box is
    // only wide enough for a few characters.
    auto bpmAttr = sv ([] (float x) { return juce::String (juce::roundToInt (x)); },
                       [] (const juce::String& t) { return t.getFloatValue(); });
    auto ms  = sv ([] (float x) { return juce::String (x, 1) + " ms"; },
                   [] (const juce::String& t) { return t.getFloatValue(); });
    auto ratio = sv ([] (float x) { return juce::String (x, 1) + ":1"; },
                     [] (const juce::String& t) { return t.getFloatValue(); });
    auto mult = sv ([] (float x) { return juce::String (x, 1) + "x"; },
                    [] (const juce::String& t) { return t.getFloatValue(); });
    auto pan = sv ([] (float x) { int p = juce::roundToInt (x * 100.0f);
                                  return p == 0 ? juce::String ("C")
                                       : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p)); },
                   [] (const juce::String& t) { auto u = t.trim().toUpperCase();
                                                if (u.startsWithChar ('L')) return -u.substring (1).getFloatValue() * 0.01f;
                                                if (u.startsWithChar ('R')) return  u.substring (1).getFloatValue() * 0.01f;
                                                if (u.startsWithChar ('C')) return 0.0f;
                                                return t.getFloatValue() * 0.01f; });
    auto amt = sv ([] (float x) { return juce::String (x > 0 ? "+" : "") + juce::String (x, 2); },
                   [] (const juce::String& t) { return t.getFloatValue(); });
    auto num2 = sv ([] (float x) { return juce::String (x, 2); },
                    [] (const juce::String& t) { return t.getFloatValue(); });

    auto pf = [&] (const juce::String& id, const juce::String& name,
                   juce::NormalisableRange<float> range, float def, const Attr& attr)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def, attr));
    };

    const juce::StringArray oscTypeNames { "Phase Distortion", "Saw", "Square", "Triangle", "Pulse", "Vector PS", "Scanned", "VOSIM", "Walsh", "Supersaw", "Harmonic", "PAF", "Granular", "Wavetable" };
    const juce::StringArray pdWaveNames  { "Sawtooth", "Square", "Pulse", "Double Sine",
                                           "Saw-Pulse", "Resonant I", "Resonant II", "Resonant III" };

    // Two oscillator slots (A defaults to PD, B to a saw an octave down but silent).
    auto addOscGroup = [&] (const juce::String& id, const juce::String& label,
                            int defType, float defLevel)
    {
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id + "Type", 1 }, label + " Type", oscTypeNames, defType));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id + "Wave", 1 }, label + " PD Wave", pdWaveNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id + "Wave2", 1 }, label + " PD Wave 2", pdWaveNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id + "Combine", 1 }, label + " Wave Combine", false));
        pf (id + "Amount", label + " PD Amount",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.30f, pct);
        pf (id + "PulseWidth", label + " Pulse Width",
            juce::NormalisableRange<float> (0.05f, 0.95f), 0.50f, pct);
        // Per-engine extra: VOSIM pulse count / Scanned morph rate / Walsh fold.
        pf (id + "Engine", label + " Engine Param",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.40f, pct);
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id + "Excite", 1 }, label + " Excite Shape",
            juce::StringArray { "Pluck", "Impulse", "Noise", "Triangle" }, 0));
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id + "Octave", 1 }, label + " Octave", -3, 3, 0));
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id + "Semi", 1 }, label + " Semitone", -12, 12, 0));
        pf (id + "Fine", label + " Fine",
            juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f, cnt);
        pf (id + "Level", label + " Level",
            juce::NormalisableRange<float> (0.0f, 1.0f), defLevel, pct);
        // Mixer mute. Defaults to on, so presets saved before it existed load
        // with the oscillator audible and sound exactly as they did.
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id + "On", 1 }, label + " On", true));
        const juce::NormalisableRange<float> eqRange (-18.0f, 18.0f);
        pf (id + "EqLow",  label + " EQ Low",  eqRange, 0.0f, db);
        pf (id + "EqMid",  label + " EQ Mid",  eqRange, 0.0f, db);
        pf (id + "EqHigh", label + " EQ High", eqRange, 0.0f, db);
    };
    addOscGroup ("oscA", "Osc A", 0, 1.0f);   // Phase Distortion, full level
    addOscGroup ("oscB", "Osc B", 1, 0.0f);   // Saw, silent by default

    pf ("noiseLevel", "Noise Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("ringMod", "Ring Mod", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "oscCrossMod", 1 }, "Osc Cross Mod",
        juce::StringArray { "Off", "Hard Sync", "Phase Mod" }, 0));
    pf ("crossModAmount", "Cross Mod Amount", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    // Karplus-Strong pluck (osc mix excites a tuned string). Off by default.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "pluckOn", 1 }, "Pluck", false));
    pf ("pluckDecay", "Pluck Decay", juce::NormalisableRange<float> (0.0f, 1.0f), 0.70f, pct);
    pf ("pluckDamp",  "Pluck Damp",  juce::NormalisableRange<float> (0.0f, 1.0f), 0.30f, pct);
    pf ("pluckDispersion", "Pluck Dispersion", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("pluckBurst", "Pluck Burst", juce::NormalisableRange<float> (0.5f, 50.0f, 0.0f, 0.5f), 20.0f, ms);
    // Default 1 = the string replaces the oscillators, which is how the pluck
    // behaved before this existed, so old presets are unaffected.
    pf ("pluckMix", "Pluck Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct);

    // Casio CZ-style vibrato (dedicated per-voice pitch LFO). Off by default.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "vibratoOn", 1 }, "Vibrato", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "vibratoWave", 1 }, "Vibrato Wave",
        juce::StringArray { "Triangle", "Square", "Ramp Up", "Ramp Down" }, 0));
    pf ("vibratoRate",  "Vibrato Rate",  juce::NormalisableRange<float> (0.1f, 12.0f, 0.0f, 0.5f), 5.0f, rate);
    pf ("vibratoDepth", "Vibrato Depth", juce::NormalisableRange<float> (0.0f, 100.0f), 20.0f, cnt);
    pf ("vibratoDelay", "Vibrato Delay", juce::NormalisableRange<float> (0.0f, 3.0f, 0.0f, 0.5f), 0.0f, sec);

    pf ("cutoff", "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 18000.0f, 0.0f, 0.3f), 8000.0f, hz);

    pf ("resonance", "Filter Resonance", juce::NormalisableRange<float> (0.0f, 1.0f), 0.20f, pct);

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filterType", 1 }, "Filter Type",
        juce::StringArray { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass",
                            "Formant", "Diode Ladder", "Band Split" }, 0));

    pf ("filterMorph", "Filter Morph", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    pf ("keyTrack", "Filter Key Track", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    pf ("filterEnvAmount", "Filter Env Amount",
        juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f, oct);   // octaves, bipolar

    pf ("filterEnvA", "Filter Env Attack",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.01f, sec);
    pf ("filterEnvD", "Filter Env Decay",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f, sec);
    pf ("filterEnvS", "Filter Env Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("filterEnvR", "Filter Env Release",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.30f, sec);

    // --- Filter B + routing ---
    const juce::StringArray filterTypeNames { "Ladder", "State Variable", "PD Resonator",
                                              "Comb", "Allpass", "Formant", "Diode Ladder", "Band Split" };
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filterRouting", 1 }, "Filter Routing",
        juce::StringArray { "Single", "Series", "Parallel" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filter2Type", 1 }, "Filter 2 Type", filterTypeNames, 0));
    pf ("filter2Cutoff", "Filter 2 Cutoff",
        juce::NormalisableRange<float> (20.0f, 18000.0f, 0.0f, 0.3f), 8000.0f, hz);
    pf ("filter2Res", "Filter 2 Resonance", juce::NormalisableRange<float> (0.0f, 1.0f), 0.20f, pct);
    pf ("filter2Morph", "Filter 2 Morph", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    pf ("filter2EnvAmount", "Filter 2 Env Amount",
        juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f, oct);
    pf ("filter2EnvA", "Filter 2 Env Attack",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.01f, sec);
    pf ("filter2EnvD", "Filter 2 Env Decay",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f, sec);
    pf ("filter2EnvS", "Filter 2 Env Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("filter2EnvR", "Filter 2 Env Release",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.30f, sec);

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "driveOn", 1 }, "Overdrive On", true));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "driveType", 1 }, "Drive Type",
        juce::StringArray { "Soft", "Cubic", "Hard Clip", "Tube", "Diode", "Fuzz",
                            "Rectify", "Wavefold", "Foldback" }, 0));
    pf ("drive", "Overdrive", juce::NormalisableRange<float> (1.0f, 50.0f, 0.0f, 0.3f), 1.0f, mult);

    pf ("bias", "Overdrive Bias", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("crushBits", "Bit Crush", juce::NormalisableRange<float> (1.0f, 16.0f), 16.0f,
        sv ([] (float v) { return v >= 15.95f ? juce::String ("off") : juce::String (v, 1) + " bit"; },
            [] (const juce::String& t) { return t.startsWithIgnoreCase ("off") ? 16.0f : t.getFloatValue(); }));
    pf ("downsample", "Downsample", juce::NormalisableRange<float> (1.0f, 50.0f, 1.0f), 1.0f,
        sv ([] (float v) { return v <= 1.0f ? juce::String ("off") : juce::String ("/") + juce::String ((int) v); },
            [] (const juce::String& t) { return t.startsWithIgnoreCase ("off") ? 1.0f : t.getFloatValue(); }));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "drivePos", 1 }, "Drive Position",
        juce::StringArray { "Post Filter", "Pre Filter" }, 0));

    pf ("gain", "Gain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.80f, pct);

    // Velocity sensitivity + CZ noise pitch modulation.
    pf ("ampVelSens", "Amp Vel Sens", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct);
    pf ("filterVelSens", "Filter Vel Sens", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("noiseMod", "Noise Mod", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    pf ("pan", "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, pan);
    pf ("panSpread", "Pan Spread", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("drift", "Analog Drift", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    // --- Unison ---
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "unisonVoices", 1 }, "Unison Voices", 1, 6, 1));
    // 0.5 = even spacing (the historical distribution); below bunches toward
    // the centre pitch, above pushes the stack to the edges of the detune range.
    pf ("unisonSpread", "Unison Spread", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    // Per-voice send into the chorus/delay/reverb group. 1 = the whole voice
    // goes through them, which is what the chain did before it became a send.
    pf ("fxSend", "FX Send", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct);
    pf ("unisonDetune", "Unison Detune", juce::NormalisableRange<float> (0.0f, 50.0f), 15.0f, cnt);
    pf ("unisonWidth", "Unison Width", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);

    // --- Output compressor (ratio 1 = bypass) ---
    pf ("compThreshold", "Comp Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f), -12.0f, db);
    pf ("compRatio", "Comp Ratio", juce::NormalisableRange<float> (1.0f, 20.0f, 0.0f, 0.4f), 1.0f, ratio);
    pf ("compAttack", "Comp Attack",
        juce::NormalisableRange<float> (0.0005f, 0.2f, 0.0f, 0.3f), 0.005f, sec);
    pf ("compRelease", "Comp Release",
        juce::NormalisableRange<float> (0.01f, 1.0f, 0.0f, 0.3f), 0.10f, sec);
    pf ("compMakeup", "Comp Makeup", juce::NormalisableRange<float> (0.0f, 24.0f), 0.0f, db);
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "compOn", 1 }, "Compressor On", true));

    // --- Tempo ---
    // "Local" overrides the host outright; "Host" follows the host but still
    // falls back to this knob when the host reports no tempo, which is what
    // makes the standalone build (and any host without a transport) usable.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "tempoMode", 1 }, "Tempo Source",
        juce::StringArray { "Tempo: Host", "Tempo: Local" }, 0));
    pf ("internalBpm", "Local BPM",
        juce::NormalisableRange<float> (20.0f, 300.0f), 120.0f, bpmAttr);

    // Envelope tempo sync: one switch per envelope, not per stage. Turning it on
    // snaps that envelope's attack, decay and release to whichever note division
    // each is nearest at the current tempo, so the knobs keep working exactly as
    // they did and the whole envelope moves with the tempo together.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "ampEnvSync", 1 }, "Amp Env Sync", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "filtEnvSync", 1 }, "Filter Env Sync", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "filt2EnvSync", 1 }, "Filter 2 Env Sync", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "modEnvSync", 1 }, "Mod Env Sync", false));

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

    // --- Arpeggiator ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "arpOn", 1 }, "Arp On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "arpMode", 1 }, "Arp Mode",
        juce::StringArray { "Up", "Down", "Up-Down", "Random", "As Played" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "arpRate", 1 }, "Arp Rate",
        juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4.", "1/8.", "1/4T", "1/8T" }, 4));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "arpOctaves", 1 }, "Arp Octaves", 1, 4, 1));
    pf ("arpGate", "Arp Gate", juce::NormalisableRange<float> (0.05f, 1.0f), 0.5f, pct);
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "arpLatch", 1 }, "Arp Latch", false));
    // Which layer the arp drives. "Both" is index 0 so existing patches keep
    // their behaviour. A layer the arp does not target still receives the held
    // notes directly, so e.g. Bass Only arpeggiates the sub while the poly
    // voices hold the chord.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "arpTarget", 1 }, "Arp Target",
        juce::StringArray { "Arp: Poly + Bass", "Arp: Poly Only", "Arp: Bass Only" }, 0));

    // --- Chorus / ensemble ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "chorusOn", 1 }, "Chorus On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "chorusMode", 1 }, "Chorus Mode",
        juce::StringArray { "I", "II", "I+II" }, 2));
    pf ("chorusRate", "Chorus Rate", juce::NormalisableRange<float> (0.05f, 5.0f, 0.0f, 0.4f), 0.5f, rate);
    pf ("chorusDepth", "Chorus Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    pf ("chorusMix", "Chorus Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);

    // --- Reverb ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "reverbOn", 1 }, "Reverb On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fxRouting", 1 }, "Delay/Reverb Routing",
        juce::StringArray { "Delay -> Reverb", "Reverb -> Delay", "Reverb, Dry Delay" }, 0));
    pf ("reverbSize", "Reverb Size", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    pf ("reverbDamp", "Reverb Damp", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    pf ("reverbWidth", "Reverb Width", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct);
    pf ("reverbMix", "Reverb Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct);

    // --- Delay (mix 0 = bypass) ---
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayMode", 1 }, "Delay Mode",
        juce::StringArray { "Mono", "Stereo", "Ping-Pong" }, 1));
    // One switch for both taps, matching the envelopes: on, each time knob
    // snaps to the note division it is nearest. This replaces a pair of
    // per-tap "Free / 1-1 / 1-2 ..." dropdowns, which took up most of the card
    // and made the time knobs beside them look inert.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "delaySync", 1 }, "Delay Sync", false));
    pf ("delayTimeL", "Delay Time L",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.0f, 0.3f), 0.30f, sec);
    pf ("delayTimeR", "Delay Time R",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.0f, 0.3f), 0.45f, sec);
    pf ("delayFeedback", "Delay Feedback", juce::NormalisableRange<float> (0.0f, 0.95f), 0.30f, pct);
    pf ("delayMix", "Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("delayDuck", "Delay Ducking", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "delayOn", 1 }, "Delay On", true));

    // --- Global master EQ (final stage; 0 dB per band = transparent) ---
    const juce::NormalisableRange<float> geGainRange (-18.0f, 18.0f);
    pf ("geLowFreq",  "EQ Low Freq",
        juce::NormalisableRange<float> (20.0f, 1000.0f, 0.0f, 0.3f), 120.0f, hz);
    pf ("geLowGain",  "EQ Low Gain",  geGainRange, 0.0f, db);
    pf ("geMid1Freq", "EQ Mid 1 Freq",
        juce::NormalisableRange<float> (100.0f, 4000.0f, 0.0f, 0.3f), 500.0f, hz);
    pf ("geMid1Gain", "EQ Mid 1 Gain", geGainRange, 0.0f, db);
    pf ("geMid2Freq", "EQ Mid 2 Freq",
        juce::NormalisableRange<float> (500.0f, 12000.0f, 0.0f, 0.3f), 2000.0f, hz);
    pf ("geMid2Gain", "EQ Mid 2 Gain", geGainRange, 0.0f, db);
    pf ("geHighFreq", "EQ High Freq",
        juce::NormalisableRange<float> (1500.0f, 18000.0f, 0.0f, 0.3f), 8000.0f, hz);
    pf ("geHighGain", "EQ High Gain", geGainRange, 0.0f, db);
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "globalEqOn", 1 }, "Global EQ On", true));

    // --- Monophonic sub-bass layer ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bassOn", 1 }, "Bass On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "bassWave", 1 }, "Bass Wave",
        juce::StringArray { "Saw", "Square", "Triangle", "Pulse" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "bassOctave", 1 }, "Bass Octave", -3, 3, -1));
    pf ("bassTune", "Bass Tune", juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f, cnt);
    pf ("bassHarmonics", "Bass Harmonics", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("bassLevel", "Bass Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.80f, pct);
    pf ("bassGlide", "Bass Glide",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.3f), 0.05f, sec);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "bassPriority", 1 }, "Bass Priority",
        juce::StringArray { "Last", "Top", "Bottom" }, 0));
    pf ("bassAttack", "Bass Attack",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.005f, sec);
    pf ("bassDecay", "Bass Decay",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f, sec);
    pf ("bassSustain", "Bass Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.80f, pct);
    pf ("bassRelease", "Bass Release",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f, sec);

    // --- Master output ---
    pf ("masterLevel", "Master Level", juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, db);
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "masterLimiter", 1 }, "Master Limiter", true));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "osQuality", 1 }, "Oversampling",
        juce::StringArray { "1x", "2x", "4x", "8x" }, 2));   // default 4x

    // --- v6.0: Voice allocation ---
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "polyphony", 1 }, "Polyphony", 1, 16, 16));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "voiceMode", 1 }, "Voice Mode",
        juce::StringArray { "Poly", "Mono", "Legato", "Unison Legato" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "notePriority", 1 }, "Note Priority",
        juce::StringArray { "Last", "Top", "Bottom" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "stealPolicy", 1 }, "Steal Policy",
        juce::StringArray { "Oldest", "Quietest" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "monoRetrigger", 1 }, "Retrigger", true));
    pf ("pitchBendRange", "Pitch Bend Range",
        juce::NormalisableRange<float> (1.0f, 24.0f), 2.0f, cnt);
    pf ("masterTune", "Master Tune",
        juce::NormalisableRange<float> (415.0f, 465.0f), 440.0f,
        sv ([] (float v) { return juce::String (v, 1) + " Hz"; },
            [] (const juce::String& t) { return t.getFloatValue(); }));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "transpose", 1 }, "Transpose", -24, 24, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "velCurve", 1 }, "Velocity Curve",
        juce::StringArray { "Linear", "Soft", "Hard", "Fixed" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "tuningScale", 1 }, "Tuning",
        juce::StringArray { "Equal", "Just", "Pythagorean" }, 0));

    // --- Glide / portamento ---
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "glideMode", 1 }, "Glide Mode",
        juce::StringArray { "Off", "Always", "Legato" }, 0));
    pf ("glideTime", "Glide Time",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.0f, 0.3f), 0.10f, sec);
    pf ("glideCurve", "Glide Curve",
        juce::NormalisableRange<float> (0.25f, 4.0f, 0.0f, 0.5f), 1.0f, num2);

    pf ("attack", "Attack",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.01f, sec);

    pf ("decay", "Decay",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.10f, sec);

    pf ("sustain", "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.70f, pct);

    pf ("release", "Release",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f, sec);

    // --- Modulation ---
    const juce::StringArray lfoWaveNames { "Sine", "Triangle", "Square", "Saw",
                                           "Ramp Down", "Sample & Hold", "Smooth Random",
                                           "Exponential" };
    pf ("lfoRate", "LFO Rate", juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 5.0f, rate);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfoWave", 1 }, "LFO Wave", lfoWaveNames, 0));
    const juce::StringArray syncNames { "Free", "1/1", "1/2", "1/4", "1/8", "1/16",
                                        "1/4.", "1/8.", "1/4T", "1/8T" };
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfoSync", 1 }, "LFO Sync", syncNames, 0));
    pf ("lfoFade", "LFO Fade In", juce::NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.4f), 0.0f, sec);
    pf ("lfoPhase", "LFO Phase", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfoRetrig", 1 }, "LFO Retrigger",
        juce::StringArray { "Free", "Retrig" }, 1));
    pf ("lfo2Rate", "LFO 2 Rate", juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 0.5f, rate);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfo2Wave", 1 }, "LFO 2 Wave", lfoWaveNames, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfo2Sync", 1 }, "LFO 2 Sync", syncNames, 0));
    pf ("lfo2Fade", "LFO 2 Fade In", juce::NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.4f), 0.0f, sec);
    pf ("lfo2Phase", "LFO 2 Phase", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfo2Retrig", 1 }, "LFO 2 Retrigger",
        juce::StringArray { "Free", "Retrig" }, 1));

    // Global LFO + macros (sources for the global modulation pass).
    pf ("globalLfoRate", "Global LFO Rate",
        juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 0.5f, rate);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "globalLfoWave", 1 }, "Global LFO Wave", lfoWaveNames, 0));
    pf ("macro1", "Macro 1", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("macro2", "Macro 2", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    pf ("modEnvA", "Mod Env Attack",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.01f, sec);
    pf ("modEnvD", "Mod Env Decay",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f, sec);
    pf ("modEnvS", "Mod Env Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("modEnvR", "Mod Env Release",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.30f, sec);

    // --- CZ-style multi-stage envelope (8 rate/level stages) ---
    pf ("czAmount", "Multi Env Amount", juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f, oct);
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "czSustain", 1 }, "Multi Env Sustain", 1, 8, 5));
    const float czRateDef[8]  = { 0.02f, 0.15f, 0.10f, 0.30f, 0.50f, 0.40f, 0.60f, 0.50f };
    const float czLevelDef[8] = { 1.00f, 0.80f, 0.60f, 0.50f, 0.50f, 0.30f, 0.15f, 0.00f };
    for (int i = 1; i <= 8; ++i)
    {
        const auto s = juce::String (i);
        pf ("czRate" + s, "Multi Env Rate " + s,
            juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), czRateDef[i - 1], sec);
        pf ("czLevel" + s, "Multi Env Level " + s,
            juce::NormalisableRange<float> (0.0f, 1.0f), czLevelDef[i - 1], pct);
    }

    // --- CZ-style 8-stage pitch (DCO) envelope. Levels are bipolar around 0.5
    //     (0.5 = no pitch offset); pitchEnvAmount scales the deviation in semis.
    pf ("pitchEnvAmount", "Pitch Env Amount", juce::NormalisableRange<float> (-48.0f, 48.0f), 0.0f, cnt);
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "pitchEnvSustain", 1 }, "Pitch Env Sustain", 1, 8, 8));
    const float peRateDef[8]  = { 0.02f, 0.20f, 0.30f, 0.40f, 0.50f, 0.50f, 0.50f, 0.50f };
    const float peLevelDef[8] = { 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f };
    for (int i = 1; i <= 8; ++i)
    {
        const auto s = juce::String (i);
        pf ("pitchEnvRate" + s, "Pitch Env Rate " + s,
            juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), peRateDef[i - 1], sec);
        pf ("pitchEnvLevel" + s, "Pitch Env Level " + s,
            juce::NormalisableRange<float> (0.0f, 1.0f), peLevelDef[i - 1], pct);
    }

    // --- CZ-style 8-stage DCW (wave-depth) envelope (bipolar around 0.5). ---
    pf ("dcwEnvAmount", "DCW Env Amount", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, pct);
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "dcwEnvSustain", 1 }, "DCW Env Sustain", 1, 8, 8));
    for (int i = 1; i <= 8; ++i)
    {
        const auto s = juce::String (i);
        pf ("dcwEnvRate" + s, "DCW Env Rate " + s,
            juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), peRateDef[i - 1], sec);
        pf ("dcwEnvLevel" + s, "DCW Env Level " + s,
            juce::NormalisableRange<float> (0.0f, 1.0f), peLevelDef[i - 1], pct);
    }

    const juce::StringArray srcNames { "None", "Mod Env", "LFO", "Velocity", "Pressure",
                                       "Timbre", "Pitch Bend", "Key Track", "Mod Wheel", "LFO 2",
                                       "Multi Env", "Amp Env", "Filt Env A", "Filt Env B",
                                       "Random", "Global LFO", "Macro 1", "Macro 2", "Pitch Env" };
    const juce::StringArray dstNames { "None", "Pitch", "PD Amount", "Pulse Width", "Cutoff",
                                       "Resonance", "Morph", "Drive", "Amplitude", "Pan",
                                       "Osc A Lvl", "Osc B Lvl", "Detune", "Filter 2 Cutoff",
                                       "LFO Rate", "LFO 2 Rate", "Noise Lvl",
                                       "Delay Mix", "Delay Fbk", "Master Pan", "Global EQ",
                                       "Ring Mod", "Cross Mod", "Engine", "PD Amount B",
                                       "Pluck Decay", "Pluck Damp", "Chorus Depth", "Reverb Mix", "FX Send" };
    for (int i = 1; i <= pdhybrid::ModMatrix::kNumSlots; ++i)
    {
        const auto s = juce::String (i);
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mod" + s + "Source", 1 }, "Mod " + s + " Source", srcNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mod" + s + "Dest", 1 }, "Mod " + s + " Dest", dstNames, 0));
        pf ("mod" + s + "Depth", "Mod " + s + " Depth",
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, amt);
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mod" + s + "Curve", 1 }, "Mod " + s + " Curve",
            juce::StringArray { "Linear", "Exp", "S-Curve" }, 0));
    }

    return { params.begin(), params.end() };
}

PDHybridAudioProcessor::PDHybridAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    // Reported here as well as in prepareToPlay so the host sees it before
    // prepare. Both terms are in host-rate samples and independent of sample
    // rate, so the constant holds everywhere.
    setLatencySamples (kTotalLatencySamples);
}

void PDHybridAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setSampleRate (sampleRate);
    compressor.setSampleRate (sampleRate);
    compressor.reset();
    chorus.setSampleRate (sampleRate);
    chorus.reset();
    delay.setSampleRate (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    delay.reset();
    globalEq.setSampleRate (sampleRate);
    globalEq.reset();
    monoBass.setSampleRate (sampleRate);
    monoBass.reset();
    master.setSampleRate (sampleRate);
    master.reset();
    globalLfo.setSampleRate (sampleRate);
    globalLfo.reset();
    const auto n = static_cast<std::size_t> (juce::jmax (1, samplesPerBlock));
    scratchL.assign (n, 0.0f);
    scratchR.assign (n, 0.0f);
    scratchBass.assign (n, 0.0f);
    fxScratchL_.assign (n, 0.0f);
    fxScratchR_.assign (n, 0.0f);

    // Two fixed contributions, both measurable with pdhybrid_latency:
    //
    //   8  the oscillator anti-alias decimation FIR (16 taps/phase, linear phase)
    //   15 the output stage's 4x soft-clip knee oversampler
    //
    // The second used to be missing here, so the host under-compensated by 15
    // samples whenever the limiter was on -- which is the default. It was also
    // genuinely conditional, because the oversampler was skipped with the
    // limiter off; MasterStage now runs a matching plain delay in that case, so
    // the figure below is true in every state rather than in one of them.
    //
    // Still uncompensated by choice: the small extra delay when overdrive is
    // engaged, and the 0 delay at 1x oversampling, both of which would mean
    // changing latency mid-session.
    setLatencySamples (kTotalLatencySamples);
}

void PDHybridAudioProcessor::pushParams()
{
    pdhybrid::SynthParams p;

    // Tempo is resolved first: the envelopes, LFOs, delay and arp all need it.
    // "Host" mode still falls back to the internal BPM when the host reports
    // none, so the standalone build follows the knob rather than a fixed 120.
    const double internalBpm = apvts.getRawParameterValue ("internalBpm")->load();
    const int tempoMode = static_cast<int> (apvts.getRawParameterValue ("tempoMode")->load());
    double bpm = internalBpm;
    if (tempoMode == 0)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    if (*b > 1.0)
                        bpm = *b;
    currentBpm_.store (bpm, std::memory_order_relaxed);

    // One sync switch per envelope: off leaves the knob in seconds, on snaps
    // each stage to the note division it is nearest at the current tempo.
    auto envSynced = [&] (const char* syncId)
    { return apvts.getRawParameterValue (syncId)->load() > 0.5f; };
    auto envTime = [&] (bool sync, double seconds)
    { return pdhybrid::syncedEnvTime (seconds, bpm, sync); };

    auto readOscGroup = [&] (const juce::String& id,
                             pdhybrid::OscType& type, int& wave, double& amount,
                             double& pw, int& octave, int& semi, double& fine, double& level,
                             double& eqLow, double& eqMid, double& eqHigh)
    {
        type   = static_cast<pdhybrid::OscType> (
                     static_cast<int> (apvts.getRawParameterValue (id + "Type")->load()));
        wave   = static_cast<int> (apvts.getRawParameterValue (id + "Wave")->load());
        amount = apvts.getRawParameterValue (id + "Amount")->load();
        pw     = apvts.getRawParameterValue (id + "PulseWidth")->load();
        octave = static_cast<int> (apvts.getRawParameterValue (id + "Octave")->load());
        semi   = static_cast<int> (apvts.getRawParameterValue (id + "Semi")->load());
        fine   = apvts.getRawParameterValue (id + "Fine")->load();
        level  = apvts.getRawParameterValue (id + "Level")->load();
        eqLow  = apvts.getRawParameterValue (id + "EqLow")->load();
        eqMid  = apvts.getRawParameterValue (id + "EqMid")->load();
        eqHigh = apvts.getRawParameterValue (id + "EqHigh")->load();
    };
    readOscGroup ("oscA", p.oscAType, p.oscAWave, p.oscAAmount, p.oscAPulseWidth,
                  p.oscAOctave, p.oscASemi, p.oscAFine, p.oscALevel,
                  p.oscAEqLow, p.oscAEqMid, p.oscAEqHigh);
    readOscGroup ("oscB", p.oscBType, p.oscBWave, p.oscBAmount, p.oscBPulseWidth,
                  p.oscBOctave, p.oscBSemi, p.oscBFine, p.oscBLevel,
                  p.oscBEqLow, p.oscBEqMid, p.oscBEqHigh);
    p.oscAWave2   = static_cast<int> (apvts.getRawParameterValue ("oscAWave2")->load());
    p.oscACombine = apvts.getRawParameterValue ("oscACombine")->load() > 0.5f;
    p.oscBWave2   = static_cast<int> (apvts.getRawParameterValue ("oscBWave2")->load());
    p.oscBCombine = apvts.getRawParameterValue ("oscBCombine")->load() > 0.5f;
    p.oscAEngine  = apvts.getRawParameterValue ("oscAEngine")->load();
    p.oscBEngine  = apvts.getRawParameterValue ("oscBEngine")->load();
    p.oscAExcite  = static_cast<int> (apvts.getRawParameterValue ("oscAExcite")->load());
    p.oscBExcite  = static_cast<int> (apvts.getRawParameterValue ("oscBExcite")->load());
    p.oscAOn      = apvts.getRawParameterValue ("oscAOn")->load() > 0.5f;
    p.oscBOn      = apvts.getRawParameterValue ("oscBOn")->load() > 0.5f;
    p.noiseLevel  = apvts.getRawParameterValue ("noiseLevel")->load();
    p.ringModLevel = apvts.getRawParameterValue ("ringMod")->load();
    p.oscCrossMod    = static_cast<int> (apvts.getRawParameterValue ("oscCrossMod")->load());
    p.crossModAmount = apvts.getRawParameterValue ("crossModAmount")->load();

    p.pluckOn         = apvts.getRawParameterValue ("pluckOn")->load() > 0.5f;
    p.pluckDecay      = apvts.getRawParameterValue ("pluckDecay")->load();
    p.pluckDamp       = apvts.getRawParameterValue ("pluckDamp")->load();
    p.pluckDispersion = apvts.getRawParameterValue ("pluckDispersion")->load();
    p.pluckBurstMs    = apvts.getRawParameterValue ("pluckBurst")->load();
    p.pluckMix        = apvts.getRawParameterValue ("pluckMix")->load();

    p.vibratoOn    = apvts.getRawParameterValue ("vibratoOn")->load() > 0.5f;
    p.vibratoWave  = static_cast<int> (apvts.getRawParameterValue ("vibratoWave")->load());
    p.vibratoRate  = apvts.getRawParameterValue ("vibratoRate")->load();
    p.vibratoDepth = apvts.getRawParameterValue ("vibratoDepth")->load();
    p.vibratoDelay = apvts.getRawParameterValue ("vibratoDelay")->load();

    p.cutoffHz    = apvts.getRawParameterValue ("cutoff")->load();
    p.resonance   = apvts.getRawParameterValue ("resonance")->load();
    p.filterType  = static_cast<pdhybrid::FilterType> (
                        static_cast<int> (apvts.getRawParameterValue ("filterType")->load()));
    p.filterMorph = apvts.getRawParameterValue ("filterMorph")->load();
    p.keyTrack        = apvts.getRawParameterValue ("keyTrack")->load();
    p.filterEnvAmount = apvts.getRawParameterValue ("filterEnvAmount")->load();
    const bool filtEnvSync = envSynced ("filtEnvSync");
    p.filterEnvA  = envTime (filtEnvSync, apvts.getRawParameterValue ("filterEnvA")->load());
    p.filterEnvD  = envTime (filtEnvSync, apvts.getRawParameterValue ("filterEnvD")->load());
    p.filterEnvS  = apvts.getRawParameterValue ("filterEnvS")->load();
    p.filterEnvR  = envTime (filtEnvSync, apvts.getRawParameterValue ("filterEnvR")->load());
    p.filterRouting = static_cast<pdhybrid::FilterRouting> (
        static_cast<int> (apvts.getRawParameterValue ("filterRouting")->load()));
    p.filter2Type   = static_cast<pdhybrid::FilterType> (
        static_cast<int> (apvts.getRawParameterValue ("filter2Type")->load()));
    p.filter2Cutoff = apvts.getRawParameterValue ("filter2Cutoff")->load();
    p.filter2Res    = apvts.getRawParameterValue ("filter2Res")->load();
    p.filter2Morph  = apvts.getRawParameterValue ("filter2Morph")->load();
    p.filter2EnvAmount = apvts.getRawParameterValue ("filter2EnvAmount")->load();
    const bool filt2EnvSync = envSynced ("filt2EnvSync");
    p.filter2EnvA = envTime (filt2EnvSync, apvts.getRawParameterValue ("filter2EnvA")->load());
    p.filter2EnvD = envTime (filt2EnvSync, apvts.getRawParameterValue ("filter2EnvD")->load());
    p.filter2EnvS = apvts.getRawParameterValue ("filter2EnvS")->load();
    p.filter2EnvR = envTime (filt2EnvSync, apvts.getRawParameterValue ("filter2EnvR")->load());
    p.driveOn     = apvts.getRawParameterValue ("driveOn")->load() > 0.5f;
    p.drive       = apvts.getRawParameterValue ("drive")->load();
    p.driveType   = static_cast<int> (apvts.getRawParameterValue ("driveType")->load());
    p.crushBits   = apvts.getRawParameterValue ("crushBits")->load();
    p.downsample  = apvts.getRawParameterValue ("downsample")->load();
    p.drivePos    = static_cast<int> (apvts.getRawParameterValue ("drivePos")->load());
    p.ampVelSens    = apvts.getRawParameterValue ("ampVelSens")->load();
    p.filterVelSens = apvts.getRawParameterValue ("filterVelSens")->load();
    p.noiseModDepth = apvts.getRawParameterValue ("noiseMod")->load();
    p.bias      = apvts.getRawParameterValue ("bias")->load();
    const bool ampEnvSyncOn = envSynced ("ampEnvSync");
    p.attack    = envTime (ampEnvSyncOn, apvts.getRawParameterValue ("attack")->load());
    p.decay     = envTime (ampEnvSyncOn, apvts.getRawParameterValue ("decay")->load());
    p.sustain   = apvts.getRawParameterValue ("sustain")->load();
    p.release   = envTime (ampEnvSyncOn, apvts.getRawParameterValue ("release")->load());
    p.gain      = apvts.getRawParameterValue ("gain")->load();
    const int osIdx = static_cast<int> (apvts.getRawParameterValue ("osQuality")->load());
    const int osFactor[] = { 1, 2, 4, 8 };
    p.oscOversampling = osFactor[juce::jlimit (0, 3, osIdx)];
    p.pan       = apvts.getRawParameterValue ("pan")->load();
    p.panSpread = apvts.getRawParameterValue ("panSpread")->load();
    p.drift     = apvts.getRawParameterValue ("drift")->load();
    p.unisonVoices = static_cast<int> (apvts.getRawParameterValue ("unisonVoices")->load());
    p.unisonDetune = apvts.getRawParameterValue ("unisonDetune")->load();
    p.unisonWidth  = apvts.getRawParameterValue ("unisonWidth")->load();
    p.wavetable    = wavetable_;   // shared; null keeps the built-in default set
    p.unisonSpread = apvts.getRawParameterValue ("unisonSpread")->load();
    p.fxSend       = apvts.getRawParameterValue ("fxSend")->load();
    p.glideMode = static_cast<pdhybrid::GlideMode> (
        static_cast<int> (apvts.getRawParameterValue ("glideMode")->load()));
    p.glideTime  = apvts.getRawParameterValue ("glideTime")->load();
    p.glideCurve = apvts.getRawParameterValue ("glideCurve")->load();

    compressor.setThreshold (apvts.getRawParameterValue ("compThreshold")->load());
    compressor.setRatio     (apvts.getRawParameterValue ("compRatio")->load());
    compressor.setAttack    (apvts.getRawParameterValue ("compAttack")->load());
    compressor.setRelease   (apvts.getRawParameterValue ("compRelease")->load());
    compressor.setMakeup    (apvts.getRawParameterValue ("compMakeup")->load());

    // Arpeggiator (step length from tempo; note events generated in processBlock).
    chordOn_ = apvts.getRawParameterValue ("chordOn")->load() > 0.5f;
    chordSplitCached_ = static_cast<int> (apvts.getRawParameterValue ("chordSplit")->load());
    chord_.setEnabled   (chordOn_);
    chord_.setSplitNote (chordSplitCached_);
    // Push the quality only when the *parameter* moved. Pushing every block
    // would overwrite a latch just set by a quality key, undoing the key press
    // before it was ever audible.
    {
        const int q = static_cast<int> (apvts.getRawParameterValue ("chordQuality")->load());
        if (q != chordQualitySeen_)
        {
            chord_.setQuality (q);
            chordQualitySeen_ = q;
        }
    }
    chord_.setVoicing   (static_cast<int> (apvts.getRawParameterValue ("chordVoicing")->load()));
    chord_.setSpread    (apvts.getRawParameterValue ("chordSpread")->load());
    chord_.setOctave    (static_cast<int> (apvts.getRawParameterValue ("chordOctave")->load()));

    arpOn_ = apvts.getRawParameterValue ("arpOn")->load() > 0.5f;
    arpTarget_ = static_cast<int> (apvts.getRawParameterValue ("arpTarget")->load());
    {
        const int arpRate = static_cast<int> (apvts.getRawParameterValue ("arpRate")->load());
        arp_.setStepSamples (pdhybrid::syncedDelaySeconds (bpm, arpRate) * getSampleRate());
        arp_.setMode    (static_cast<int> (apvts.getRawParameterValue ("arpMode")->load()));
        arp_.setOctaves (static_cast<int> (apvts.getRawParameterValue ("arpOctaves")->load()));
        arp_.setGate    (apvts.getRawParameterValue ("arpGate")->load());
        arp_.setLatch   (apvts.getRawParameterValue ("arpLatch")->load() > 0.5f);
    }

    delay.setMode (static_cast<pdhybrid::DelayMode> (
        static_cast<int> (apvts.getRawParameterValue ("delayMode")->load())));
    // Both taps snap independently to whichever division each is nearest, so
    // the classic offset pair (say 1/8 against 1/8.) still works from one switch.
    const bool delaySyncOn = apvts.getRawParameterValue ("delaySync")->load() > 0.5f;
    const double delayL = pdhybrid::syncedEnvTime (
        apvts.getRawParameterValue ("delayTimeL")->load(), bpm, delaySyncOn,
        pdhybrid::Delay::kMaxDelaySeconds);
    const double delayR = pdhybrid::syncedEnvTime (
        apvts.getRawParameterValue ("delayTimeR")->load(), bpm, delaySyncOn,
        pdhybrid::Delay::kMaxDelaySeconds);
    delay.setTimes    (delayL, delayR);
    delay.setFeedback (apvts.getRawParameterValue ("delayFeedback")->load());
    delay.setMix      (apvts.getRawParameterValue ("delayMix")->load());
    delay.setDuck     (apvts.getRawParameterValue ("delayDuck")->load());

    chorusOn_ = apvts.getRawParameterValue ("chorusOn")->load() > 0.5f;
    chorus.setMode  (static_cast<int> (apvts.getRawParameterValue ("chorusMode")->load()));
    chorus.setRate  (apvts.getRawParameterValue ("chorusRate")->load());
    chorus.setDepth (apvts.getRawParameterValue ("chorusDepth")->load());
    chorus.setMix   (apvts.getRawParameterValue ("chorusMix")->load());

    reverbOn_  = apvts.getRawParameterValue ("reverbOn")->load() > 0.5f;
    fxRouting_ = static_cast<int> (apvts.getRawParameterValue ("fxRouting")->load());
    reverb.setSize  (apvts.getRawParameterValue ("reverbSize")->load());
    reverb.setDamp  (apvts.getRawParameterValue ("reverbDamp")->load());
    reverb.setWidth (apvts.getRawParameterValue ("reverbWidth")->load());
    reverb.setMix   (apvts.getRawParameterValue ("reverbMix")->load());

    // Master EQ bands (high-shelf gain is further modulated per block below).
    globalEq.setBand (pdhybrid::GlobalEq::LowShelf,
                      apvts.getRawParameterValue ("geLowFreq")->load(),
                      apvts.getRawParameterValue ("geLowGain")->load());
    globalEq.setBand (pdhybrid::GlobalEq::Mid1,
                      apvts.getRawParameterValue ("geMid1Freq")->load(),
                      apvts.getRawParameterValue ("geMid1Gain")->load());
    globalEq.setBand (pdhybrid::GlobalEq::Mid2,
                      apvts.getRawParameterValue ("geMid2Freq")->load(),
                      apvts.getRawParameterValue ("geMid2Gain")->load());
    eqHighFreqBase_ = apvts.getRawParameterValue ("geHighFreq")->load();
    eqHighGainBase_ = apvts.getRawParameterValue ("geHighGain")->load();
    globalEq.setBand (pdhybrid::GlobalEq::HighShelf, eqHighFreqBase_, eqHighGainBase_);

    // Mono sub-bass configuration (note events are routed in handleMidiMessage).
    compOn_     = apvts.getRawParameterValue ("compOn")->load() > 0.5f;
    delayOn_    = apvts.getRawParameterValue ("delayOn")->load() > 0.5f;
    globalEqOn_ = apvts.getRawParameterValue ("globalEqOn")->load() > 0.5f;

    monoBass.setEnabled  (apvts.getRawParameterValue ("bassOn")->load() > 0.5f);
    monoBass.setWaveform (static_cast<pdhybrid::AnalogWave> (
        static_cast<int> (apvts.getRawParameterValue ("bassWave")->load())));
    monoBass.setOctave   (static_cast<int> (apvts.getRawParameterValue ("bassOctave")->load()));
    monoBass.setTuneCents(apvts.getRawParameterValue ("bassTune")->load());
    monoBass.setHarmonics(apvts.getRawParameterValue ("bassHarmonics")->load());
    monoBass.setLevel    (apvts.getRawParameterValue ("bassLevel")->load());
    monoBass.setGlideTime(apvts.getRawParameterValue ("bassGlide")->load());
    monoBass.setPriority (static_cast<pdhybrid::BassPriority> (
        static_cast<int> (apvts.getRawParameterValue ("bassPriority")->load())));
    monoBass.setADSR (apvts.getRawParameterValue ("bassAttack")->load(),
                      apvts.getRawParameterValue ("bassDecay")->load(),
                      apvts.getRawParameterValue ("bassSustain")->load(),
                      apvts.getRawParameterValue ("bassRelease")->load());
    monoBass.setMasterTune (apvts.getRawParameterValue ("masterTune")->load(),
                            static_cast<int> (apvts.getRawParameterValue ("transpose")->load()),
                            static_cast<int> (apvts.getRawParameterValue ("tuningScale")->load()));

    master.setGainDb (apvts.getRawParameterValue ("masterLevel")->load());
    master.setLimiterEnabled (apvts.getRawParameterValue ("masterLimiter")->load() > 0.5f);

    // v6.0: Voice allocation
    p.polyphony      = static_cast<int> (apvts.getRawParameterValue ("polyphony")->load());
    p.voiceMode      = static_cast<int> (apvts.getRawParameterValue ("voiceMode")->load());
    p.notePriority   = static_cast<int> (apvts.getRawParameterValue ("notePriority")->load());
    p.stealPolicy    = static_cast<int> (apvts.getRawParameterValue ("stealPolicy")->load());
    p.monoRetrigger  = apvts.getRawParameterValue ("monoRetrigger")->load() > 0.5f;
    p.pitchBendRange = apvts.getRawParameterValue ("pitchBendRange")->load();
    pitchBendRangeSemis = p.pitchBendRange;   // used when converting MIDI bend
    p.masterTuneHz = apvts.getRawParameterValue ("masterTune")->load();
    p.transpose    = static_cast<int> (apvts.getRawParameterValue ("transpose")->load());
    p.tuningScale  = static_cast<int> (apvts.getRawParameterValue ("tuningScale")->load());
    velCurve_      = static_cast<int> (apvts.getRawParameterValue ("velCurve")->load());

    const int lfoSync  = static_cast<int> (apvts.getRawParameterValue ("lfoSync")->load());
    const int lfo2Sync = static_cast<int> (apvts.getRawParameterValue ("lfo2Sync")->load());
    p.lfoRate  = (lfoSync == 0) ? apvts.getRawParameterValue ("lfoRate")->load()
                                : pdhybrid::syncedLfoHz (bpm, lfoSync - 1);
    p.lfo2Rate = (lfo2Sync == 0) ? apvts.getRawParameterValue ("lfo2Rate")->load()
                                 : pdhybrid::syncedLfoHz (bpm, lfo2Sync - 1);
    p.lfoWave  = static_cast<int> (apvts.getRawParameterValue ("lfoWave")->load());
    p.lfo2Wave = static_cast<int> (apvts.getRawParameterValue ("lfo2Wave")->load());
    p.lfoFade   = apvts.getRawParameterValue ("lfoFade")->load();
    p.lfoPhase  = apvts.getRawParameterValue ("lfoPhase")->load();
    p.lfoRetrig = apvts.getRawParameterValue ("lfoRetrig")->load() > 0.5f;
    p.lfo2Fade   = apvts.getRawParameterValue ("lfo2Fade")->load();
    p.lfo2Phase  = apvts.getRawParameterValue ("lfo2Phase")->load();
    p.lfo2Retrig = apvts.getRawParameterValue ("lfo2Retrig")->load() > 0.5f;
    const bool modEnvSyncOn = envSynced ("modEnvSync");
    p.modEnvA = envTime (modEnvSyncOn, apvts.getRawParameterValue ("modEnvA")->load());
    p.modEnvD = envTime (modEnvSyncOn, apvts.getRawParameterValue ("modEnvD")->load());
    p.modEnvS = apvts.getRawParameterValue ("modEnvS")->load();
    p.modEnvR = envTime (modEnvSyncOn, apvts.getRawParameterValue ("modEnvR")->load());

    p.czAmount  = apvts.getRawParameterValue ("czAmount")->load();
    p.czSustain = static_cast<int> (apvts.getRawParameterValue ("czSustain")->load());
    for (int i = 1; i <= 8; ++i)
    {
        const auto s = juce::String (i);
        p.czRate[i - 1]  = apvts.getRawParameterValue ("czRate" + s)->load();
        p.czLevel[i - 1] = apvts.getRawParameterValue ("czLevel" + s)->load();
    }

    p.pitchEnvAmount  = apvts.getRawParameterValue ("pitchEnvAmount")->load();
    p.pitchEnvSustain = static_cast<int> (apvts.getRawParameterValue ("pitchEnvSustain")->load());
    for (int i = 1; i <= 8; ++i)
    {
        const auto s = juce::String (i);
        p.pitchEnvRate[i - 1]  = apvts.getRawParameterValue ("pitchEnvRate" + s)->load();
        p.pitchEnvLevel[i - 1] = apvts.getRawParameterValue ("pitchEnvLevel" + s)->load();
    }

    p.dcwEnvAmount  = apvts.getRawParameterValue ("dcwEnvAmount")->load();
    p.dcwEnvSustain = static_cast<int> (apvts.getRawParameterValue ("dcwEnvSustain")->load());
    for (int i = 1; i <= 8; ++i)
    {
        const auto s = juce::String (i);
        p.dcwEnvRate[i - 1]  = apvts.getRawParameterValue ("dcwEnvRate" + s)->load();
        p.dcwEnvLevel[i - 1] = apvts.getRawParameterValue ("dcwEnvLevel" + s)->load();
    }

    p.macro1 = apvts.getRawParameterValue ("macro1")->load();
    p.macro2 = apvts.getRawParameterValue ("macro2")->load();

    p.modMatrix.clear();
    for (int i = 1; i <= pdhybrid::ModMatrix::kNumSlots; ++i)
    {
        const auto s = juce::String (i);
        const auto src = static_cast<pdhybrid::ModSource> (
            static_cast<int> (apvts.getRawParameterValue ("mod" + s + "Source")->load()));
        const auto dst = static_cast<pdhybrid::ModDest> (
            static_cast<int> (apvts.getRawParameterValue ("mod" + s + "Dest")->load()));
        const double depth = apvts.getRawParameterValue ("mod" + s + "Depth")->load();
        const auto curve = static_cast<pdhybrid::ModCurve> (
            static_cast<int> (apvts.getRawParameterValue ("mod" + s + "Curve")->load()));
        p.modMatrix.setRoute (i - 1, src, dst, depth, curve);
    }

    engine.setParams (p);

    // Cache what the global modulation pass needs.
    globalMatrix  = p.modMatrix;
    macro1_       = p.macro1;
    macro2_       = p.macro2;
    delayMixBase_ = apvts.getRawParameterValue ("delayMix")->load();
    delayFbBase_  = apvts.getRawParameterValue ("delayFeedback")->load();
    chorusDepthBase_ = apvts.getRawParameterValue ("chorusDepth")->load();
    reverbMixBase_   = apvts.getRawParameterValue ("reverbMix")->load();
    globalLfo.setFrequency (apvts.getRawParameterValue ("globalLfoRate")->load());
    globalLfo.setWaveform (static_cast<pdhybrid::LfoWave> (
        static_cast<int> (apvts.getRawParameterValue ("globalLfoWave")->load())));
}

bool PDHybridAudioProcessor::loadWavetable (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 2)
        return false;

    const int total = static_cast<int> (juce::jmin (reader->lengthInSamples,
                                                    (juce::int64) (2048 * 256)));
    juce::AudioBuffer<float> buf (static_cast<int> (reader->numChannels), total);
    reader->read (&buf, 0, total, 0, true, reader->numChannels > 1);

    // Mono sum: a wavetable is a waveform, not a stereo image.
    std::vector<float> mono (static_cast<std::size_t> (total), 0.0f);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* src = buf.getReadPointer (ch);
        for (int i = 0; i < total; ++i)
            mono[static_cast<std::size_t> (i)] += src[i];
    }
    if (buf.getNumChannels() > 1)
        for (auto& s : mono)
            s /= static_cast<float> (buf.getNumChannels());

    // The near-universal convention is 2048-sample single-cycle frames; a file
    // that is not a whole number of those is treated as one cycle and resampled.
    constexpr int kFrame = pdhybrid::WavetableOscillator::kFrameLen;
    int frameLen  = kFrame;
    int numFrames = total / kFrame;
    if (numFrames < 1 || (total % kFrame) != 0)
    {
        frameLen  = total;
        numFrames = 1;
    }

    auto set = pdhybrid::WavetableOscillator::makeWavetableSet (mono.data(), numFrames, frameLen);
    if (set == nullptr || set->numFrames <= 0)
        return false;

    if (wavetable_ != nullptr)
        retiredWavetables_.push_back (wavetable_);   // never freed on the audio thread
    wavetable_     = std::move (set);
    wavetableName_ = file.getFileNameWithoutExtension();
    wavetablePath_ = file.getFullPathName();
    return true;
}

void PDHybridAudioProcessor::applyGlobalModulation (juce::AudioBuffer<float>& buffer, int numSamples)
{
    pdhybrid::ModSources g;                       // per-voice-only sources stay 0 here
    g[pdhybrid::ModSource::GlobalLfo] = globalLfo.value();
    g[pdhybrid::ModSource::Macro1]    = macro1_;
    g[pdhybrid::ModSource::Macro2]    = macro2_;
    g[pdhybrid::ModSource::ModWheel]  = modWheel_;
    globalLfo.advance (numSamples);

    double gm[pdhybrid::ModMatrix::kNumDests];
    globalMatrix.evaluate (g, gm);
    auto md = [&] (pdhybrid::ModDest d) { return gm[static_cast<int> (d)]; };

    delay.setMix      (juce::jlimit (0.0, 1.0,  delayMixBase_ + md (pdhybrid::ModDest::DelayMix)));
    delay.setFeedback (juce::jlimit (0.0, 0.95, delayFbBase_  + md (pdhybrid::ModDest::DelayFeedback)));

    chorus.setDepth (juce::jlimit (0.0, 1.0, chorusDepthBase_ + md (pdhybrid::ModDest::ChorusDepth)));
    reverb.setMix   (juce::jlimit (0.0, 1.0, reverbMixBase_   + md (pdhybrid::ModDest::ReverbMix)));

    // Modulate the master EQ high-shelf gain (matrix output scaled to dB).
    const double eqGain = juce::jlimit (-24.0, 24.0,
        eqHighGainBase_ + 12.0 * md (pdhybrid::ModDest::GlobalEqGain));
    globalEq.setBand (pdhybrid::GlobalEq::HighShelf, eqHighFreqBase_, eqGain);

    const double mp = juce::jlimit (-1.0, 1.0, md (pdhybrid::ModDest::MasterPan));
    if (std::abs (mp) > 1.0e-4 && buffer.getNumChannels() >= 2)
    {
        const float gl = static_cast<float> (mp <= 0.0 ? 1.0 : 1.0 - mp);   // linear balance
        const float gr = static_cast<float> (mp >= 0.0 ? 1.0 : 1.0 + mp);
        buffer.applyGain (0, 0, numSamples, gl);
        buffer.applyGain (1, 0, numSamples, gr);
    }
}

void PDHybridAudioProcessor::dispatchChordEvents (const pdhybrid::ChordMode::Event* ev,
                                                  int n, int channel,
                                                  bool toPoly, bool toBass)
{
    // isRoot events are the bass layer's; the rest are chord notes for the poly
    // voices. With chord mode off, ChordMode passes the played note through as a
    // single isRoot event, so the `! chordOn_` terms route it to both.
    for (int i = 0; i < n; ++i)
    {
        const auto& e = ev[i];
        const bool wantPoly = toPoly && (! e.isRoot || ! chordOn_);
        const bool wantBass = toBass && (e.isRoot   || ! chordOn_);

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

        // Track what is sounding for the chord readout. This is the one place
        // every note reaches a synth layer, whatever produced it -- played
        // directly, built by chord mode or stepped out by the arpeggiator -- so
        // the readout names what you actually hear.
        if (wantPoly || wantBass)
            trackSoundingNote (e.note, e.noteOn);
    }

    publishSoundingNotes();
}

void PDHybridAudioProcessor::trackSoundingNote (int note, bool on) noexcept
{
    if (note < 0 || note > 127)
        return;

    // Reference-counted: the same note can be held on two MIDI channels (and is,
    // under MPE), and the first note-off must not blank a note still sounding.
    if (on)
    {
        if (noteRefs_[note] < 255) ++noteRefs_[note];
    }
    else if (noteRefs_[note] > 0)
        --noteRefs_[note];
}

void PDHybridAudioProcessor::publishSoundingNotes() noexcept
{
    std::uint32_t m[4] = { 0, 0, 0, 0 };
    for (int n = 0; n < 128; ++n)
        if (noteRefs_[n] > 0)
            m[n >> 5] |= 1u << (n & 31);

    for (int i = 0; i < 4; ++i)
        soundingMask_[i].store (m[i], std::memory_order_relaxed);
}

int PDHybridAudioProcessor::soundingNotes (int* out, int maxOut) const noexcept
{
    int n = 0;
    for (int w = 0; w < 4 && n < maxOut; ++w)
    {
        const std::uint32_t m = soundingMask_[w].load (std::memory_order_relaxed);
        for (int b = 0; b < 32 && n < maxOut; ++b)
            if (m & (1u << b))
                out[n++] = w * 32 + b;
    }
    return n;
}

void PDHybridAudioProcessor::syncChordQualityParam()
{
    // A quality key moved the latch. Write it back so the parameter -- which is
    // what gets pushed in every block, saved with the preset and shown in the
    // editor -- agrees with what is actually latched.
    if (! chord_.consumeQualityChanged())
        return;

    const int q = chord_.latchedQuality();
    chordQualitySeen_ = q;

    if (auto* p = apvts.getParameter ("chordQuality"))
    {
        const float norm = p->convertTo0to1 (static_cast<float> (q));
        p->setValueNotifyingHost (norm);
    }
}

void PDHybridAudioProcessor::publishChordState() noexcept
{
    int notes[pdhybrid::ChordMode::kMaxChordNotes];
    const int n = chord_.voicedNotes (notes, pdhybrid::ChordMode::kMaxChordNotes);
    for (int i = 0; i < n; ++i)
        chordNotes_[i].store (notes[i], std::memory_order_relaxed);
    chordNoteCount_.store (n, std::memory_order_relaxed);
    chordRoot_.store (chord_.heldRoot(), std::memory_order_relaxed);
}

int PDHybridAudioProcessor::chordVoicedNotes (int* out, int maxOut) const noexcept
{
    const int n = juce::jmin (maxOut, chordNoteCount_.load (std::memory_order_relaxed));
    for (int i = 0; i < n; ++i)
        out[i] = chordNotes_[i].load (std::memory_order_relaxed);
    return n;
}

void PDHybridAudioProcessor::handleMidiMessage (const juce::MidiMessage& msg,
                                                bool toPoly, bool toBass)
{
    const int channel = msg.getChannel();   // used as the per-note expression id

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
        syncChordQualityParam();
        publishChordState();
    }
    else if (msg.isNoteOff())
    {
        pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
        const int n = chord_.handleNoteOff (msg.getNoteNumber(),
                                            ev, pdhybrid::ChordMode::kMaxEvents);
        dispatchChordEvents (ev, n, channel, toPoly, toBass);
        syncChordQualityParam();
        publishChordState();
    }
    else if (msg.isPitchWheel())
        engine.setNotePitchBend (channel,
            (msg.getPitchWheelValue() - 8192) / 8192.0 * pitchBendRangeSemis);
    else if (msg.isChannelPressure())
        engine.setNotePressure (channel, msg.getChannelPressureValue() / 127.0);
    else if (msg.isController() && msg.getControllerNumber() == 74)
        engine.setNoteTimbre (channel, msg.getControllerValue() / 127.0);
    else if (msg.isController() && msg.getControllerNumber() == 1)
    {
        modWheel_ = msg.getControllerValue() / 127.0;
        engine.setModWheel (modWheel_);
    }
    else if (msg.isController() && msg.getControllerNumber() == 64)
    {
        // Sustain pedal (CC64): >= 64 = pedal down, < 64 = pedal up.
        engine.setSustain (msg.getControllerValue() >= 64);
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        engine.allNotesOff();
        monoBass.allNotesOff();
        clearSoundingNotes();
    }
}

void PDHybridAudioProcessor::renderSegment (juce::AudioBuffer<float>& buffer,
                                            int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    if (static_cast<int> (scratchL.size()) < numSamples)
    {
        scratchL.resize (static_cast<std::size_t> (numSamples));
        scratchR.resize (static_cast<std::size_t> (numSamples));
        scratchBass.resize (static_cast<std::size_t> (numSamples));
    }

    // Segments cover disjoint ranges of the block, so the engine can fill this
    // segment's slice of the block-wide send bus directly (it zero-fills what
    // it is given). No scratch, no copy.
    float* segSendL = fxSendActive_ ? sendL_.data() + startSample : nullptr;
    float* segSendR = fxSendActive_ ? sendR_.data() + startSample : nullptr;

    engine.renderBlock (scratchL.data(), scratchR.data(), segSendL, segSendR, numSamples);

    // Mono sub-bass, summed at centre into both oscillator channels (pre-FX).
    // Skipped entirely when the layer is off (its default) to avoid the scratch
    // zero-fill + mix loops.
    if (monoBass.enabled())
    {
        for (int i = 0; i < numSamples; ++i)
            scratchBass[i] = 0.0f;
        monoBass.renderBlock (scratchBass.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            scratchL[i] += scratchBass[i];
            scratchR[i] += scratchBass[i];
        }
        // The bass layer has no send control of its own, so it goes through the
        // FX group in full -- exactly as it did when the chain was an insert.
        if (fxSendActive_)
            for (int i = 0; i < numSamples; ++i)
            {
                segSendL[i] += scratchBass[i];
                segSendR[i] += scratchBass[i];
            }
    }

    const int numCh = buffer.getNumChannels();
    if (numCh > 0) buffer.copyFrom (0, startSample, scratchL.data(), numSamples);
    if (numCh > 1) buffer.copyFrom (1, startSample, scratchR.data(), numSamples);
    // Any further channels get the left signal.
    for (int ch = 2; ch < numCh; ++ch)
        buffer.copyFrom (ch, startSample, scratchL.data(), numSamples);
}

void PDHybridAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    pushParams();

    // Only pay for the send bus when something actually sends less than fully:
    // an unmodulated send of 1 is the historical insert chain, and the plain
    // two-bus render path stays byte-identical.
    {
        const double sendParam = apvts.getRawParameterValue ("fxSend")->load();
        bool sendRouted = false;
        for (int i = 0; i < pdhybrid::ModMatrix::kNumSlots && ! sendRouted; ++i)
        {
            const auto r = globalMatrix.route (i);
            sendRouted = (r.dest == pdhybrid::ModDest::FxSend
                          && r.source != pdhybrid::ModSource::None
                          && std::abs (r.depth) > 1.0e-6);
        }

        fxSendActive_ = (sendParam < 0.999) || sendRouted;
        if (fxSendActive_)
        {
            const auto n = static_cast<std::size_t> (buffer.getNumSamples());
            if (sendL_.size() < n)
            {
                sendL_.resize (n);
                sendR_.resize (n);
                compGain_.resize (n);
            }
        }
    }

    if (panic_.exchange (false))   // editor "Panic": kill all sounding notes
    {
        engine.allNotesOff();
        monoBass.allNotesOff();
        clearSoundingNotes();
        arp_.reset();
    }

    const int numSamples = buffer.getNumSamples();
    int cursor = 0;

    // Flush hanging notes when the arpeggiator is switched on or off, or when
    // it is re-pointed at a different layer (the layer it just stopped driving
    // would otherwise be left holding a note nothing will release).
    if (arpOn_ != arpWasOn_ || arpTarget_ != arpWasTarget_)
    {
        engine.allNotesOff();
        monoBass.allNotesOff();
        clearSoundingNotes();
        arp_.reset();
    }
    arpWasOn_     = arpOn_;
    arpWasTarget_ = arpTarget_;

    // Same for chord mode: switching it, or moving the split under held notes,
    // would otherwise strand a note on the wrong side of the boundary.
    if (chordOn_ != chordWasOn_ || chordSplitCached_ != chordLastSplit_)
    {
        pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
        chord_.flush (ev, pdhybrid::ChordMode::kMaxEvents);
        engine.allNotesOff();
        monoBass.allNotesOff();
        clearSoundingNotes();
        arp_.reset();
        publishChordState();
    }
    chordWasOn_     = chordOn_;
    chordLastSplit_ = chordSplitCached_;

    // Drain any re-voice owed to a parameter change (quality moved under host
    // automation, say) -- a quality *key* re-voices inside handleNoteOn instead.
    {
        pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
        const int n = chord_.refresh (ev, pdhybrid::ChordMode::kMaxEvents);
        if (n > 0)
        {
            dispatchChordEvents (ev, n, 1, true, true);
            publishChordState();
        }
    }

    if (arpOn_)
    {
        const bool arpDrivesPoly = (arpTarget_ == 0 || arpTarget_ == 1);
        const bool arpDrivesBass = (arpTarget_ == 0 || arpTarget_ == 2);

        // Held notes feed the arp pool; other messages pass through (block-rate).
        // A layer the arp is not driving gets the held notes as played, so the
        // poly voices can sustain a chord under an arpeggiated bass (or vice
        // versa). Those pass-throughs share the arp path's block-rate timing.
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff())
            {
                // Chord mode runs first, so the arp pool receives the chord's
                // notes rather than the single key that produced them -- hold a
                // chord, get a running arpeggio of it. The root-flagged event is
                // the bass layer's and has no business in the arp.
                pdhybrid::ChordMode::Event ev[pdhybrid::ChordMode::kMaxEvents];
                const int n = msg.isNoteOn()
                    ? chord_.handleNoteOn (msg.getNoteNumber(), msg.getFloatVelocity(),
                                           ev, pdhybrid::ChordMode::kMaxEvents)
                    : chord_.handleNoteOff (msg.getNoteNumber(),
                                            ev, pdhybrid::ChordMode::kMaxEvents);
                syncChordQualityParam();
                publishChordState();

                for (int i = 0; i < n; ++i)
                {
                    if (chordOn_ && ev[i].isRoot) continue;
                    if (ev[i].noteOn) arp_.noteOn  (ev[i].note, ev[i].velocity);
                    else              arp_.noteOff (ev[i].note);

                    // The chord readout follows the arp's pool, not its steps.
                    // The steps are one note at a time, which would just make
                    // the readout flicker; the pool is the chord being held,
                    // which is what the player is actually playing.
                    trackSoundingNote (ev[i].note, ev[i].noteOn);
                }
                publishSoundingNotes();

                if (! arpDrivesPoly || ! arpDrivesBass)
                    handleMidiMessage (msg, ! arpDrivesPoly, ! arpDrivesBass);
            }
            else
            {
                handleMidiMessage (msg);
            }
        }

        pdhybrid::Arpeggiator::Event ev[128];
        const int nev = arp_.generate (numSamples, ev, 128);
        for (int e = 0; e < nev; ++e)
        {
            const int pos = juce::jlimit (0, numSamples, ev[e].pos);
            renderSegment (buffer, cursor, pos - cursor);
            cursor = pos;
            if (ev[e].noteOn)
            {
                if (arpDrivesPoly) engine.noteOn (ev[e].note, ev[e].velocity, 1);
                if (arpDrivesBass) monoBass.noteOn (ev[e].note, ev[e].velocity);
            }
            else
            {
                if (arpDrivesPoly) engine.noteOff (ev[e].note, 1);
                if (arpDrivesBass) monoBass.noteOff (ev[e].note);
            }
        }
    }
    else
    {
        for (const auto meta : midi)
        {
            const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
            renderSegment (buffer, cursor, pos - cursor);
            cursor = pos;
            handleMidiMessage (meta.getMessage());
        }
    }

    renderSegment (buffer, cursor, numSamples - cursor);

    // Global modulation (sets delay mix/feedback + master pan for this block).
    applyGlobalModulation (buffer, numSamples);
    publishModLevels();

    // Global output effects across the whole block: compressor then delay.
    if (buffer.getNumChannels() >= 2)
    {
        if (compOn_)
        {
            // The send bus has to receive the identical gain curve, or the two
            // buses stop summing back to the original signal.
            compressor.processStereo (buffer.getWritePointer (0),
                                      buffer.getWritePointer (1), numSamples,
                                      fxSendActive_ ? compGain_.data() : nullptr);
            if (fxSendActive_)
                for (int i = 0; i < numSamples; ++i)
                {
                    sendL_[static_cast<std::size_t> (i)] *= compGain_[static_cast<std::size_t> (i)];
                    sendR_[static_cast<std::size_t> (i)] *= compGain_[static_cast<std::size_t> (i)];
                }
        }

        // Send routing: hold back the un-sent residue, run the modulation FX on
        // the send bus alone, then sum. At send 1 the residue is zero and this
        // is bit-for-bit the old insert chain.
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);
        if (fxSendActive_)
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] -= sendL_[static_cast<std::size_t> (i)];
                R[i] -= sendR_[static_cast<std::size_t> (i)];
            }

        float* fxL = fxSendActive_ ? sendL_.data() : L;
        float* fxR = fxSendActive_ ? sendR_.data() : R;

        if (chorusOn_)
            chorus.processStereo (fxL, fxR, numSamples);
        if (fxRouting_ == 2 && delayOn_ && reverbOn_)
        {
            // Parallel: reverb the main path; delay is fed the pre-reverb signal
            // and its (wet-only) echoes are summed back clean.
            if (static_cast<int> (fxScratchL_.size()) < numSamples)
            {
                fxScratchL_.resize (static_cast<std::size_t> (numSamples));
                fxScratchR_.resize (static_cast<std::size_t> (numSamples));
            }
            for (int i = 0; i < numSamples; ++i) { fxScratchL_[i] = fxL[i]; fxScratchR_[i] = fxR[i]; }
            reverb.processStereo (fxL, fxR, numSamples);
            delay.processWet (fxScratchL_.data(), fxScratchR_.data(), numSamples);
            for (int i = 0; i < numSamples; ++i) { fxL[i] += fxScratchL_[i]; fxR[i] += fxScratchR_[i]; }
        }
        else if (fxRouting_ == 1)
        {
            if (reverbOn_) reverb.processStereo (fxL, fxR, numSamples);
            if (delayOn_)  delay.processStereo (fxL, fxR, numSamples);
        }
        else   // 0 = Delay -> Reverb (default), and the fallbacks for mode 2
        {
            if (delayOn_)  delay.processStereo (fxL, fxR, numSamples);
            if (reverbOn_) reverb.processStereo (fxL, fxR, numSamples);
        }

        // Sum the processed send back onto the un-sent residue.
        if (fxSendActive_)
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] += sendL_[static_cast<std::size_t> (i)];
                R[i] += sendR_[static_cast<std::size_t> (i)];
            }

        if (globalEqOn_)
            globalEq.processStereo (buffer.getWritePointer (0),
                                    buffer.getWritePointer (1), numSamples);
        master.processStereo (buffer.getWritePointer (0),
                              buffer.getWritePointer (1), numSamples);

        // Final safety net: never emit a non-finite sample. A feedback path
        // (delay/reverb/comb) reacting to an abrupt patch change could in
        // principle produce a NaN/Inf; scrub it here so it reaches neither the
        // host nor the scope tap (whose Path rasteriser would otherwise crash).
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (d[i])) d[i] = 0.0f;
        }

        pushScope (buffer.getReadPointer (0), buffer.getReadPointer (1), numSamples);
    }
}

void PDHybridAudioProcessor::pushScope (const float* left, const float* right, int n) noexcept
{
    int w = scopeWrite_.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        scopeBuf_[w & (kScopeSize - 1)] = 0.5f * (left[i] + right[i]);
        ++w;
    }
    scopeWrite_.store (w, std::memory_order_release);
}

void PDHybridAudioProcessor::readScope (float* dest, int num) const noexcept
{
    if (num > kScopeSize) num = kScopeSize;
    const int w = scopeWrite_.load (std::memory_order_acquire);
    // Copy the `num` most recent samples in chronological order.
    for (int i = 0; i < num; ++i)
        dest[i] = scopeBuf_[(w - num + i) & (kScopeSize - 1)];
}

void PDHybridAudioProcessor::publishModLevels() noexcept
{
    // Per-voice sources come from the newest sounding voice; the global ones are
    // only known here. If nothing is playing the per-voice slots stay zero, so
    // the meters fall back to rest rather than freezing on the last note.
    pdhybrid::ModSources s;
    engine.latestModSources (s);
    s[pdhybrid::ModSource::GlobalLfo] = globalLfo.value();
    s[pdhybrid::ModSource::Macro1]    = macro1_;
    s[pdhybrid::ModSource::Macro2]    = macro2_;
    s[pdhybrid::ModSource::ModWheel]  = modWheel_;

    for (int i = 0; i < kNumModSources; ++i)
        modLevels_[i].store (static_cast<float> (s.v[i]), std::memory_order_relaxed);

    gainReduction_.store (static_cast<float> (compressor.gainReductionDb()),
                          std::memory_order_relaxed);
}

void PDHybridAudioProcessor::readModLevels (float* dest, int num) const noexcept
{
    for (int i = 0; i < juce::jmin (num, kNumModSources); ++i)
        dest[i] = modLevels_[i].load (std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PDHybridAudioProcessor::createEditor()
{
    return new PDHybridEditor (*this);
}

void PDHybridAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        // The wavetable is a file reference, not a parameter, so it rides
        // alongside the parameter tree rather than in it.
        state.setProperty ("wavetablePath", wavetablePath_, nullptr);
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

void PDHybridAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        apvts.replaceState (tree);

        // Re-load the referenced table if it is still where it was. A missing
        // file is not an error: the engine falls back to its built-in set and
        // the patch still plays.
        const auto path = tree.getProperty ("wavetablePath").toString();
        if (path.isNotEmpty())
            loadWavetable (juce::File (path));
    }
}

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PDHybridAudioProcessor();
}
