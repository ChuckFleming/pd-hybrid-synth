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
constexpr int kStripH    = 142;  // fixed performance strip between title bar and tabs
constexpr int kFooterH   = 24;   // page indicator / route count / panic + rand
constexpr int kDeletePresetId = 9000;   // menu id, kept clear of the preset ids
constexpr int kStripCurveH = 56; // amp-envelope curve, inline beside its knobs
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
const juce::Colour kModCol   (0xffe8a54b);   // amber: "modulation lands here"

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

    // Group consecutive same-stackId sections into one unit; everything else is
    // a unit of one. A unit occupies `span` columns of the fixed grid.
    struct Unit { std::vector<int> members; int span; };
    std::vector<Unit> units;
    for (std::size_t i = 0; i < sections.size(); )
    {
        Unit u; u.span = spanOf (sections[i]);
        if (sections[i].stackId != 0)
        {
            const int id = sections[i].stackId;
            while (i < sections.size() && sections[i].stackId == id)
            {
                u.members.push_back (static_cast<int> (i));
                u.span = juce::jmax (u.span, spanOf (sections[i]));
                ++i;
            }
        }
        else
        {
            u.members.push_back (static_cast<int> (i));
            ++i;
        }
        units.push_back (std::move (u));
    }

    // Pack units into rows of the fixed grid.
    std::vector<int> unitRow (units.size(), 0), unitCol (units.size(), 0);
    {
        int col = 0, row = 0;
        for (std::size_t u = 0; u < units.size(); ++u)
        {
            if (col > 0 && col + units[u].span > numCols) { ++row; col = 0; }
            unitRow[u] = row; unitCol[u] = col; col += units[u].span;
        }
    }
    const int numRows = units.empty() ? 0 : (unitRow.back() + 1);

    // A row shares one combo-zone depth and one height, so card bottoms line up.
    // Stacked units keep their own compact combo zones.
    std::vector<int> rowComboRows (juce::jmax (1, numRows), 0), rowHeight (juce::jmax (1, numRows), 0);
    for (std::size_t u = 0; u < units.size(); ++u)
        if (units[u].members.size() == 1)
            rowComboRows[unitRow[u]] = juce::jmax (rowComboRows[unitRow[u]],
                comboRowsFor (sections[units[u].members[0]], cardW (units[u].span)));

    for (std::size_t u = 0; u < units.size(); ++u)
    {
        const int cw = cardW (units[u].span);
        int h = 0;
        if (units[u].members.size() == 1)
            h = sectionHeight (sections[units[u].members[0]], rowComboRows[unitRow[u]]);
        else
            for (int idx : units[u].members)   // stacked: heights add, plus gaps
                h += sectionHeight (sections[(std::size_t) idx],
                                    comboRowsFor (sections[(std::size_t) idx], cw)) + kGap;
        rowHeight[unitRow[u]] = juce::jmax (rowHeight[unitRow[u]], h);
    }

    std::vector<int> rowY (juce::jmax (1, numRows), kMargin);
    { int yy = kMargin; for (int r = 0; r < numRows; ++r) { rowY[r] = yy; yy += rowHeight[r] + kGap; } }

    if (apply)
        for (std::size_t u = 0; u < units.size(); ++u)
        {
            const int r  = unitRow[u];
            const int cx = kMargin + unitCol[u] * colPitch;
            const int cw = cardW (units[u].span);

            if (units[u].members.size() == 1)
            {
                placeCard (sections[units[u].members[0]], { cx, rowY[r], cw, rowHeight[r] },
                           rowComboRows[r]);
            }
            else
            {
                // Split the row height evenly between the stacked members, so
                // the block's bottom lines up with its neighbours.
                const int n  = static_cast<int> (units[u].members.size());
                const int mh = (rowHeight[r] - (n - 1) * kGap) / juce::jmax (1, n);
                int yy = rowY[r];
                for (int idx : units[u].members)
                {
                    Section& s = sections[(std::size_t) idx];
                    placeCard (s, { cx, yy, cw, mh }, comboRowsFor (s, cw));
                    yy += mh + kGap;
                }
            }
        }

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
//  StageEnvelopePanel — the three CZ 8-stage envelopes as one draggable curve
//==============================================================================
namespace {
constexpr int kEnvSelH   = 32;    // stage-selector row (button + destination caption)
constexpr int kEnvGraphH = 118;   // curve area
// The 16 rate/level knobs use a compact cell: they are reference numbers under
// the graph, not the primary way to edit the envelope.
constexpr int kEnvKnobH  = 62;
constexpr float kNodeR   = 4.5f;  // breakpoint handle radius
}

PDHybridEditor::StageEnvelopePanel::StageEnvelopePanel() = default;
PDHybridEditor::StageEnvelopePanel::~StageEnvelopePanel() { stopTimer(); }

int PDHybridEditor::StageEnvelopePanel::preferredHeight()
{
    // selector + graph + a gap + two rows of knobs (R1..R8 then L1..L8 + Amt/Sus)
    return kEnvSelH + kEnvGraphH + kCardPad + 2 * kEnvKnobH;
}

void PDHybridEditor::StageEnvelopePanel::addBank (Bank b)
{
    const int index = static_cast<int> (banks.size());

    auto btn = std::make_unique<juce::TextButton> (b.name);
    btn->setClickingTogglesState (false);
    btn->setTooltip (b.name + "  " + b.dest);
    btn->onClick = [this, index] { selectBank (index); };
    addAndMakeVisible (*btn);
    bankButtons.push_back (std::move (btn));

    for (auto* k : b.knobs)
    {
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
    }

    banks.push_back (std::move (b));
}

void PDHybridEditor::StageEnvelopePanel::start()
{
    selectBank (0);
    startTimerHz (20);
}

void PDHybridEditor::StageEnvelopePanel::selectBank (int index)
{
    if (index < 0 || index >= static_cast<int> (banks.size()))
        return;

    active = index;
    for (std::size_t i = 0; i < banks.size(); ++i)
    {
        const bool on = (static_cast<int> (i) == active);
        bankButtons[i]->setToggleState (on, juce::dontSendNotification);
        for (auto* k : banks[i].knobs)
        {
            k->slider.setVisible (on);
            k->label.setVisible (on);
        }
    }
    resized();
    repaint();
}

double PDHybridEditor::StageEnvelopePanel::totalTime() const
{
    if (banks.empty()) return 1.0;
    double t = 0.0;
    for (auto* p : banks[(std::size_t) active].rate)
        if (p != nullptr) t += p->convertFrom0to1 (p->getValue());
    return juce::jmax (1.0e-4, t);
}

juce::Rectangle<int> PDHybridEditor::StageEnvelopePanel::graphArea() const
{
    return getLocalBounds().withTrimmedTop (kEnvSelH).withHeight (kEnvGraphH).reduced (2, 2);
}

