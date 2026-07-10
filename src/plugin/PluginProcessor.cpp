#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    const juce::StringArray oscTypeNames { "Phase Distortion", "Saw", "Square", "Triangle", "Pulse" };
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
        pf (id + "Amount", label + " PD Amount",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.30f, pct);
        pf (id + "PulseWidth", label + " Pulse Width",
            juce::NormalisableRange<float> (0.05f, 0.95f), 0.50f, pct);
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id + "Octave", 1 }, label + " Octave", -3, 3, 0));
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id + "Semi", 1 }, label + " Semitone", -12, 12, 0));
        pf (id + "Fine", label + " Fine",
            juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f, cnt);
        pf (id + "Level", label + " Level",
            juce::NormalisableRange<float> (0.0f, 1.0f), defLevel, pct);
        const juce::NormalisableRange<float> eqRange (-18.0f, 18.0f);
        pf (id + "EqLow",  label + " EQ Low",  eqRange, 0.0f, db);
        pf (id + "EqMid",  label + " EQ Mid",  eqRange, 0.0f, db);
        pf (id + "EqHigh", label + " EQ High", eqRange, 0.0f, db);
    };
    addOscGroup ("oscA", "Osc A", 0, 1.0f);   // Phase Distortion, full level
    addOscGroup ("oscB", "Osc B", 1, 0.0f);   // Saw, silent by default

    pf ("noiseLevel", "Noise Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    pf ("cutoff", "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 18000.0f, 0.0f, 0.3f), 8000.0f, hz);

    pf ("resonance", "Filter Resonance", juce::NormalisableRange<float> (0.0f, 1.0f), 0.20f, pct);

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filterType", 1 }, "Filter Type",
        juce::StringArray { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" }, 0));

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
                                              "Comb", "Allpass" };
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

    pf ("gain", "Gain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.80f, pct);

    pf ("pan", "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, pan);
    pf ("panSpread", "Pan Spread", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    pf ("drift", "Analog Drift", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);

    // --- Unison ---
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "unisonVoices", 1 }, "Unison Voices", 1, 6, 1));
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

    // --- Delay (mix 0 = bypass) ---
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayMode", 1 }, "Delay Mode",
        juce::StringArray { "Mono", "Stereo", "Ping-Pong" }, 1));
    const juce::StringArray delaySyncNames { "Free", "1/1", "1/2", "1/4", "1/8", "1/16",
                                             "1/4.", "1/8.", "1/4T", "1/8T" };
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delaySyncL", 1 }, "Delay Sync L", delaySyncNames, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delaySyncR", 1 }, "Delay Sync R", delaySyncNames, 0));
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
        juce::StringArray { "1x", "2x", "4x" }, 2));   // default 4x

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
    pf ("lfo2Rate", "LFO 2 Rate", juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 0.5f, rate);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfo2Wave", 1 }, "LFO 2 Wave", lfoWaveNames, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfo2Sync", 1 }, "LFO 2 Sync", syncNames, 0));

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

    const juce::StringArray srcNames { "None", "Mod Env", "LFO", "Velocity", "Pressure",
                                       "Timbre", "Pitch Bend", "Key Track", "Mod Wheel", "LFO 2",
                                       "Multi Env", "Amp Env", "Filt Env A", "Filt Env B",
                                       "Random", "Global LFO", "Macro 1", "Macro 2" };
    const juce::StringArray dstNames { "None", "Pitch", "PD Amount", "Pulse Width", "Cutoff",
                                       "Resonance", "Morph", "Drive", "Amplitude", "Pan",
                                       "Osc A Lvl", "Osc B Lvl", "Detune", "Filter 2 Cutoff",
                                       "Delay Mix", "Delay Fbk", "Master Pan", "Global EQ" };
    for (int i = 1; i <= pdhybrid::ModMatrix::kNumSlots; ++i)
    {
        const auto s = juce::String (i);
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mod" + s + "Source", 1 }, "Mod " + s + " Source", srcNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mod" + s + "Dest", 1 }, "Mod " + s + " Dest", dstNames, 0));
        pf ("mod" + s + "Depth", "Mod " + s + " Depth",
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, amt);
    }

    return { params.begin(), params.end() };
}

PDHybridAudioProcessor::PDHybridAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

void PDHybridAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setSampleRate (sampleRate);
    compressor.setSampleRate (sampleRate);
    compressor.reset();
    delay.setSampleRate (sampleRate);
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
}

