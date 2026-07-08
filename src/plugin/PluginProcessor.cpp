#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout PDHybridAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

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
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "Amount", 1 }, label + " PD Amount",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.30f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "PulseWidth", 1 }, label + " Pulse Width",
            juce::NormalisableRange<float> (0.05f, 0.95f), 0.50f));
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id + "Octave", 1 }, label + " Octave", -3, 3, 0));
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id + "Semi", 1 }, label + " Semitone", -12, 12, 0));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "Fine", 1 }, label + " Fine",
            juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "Level", 1 }, label + " Level",
            juce::NormalisableRange<float> (0.0f, 1.0f), defLevel));
        const juce::NormalisableRange<float> eqRange (-18.0f, 18.0f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "EqLow", 1 },  label + " EQ Low",  eqRange, 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "EqMid", 1 },  label + " EQ Mid",  eqRange, 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "EqHigh", 1 }, label + " EQ High", eqRange, 0.0f));
    };
    addOscGroup ("oscA", "Osc A", 0, 1.0f);   // Phase Distortion, full level
    addOscGroup ("oscB", "Osc B", 1, 0.0f);   // Saw, silent by default

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "noiseLevel", 1 }, "Noise Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "cutoff", 1 }, "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 18000.0f, 0.0f, 0.3f), 8000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "resonance", 1 }, "Filter Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.20f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filterType", 1 }, "Filter Type",
        juce::StringArray { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filterMorph", 1 }, "Filter Morph",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "keyTrack", 1 }, "Filter Key Track",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filterEnvAmount", 1 }, "Filter Env Amount",
        juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f));   // octaves, bipolar

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filterEnvA", 1 }, "Filter Env Attack",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.01f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filterEnvD", 1 }, "Filter Env Decay",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filterEnvS", 1 }, "Filter Env Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filterEnvR", 1 }, "Filter Env Release",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.30f));

    // --- Filter B + routing ---
    const juce::StringArray filterTypeNames { "Ladder", "State Variable", "PD Resonator",
                                              "Comb", "Allpass" };
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filterRouting", 1 }, "Filter Routing",
        juce::StringArray { "Single", "Series", "Parallel" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filter2Type", 1 }, "Filter 2 Type", filterTypeNames, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filter2Cutoff", 1 }, "Filter 2 Cutoff",
        juce::NormalisableRange<float> (20.0f, 18000.0f, 0.0f, 0.3f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filter2Res", 1 }, "Filter 2 Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.20f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filter2Morph", 1 }, "Filter 2 Morph",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drive", 1 }, "Overdrive",
        juce::NormalisableRange<float> (1.0f, 50.0f, 0.0f, 0.3f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bias", 1 }, "Overdrive Bias",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 }, "Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.80f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pan", 1 }, "Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "panSpread", 1 }, "Pan Spread",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drift", 1 }, "Analog Drift",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // --- Unison ---
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "unisonVoices", 1 }, "Unison Voices", 1, 6, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "unisonDetune", 1 }, "Unison Detune",
        juce::NormalisableRange<float> (0.0f, 50.0f), 15.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "unisonWidth", 1 }, "Unison Width",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    // --- Output compressor (ratio 1 = bypass) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compThreshold", 1 }, "Comp Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f), -12.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compRatio", 1 }, "Comp Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.0f, 0.4f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compAttack", 1 }, "Comp Attack",
        juce::NormalisableRange<float> (0.0005f, 0.2f, 0.0f, 0.3f), 0.005f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compRelease", 1 }, "Comp Release",
        juce::NormalisableRange<float> (0.01f, 1.0f, 0.0f, 0.3f), 0.10f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compMakeup", 1 }, "Comp Makeup",
        juce::NormalisableRange<float> (0.0f, 24.0f), 0.0f));

    // --- Delay (mix 0 = bypass) ---
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayMode", 1 }, "Delay Mode",
        juce::StringArray { "Mono", "Stereo", "Ping-Pong" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delayTimeL", 1 }, "Delay Time L",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.0f, 0.3f), 0.30f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delayTimeR", 1 }, "Delay Time R",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.0f, 0.3f), 0.45f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delayFeedback", 1 }, "Delay Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.30f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delayMix", 1 }, "Delay Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delayDuck", 1 }, "Delay Ducking",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // --- Glide / portamento ---
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "glideMode", 1 }, "Glide Mode",
        juce::StringArray { "Off", "Always", "Legato" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "glideTime", 1 }, "Glide Time",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.0f, 0.3f), 0.10f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "glideCurve", 1 }, "Glide Curve",
        juce::NormalisableRange<float> (0.25f, 4.0f, 0.0f, 0.5f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "attack", 1 }, "Attack",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.01f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay", 1 }, "Decay",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.10f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sustain", 1 }, "Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.70f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "release", 1 }, "Release",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f));

    // --- Modulation ---
    const juce::StringArray lfoWaveNames { "Sine", "Triangle", "Square", "Saw",
                                           "Ramp Down", "Sample & Hold", "Smooth Random",
                                           "Exponential" };
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lfoRate", 1 }, "LFO Rate",
        juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 5.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfoWave", 1 }, "LFO Wave", lfoWaveNames, 0));
    const juce::StringArray syncNames { "Free", "1/1", "1/2", "1/4", "1/8", "1/16",
                                        "1/4.", "1/8.", "1/4T", "1/8T" };
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfoSync", 1 }, "LFO Sync", syncNames, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lfo2Rate", 1 }, "LFO 2 Rate",
        juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfo2Wave", 1 }, "LFO 2 Wave", lfoWaveNames, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfo2Sync", 1 }, "LFO 2 Sync", syncNames, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "modEnvA", 1 }, "Mod Env Attack",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.01f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "modEnvD", 1 }, "Mod Env Decay",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.20f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "modEnvS", 1 }, "Mod Env Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "modEnvR", 1 }, "Mod Env Release",
        juce::NormalisableRange<float> (0.001f, 30.0f, 0.0f, 0.25f), 0.30f));

    const juce::StringArray srcNames { "None", "Mod Env", "LFO", "Velocity", "Pressure",
                                       "Timbre", "Pitch Bend", "Key Track", "Mod Wheel", "LFO 2" };
    const juce::StringArray dstNames { "None", "Pitch", "PD Amount", "Pulse Width", "Cutoff",
                                       "Resonance", "Morph", "Drive", "Amplitude" };
    for (int i = 1; i <= 6; ++i)
    {
        const auto s = juce::String (i);
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mod" + s + "Source", 1 }, "Mod " + s + " Source", srcNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mod" + s + "Dest", 1 }, "Mod " + s + " Dest", dstNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mod" + s + "Depth", 1 }, "Mod " + s + " Depth",
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
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
    const auto n = static_cast<std::size_t> (juce::jmax (1, samplesPerBlock));
    scratchL.assign (n, 0.0f);
    scratchR.assign (n, 0.0f);
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
    p.drive       = apvts.getRawParameterValue ("drive")->load();
    p.bias      = apvts.getRawParameterValue ("bias")->load();
    p.attack    = apvts.getRawParameterValue ("attack")->load();
    p.decay     = apvts.getRawParameterValue ("decay")->load();
    p.sustain   = apvts.getRawParameterValue ("sustain")->load();
    p.release   = apvts.getRawParameterValue ("release")->load();
    p.gain      = apvts.getRawParameterValue ("gain")->load();
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

    delay.setMode (static_cast<pdhybrid::DelayMode> (
        static_cast<int> (apvts.getRawParameterValue ("delayMode")->load())));
    delay.setTimes    (apvts.getRawParameterValue ("delayTimeL")->load(),
                       apvts.getRawParameterValue ("delayTimeR")->load());
    delay.setFeedback (apvts.getRawParameterValue ("delayFeedback")->load());
    delay.setMix      (apvts.getRawParameterValue ("delayMix")->load());
    delay.setDuck     (apvts.getRawParameterValue ("delayDuck")->load());

    // Host tempo for LFO sync (falls back to 120 BPM when the host has none).
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

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

    p.modMatrix.clear();
    for (int i = 1; i <= 6; ++i)
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
}

