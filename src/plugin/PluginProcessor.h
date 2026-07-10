#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SynthEngine.h"
#include "dsp/Compressor.h"
#include "dsp/Delay.h"
#include "dsp/GlobalEq.h"
#include "dsp/MonoBass.h"
#include "PresetManager.h"
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
    double getTailLengthSeconds() const override { return pdhybrid::Delay::kMaxDelaySeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    PresetManager& getPresetManager() noexcept { return presets; }

private:
    PresetManager presets { apvts };   // constructed after apvts (declaration order)

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void pushParams();
    void handleMidiMessage (const juce::MidiMessage& msg);
    void renderSegment (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    void applyGlobalModulation (juce::AudioBuffer<float>& buffer, int numSamples);

    pdhybrid::SynthEngine engine;
    pdhybrid::Compressor  compressor;           // global output compressor
    pdhybrid::Delay       delay;                // global ducking delay
    pdhybrid::GlobalEq    globalEq;             // final master EQ
    pdhybrid::MonoBass    monoBass;             // monophonic sub-bass layer
    std::vector<float>    scratchL, scratchR;   // stereo render buffers
    std::vector<float>    scratchBass;          // mono bass render buffer
    double                pitchBendRangeSemis = 2.0;

    // Global modulation pass (processor-level sources -> global FX destinations).
    pdhybrid::Lfo         globalLfo;
    pdhybrid::ModMatrix   globalMatrix;
    double                macro1_ = 0.0, macro2_ = 0.0, modWheel_ = 0.0;
    double                delayMixBase_ = 0.0, delayFbBase_ = 0.30;
    double                eqHighFreqBase_ = 8000.0, eqHighGainBase_ = 0.0;
    bool                  compOn_ = true, delayOn_ = true, globalEqOn_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PDHybridAudioProcessor)
};
