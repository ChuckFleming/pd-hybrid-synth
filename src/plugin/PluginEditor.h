#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

/**
    Hand-built editor: grouped rotary controls for the oscillator, filter,
    overdrive and envelope sections, plus a filter-type selector. Each control
    is bound to the parameter tree via an attachment.
*/
class PDHybridEditor : public juce::AudioProcessorEditor
{
public:
    explicit PDHybridEditor (PDHybridAudioProcessor&);
    ~PDHybridEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct LabeledKnob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    // A section holds a title and a set of knobs laid out in a row.
    struct Section
    {
        juce::String title;
        std::vector<LabeledKnob*> knobs;
        juce::Rectangle<int> bounds;
    };

    LabeledKnob& addKnob (const juce::String& paramId, const juce::String& text);

    PDHybridAudioProcessor& proc;

    std::vector<std::unique_ptr<LabeledKnob>> knobs;

    juce::Label    oscTypeLabel;
    juce::ComboBox oscTypeBox;
    std::unique_ptr<ComboBoxAttachment> oscTypeAttachment;

    juce::Label    filterTypeLabel;
    juce::ComboBox filterTypeBox;
    std::unique_ptr<ComboBoxAttachment> filterTypeAttachment;

    juce::ComboBox lfoWaveBox;
    std::unique_ptr<ComboBoxAttachment> lfoWaveAttachment;

    // Modulation matrix rows: source combo, destination combo, depth knob.
    juce::ComboBox modSrcBox[4];
    juce::ComboBox modDestBox[4];
    juce::Slider   modDepthSlider[4];
    std::unique_ptr<ComboBoxAttachment> modSrcAtt[4];
    std::unique_ptr<ComboBoxAttachment> modDestAtt[4];
    std::unique_ptr<SliderAttachment>   modDepthAtt[4];
    juce::Rectangle<int> matrixBounds;

    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PDHybridEditor)
};
