#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "SynthLookAndFeel.h"
#include "ScopeDisplay.h"
#include "Displays.h"
#include "dsp/ChordNamer.h"
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
                       private juce::AsyncUpdater,
                       private juce::Timer
{
public:
    explicit PDHybridEditor (PDHybridAudioProcessor&);
    ~PDHybridEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // knob -> Inspector selection

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
        juce::String paramId;
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
        // Which column this card belongs in, or -1 to let the packer choose the
        // shortest. Which cards sit next to each other is a design decision, not
        // something to leave to a fill heuristic -- greedy packing is well
        // balanced but scatters related cards, so every page states its own
        // arrangement and the automatic path is the fallback.
        int column = -1;
        // Stacking is obsolete under column packing: the packer already keeps
        // columns level, and forcing two cards into one unit defeats it -- LFO 1
        // and LFO 2 shared an id and so both landed in the same column, leaving
        // it 220 px longer than its neighbour. Kept at 0 for every section.
        int stackId = 0;
        juce::Component* custom = nullptr;
        int customH = 0;          // height reserved for `custom`, 0 = none
        // A second display placed *between* two groups of knobs, so a card can
        // hold e.g. a filter's response, its controls, its envelope curve and
        // then that envelope's controls — one object instead of two cards.
        juce::Component* custom2 = nullptr;
        int customH2 = 0;
        int knobSplit = -1;       // knobs before this index go above custom2
        juce::Colour titleCol {};   // set from the theme when the section is built
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
        /** Re-height the card hosting `c` (used when a card's body collapses). */
        void setCustomHeight (juce::Component* c, int newHeight);

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

    /** Editor for the three Casio-style 8-stage envelopes (DCO -> pitch,
        DCW -> PD amount, MULTI -> filter).

        The three banks share one card: a stage selector chooses which is being
        edited, the curve is drawn as a draggable breakpoint polyline (x is real
        elapsed time, so stage widths show their true durations), and the bank's
        18 parameters stay underneath as ordinary knobs. Nothing is hidden from
        the host — every rate, level, amount and sustain point keeps its own
        parameter and automation lane; the graph is a second view of them.

        Values are polled on a timer rather than listened to, so automation and
        preset loads move the curve without any audio-thread callbacks. */
    class StageEnvelopePanel : public juce::Component,
                               private juce::Timer
    {
    public:
        struct Bank
        {
            juce::String name;                   // "DCO"
            juce::String dest;                   // "-> pitch"
            juce::RangedAudioParameter* rate[8]  {};
            juce::RangedAudioParameter* level[8] {};
            juce::RangedAudioParameter* sustain = nullptr;   // int, 1..8
            std::vector<LabeledKnob*> knobs;      // R1..R8, L1..L8, Amt, Sus
            bool bipolar = false;                 // levels are centred on 0.5
            juce::Colour colour;
        };

        StageEnvelopePanel();
        ~StageEnvelopePanel() override;

        void addBank (Bank b);
        void start();                             // after the last addBank
        int  preferredHeight() const;             // selector + graph + knob rows
        std::function<void()> onHeightChanged;    // fired when NUMERIC collapses

        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        void timerCallback() override;
        void selectBank (int index);
        juce::Rectangle<int> graphArea() const;
        // Node i's centre, 0..7; node -1 is the (0, 0) origin the curve starts at.
        juce::Point<float> nodePos (int i, double totalOverride = 0.0) const;
        double totalTime() const;
        int    hitNode (juce::Point<float> p) const;

        juce::Rectangle<int> selectorArea() const;
        juce::Rectangle<int> segmentArea (int i) const;
        juce::Rectangle<int> expanderArea() const;

        std::vector<Bank> banks;
        bool expanded = true;    // NUMERIC row of R/L knobs showing
        // How many rows the sixteen numeric knobs actually took last layout.
        // preferredHeight has to agree with resized(), and resized() only knows
        // once it has a width -- so it records the answer here and fires
        // onHeightChanged when it changes.
        int  numericRows = 1;
        int  active = 0;
        int  dragNode = -1;      // node being dragged, -1 = none
        int  hoverNode = -1;
        double dragTotal = 0.0;  // time base frozen for the duration of a drag
        std::vector<float> lastValues;   // change detection for the repaint timer
    };

    // Trivial holder whose resized()/paint() defer to callbacks (used for the
    // modulation matrix and the performance strip).
    struct CallbackComponent : public juce::Component
    {
        std::function<void()> onResized;
        std::function<void (juce::Graphics&)> onPaint;
        std::function<void (const juce::MouseEvent&)> onMouseDown;
        std::function<bool (const juce::KeyPress&)> onKeyPress;
        void resized() override { if (onResized) onResized(); }
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
        void mouseDown (const juce::MouseEvent& e) override { if (onMouseDown) onMouseDown (e); }
        bool keyPressed (const juce::KeyPress& k) override
        { return onKeyPress ? onKeyPress (k) : false; }
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

    juce::TextButton initButton { "INIT" };
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton prevButton { "<" };
    juce::TextButton nextButton { ">" };
    juce::TextButton abButton { "A/B: A" };
    juce::ComboBox   themeBox;
    // The Inspector is a drawer now, not a reserved column; this is its latch.
    // The drawer's grab handle, on the edge it opens from. Custom-painted: a
    // TextButton with a glyph read as decoration, so this draws a real tab with
    // an arrowhead and a grip, and captions itself INSPECTOR down its length.
    CallbackComponent inspectorHandle;
    bool             inspectorOpen_ = false;
    juce::TextButton presetButton { "Presets" };   // opens the hierarchical preset menu

    /** Installs a skin at runtime: pushes it onto the LookAndFeel, re-applies
        every colour/font baked in at construction time (strip/matrix textbox
        colours, tab bar colours, knob-label fonts), and repaints the tree. */
    void applyTheme (pdtheme::ThemeId id);

    // Footer strip: where you are, how much modulation is live, and the two
    // actions that are not part of editing a patch.
    CallbackComponent footer;
    juce::TextButton randButton { "RAND" };
    juce::TextButton panicButton { "PANIC" };

    // Wavetable import. The button doubles as the readout for which table is
    // loaded, so the Wavetable engine is not a mystery box.
    juce::TextButton wavetableButton { "WAVETABLE: default" };
    std::unique_ptr<juce::FileChooser> wavetableChooser;
    void chooseWavetable();
    void refreshWavetableButton();
    void paintFooter (juce::Graphics&);
    void layoutFooter();

    juce::ValueTree  abState_[2];   // A/B compare snapshots
    int              abSlot_ = 0;

    /** Pushes the last tab (GLOBAL) to the right-hand end of the bar, so the
        settings page reads as off to one side rather than as the fifth stage of
        the signal path. Re-applied after every re-layout the bar performs. */
    struct SignalPathTabs : public juce::TabbedComponent
    {
        using juce::TabbedComponent::TabbedComponent;
        void resized() override            { juce::TabbedComponent::resized(); pinLast(); }
        void currentTabChanged (int, const juce::String&) override { pinLast(); }
        void pinLast()
        {
            auto& bar = getTabbedButtonBar();
            if (bar.getNumTabs() < 2) return;
            if (auto* last = bar.getTabButton (bar.getNumTabs() - 1))
                last->setBounds (last->getBounds().withX (bar.getWidth() - last->getWidth() - 6));
        }
    };
    SignalPathTabs tabs { juce::TabbedButtonBar::TabsAtTop };

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

    // Fixed performance strip above the tab bar: the controls reached on every
    // patch, so they never leave the screen whichever page is showing.
    CallbackComponent strip;
    std::vector<LabeledKnob*> stripKnobs;   // cutoff, reso, macro 1/2, A D S R Vel, master
    juce::ComboBox* stripPoly = nullptr;
    juce::Button*   stripAmpSync = nullptr;    // amp envelope tempo sync
    LabeledKnob*    stripBpm = nullptr;        // always-visible tempo
    juce::ComboBox* stripTempoMode = nullptr;
    juce::Button*   stripLimiter = nullptr;
    juce::Button*   stripArp = nullptr;
    std::vector<int> stripDividers_;        // x positions of the cluster rules
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> stripGroups_;

    // Named sections (built once, then handed to pages).
    Section oscA, oscB, mixer, chordSec;                                  // Voice page
    Section glideSec, unison, bassSec;
    Section pluckSec, drive, routingSec, filter, filter2;   // Shape page
    Section stageEnvSec, modEnv, lfo, lfo2, vibratoSec, arpSec;   // Mod page
    StageEnvelopePanel stageEnv;
    void buildStageEnvelopes();

    // Waveform / curve readouts embedded in the cards that own the parameters.
    // No captions: the card title already names them.
    pdui::EnvelopeCurve ampCurve   { "AMP" };
    pdui::EnvelopeCurve modCurve   { {} };
    pdui::EnvelopeCurve filt1Curve { {} };
    pdui::EnvelopeCurve filt2Curve { {} };
    pdui::EnvelopeCurve bassCurve  { {} };
    pdui::LfoCurve      lfo1Curve;
    pdui::LfoCurve      lfo2Curve;
    pdui::RoutingDiagram routingDiagram;
    pdui::WaveCyclePreview oscACycle, oscBCycle;
    pdui::ChordKeyboard    chordKeys;
    pdui::FilterResponse filt1Resp, filt2Resp;
    pdui::TransferCurve  driveCurve;
    pdui::EqResponse     eqResp;
    pdui::VelocityCurve  velCurveDisp;
    pdui::ScaleOffsets   scaleDisp;
    pdui::LfoCurve       globalLfoCurve;
    // LFO cards put the waveform beside the rate knob, so these small holders
    // own both and lay them out side by side.
    CallbackComponent    lfo1Head, lfo2Head, globalLfoHead;
    pdui::GainReductionMeter grMeter;
    pdui::DelayTaps      delayTaps;
    pdui::ReverbDecay    reverbDecay;

    //--------------------------------------------------------------------------
    // Modulation Inspector: a permanent right-hand column that makes the matrix
    // readable from the outside. Click any knob and it lists every route
    // pointing at it; knobs that are routed to carry an amber ring, so a patch's
    // modulation is visible on the panel instead of only inside the matrix.
    CallbackComponent inspector;
    juce::TextButton inspAddRoute { "+ ADD ROUTE" };
    juce::TextButton inspFullMatrix { "FULL MATRIX" };
    juce::TextButton inspSourcesBtn { "SOURCES" };
    juce::TextButton inspDestsBtn { "DESTINATIONS" };

    juce::String selectedParam;              // knob the Inspector is showing
    juce::String selectedName;
    int  selectedSource = 2;                 // modulator shown in DESTINATIONS mode (LFO)
    bool showDestinations = false;           // which direction the Inspector reads

    // Geometry is computed once in layoutInspector() and reused when painting,
    // so the child curves and the painted rows can never drift apart.
    struct RouteRow { juce::Rectangle<int> text, bar; int routeIndex; };
    std::vector<RouteRow> routeRows_;
    juce::Rectangle<int> selCard_, srcCard_, matrixCard_;   // the Inspector's three cards
    int selCardTop_ = 0;

    pdui::SourceMeters sourceMeters;   // real DSP levels, not parameter positions

    void selectParameter (const juce::String& paramId);
    void layoutInspector();
    void paintInspector (juce::Graphics&);
    void inspectorClicked (const juce::MouseEvent&);
    void refreshModRings();                  // tag destination knobs for the ring
    int  firstFreeMatrixSlot() const;
    void addRouteToSelected();
    void timerCallback() override;           // live values + ring refresh

    // Envelope time knobs read out as note divisions while their envelope's
    // SYNC is on. Held so the timer can refresh them when the switch or the
    // tempo moves — the value has not changed, only what it means.
    LabeledKnob* findKnob (const juce::String& paramId);
    void setupEnvTimeReadouts();
    void refreshEnvTimeReadouts();
    std::vector<LabeledKnob*> envTimeKnobs;
    juce::String lastEnvSyncState;           // change detection for the refresh

    // Matrix state as of the last refresh, so painting never touches the APVTS
    // in a tight loop.
    struct RouteView { int slot; int source; int dest; float depth; int curve; };
    std::vector<RouteView> routes_;
    Section chorusSec, delaySec, reverbSec, comp, globalEqSec, stereo;   // Out page
    Section voiceSec, tuningSec, globalLfoSec, qualitySec, tempoSec;     // Global page
    Section envelope;                                                    // amp env (lives in the strip)

    // Modulation matrix. Lives as an overlay over the whole editor rather than
    // on a page: it is the "show me everything" view, opened from the Inspector.
    static constexpr int kNumModRows = 10;
    CallbackComponent matrixHolder;
    juce::TextButton matrixCloseButton { "CLOSE" };
    juce::Rectangle<int> matrixPanel_;      // the card inside the overlay
    void paintMatrix (juce::Graphics&);
    void showMatrix (bool shouldShow);
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