juce::Point<float> PDHybridEditor::StageEnvelopePanel::nodePos (int i, double totalOverride) const
{
    const auto g = graphArea().toFloat().reduced (6.0f, 8.0f);
    const auto& bank = banks[(std::size_t) active];
    const double total = totalOverride > 0.0 ? totalOverride : totalTime();

    if (i < 0)   // the origin the envelope departs from
        return { g.getX(), g.getBottom() };

    double cum = 0.0;
    for (int s = 0; s <= i; ++s)
        if (bank.rate[s] != nullptr)
            cum += bank.rate[s]->convertFrom0to1 (bank.rate[s]->getValue());

    const float lv = bank.level[i] != nullptr
                       ? bank.level[i]->convertFrom0to1 (bank.level[i]->getValue()) : 0.0f;
    return { g.getX() + g.getWidth() * static_cast<float> (cum / total),
             g.getBottom() - g.getHeight() * juce::jlimit (0.0f, 1.0f, lv) };
}

int PDHybridEditor::StageEnvelopePanel::hitNode (juce::Point<float> p) const
{
    for (int i = 0; i < 8; ++i)
        if (nodePos (i).getDistanceFrom (p) <= kNodeR + 4.0f)
            return i;
    return -1;
}

void PDHybridEditor::StageEnvelopePanel::mouseMove (const juce::MouseEvent& e)
{
    const int h = graphArea().contains (e.getPosition()) ? hitNode (e.position) : -1;
    if (h != hoverNode) { hoverNode = h; repaint(); }
}

void PDHybridEditor::StageEnvelopePanel::mouseExit (const juce::MouseEvent&)
{
    if (hoverNode != -1) { hoverNode = -1; repaint(); }
}

void PDHybridEditor::StageEnvelopePanel::mouseDown (const juce::MouseEvent& e)
{
    if (! graphArea().contains (e.getPosition()))
        return;

    dragNode = hitNode (e.position);
    if (dragNode < 0)
        return;

    // Freeze the time base for the gesture, otherwise editing a rate rescales
    // the x axis under the cursor and the drag fights itself.
    dragTotal = totalTime();
    auto& bank = banks[(std::size_t) active];
    if (bank.rate[dragNode]  != nullptr) bank.rate[dragNode]->beginChangeGesture();
    if (bank.level[dragNode] != nullptr) bank.level[dragNode]->beginChangeGesture();
}

void PDHybridEditor::StageEnvelopePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragNode < 0) return;

    const auto g = graphArea().toFloat().reduced (6.0f, 8.0f);
    auto& bank = banks[(std::size_t) active];

    if (auto* lp = bank.level[dragNode])
    {
        const float lv = juce::jlimit (0.0f, 1.0f, (g.getBottom() - e.position.y) / g.getHeight());
        lp->setValueNotifyingHost (lp->convertTo0to1 (lv));
    }

    if (auto* rp = bank.rate[dragNode])
    {
        // x is cumulative time, so this stage's duration is the gap between the
        // cursor and the end of the previous stage.
        double before = 0.0;
        for (int s = 0; s < dragNode; ++s)
            if (bank.rate[s] != nullptr)
                before += bank.rate[s]->convertFrom0to1 (bank.rate[s]->getValue());

        const double wantCum = juce::jlimit (0.0f, 1.0f, (e.position.x - g.getX()) / g.getWidth()) * dragTotal;
        const auto range = rp->getNormalisableRange();
        const float t = juce::jlimit (range.start, range.end, static_cast<float> (wantCum - before));
        rp->setValueNotifyingHost (rp->convertTo0to1 (t));
    }

    repaint();
}

void PDHybridEditor::StageEnvelopePanel::mouseUp (const juce::MouseEvent&)
{
    if (dragNode < 0) return;
    auto& bank = banks[(std::size_t) active];
    if (bank.rate[dragNode]  != nullptr) bank.rate[dragNode]->endChangeGesture();
    if (bank.level[dragNode] != nullptr) bank.level[dragNode]->endChangeGesture();
    dragNode = -1;
}

void PDHybridEditor::StageEnvelopePanel::timerCallback()
{
    // Repaint only when something actually moved (automation, preset load, or a
    // knob being turned underneath the graph).
    if (banks.empty()) return;
    const auto& bank = banks[(std::size_t) active];

    std::vector<float> now;
    now.reserve (17);
    for (int i = 0; i < 8; ++i)
    {
        now.push_back (bank.rate[i]  != nullptr ? bank.rate[i]->getValue()  : 0.0f);
        now.push_back (bank.level[i] != nullptr ? bank.level[i]->getValue() : 0.0f);
    }
    now.push_back (bank.sustain != nullptr ? bank.sustain->getValue() : 0.0f);

    if (now != lastValues) { lastValues = now; repaint(); }
}

void PDHybridEditor::StageEnvelopePanel::resized()
{
    auto r = getLocalBounds();

    auto sel = r.removeFromTop (kEnvSelH);
    for (auto& b : bankButtons)
        b->setBounds (sel.removeFromLeft (96).withTrimmedBottom (12).reduced (1, 2));

    r.removeFromTop (kEnvGraphH + kCardPad);

    // Two knob rows for the active bank: R1..R8, then L1..L8 with Amt and Sus.
    if (banks.empty()) return;
    auto& knobs = banks[(std::size_t) active].knobs;
    const int cols  = 10;
    const int cellW = r.getWidth() / cols;

    for (std::size_t k = 0; k < knobs.size(); )
    {
        auto krow = r.removeFromTop (kEnvKnobH);
        for (int c = 0; c < cols && k < knobs.size(); ++c, ++k)
        {
            auto cell = krow.removeFromLeft (cellW);
            knobs[k]->label.setBounds  (cell.removeFromTop (kLabelH - 2));
            knobs[k]->slider.setBounds (cell);
        }
    }
}

