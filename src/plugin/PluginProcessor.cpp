#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout PDHybridAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "oscType", 1 }, "Oscillator",
        juce::StringArray { "Phase Distortion", "Saw", "Square", "Triangle", "Pulse" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "PD Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.30f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pulseWidth", 1 }, "Pulse Width",
        juce::NormalisableRange<float> (0.05f, 0.95f), 0.50f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "cutoff", 1 }, "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 18000.0f, 0.0f, 0.3f), 8000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "resonance", 1 }, "Filter Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.20f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filterType", 1 }, "Filter Type",
        juce::StringArray { "Ladder", "PD Resonator", "Comb", "Allpass" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filterMorph", 1 }, "Filter Morph",
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
        juce::ParameterID { "attack", 1 }, "Attack",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.0f, 0.3f), 0.01f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay", 1 }, "Decay",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.0f, 0.3f), 0.10f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sustain", 1 }, "Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.70f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "release", 1 }, "Release",
        juce::NormalisableRange<float> (0.001f, 3.0f, 0.0f, 0.3f), 0.20f));

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
    scratch.assign (static_cast<std::size_t> (juce::jmax (1, samplesPerBlock)), 0.0f);
}

void PDHybridAudioProcessor::pushParams()
{
    pdhybrid::SynthParams p;
    p.oscType     = static_cast<pdhybrid::OscType> (
                        static_cast<int> (apvts.getRawParameterValue ("oscType")->load()));
    p.pdAmount    = apvts.getRawParameterValue ("amount")->load();
    p.pulseWidth  = apvts.getRawParameterValue ("pulseWidth")->load();
    p.cutoffHz    = apvts.getRawParameterValue ("cutoff")->load();
    p.resonance   = apvts.getRawParameterValue ("resonance")->load();
    p.filterType  = static_cast<pdhybrid::FilterType> (
                        static_cast<int> (apvts.getRawParameterValue ("filterType")->load()));
    p.filterMorph = apvts.getRawParameterValue ("filterMorph")->load();
    p.drive       = apvts.getRawParameterValue ("drive")->load();
    p.bias      = apvts.getRawParameterValue ("bias")->load();
    p.attack    = apvts.getRawParameterValue ("attack")->load();
    p.decay     = apvts.getRawParameterValue ("decay")->load();
    p.sustain   = apvts.getRawParameterValue ("sustain")->load();
    p.release   = apvts.getRawParameterValue ("release")->load();
    p.gain      = apvts.getRawParameterValue ("gain")->load();
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
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        engine.allNotesOff();
}

void PDHybridAudioProcessor::renderSegment (juce::AudioBuffer<float>& buffer,
                                            int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    if (static_cast<int> (scratch.size()) < numSamples)
        scratch.resize (static_cast<std::size_t> (numSamples));

    engine.renderBlock (scratch.data(), numSamples);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, startSample, scratch.data(), numSamples);
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