void PDHybridAudioProcessor::pushParams()
{
    pdhybrid::SynthParams p;

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
    p.noiseLevel  = apvts.getRawParameterValue ("noiseLevel")->load();

    p.cutoffHz    = apvts.getRawParameterValue ("cutoff")->load();
    p.resonance   = apvts.getRawParameterValue ("resonance")->load();
    p.filterType  = static_cast<pdhybrid::FilterType> (
                        static_cast<int> (apvts.getRawParameterValue ("filterType")->load()));
    p.filterMorph = apvts.getRawParameterValue ("filterMorph")->load();
    p.keyTrack        = apvts.getRawParameterValue ("keyTrack")->load();
    p.filterEnvAmount = apvts.getRawParameterValue ("filterEnvAmount")->load();
    p.filterEnvA  = apvts.getRawParameterValue ("filterEnvA")->load();
    p.filterEnvD  = apvts.getRawParameterValue ("filterEnvD")->load();
    p.filterEnvS  = apvts.getRawParameterValue ("filterEnvS")->load();
    p.filterEnvR  = apvts.getRawParameterValue ("filterEnvR")->load();
    p.filterRouting = static_cast<pdhybrid::FilterRouting> (
        static_cast<int> (apvts.getRawParameterValue ("filterRouting")->load()));
    p.filter2Type   = static_cast<pdhybrid::FilterType> (
        static_cast<int> (apvts.getRawParameterValue ("filter2Type")->load()));
    p.filter2Cutoff = apvts.getRawParameterValue ("filter2Cutoff")->load();
    p.filter2Res    = apvts.getRawParameterValue ("filter2Res")->load();
    p.filter2Morph  = apvts.getRawParameterValue ("filter2Morph")->load();
    p.filter2EnvAmount = apvts.getRawParameterValue ("filter2EnvAmount")->load();
    p.filter2EnvA = apvts.getRawParameterValue ("filter2EnvA")->load();
    p.filter2EnvD = apvts.getRawParameterValue ("filter2EnvD")->load();
    p.filter2EnvS = apvts.getRawParameterValue ("filter2EnvS")->load();
    p.filter2EnvR = apvts.getRawParameterValue ("filter2EnvR")->load();
    p.driveOn     = apvts.getRawParameterValue ("driveOn")->load() > 0.5f;
    p.drive       = apvts.getRawParameterValue ("drive")->load();
    p.driveType   = static_cast<int> (apvts.getRawParameterValue ("driveType")->load());
    p.crushBits   = apvts.getRawParameterValue ("crushBits")->load();
    p.downsample  = apvts.getRawParameterValue ("downsample")->load();
    p.bias      = apvts.getRawParameterValue ("bias")->load();
    p.attack    = apvts.getRawParameterValue ("attack")->load();
    p.decay     = apvts.getRawParameterValue ("decay")->load();
    p.sustain   = apvts.getRawParameterValue ("sustain")->load();
    p.release   = apvts.getRawParameterValue ("release")->load();
    p.gain      = apvts.getRawParameterValue ("gain")->load();
    const int osIdx = static_cast<int> (apvts.getRawParameterValue ("osQuality")->load());
    const int osFactor[] = { 1, 2, 4 };
    p.oscOversampling = osFactor[juce::jlimit (0, 2, osIdx)];
    p.pan       = apvts.getRawParameterValue ("pan")->load();
    p.panSpread = apvts.getRawParameterValue ("panSpread")->load();
    p.drift     = apvts.getRawParameterValue ("drift")->load();
    p.unisonVoices = static_cast<int> (apvts.getRawParameterValue ("unisonVoices")->load());
    p.unisonDetune = apvts.getRawParameterValue ("unisonDetune")->load();
    p.unisonWidth  = apvts.getRawParameterValue ("unisonWidth")->load();
    p.glideMode = static_cast<pdhybrid::GlideMode> (
        static_cast<int> (apvts.getRawParameterValue ("glideMode")->load()));
    p.glideTime  = apvts.getRawParameterValue ("glideTime")->load();
    p.glideCurve = apvts.getRawParameterValue ("glideCurve")->load();

    compressor.setThreshold (apvts.getRawParameterValue ("compThreshold")->load());
    compressor.setRatio     (apvts.getRawParameterValue ("compRatio")->load());
    compressor.setAttack    (apvts.getRawParameterValue ("compAttack")->load());
    compressor.setRelease   (apvts.getRawParameterValue ("compRelease")->load());
    compressor.setMakeup    (apvts.getRawParameterValue ("compMakeup")->load());

    // Host tempo for LFO + delay sync (falls back to 120 BPM when the host has none).
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

    delay.setMode (static_cast<pdhybrid::DelayMode> (
        static_cast<int> (apvts.getRawParameterValue ("delayMode")->load())));
    const int dSyncL = static_cast<int> (apvts.getRawParameterValue ("delaySyncL")->load());
    const int dSyncR = static_cast<int> (apvts.getRawParameterValue ("delaySyncR")->load());
    const double delayL = dSyncL == 0 ? apvts.getRawParameterValue ("delayTimeL")->load()
                                      : pdhybrid::syncedDelaySeconds (bpm, dSyncL - 1);
    const double delayR = dSyncR == 0 ? apvts.getRawParameterValue ("delayTimeR")->load()
                                      : pdhybrid::syncedDelaySeconds (bpm, dSyncR - 1);
    delay.setTimes    (delayL, delayR);
    delay.setFeedback (apvts.getRawParameterValue ("delayFeedback")->load());
    delay.setMix      (apvts.getRawParameterValue ("delayMix")->load());
    delay.setDuck     (apvts.getRawParameterValue ("delayDuck")->load());

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

    master.setGainDb (apvts.getRawParameterValue ("masterLevel")->load());
    master.setLimiterEnabled (apvts.getRawParameterValue ("masterLimiter")->load() > 0.5f);

    const int lfoSync  = static_cast<int> (apvts.getRawParameterValue ("lfoSync")->load());
    const int lfo2Sync = static_cast<int> (apvts.getRawParameterValue ("lfo2Sync")->load());
    p.lfoRate  = (lfoSync == 0) ? apvts.getRawParameterValue ("lfoRate")->load()
                                : pdhybrid::syncedLfoHz (bpm, lfoSync - 1);
    p.lfo2Rate = (lfo2Sync == 0) ? apvts.getRawParameterValue ("lfo2Rate")->load()
                                 : pdhybrid::syncedLfoHz (bpm, lfo2Sync - 1);
    p.lfoWave  = static_cast<int> (apvts.getRawParameterValue ("lfoWave")->load());
    p.lfo2Wave = static_cast<int> (apvts.getRawParameterValue ("lfo2Wave")->load());
    p.modEnvA = apvts.getRawParameterValue ("modEnvA")->load();
    p.modEnvD = apvts.getRawParameterValue ("modEnvD")->load();
    p.modEnvS = apvts.getRawParameterValue ("modEnvS")->load();
    p.modEnvR = apvts.getRawParameterValue ("modEnvR")->load();

    p.czAmount  = apvts.getRawParameterValue ("czAmount")->load();
    p.czSustain = static_cast<int> (apvts.getRawParameterValue ("czSustain")->load());
    for (int i = 1; i <= 8; ++i)
    {
        const auto s = juce::String (i);
        p.czRate[i - 1]  = apvts.getRawParameterValue ("czRate" + s)->load();
        p.czLevel[i - 1] = apvts.getRawParameterValue ("czLevel" + s)->load();
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
        p.modMatrix.setRoute (i - 1, src, dst, depth);
    }

    engine.setParams (p);

    // Cache what the global modulation pass needs.
    globalMatrix  = p.modMatrix;
    macro1_       = p.macro1;
    macro2_       = p.macro2;
    delayMixBase_ = apvts.getRawParameterValue ("delayMix")->load();
    delayFbBase_  = apvts.getRawParameterValue ("delayFeedback")->load();
    globalLfo.setFrequency (apvts.getRawParameterValue ("globalLfoRate")->load());
    globalLfo.setWaveform (static_cast<pdhybrid::LfoWave> (
        static_cast<int> (apvts.getRawParameterValue ("globalLfoWave")->load())));
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

void PDHybridAudioProcessor::handleMidiMessage (const juce::MidiMessage& msg)
{
    const int channel = msg.getChannel();   // used as the per-note expression id

    if (msg.isNoteOn())
    {
        engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), channel);
        monoBass.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
    }
    else if (msg.isNoteOff())
    {
        engine.noteOff (msg.getNoteNumber(), channel);
        monoBass.noteOff (msg.getNoteNumber());
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
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        engine.allNotesOff();
        monoBass.allNotesOff();
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

    engine.renderBlock (scratchL.data(), scratchR.data(), numSamples);

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

    const int numSamples = buffer.getNumSamples();
    int cursor = 0;

    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderSegment (buffer, cursor, pos - cursor);
        cursor = pos;
        handleMidiMessage (meta.getMessage());
    }

    renderSegment (buffer, cursor, numSamples - cursor);

    // Global modulation (sets delay mix/feedback + master pan for this block).
    applyGlobalModulation (buffer, numSamples);

    // Global output effects across the whole block: compressor then delay.
    if (buffer.getNumChannels() >= 2)
    {
        if (compOn_)
            compressor.processStereo (buffer.getWritePointer (0),
                                      buffer.getWritePointer (1), numSamples);
        if (delayOn_)
            delay.processStereo (buffer.getWritePointer (0),
                                 buffer.getWritePointer (1), numSamples);
        if (globalEqOn_)
            globalEq.processStereo (buffer.getWritePointer (0),
                                    buffer.getWritePointer (1), numSamples);
        master.processStereo (buffer.getWritePointer (0),
                              buffer.getWritePointer (1), numSamples);
    }
}

juce::AudioProcessorEditor* PDHybridAudioProcessor::createEditor()
{
    return new PDHybridEditor (*this);
}

void PDHybridAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

void PDHybridAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PDHybridAudioProcessor();
}
