#include "PluginProcessor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout PDHybridAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "PD Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.30f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "cutoff", 1 }, "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 18000.0f, 0.0f, 0.3f), 8000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "resonance", 1 }, "Filter Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.20f));

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

void PDHybridAudioProcessor::prepareToPlay (double sampleRate, int)
{
    osc.setSampleRate (sampleRate);
    filter.setSampleRate (sampleRate);
    filter.reset();
    amp.setSampleRate (sampleRate);
    amp.setOversampling (4);
    amp.reset();
    env.setSampleRate (sampleRate);
    env.setADSR (0.01, 0.10, 0.70, 0.20);   // preallocate stages
    env.reset();
}

void PDHybridAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    env.setADSR (apvts.getRawParameterValue ("attack")->load(),
                 apvts.getRawParameterValue ("decay")->load(),
                 apvts.getRawParameterValue ("sustain")->load(),
                 apvts.getRawParameterValue ("release")->load());

    osc.setAmount (apvts.getRawParameterValue ("amount")->load());
    filter.setCutoff (apvts.getRawParameterValue ("cutoff")->load());
    filter.setResonance (apvts.getRawParameterValue ("resonance")->load());
    amp.setDrive (apvts.getRawParameterValue ("drive")->load());
    amp.setBias (apvts.getRawParameterValue ("bias")->load());
    const float gain = apvts.getRawParameterValue ("gain")->load();

    const int numSamples   = buffer.getNumSamples();
    const int numChannels  = buffer.getNumChannels();

    auto midiIt  = midi.begin();
    auto midiEnd = midi.end();

    for (int i = 0; i < numSamples; ++i)
    {
        while (midiIt != midiEnd && (*midiIt).samplePosition <= i)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())
            {
                currentNote = msg.getNoteNumber();
                osc.setFrequency (juce::MidiMessage::getMidiNoteInHertz (currentNote));
                env.noteOn();
            }
            else if (msg.isNoteOff() && msg.getNoteNumber() == currentNote)
            {
                env.noteOff();
            }
            ++midiIt;
        }

        const float driven = amp.processSample (filter.processSample (osc.processSample()));
        const float s = driven * env.processSample() * gain;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, s);
    }
}

juce::AudioProcessorEditor* PDHybridAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
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