void PDHybridAudioProcessor::handleMidiMessage (const juce::MidiMessage& msg)
{
    const int channel = msg.getChannel();   // used as the per-note expression id

    if (msg.isNoteOn())
        engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), channel);
    else if (msg.isNoteOff())
        engine.noteOff (msg.getNoteNumber(), channel);
    else if (msg.isPitchWheel())
        engine.setNotePitchBend (channel,
            (msg.getPitchWheelValue() - 8192) / 8192.0 * pitchBendRangeSemis);
    else if (msg.isChannelPressure())
        engine.setNotePressure (channel, msg.getChannelPressureValue() / 127.0);
    else if (msg.isController() && msg.getControllerNumber() == 74)
        engine.setNoteTimbre (channel, msg.getControllerValue() / 127.0);
    else if (msg.isController() && msg.getControllerNumber() == 1)
        engine.setModWheel (msg.getControllerValue() / 127.0);
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        engine.allNotesOff();
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
    }

    engine.renderBlock (scratchL.data(), scratchR.data(), numSamples);

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

    // Global output effects across the whole block: compressor then delay.
    if (buffer.getNumChannels() >= 2)
    {
        compressor.processStereo (buffer.getWritePointer (0),
                                  buffer.getWritePointer (1), numSamples);
        delay.processStereo (buffer.getWritePointer (0),
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
