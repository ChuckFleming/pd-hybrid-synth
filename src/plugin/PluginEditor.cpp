#include "PluginEditor.h"

namespace {
// Layout is on an 8-point grid so vertical/horizontal rhythm stays consistent.
constexpr int kKnobW    = 64;
constexpr int kKnobH    = 64;   // rotary + text box below
constexpr int kLabelH   = 16;
constexpr int kCellW    = 72;   // one knob cell (knob + gutter)
constexpr int kCellH    = kLabelH + kKnobH;   // 80
constexpr int kHeaderH  = 24;   // card title strip
constexpr int kComboRowH = 24;
constexpr int kCardPad  = 8;    // inner padding of a card
constexpr int kGap      = 8;    // gap between cards
constexpr int kMargin   = 16;   // panel outer margin
constexpr int kMatrixRowH = 26;
constexpr int kTopBar   = 48;   // title bar above the tabs
constexpr int kGridCols = 6;    // every tab page uses this fixed column count
constexpr int kStripH    = 116;  // fixed performance strip between title bar and tabs
constexpr int kStripCapH = 13;   // caption band along the strip's top edge

// Palette — "CZ Terminal": black with green phosphor, outlined boxes.
const juce::Colour kBg       (0xff000000);
const juce::Colour kCardBg   (0xff000000);
const juce::Colour kCardEdge (0xff1c3a2b);   // dim green box outline
const juce::Colour kHeaderBg (0xff000000);
const juce::Colour kAccent   (0xff4be08a);   // phosphor green
const juce::Colour kTitleCol (0xff4be08a);
const juce::Colour kLabelCol (0xff37b06e);   // dim green control labels
const juce::Colour kValueCol (0xff4be08a);   // bright green readouts

juce::Font monoFont (float height, bool bold = false)
{
    return juce::Font (juce::Font::getDefaultMonospacedFontName(), height,
                       bold ? juce::Font::bold : juce::Font::plain);
}

const juce::StringArray kOscTypeNames { "Phase Distortion", "Saw", "Square", "Triangle", "Pulse", "Vector PS", "Scanned", "VOSIM", "Walsh" };
const juce::StringArray kPdWaveNames  { "Sawtooth", "Square", "Pulse", "Double Sine",
                                        "Saw-Pulse", "Resonant I", "Resonant II", "Resonant III" };

// Per-type role of the two shared timbre knobs (the "amount" and "pulse width"
// knobs). Each engine reads them differently, and some ignore one or both, so
// the editor relabels them and greys out the ones an engine doesn't use.
struct OscKnobRoles
{
    juce::String amountLabel; bool amountActive;
    juce::String pwLabel;     bool pwActive;
    juce::String engineLabel; bool engineActive;   // the third "engine extra" knob
    bool         exciteActive;                      // the Scanned excite-shape combo
};
OscKnobRoles oscKnobRoles (int type)
{
    switch (static_cast<pdhybrid::OscType> (type))
    {
        case pdhybrid::OscType::PhaseDistortion: return { "PD Amt", true,  "Width", false, "Engine",  false, false };  // DCW; PD ignores pulse width
        case pdhybrid::OscType::Saw:             return { "PD Amt", false, "Width", false, "Engine",  false, false };
        case pdhybrid::OscType::Square:          return { "PD Amt", false, "Width", true,  "Engine",  false, false };
        case pdhybrid::OscType::Triangle:        return { "PD Amt", false, "Width", false, "Engine",  false, false };
        case pdhybrid::OscType::Pulse:           return { "PD Amt", false, "Width", true,  "Engine",  false, false };
        case pdhybrid::OscType::VPS:             return { "Vert",   true,  "Horiz", true,  "Engine",  false, false };  // 2D inflection point
        case pdhybrid::OscType::Scanned:         return { "Stiff",  true,  "Damp",  true,  "Morph",   true,  true  };  // + excite shape
        case pdhybrid::OscType::Vosim:           return { "Formant", true, "Decay", true,  "Pulses",  true,  false };  // engine = pulse count
        case pdhybrid::OscType::Walsh:           return { "Tilt",   true,  "Odd",   true,  "Fold",    true,  false };  // engine = wavefold
        default:                                 return { "PD Amt", true,  "Width", true,  "Engine",  true,  false };
    }
}
// Must match the "modXSource" choice parameter exactly: ComboBoxParameterAttachment
// maps by item index / (item count - 1), so a shorter list mis-routes every source.
const juce::StringArray kSrcNames { "None", "Mod Env", "LFO", "Velocity", "Pressure",
                                    "Timbre", "Pitch Bend", "Key Track", "Mod Wheel", "LFO 2",
                                    "Multi Env", "Amp Env", "Filt Env A", "Filt Env B",
                                    "Random", "Global LFO", "Macro 1", "Macro 2", "Pitch Env" };
const juce::StringArray kLfoWaveNames { "Sine", "Triangle", "Square", "Saw", "Ramp Down",
                                        "Sample & Hold", "Smooth Random", "Exponential" };
const juce::StringArray kSyncNames { "Free", "1/1", "1/2", "1/4", "1/8", "1/16",
                                     "1/4.", "1/8.", "1/4T", "1/8T" };
const juce::StringArray kDstNames { "None", "Pitch", "PD Amount", "Pulse Width", "Cutoff",
                                    "Resonance", "Morph", "Drive", "Amplitude", "Pan",
                                    "Osc A Lvl", "Osc B Lvl", "Detune", "Filter 2 Cutoff",
                                    "LFO Rate", "LFO 2 Rate", "Noise Lvl",
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
    for (auto* t : s.toggles)
        addAndMakeVisible (*t);
    if (s.custom != nullptr)
        addAndMakeVisible (*s.custom);

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
    const int comboMinW = 112;   // enough to read the longest choice ("Phase Distortion")

    auto knobRows = [&] (const Section& s)
    {
        return (static_cast<int> (s.knobs.size()) + s.cols - 1) / juce::jmax (1, s.cols);
    };

    // FIXED grid: the column count is a constant, not a function of width. Cards
    // therefore always land in the same row and column; resizing only stretches
    // the columns. (This is deliberate — see the SectionPanel comment.)
    const int contentW = juce::jmax (kGridCols * 60, width - 2 * kMargin);
    const int numCols  = kGridCols;
    const int colW     = (contentW - (numCols - 1) * kGap) / numCols;
    const int colPitch = colW + kGap;

    auto spanOf = [&] (const Section& s) { return juce::jlimit (1, numCols, s.span); };
    auto cardW  = [&] (int span) { return span * colW + (span - 1) * kGap; };
    auto comboRowsFor = [&] (const Section& s, int cw)
    {
        if (s.combos.empty()) return 0;
        const int cpr = juce::jmax (1, (cw - 2 * kCardPad) / comboMinW);
        return (static_cast<int> (s.combos.size()) + cpr - 1) / cpr;
    };
    auto sectionHeight = [&] (const Section& s, int comboRowsCount)
    {
        return kHeaderH + 2 * kCardPad + comboRowsCount * kComboRowH
             + s.customH + (s.customH > 0 ? kCardPad : 0)
             + knobRows (s) * kCellH;
    };

    // Position one card's toggles, combos, custom component and knobs.
    auto placeCard = [&] (Section& s, juce::Rectangle<int> bounds, int comboRowsCount)
    {
        s.bounds = bounds;

        // LED toggles sit in the title strip, right-aligned, breaking the frame
        // the same way the title tag does on the left.
        if (! s.toggles.empty())
        {
            int tx = bounds.getRight() - 8;
            for (auto it = s.toggles.rbegin(); it != s.toggles.rend(); ++it)
            {
                const int tw = juce::jmax (34, (*it)->getButtonText().length() * 7 + 14);
                tx -= tw;
                (*it)->setBounds (tx, bounds.getY() - 1, tw, kHeaderH - 6);
                tx -= 4;
            }
        }

        auto inner = bounds;
        inner.removeFromTop (kHeaderH);
        inner = inner.reduced (kCardPad);

        auto comboZone = inner.removeFromTop (comboRowsCount * kComboRowH);
        if (! s.combos.empty())
        {
            const int cpr = juce::jmax (1, comboZone.getWidth() / comboMinW);
            std::size_t ci = 0;
            while (ci < s.combos.size())
            {
                auto crow = comboZone.removeFromTop (kComboRowH).reduced (0, 2);
                const int inThis = juce::jmin (cpr, static_cast<int> (s.combos.size() - ci));
                const int cwd = crow.getWidth() / inThis;
                for (int k = 0; k < inThis; ++k, ++ci)
                    s.combos[ci]->setBounds (crow.removeFromLeft (cwd).reduced (2, 0));
            }
        }

        if (s.custom != nullptr && s.customH > 0)
        {
            s.custom->setBounds (inner.removeFromTop (s.customH));
            inner.removeFromTop (kCardPad);
        }

        const int cellW = inner.getWidth() / juce::jmax (1, s.cols);
        std::size_t k = 0;
        while (k < s.knobs.size())
        {
            auto krow = inner.removeFromTop (kCellH);
            for (int c = 0; c < s.cols && k < s.knobs.size(); ++c, ++k)
            {
                auto cell = krow.removeFromLeft (cellW);
                s.knobs[k]->label.setBounds (cell.removeFromTop (kLabelH));
                // Slider takes the whole cell so its value box has room; the
                // look-and-feel scales the drawn rotary by the knob's size tag.
                s.knobs[k]->slider.setBounds (cell);
            }
        }
    };

    // Pack cards into rows of the fixed grid, honouring each card's declared span.
    std::vector<int> secRow (sections.size(), 0), secCol (sections.size(), 0);
    {
        int col = 0, row = 0;
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const int sp = spanOf (sections[i]);
            if (col > 0 && col + sp > numCols) { ++row; col = 0; }
            secRow[i] = row; secCol[i] = col; col += sp;
        }
    }
    const int numRows = sections.empty() ? 0 : (secRow.back() + 1);

