#include "PluginEditor.h"

namespace {
// Layout metrics, sized so every page fits 1280 x 800 without scrolling -- the
// editor used to open at 1348 x 1382, taller than a 1080p screen.
//
// kKnobH covers the rotary AND the value text box beneath it; kLabelH is the
// caption on top. So one cell is 61 px tall, not 48. That distinction matters:
// a row's height is set by the tallest *cell* in it, which means shrinking a
// display next to a knob buys nothing until the knob shrinks too.
constexpr int kKnobW    = 48;
constexpr int kKnobH    = 48;   // rotary + text box below
constexpr int kLabelH   = 13;
constexpr int kCellW    = 58;   // one knob cell (knob + gutter)
constexpr int kCellH    = kLabelH + kKnobH;   // 61
constexpr int kHeaderH  = 19;   // card title strip
constexpr int kComboRowH = 21;
constexpr int kCardPad  = 6;    // inner padding of a card
constexpr int kGap      = 6;    // gap between cards
constexpr int kMargin   = 10;   // panel outer margin
constexpr int kMatrixRowH = 26;
constexpr int kTopBar   = 40;   // title bar above the tabs
// Three columns, packed shortest-first rather than laid out in rows. A card
// whose span reaches this count takes a full-width band of its own.
constexpr int kGridCols = 3;
constexpr int kStripH    = 110;  // fixed performance strip between title bar and tabs
constexpr int kFooterH   = 20;   // page indicator / route count / panic + rand
constexpr int kDeletePresetId = 9000;   // menu id, kept clear of the preset ids
constexpr int kStripCurveH = 34; // amp-envelope curve, above its row of knobs
constexpr int kStripCapH = 11;   // caption band along the strip's top edge

// Palette lookups read through the editor's LookAndFeel (pdui::ColourIds, see
// Theme.h), so a theme change repaints into the new skin with no other code
// involved. Call findColour(...) directly at each site — every site below is
// inside a Component member function or a paint lambda that captures `this`.

const juce::StringArray kOscTypeNames { "Phase Distortion", "Saw", "Square", "Triangle", "Pulse", "Vector PS", "Scanned", "VOSIM", "Walsh", "Supersaw", "Harmonic", "PAF", "Granular", "Wavetable" };
const juce::StringArray kPdWaveNames  { "Sawtooth", "Square", "Pulse", "Double Sine",
                                        "Saw-Pulse", "Resonant I", "Resonant II", "Resonant III" };
// Must match the "filterType" / "filter2Type" choice parameters exactly.
const juce::StringArray kFilterTypeNames { "Ladder", "State Variable", "PD Resonator",
                                           "Comb", "Allpass", "Formant", "Diode Ladder", "Band Split" };

// Per-type role of the filter MORPH knob. Every engine reads it differently and
// two ignore it entirely, so the editor relabels it and greys it out when dead.
struct FilterKnobRoles
{
    juce::String morphLabel; bool morphActive;
};
FilterKnobRoles filterKnobRoles (int type)
{
    switch (static_cast<pdhybrid::FilterType> (type))
    {
        case pdhybrid::FilterType::StateVariable: return { "LP-BP-HP", true };
        case pdhybrid::FilterType::PdResonator:   return { "PD Amt",   true };
        case pdhybrid::FilterType::Comb:          return { "Damp",     true };
        case pdhybrid::FilterType::Allpass:       return { "Stages",   true };
        case pdhybrid::FilterType::Formant:       return { "Vowel",    true };
        case pdhybrid::FilterType::BandSplit:     return { "Tilt",     true };
        case pdhybrid::FilterType::DiodeLadder:   return { "Morph",    false };  // 24 dB LP only
        case pdhybrid::FilterType::Ladder:
        default:                                  return { "Morph",    false };  // 24 dB LP only
    }
}

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
        case pdhybrid::OscType::Supersaw:        return { "Detune", true,  "Mix",   true,  "Voices",  true,  false };  // engine = saw count
        case pdhybrid::OscType::Harmonic:        return { "Partial", true, "Odd/Ev", true, "Width",   true,  false };  // engine = window width
        case pdhybrid::OscType::Paf:             return { "Formant", true, "Band",  true,  "Shape",   true,  false };  // phase-aligned formant
        case pdhybrid::OscType::Granular:        return { "Scatter", true, "Size",  true,  "Density", true,  false };  // grain cloud
        case pdhybrid::OscType::Wavetable:       return { "Pos",    true,  "Warp",  true,  "Fmnt",    true,  false };  // engine = formant shift
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
                                    "Delay Mix", "Delay Fbk", "Master Pan", "Global EQ",
                                    "Ring Mod", "Cross Mod", "Engine", "PD Amount B",
                                    "Pluck Decay", "Pluck Damp", "Chorus Depth", "Reverb Mix", "FX Send" };
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
    if (s.custom2 != nullptr)
        addAndMakeVisible (*s.custom2);

    sections.push_back (s);
}

