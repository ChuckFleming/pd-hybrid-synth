#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "theme/ThemePalette.h"

namespace pdui {

/**
    Custom colour ids. Every painting site in the editor reads one of these
    through Component::findColour, which resolves against the component's
    LookAndFeel -- so a theme change is per-editor-instance and propagates
    without any component needing to know a theme exists.

    The base is chosen to sit clear of JUCE's own id ranges.
*/
enum ColourIds
{
    panelBg = 0x50d0000,
    panelCard,
    panelEdge,
    panelHighlight,
    panelGrain,

    textInk,
    textDim,
    textFaint,

    accentCol,
    liveCol,

    screenBg,
    screenTrace,
    screenDim,
    screenGrid,

    fieldBg,
    fieldInk,

    ledOnBg,
    ledOnInk,
    ledOnEdge,

    tabBg,
    tabOnBg,
    tabOnInk,
    tabOnEdge,

    knobTrack,
    knobFill,
    knobCap1,
    knobCap2,
    knobCap3,
    knobPointer,
};

/** Non-colour parts of a skin. Colours ride on ColourIds; these cannot. */
struct Traits
{
    /** True when labels are set in the UI sans and values in mono (Panel 1985).
        False when everything is mono (Phosphor). */
    bool mixedTypography = true;
    /** True to draw knobs as a moulded cap with a pointer; false for the thin
        ring-and-dot of the terminal look. */
    bool physicalKnobs = true;
    /** True to rule a coloured legend stripe under the wordmark. */
    bool legendStripe = true;
    /** Where the CRT overlay may draw. */
    enum class Crt { Never, ScreensOnly, WholePanel };
    Crt crt = Crt::ScreensOnly;
    /** Corner radius for cards and fields, in pixels. */
    float cornerRadius = 2.0f;
};

/** A skin: the palette adapted to juce::Colour, plus its traits. */
struct Theme
{
    pdtheme::ThemeId id = pdtheme::ThemeId::Panel1985;
    Traits traits {};

    static Theme fromId (pdtheme::ThemeId id);

    /** Pushes every colour onto `lnf` under the ids above, and also onto the
        stock JUCE ids so ComboBox / PopupMenu / TextButton / Slider restyle
        without any bespoke drawing. */
    void applyTo (juce::LookAndFeel& lnf) const;

private:
    const pdtheme::Palette* pal_ = nullptr;
};

/** The theme in force for `c`, via its LookAndFeel. Falls back to Panel 1985
    if the component is not under a SynthLookAndFeel, so painting never has to
    null-check. */
const Theme& themeOf (const juce::Component& c) noexcept;

/** Silkscreen legends and headings. Sans on Panel 1985, mono on Phosphor. */
juce::Font labelFont (const juce::Component& c, float height, bool bold = false);
/** Numeric readouts. Always mono, so digits line up in both skins. */
juce::Font valueFont (const juce::Component& c, float height, bool bold = false);

} // namespace pdui
