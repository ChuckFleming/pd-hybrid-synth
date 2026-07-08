#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "SynthLookAndFeel.h"
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
    ~PDHybridEditor() override;

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

    // A section holds a title, a set of knobs laid out in a row, and optional
    // combo boxes placed in its header (right-aligned).
    struct Section
    {
        juce::String title;
        std::vector<LabeledKnob*> knobs;
        std::vector<juce::ComboBox*> combos;
        juce::Rectangle<int> bounds;
    };

    LabeledKnob& addKnob (const juce::String& paramId, const juce::String& text,
                          int decimals = 2);
    juce::ComboBox& addCombo (const juce::String& paramId, const juce::StringArray& items);

    PDHybridAudioProcessor& proc;
    SynthLookAndFeel lnf;

    std::vector<std::unique_ptr<LabeledKnob>> knobs;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;

    juce::ComboBox* oscATypeBox = nullptr;
    juce::ComboBox* oscAWaveBox = nullptr;
    juce::ComboBox* oscBTypeBox = nullptr;
    juce::ComboBox* oscBWaveBox = nullptr;
    juce::ComboBox* filterTypeBox = nullptr;
    juce::ComboBox* lfoWaveBox = nullptr;

    // Modulation matrix rows: source combo, destination combo, depth knob.
    static constexpr int kNumModRows = 6;
    juce::ComboBox modSrcBox[kNumModRows];
    juce::ComboBox modDestBox[kNumModRows];
    juce::Slider   modDepthSlider[kNumModRows];
    std::unique_ptr<ComboBoxAttachment> modSrcAtt[kNumModRows];
    std::unique_ptr<ComboBoxAttachment> modDestAtt[kNumModRows];
    std::unique_ptr<SliderAttachment>   modDepthAtt[kNumModRows];
    juce::Rectangle<int> matrixBounds;

    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PDHybridEditor)
};