void PDHybridEditor::StageEnvelopePanel::paint (juce::Graphics& g)
{
    if (banks.empty()) return;

    // Destination caption under each stage button, so the selector doubles as
    // documentation of what each envelope actually drives.
    g.setFont (monoFont (8.0f));
    for (std::size_t i = 0; i < banks.size(); ++i)
    {
        auto b = bankButtons[i]->getBounds();
        g.setColour (static_cast<int> (i) == active ? banks[i].colour : kLabelCol.withAlpha (0.55f));
        g.drawText (banks[i].dest, b.getX(), b.getBottom(), b.getWidth(), 12,
                    juce::Justification::centred);
    }
    g.setColour (kLabelCol.withAlpha (0.5f));
    g.drawText ("8 STAGES  -  DRAG A NODE: UP/DOWN = LEVEL, LEFT/RIGHT = RATE",
                juce::Rectangle<int> (static_cast<int> (banks.size()) * 96 + 12, 0,
                                      getWidth(), kEnvSelH),
                juce::Justification::centredLeft);

    const auto& bank  = banks[(std::size_t) active];
    const auto  frame = graphArea();
    const auto  area  = frame.toFloat().reduced (6.0f, 8.0f);
    const auto  col   = bank.colour;

    g.setColour (kCardEdge);
    g.drawRect (frame, 1);

    // Horizontal guides; bipolar banks also get a centre line at "no offset".
    g.setColour (juce::Colour (0xff0e2116));
    for (int i = 1; i < 4; ++i)
        g.fillRect (area.getX(), area.getY() + area.getHeight() * i / 4.0f, area.getWidth(), 1.0f);
    if (bank.bipolar)
    {
        g.setColour (col.withAlpha (0.35f));
        g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.0f);
    }

    // The curve, from the origin through all eight breakpoints.
    juce::Path curve;
    auto p = nodePos (-1);
    curve.startNewSubPath (p);
    for (int i = 0; i < 8; ++i)
        curve.lineTo (nodePos (i));

    auto fill = curve;
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(),     area.getBottom());
    fill.closeSubPath();
    g.setColour (col.withAlpha (0.11f));
    g.fillPath (fill);

    g.setColour (col);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // Sustain point: the stage the envelope holds on until note-off.
    const int susStage = bank.sustain != nullptr
        ? juce::roundToInt (bank.sustain->convertFrom0to1 (bank.sustain->getValue())) : 0;
    if (susStage >= 1 && susStage <= 8)
    {
        const float sx = nodePos (susStage - 1).x;
        g.setColour (col.withAlpha (0.55f));
        for (float y = frame.getY() + 2.0f; y < frame.getBottom() - 2.0f; y += 6.0f)
            g.fillRect (sx, y, 1.0f, 3.0f);
        g.setFont (monoFont (9.0f));
        g.drawText ("SUS", juce::Rectangle<float> (sx + 3.0f, (float) frame.getY() + 2.0f, 30.0f, 11.0f),
                    juce::Justification::centredLeft);
    }

    // Breakpoint handles; the hovered one reads out its rate and level.
    for (int i = 0; i < 8; ++i)
    {
        const auto np = nodePos (i);
        const bool lit = (i == dragNode || i == hoverNode);
        g.setColour (kCardBg);
        g.fillRect (np.x - kNodeR, np.y - kNodeR, kNodeR * 2.0f, kNodeR * 2.0f);
        g.setColour (lit ? juce::Colours::white.interpolatedWith (col, 0.6f) : col);
        g.drawRect (juce::Rectangle<float> (np.x - kNodeR, np.y - kNodeR, kNodeR * 2.0f, kNodeR * 2.0f), 1.4f);
    }

    if (hoverNode >= 0 && dragNode < 0)
    {
        const auto np = nodePos (hoverNode);
        const auto* rp = bank.rate[hoverNode];
        const auto* lp = bank.level[hoverNode];
        const juce::String txt = "R" + juce::String (hoverNode + 1) + " "
            + (rp != nullptr ? rp->getCurrentValueAsText() : juce::String())
            + "   L" + juce::String (hoverNode + 1) + " "
            + (lp != nullptr ? lp->getCurrentValueAsText() : juce::String());

        g.setFont (monoFont (9.5f));
        const int tw = g.getCurrentFont().getStringWidth (txt) + 12;
        auto box = juce::Rectangle<int> ((int) np.x + 8, (int) np.y - 9, tw, 17);
        if (box.getRight() > frame.getRight() - 2) box.setX ((int) np.x - 8 - tw);
        g.setColour (kCardBg);   g.fillRect (box);
        g.setColour (col.withAlpha (0.7f)); g.drawRect (box, 1);
        g.setColour (col);       g.drawText (txt, box.reduced (6, 0), juce::Justification::centredLeft);
    }

    // Stage numbers along the bottom edge of the graph.
    g.setFont (monoFont (8.5f));
    g.setColour (kLabelCol.withAlpha (0.7f));
    for (int i = 0; i < 8; ++i)
    {
        const float x0 = (i == 0 ? area.getX() : nodePos (i - 1).x);
        const float x1 = nodePos (i).x;
        if (x1 - x0 > 12.0f)
            g.drawText (juce::String (i + 1),
                        juce::Rectangle<float> (x0, (float) frame.getBottom() - 13.0f, x1 - x0, 11.0f),
                        juce::Justification::centred);
    }
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

    knob->paramId = paramId;
    knob->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, knob->slider);
    // Clicks also point the Inspector at this parameter (see mouseDown).
    knob->slider.addMouseListener (this, false);

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
    oscACycle.attach (proc.apvts, "oscA");
    oscA.title  = "Osc A";
    oscA.cols   = 6;
    oscA.span   = 3;
    oscA.custom = &oscACycle;
    oscA.customH = 56;
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

    oscBCycle.attach (proc.apvts, "oscB");
    oscB.title  = "Osc B";
    oscB.cols   = 6;
    oscB.span   = 3;
    oscB.custom = &oscBCycle;
    oscB.customH = 56;
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

    bassCurve.attach (proc.apvts, "bassAttack", "bassDecay", "bassSustain", "bassRelease");
    bassSec.title   = "Mono Bass";
    bassSec.cols    = 5;
    bassSec.span    = 4;
    bassSec.custom  = &bassCurve;
    bassSec.customH = 46;
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
    routingSec.title   = "Routing";
    routingSec.cols    = 1;
    routingSec.span    = 2;
    routingSec.combos  = { &addCombo ("filterRouting", { "Single Filter", "Filters Series", "Filters Parallel" }),
                           &addCombo ("drivePos", { "Drive Post Filter", "Drive Pre Filter" }),
                           &addCombo ("fxRouting", { "Delay -> Reverb", "Reverb -> Delay", "Reverb, Dry Delay" }) };
    // The diagram redraws whenever one of those three choices changes, which is
    // the whole point of collecting them here.
    routingDiagram.attach (proc.apvts);
    routingSec.custom  = &routingDiagram;
    routingSec.customH = 78;

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

    // Each filter envelope sits directly beneath the filter it drives, and shows
    // its shape rather than only four numbers.
    filt1Curve.attach (proc.apvts, "filterEnvA", "filterEnvD", "filterEnvS", "filterEnvR");
    filterEnv.title   = "Filter 1 Env";
    filterEnv.cols    = 5;
    filterEnv.span    = 3;
    filterEnv.custom  = &filt1Curve;
    filterEnv.customH = 62;
    filterEnv.knobs = { &addKnob ("filterEnvA", "Atk"), &addKnob ("filterEnvD", "Dec"),
                        &addKnob ("filterEnvS", "Sus"), &addKnob ("filterEnvR", "Rel"),
                        &addKnob ("filterVelSens", "Vel") };

    filt2Curve.attach (proc.apvts, "filter2EnvA", "filter2EnvD", "filter2EnvS", "filter2EnvR");
    filter2Env.title   = "Filter 2 Env";
    filter2Env.cols    = 5;
    filter2Env.span    = 3;
    filter2Env.custom  = &filt2Curve;
    filter2Env.customH = 62;
    filter2Env.knobs = { &addKnob ("filter2EnvA", "Atk"), &addKnob ("filter2EnvD", "Dec"),
                         &addKnob ("filter2EnvS", "Sus"), &addKnob ("filter2EnvR", "Rel") };

    // ------------------------------------------------------------------ MOD --
    // The amp envelope lives in the performance strip, not on a page.
    envelope.knobs = { &addKnob ("attack", "A", 2, KnobSize::Small),
                       &addKnob ("decay", "D", 2, KnobSize::Small),
                       &addKnob ("sustain", "S", 2, KnobSize::Small),
                       &addKnob ("release", "R", 2, KnobSize::Small) };

    modCurve.attach (proc.apvts, "modEnvA", "modEnvD", "modEnvS", "modEnvR");
    modEnv.title   = "Mod Env";
    modEnv.cols    = 4;
    modEnv.span    = 2;
    modEnv.custom  = &modCurve;
    modEnv.customH = 62;
    modEnv.knobs = { &addKnob ("modEnvA", "Atk"), &addKnob ("modEnvD", "Dec"),
                     &addKnob ("modEnvS", "Sus"), &addKnob ("modEnvR", "Rel") };

    // The three CZ 8-stage envelopes share one card with a stage selector and a
    // draggable curve; see buildStageEnvelopes().
    stageEnvSec.title   = "Multi-Stage Envelopes (CZ)";
    stageEnvSec.span    = 4;   // LFO 1 / LFO 2 stack beside it in the other two
    stageEnvSec.custom  = &stageEnv;
    stageEnvSec.customH = StageEnvelopePanel::preferredHeight();

    // --- LFOs: the card shows the real output traced over a dim shape guide ---
    lfo1Curve.attach (proc.apvts, "lfoWave", "lfoRate");
    lfo1Curve.setLiveReader ([this]
    {
        float lv[PDHybridAudioProcessor::kNumModSources] {};
        proc.readModLevels (lv, PDHybridAudioProcessor::kNumModSources);
        return lv[(int) pdhybrid::ModSource::Lfo];
    });
    lfo.title   = "LFO 1";
    lfo.cols    = 3;
    lfo.span    = 2;
    lfo.toggles = { &addToggle ("lfoRetrig", "RTRG") };
    lfo.combos  = { &addCombo ("lfoWave", kLfoWaveNames), &addCombo ("lfoSync", kSyncNames) };
    lfo.custom  = &lfo1Curve;
    lfo.customH = 62;
    lfo.stackId = 1;   // LFO 1 above LFO 2, beside the envelope card
    lfo.knobs   = { &addKnob ("lfoRate", "Rate"), &addKnob ("lfoFade", "Fade"),
                    &addKnob ("lfoPhase", "Phase") };

    lfo2Curve.attach (proc.apvts, "lfo2Wave", "lfo2Rate");
    lfo2Curve.setLiveReader ([this]
    {
        float lv[PDHybridAudioProcessor::kNumModSources] {};
        proc.readModLevels (lv, PDHybridAudioProcessor::kNumModSources);
        return lv[(int) pdhybrid::ModSource::Lfo2];
    });
    lfo2.title   = "LFO 2";
    lfo2.cols    = 3;
    lfo2.span    = 2;
    lfo2.toggles = { &addToggle ("lfo2Retrig", "RTRG") };
    lfo2.combos  = { &addCombo ("lfo2Wave", kLfoWaveNames), &addCombo ("lfo2Sync", kSyncNames) };
    lfo2.custom  = &lfo2Curve;
    lfo2.customH = 62;
    lfo2.stackId = 1;
    lfo2.knobs   = { &addKnob ("lfo2Rate", "Rate"), &addKnob ("lfo2Fade", "Fade"),
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
    voiceSec.title = "Voice Allocation";
    voiceSec.cols  = 3;
    voiceSec.span  = 3;
    voiceSec.toggles = { &addToggle ("monoRetrigger", "RETRIG") };
    voiceSec.combos = { &addCombo ("notePriority", { "Priority: Last", "Priority: Top", "Priority: Bottom" }),
                        &addCombo ("stealPolicy", { "Steal Oldest", "Steal Quietest" }),
                        &addCombo ("velCurve", { "Vel Linear", "Vel Soft", "Vel Hard", "Vel Fixed" }) };
    voiceSec.knobs = { &addKnob ("polyphony", "Poly", 0, KnobSize::Large),
                       &addKnob ("ampVelSens", "Vel Sens"),
                       &addKnob ("pitchBendRange", "Bend", 0) };

    tuningSec.title  = "Tuning";
    tuningSec.cols   = 3;
    tuningSec.span   = 3;
    tuningSec.combos = { &addCombo ("tuningScale", { "Equal Temperament", "Just Intonation", "Pythagorean" }) };
    tuningSec.knobs  = { &addKnob ("masterTune", "Tune", 1), &addKnob ("transpose", "Transpose", 0) };

    // The global LFO is a modulation-matrix source ("Global LFO") that had no
    // control at all in the editor before this rebuild.
    globalLfoSec.title  = "Global LFO";
    globalLfoSec.cols   = 1;
    globalLfoSec.span   = 4;
    globalLfoSec.combos = { &addCombo ("globalLfoWave", kLfoWaveNames) };
    globalLfoSec.knobs  = { &addKnob ("globalLfoRate", "Rate", 2, KnobSize::Large) };

    qualitySec.title  = "Quality";
    qualitySec.cols   = 1;
    qualitySec.span   = 2;
    qualitySec.combos = { &addCombo ("osQuality", { "Oversampling 1x", "Oversampling 2x",
                                                    "Oversampling 4x", "Oversampling 8x" }) };
}

