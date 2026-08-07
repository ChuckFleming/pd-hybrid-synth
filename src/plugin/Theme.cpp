#include "Theme.h"
#include "SynthLookAndFeel.h"

namespace pdui {

namespace {

juce::Colour c (std::uint32_t argb) { return juce::Colour (argb); }

const Theme& fallbackTheme()
{
    static const Theme t = Theme::fromId (pdtheme::ThemeId::Panel1985);
    return t;
}

} // namespace

Theme Theme::fromId (pdtheme::ThemeId id)
{
    Theme t;
    t.id   = id;
    t.pal_ = &pdtheme::paletteFor (id);

    if (id == pdtheme::ThemeId::PhosphorMkII)
    {
        t.traits.mixedTypography = false;
        t.traits.physicalKnobs   = false;
        t.traits.legendStripe    = false;
        t.traits.cornerRadius    = 2.0f;
    }
    else
    {
        t.traits.mixedTypography = true;
        t.traits.physicalKnobs   = true;
        t.traits.legendStripe    = true;
        t.traits.cornerRadius    = 2.0f;
    }
    return t;
}

void Theme::applyTo (juce::LookAndFeel& lnf) const
{
    // A default-constructed Theme (never given a palette via fromId()) has a
    // null pal_. Nothing builds one that way today, but this guards the next
    // caller that does instead of crashing on the dereference below.
    jassert (pal_ != nullptr);
    if (pal_ == nullptr)
        return;

    const auto& p = *pal_;

    lnf.setColour (panelBg,        c (p.bg));
    lnf.setColour (panelCard,      c (p.card));
    lnf.setColour (panelEdge,      c (p.edge));
    lnf.setColour (panelHighlight, c (p.highlight));
    lnf.setColour (panelGrain,     c (p.grain));

    lnf.setColour (textInk,   c (p.ink));
    lnf.setColour (textDim,   c (p.dim));
    lnf.setColour (textFaint, c (p.faint));

    lnf.setColour (accentCol, c (p.accent));
    lnf.setColour (liveCol,   c (p.live));

    lnf.setColour (pdui::screenBg,    c (p.screenBg));
    lnf.setColour (pdui::screenTrace, c (p.screenTrace));
    lnf.setColour (pdui::screenDim,   c (p.screenDim));
    lnf.setColour (pdui::screenGrid,  c (p.screenGrid));

    lnf.setColour (fieldBg,  c (p.selBg));
    lnf.setColour (fieldInk, c (p.selInk));

    lnf.setColour (pdui::ledOnBg,   c (p.ledOnBg));
    lnf.setColour (pdui::ledOnInk,  c (p.ledOnInk));
    lnf.setColour (pdui::ledOnEdge, c (p.ledOnEdge));

    lnf.setColour (pdui::tabBg,     c (p.tabBg));
    lnf.setColour (pdui::tabOnBg,   c (p.tabOnBg));
    lnf.setColour (pdui::tabOnInk,  c (p.tabOnInk));
    lnf.setColour (pdui::tabOnEdge, c (p.tabOnEdge));

    lnf.setColour (pdui::knobTrack,   c (p.knobTrack));
    lnf.setColour (pdui::knobFill,    c (p.knobFill));
    lnf.setColour (pdui::knobCap1,    c (p.knobCap1));
    lnf.setColour (pdui::knobCap2,    c (p.knobCap2));
    lnf.setColour (pdui::knobCap3,    c (p.knobCap3));
    lnf.setColour (pdui::knobPointer, c (p.knobPointer));

    // Stock JUCE ids, so the built-in widgets follow without bespoke drawing.
    lnf.setColour (juce::Slider::textBoxTextColourId,       c (p.ink));
    lnf.setColour (juce::Slider::textBoxBackgroundColourId, c (p.selBg));
    lnf.setColour (juce::Slider::textBoxOutlineColourId,    c (p.edge));

    lnf.setColour (juce::ComboBox::backgroundColourId, c (p.selBg));
    lnf.setColour (juce::ComboBox::textColourId,       c (p.selInk));
    lnf.setColour (juce::ComboBox::outlineColourId,    c (p.edge));
    lnf.setColour (juce::ComboBox::arrowColourId,      c (p.dim));

    lnf.setColour (juce::PopupMenu::backgroundColourId,            c (p.card));
    lnf.setColour (juce::PopupMenu::textColourId,                  c (p.ink));
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, c (p.accent));
    lnf.setColour (juce::PopupMenu::highlightedTextColourId,       c (p.ledOnInk));

    lnf.setColour (juce::TextButton::buttonColourId,  c (p.selBg));
    lnf.setColour (juce::TextButton::textColourOffId, c (p.dim));
    lnf.setColour (juce::TextButton::textColourOnId,  c (p.ink));

    lnf.setColour (juce::Label::textColourId,           c (p.dim));
    lnf.setColour (juce::Label::backgroundColourId,     juce::Colours::transparentBlack);
    lnf.setColour (juce::TooltipWindow::backgroundColourId, c (p.card));
    lnf.setColour (juce::TooltipWindow::textColourId,       c (p.ink));
}

const Theme& themeOf (const juce::Component& comp) noexcept
{
    if (auto* lnf = dynamic_cast<SynthLookAndFeel*> (&comp.getLookAndFeel()))
        return lnf->theme();
    return fallbackTheme();
}

juce::Font labelFont (const juce::Component& comp, float height, bool bold)
{
    const auto& t = themeOf (comp);
    const auto flags = bold ? juce::Font::bold : juce::Font::plain;
    if (! t.traits.mixedTypography)
        return juce::Font (juce::Font::getDefaultMonospacedFontName(), height, flags);
    return juce::Font (juce::Font::getDefaultSansSerifFontName(), height, flags);
}

juce::Font valueFont (const juce::Component&, float height, bool bold)
{
    // Always monospaced: a readout that reflows as digits change is worse than
    // one that does not match the labels.
    return juce::Font (juce::Font::getDefaultMonospacedFontName(), height,
                       bold ? juce::Font::bold : juce::Font::plain);
}

} // namespace pdui