    // A row shares one combo-zone depth and one height, so card bottoms line up.
    std::vector<int> rowComboRows (juce::jmax (1, numRows), 0), rowHeight (juce::jmax (1, numRows), 0);
    for (std::size_t i = 0; i < sections.size(); ++i)
        rowComboRows[secRow[i]] = juce::jmax (rowComboRows[secRow[i]],
            comboRowsFor (sections[i], cardW (spanOf (sections[i]))));

    for (std::size_t i = 0; i < sections.size(); ++i)
        rowHeight[secRow[i]] = juce::jmax (rowHeight[secRow[i]],
            sectionHeight (sections[i], rowComboRows[secRow[i]]));

    std::vector<int> rowY (juce::jmax (1, numRows), kMargin);
    { int yy = kMargin; for (int r = 0; r < numRows; ++r) { rowY[r] = yy; yy += rowHeight[r] + kGap; } }

    if (apply)
        for (std::size_t i = 0; i < sections.size(); ++i)
            placeCard (sections[i],
                       { kMargin + secCol[i] * colPitch, rowY[secRow[i]],
                         cardW (spanOf (sections[i])), rowHeight[secRow[i]] },
                       rowComboRows[secRow[i]]);

    int y = numRows > 0 ? (rowY[numRows - 1] + rowHeight[numRows - 1]) : kMargin;

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
        // Black box with a thin green outline (square corners, terminal style).
        g.setColour (kCardBg);
        g.fillRect (b);
        g.setColour (kCardEdge);
        g.drawRect (b, 1);

        // Title label sits in a black notch breaking the top border.
        g.setFont (monoFont (11.5f));
        const int tw = g.getCurrentFont().getStringWidth (title) + 14;
        juce::Rectangle<int> tag (b.getX() + 12, b.getY() - 1, tw, kHeaderH - 6);
        g.setColour (kCardBg);
        g.fillRect (tag);
        g.setColour (kTitleCol);
        g.drawText (title, tag.withTrimmedLeft (5), juce::Justification::centredLeft);
    };

    for (const auto& s : sections)
    {
        drawFrame (s.bounds, s.title);

        // Punch a black notch behind each header toggle so it reads as breaking
        // the frame, matching the title tag on the other end.
        g.setColour (kCardBg);
        for (auto* t : s.toggles)
            g.fillRect (t->getBounds().expanded (3, 0));
    }

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
                                                      const juce::String& text, int decimals,
                                                      KnobSize size)
{
    auto knob = std::make_unique<LabeledKnob>();

    knob->size = size;
    knob->slider.getProperties().set ("knobSize", static_cast<int> (size));
    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW, 14);
    knob->slider.setNumDecimalPlacesToDisplay (decimals);
    if (auto* p = proc.apvts.getParameter (paramId))
        knob->slider.setTooltip (p->getName (64));

    knob->label.setText (text, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    // Large knobs get the bright accent label so the promoted controls read first.
    knob->label.setFont (monoFont (size == KnobSize::Large ? 11.0f : 10.5f));
    knob->label.setColour (juce::Label::textColourId,
                           size == KnobSize::Large ? kAccent : kLabelCol);

    knob->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, knob->slider);

    knobs.push_back (std::move (knob));
    return *knobs.back();
}