//==============================================================================
//  Modulation Inspector
//==============================================================================
namespace {
constexpr int kInspW    = 236;   // permanent Inspector column width
constexpr int kInspRowH = 15;

// Which knob(s) a modulation destination lands on. Pitch and Amplitude have no
// single knob of their own, so they are absent — a route to them shows in the
// matrix summary but has nothing to ring.
struct DestParam { pdhybrid::ModDest dest; const char* paramId; };
const DestParam kDestParams[] = {
    { pdhybrid::ModDest::PdAmount,      "oscAAmount" },
    { pdhybrid::ModDest::PdAmount,      "oscBAmount" },
    { pdhybrid::ModDest::PulseWidth,    "oscAPulseWidth" },
    { pdhybrid::ModDest::PulseWidth,    "oscBPulseWidth" },
    { pdhybrid::ModDest::Cutoff,        "cutoff" },
    { pdhybrid::ModDest::Resonance,     "resonance" },
    { pdhybrid::ModDest::Morph,         "filterMorph" },
    { pdhybrid::ModDest::Drive,         "drive" },
    { pdhybrid::ModDest::Pan,           "pan" },
    { pdhybrid::ModDest::OscALevel,     "oscALevel" },
    { pdhybrid::ModDest::OscBLevel,     "oscBLevel" },
    { pdhybrid::ModDest::Detune,        "unisonDetune" },
    { pdhybrid::ModDest::Filter2Cutoff, "filter2Cutoff" },
    { pdhybrid::ModDest::LfoRate,       "lfoRate" },
    { pdhybrid::ModDest::Lfo2Rate,      "lfo2Rate" },
    { pdhybrid::ModDest::NoiseLevel,    "noiseLevel" },
    { pdhybrid::ModDest::DelayMix,      "delayMix" },
    { pdhybrid::ModDest::DelayFeedback, "delayFeedback" },
    { pdhybrid::ModDest::MasterPan,     "pan" },
    { pdhybrid::ModDest::GlobalEqGain,  "geHighGain" },
};

int destForParam (const juce::String& paramId)
{
    for (const auto& d : kDestParams)
        if (paramId == d.paramId)
            return static_cast<int> (d.dest);
    return 0;
}

// Sources the Inspector meters, in the order they are shown. Indices are
// ModSource values; the names must match kSrcNames.
struct MeterRow { int src; const char* name; bool trace; bool bipolar; };
const std::vector<MeterRow> kMeterRows {
    { (int) pdhybrid::ModSource::Lfo,        "LFO 1",   true,  true  },
    { (int) pdhybrid::ModSource::Lfo2,       "LFO 2",   true,  true  },
    { (int) pdhybrid::ModSource::GlobalLfo,  "GLB LFO", true,  true  },
    { (int) pdhybrid::ModSource::AmpEnv,     "AMP ENV", true,  false },
    { (int) pdhybrid::ModSource::ModEnv,     "MOD ENV", true,  false },
    { (int) pdhybrid::ModSource::FilterEnvA, "FLT ENV", true,  false },
    { (int) pdhybrid::ModSource::MultiEnv,   "MULTI",   true,  false },
    { (int) pdhybrid::ModSource::PitchEnv,   "PITCH",   true,  true  },
    { (int) pdhybrid::ModSource::Velocity,   "VEL",     false, false },
    { (int) pdhybrid::ModSource::ModWheel,   "MODWHL",  false, false },
    { (int) pdhybrid::ModSource::Macro1,     "MACRO 1", false, false },
    { (int) pdhybrid::ModSource::Macro2,     "MACRO 2", false, false },
};
}

