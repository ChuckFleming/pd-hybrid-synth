#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "SynthLookAndFeel.h"
#include "CrtOverlay.h"
#include <functional>
#include <memory>
#include <vector>

/**
    Hand-built editor. Controls are grouped into "cards" (a titled section with a
    row of rotary knobs and optional combo boxes), and the cards are distributed
    across a set of tabs (Oscillators / Filters / Envelopes / Modulation / FX).

    Each tab flows its cards to the available width and lives inside a Viewport,
    so the window stays small, is freely resizable, and never clips its contents.
*/
class PDHybridEditor : public juce::AudioProcessorEditor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::AsyncUpdater
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

    // One titled card: knobs laid out in `cols` columns, optional combos in a
    // header row. `bounds` is filled in during layout and used when painting.
    struct Section
    {
        juce::String title;
        std::vector<LabeledKnob*> knobs;
        std::vector<juce::ComboBox*> combos;
        int cols = 4;
        int stackId = 0;   // non-zero: stacks vertically with consecutive same-id sections
        juce::Rectangle<int> bounds;
    };

    // A tab page: flows a list of Section cards left-to-right, wrapping to the
    // panel width, and optionally hosts one full-width trailing component
    // (used for the modulation matrix). Sized taller than the viewport when
    // needed so the viewport scrolls.
    class SectionPanel : public juce::Component
    {
    public:
        void addSection (const Section& s);
        void setTrailing (juce::Component* c, int fullHeight, juce::String title);

        int  preferredHeight (int width);   // height needed to lay out at `width`
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        int layout (bool apply, int width);   // returns bottom edge

        std::vector<Section> sections;
        juce::Component* trailing = nullptr;
        int              trailingHeight = 0;
        juce::String     trailingTitle;
    };

    // Viewport that keeps its viewed SectionPanel the full visible width and as
    // tall as its content needs.
    class ScrollPanel : public juce::Viewport
    {
    public:
        SectionPanel* panel = nullptr;
        void resized() override;
    };

    // Trivial holder whose resized() defers to a callback (lays out the matrix).
    struct CallbackComponent : public juce::Component
    {
        std::function<void()> onResized;
        void resized() override { if (onResized) onResized(); }
    };

    LabeledKnob& addKnob (const juce::String& paramId, const juce::String& text,
                          int decimals = 2);
    juce::ComboBox& addCombo (const juce::String& paramId, const juce::StringArray& items);

    void buildSections();
    void layoutMatrix();

    // Track each slot's engine type: grey out the PD-only wave controls, and
    // relabel / grey the two shared timbre knobs to match the active engine.
    // Driven off parameter listeners (not the state tree) so it survives the
    // replaceState() a preset load / A-B compare does; the change may arrive off
    // the message thread, so it's bounced through the AsyncUpdater.
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void updateOscControls();

    PDHybridAudioProcessor& proc;
    SynthLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton initButton { "Init" };
    juce::TextButton randButton { "Rand" };
    juce::TextButton panicButton { "Panic" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton deleteButton { "Del" };
    juce::TextButton prevButton { "<" };
    juce::TextButton nextButton { ">" };
    juce::TextButton abButton { "A/B: A" };
    juce::TextButton crtButton { "CRT" };
    juce::ComboBox   presetBox;

    juce::ValueTree  abState_[2];   // A/B compare snapshots
    int              abSlot_ = 0;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    void refreshPresetList();
    void showSavePresetDialog();
    void randomizePatch();

    std::vector<std::unique_ptr<LabeledKnob>> knobs;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;

    std::vector<std::unique_ptr<SectionPanel>> pages;
    std::vector<std::unique_ptr<ScrollPanel>>  scrollers;

    CrtOverlay crtOverlay;   // click-through CRT effect layered over everything

    // Named sections (built once, then handed to pages).
    Section oscA, oscB, mixer, unison, glideSec, stereo, voiceSec, bassSec;  // Oscillators
    Section filter, filter2, filterEnv, filter2Env;                      // Filters
    Section envelope, modEnv, multiEnvSec, pitchEnvSec, dcwEnvSec;       // Envelopes
    Section lfo, lfo2, arpSec;                                           // Modulation
    Section drive, chorusSec, comp, delaySec, reverbSec, globalEqSec, masterSec;  // FX

    // Modulation matrix.
    static constexpr int kNumModRows = 10;
    CallbackComponent matrixHolder;
    juce::ComboBox modSrcBox[kNumModRows];
    juce::ComboBox modDestBox[kNumModRows];
    juce::ComboBox modCurveBox[kNumModRows];
    juce::Slider   modDepthSlider[kNumModRows];
    std::unique_ptr<ComboBoxAttachment> modSrcAtt[kNumModRows];
    std::unique_ptr<ComboBoxAttachment> modDestAtt[kNumModRows];
    std::unique_ptr<ComboBoxAttachment> modCurveAtt[kNumModRows];
    std::unique_ptr<SliderAttachment>   modDepthAtt[kNumModRows];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PDHybridEditor)
};
