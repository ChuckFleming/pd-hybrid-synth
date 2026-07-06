#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/PhaseDistortionOscillator.h"
#include "dsp/LadderFilter.h"
#include "dsp/OverdriveAmp.h"

/**
    Minimal vertical-slice synth: one PD oscillator -> JUCE ADSR gate -> gain,
    driven by MIDI (monophonic). Intentionally thin -- each real module (filter,
    overdrive, multi-stage envelope, polyphony) will be built test-first in the
    DSP core and wired in here as it goes green. The editor is a generic,
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

    pdhybrid::PhaseDistortionOscillator osc;
    pdhybrid::LadderFilter filter;
    pdhybrid::OverdriveAmp amp;
    juce::ADSR             env;
    juce::ADSR::Parameters envParams;
    int    currentNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PDHybridAudioProcessor)
};