void PDHybridEditor::selectParameter (const juce::String& paramId)
{
    if (paramId == selectedParam) return;
    selectedParam = paramId;
    if (auto* p = proc.apvts.getParameter (paramId))
        selectedName = p->getName (40);
    layoutInspector();
    inspector.repaint();
}

void PDHybridEditor::mouseDown (const juce::MouseEvent& e)
{
    // Knob sliders forward their clicks here (addMouseListener in addKnob), so a
    // click both grabs the knob and points the Inspector at it.
    for (auto& k : knobs)
        if (e.eventComponent == &k->slider)
        {
            showDestinations = false;
            selectParameter (k->paramId);
            layoutInspector();
            inspector.repaint();
            return;
        }
}

int PDHybridEditor::firstFreeMatrixSlot() const
{
    for (int i = 1; i <= kNumModRows; ++i)
    {
        const auto s = juce::String (i);
        const bool srcOff  = proc.apvts.getRawParameterValue ("mod" + s + "Source")->load() < 0.5f;
        const bool destOff = proc.apvts.getRawParameterValue ("mod" + s + "Dest")->load() < 0.5f;
        if (srcOff || destOff)
            return i;
    }
    return 0;
}

void PDHybridEditor::addRouteToSelected()
{
    const int dest = destForParam (selectedParam);
    const int slot = firstFreeMatrixSlot();
    if (dest == 0 || slot == 0)
        return;

    // Point a free slot at this knob and give it a usable default source, then
    // show the matrix so the choice can be changed.
    if (auto* d = proc.apvts.getParameter ("mod" + juce::String (slot) + "Dest"))
        d->setValueNotifyingHost (d->convertTo0to1 ((float) dest));
    if (auto* s = proc.apvts.getParameter ("mod" + juce::String (slot) + "Source"))
        if (s->getValue() <= 0.0f)
            s->setValueNotifyingHost (s->convertTo0to1 ((float) pdhybrid::ModSource::Lfo));
    // A new route starts at an audible depth, so the button does something you
    // can hear; the depth is then adjustable in the matrix or by dragging.
    if (auto* d = proc.apvts.getParameter ("mod" + juce::String (slot) + "Depth"))
        if (std::abs (d->convertFrom0to1 (d->getValue())) < 1.0e-4f)
            d->setValueNotifyingHost (d->convertTo0to1 (0.30f));

    tabs.setCurrentTabIndex (2);   // "3 - Mod", where the matrix lives
    refreshModRings();
    inspector.repaint();
}

void PDHybridEditor::refreshModRings()
{
    routes_.clear();
    for (int i = 1; i <= kNumModRows; ++i)
    {
        const auto s = juce::String (i);
        const int src  = juce::roundToInt (proc.apvts.getRawParameterValue ("mod" + s + "Source")->load());
        const int dst  = juce::roundToInt (proc.apvts.getRawParameterValue ("mod" + s + "Dest")->load());
        const float dp = proc.apvts.getRawParameterValue ("mod" + s + "Depth")->load();
        const int cv   = juce::roundToInt (proc.apvts.getRawParameterValue ("mod" + s + "Curve")->load());
        if (src > 0 && dst > 0)
            routes_.push_back ({ i, src, dst, dp, cv });
    }

    // Tag every knob that at least one route lands on; the look-and-feel draws
    // the amber ring from this property.
    for (auto& k : knobs)
    {
        const int d = destForParam (k->paramId);
        bool modded = false;
        if (d != 0)
            for (const auto& r : routes_)
                if (r.dest == d) { modded = true; break; }

        if ((bool) k->slider.getProperties().getWithDefault ("modded", false) != modded)
        {
            k->slider.getProperties().set ("modded", modded);
            k->slider.repaint();
        }
    }
}

void PDHybridEditor::timerCallback()
{
    refreshModRings();
    layoutInspector();   // the route list grows and shrinks, so re-place the buttons
    inspector.repaint();
    footer.repaint();    // page indicator and route count both change under us
}

