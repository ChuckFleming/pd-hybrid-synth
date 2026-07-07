#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SynthEngine.h"
#include "dsp/Compressor.h"
#include <vector>

/**
    Polyphonic hybrid synth: drives the headless SynthEngine (PD osc -> ladder
    filter -> overdrive -> multi-stage envelope, per voice). MIDI is rendered
    sample-accurately by splitting each block at event boundaries. Per-note
    expression (pitch bend / pressure / CC74 timbre) is keyed by MIDI channel,
    which covers both MPE and legacy controllers. The editor is a generic,
    auto-generated parameter panel for now.
*/
class PDHybridAudioProcessor : public juce::AudioProcessor
{
public:
    PDHybridAudioProcessor();
    ~PDHybridAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PD Hybrid Synth"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void pushParams();
    void handleMidiMessage (const juce::MidiMessage& msg);
    void renderSegment (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    pdhybrid::SynthEngine engine;
    pdhybrid::Compressor  compressor;           // global output compressor
    std::vector<float>    scratchL, scratchR;   // stereo render buffers
    double                pitchBendRangeSemis = 2.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PDHybridAudioProcessor)
};