juce::Button& PDHybridEditor::addToggle (const juce::String& paramId, const juce::String& text)
{
    auto b = std::make_unique<juce::TextButton> (text);
    b->setClickingTogglesState (true);
    if (auto* p = proc.apvts.getParameter (paramId))
        b->setTooltip (p->getName (64));
    buttonAttachments.push_back (
        std::make_unique<ButtonAttachment> (proc.apvts, paramId, *b));
    toggleButtons.push_back (std::move (b));
    return *toggleButtons.back();
}

juce::ComboBox& PDHybridEditor::addCombo (const juce::String& paramId,
                                          const juce::StringArray& items)
{
    auto box = std::make_unique<juce::ComboBox>();
    box->addItemList (items, 1);
    box->setJustificationType (juce::Justification::centredLeft);
    if (auto* p = proc.apvts.getParameter (paramId))
        box->setTooltip (p->getName (64));
    comboAttachments.push_back (
        std::make_unique<ComboBoxAttachment> (proc.apvts, paramId, *box));
    combos.push_back (std::move (box));
    return *combos.back();
}

void PDHybridEditor::buildSections()
{
    // ---------------------------------------------------------------- VOICE --
    // Osc A/B carry their own Level knob (promoted, alongside the timbre knob)
    // so the two decisions that define a layer sit together.
    oscA.title  = "Osc A";
    oscA.cols   = 4;
    oscA.span   = 2;
    oscA.combos = { &addCombo ("oscAType", kOscTypeNames), &addCombo ("oscAWave", kPdWaveNames),
                    &addCombo ("oscAWave2", kPdWaveNames),
                    &addCombo ("oscAExcite", { "Pluck", "Impulse", "Noise", "Triangle" }) };
    oscA.toggles = { &addToggle ("oscACombine", "CMB") };
    // NOTE: updateOscControls() addresses knobs 0/1/2 as amount / width / engine.
    oscA.knobs  = { &addKnob ("oscAAmount", "PD Amt", 2, KnobSize::Large),
                    &addKnob ("oscAPulseWidth", "Width"),
                    &addKnob ("oscAEngine", "Engine"),
                    &addKnob ("oscALevel", "Level", 2, KnobSize::Large),
                    &addKnob ("oscAOctave", "Oct", 0), &addKnob ("oscASemi", "Semi", 0),
                    &addKnob ("oscAFine", "Fine"), &addKnob ("oscAEqLow", "EQ Lo"),
                    &addKnob ("oscAEqMid", "EQ Mid"), &addKnob ("oscAEqHigh", "EQ Hi") };

    oscB.title  = "Osc B";
    oscB.cols   = 4;
    oscB.span   = 2;
    oscB.combos = { &addCombo ("oscBType", kOscTypeNames), &addCombo ("oscBWave", kPdWaveNames),
                    &addCombo ("oscBWave2", kPdWaveNames),
                    &addCombo ("oscBExcite", { "Pluck", "Impulse", "Noise", "Triangle" }) };
    oscB.toggles = { &addToggle ("oscBCombine", "CMB") };
    oscB.knobs  = { &addKnob ("oscBAmount", "PD Amt", 2, KnobSize::Large),
                    &addKnob ("oscBPulseWidth", "Width"),
                    &addKnob ("oscBEngine", "Engine"),
                    &addKnob ("oscBLevel", "Level", 2, KnobSize::Large),
                    &addKnob ("oscBOctave", "Oct", 0), &addKnob ("oscBSemi", "Semi", 0),
                    &addKnob ("oscBFine", "Fine"), &addKnob ("oscBEqLow", "EQ Lo"),
                    &addKnob ("oscBEqMid", "EQ Mid"), &addKnob ("oscBEqHigh", "EQ Hi") };

    mixer.title  = "Mixer";
    mixer.cols   = 4;
    mixer.span   = 2;
    mixer.combos = { &addCombo ("oscCrossMod", { "No Cross Mod", "Hard Sync", "Phase Mod" }) };
    mixer.knobs  = { &addKnob ("noiseLevel", "Noise"), &addKnob ("ringMod", "Ring"),
                     &addKnob ("noiseMod", "N.Mod"), &addKnob ("crossModAmount", "X-Amt") };

    bassSec.title   = "Mono Bass";
    bassSec.cols    = 5;
    bassSec.span    = 3;
    bassSec.toggles = { &addToggle ("bassOn", "ON") };
    bassSec.combos  = { &addCombo ("bassWave", { "Saw", "Square", "Triangle", "Pulse" }),
                        &addCombo ("bassPriority", { "Last", "Top", "Bottom" }) };
    bassSec.knobs   = { &addKnob ("bassOctave", "Oct", 0), &addKnob ("bassTune", "Tune"),
                        &addKnob ("bassHarmonics", "Harm"), &addKnob ("bassLevel", "Level"),
                        &addKnob ("bassGlide", "Glide"),
                        &addKnob ("bassAttack", "Atk"), &addKnob ("bassDecay", "Dec"),
                        &addKnob ("bassSustain", "Sus"), &addKnob ("bassRelease", "Rel") };

    unison.title = "Unison / Drift";
    unison.cols  = 4;
    unison.span  = 2;
    unison.knobs = { &addKnob ("unisonVoices", "Voices", 0), &addKnob ("unisonDetune", "Detune"),
                     &addKnob ("unisonWidth", "Width"), &addKnob ("drift", "Drift") };

    glideSec.title  = "Glide";
    glideSec.cols   = 2;
    glideSec.span   = 1;
    glideSec.combos = { &addCombo ("glideMode", { "Off", "Always", "Legato" }) };
    glideSec.knobs  = { &addKnob ("glideTime", "Time"), &addKnob ("glideCurve", "Curve") };

    // ---------------------------------------------------------------- SHAPE --
    // Signal topology gets its own card instead of hiding in three unrelated ones.
    routingSec.title  = "Routing";
    routingSec.cols   = 1;
    routingSec.span   = 2;
    routingSec.combos = { &addCombo ("filterRouting", { "Single Filter", "Filters Series", "Filters Parallel" }),
                          &addCombo ("drivePos", { "Drive Post Filter", "Drive Pre Filter" }),
                          &addCombo ("fxRouting", { "Delay -> Reverb", "Reverb -> Delay", "Reverb, Dry Delay" }) };

    pluckSec.title   = "Pluck";
    pluckSec.cols    = 4;
    pluckSec.span    = 2;
    pluckSec.toggles = { &addToggle ("pluckOn", "ON") };
    pluckSec.knobs   = { &addKnob ("pluckDecay", "Decay"), &addKnob ("pluckDamp", "Damp"),
                         &addKnob ("pluckDispersion", "Disp"), &addKnob ("pluckBurst", "Burst", 1) };

    drive.title   = "Overdrive";
    drive.cols    = 3;
    drive.span    = 2;
    drive.toggles = { &addToggle ("driveOn", "ON") };
    drive.combos  = { &addCombo ("driveType",
                       { "Soft", "Cubic", "Hard Clip", "Tube", "Diode", "Fuzz", "Rectify",
                         "Wavefold", "Foldback" }) };
    drive.knobs   = { &addKnob ("drive", "Drive", 2, KnobSize::Large), &addKnob ("bias", "Bias"),
                      &addKnob ("gain", "Gain"), &addKnob ("crushBits", "Crush"),
                      &addKnob ("downsample", "Downsmpl") };

    filter.title  = "Filter 1";
    filter.cols   = 5;
    filter.span   = 3;
    filter.combos = { &addCombo ("filterType",
                        { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" }) };
    filter.knobs  = { &addKnob ("cutoff", "Cutoff", 2, KnobSize::Large),
                      &addKnob ("resonance", "Reso", 2, KnobSize::Large),
                      &addKnob ("filterMorph", "Morph"), &addKnob ("keyTrack", "Key Trk"),
                      &addKnob ("filterEnvAmount", "Env Amt") };

    filter2.title  = "Filter 2";
    filter2.cols   = 5;
    filter2.span   = 3;
    filter2.combos = { &addCombo ("filter2Type",
                        { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" }) };
    filter2.knobs  = { &addKnob ("filter2Cutoff", "Cutoff", 2, KnobSize::Large),
                       &addKnob ("filter2Res", "Reso", 2, KnobSize::Large),
                       &addKnob ("filter2Morph", "Morph"), &addKnob ("filter2EnvAmount", "Env Amt") };

    // Each filter envelope sits directly beneath the filter it drives.
    filterEnv.title = "Filter 1 Env";
    filterEnv.cols  = 5;
    filterEnv.span  = 3;
    filterEnv.knobs = { &addKnob ("filterEnvA", "Atk"), &addKnob ("filterEnvD", "Dec"),
                        &addKnob ("filterEnvS", "Sus"), &addKnob ("filterEnvR", "Rel"),
                        &addKnob ("filterVelSens", "Vel") };

    filter2Env.title = "Filter 2 Env";
    filter2Env.cols  = 5;
    filter2Env.span  = 3;
    filter2Env.knobs = { &addKnob ("filter2EnvA", "Atk"), &addKnob ("filter2EnvD", "Dec"),
                         &addKnob ("filter2EnvS", "Sus"), &addKnob ("filter2EnvR", "Rel") };

    // ------------------------------------------------------------------ MOD --
    // The amp envelope lives in the performance strip, not on a page.
    envelope.knobs = { &addKnob ("attack", "Atk", 2, KnobSize::Small),
                       &addKnob ("decay", "Dec", 2, KnobSize::Small),
                       &addKnob ("sustain", "Sus", 2, KnobSize::Small),
                       &addKnob ("release", "Rel", 2, KnobSize::Small),
                       &addKnob ("ampVelSens", "Vel", 2, KnobSize::Small) };

    modEnv.title = "Mod Env";
    modEnv.cols  = 4;
    modEnv.span  = 2;
    modEnv.knobs = { &addKnob ("modEnvA", "Atk"), &addKnob ("modEnvD", "Dec"),
                     &addKnob ("modEnvS", "Sus"), &addKnob ("modEnvR", "Rel") };

    // --- CZ multi-stage envelope (8 rate + 8 level, aligned in rows) ---
    multiEnvSec.title = "Multi-Stage Env (CZ)  ->  filter";
    multiEnvSec.cols  = 10;
    multiEnvSec.span  = 6;
    for (int i = 1; i <= 8; ++i)
        multiEnvSec.knobs.push_back (&addKnob ("czRate" + juce::String (i), "R" + juce::String (i)));
    for (int i = 1; i <= 8; ++i)
        multiEnvSec.knobs.push_back (&addKnob ("czLevel" + juce::String (i), "L" + juce::String (i)));
    multiEnvSec.knobs.push_back (&addKnob ("czAmount", "Amt"));
    multiEnvSec.knobs.push_back (&addKnob ("czSustain", "Sus", 0));

    // --- CZ pitch (DCO) envelope (8 rate + 8 level, aligned in rows) ---
    pitchEnvSec.title = "Pitch Env (CZ)  ->  pitch";
    pitchEnvSec.cols  = 10;
    pitchEnvSec.span  = 6;
    for (int i = 1; i <= 8; ++i)
        pitchEnvSec.knobs.push_back (&addKnob ("pitchEnvRate" + juce::String (i), "R" + juce::String (i)));
    for (int i = 1; i <= 8; ++i)
        pitchEnvSec.knobs.push_back (&addKnob ("pitchEnvLevel" + juce::String (i), "L" + juce::String (i)));
    pitchEnvSec.knobs.push_back (&addKnob ("pitchEnvAmount", "Amt", 0));
    pitchEnvSec.knobs.push_back (&addKnob ("pitchEnvSustain", "Sus", 0));

    // --- CZ DCW (wave-depth) envelope ---
    dcwEnvSec.title = "DCW Env (CZ)  ->  PD amount";
    dcwEnvSec.cols  = 10;
    dcwEnvSec.span  = 6;
    for (int i = 1; i <= 8; ++i)
        dcwEnvSec.knobs.push_back (&addKnob ("dcwEnvRate" + juce::String (i), "R" + juce::String (i)));
    for (int i = 1; i <= 8; ++i)
        dcwEnvSec.knobs.push_back (&addKnob ("dcwEnvLevel" + juce::String (i), "L" + juce::String (i)));
    dcwEnvSec.knobs.push_back (&addKnob ("dcwEnvAmount", "Amt", 1));
    dcwEnvSec.knobs.push_back (&addKnob ("dcwEnvSustain", "Sus", 0));

    // --- LFOs ---
    lfo.title  = "LFO 1";
    lfo.cols   = 3;
    lfo.span   = 2;
    lfo.toggles = { &addToggle ("lfoRetrig", "RTRG") };
    lfo.combos = { &addCombo ("lfoWave", kLfoWaveNames), &addCombo ("lfoSync", kSyncNames) };
    lfo.knobs  = { &addKnob ("lfoRate", "Rate"), &addKnob ("lfoFade", "Fade"),
                   &addKnob ("lfoPhase", "Phase") };

    lfo2.title  = "LFO 2";
    lfo2.cols   = 3;
    lfo2.span   = 2;
    lfo2.toggles = { &addToggle ("lfo2Retrig", "RTRG") };
    lfo2.combos = { &addCombo ("lfo2Wave", kLfoWaveNames), &addCombo ("lfo2Sync", kSyncNames) };
    lfo2.knobs  = { &addKnob ("lfo2Rate", "Rate"), &addKnob ("lfo2Fade", "Fade"),
                    &addKnob ("lfo2Phase", "Phase") };

    // --- Vibrato (Casio CZ-style) ---
    vibratoSec.title   = "Vibrato";
    vibratoSec.cols    = 3;
    vibratoSec.span    = 2;
    vibratoSec.toggles = { &addToggle ("vibratoOn", "ON") };
    vibratoSec.combos  = { &addCombo ("vibratoWave", { "Triangle", "Square", "Ramp Up", "Ramp Down" }) };
    vibratoSec.knobs   = { &addKnob ("vibratoRate", "Rate"), &addKnob ("vibratoDepth", "Depth", 0),
                           &addKnob ("vibratoDelay", "Delay") };

    // --- Arpeggiator ---
    arpSec.title   = "Arpeggiator";
    arpSec.cols    = 2;
    arpSec.span    = 2;
    arpSec.toggles = { &addToggle ("arpOn", "ON"), &addToggle ("arpLatch", "LATCH") };
    arpSec.combos  = { &addCombo ("arpMode", { "Up", "Down", "Up-Down", "Random", "As Played" }),
                       &addCombo ("arpRate", { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4.", "1/8.", "1/4T", "1/8T" }) };
    arpSec.knobs   = { &addKnob ("arpOctaves", "Oct", 0), &addKnob ("arpGate", "Gate") };

    // ------------------------------------------------------------------ OUT --
    chorusSec.title   = "Chorus";
    chorusSec.cols    = 3;
    chorusSec.span    = 2;
    chorusSec.toggles = { &addToggle ("chorusOn", "ON") };
    chorusSec.combos  = { &addCombo ("chorusMode", { "Mode I", "Mode II", "Mode I+II" }) };
    chorusSec.knobs   = { &addKnob ("chorusRate", "Rate"), &addKnob ("chorusDepth", "Depth"),
                          &addKnob ("chorusMix", "Mix") };

    delaySec.title   = "Delay";
    delaySec.cols    = 3;
    delaySec.span    = 2;
    delaySec.toggles = { &addToggle ("delayOn", "ON") };
    delaySec.combos  = { &addCombo ("delayMode", { "Mono", "Stereo", "Ping-Pong" }),
                         &addCombo ("delaySyncL", kSyncNames), &addCombo ("delaySyncR", kSyncNames) };
    delaySec.knobs   = { &addKnob ("delayTimeL", "Time L"), &addKnob ("delayTimeR", "Time R"),
                         &addKnob ("delayFeedback", "Fbk"), &addKnob ("delayMix", "Mix"),
                         &addKnob ("delayDuck", "Duck") };

    reverbSec.title   = "Reverb";
    reverbSec.cols    = 4;
    reverbSec.span    = 2;
    reverbSec.toggles = { &addToggle ("reverbOn", "ON") };
    reverbSec.knobs   = { &addKnob ("reverbSize", "Size"), &addKnob ("reverbDamp", "Damp"),
                          &addKnob ("reverbWidth", "Width"), &addKnob ("reverbMix", "Mix") };

    comp.title   = "Compressor";
    comp.cols    = 3;
    comp.span    = 2;
    comp.toggles = { &addToggle ("compOn", "ON") };
    comp.knobs   = { &addKnob ("compThreshold", "Thr"), &addKnob ("compRatio", "Ratio"),
                     &addKnob ("compAttack", "Atk"), &addKnob ("compRelease", "Rel"),
                     &addKnob ("compMakeup", "Gain") };

    globalEqSec.title   = "Global EQ";
    globalEqSec.cols    = 4;
    globalEqSec.span    = 2;
    globalEqSec.toggles = { &addToggle ("globalEqOn", "ON") };
    globalEqSec.knobs = { &addKnob ("geLowFreq", "Lo Hz", 0),  &addKnob ("geLowGain", "Lo dB", 1),
                          &addKnob ("geMid1Freq", "M1 Hz", 0), &addKnob ("geMid1Gain", "M1 dB", 1),
                          &addKnob ("geMid2Freq", "M2 Hz", 0), &addKnob ("geMid2Gain", "M2 dB", 1),
                          &addKnob ("geHighFreq", "Hi Hz", 0), &addKnob ("geHighGain", "Hi dB", 1) };

    stereo.title = "Stereo";
    stereo.cols  = 2;
    stereo.span  = 2;
    stereo.knobs = { &addKnob ("pan", "Pan"), &addKnob ("panSpread", "Spread") };

    // --------------------------------------------------------------- GLOBAL --
    // Voice allocation and tuning are not part of the audio path, so they sit
    // off it entirely rather than interrupting the chain the way the old
    // "Voice" tab did. (voiceMode lives in the performance strip.)
    voiceSec.title = "Voice Allocation & Tuning";
    voiceSec.cols  = 4;
    voiceSec.span  = 4;
    voiceSec.toggles = { &addToggle ("monoRetrigger", "RETRIG") };
    voiceSec.combos = { &addCombo ("notePriority", { "Priority: Last", "Priority: Top", "Priority: Bottom" }),
                        &addCombo ("stealPolicy", { "Steal Oldest", "Steal Quietest" }),
                        &addCombo ("velCurve", { "Vel Linear", "Vel Soft", "Vel Hard", "Vel Fixed" }),
                        &addCombo ("tuningScale", { "Equal Temp.", "Just Intonation", "Pythagorean" }) };
    voiceSec.knobs = { &addKnob ("polyphony", "Poly", 0), &addKnob ("pitchBendRange", "Bend", 0),
                       &addKnob ("masterTune", "Tune", 1), &addKnob ("transpose", "Transp", 0) };

    // Global LFO + oversampling. The global LFO is a modulation-matrix source
    // ("Global LFO") that previously had no control at all in the editor.
    globalLfoSec.title  = "Global LFO / Quality";
    globalLfoSec.cols   = 1;
    globalLfoSec.span   = 2;
    globalLfoSec.combos = { &addCombo ("globalLfoWave", kLfoWaveNames),
                            &addCombo ("osQuality", { "OS 1x", "OS 2x", "OS 4x", "OS 8x" }) };
    globalLfoSec.knobs  = { &addKnob ("globalLfoRate", "Rate") };
}

//==============================================================================
//  Performance strip — the controls reached on every patch, pinned above the
//  tabs so they never leave the screen. Two of them (Macro 1 / Macro 2) had no
//  control anywhere in the editor before this.
//==============================================================================
void PDHybridEditor::buildStrip()
{
    stripKnobs = { &addKnob ("cutoff", "Cutoff", 2, KnobSize::Large),
                   &addKnob ("resonance", "Reso", 2, KnobSize::Large),
                   &addKnob ("macro1", "Macro 1", 2, KnobSize::Large),
                   &addKnob ("macro2", "Macro 2", 2, KnobSize::Large),
                   envelope.knobs[0], envelope.knobs[1], envelope.knobs[2],
                   envelope.knobs[3], envelope.knobs[4],
                   &addKnob ("masterLevel", "Master", 1, KnobSize::Large) };

    for (auto* k : stripKnobs)
    {
        strip.addAndMakeVisible (k->slider);
        strip.addAndMakeVisible (k->label);
    }

    stripPoly = &addCombo ("voiceMode", { "Poly", "Mono", "Legato", "Unison Legato" });
    strip.addAndMakeVisible (*stripPoly);
    stripLimiter = &addToggle ("masterLimiter", "LIMITER");
    strip.addAndMakeVisible (*stripLimiter);

    strip.addAndMakeVisible (scope_);
    strip.onResized = [this] { layoutStrip(); };
    strip.onPaint   = [this] (juce::Graphics& g)
    {
        auto b = strip.getLocalBounds();
        g.setColour (kCardEdge);
        g.drawRect (b, 1);

        // Group captions sit in the reserved band along the top edge, each in a
        // black notch that breaks the frame the way the card titles do.
        g.setFont (monoFont (9.0f));
        for (const auto& grp : stripGroups_)
        {
            const int tw = g.getCurrentFont().getStringWidth (grp.first) + 10;
            g.setColour (kCardBg);
            g.fillRect (grp.second.getX() + 2, b.getY() - 1, tw, kStripCapH);
            g.setColour (kTitleCol);
            g.drawText (grp.first, grp.second.getX() + 6, b.getY() - 1, tw, kStripCapH,
                        juce::Justification::centredLeft);
        }
        g.setColour (kCardEdge);
        for (int x : stripDividers_)
            g.fillRect (x, b.getY() + kStripCapH, 1, b.getHeight() - kStripCapH - 8);
    };
    addAndMakeVisible (strip);
}

void PDHybridEditor::layoutStrip()
{
    stripDividers_.clear();
    stripGroups_.clear();

    auto r = strip.getLocalBounds();
    r.removeFromTop (kStripCapH);          // band reserved for the group captions
    r = r.reduced (10, 0).withTrimmedBottom (8);

    // One knob cell: label above, rotary below (sized by its own size tag).
    auto placeKnob = [&] (LabeledKnob* k, juce::Rectangle<int> cell)
    {
        k->label.setBounds  (cell.removeFromTop (kLabelH));
        k->slider.setBounds (cell);
    };
    auto group = [&] (const juce::String& name, int width)
    {
        auto zone = r.removeFromLeft (width);
        stripGroups_.emplace_back (name, zone);
        return zone;
    };
    auto divider = [&] { r.removeFromLeft (5); stripDividers_.push_back (r.removeFromLeft (1).getX()); r.removeFromLeft (5); };

    {   // Filter — duplicated from the Shape page on purpose: these two are
        // reached constantly, so they are the one thing worth showing twice.
        auto z = group ("FILTER", 176);
        placeKnob (stripKnobs[0], z.removeFromLeft (88));
        placeKnob (stripKnobs[1], z.removeFromLeft (88));
    }
    divider();
    {
        auto z = group ("MACROS", 176);
        placeKnob (stripKnobs[2], z.removeFromLeft (88));
        placeKnob (stripKnobs[3], z.removeFromLeft (88));
    }
    divider();
    {
        auto z = group ("AMP ENV", 320);
        const int cw = z.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
            placeKnob (stripKnobs[4 + i], z.removeFromLeft (cw));
    }
    divider();

    {   // Master cluster is pinned to the right; the scope takes what is left.
        auto z = r.removeFromRight (92);
        stripGroups_.emplace_back ("MASTER", z);
        placeKnob (stripKnobs[9], z);

        auto ctl = r.removeFromRight (112);
        r.removeFromRight (6);
        stripPoly->setBounds    (ctl.removeFromTop (kComboRowH).reduced (0, 2));
        ctl.removeFromTop (4);
        stripLimiter->setBounds (ctl.removeFromTop (20));

        r.removeFromRight (6);
        scope_.setBounds (r);
    }
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

    // Preset browser: a button that opens a hierarchical menu (top-level presets
    // plus a submenu per category folder), </> to step, Save to store.
    addAndMakeVisible (presetButton);
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (prevButton);
    prevButton.onClick = [this] { proc.getPresetManager().loadByOffset (-1); refreshPresetList(); };
    addAndMakeVisible (nextButton);
    nextButton.onClick = [this] { proc.getPresetManager().loadByOffset (1); refreshPresetList(); };
    addAndMakeVisible (saveButton);
    saveButton.onClick = [this] { showSavePresetDialog(); };
    addAndMakeVisible (deleteButton);
    deleteButton.onClick = [this]
    {
        const auto name = proc.getPresetManager().getCurrentPresetName();
        if (name.isNotEmpty())
        {
            proc.getPresetManager().deletePreset (name);
            refreshPresetList();
        }
    };
    addAndMakeVisible (panicButton);
    panicButton.onClick = [this] { proc.triggerPanic(); };
    addAndMakeVisible (randButton);
    randButton.onClick = [this] { randomizePatch(); };

    // A/B compare: two snapshots; the button stashes the current state into the
    // active slot and loads the other.
    abState_[0] = proc.apvts.copyState();
    abState_[1] = proc.apvts.copyState();
    addAndMakeVisible (abButton);
    abButton.onClick = [this]
    {
        abState_[abSlot_] = proc.apvts.copyState();
        abSlot_ ^= 1;
        if (abState_[abSlot_].isValid())
            proc.apvts.replaceState (abState_[abSlot_].createCopy());
        abButton.setButtonText (abSlot_ == 0 ? "A/B: A" : "A/B: B");
    };

    // CRT effect toggle (persisted as a non-automatable property on the state
    // tree, so it survives reopening the editor without being a synth param).
    addAndMakeVisible (crtButton);
    crtButton.setClickingTogglesState (true);
    const bool crtOn = (bool) proc.apvts.state.getProperty ("crtEnabled", true);
    crtButton.setToggleState (crtOn, juce::dontSendNotification);
    crtOverlay.setEffectEnabled (crtOn);
    crtButton.onClick = [this]
    {
        const bool on = crtButton.getToggleState();
        crtOverlay.setEffectEnabled (on);
        proc.apvts.state.setProperty ("crtEnabled", on, nullptr);
    };

    refreshPresetList();

    buildSections();
    buildStrip();

    // Keep the shared timbre controls in sync with each slot's engine type.
    // Listening to the parameters (rather than the state tree) means this keeps
    // working after a preset load / A-B compare replaces the tree, and it doesn't
    // steal the combo's onChange, which the ComboBoxAttachment owns.
    proc.apvts.addParameterListener ("oscAType", this);
    proc.apvts.addParameterListener ("oscBType", this);
    updateOscControls();

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

        modCurveBox[i].addItemList ({ "Lin", "Exp", "S" }, 1);
        matrixHolder.addAndMakeVisible (modCurveBox[i]);
        modCurveAtt[i] = std::make_unique<ComboBoxAttachment> (proc.apvts, "mod" + s + "Curve", modCurveBox[i]);
    }
    matrixHolder.onResized = [this] { layoutMatrix(); };

    // --- Assemble tabs ---
    tabs.setTabBarDepth (30);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, kBg);
    tabs.setColour (juce::TabbedComponent::outlineColourId, kCardEdge);
    tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::tabTextColourId, kLabelCol);
    tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontTextColourId, kAccent);
    addAndMakeVisible (tabs);

    struct Page { juce::String name; std::vector<Section*> secs; juce::Component* trailing; juce::String trailingTitle; int trailingH; };
    const int matrixH = kHeaderH + (kNumModRows / 2) * kMatrixRowH + kCardPad * 2;

    // Tabs follow the signal path rather than naming categories of component:
    // where a control sits now tells you when it happens. Global settings sit
    // off the path entirely.
    std::vector<Page> layout {
        { "1 - Voice",  { &oscA, &oscB, &mixer, &bassSec, &unison, &glideSec }, nullptr, {}, 0 },
        { "2 - Shape",  { &pluckSec, &drive, &routingSec,
                          &filter, &filter2, &filterEnv, &filter2Env },         nullptr, {}, 0 },
        { "3 - Mod",    { &pitchEnvSec, &dcwEnvSec, &multiEnvSec,
                          &modEnv, &lfo, &lfo2, &vibratoSec, &arpSec }, &matrixHolder,
          "Modulation Matrix   (Source -> Destination x Depth)", matrixH },
        { "4 - Out",    { &chorusSec, &delaySec, &reverbSec,
                          &comp, &globalEqSec, &stereo },                       nullptr, {}, 0 },
        { "Global",     { &voiceSec, &globalLfoSec },                           nullptr, {}, 0 },
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

    // Added last so it sits on top of the tabs; it never intercepts the mouse.
    addAndMakeVisible (crtOverlay);

    setResizable (true, true);
    setResizeLimits (980, 560, 2400, 1600);
    setSize (1280, 900);
}

PDHybridEditor::~PDHybridEditor()
{
    cancelPendingUpdate();
    proc.apvts.removeParameterListener ("oscAType", this);
    proc.apvts.removeParameterListener ("oscBType", this);
    tabs.clearTabs();     // release content components before members are destroyed
    setLookAndFeel (nullptr);
}

void PDHybridEditor::parameterChanged (const juce::String&, float)
{
    // May be called on the audio thread (automation); update the UI on the
    // message thread.
    triggerAsyncUpdate();
}

void PDHybridEditor::handleAsyncUpdate()
{
    updateOscControls();
}

void PDHybridEditor::updateOscControls()
{
    auto comboActive = [] (juce::ComboBox* c, bool on)
    {
        c->setEnabled (on);
        c->setAlpha (on ? 1.0f : 0.4f);
    };
    auto knobRole = [] (LabeledKnob* k, const juce::String& label, bool on)
    {
        k->label.setText (label, juce::dontSendNotification);
        k->slider.setEnabled (on);
        k->slider.setAlpha (on ? 1.0f : 0.4f);
        k->label.setAlpha  (on ? 1.0f : 0.4f);
    };
    auto apply = [&] (Section& sec, const char* typeParam)
    {
        const int type = juce::roundToInt (proc.apvts.getRawParameterValue (typeParam)->load());
        const auto roles = oscKnobRoles (type);
        // Combos are Type / PD Wave / PD Wave 2 / Excite. The two wave choices and
        // the Combine toggle feed only the PD engine; Excite only the Scanned one.
        comboActive (sec.combos[1], type == 0);
        comboActive (sec.combos[2], type == 0);
        comboActive (sec.combos[3], roles.exciteActive);
        for (auto* t : sec.toggles)
        {
            t->setEnabled (type == 0);
            t->setAlpha (type == 0 ? 1.0f : 0.4f);
        }
        // The three shared timbre knobs relabel to the active engine's role and
        // grey out when it doesn't use them.
        knobRole (sec.knobs[0], roles.amountLabel, roles.amountActive);
        knobRole (sec.knobs[1], roles.pwLabel,     roles.pwActive);
        knobRole (sec.knobs[2], roles.engineLabel, roles.engineActive);
    };
    apply (oscA, "oscAType");
    apply (oscB, "oscBType");
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
            modSrcBox[idx].setBounds  (cell.removeFromLeft (92));
            cell.removeFromLeft (4);
            modDestBox[idx].setBounds (cell.removeFromLeft (92));
            cell.removeFromLeft (4);
            modCurveBox[idx].setBounds (cell.removeFromLeft (64));
            cell.removeFromLeft (6);
            modDepthSlider[idx].setBounds (cell);
        }
    }
}

void PDHybridEditor::refreshPresetList()
{
    // The menu is built on demand; here we just reflect the current preset on the
    // button, showing only the leaf name (the folder is implied by the menu).
    const auto cur = proc.getPresetManager().getCurrentPresetName();
    const auto leaf = cur.contains ("/") ? cur.fromLastOccurrenceOf ("/", false, false) : cur;
    presetButton.setButtonText (leaf.isEmpty() ? "Presets" : leaf);
}

void PDHybridEditor::showPresetMenu()
{
    const auto tree = proc.getPresetManager().getPresetTree();
    const auto current = proc.getPresetManager().getCurrentPresetName();

    juce::PopupMenu menu;
    auto paths = std::make_shared<juce::StringArray>();   // menu id (1-based) -> path

    auto addPreset = [&] (juce::PopupMenu& m, const juce::String& label, const juce::String& path)
    {
        paths->add (path);
        m.addItem (paths->size(), label, true, path == current);
    };

    for (const auto& name : tree.root)
        addPreset (menu, name, name);

    if (! tree.root.isEmpty() && ! tree.folders.empty())
        menu.addSeparator();

    for (const auto& folder : tree.folders)
    {
        juce::PopupMenu sub;
        for (const auto& p : folder.presets)
            addPreset (sub, p, folder.name + "/" + p);
        menu.addSubMenu (folder.name, sub);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [this, paths] (int result)
                        {
                            if (result >= 1 && result <= paths->size())
                            {
                                proc.getPresetManager().loadPreset ((*paths)[result - 1]);
                                refreshPresetList();
                            }
                        });
}

void PDHybridEditor::randomizePatch()
{
    // Curated set of timbre/character params. Audibility-critical params
    // (osc/master levels, amp envelope, polyphony, FX on/off) are left alone so
    // a random patch always plays.
    static const char* ids[] = {
        "oscAWave", "oscAWave2", "oscACombine", "oscAAmount", "oscAPulseWidth", "oscASemi",
        "oscBType", "oscBWave", "oscBAmount", "oscBSemi", "oscBLevel",
        "ringMod", "oscCrossMod", "crossModAmount", "noiseMod",
        "filterType", "resonance", "filterMorph", "filterEnvAmount", "keyTrack",
        "filterEnvD", "filterEnvS",
        "driveType", "drive", "bias",
        "decay", "czAmount", "pitchEnvAmount",
        "lfoWave", "lfoRate", "lfo2Wave", "lfo2Rate",
        "unisonDetune", "drift", "panSpread"
    };

    juce::Random rng;
    for (const char* id : ids)
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (rng.nextFloat());

    // Cutoff: keep it out of the muddy bottom so the patch stays bright enough.
    if (auto* c = proc.apvts.getParameter ("cutoff"))
        c->setValueNotifyingHost (0.4f + 0.6f * rng.nextFloat());
}

void PDHybridEditor::showSavePresetDialog()
{
    auto* aw = new juce::AlertWindow ("Save Preset", "Preset name (use Folder/Name for a category):",
                                      juce::MessageBoxIconType::NoIcon);
    // Pre-fill with the leaf name so re-saving a factory patch stores a user copy
    // at the top level rather than overwriting the factory file in its folder.
    const auto cur = proc.getPresetManager().getCurrentPresetName();
    aw->addTextEditor ("name", cur.contains ("/") ? cur.fromLastOccurrenceOf ("/", false, false) : cur);
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
        {
            const auto name = aw->getTextEditorContents ("name").trim();
            if (name.isNotEmpty())
            {
                proc.getPresetManager().savePreset (name);
                refreshPresetList();
            }
        }
    }), true);
}

void PDHybridEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kTopBar);
    const int y = (kTopBar - 26) / 2;

    int x = top.getRight() - 76;
    initButton.setBounds (x, y, 64, 26);
    x -= 58;  randButton.setBounds (x, y, 52, 26);
    x -= 62;  panicButton.setBounds (x, y, 56, 26);
    x -= 70;  saveButton.setBounds (x, y, 64, 26);
    x -= 46;  deleteButton.setBounds (x, y, 40, 26);
    x -= 32;  nextButton.setBounds (x, y, 28, 26);
    x -= 32;  prevButton.setBounds (x, y, 28, 26);
    x -= 52;  crtButton.setBounds  (x, y, 46, 26);
    x -= 58;  abButton.setBounds   (x, y, 52, 26);
    x -= 190; presetButton.setBounds (x, y, 184, 26);

    strip.setBounds (r.removeFromTop (kStripH).reduced (kMargin, 4));
    tabs.setBounds (r);
    crtOverlay.setBounds (getLocalBounds());
}

void PDHybridEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    auto top = getLocalBounds().removeFromTop (kTopBar);
    g.setColour (kTitleCol);
    g.setFont (monoFont (18.0f));
    g.drawText ("  PD_HYBRID", top, juce::Justification::centredLeft);
    g.setColour (kLabelCol);
    g.setFont (monoFont (11.0f));
    g.drawText ("v7", top.withTrimmedLeft (150), juce::Justification::centredLeft);
    g.setColour (kCardEdge);
    g.fillRect (0, kTopBar - 1, getWidth(), 1);
}