void PDHybridEditor::layoutInspector()
{
    auto r = inspector.getLocalBounds().reduced (8, 6);
    r.removeFromTop (16);                       // "INSPECTOR" heading

    // Direction toggle: routes *into* the selected knob, or routes *out of* the
    // selected modulator.
    auto seg = r.removeFromTop (18);
    inspSourcesBtn.setBounds (seg.removeFromLeft (seg.getWidth() / 2));
    inspDestsBtn.setBounds (seg);
    inspSourcesBtn.setToggleState (! showDestinations, juce::dontSendNotification);
    inspDestsBtn.setToggleState   (showDestinations,   juce::dontSendNotification);
    r.removeFromTop (4);

    r.removeFromTop (kInspRowH + 6);            // selected name + value

    routeRows_.clear();
    const int dest = destForParam (selectedParam);
    for (std::size_t i = 0; i < routes_.size() && routeRows_.size() < 6; ++i)
    {
        const auto& rt = routes_[i];
        const bool match = showDestinations ? (rt.source == selectedSource)
                                            : (dest != 0 && rt.dest == dest);
        if (! match) continue;

        RouteRow row;
        row.routeIndex = static_cast<int> (i);
        row.text = r.removeFromTop (kInspRowH);
        row.bar  = r.removeFromTop (5);
        r.removeFromTop (4);
        routeRows_.push_back (row);
    }

    r.removeFromTop (4);
    inspAddRoute.setBounds (r.removeFromTop (20));
    inspAddRoute.setEnabled (! showDestinations && dest != 0 && firstFreeMatrixSlot() != 0);
    inspAddRoute.setVisible (! showDestinations);

    // --- bottom block: the live source meters ---------------------------
    auto bottom = inspector.getLocalBounds().reduced (8, 6);
    inspFullMatrix.setBounds (bottom.removeFromBottom (20));
    bottom.removeFromBottom (16);               // MATRIX count line

    const int want = sourceMeters.preferredHeight();
    auto zone = bottom.removeFromBottom (juce::jmin (bottom.getHeight(), want + 16));
    zone.removeFromTop (16);                    // "SOURCES" heading
    sourceMeters.setBounds (zone);

    // Mark which sources a route actually uses, and which one is being followed.
    std::vector<bool> used;
    used.reserve (kMeterRows.size());
    for (const auto& m : kMeterRows)
    {
        bool u = false;
        for (const auto& rt : routes_)
            if (rt.source == m.src) { u = true; break; }
        used.push_back (u);
    }
    sourceMeters.setUsage (std::move (used), showDestinations ? selectedSource : -1);
}

void PDHybridEditor::inspectorClicked (const juce::MouseEvent&)
{
    // Source rows are handled by SourceMeters itself (onRowClicked).
}

void PDHybridEditor::paintInspector (juce::Graphics& g)
{
    auto full = inspector.getLocalBounds();
    g.setColour (kCardEdge);
    g.drawRect (full, 1);

    auto r = full.reduced (8, 6);

    g.setFont (monoFont (9.0f));
    g.setColour (kLabelCol);
    g.drawText ("INSPECTOR", r.removeFromTop (16), juce::Justification::centredLeft);
    r.removeFromTop (18 + 4);                 // the direction toggle (buttons paint themselves)

    const int dest = destForParam (selectedParam);

    // --- what is selected ------------------------------------------------
    auto nameRow = r.removeFromTop (kInspRowH);
    g.setFont (monoFont (10.5f));
    if (showDestinations)
    {
        g.setColour (kModCol);
        g.drawText (kSrcNames[selectedSource], nameRow, juce::Justification::centredLeft);
        g.setFont (monoFont (8.5f));
        g.setColour (kLabelCol);
        g.drawText ("modulator", nameRow, juce::Justification::centredRight);
    }
    else
    {
        g.setColour (dest != 0 ? kModCol : kAccent);
        g.drawText (selectedParam.isEmpty() ? "Click a knob" : selectedName,
                    nameRow, juce::Justification::centredLeft);
        if (auto* p = proc.apvts.getParameter (selectedParam))
        {
            g.setFont (monoFont (9.0f));
            g.setColour (kLabelCol);
            g.drawText (p->getCurrentValueAsText(), nameRow, juce::Justification::centredRight);
        }
    }

    // --- the routes, in whichever direction is selected -------------------
    for (const auto& row : routeRows_)
    {
        const auto& rt = routes_[(std::size_t) row.routeIndex];

        g.setFont (monoFont (9.0f));
        g.setColour (kAccent);
        g.drawText (showDestinations ? kDstNames[rt.dest] : kSrcNames[rt.source],
                    row.text, juce::Justification::centredLeft);
        g.setColour (kLabelCol);
        g.drawText (juce::String (rt.depth, 2), row.text, juce::Justification::centredRight);

        // Bipolar bar: depth grows left or right of centre.
        g.setColour (juce::Colour (0xff0e2116));
        g.fillRect (row.bar);
        const float d = juce::jlimit (-1.0f, 1.0f, rt.depth);
        const int mid = row.bar.getCentreX();
        const int w   = juce::roundToInt (std::abs (d) * row.bar.getWidth() * 0.5f);
        g.setColour (kModCol);
        g.fillRect (d >= 0.0f ? mid : mid - w, row.bar.getY(), juce::jmax (1, w), row.bar.getHeight());
    }

    if (routeRows_.empty())
    {
        g.setFont (monoFont (9.0f));
        g.setColour (kLabelCol.withAlpha (0.65f));
        const juce::String msg = showDestinations
            ? "Goes nowhere yet."
            : (dest == 0 && selectedParam.isNotEmpty() ? "Not a modulation destination."
                                                       : "Nothing modulates this.");
        g.drawFittedText (msg, r.removeFromTop (26), juce::Justification::topLeft, 2);
    }

    // --- live sources (SourceMeters paints itself) -----------------------
    g.setFont (monoFont (9.0f));
    g.setColour (kLabelCol);
    g.drawText ("LIVE SOURCES  (click to follow)",
                sourceMeters.getBounds().withY (sourceMeters.getY() - 15).withHeight (14),
                juce::Justification::centredLeft);

    // --- matrix summary --------------------------------------------------
    g.setFont (monoFont (9.0f));
    g.setColour (kLabelCol);
    g.drawText ("MATRIX  " + juce::String ((int) routes_.size()) + " / "
                    + juce::String (kNumModRows),
                inspFullMatrix.getBounds().withY (inspFullMatrix.getY() - 16).withHeight (14),
                juce::Justification::centredLeft);
}

//==============================================================================
//  The three CZ envelopes. Each bank keeps all 18 of its parameters as ordinary
//  knobs (so automation lanes and existing presets are untouched); the curve is
//  a second view of the same values.
//==============================================================================
void PDHybridEditor::buildStageEnvelopes()
{
    auto param = [this] (const juce::String& id) { return proc.apvts.getParameter (id); };

    auto makeBank = [&] (const juce::String& name, const juce::String& dest,
                         const juce::String& prefix, const juce::String& amountId,
                         const juce::String& sustainId, int amountDecimals,
                         bool bipolar, juce::Colour colour)
    {
        StageEnvelopePanel::Bank b;
        b.name = name;
        b.dest = dest;
        b.bipolar = bipolar;
        b.colour = colour;
        b.sustain = param (sustainId);

        for (int i = 1; i <= 8; ++i)
        {
            b.rate[i - 1]  = param (prefix + "Rate"  + juce::String (i));
            b.level[i - 1] = param (prefix + "Level" + juce::String (i));
        }
        for (int i = 1; i <= 8; ++i)
            b.knobs.push_back (&addKnob (prefix + "Rate" + juce::String (i),
                                         "R" + juce::String (i), 2, KnobSize::Small));
        for (int i = 1; i <= 8; ++i)
            b.knobs.push_back (&addKnob (prefix + "Level" + juce::String (i),
                                         "L" + juce::String (i), 2, KnobSize::Small));
        b.knobs.push_back (&addKnob (amountId,  "Amt", amountDecimals, KnobSize::Large));
        b.knobs.push_back (&addKnob (sustainId, "Sus Pt", 0));

        stageEnv.addBank (std::move (b));
    };

    // Parameter ids are "czRate1..8" / "czLevel1..8" for the filter envelope and
    // "<prefix>Rate/Level" for the other two.
    makeBank ("DCO",   "-> pitch",     "pitchEnv", "pitchEnvAmount", "pitchEnvSustain",
              0, true,  juce::Colour (0xffe8a54b));
    makeBank ("DCW",   "-> PD amount", "dcwEnv",   "dcwEnvAmount",   "dcwEnvSustain",
              1, true,  juce::Colour (0xffe8a54b));
    makeBank ("MULTI", "-> filter",    "cz",       "czAmount",       "czSustain",
              2, false, juce::Colour (0xff4be08a));

    stageEnv.start();
}

