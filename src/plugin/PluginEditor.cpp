#include "PluginEditor.h"

namespace {
constexpr int kKnobW    = 66;
constexpr int kKnobH    = 64;   // rotary + text box below
constexpr int kLabelH   = 14;
constexpr int kCellW    = 72;   // one knob cell (knob + gutter)
constexpr int kCellH    = kLabelH + kKnobH;
constexpr int kHeaderH  = 22;   // card title strip
constexpr int kComboRowH = 24;
constexpr int kCardPad  = 7;    // inner padding of a card
constexpr int kGap      = 10;   // gap between cards
constexpr int kMargin   = 12;   // panel outer margin
constexpr int kMatrixRowH = 26;
constexpr int kTopBar   = 46;   // title bar above the tabs

// Palette
const juce::Colour kBg       (0xff1b1f26);
const juce::Colour kCardBg   (0xff262b34);
const juce::Colour kCardEdge (0xff333b47);
const juce::Colour kHeaderBg (0xff2f3846);
const juce::Colour kAccent   (0xff8fb7ff);
const juce::Colour kTitleCol (0xffe7edf6);

const juce::StringArray kOscTypeNames { "Phase Distortion", "Saw", "Square", "Triangle", "Pulse" };
const juce::StringArray kPdWaveNames  { "Sawtooth", "Square", "Pulse", "Double Sine",
                                        "Saw-Pulse", "Resonant I", "Resonant II", "Resonant III" };
// Must match the "modXSource" choice parameter exactly: ComboBoxParameterAttachment
// maps by item index / (item count - 1), so a shorter list mis-routes every source.
const juce::StringArray kSrcNames { "None", "Mod Env", "LFO", "Velocity", "Pressure",
                                    "Timbre", "Pitch Bend", "Key Track", "Mod Wheel", "LFO 2",
                                    "Multi Env", "Amp Env", "Filt Env A", "Filt Env B",
                                    "Random", "Global LFO", "Macro 1", "Macro 2" };
const juce::StringArray kLfoWaveNames { "Sine", "Triangle", "Square", "Saw", "Ramp Down",
                                        "Sample & Hold", "Smooth Random", "Exponential" };
const juce::StringArray kSyncNames { "Free", "1/1", "1/2", "1/4", "1/8", "1/16",
                                     "1/4.", "1/8.", "1/4T", "1/8T" };
const juce::StringArray kDstNames { "None", "Pitch", "PD Amount", "Pulse Width", "Cutoff",
                                    "Resonance", "Morph", "Drive", "Amplitude", "Pan",
                                    "Osc A Lvl", "Osc B Lvl", "Detune", "Filter 2 Cutoff",
                                    "Delay Mix", "Delay Fbk", "Master Pan", "Global EQ" };
}

//==============================================================================
//  SectionPanel — flows Section cards, paints their frames, hosts a trailing
//  full-width component (the modulation matrix).
//==============================================================================
void PDHybridEditor::SectionPanel::addSection (const Section& s)
{
    for (auto* k : s.knobs)
    {
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
    }
    for (auto* c : s.combos)
        addAndMakeVisible (*c);

    sections.push_back (s);
}

void PDHybridEditor::SectionPanel::setTrailing (juce::Component* c, int fullHeight,
                                                juce::String title)
{
    trailing = c;
    trailingHeight = fullHeight;
    trailingTitle = std::move (title);
    if (trailing != nullptr)
        addAndMakeVisible (*trailing);
}