void PDHybridEditor::SectionPanel::setCustomHeight (juce::Component* c, int newHeight)
{
    for (auto& s : sections)
        if (s.custom == c && s.customH != newHeight)
        {
            s.customH = newHeight;
            resized();
            repaint();
            if (auto* vp = findParentComponentOfClass<juce::Viewport>())
                vp->resized();     // the panel's preferred height just changed
            return;
        }
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

    // A split card lays its two knob groups out independently, so each group
    // rounds up to whole rows on its own.
    auto knobRows = [&] (const Section& s)
    {
        const int cols = juce::jmax (1, s.cols);
        const int n = static_cast<int> (s.knobs.size());
        if (s.custom2 == nullptr || s.knobSplit <= 0 || s.knobSplit >= n)
            return (n + cols - 1) / cols;
        return (s.knobSplit + cols - 1) / cols + (n - s.knobSplit + cols - 1) / cols;
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
             + s.customH  + (s.customH  > 0 ? kCardPad : 0)
             + s.customH2 + (s.customH2 > 0 ? kCardPad : 0)
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
            // The second display drops in once the first knob group is placed.
            if (s.custom2 != nullptr && s.knobSplit >= 0
                && k == static_cast<std::size_t> (s.knobSplit))
            {
                s.custom2->setBounds (inner.removeFromTop (s.customH2));
                inner.removeFromTop (kCardPad);
            }

            auto krow = inner.removeFromTop (kCellH);
            for (int c = 0; c < s.cols && k < s.knobs.size(); ++c, ++k)
            {
                // Start a fresh row at the split, even mid-row, so the second
                // display always lands between the two knob groups.
                if (s.custom2 != nullptr && s.knobSplit > 0 && c > 0
                    && k == static_cast<std::size_t> (s.knobSplit))
                    break;

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

    // Column pack. The old grid laid units out in rows and padded every card in
    // a row to the tallest card in it -- which is where the wasted space came
    // from: a one-knob Mixer beside a Mono Bass with an envelope display was
    // stretched to match it. Here each card keeps its own natural height and
    // drops into whichever column is currently shortest, so a short card no
    // longer inherits a tall neighbour's height.
    //
    // `colBottom[c]` is the next free y in column c.
    std::vector<int> colBottom ((std::size_t) numCols, kMargin);

    auto highestOf = [&] (int first, int sp)
    {
        int y = colBottom[(std::size_t) first];
        for (int c = first + 1; c < first + sp; ++c)
            y = juce::jmax (y, colBottom[(std::size_t) c]);
        return y;
    };

    // Places one unit's members top-to-bottom at their natural heights.
    auto emit = [&] (const Unit& u, int cx, int cw, int top)
    {
        int yy = top;
        for (int idx : u.members)
        {
            Section& s = sections[(std::size_t) idx];
            const int rows = comboRowsFor (s, cw);
            const int h    = sectionHeight (s, rows);
            if (apply)
                placeCard (s, { cx, yy, cw, h }, rows);
            yy += h + kGap;
        }
        return yy - kGap;   // bottom of the last member
    };

    for (const auto& u : units)
    {
        // A unit asking for the whole grid takes a band of its own across the
        // full width. This is not a nicety: the CZ stage-envelope card carries
        // sixteen numeric knobs in one row, which needs more width than any
        // single column can offer, so it cannot be packed like the others.
        const bool band = u.span >= numCols;
        const int  sp   = band ? numCols : u.span;

        int best = 0;
        if (! band)
        {
            // An explicit column wins; otherwise fill the shortest.
            const int want = sections[(std::size_t) u.members.front()].column;
            if (want >= 0 && want + sp <= numCols)
            {
                best = want;
            }
            else
            {
                int bestY = highestOf (0, sp);
                for (int c = 1; c + sp <= numCols; ++c)
                {
                    const int y = highestOf (c, sp);
                    if (y < bestY) { bestY = y; best = c; }   // ties keep the left-most
                }
            }
        }

        const int top = highestOf (best, sp);
        const int cx  = kMargin + best * colPitch;
        const int cw  = cardW (sp);
        const int bot = emit (u, cx, cw, top);

        for (int c = best; c < best + sp; ++c)
            colBottom[(std::size_t) c] = bot + kGap;
    }

    int y = kMargin;
    for (int c = 0; c < numCols; ++c)
        y = juce::jmax (y, colBottom[(std::size_t) c] - kGap);

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
    auto drawFrame = [this, &g] (juce::Rectangle<int> b, const juce::String& title,
                                 juce::Colour titleColour)
    {
        // Black box with a thin green outline (square corners, terminal style).
        g.setColour (findColour (pdui::panelCard));
        g.fillRect (b);

        // A one-pixel sheen along the top edge reads as a raised moulding. Skins
        // that are not physical set highlight to transparent, so this is a no-op
        // for them rather than needing a branch.
        g.setColour (findColour (pdui::panelHighlight));
        g.fillRect (b.getX() + 1, b.getY() + 1, b.getWidth() - 2, 1);

        g.setColour (findColour (pdui::panelEdge));
        g.drawRect (b, 1);

        // Title label sits in a black notch breaking the top border.
        g.setFont (pdui::labelFont (*this, 11.5f));
        const int tw = g.getCurrentFont().getStringWidth (title) + 14;
        juce::Rectangle<int> tag (b.getX() + 12, b.getY() - 1, tw, kHeaderH - 6);
        g.setColour (findColour (pdui::panelCard));
        g.fillRect (tag);
        g.setColour (titleColour);
        g.drawText (title, tag.withTrimmedLeft (5), juce::Justification::centredLeft);
    };

    for (const auto& s : sections)
    {
        drawFrame (s.bounds, s.title,
                   findColour (s.titleLive ? pdui::liveCol : pdui::textInk));

        // Punch a black notch behind each header toggle so it reads as breaking
        // the frame, matching the title tag on the other end.
        g.setColour (findColour (pdui::panelCard));
        for (auto* t : s.toggles)
            g.fillRect (t->getBounds().expanded (3, 0));
    }

    if (trailing != nullptr)
        drawFrame (trailing->getBounds(), trailingTitle, findColour (pdui::textInk));
}

//==============================================================================
//  StageEnvelopePanel — the three CZ 8-stage envelopes as one draggable curve
//==============================================================================
namespace {
constexpr int kEnvSelH   = 26;    // stage-selector row (button + destination caption)
constexpr int kEnvGraphH = 88;   // curve area
// The 16 rate/level knobs use a compact cell: they are reference numbers under
// the graph, not the primary way to edit the envelope.
constexpr int kEnvKnobH  = 50;
constexpr int kEnvSegW   = 92;   // one stage-selector segment
constexpr float kNodeR   = 4.5f;  // breakpoint handle radius
}

PDHybridEditor::StageEnvelopePanel::StageEnvelopePanel() = default;
PDHybridEditor::StageEnvelopePanel::~StageEnvelopePanel() { stopTimer(); }

int PDHybridEditor::StageEnvelopePanel::preferredHeight() const
{
    // Selector + graph + the AMT / SUS PT row, then the collapsible numerics.
    // As a full-width band all sixteen fit on one row, so this asks for one --
    // see resized(), which falls back to two rows only if the card is narrowed.
    return kEnvSelH + kEnvGraphH + kCardPad + kEnvKnobH
         + (expanded ? numericRows * kEnvKnobH : 0);
}

juce::Rectangle<int> PDHybridEditor::StageEnvelopePanel::selectorArea() const
{
    return getLocalBounds().withHeight (kEnvSelH).reduced (0, 2);
}

juce::Rectangle<int> PDHybridEditor::StageEnvelopePanel::segmentArea (int i) const
{
    return selectorArea().withX (selectorArea().getX() + i * kEnvSegW).withWidth (kEnvSegW);
}

juce::Rectangle<int> PDHybridEditor::StageEnvelopePanel::expanderArea() const
{
    return { 2, kEnvSelH + kEnvGraphH + kCardPad + 4, 92, 18 };
}

void PDHybridEditor::StageEnvelopePanel::addBank (Bank b)
{
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
        for (std::size_t k = 0; k < banks[i].knobs.size(); ++k)
        {
            // The last two knobs (Amt, Sus Pt) stay out on the expander row;
            // the sixteen numeric ones follow the NUMERIC toggle.
            const bool numeric = (k < 16);
            const bool vis = on && (! numeric || expanded);
            banks[i].knobs[k]->slider.setVisible (vis);
            banks[i].knobs[k]->label.setVisible (vis);
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
    if (h != hoverNode)
    {
        hoverNode = h;
        // Nothing else signalled that these nodes are grabbable.
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor
                               : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PDHybridEditor::StageEnvelopePanel::mouseExit (const juce::MouseEvent&)
{
    if (hoverNode != -1) { hoverNode = -1; repaint(); }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PDHybridEditor::StageEnvelopePanel::mouseDown (const juce::MouseEvent& e)
{
    // Stage selector: three segments choosing which envelope is being edited.
    for (int i = 0; i < static_cast<int> (banks.size()); ++i)
        if (segmentArea (i).contains (e.getPosition()))
        {
            selectBank (i);
            return;
        }

    if (expanderArea().contains (e.getPosition()))
    {
        expanded = ! expanded;
        selectBank (active);                       // re-apply knob visibility
        if (onHeightChanged) onHeightChanged();
        return;
    }

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
    if (banks.empty()) return;

    auto r = getLocalBounds();
    r.removeFromTop (kEnvSelH + kEnvGraphH + kCardPad);

    auto& knobs = banks[(std::size_t) active].knobs;

    // Amount and the sustain point ride the expander row, right-aligned: they
    // are the envelope's two headline controls, not part of the numeric grid.
    auto amtRow = r.removeFromTop (kEnvKnobH);
    for (int i = 1; i >= 0; --i)
    {
        auto cell = amtRow.removeFromRight (74);
        knobs[(std::size_t) (16 + i)]->label.setBounds  (cell.removeFromTop (kLabelH - 2));
        knobs[(std::size_t) (16 + i)]->slider.setBounds (cell);
    }

    // All sixteen rates and levels on one row when the card is wide enough to
    // hold them, which it is as a full-width band -- 16 * kCellW is under any
    // realistic content width. Falling back to two rows of eight keeps this
    // correct if the card is ever narrowed.
    const bool oneRow = r.getWidth() >= 16 * kCellW;
    const int  perRow = oneRow ? 16 : 8;
    const int  rows   = oneRow ? 1 : 2;
    const int  cellW  = r.getWidth() / perRow;

    if (rows != numericRows)
    {
        numericRows = rows;
        if (onHeightChanged) onHeightChanged();
    }

    for (int row = 0; row < rows; ++row)
    {
        auto krow = r.removeFromTop (kEnvKnobH);
        for (int c = 0; c < perRow; ++c)
        {
            auto cell = krow.removeFromLeft (cellW);
            auto* k = knobs[(std::size_t) (row * perRow + c)];
            k->label.setBounds  (cell.removeFromTop (kLabelH - 2));
            k->slider.setBounds (cell);
        }
    }
}

void PDHybridEditor::StageEnvelopePanel::paint (juce::Graphics& g)
{
    if (banks.empty()) return;

    const auto& bank  = banks[(std::size_t) active];
    const auto  frame = graphArea();
    const int susStage = bank.sustain != nullptr
        ? juce::roundToInt (bank.sustain->convertFrom0to1 (bank.sustain->getValue())) : 0;

    // --- stage selector: one outlined strip, segment per envelope -----------
    {
        auto sel = selectorArea();
        g.setColour (findColour (pdui::panelEdge));
        g.drawRect (sel, 1);

        for (int i = 0; i < static_cast<int> (banks.size()); ++i)
        {
            auto seg = segmentArea (i);
            const bool on = (i == active);
            if (on)
            {
                g.setColour (findColour (banks[(std::size_t) i].live ? pdui::liveCol
                                                                     : pdui::accentCol)
                                 .withAlpha (0.10f));
                g.fillRect (seg.reduced (1));
            }
            g.setColour (findColour (pdui::panelEdge));
            g.fillRect (seg.getRight(), seg.getY(), 1, seg.getHeight());

            auto inner = seg.reduced (8, 3);
            g.setFont (pdui::labelFont (*this, 10.0f));
            g.setColour (on ? findColour (banks[(std::size_t) i].live ? pdui::liveCol : pdui::accentCol)
                            : findColour (pdui::textDim).withAlpha (0.65f));
            g.drawText (banks[(std::size_t) i].name, inner.removeFromTop (13),
                        juce::Justification::centredLeft);
            g.setFont (pdui::labelFont (*this, 8.0f));
            g.setColour (on ? findColour (pdui::textDim) : findColour (pdui::textDim).withAlpha (0.45f));
            g.drawText (banks[(std::size_t) i].dest, inner, juce::Justification::topLeft);
        }

        // Trailing segment: what the graph is and how to drive it, including
        // the sustain stage the envelope is actually holding on.
        auto hint = selectorArea().withTrimmedLeft (static_cast<int> (banks.size()) * kEnvSegW + 10);
        g.setFont (pdui::labelFont (*this, 8.5f));
        g.setColour (findColour (pdui::textDim).withAlpha (0.6f));
        const juce::String dot (juce::CharPointer_UTF8 ("\xc2\xb7"));
        g.drawText ("8 STAGES  " + dot + "  SUSTAIN AT " + juce::String (susStage)
                        + "  " + dot + "  DRAG: UP/DOWN = LEVEL, LEFT/RIGHT = RATE",
                    hint, juce::Justification::centredLeft);
    }

    // --- NUMERIC expander --------------------------------------------------
    {
        auto ex = expanderArea();
        g.setColour (expanded ? findColour (pdui::tabOnBg) : findColour (pdui::panelCard));
        g.fillRect (ex);
        g.setColour (expanded ? findColour (pdui::accentCol) : findColour (pdui::panelEdge));
        g.drawRect (ex, 1);
        g.setFont (pdui::labelFont (*this, 8.5f));
        g.setColour (findColour (pdui::accentCol));
        g.drawText (juce::String (expanded ? juce::CharPointer_UTF8 ("\xe2\x96\xbe")
                                           : juce::CharPointer_UTF8 ("\xe2\x96\xb8")) + " NUMERIC",
                    ex, juce::Justification::centred);

        g.setFont (pdui::labelFont (*this, 8.0f));
        g.setColour (findColour (pdui::textDim).withAlpha (0.5f));
        g.drawText ("all 18 params still automatable",
                    ex.withX (ex.getRight() + 8).withWidth (260), juce::Justification::centredLeft);
    }
    const auto  area  = frame.toFloat().reduced (6.0f, 8.0f);
    const auto  col   = findColour (bank.live ? pdui::liveCol : pdui::accentCol);

    g.setColour (findColour (pdui::panelEdge));
    g.drawRect (frame, 1);

    // Horizontal guides; bipolar banks also get a centre line at "no offset".
    g.setColour (findColour (pdui::screenGrid));
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
    if (susStage >= 1 && susStage <= 8)
    {
        const float sx = nodePos (susStage - 1).x;
        g.setColour (col.withAlpha (0.55f));
        for (float y = frame.getY() + 2.0f; y < frame.getBottom() - 2.0f; y += 6.0f)
            g.fillRect (sx, y, 1.0f, 3.0f);
        g.setFont (pdui::labelFont (*this, 9.0f));
        g.drawText ("SUS", juce::Rectangle<float> (sx + 3.0f, (float) frame.getY() + 2.0f, 30.0f, 11.0f),
                    juce::Justification::centredLeft);
    }

    // Breakpoint handles; the hovered one reads out its rate and level.
    for (int i = 0; i < 8; ++i)
    {
        const auto np = nodePos (i);
        const bool lit = (i == dragNode || i == hoverNode);
        g.setColour (findColour (pdui::panelCard));
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

        g.setFont (pdui::valueFont (*this, 9.5f));
        const int tw = g.getCurrentFont().getStringWidth (txt) + 12;
        auto box = juce::Rectangle<int> ((int) np.x + 8, (int) np.y - 9, tw, 17);
        if (box.getRight() > frame.getRight() - 2) box.setX ((int) np.x - 8 - tw);
        g.setColour (findColour (pdui::panelCard));   g.fillRect (box);
        g.setColour (col.withAlpha (0.7f)); g.drawRect (box, 1);
        g.setColour (col);       g.drawText (txt, box.reduced (6, 0), juce::Justification::centredLeft);
    }

    // Stage numbers along the bottom edge of the graph.
    g.setFont (pdui::valueFont (*this, 8.5f));
    g.setColour (findColour (pdui::textDim).withAlpha (0.7f));
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
    knob->label.setFont (pdui::labelFont (*this, size == KnobSize::Large ? 11.0f : 10.5f));
    knob->label.setColour (juce::Label::textColourId,
                           size == KnobSize::Large ? findColour (pdui::accentCol) : findColour (pdui::textDim));

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
    oscA.span   = 1;
    oscA.column = 0;
    oscA.custom = &oscACycle;
    oscA.customH = 42;
    oscA.combos = { &addCombo ("oscAType", kOscTypeNames), &addCombo ("oscAWave", kPdWaveNames),
                    &addCombo ("oscAWave2", kPdWaveNames),
                    &addCombo ("oscAExcite", { "Pluck", "Impulse", "Noise", "Triangle" }) };
    // ON is the mixer mute: it silences the oscillator without moving its Level.
    oscA.toggles = { &addToggle ("oscAOn", "ON"), &addToggle ("oscACombine", "CMB") };
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
    oscB.span   = 1;
    oscB.column = 1;
    oscB.custom = &oscBCycle;
    oscB.customH = 42;
    oscB.combos = { &addCombo ("oscBType", kOscTypeNames), &addCombo ("oscBWave", kPdWaveNames),
                    &addCombo ("oscBWave2", kPdWaveNames),
                    &addCombo ("oscBExcite", { "Pluck", "Impulse", "Noise", "Triangle" }) };
    oscB.toggles = { &addToggle ("oscBOn", "ON"), &addToggle ("oscBCombine", "CMB") };
    oscB.knobs  = { &addKnob ("oscBAmount", "PD Amt", 2, KnobSize::Large),
                    &addKnob ("oscBPulseWidth", "Width"),
                    &addKnob ("oscBEngine", "Engine"),
                    &addKnob ("oscBLevel", "Level", 2, KnobSize::Large),
                    &addKnob ("oscBOctave", "Oct", 0), &addKnob ("oscBSemi", "Semi", 0),
                    &addKnob ("oscBFine", "Fine"), &addKnob ("oscBEqLow", "EQ Lo"),
                    &addKnob ("oscBEqMid", "EQ Mid"), &addKnob ("oscBEqHigh", "EQ Hi") };

    mixer.title  = "Mixer";
    mixer.cols   = 4;
    mixer.span   = 1;
    mixer.column = 0;
    mixer.combos = { &addCombo ("oscCrossMod", { "No Cross Mod", "Hard Sync", "Phase Mod" }) };
    mixer.knobs  = { &addKnob ("noiseLevel", "Noise"), &addKnob ("ringMod", "Ring"),
                     &addKnob ("noiseMod", "N.Mod"), &addKnob ("crossModAmount", "X-Amt") };
    // The wavetable is shared by both oscillator slots, so it belongs with the
    // mixer rather than inside either Osc card.
    wavetableButton.onClick = [this] { chooseWavetable(); };
    refreshWavetableButton();
    mixer.toggles = { &wavetableButton };

    // Chord mode. Wide card: the keyboard graphic is the documentation, so it
    // needs the room to stay legible.
    chordKeys.attach (proc.apvts,
                      [this] { return proc.chordHeldRoot(); },
                      [this] (int* out, int maxOut) { return proc.chordVoicedNotes (out, maxOut); });
    chordSec.title   = "Chord";
    chordSec.cols    = 3;
    chordSec.span    = 1;
    chordSec.column  = 2;
    chordSec.custom  = &chordKeys;
    chordSec.customH = 70;
    chordSec.toggles = { &addToggle ("chordOn", "ON") };
    // Must match the chordVoicing choice parameter exactly, in order.
    chordSec.combos  = { &addCombo ("chordVoicing", { "Voice-Led", "Root Position",
                                                      "Closed", "Drop-2", "Shell" }) };
    chordSec.knobs   = { &addKnob ("chordSplit", "Split", 0),
                         &addKnob ("chordSpread", "Spread"),
                         &addKnob ("chordOctave", "Octave", 0) };

    // Names whatever is sounding, chord mode or not: hold a triad by hand with
    // chord mode off and it still reads it. Sits under Chord because that is
    // where the space is and where you look while setting a split up, but it is
    // fed from every note reaching the synth, not from ChordMode's output.
    chordReadout.attach ([this] (int* out, int maxOut)
                         { return proc.soundingNotes (out, maxOut); });
    chordIdSec.title   = "Chord ID";
    chordIdSec.cols    = 1;
    chordIdSec.span    = 1;
    chordIdSec.column  = 2;
    chordIdSec.custom  = &chordReadout;
    chordIdSec.customH = 64;

    bassCurve.attach (proc.apvts, "bassAttack", "bassDecay", "bassSustain", "bassRelease");
    bassSec.title   = "Mono Bass";
    bassSec.cols    = 5;
    bassSec.span    = 1;
    bassSec.column  = 2;
    bassSec.custom  = &bassCurve;
    bassSec.customH = 42;
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
    unison.span  = 1;
    unison.column = 1;
    unison.knobs = { &addKnob ("unisonVoices", "Voices", 0), &addKnob ("unisonDetune", "Detune"),
                     &addKnob ("unisonWidth", "Width"), &addKnob ("unisonSpread", "Spread"),
                     &addKnob ("drift", "Drift"), &addKnob ("fxSend", "FX Send") };

    glideSec.title  = "Glide";
    glideSec.cols   = 2;
    glideSec.span   = 1;
    // Glide is the taller of the two; it goes in the column with the shorter
    // Mixer so the two columns finish level. Unison / Drift is 160 px against
    // Mixer.s 113, so the short card belongs beside Unison, not beside Mixer.
    glideSec.column = 0;
    glideSec.combos = { &addCombo ("glideMode", { "Off", "Always", "Legato" }) };
    glideSec.knobs  = { &addKnob ("glideTime", "Time"), &addKnob ("glideCurve", "Curve") };

    // ---------------------------------------------------------------- SHAPE --
    // Signal topology gets its own card instead of hiding in three unrelated ones.
    routingSec.title   = "Routing";
    routingSec.cols    = 1;
    routingSec.span    = 1;
    routingSec.column  = 2;
    routingSec.combos  = { &addCombo ("filterRouting", { "Single Filter", "Filters Series", "Filters Parallel" }),
                           &addCombo ("drivePos", { "Drive Post Filter", "Drive Pre Filter" }),
                           &addCombo ("fxRouting", { "Delay -> Reverb", "Reverb -> Delay", "Reverb, Dry Delay" }) };
    // The diagram redraws whenever one of those three choices changes, which is
    // the whole point of collecting them here.
    routingDiagram.attach (proc.apvts);
    routingSec.custom  = &routingDiagram;
    routingSec.customH = 62;

    pluckSec.title   = "Pluck";
    // Five across, not four: a fifth knob on a second row would add 61 px to
    // this column and push the VOICE page past its window (pdhybrid_uicheck
    // catches that). A column is wide enough for seven cells.
    pluckSec.cols    = 5;
    pluckSec.span    = 1;
    pluckSec.column  = 1;
    pluckSec.toggles = { &addToggle ("pluckOn", "ON") };
    pluckSec.knobs   = { &addKnob ("pluckDecay", "Decay"), &addKnob ("pluckDamp", "Damp"),
                         &addKnob ("pluckDispersion", "Disp"), &addKnob ("pluckBurst", "Burst", 1),
                         &addKnob ("pluckMix", "Mix") };

    driveCurve.attach (proc.apvts, "driveType", "drive", "bias");
    drive.title   = "Overdrive";
    drive.cols    = 3;
    drive.span    = 1;
    drive.column  = 2;
    drive.custom  = &driveCurve;
    drive.customH = 52;
    drive.toggles = { &addToggle ("driveOn", "ON") };
    drive.combos  = { &addCombo ("driveType",
                       { "Soft", "Cubic", "Hard Clip", "Tube", "Diode", "Fuzz", "Rectify",
                         "Wavefold", "Foldback" }) };
    drive.knobs   = { &addKnob ("drive", "Drive", 2, KnobSize::Large), &addKnob ("bias", "Bias"),
                      &addKnob ("gain", "Gain"), &addKnob ("crushBits", "Crush"),
                      &addKnob ("downsample", "Downsmpl") };

    // A filter and its envelope are one object: response curve, the filter's own
    // controls, the envelope curve, then that envelope's controls — all in the
    // one card, rather than two cards a row apart.
    filt1Resp.attach (proc.apvts, "filterType", "cutoff", "resonance", "filterMorph");
    filt1Curve.attach (proc.apvts, "filterEnvA", "filterEnvD", "filterEnvS", "filterEnvR");
    filter.title    = "Filter 1";
    filter.cols     = 5;
    filter.span     = 1;
    filter.column   = 0;
    filter.custom   = &filt1Resp;
    filter.customH  = 52;
    filter.custom2  = &filt1Curve;
    filter.customH2 = 40;
    filter.knobSplit = 5;
    filter.combos = { &addCombo ("filterType", kFilterTypeNames) };
    filter.toggles = { &addToggle ("filtEnvSync", "SYNC") };
    filter.knobs  = { &addKnob ("cutoff", "Cutoff", 2, KnobSize::Large),
                      &addKnob ("resonance", "Reso", 2, KnobSize::Large),
                      &addKnob ("filterMorph", "Morph"), &addKnob ("keyTrack", "Key Trk"),
                      &addKnob ("filterEnvAmount", "Env Amt"),
                      // --- split: the envelope's own controls ---
                      &addKnob ("filterEnvA", "Atk"), &addKnob ("filterEnvD", "Dec"),
                      &addKnob ("filterEnvS", "Sus"), &addKnob ("filterEnvR", "Rel"),
                      &addKnob ("filterVelSens", "Vel") };

    filt2Resp.attach (proc.apvts, "filter2Type", "filter2Cutoff", "filter2Res", "filter2Morph");
    filt2Curve.attach (proc.apvts, "filter2EnvA", "filter2EnvD", "filter2EnvS", "filter2EnvR");
    filter2.title    = "Filter 2";
    filter2.cols     = 5;
    filter2.span     = 1;
    filter2.column   = 1;
    filter2.custom   = &filt2Resp;
    filter2.customH  = 52;
    filter2.custom2  = &filt2Curve;
    filter2.customH2 = 40;
    filter2.knobSplit = 4;
    filter2.combos = { &addCombo ("filter2Type", kFilterTypeNames) };
    filter2.toggles = { &addToggle ("filt2EnvSync", "SYNC") };
    filter2.knobs  = { &addKnob ("filter2Cutoff", "Cutoff", 2, KnobSize::Large),
                       &addKnob ("filter2Res", "Reso", 2, KnobSize::Large),
                       &addKnob ("filter2Morph", "Morph"), &addKnob ("filter2EnvAmount", "Env Amt"),
                       // --- split ---
                       &addKnob ("filter2EnvA", "Atk"), &addKnob ("filter2EnvD", "Dec"),
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
    modEnv.span    = 1;
    modEnv.column  = 2;
    modEnv.custom  = &modCurve;
    modEnv.customH = 48;
    modEnv.toggles = { &addToggle ("modEnvSync", "SYNC") };
    modEnv.knobs = { &addKnob ("modEnvA", "Atk"), &addKnob ("modEnvD", "Dec"),
                     &addKnob ("modEnvS", "Sus"), &addKnob ("modEnvR", "Rel") };

    // The three CZ 8-stage envelopes share one card with a stage selector and a
    // draggable curve; see buildStageEnvelopes(). Amber-titled because the card
    // *is* a modulation source, the same way modulated knobs are ringed amber.
    stageEnvSec.title     = "Multi-Stage Envelopes (CZ)";
    stageEnvSec.titleLive = true;   // this card IS a modulation source
    // Full-width band: the sixteen numeric knobs need more width than one
    // column can give, so this card takes a row of its own.
    stageEnvSec.span      = kGridCols;
    stageEnvSec.custom    = &stageEnv;
    stageEnvSec.customH   = stageEnv.preferredHeight();

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
    lfo.span    = 1;
    lfo.column  = 0;
    lfo.toggles = { &addToggle ("lfoRetrig", "RETRIG") };
    lfo.combos  = { &addCombo ("lfoWave", kLfoWaveNames), &addCombo ("lfoSync", kSyncNames) };
    lfo.custom  = &lfo1Head;
    lfo.customH = kCellH;
    lfo.cols    = 2;
    lfo.knobs   = { &addKnob ("lfoFade", "Fade"), &addKnob ("lfoPhase", "Phase") };
    {
        auto& rate = addKnob ("lfoRate", "Rate");
        lfo1Head.addAndMakeVisible (lfo1Curve);
        lfo1Head.addAndMakeVisible (rate.slider);
        lfo1Head.addAndMakeVisible (rate.label);
        lfo1Head.onResized = [this, &rate]
        {
            auto b = lfo1Head.getLocalBounds();
            auto cell = b.removeFromRight (78);
            rate.label.setBounds  (cell.removeFromTop (kLabelH));
            rate.slider.setBounds (cell);
            lfo1Curve.setBounds (b.withTrimmedRight (6).reduced (0, 6));
        };
    }

    lfo2Curve.attach (proc.apvts, "lfo2Wave", "lfo2Rate");
    lfo2Curve.setLiveReader ([this]
    {
        float lv[PDHybridAudioProcessor::kNumModSources] {};
        proc.readModLevels (lv, PDHybridAudioProcessor::kNumModSources);
        return lv[(int) pdhybrid::ModSource::Lfo2];
    });
    lfo2.title   = "LFO 2";
    lfo2.cols    = 3;
    lfo2.span    = 1;
    lfo2.column  = 1;
    lfo2.toggles = { &addToggle ("lfo2Retrig", "RETRIG") };
    lfo2.combos  = { &addCombo ("lfo2Wave", kLfoWaveNames), &addCombo ("lfo2Sync", kSyncNames) };
    lfo2.custom  = &lfo2Head;
    lfo2.customH = kCellH;
    lfo2.cols    = 2;
    lfo2.knobs   = { &addKnob ("lfo2Fade", "Fade"), &addKnob ("lfo2Phase", "Phase") };
    {
        auto& rate = addKnob ("lfo2Rate", "Rate");
        lfo2Head.addAndMakeVisible (lfo2Curve);
        lfo2Head.addAndMakeVisible (rate.slider);
        lfo2Head.addAndMakeVisible (rate.label);
        lfo2Head.onResized = [this, &rate]
        {
            auto b = lfo2Head.getLocalBounds();
            auto cell = b.removeFromRight (78);
            rate.label.setBounds  (cell.removeFromTop (kLabelH));
            rate.slider.setBounds (cell);
            lfo2Curve.setBounds (b.withTrimmedRight (6).reduced (0, 6));
        };
    }

    // --- Vibrato (Casio CZ-style) ---
    vibratoSec.title   = "Vibrato";
    vibratoSec.cols    = 3;
    vibratoSec.span    = 1;
    vibratoSec.column  = 0;
    vibratoSec.toggles = { &addToggle ("vibratoOn", "ON") };
    vibratoSec.combos  = { &addCombo ("vibratoWave", { "Triangle", "Square", "Ramp Up", "Ramp Down" }) };
    vibratoSec.knobs   = { &addKnob ("vibratoRate", "Rate"), &addKnob ("vibratoDepth", "Depth", 0),
                           &addKnob ("vibratoDelay", "Delay") };

    // --- Arpeggiator ---
    arpSec.title   = "Arpeggiator";
    arpSec.cols    = 2;
    arpSec.span    = 1;
    arpSec.column  = 1;
    arpSec.toggles = { &addToggle ("arpOn", "ON"), &addToggle ("arpLatch", "LATCH") };
    arpSec.combos  = { &addCombo ("arpMode", { "Up", "Down", "Up-Down", "Random", "As Played" }),
                       &addCombo ("arpRate", { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4.", "1/8.", "1/4T", "1/8T" }),
                       &addCombo ("arpTarget", { "Arp: Poly + Bass", "Arp: Poly Only", "Arp: Bass Only" }) };
    arpSec.knobs   = { &addKnob ("arpOctaves", "Oct", 0), &addKnob ("arpGate", "Gate") };

    // ------------------------------------------------------------------ OUT --
    chorusSec.title   = "Chorus";
    chorusSec.cols    = 3;
    chorusSec.span    = 1;
    chorusSec.column  = 0;
    chorusSec.toggles = { &addToggle ("chorusOn", "ON") };
    chorusSec.combos  = { &addCombo ("chorusMode", { "Mode I", "Mode II", "Mode I+II" }) };
    chorusSec.knobs   = { &addKnob ("chorusRate", "Rate"), &addKnob ("chorusDepth", "Depth"),
                          &addKnob ("chorusMix", "Mix") };

    delayTaps.attach (proc.apvts);
    delaySec.title   = "Delay";
    delaySec.cols    = 3;
    delaySec.span    = 1;
    delaySec.column  = 1;
    delaySec.custom  = &delayTaps;
    delaySec.customH = 44;
    delaySec.toggles = { &addToggle ("delayOn", "ON"), &addToggle ("delaySync", "SYNC") };
    delaySec.combos  = { &addCombo ("delayMode", { "Mono", "Stereo", "Ping-Pong" }) };
    delaySec.knobs   = { &addKnob ("delayTimeL", "Time L"), &addKnob ("delayTimeR", "Time R"),
                         &addKnob ("delayFeedback", "Fbk"), &addKnob ("delayMix", "Mix"),
                         &addKnob ("delayDuck", "Duck") };

    reverbDecay.attach (proc.apvts);
    reverbSec.title   = "Reverb";
    reverbSec.cols    = 4;
    reverbSec.span    = 1;
    reverbSec.column  = 2;
    reverbSec.custom  = &reverbDecay;
    reverbSec.customH = 36;
    reverbSec.toggles = { &addToggle ("reverbOn", "ON") };
    reverbSec.knobs   = { &addKnob ("reverbSize", "Size"), &addKnob ("reverbDamp", "Damp"),
                          &addKnob ("reverbWidth", "Width"), &addKnob ("reverbMix", "Mix") };

    grMeter.setReader ([this] { return proc.gainReductionDb(); });
    comp.title   = "Compressor";
    comp.cols    = 3;
    comp.span    = 1;
    comp.column  = 1;
    comp.custom  = &grMeter;
    comp.customH = 28;
    comp.toggles = { &addToggle ("compOn", "ON") };
    comp.knobs   = { &addKnob ("compThreshold", "Thr", 1, KnobSize::Large),
                     &addKnob ("compRatio", "Ratio"), &addKnob ("compMakeup", "Gain"),
                     &addKnob ("compAttack", "Atk"), &addKnob ("compRelease", "Rel") };

    eqResp.attach (proc.apvts);
    globalEqSec.title   = "Global EQ";
    globalEqSec.cols    = 8;
    globalEqSec.span    = 1;
    globalEqSec.column  = 0;
    globalEqSec.custom  = &eqResp;
    globalEqSec.customH = 56;
    globalEqSec.toggles = { &addToggle ("globalEqOn", "ON") };
    globalEqSec.knobs = { &addKnob ("geLowFreq", "Lo Hz", 0),  &addKnob ("geLowGain", "Lo dB", 1),
                          &addKnob ("geMid1Freq", "M1 Hz", 0), &addKnob ("geMid1Gain", "M1 dB", 1),
                          &addKnob ("geMid2Freq", "M2 Hz", 0), &addKnob ("geMid2Gain", "M2 dB", 1),
                          &addKnob ("geHighFreq", "Hi Hz", 0), &addKnob ("geHighGain", "Hi dB", 1) };

    stereo.title   = "Stereo";
    stereo.cols    = 2;
    stereo.span    = 1;
    stereo.column  = 2;
    stereo.knobs   = { &addKnob ("pan", "Pan"), &addKnob ("panSpread", "Spread") };

    // --------------------------------------------------------------- GLOBAL --
    // Voice allocation and tuning are not part of the audio path, so they sit
    // off it entirely rather than interrupting the chain the way the old
    // "Voice" tab did. (voiceMode lives in the performance strip.)
    voiceSec.title = "Voice Allocation";
    voiceSec.cols  = 3;
    voiceSec.span  = 1;
    voiceSec.column = 0;
    voiceSec.toggles = { &addToggle ("monoRetrigger", "RETRIG") };
    voiceSec.combos = { &addCombo ("notePriority", { "Priority: Last", "Priority: Top", "Priority: Bottom" }),
                        &addCombo ("stealPolicy", { "Steal Oldest", "Steal Quietest" }),
                        &addCombo ("velCurve", { "Vel Linear", "Vel Soft", "Vel Hard", "Vel Fixed" }) };
    velCurveDisp.attach (proc.apvts, "velCurve");
    voiceSec.custom  = &velCurveDisp;
    voiceSec.customH = 56;
    voiceSec.knobs = { &addKnob ("polyphony", "Poly", 0, KnobSize::Large),
                       &addKnob ("ampVelSens", "Vel Sens"),
                       &addKnob ("pitchBendRange", "Bend", 0) };

    scaleDisp.attach (proc.apvts, "tuningScale");
    tuningSec.title  = "Tuning";
    tuningSec.cols   = 3;
    tuningSec.span   = 1;
    tuningSec.column  = 1;
    tuningSec.custom  = &scaleDisp;
    tuningSec.customH = 56;
    tuningSec.combos = { &addCombo ("tuningScale", { "Equal Temperament", "Just Intonation", "Pythagorean" }) };
    tuningSec.knobs  = { &addKnob ("masterTune", "Tune", 1), &addKnob ("transpose", "Transpose", 0) };

    // The global LFO is a modulation-matrix source ("Global LFO") that had no
    // control at all in the editor before this rebuild.
    globalLfoCurve.attach (proc.apvts, "globalLfoWave", "globalLfoRate");
    globalLfoCurve.setLiveReader ([this]
    {
        float lv[PDHybridAudioProcessor::kNumModSources] {};
        proc.readModLevels (lv, PDHybridAudioProcessor::kNumModSources);
        return lv[(int) pdhybrid::ModSource::GlobalLfo];
    });
    globalLfoSec.title  = "Global LFO";
    globalLfoSec.cols   = 1;
    globalLfoSec.span   = 1;
    globalLfoSec.column  = 0;
    globalLfoSec.custom  = &globalLfoHead;
    globalLfoSec.customH = kCellH;
    globalLfoSec.combos = { &addCombo ("globalLfoWave", kLfoWaveNames) };
    {
        auto& rate = addKnob ("globalLfoRate", "Rate", 2, KnobSize::Large);
        globalLfoHead.addAndMakeVisible (globalLfoCurve);
        globalLfoHead.addAndMakeVisible (rate.slider);
        globalLfoHead.addAndMakeVisible (rate.label);
        globalLfoHead.onResized = [this, &rate]
        {
            auto b = globalLfoHead.getLocalBounds();
            auto cell = b.removeFromRight (86);
            rate.label.setBounds  (cell.removeFromTop (kLabelH));
            rate.slider.setBounds (cell);
            globalLfoCurve.setBounds (b.withTrimmedRight (6).reduced (0, 6));
        };
    }

    qualitySec.title  = "Quality";
    qualitySec.cols   = 1;
    qualitySec.span   = 1;
    qualitySec.column  = 2;
    qualitySec.combos = { &addCombo ("osQuality", { "Oversampling 1x", "Oversampling 2x",
                                                    "Oversampling 4x", "Oversampling 8x" }) };

    // Tempo. Host mode still falls back to this BPM when no host transport is
    // present, which is the standalone case.
    tempoSec.title  = "Tempo";
    tempoSec.cols   = 1;
    tempoSec.span   = 1;
    tempoSec.column  = 1;
    tempoSec.combos = { &addCombo ("tempoMode", { "Tempo: Host", "Tempo: Local" }) };
    tempoSec.knobs  = { &addKnob ("internalBpm", "BPM", 1, KnobSize::Large) };

    // Title colours are resolved at paint time from Section::titleLive, so
    // nothing needs listing here -- a card added later cannot be forgotten and
    // left with an unset colour, which is what happened to Chord ID.
}

//==============================================================================
//  Modulation Inspector
//==============================================================================
namespace {
constexpr int kInspW    = 236;   // permanent Inspector column width
constexpr int kInspRowH = 15;
constexpr int kInspMatrixH = 80;   // title + three hint lines + the button

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
// Seven rows, as drawn. There is deliberately no DCW row: the DCW envelope
// drives PD amount directly and is not a matrix source, so there is nothing
// honest to meter for it.
struct MeterRow { int src; const char* name; bool trace; bool bipolar; bool handle; };
const std::vector<MeterRow> kMeterRows {
    { (int) pdhybrid::ModSource::Lfo,       "LFO 1",   true,  true,  false },
    { (int) pdhybrid::ModSource::Lfo2,      "LFO 2",   true,  true,  false },
    { (int) pdhybrid::ModSource::GlobalLfo, "GLB LFO", true,  true,  false },
    { (int) pdhybrid::ModSource::PitchEnv,  "DCO ENV", true,  true,  false },
    { (int) pdhybrid::ModSource::ModEnv,    "MOD ENV", true,  false, false },
    { (int) pdhybrid::ModSource::Macro1,    "MACRO 1", false, false, true  },
    { (int) pdhybrid::ModSource::Macro2,    "MACRO 2", false, false, true  },
};

// Card frame matching the page cards: black box, thin outline, title in a notch.
// A free function (no Component in scope), so the card background is passed
// in rather than looked up here; every caller is a Component member that can
// resolve it via findColour.
void drawInspCard (juce::Graphics& g, const juce::Component& owner, juce::Rectangle<int> b,
                   const juce::String& title, juce::Colour cardBg, juce::Colour edge,
                   juce::Colour titleCol)
{
    g.setColour (cardBg);   g.fillRect (b);
    g.setColour (edge);     g.drawRect (b, 1);
    g.setFont (pdui::labelFont (owner, 8.5f));
    const int tw = g.getCurrentFont().getStringWidth (title) + 12;
    juce::Rectangle<int> tag (b.getX() + 8, b.getY() - 1, tw, 12);
    g.setColour (cardBg);   g.fillRect (tag);
    g.setColour (titleCol);
    g.drawText (title, tag.withTrimmedLeft (4), juce::Justification::centredLeft);
}
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

    showMatrix (true);             // show the route that was just created
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

PDHybridEditor::LabeledKnob* PDHybridEditor::findKnob (const juce::String& paramId)
{
    for (auto& k : knobs)
        if (k->paramId == paramId)
            return k.get();
    return nullptr;
}

void PDHybridEditor::setupEnvTimeReadouts()
{
    // Each group of time controls, paired with the switch that retimes them and
    // (for the envelopes) the curve that draws them. The delay's two taps work
    // the same way, so they share the machinery: `maxSeconds` keeps its snap
    // inside the delay line's two-second reach.
    struct EnvGroup
    {
        const char* sync; const char* a; const char* d; const char* r;
        pdui::EnvelopeCurve* curve;
        double maxSeconds;
    };
    const EnvGroup kGroups[] = {
        { "ampEnvSync",   "attack",      "decay",       "release",     &ampCurve,   1.0e9 },
        { "filtEnvSync",  "filterEnvA",  "filterEnvD",  "filterEnvR",  &filt1Curve, 1.0e9 },
        { "filt2EnvSync", "filter2EnvA", "filter2EnvD", "filter2EnvR", &filt2Curve, 1.0e9 },
        { "modEnvSync",   "modEnvA",     "modEnvD",     "modEnvR",     &modCurve,   1.0e9 },
        { "delaySync",    "delayTimeL",  "delayTimeR",  nullptr,       nullptr,
          pdhybrid::Delay::kMaxDelaySeconds },
    };

    for (const auto& g : kGroups)
    {
        const juce::String syncId (g.sync);
        const double maxSeconds = g.maxSeconds;
        auto synced = [this, syncId]
        { return proc.apvts.getRawParameterValue (syncId)->load() > 0.5f; };

        for (const char* id : { g.a, g.d, g.r })
        {
            if (id == nullptr)
                continue;
            auto* k = findKnob (id);
            if (k == nullptr)
                continue;

            // Replaces the text the attachment installed from the parameter,
            // which can only ever render seconds: it knows nothing about the
            // sync switch or the tempo. Showing "237 ms" for a stage that is
            // actually playing 250 ms would be a readout that lies.
            k->slider.textFromValueFunction = [this, synced, maxSeconds] (double v)
            {
                if (synced())
                    return juce::String (pdhybrid::envDivisionName (
                        pdhybrid::nearestDivisionIndex (v, proc.currentBpm(), maxSeconds)));

                return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms"
                               : juce::String (v, 2) + " s";
            };
            k->slider.updateText();
            envTimeKnobs.push_back (k);
        }

        // The curve draws the time that is actually played, so while SYNC is on
        // it holds still until the division changes.
        if (g.curve != nullptr)
            g.curve->setTimeMapper ([this, synced, maxSeconds] (double seconds)
            { return pdhybrid::syncedEnvTime (seconds, proc.currentBpm(), synced(), maxSeconds); });
    }

    // The delay's tap pattern is the same kind of display and needs the same
    // treatment, or it animates through a division the way the curves did.
    {
        auto synced = [this] { return proc.apvts.getRawParameterValue ("delaySync")->load() > 0.5f; };
        delayTaps.setTimeMapper ([this, synced] (double seconds)
        {
            return pdhybrid::syncedEnvTime (seconds, proc.currentBpm(), synced(),
                                            pdhybrid::Delay::kMaxDelaySeconds);
        });
    }
}

void PDHybridEditor::refreshEnvTimeReadouts()
{
    // Cheap change detection: the readouts only need redrawing when a sync
    // switch flips or the tempo moves, not on every timer tick.
    juce::String state;
    for (const char* id : { "ampEnvSync", "filtEnvSync", "filt2EnvSync", "modEnvSync",
                            "delaySync" })
        state << (proc.apvts.getRawParameterValue (id)->load() > 0.5f ? '1' : '0');
    state << juce::String (juce::roundToInt (proc.currentBpm()));

    if (state == lastEnvSyncState)
        return;
    lastEnvSyncState = state;

    for (auto* k : envTimeKnobs)
        k->slider.updateText();
}

void PDHybridEditor::timerCallback()
{
    refreshEnvTimeReadouts();
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

    // --- selected-object card -------------------------------------------
    selCardTop_ = r.getY();
    r.removeFromTop (10);                       // card title notch
    r.removeFromTop (kInspRowH + 4);            // selected name + value

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

    if (routeRows_.empty())
        r.removeFromTop (26);                   // "nothing modulates this" line

    r.removeFromTop (4);
    inspAddRoute.setBounds (r.removeFromTop (20).reduced (4, 0));
    inspAddRoute.setEnabled (! showDestinations && dest != 0 && firstFreeMatrixSlot() != 0);
    inspAddRoute.setVisible (! showDestinations);
    selCard_ = { 0, selCardTop_, inspector.getWidth(), r.getY() + 6 - selCardTop_ };
    selCard_ = selCard_.reduced (4, 0);

    // --- matrix card, pinned to the bottom ------------------------------
    // Title notch, three lines of hint, then the button — sized explicitly so
    // the text can never run under either.
    auto bottom = inspector.getLocalBounds().reduced (4, 6);
    matrixCard_ = bottom.removeFromBottom (kInspMatrixH);
    inspFullMatrix.setBounds (matrixCard_.reduced (6, 0).withTop (matrixCard_.getBottom() - 24)
                                                        .withHeight (20));

    // --- live sources card ----------------------------------------------
    bottom.removeFromBottom (10);
    const int want = sourceMeters.preferredHeight();
    srcCard_ = bottom.removeFromBottom (juce::jmin (bottom.getHeight(), want + 20));
    auto zone = srcCard_.reduced (5, 0).withTrimmedTop (14).withTrimmedBottom (5);
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
    // Opaque. As a reserved column this only needed an outline, because the page
    // background sat behind it; as a drawer it floats over the cards, so without
    // a fill they read straight through it.
    g.setColour (findColour (pdui::panelCard));
    g.fillRect (full);
    g.setColour (findColour (pdui::panelEdge));
    g.drawRect (full, 1);

    auto r = full.reduced (8, 6);

    g.setFont (pdui::labelFont (*this, 9.0f));
    g.setColour (findColour (pdui::textDim));
    g.drawText ("INSPECTOR", r.removeFromTop (16), juce::Justification::centredLeft);
    r.removeFromTop (18 + 4);                 // the direction toggle (buttons paint themselves)

    const int dest = destForParam (selectedParam);

    // --- the three cards -------------------------------------------------
    // The selected card is amber-edged when it is showing a modulation object,
    // matching the amber rings on the panel.
    const bool amber = showDestinations || dest != 0;
    drawInspCard (g, *this, selCard_,
                  showDestinations ? kSrcNames[selectedSource].toUpperCase() + "  MODULATOR"
                                   : "SELECTED",
                  findColour (pdui::panelCard),
                  amber ? findColour (pdui::liveCol).withAlpha (0.55f) : findColour (pdui::panelEdge),
                  amber ? findColour (pdui::liveCol) : findColour (pdui::textInk));
    drawInspCard (g, *this, srcCard_, "MODULATORS " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
                                 + " LIVE", findColour (pdui::panelCard),
                  findColour (pdui::panelEdge), findColour (pdui::textInk));
    drawInspCard (g, *this, matrixCard_, "MATRIX " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
                                    + " " + juce::String ((int) routes_.size())
                                    + " / " + juce::String (kNumModRows), findColour (pdui::panelCard),
                  findColour (pdui::panelEdge), findColour (pdui::textInk));

    g.setFont (pdui::labelFont (*this, 8.0f));
    g.setColour (findColour (pdui::textDim).withAlpha (0.55f));
    g.drawFittedText ("Amber rings on the panel mark every knob a route points at. "
                      "Click one to inspect its sources.",
                      matrixCard_.reduced (7, 0).withTrimmedTop (13).withTrimmedBottom (26),
                      juce::Justification::topLeft, 3);

    r.removeFromTop (10);                     // selected card's title notch

    // --- what is selected ------------------------------------------------
    auto nameRow = r.removeFromTop (kInspRowH);
    g.setFont (pdui::labelFont (*this, 10.5f));
    if (showDestinations)
    {
        g.setColour (findColour (pdui::liveCol));
        g.drawText (kSrcNames[selectedSource], nameRow, juce::Justification::centredLeft);
        g.setFont (pdui::labelFont (*this, 8.5f));
        g.setColour (findColour (pdui::textDim));
        g.drawText ("modulator", nameRow, juce::Justification::centredRight);
    }
    else
    {
        g.setColour (dest != 0 ? findColour (pdui::liveCol) : findColour (pdui::accentCol));
        g.drawText (selectedParam.isEmpty() ? "Click a knob" : selectedName,
                    nameRow, juce::Justification::centredLeft);
        if (auto* p = proc.apvts.getParameter (selectedParam))
        {
            g.setFont (pdui::valueFont (*this, 9.0f));
            g.setColour (findColour (pdui::textDim));
            g.drawText (p->getCurrentValueAsText(), nameRow, juce::Justification::centredRight);
        }
    }

    // --- the routes, in whichever direction is selected -------------------
    for (const auto& row : routeRows_)
    {
        const auto& rt = routes_[(std::size_t) row.routeIndex];

        g.setFont (pdui::labelFont (*this, 9.0f));
        g.setColour (findColour (pdui::accentCol));
        g.drawText (showDestinations ? kDstNames[rt.dest] : kSrcNames[rt.source],
                    row.text, juce::Justification::centredLeft);
        g.setFont (pdui::valueFont (*this, 9.0f));
        g.setColour (findColour (pdui::textDim));
        g.drawText (juce::String (rt.depth, 2), row.text, juce::Justification::centredRight);

        // Bipolar bar: depth grows left or right of centre.
        g.setColour (findColour (pdui::screenGrid));
        g.fillRect (row.bar);
        const float d = juce::jlimit (-1.0f, 1.0f, rt.depth);
        const int mid = row.bar.getCentreX();
        const int w   = juce::roundToInt (std::abs (d) * row.bar.getWidth() * 0.5f);
        g.setColour (findColour (pdui::liveCol));
        g.fillRect (d >= 0.0f ? mid : mid - w, row.bar.getY(), juce::jmax (1, w), row.bar.getHeight());
    }

    if (routeRows_.empty())
    {
        g.setFont (pdui::labelFont (*this, 9.0f));
        g.setColour (findColour (pdui::textDim).withAlpha (0.65f));
        const juce::String msg = showDestinations
            ? "Goes nowhere yet."
            : (dest == 0 && selectedParam.isNotEmpty() ? "Not a modulation destination."
                                                       : "Nothing modulates this.");
        g.drawFittedText (msg, r.removeFromTop (26), juce::Justification::topLeft, 2);
    }

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
                         bool bipolar, bool live)
    {
        StageEnvelopePanel::Bank b;
        b.name = name;
        b.dest = dest;
        b.bipolar = bipolar;
        b.live = live;
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
              0, true,  true);
    makeBank ("DCW",   "-> PD amount", "dcwEnv",   "dcwEnvAmount",   "dcwEnvSustain",
              1, true,  true);
    makeBank ("MULTI", "-> filter",    "cz",       "czAmount",       "czSustain",
              2, false, false);

    stageEnv.start();
    // Collapsing the numeric rows changes the card's height, so the page it
    // sits on has to be re-laid-out.
    stageEnv.onHeightChanged = [this]
    {
        for (auto& page : pages)
            page->setCustomHeight (&stageEnv, stageEnv.preferredHeight());
    };
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
        // The strip reads as one instrument face, so its readouts are bare text
        // rather than boxed fields. The text colour has to be set explicitly
        // here: dropping the box makes the label fall back to its own default
        // rather than inheriting the look-and-feel's phosphor green.
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        k->slider.setColour (juce::Slider::textBoxTextColourId, findColour (pdui::textInk));
        strip.addAndMakeVisible (k->slider);
        strip.addAndMakeVisible (k->label);
    }

    // The amp envelope's shape sits above its four knobs, exactly as on the page
    // cards, so the strip is not just a row of numbers.
    ampCurve.attach (proc.apvts, "attack", "decay", "sustain", "release");
    strip.addAndMakeVisible (ampCurve);

    // The amp envelope's sync switch sits with the amp envelope's own knobs,
    // where the thing it changes actually is.
    stripAmpSync = &addToggle ("ampEnvSync", "SYNC");
    strip.addAndMakeVisible (*stripAmpSync);

    // Tempo lives in the strip because everything synced reads from it -- the
    // arp, both LFOs, the delay and now the envelopes. It was previously only
    // on the Global page, where it was effectively invisible.
    stripBpm = &addKnob ("internalBpm", "BPM", 1, KnobSize::Normal);
    stripBpm->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    stripBpm->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    stripBpm->slider.setColour (juce::Slider::textBoxTextColourId, findColour (pdui::textInk));
    strip.addAndMakeVisible (stripBpm->slider);
    strip.addAndMakeVisible (stripBpm->label);
    stripTempoMode = &addCombo ("tempoMode", { "HOST", "LOCAL" });
    strip.addAndMakeVisible (*stripTempoMode);

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
        g.setColour (findColour (pdui::panelEdge));
        g.drawRect (b, 1);

        // One card title in a notch, exactly like a page card — no per-cluster
        // captions; the dividers already group the clusters.
        g.setFont (pdui::labelFont (*this, 9.5f));
        {
            const juce::String title = "PERFORMANCE - ALWAYS VISIBLE";
            const int tw = g.getCurrentFont().getStringWidth (title) + 14;
            g.setColour (findColour (pdui::panelCard));
            g.fillRect (b.getX() + 12, b.getY() - 1, tw, kStripCapH);
            g.setColour (findColour (pdui::textInk));
            g.drawText (title, b.getX() + 17, b.getY() - 1, tw, kStripCapH,
                        juce::Justification::centredLeft);
        }
        g.setColour (findColour (pdui::panelEdge));
        for (int x : stripDividers_)
            g.fillRect (x, b.getY() + kStripCapH, 1, b.getHeight() - kStripCapH - 8);

        // Voice count beside the voice-mode combo, so the combo reads as
        // "POLY / 16" the way a hardware display would show it.
        if (stripPoly != nullptr)
            if (auto* p = proc.apvts.getParameter ("polyphony"))
            {
                g.setFont (pdui::valueFont (*this, 9.0f));
                g.setColour (findColour (pdui::textDim));
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
    {   // Curve across the top with the SYNC switch on its right, then the four
        // knobs in a single row underneath. SYNC belongs here, next to the
        // envelope it retimes, rather than on another page.
        auto z = group ("", 248, kStripCurveH + 4 + kCellH);
        auto curveRow = z.removeFromTop (kStripCurveH);
        stripAmpSync->setBounds (curveRow.removeFromRight (40)
                                     .withSizeKeepingCentre (38, 18));
        curveRow.removeFromRight (4);
        ampCurve.setBounds (curveRow);
        z.removeFromTop (4);
        const int cw = z.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            placeKnob (stripKnobs[4 + i], z.removeFromLeft (cw));
    }
    divider();

    {   // Tempo: the mode above, the BPM knob below.
        auto z = group ("", 74, kComboRowH + 4 + kCellH);
        stripTempoMode->setBounds (z.removeFromTop (kComboRowH).reduced (2, 1));
        z.removeFromTop (4);
        placeKnob (stripBpm, z);
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

    // Theme is a display preference, not a patch setting: it is stored as a
    // property on the state tree rather than as a parameter, so it is neither
    // automatable nor swapped by A/B compare.
    // Inspector drawer handle. Stored on the state tree beside the theme: a
    // display preference, not a patch setting.
    addAndMakeVisible (inspectorHandle);
    inspectorOpen_ = (bool) proc.apvts.state.getProperty ("inspectorOpen", false);
    inspectorHandle.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    inspectorHandle.onMouseDown = [this] (const juce::MouseEvent&)
    {
        inspectorOpen_ = ! inspectorOpen_;
        proc.apvts.state.setProperty ("inspectorOpen", inspectorOpen_, nullptr);
        resized();
    };
    inspectorHandle.onPaint = [this] (juce::Graphics& g)
    {
        auto b = inspectorHandle.getLocalBounds();

        // A raised tab, the same material as a card, so it reads as something
        // to grab rather than a sliver of background.
        g.setColour (findColour (pdui::panelCard));
        g.fillRect (b);
        g.setColour (findColour (pdui::panelEdge));
        g.drawRect (b, 1);
        g.setColour (findColour (pdui::panelHighlight));
        g.fillRect (b.getX() + 1, b.getY() + 1, b.getWidth() - 2, 1);

        // A solid arrowhead pointing the way the drawer will move. Filled, not a
        // glyph: at this width a text chevron is a few grey pixels.
        const float cx = (float) b.getCentreX();
        const float cy = (float) b.getCentreY();
        const float w  = 4.5f, h = 7.0f;
        juce::Path arrow;
        if (inspectorOpen_)   // points right: it will close
        {
            arrow.startNewSubPath (cx - w * 0.5f, cy - h);
            arrow.lineTo (cx + w * 0.5f, cy);
            arrow.lineTo (cx - w * 0.5f, cy + h);
        }
        else                  // points left: it will open
        {
            arrow.startNewSubPath (cx + w * 0.5f, cy - h);
            arrow.lineTo (cx - w * 0.5f, cy);
            arrow.lineTo (cx + w * 0.5f, cy + h);
        }
        arrow.closeSubPath();
        g.setColour (findColour (pdui::accentCol));
        g.fillPath (arrow);

        // Grip dots above and below, the universal "drag or click me" texture.
        g.setColour (findColour (pdui::textFaint));
        for (int i = 0; i < 3; ++i)
        {
            const float dy = 22.0f + (float) i * 5.0f;
            g.fillRect (cx - 1.0f, cy - dy, 2.0f, 2.0f);
            g.fillRect (cx - 1.0f, cy + dy, 2.0f, 2.0f);
        }

        // The word itself, running down the tab, so nothing has to be guessed.
        if (b.getHeight() > 150)
        {
            g.saveState();
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                             cx, cy));
            g.setFont (pdui::labelFont (*this, 8.5f));
            g.setColour (findColour (pdui::textDim));
            g.drawText ("INSPECTOR",
                        juce::Rectangle<float> (cx - 90.0f, cy + 46.0f, 180.0f, 12.0f).toNearestInt(),
                        juce::Justification::centred);
            g.restoreState();
        }
    };

    addAndMakeVisible (themeBox);
    for (int i = 0; i < pdtheme::kNumThemes; ++i)
        themeBox.addItem (pdtheme::themeName (static_cast<pdtheme::ThemeId> (i)), i + 1);

    const int storedTheme = (int) proc.apvts.state.getProperty ("themeId", 0);
    themeBox.setSelectedId (juce::jlimit (1, pdtheme::kNumThemes, storedTheme + 1),
                            juce::dontSendNotification);
    applyTheme (static_cast<pdtheme::ThemeId> (themeBox.getSelectedId() - 1));

    themeBox.onChange = [this]
    {
        const auto id = static_cast<pdtheme::ThemeId> (themeBox.getSelectedId() - 1);
        proc.apvts.state.setProperty ("themeId", (int) id, nullptr);
        applyTheme (id);
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
    // Combine gates the PD Wave 2 combo, so the greying has to follow it too.
    proc.apvts.addParameterListener ("oscACombine", this);
    proc.apvts.addParameterListener ("oscBCombine", this);
    proc.apvts.addParameterListener ("filterType", this);
    proc.apvts.addParameterListener ("filter2Type", this);
    updateOscControls();

    // After every knob and its attachment exist: the attachment installs the
    // parameter's own text function, so this has to override it afterwards.
    setupEnvTimeReadouts();

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
        modDepthSlider[i].setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        modDepthSlider[i].setNumDecimalPlacesToDisplay (2);
        // Set explicitly rather than relying on inheritance: these live on an
        // overlay rather than inside a page, and rendered with default colours.
        modDepthSlider[i].setColour (juce::Slider::textBoxTextColourId, findColour (pdui::textInk));
        modDepthSlider[i].setColour (juce::Slider::textBoxBackgroundColourId, findColour (pdui::panelCard));
        modDepthSlider[i].setColour (juce::Slider::textBoxOutlineColourId, findColour (pdui::panelEdge));
        matrixHolder.addAndMakeVisible (modDepthSlider[i]);
        modDepthAtt[i] = std::make_unique<SliderAttachment> (proc.apvts, "mod" + s + "Depth", modDepthSlider[i]);

        modCurveBox[i].addItemList ({ "Lin", "Exp", "S" }, 1);
        matrixHolder.addAndMakeVisible (modCurveBox[i]);
        modCurveAtt[i] = std::make_unique<ComboBoxAttachment> (proc.apvts, "mod" + s + "Curve", modCurveBox[i]);
    }
    matrixHolder.onResized = [this] { layoutMatrix(); };
    matrixHolder.onPaint   = [this] (juce::Graphics& g) { paintMatrix (g); };
    // Clicking the dimmed area outside the card dismisses it.
    matrixHolder.onMouseDown = [this] (const juce::MouseEvent& e)
    {
        if (! matrixPanel_.contains (e.getPosition()))
            showMatrix (false);
    };
    matrixHolder.onKeyPress = [this] (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey) { showMatrix (false); return true; }
        return false;
    };
    matrixHolder.setWantsKeyboardFocus (true);
    matrixHolder.addAndMakeVisible (matrixCloseButton);
    matrixCloseButton.onClick = [this] { showMatrix (false); };
    addChildComponent (matrixHolder);   // hidden until FULL MATRIX is pressed

    // --- Assemble tabs ---
    tabs.setTabBarDepth (30);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, findColour (pdui::panelBg));
    tabs.setColour (juce::TabbedComponent::outlineColourId, findColour (pdui::panelEdge));
    tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::tabTextColourId, findColour (pdui::textDim));
    tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontTextColourId, findColour (pdui::accentCol));
    addAndMakeVisible (tabs);

    struct Page { juce::String name; std::vector<Section*> secs; juce::Component* trailing; juce::String trailingTitle; int trailingH; };

    // Tabs follow the signal path rather than naming categories of component:
    // where a control sits now tells you when it happens. Global settings sit
    // off the path entirely.
    std::vector<Page> layout {
        { "VOICE",
                        { &oscA, &oscB, &mixer, &bassSec, &pluckSec, &unison, &glideSec, &chordSec,
                          &chordIdSec },
                                                                                nullptr, {}, 0 },
        { "SHAPE",
                        { &filter, &filter2, &routingSec, &drive },              nullptr, {}, 0 },
        { "MOD",
                        { &stageEnvSec, &lfo, &lfo2,
                          &modEnv, &vibratoSec, &arpSec },                      nullptr, {}, 0 },
        { "OUT",
                        { &globalEqSec, &delaySec, &reverbSec,
                          &chorusSec, &comp, &stereo },                        nullptr, {}, 0 },
        { "GLOBAL",
                        { &voiceSec, &tuningSec, &globalLfoSec, &tempoSec, &qualitySec },  nullptr, {}, 0 },
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

        tabs.addTab (pg.name, findColour (pdui::panelBg), scroller.get(), false);

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
            rows.push_back ({ m.src, m.name, m.trace, m.bipolar, m.handle });
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
    inspFullMatrix.onClick = [this] { showMatrix (true); };
    addAndMakeVisible (inspector);
    selectParameter ("cutoff");
    refreshModRings();
    startTimerHz (12);   // live meters + ring/route refresh

    // Now every child exists, hand them the live theme. applyTheme ran earlier
    // in this constructor, before any of them had been built, so its loops were
    // no-ops -- without this the construction-time colours would be the only
    // thing dressing them, free to disagree with what a switch produces.
    refreshThemedChildren();

    setResizable (true, true);
    setResizeLimits (1100, 700, 2600, 1600);

    // A fixed opening size, not one derived from the content. The editor used to
    // grow to fit its tallest page so that nothing ever had to be scrolled --
    // which produced a 1382 px window, taller than a 1080p screen, and so the
    // whole editor could not be seen at once. The pages are now packed to fit
    // this size instead; the Viewport underneath stays as a safety net for
    // someone who drags the window smaller than the minimum content.
    setSize (1280, 800);
}

void PDHybridEditor::applyTheme (pdtheme::ThemeId id)
{
    const auto theme = pdui::Theme::fromId (id);
    lnf.setTheme (theme);
    refreshThemedChildren();

    // Pushes a lookAndFeelChanged() through the whole tree, so every child
    // re-reads its colours. Without this the change only lands on repaint of
    // whatever happens to be dirty.
    sendLookAndFeelChange();
    repaint();
}

std::vector<PDHybridEditor::PageFit> PDHybridEditor::measurePageFit (int width, int height)
{
    // Mirrors what resized() hands the tab area, without touching the editor:
    // the strip, tab bar and footer come off the top and bottom, and the page
    // keeps what is left. The Inspector is a drawer that overlays rather than
    // squeezing, so it takes no width here -- which is exactly why it became a
    // drawer.
    static const char* kNames[] = { "VOICE", "SHAPE", "MOD", "OUT", "GLOBAL" };

    const int contentW = width - 2 * kMargin;
    const int available = height - kTopBar - kStripH - tabs.getTabBarDepth()
                        - kFooterH - 6;

    std::vector<PageFit> out;
    for (std::size_t i = 0; i < pages.size(); ++i)
    {
        PageFit f;
        f.name      = i < 5 ? kNames[i] : juce::String ((int) i);
        f.needed    = pages[i]->preferredHeight (contentW);
        f.available = available;
        out.push_back (std::move (f));
    }
    return out;
}

void PDHybridEditor::refreshThemedChildren()
{
    // Everything here is a colour or font that JUCE keeps *on* the component
    // rather than resolving through the LookAndFeel at paint time, so a theme
    // change cannot reach it on its own. Both construction and switching call
    // this, which is the point: when these lived only in the switch path, every
    // new baked colour added at a construction site was correct on load and
    // wrong the moment you changed skin. That bug shipped three times -- tab
    // backgrounds, card titles, and knob captions.
    for (auto& k : knobs)
    {
        k->label.setFont (pdui::labelFont (*this, k->size == KnobSize::Large ? 11.0f : 10.5f));
        // The colour matters as much as the font here and was missed: knob
        // captions kept the skin they were built under, so switching left them
        // near-black on a dark panel or phosphor green on a cream one.
        k->label.setColour (juce::Label::textColourId,
                            k->size == KnobSize::Large ? findColour (pdui::accentCol)
                                                       : findColour (pdui::textDim));
    }

    for (auto* k : stripKnobs)
    {
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        k->slider.setColour (juce::Slider::textBoxTextColourId, findColour (pdui::textInk));
    }
    if (stripBpm != nullptr)
    {
        stripBpm->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        stripBpm->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        stripBpm->slider.setColour (juce::Slider::textBoxTextColourId, findColour (pdui::textInk));
    }

    for (auto& s : modDepthSlider)
    {
        s.setColour (juce::Slider::textBoxTextColourId, findColour (pdui::textInk));
        s.setColour (juce::Slider::textBoxBackgroundColourId, findColour (pdui::panelCard));
        s.setColour (juce::Slider::textBoxOutlineColourId, findColour (pdui::panelEdge));
    }

    tabs.setColour (juce::TabbedComponent::backgroundColourId, findColour (pdui::panelBg));
    tabs.setColour (juce::TabbedComponent::outlineColourId, findColour (pdui::panelEdge));
    tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::tabTextColourId, findColour (pdui::textDim));
    tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontTextColourId, findColour (pdui::accentCol));

    // addTab() copies a background colour into each tab, so those copies are
    // stuck on whichever skin was live when the tabs were built -- which left
    // the whole page area showing the other theme's panel colour after a
    // switch. Re-stamp every tab, not just the TabbedComponent's own id.
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        tabs.setTabBackgroundColour (i, findColour (pdui::panelBg));
}

PDHybridEditor::~PDHybridEditor()
{
    stopTimer();
    cancelPendingUpdate();
    for (auto& k : knobs)
        k->slider.removeMouseListener (this);
    proc.apvts.removeParameterListener ("oscAType", this);
    proc.apvts.removeParameterListener ("oscACombine", this);
    proc.apvts.removeParameterListener ("oscBCombine", this);
    proc.apvts.removeParameterListener ("oscBType", this);
    proc.apvts.removeParameterListener ("filterType", this);
    proc.apvts.removeParameterListener ("filter2Type", this);
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
    auto apply = [&] (Section& sec, const juce::String& id)
    {
        const int type = juce::roundToInt (proc.apvts.getRawParameterValue (id + "Type")->load());
        const bool combine = proc.apvts.getRawParameterValue (id + "Combine")->load() > 0.5f;
        const auto roles = oscKnobRoles (type);
        // Combos are Type / PD Wave / PD Wave 2 / Excite. The two wave choices and
        // the Combine toggle feed only the PD engine; Excite only the Scanned one.
        comboActive (sec.combos[1], type == 0);
        // PD Wave 2 is the alternate cycle of the wave-combine mode, so it is
        // read only while Combine is on -- see PhaseDistortionOscillator, which
        // picks waveB_ only when (combine_ && useB_). Left always-on it looked
        // like a duplicate of the first wave selector that did nothing.
        comboActive (sec.combos[2], type == 0 && combine);
        comboActive (sec.combos[3], roles.exciteActive);
        // Toggles are ON / Combine. Only Combine is PD-only; ON is the mixer
        // mute and has to stay live for every engine -- greying it out would
        // leave a non-PD oscillator with no way to silence it.
        if (sec.toggles.size() > 1)
        {
            auto* combine = sec.toggles[1];
            combine->setEnabled (type == 0);
            combine->setAlpha (type == 0 ? 1.0f : 0.4f);
        }
        // The three shared timbre knobs relabel to the active engine's role and
        // grey out when it doesn't use them.
        knobRole (sec.knobs[0], roles.amountLabel, roles.amountActive);
        knobRole (sec.knobs[1], roles.pwLabel,     roles.pwActive);
        knobRole (sec.knobs[2], roles.engineLabel, roles.engineActive);
    };
    apply (oscA, "oscA");
    apply (oscB, "oscB");

    // The filter MORPH knob is the same shared control: it means a different
    // thing per engine, and the two plain lowpasses don't use it at all.
    auto applyFilter = [&] (Section& sec, const char* typeParam)
    {
        const int type = juce::roundToInt (proc.apvts.getRawParameterValue (typeParam)->load());
        const auto roles = filterKnobRoles (type);
        knobRole (sec.knobs[2], roles.morphLabel, roles.morphActive);
    };
    applyFilter (filter,  "filterType");
    applyFilter (filter2, "filter2Type");
}

void PDHybridEditor::showMatrix (bool shouldShow)
{
    matrixHolder.setVisible (shouldShow);
    if (shouldShow)
    {
        matrixHolder.toFront (true);
        matrixHolder.grabKeyboardFocus();   // so Escape closes it
        layoutMatrix();
    }
}

void PDHybridEditor::layoutMatrix()
{
    // One row per slot, in a centred card. All ten are visible at once — that is
    // the whole point of the view.
    const int cardW = juce::jmin (matrixHolder.getWidth() - 40, 700);
    const int cardH = kHeaderH + 18 + kNumModRows * kMatrixRowH + 34 + 2 * kCardPad;
    matrixPanel_ = juce::Rectangle<int> (0, 0, cardW, cardH)
                       .withCentre (matrixHolder.getLocalBounds().getCentre());

    auto inner = matrixPanel_.reduced (kCardPad + 4, kCardPad);
    inner.removeFromTop (kHeaderH);
    inner.removeFromTop (18);                    // column headings

    for (int i = 0; i < kNumModRows; ++i)
    {
        auto row = inner.removeFromTop (kMatrixRowH).reduced (0, 2);
        row.removeFromLeft (22);                 // slot number, painted
        modSrcBox[i].setBounds   (row.removeFromLeft (128));
        row.removeFromLeft (6);
        modDestBox[i].setBounds  (row.removeFromLeft (128));
        row.removeFromLeft (6);
        modCurveBox[i].setBounds (row.removeFromLeft (56));
        row.removeFromLeft (8);
        modDepthSlider[i].setBounds (row);
    }

    inner.removeFromTop (6);
    matrixCloseButton.setBounds (inner.removeFromTop (22).removeFromRight (72));
}

void PDHybridEditor::paintMatrix (juce::Graphics& g)
{
    // Dim everything behind, then the card itself.
    g.fillAll (juce::Colour (0xcc000000));

    g.setColour (findColour (pdui::panelCard));
    g.fillRect (matrixPanel_);
    g.setColour (findColour (pdui::accentCol));
    g.drawRect (matrixPanel_, 1);

    auto head = matrixPanel_.reduced (kCardPad + 4, kCardPad).removeFromTop (kHeaderH);
    g.setFont (pdui::labelFont (*this, 11.0f));
    g.setColour (findColour (pdui::textInk));
    g.drawText ("MODULATION MATRIX", head, juce::Justification::centredLeft);
    g.setFont (pdui::labelFont (*this, 9.0f));
    g.setColour (findColour (pdui::textDim));
    g.drawText (juce::String ((int) routes_.size()) + " / " + juce::String (kNumModRows)
                    + " SLOTS IN USE",
                head, juce::Justification::centredRight);

    // Column headings, aligned to the widgets beneath them.
    g.setFont (pdui::labelFont (*this, 8.0f));
    g.setColour (findColour (pdui::textDim).withAlpha (0.7f));
    if (kNumModRows > 0)
    {
        const int y = modSrcBox[0].getY() - 15;
        g.drawText ("SOURCE",      modSrcBox[0].getX(),   y, 128, 12, juce::Justification::centredLeft);
        g.drawText ("DESTINATION", modDestBox[0].getX(),  y, 128, 12, juce::Justification::centredLeft);
        g.drawText ("CURVE",       modCurveBox[0].getX(), y, 56,  12, juce::Justification::centredLeft);
        g.drawText ("DEPTH",       modDepthSlider[0].getX(), y,
                    modDepthSlider[0].getWidth(), 12, juce::Justification::centredLeft);
    }

    // Slot numbers; an in-use slot is amber, matching the rings on the panel.
    for (int i = 0; i < kNumModRows; ++i)
    {
        bool used = false;
        for (const auto& rt : routes_)
            if (rt.slot == i + 1) { used = true; break; }

        g.setFont (pdui::valueFont (*this, 9.0f));
        g.setColour (used ? findColour (pdui::liveCol) : findColour (pdui::textDim).withAlpha (0.45f));
        g.drawText (juce::String (i + 1),
                    modSrcBox[i].getX() - 22, modSrcBox[i].getY(), 18, modSrcBox[i].getHeight(),
                    juce::Justification::centredRight);
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
    g.setColour (findColour (pdui::panelEdge));
    g.fillRect (0, 0, r.getWidth(), 1);

    static const char* names[] = { "VOICE", "SHAPE", "MOD", "OUT", "GLOBAL" };
    const int page = juce::jlimit (0, 4, tabs.getCurrentTabIndex());

    g.setFont (pdui::labelFont (*this, 9.0f));
    g.setColour (findColour (pdui::textDim));
    g.drawText (names[page], r.withTrimmedLeft (6), juce::Justification::centredLeft);

    const int active = static_cast<int> (routes_.size());
    g.setColour (active > 0 ? findColour (pdui::liveCol) : findColour (pdui::textDim).withAlpha (0.6f));
    g.drawText (juce::String (active) + " MOD ROUTE" + (active == 1 ? "" : "S") + " ACTIVE",
                r.withTrimmedRight (140), juce::Justification::centredRight);

    // Reverse lookup of whatever is sounding, centred. Amber because it is live
    // state rather than a setting -- the same language the mod meters use.
    int notes[32];
    const int n = proc.soundingNotes (notes, 32);
    if (n > 0)
    {
        char nameBuf[pdhybrid::ChordNamer::kMaxName] = { 0 };
        pdhybrid::ChordNamer::name (notes, n, nameBuf, pdhybrid::ChordNamer::kMaxName);

        g.setFont (pdui::valueFont (*this, 12.0f));
        g.setColour (findColour (pdui::liveCol));
        g.drawText (nameBuf, r, juce::Justification::centred);
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

void PDHybridEditor::refreshWavetableButton()
{
    const auto name = proc.wavetableName();
    wavetableButton.setButtonText ("WT: " + (name.isEmpty() ? juce::String ("default") : name));
}

void PDHybridEditor::chooseWavetable()
{
    wavetableChooser = std::make_unique<juce::FileChooser> (
        "Load a wavetable (single-cycle frames, 2048 samples each)",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff");

    wavetableChooser->launchAsync (juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File{})
                return;   // cancelled

            if (proc.loadWavetable (file))
            {
                refreshWavetableButton();
                oscACycle.setWavetable (proc.wavetableSet());
                oscBCycle.setWavetable (proc.wavetableSet());
            }
            else
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Wavetable",
                    "Could not read " + file.getFileName()
                        + ".\nThe previous table is still loaded.");
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
    x -= 126; themeBox.setBounds   (x, y, 120, 26);
    x -= 70;  saveButton.setBounds (x, y, 64, 26);
    x -= 58;  abButton.setBounds   (x, y, 52, 26);
    x -= 32;  nextButton.setBounds (x, y, 28, 26);
    x -= 32;  prevButton.setBounds (x, y, 28, 26);
    x -= 226; presetButton.setBounds (x, y, 220, 26);

    strip.setBounds (r.removeFromTop (kStripH).reduced (kMargin, 4));
    footer.setBounds (r.removeFromBottom (kFooterH).reduced (kMargin, 0));
    // The Inspector is a drawer, not a reserved column. It used to hold 236 px
    // on every page whether or not anything was selected; the pages need that
    // width far more than a panel that is empty most of the time. Open, it
    // overlays the right-hand side rather than squeezing the cards, so the
    // layout underneath never reflows when it is toggled.
    tabs.setBounds (r);

    // The drawer and its handle. The handle rides the drawer's outer edge, so it
    // is always the thing you reach for on that side whether the drawer is open
    // or shut, and the chevron points the way it will move.
    constexpr int kHandleW = 20;
    auto drawer = r.withTrimmedTop (tabs.getTabBarDepth());

    inspector.setVisible (inspectorOpen_);
    if (inspectorOpen_)
    {
        auto panel = drawer.removeFromRight (kInspW);
        inspector.setBounds (panel.reduced (4, 4));
        inspector.toFront (false);
    }

    inspectorHandle.setBounds (drawer.getRight() - kHandleW,
                               drawer.getY() + 4, kHandleW, drawer.getHeight() - 8);
    inspectorHandle.toFront (false);
    inspectorHandle.repaint();
    // The overlay covers everything below the title bar, so the dimmed backdrop
    // reads as "the rest of the editor is inactive".
    matrixHolder.setBounds (getLocalBounds().withTrimmedTop (kTopBar));
}

void PDHybridEditor::paint (juce::Graphics& g)
{
    g.fillAll (findColour (pdui::panelBg));

    // Fine vertical grain in the moulding. Transparent on skins that do not
    // want it, and cheap enough to draw every frame.
    const auto grain = findColour (pdui::panelGrain);
    if (! grain.isTransparent())
    {
        g.setColour (grain);
        for (int gx = 0; gx < getWidth(); gx += 3)
            g.fillRect (gx, 0, 1, getHeight());
    }

    auto top = getLocalBounds().removeFromTop (kTopBar);
    g.setColour (findColour (pdui::textInk));
    g.setFont (pdui::labelFont (*this, 18.0f));
    g.drawText ("  PD_HYBRID", top, juce::Justification::centredLeft);
    g.setColour (findColour (pdui::textDim));
    g.setFont (pdui::labelFont (*this, 11.0f));
    g.drawText ("v7", top.withTrimmedLeft (150), juce::Justification::centredLeft);

    // The coloured legend stripe that ran under the badge on every panel of the
    // period. It is the theme's accent doing its one clearly-signposted job.
    if (pdui::themeOf (*this).traits.legendStripe)
    {
        auto stripe = top.removeFromBottom (3);
        g.setColour (findColour (pdui::accentCol));
        g.fillRect (stripe);
    }

    g.setColour (findColour (pdui::panelEdge));
    g.fillRect (0, kTopBar - 1, getWidth(), 1);
}