//==============================================================================
//  Performance strip — the controls reached on every patch, pinned above the
//  tabs so they never leave the screen. Two of them (Macro 1 / Macro 2) had no
//  control anywhere in the editor before this.
//==============================================================================
void PDHybridEditor::buildStrip()
{
    // Amp envelope contributes only A D S R here; its velocity sensitivity is a
    // playing-response setting and lives with the velocity curve on Global.
    stripKnobs = { &addKnob ("cutoff", "Cutoff", 2, KnobSize::Large),
                   &addKnob ("resonance", "Reso", 2, KnobSize::Large),
                   &addKnob ("macro1", "Macro 1", 2, KnobSize::Large),
                   &addKnob ("macro2", "Macro 2", 2, KnobSize::Large),
                   envelope.knobs[0], envelope.knobs[1],
                   envelope.knobs[2], envelope.knobs[3],
                   &addKnob ("masterLevel", "Master", 1, KnobSize::Large) };

    for (auto* k : stripKnobs)
    {
        strip.addAndMakeVisible (k->slider);
        strip.addAndMakeVisible (k->label);
    }

    // The amp envelope's shape sits above its four knobs, exactly as on the page
    // cards, so the strip is not just a row of numbers.
    ampCurve.attach (proc.apvts, "attack", "decay", "sustain", "release");
    strip.addAndMakeVisible (ampCurve);

    stripPoly = &addCombo ("voiceMode", { "Poly", "Mono", "Legato", "Unison Legato" });
    strip.addAndMakeVisible (*stripPoly);
    stripLimiter = &addToggle ("masterLimiter", "LIM");
    strip.addAndMakeVisible (*stripLimiter);
    stripArp = &addToggle ("arpOn", "ARP");
    strip.addAndMakeVisible (*stripArp);

    strip.addAndMakeVisible (scope_);
    strip.onResized = [this] { layoutStrip(); };
    strip.onPaint   = [this] (juce::Graphics& g)
    {
        auto b = strip.getLocalBounds();
        g.setColour (kCardEdge);
        g.drawRect (b, 1);

        // One card title in a notch, exactly like a page card — no per-cluster
        // captions; the dividers already group the clusters.
        g.setFont (monoFont (9.5f));
        {
            const juce::String title = "PERFORMANCE - ALWAYS VISIBLE";
            const int tw = g.getCurrentFont().getStringWidth (title) + 14;
            g.setColour (kCardBg);
            g.fillRect (b.getX() + 12, b.getY() - 1, tw, kStripCapH);
            g.setColour (kTitleCol);
            g.drawText (title, b.getX() + 17, b.getY() - 1, tw, kStripCapH,
                        juce::Justification::centredLeft);
        }
        g.setColour (kCardEdge);
        for (int x : stripDividers_)
            g.fillRect (x, b.getY() + kStripCapH, 1, b.getHeight() - kStripCapH - 8);

        // Voice count beside the voice-mode combo, so the combo reads as
        // "POLY / 16" the way a hardware display would show it.
        if (stripPoly != nullptr)
            if (auto* p = proc.apvts.getParameter ("polyphony"))
            {
                g.setFont (monoFont (9.0f));
                g.setColour (kLabelCol);
                g.drawText (p->getCurrentValueAsText() + " VOICES",
                            stripPoly->getBounds().translated (0, -12).withHeight (11),
                            juce::Justification::centredLeft);
            }
    };
    addAndMakeVisible (strip);
}