int PDHybridEditor::SectionPanel::layout (bool apply, int width)
{
    auto cardWidth = [] (const Section& s)
    {
        return s.cols * kCellW + 2 * kCardPad;
    };
    auto cardHeight = [] (const Section& s)
    {
        const int rows = (static_cast<int> (s.knobs.size()) + s.cols - 1) / juce::jmax (1, s.cols);
        int h = kHeaderH + 2 * kCardPad + rows * kCellH;
        if (! s.combos.empty()) h += kComboRowH;
        return h;
    };

    int x = kMargin, y = kMargin, rowH = 0;

    for (auto& s : sections)
    {
        const int cw = cardWidth (s);
        const int ch = cardHeight (s);

        if (x > kMargin && x + cw > width - kMargin)   // wrap to next row
        {
            x = kMargin;
            y += rowH + kGap;
            rowH = 0;
        }

        if (apply)
        {
            s.bounds = { x, y, cw, ch };

            auto inner = s.bounds;
            inner.removeFromTop (kHeaderH);
            inner = inner.reduced (kCardPad);

            if (! s.combos.empty())
            {
                auto comboRow = inner.removeFromTop (kComboRowH).reduced (0, 2);
                const int cwidth = comboRow.getWidth() / static_cast<int> (s.combos.size());
                for (auto* c : s.combos)
                    c->setBounds (comboRow.removeFromLeft (cwidth).reduced (2, 0));
            }

            std::size_t i = 0;
            while (i < s.knobs.size())
            {
                auto row = inner.removeFromTop (kCellH);
                for (int c = 0; c < s.cols && i < s.knobs.size(); ++c, ++i)
                {
                    auto cell = row.removeFromLeft (kCellW);
                    s.knobs[i]->label.setBounds  (cell.removeFromTop (kLabelH));
                    s.knobs[i]->slider.setBounds (cell);
                }
            }
        }

        x += cw + kGap;
        rowH = juce::jmax (rowH, ch);
    }

    y += rowH;

    if (trailing != nullptr)
    {
        y += kGap;
        if (apply)
            trailing->setBounds (kMargin, y, width - 2 * kMargin, trailingHeight);
        y += trailingHeight;
    }

    return y + kMargin;
}

int PDHybridEditor::SectionPanel::preferredHeight (int width)
{
    return layout (false, juce::jmax (width, 2 * kMargin + kCellW));
}

void PDHybridEditor::SectionPanel::resized()
{
    layout (true, getWidth());
}

void PDHybridEditor::SectionPanel::paint (juce::Graphics& g)
{
    auto drawFrame = [&g] (juce::Rectangle<int> b, const juce::String& title)
    {
        g.setColour (kCardBg);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);

        auto header = b.withHeight (kHeaderH);
        g.setColour (kHeaderBg);
        g.fillRoundedRectangle (header.toFloat(), 6.0f);
        g.fillRect (header.withTrimmedTop (kHeaderH / 2));   // square off the bottom corners

        g.setColour (kAccent);
        g.setFont (juce::Font (12.5f, juce::Font::bold));
        g.drawText (" " + title, header, juce::Justification::centredLeft);

        g.setColour (kCardEdge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 6.0f, 1.0f);
    };

    for (const auto& s : sections)
        drawFrame (s.bounds, s.title);

    if (trailing != nullptr)
        drawFrame (trailing->getBounds(), trailingTitle);
}

//==============================================================================
//  ScrollPanel
//==============================================================================
void PDHybridEditor::ScrollPanel::resized()
{
    juce::Viewport::resized();
    if (panel != nullptr)
    {
        const int w = getMaximumVisibleWidth();
        panel->setSize (w, juce::jmax (getMaximumVisibleHeight(), panel->preferredHeight (w)));
    }
}

//==============================================================================
//  Editor
//==============================================================================
PDHybridEditor::LabeledKnob& PDHybridEditor::addKnob (const juce::String& paramId,
                                                      const juce::String& text, int decimals)
{
    auto knob = std::make_unique<LabeledKnob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW, 14);
    knob->slider.setNumDecimalPlacesToDisplay (decimals);

    knob->label.setText (text, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setFont (juce::Font (11.0f, juce::Font::bold));

    knob->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, knob->slider);

    knobs.push_back (std::move (knob));
    return *knobs.back();
}

juce::ComboBox& PDHybridEditor::addCombo (const juce::String& paramId,
                                          const juce::StringArray& items)
{
    auto box = std::make_unique<juce::ComboBox>();
    box->addItemList (items, 1);
    box->setJustificationType (juce::Justification::centredLeft);
    comboAttachments.push_back (
        std::make_unique<ComboBoxAttachment> (proc.apvts, paramId, *box));
    combos.push_back (std::move (box));
    return *combos.back();
}

