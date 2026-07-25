#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "SynthLookAndFeel.h"
#include "CrtOverlay.h"
#include "ScopeDisplay.h"
#include <functional>
#include <memory>
#include <utility>
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
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Three sizes give the panel a hierarchy: Large for the handful of controls
    // that define a patch, Normal for a section's own controls, Small for trim.
    // The layout cell is the same for all three; only the rotary scales inside it.
    enum class KnobSize { Small, Normal, Large };

    struct LabeledKnob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttachment> attachment;
        KnobSize size = KnobSize::Normal;
    };

    // One titled card. `cols` is how many knob columns run inside the card;
    // `span` is how many columns of the page's fixed grid the card occupies.
    // Optional LED toggles sit in the title strip; an optional `custom`
    // component (the multi-stage envelope editor) sits above the knob rows.
    // `bounds` is filled in during layout and used when painting.
    struct Section
    {
        juce::String title;
        std::vector<LabeledKnob*> knobs;
        std::vector<juce::ComboBox*> combos;
        std::vector<juce::Button*> toggles;
        int cols = 4;
        int span = 2;             // grid columns (the page grid is kGridCols wide)
        juce::Component* custom = nullptr;
        int customH = 0;          // height reserved for `custom`, 0 = none
        juce::Rectangle<int> bounds;
    };

    // A tab page. Cards are packed into a *fixed* column grid: the column count
    // never varies with width, so a card's row and column are the same at every
    // window size and only the column width stretches. That is what makes the
    // panel learnable — nothing ever moves to a different place.
    //
    // Optionally hosts one full-width trailing component (the modulation
    // matrix), and is sized taller than the viewport when needed so it scrolls.
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

    // Trivial holder whose resized()/paint() defer to callbacks (used for the
    // modulation matrix and the performance strip).
    struct CallbackComponent : public juce::Component
    {
        std::function<void()> onResized;
        std::function<void (juce::Graphics&)> onPaint;
        void resized() override { if (onResized) onResized(); }
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
    };

    LabeledKnob& addKnob (const juce::String& paramId, const juce::String& text,
                          int decimals = 2, KnobSize size = KnobSize::Normal);
    juce::ComboBox& addCombo (const juce::String& paramId, const juce::StringArray& items);
    juce::Button&   addToggle (const juce::String& paramId, const juce::String& text);

    void buildSections();
    void buildStrip();
    void layoutStrip();
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
    juce::TextButton presetButton { "Presets" };   // opens the hierarchical preset menu

    juce::ValueTree  abState_[2];   // A/B compare snapshots
    int              abSlot_ = 0;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    void refreshPresetList();
    void showPresetMenu();
    void showSavePresetDialog();
    void randomizePatch();

    std::vector<std::unique_ptr<LabeledKnob>> knobs;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::TextButton>> toggleButtons;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    std::vector<std::unique_ptr<SectionPanel>> pages;
    std::vector<std::unique_ptr<ScrollPanel>>  scrollers;

    ScopeDisplay scope_ { [this] (float* d, int n) { proc.readScope (d, n); } };  // master output scope
    CrtOverlay crtOverlay;   // click-through CRT effect layered over everything

    // Fixed performance strip above the tab bar: the controls reached on every
    // patch, so they never leave the screen whichever page is showing.
    CallbackComponent strip;
    std::vector<LabeledKnob*> stripKnobs;   // cutoff, reso, macro 1/2, A D S R Vel, master
    juce::ComboBox* stripPoly = nullptr;
    juce::Button*   stripLimiter = nullptr;
    std::vector<int> stripDividers_;        // x positions of the cluster rules
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> stripGroups_;

    // Named sections (built once, then handed to pages).
    Section oscA, oscB, mixer;                                           // Voice page
    Section glideSec, unison, bassSec;
    Section pluckSec, drive, routingSec, filter, filter2, filterEnv, filter2Env;   // Shape page
    Section modEnv, multiEnvSec, pitchEnvSec, dcwEnvSec, lfo, lfo2, vibratoSec, arpSec;  // Mod page
    Section chorusSec, delaySec, reverbSec, comp, globalEqSec, stereo;   // Out page
    Section voiceSec, globalLfoSec;                                      // Global page
    Section envelope;                                                    // amp env (lives in the strip)

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