void PDHybridEditor::layoutStrip()
{
    stripDividers_.clear();
    stripGroups_.clear();

    auto r = strip.getLocalBounds();
    r.removeFromTop (kStripCapH);          // band reserved for the card title
    r = r.reduced (10, 0).withTrimmedBottom (8);

    // One knob cell: label above, rotary below (sized by its own size tag).
    // Cells keep the same height as on the pages and are centred in the strip,
    // so a taller strip does not inflate the rotaries.
    auto placeKnob = [&] (LabeledKnob* k, juce::Rectangle<int> cell)
    {
        cell = cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(), kCellH));
        k->label.setBounds  (cell.removeFromTop (kLabelH));
        k->slider.setBounds (cell);
    };
    // Every cluster is vertically centred on the same band, so the strip reads as
    // one row whether or not a cluster carries a curve above its knobs.
    auto group = [&] (const juce::String& name, int width, int contentH)
    {
        auto zone = r.removeFromLeft (width);
        stripGroups_.emplace_back (name, zone);
        return zone.withSizeKeepingCentre (zone.getWidth(), juce::jmin (zone.getHeight(), contentH));
    };
    auto divider = [&] { r.removeFromLeft (5); stripDividers_.push_back (r.removeFromLeft (1).getX()); r.removeFromLeft (5); };

    {   // Filter — duplicated from the Shape page on purpose: these two are
        // reached constantly, so they are the one thing worth showing twice.
        auto z = group ("", 176, kCellH);
        placeKnob (stripKnobs[0], z.removeFromLeft (88));
        placeKnob (stripKnobs[1], z.removeFromLeft (88));
    }
    divider();
    {
        auto z = group ("", 176, kCellH);
        placeKnob (stripKnobs[2], z.removeFromLeft (88));
        placeKnob (stripKnobs[3], z.removeFromLeft (88));
    }
    divider();
    {   // Curve and its four knobs on one line — the strip is a single row.
        auto z = group ("", 352, kCellH);
        for (int i = 3; i >= 0; --i)
            placeKnob (stripKnobs[4 + i], z.removeFromRight (54));
        z.removeFromRight (8);
        ampCurve.setBounds (z.withSizeKeepingCentre (z.getWidth(), kStripCurveH));
    }
    divider();

    {   // Master cluster: one column pinned right - voice mode, the two state
        // lamps, then the master knob. The scope takes whatever is left.
        auto z = r.removeFromRight (124);
        auto col = z.withSizeKeepingCentre (z.getWidth(), juce::jmin (z.getHeight(),
                                            kComboRowH + 4 + 18 + 4 + kCellH));
        stripPoly->setBounds (col.removeFromTop (kComboRowH).reduced (2, 1));
        col.removeFromTop (4);
        auto lamps = col.removeFromTop (18).reduced (2, 0);
        stripLimiter->setBounds (lamps.removeFromLeft (lamps.getWidth() / 2 - 2));
        lamps.removeFromLeft (4);
        stripArp->setBounds (lamps);
        col.removeFromTop (4);
        placeKnob (stripKnobs[8], col);

        r.removeFromRight (8);
        scope_.setBounds (r.reduced (0, 2));
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
    // Footer: page indicator + live route count on the left, the two non-editing
    // actions on the right.
    footer.onPaint   = [this] (juce::Graphics& g) { paintFooter (g); };
    footer.onResized = [this] { layoutFooter(); };
    footer.addAndMakeVisible (panicButton);
    footer.addAndMakeVisible (randButton);
    panicButton.onClick = [this] { proc.triggerPanic(); };
    randButton.onClick  = [this] { randomizePatch(); };
    addAndMakeVisible (footer);

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
    buildStageEnvelopes();
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
        { "1 " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " VOICE",
                        { &oscA, &oscB, &mixer, &bassSec, &pluckSec, &unison, &glideSec },
                                                                                nullptr, {}, 0 },
        { "2 " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " SHAPE",
                        { &filter, &filter2, &filterEnv, &filter2Env,
                          &routingSec, &drive },                                nullptr, {}, 0 },
        { "3 " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " MOD",
                        { &stageEnvSec, &lfo, &lfo2,
                          &modEnv, &vibratoSec, &arpSec },                      nullptr, {}, 0 },
        { "4 " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " OUT",
                        { &chorusSec, &delaySec, &reverbSec,
                          &globalEqSec, &comp, &stereo },                       nullptr, {}, 0 },
        { juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\x99")) + " GLOBAL",
                        { &voiceSec, &tuningSec, &globalLfoSec, &qualitySec },  nullptr, {}, 0 },
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

    // --- Modulation Inspector ---
    inspector.onPaint     = [this] (juce::Graphics& g) { paintInspector (g); };
    inspector.onResized   = [this] { layoutInspector(); };
    inspector.onMouseDown = [this] (const juce::MouseEvent& e) { inspectorClicked (e); };
    inspector.addAndMakeVisible (inspAddRoute);
    inspector.addAndMakeVisible (inspFullMatrix);
    inspector.addAndMakeVisible (inspSourcesBtn);
    inspector.addAndMakeVisible (inspDestsBtn);
    // Real levels straight off the DSP, not parameter positions.
    {
        std::vector<pdui::SourceMeters::Row> rows;
        for (const auto& m : kMeterRows)
            rows.push_back ({ m.src, m.name, m.trace, m.bipolar });
        sourceMeters.setRows (std::move (rows));
    }
    sourceMeters.setReader ([this] (float* d, int n) { proc.readModLevels (d, n); });
    sourceMeters.onRowClicked = [this] (int src)
    {
        selectedSource   = src;
        showDestinations = true;
        layoutInspector();
        inspector.repaint();
    };
    inspector.addAndMakeVisible (sourceMeters);
    inspSourcesBtn.setClickingTogglesState (false);
    inspDestsBtn.setClickingTogglesState (false);
    inspSourcesBtn.onClick = [this] { showDestinations = false; layoutInspector(); inspector.repaint(); };
    inspDestsBtn.onClick   = [this] { showDestinations = true;  layoutInspector(); inspector.repaint(); };
    inspAddRoute.onClick   = [this] { addRouteToSelected(); };
    inspFullMatrix.onClick = [this] { tabs.setCurrentTabIndex (2); };
    addAndMakeVisible (inspector);
    selectParameter ("cutoff");
    refreshModRings();
    startTimerHz (12);   // live meters + ring/route refresh

    // Added last so it sits on top of the tabs; it never intercepts the mouse.
    addAndMakeVisible (crtOverlay);

    setResizable (true, true);
    setResizeLimits (1040, 620, 2400, 1700);
    setSize (1320, 980);
}

PDHybridEditor::~PDHybridEditor()
{
    stopTimer();
    cancelPendingUpdate();
    for (auto& k : knobs)
        k->slider.removeMouseListener (this);
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

void PDHybridEditor::layoutFooter()
{
    auto r = footer.getLocalBounds().reduced (2, 3);
    randButton.setBounds  (r.removeFromRight (58));
    r.removeFromRight (5);
    panicButton.setBounds (r.removeFromRight (62));
}

void PDHybridEditor::paintFooter (juce::Graphics& g)
{
    auto r = footer.getLocalBounds();
    g.setColour (kCardEdge);
    g.fillRect (0, 0, r.getWidth(), 1);

    static const char* names[] = { "VOICE 1/4", "SHAPE 2/4", "MOD 3/4", "OUT 4/4", "GLOBAL" };
    const int page = juce::jlimit (0, 4, tabs.getCurrentTabIndex());

    g.setFont (monoFont (9.0f));
    g.setColour (kLabelCol);
    g.drawText (names[page], r.withTrimmedLeft (6), juce::Justification::centredLeft);

    const int active = static_cast<int> (routes_.size());
    g.setColour (active > 0 ? kModCol : kLabelCol.withAlpha (0.6f));
    g.drawText (juce::String (active) + " MOD ROUTE" + (active == 1 ? "" : "S") + " ACTIVE",
                r.withTrimmedRight (140), juce::Justification::centredRight);
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

    // Deleting lives here rather than as a top-bar button, so the preset being
    // deleted is named in the item itself.
    if (current.isNotEmpty())
    {
        menu.addSeparator();
        menu.addItem (kDeletePresetId, "Delete \"" + current.fromLastOccurrenceOf ("/", false, false) + "\"");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [this, paths, current] (int result)
                        {
                            if (result == kDeletePresetId)
                            {
                                proc.getPresetManager().deletePreset (current);
                                refreshPresetList();
                            }
                            else if (result >= 1 && result <= paths->size())
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

    // Seven controls only. Panic and Rand live in the footer; deleting a preset
    // is an item inside the preset menu, where the preset it deletes is named.
    int x = top.getRight() - 76;
    initButton.setBounds (x, y, 64, 26);
    x -= 52;  crtButton.setBounds  (x, y, 46, 26);
    x -= 70;  saveButton.setBounds (x, y, 64, 26);
    x -= 58;  abButton.setBounds   (x, y, 52, 26);
    x -= 32;  nextButton.setBounds (x, y, 28, 26);
    x -= 32;  prevButton.setBounds (x, y, 28, 26);
    x -= 226; presetButton.setBounds (x, y, 220, 26);

    strip.setBounds (r.removeFromTop (kStripH).reduced (kMargin, 4));
    footer.setBounds (r.removeFromBottom (kFooterH).reduced (kMargin, 0));
    // The Inspector is a column, not a page: it spans the tab area's full height
    // so nothing in it competes with the strip for the same row.
    inspector.setBounds (r.removeFromRight (kInspW).reduced (6, 6));
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