void PDHybridEditor::buildSections()
{
    // --- Oscillator A ---
    oscA.title  = "Osc A";
    oscA.cols   = 4;
    oscA.combos = { &addCombo ("oscAType", kOscTypeNames), &addCombo ("oscAWave", kPdWaveNames) };
    oscA.knobs  = { &addKnob ("oscAAmount", "PD Amt"), &addKnob ("oscAPulseWidth", "Width"),
                    &addKnob ("oscAOctave", "Oct", 0), &addKnob ("oscASemi", "Semi", 0),
                    &addKnob ("oscAFine", "Fine"), &addKnob ("oscAEqLow", "EQ Lo"),
                    &addKnob ("oscAEqMid", "EQ Mid"), &addKnob ("oscAEqHigh", "EQ Hi") };

    // --- Oscillator B ---
    oscB.title  = "Osc B";
    oscB.cols   = 4;
    oscB.combos = { &addCombo ("oscBType", kOscTypeNames), &addCombo ("oscBWave", kPdWaveNames) };
    oscB.knobs  = { &addKnob ("oscBAmount", "PD Amt"), &addKnob ("oscBPulseWidth", "Width"),
                    &addKnob ("oscBOctave", "Oct", 0), &addKnob ("oscBSemi", "Semi", 0),
                    &addKnob ("oscBFine", "Fine"), &addKnob ("oscBEqLow", "EQ Lo"),
                    &addKnob ("oscBEqMid", "EQ Mid"), &addKnob ("oscBEqHigh", "EQ Hi") };

    // --- Mixer ---
    mixer.title = "Mixer";
    mixer.cols  = 3;
    mixer.knobs = { &addKnob ("oscALevel", "Osc A"), &addKnob ("oscBLevel", "Osc B"),
                    &addKnob ("noiseLevel", "Noise") };

    // --- Unison ---
    unison.title = "Unison";
    unison.cols  = 3;
    unison.knobs = { &addKnob ("unisonVoices", "Voices", 0), &addKnob ("unisonDetune", "Detune"),
                     &addKnob ("unisonWidth", "Width") };

    // --- Glide ---
    glideSec.title  = "Glide";
    glideSec.cols   = 2;
    glideSec.combos = { &addCombo ("glideMode", { "Off", "Always", "Legato" }) };
    glideSec.knobs  = { &addKnob ("glideTime", "Time"), &addKnob ("glideCurve", "Curve") };

    // --- Stereo / Drift ---
    stereo.title = "Stereo / Drift";
    stereo.cols  = 3;
    stereo.knobs = { &addKnob ("pan", "Pan"), &addKnob ("panSpread", "Spread"),
                     &addKnob ("drift", "Drift") };

    // --- Filter ---
    filter.title  = "Filter";
    filter.cols   = 5;
    filter.combos = { &addCombo ("filterType",
                        { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" }) };
    filter.knobs  = { &addKnob ("cutoff", "Cutoff"), &addKnob ("resonance", "Reso"),
                      &addKnob ("filterMorph", "Morph"), &addKnob ("keyTrack", "Key Trk"),
                      &addKnob ("filterEnvAmount", "Env Amt") };

    // --- Filter 2 ---
    filter2.title  = "Filter 2";
    filter2.cols   = 4;
    filter2.combos = { &addCombo ("filterRouting", { "Single", "Series", "Parallel" }),
                       &addCombo ("filter2Type",
                        { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" }) };
    filter2.knobs  = { &addKnob ("filter2Cutoff", "Cutoff"), &addKnob ("filter2Res", "Reso"),
                       &addKnob ("filter2Morph", "Morph"), &addKnob ("filter2EnvAmount", "Env Amt") };

    // --- Filter Envelopes ---
    filterEnv.title = "Filter Env";
    filterEnv.cols  = 4;
    filterEnv.knobs = { &addKnob ("filterEnvA", "Atk"), &addKnob ("filterEnvD", "Dec"),
                        &addKnob ("filterEnvS", "Sus"), &addKnob ("filterEnvR", "Rel") };

    filter2Env.title = "Filter 2 Env";
    filter2Env.cols  = 4;
    filter2Env.knobs = { &addKnob ("filter2EnvA", "Atk"), &addKnob ("filter2EnvD", "Dec"),
                         &addKnob ("filter2EnvS", "Sus"), &addKnob ("filter2EnvR", "Rel") };

    // --- Amp / Mod Envelopes ---
    envelope.title = "Amp Env";
    envelope.cols  = 4;
    envelope.knobs = { &addKnob ("attack", "Atk"), &addKnob ("decay", "Dec"),
                       &addKnob ("sustain", "Sus"), &addKnob ("release", "Rel") };

    modEnv.title = "Mod Env";
    modEnv.cols  = 4;
    modEnv.knobs = { &addKnob ("modEnvA", "Atk"), &addKnob ("modEnvD", "Dec"),
                     &addKnob ("modEnvS", "Sus"), &addKnob ("modEnvR", "Rel") };

    // --- CZ multi-stage envelope (8 rate + 8 level, aligned in rows) ---
    multiEnvSec.title = "Multi-Stage Env (CZ)  ->  filter";
    multiEnvSec.cols  = 8;
    for (int i = 1; i <= 8; ++i)
        multiEnvSec.knobs.push_back (&addKnob ("czRate" + juce::String (i), "R" + juce::String (i)));
    for (int i = 1; i <= 8; ++i)
        multiEnvSec.knobs.push_back (&addKnob ("czLevel" + juce::String (i), "L" + juce::String (i)));
    multiEnvSec.knobs.push_back (&addKnob ("czAmount", "Amt"));
    multiEnvSec.knobs.push_back (&addKnob ("czSustain", "Sus", 0));

    // --- LFOs ---
    lfo.title  = "LFO";
    lfo.cols   = 2;
    lfo.combos = { &addCombo ("lfoWave", kLfoWaveNames), &addCombo ("lfoSync", kSyncNames) };
    lfo.knobs  = { &addKnob ("lfoRate", "Rate") };

    lfo2.title  = "LFO 2";
    lfo2.cols   = 2;
    lfo2.combos = { &addCombo ("lfo2Wave", kLfoWaveNames), &addCombo ("lfo2Sync", kSyncNames) };
    lfo2.knobs  = { &addKnob ("lfo2Rate", "Rate") };

    // --- Overdrive ---
    drive.title  = "Overdrive";
    drive.cols   = 5;
    drive.combos = { &addCombo ("driveType",
                       { "Soft", "Cubic", "Hard Clip", "Tube", "Diode", "Fuzz", "Rectify",
                         "Wavefold", "Foldback" }) };
    drive.knobs  = { &addKnob ("drive", "Drive"), &addKnob ("bias", "Bias"),
                     &addKnob ("gain", "Gain"), &addKnob ("crushBits", "Crush"),
                     &addKnob ("downsample", "Downsmpl") };

    // --- Compressor ---
    comp.title = "Compressor";
    comp.cols  = 5;
    comp.knobs = { &addKnob ("compThreshold", "Thr"), &addKnob ("compRatio", "Ratio"),
                   &addKnob ("compAttack", "Atk"), &addKnob ("compRelease", "Rel"),
                   &addKnob ("compMakeup", "Gain") };

    // --- Delay ---
    delaySec.title  = "Delay";
    delaySec.cols   = 5;
    delaySec.combos = { &addCombo ("delayMode", { "Mono", "Stereo", "Ping-Pong" }),
                        &addCombo ("delaySyncL", kSyncNames), &addCombo ("delaySyncR", kSyncNames) };
    delaySec.knobs  = { &addKnob ("delayTimeL", "Time L"), &addKnob ("delayTimeR", "Time R"),
                        &addKnob ("delayFeedback", "Fbk"), &addKnob ("delayMix", "Mix"),
                        &addKnob ("delayDuck", "Duck") };

    // --- Global master EQ (freq + gain per band) ---
    globalEqSec.title = "Global EQ";
    globalEqSec.cols  = 4;
    globalEqSec.knobs = { &addKnob ("geLowFreq", "Lo Hz", 0),  &addKnob ("geLowGain", "Lo dB", 1),
                          &addKnob ("geMid1Freq", "M1 Hz", 0), &addKnob ("geMid1Gain", "M1 dB", 1),
                          &addKnob ("geMid2Freq", "M2 Hz", 0), &addKnob ("geMid2Gain", "M2 dB", 1),
                          &addKnob ("geHighFreq", "Hi Hz", 0), &addKnob ("geHighGain", "Hi dB", 1) };
}

PDHybridEditor::PDHybridEditor (PDHybridAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (initButton);
    initButton.onClick = [this]
    {
        for (auto* param : proc.getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    };

    buildSections();

    // --- Modulation matrix widgets (hosted on the Modulation tab) ---
    for (int i = 0; i < kNumModRows; ++i)
    {
        const auto s = juce::String (i + 1);

        modSrcBox[i].addItemList (kSrcNames, 1);
        modDestBox[i].addItemList (kDstNames, 1);
        matrixHolder.addAndMakeVisible (modSrcBox[i]);
        matrixHolder.addAndMakeVisible (modDestBox[i]);

        modSrcAtt[i]  = std::make_unique<ComboBoxAttachment> (proc.apvts, "mod" + s + "Source", modSrcBox[i]);
        modDestAtt[i] = std::make_unique<ComboBoxAttachment> (proc.apvts, "mod" + s + "Dest",   modDestBox[i]);

        modDepthSlider[i].setSliderStyle (juce::Slider::LinearHorizontal);
        modDepthSlider[i].setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16);
        modDepthSlider[i].setNumDecimalPlacesToDisplay (2);
        matrixHolder.addAndMakeVisible (modDepthSlider[i]);
        modDepthAtt[i] = std::make_unique<SliderAttachment> (proc.apvts, "mod" + s + "Depth", modDepthSlider[i]);
    }
    matrixHolder.onResized = [this] { layoutMatrix(); };

    // --- Assemble tabs ---
    tabs.setTabBarDepth (30);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, kBg);
    tabs.setColour (juce::TabbedComponent::outlineColourId, kCardEdge);
    addAndMakeVisible (tabs);

    struct Page { juce::String name; std::vector<Section*> secs; juce::Component* trailing; juce::String trailingTitle; int trailingH; };
    const int matrixH = kHeaderH + (kNumModRows / 2) * kMatrixRowH + kCardPad * 2;

    std::vector<Page> layout {
        { "Oscillators", { &oscA, &oscB, &mixer, &unison, &glideSec, &stereo }, nullptr, {}, 0 },
        { "Filters",     { &filter, &filter2, &filterEnv, &filter2Env },        nullptr, {}, 0 },
        { "Envelopes",   { &envelope, &modEnv, &multiEnvSec },                   nullptr, {}, 0 },
        { "Modulation",  { &lfo, &lfo2 }, &matrixHolder,
          "Modulation Matrix   (Source -> Destination x Depth)", matrixH },
        { "FX",          { &drive, &comp, &delaySec, &globalEqSec },             nullptr, {}, 0 },
    };

    for (auto& pg : layout)
    {
        auto panel = std::make_unique<SectionPanel>();
        for (auto* sec : pg.secs)
            panel->addSection (*sec);
        if (pg.trailing != nullptr)
            panel->setTrailing (pg.trailing, pg.trailingH, pg.trailingTitle);

        auto scroller = std::make_unique<ScrollPanel>();
        scroller->panel = panel.get();
        scroller->setViewedComponent (panel.get(), false);
        scroller->setScrollBarsShown (true, false);

        tabs.addTab (pg.name, kBg, scroller.get(), false);

        pages.push_back (std::move (panel));
        scrollers.push_back (std::move (scroller));
    }

    setResizable (true, true);
    setResizeLimits (720, 460, 2200, 1500);
    setSize (1000, 640);
}

PDHybridEditor::~PDHybridEditor()
{
    tabs.clearTabs();     // release content components before members are destroyed
    setLookAndFeel (nullptr);
}

void PDHybridEditor::layoutMatrix()
{
    const int matRows = kNumModRows / 2;
    auto marea = matrixHolder.getLocalBounds();
    marea.removeFromTop (kHeaderH);
    marea = marea.reduced (kCardPad, kCardPad);

    for (int rowI = 0; rowI < matRows; ++rowI)
    {
        auto row = marea.removeFromTop (kMatrixRowH).reduced (2, 2);
        const int half = row.getWidth() / 2;
        for (int colI = 0; colI < 2; ++colI)
        {
            const int idx = rowI * 2 + colI;
            auto cell = row.removeFromLeft (half).reduced (3, 0);
            modSrcBox[idx].setBounds  (cell.removeFromLeft (100));
            cell.removeFromLeft (4);
            modDestBox[idx].setBounds (cell.removeFromLeft (100));
            cell.removeFromLeft (6);
            modDepthSlider[idx].setBounds (cell);
        }
    }
}

void PDHybridEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kTopBar);
    initButton.setBounds (top.getRight() - 76, (kTopBar - 26) / 2, 64, 26);
    tabs.setBounds (r);
}

void PDHybridEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    auto top = getLocalBounds().removeFromTop (kTopBar);
    g.setColour (kTitleCol);
    g.setFont (juce::Font (21.0f, juce::Font::bold));
    g.drawText ("  PD Hybrid Synth", top, juce::Justification::centredLeft);
}
