#include "ThemePalette.h"

#include <cmath>

namespace pdtheme {

namespace {

// Panel 1985 "Cream": warm grey plastic, vermilion legend stripe, dark metal
// knob caps with a light pointer, dark display windows set into the panel.
const Palette kPanel1985 = {
    /* bg        */ 0xffc9c4b8,
    /* card      */ 0xffd5d1c6,
    /* edge      */ 0xffa09a8c,
    /* highlight */ 0x80ffffff,
    /* grain     */ 0x24ffffff,

    /* ink       */ 0xff26251f,
    /* dim       */ 0xff5a564c,
    /* faint     */ 0xff6e6a5f,

    /* accent    */ 0xffb4472e,
    /* live      */ 0xff8a3221,

    /* screenBg    */ 0xff2c2c2a,
    /* screenTrace */ 0xffed8058,
    /* screenDim   */ 0xffb0aca2,
    /* screenGrid  */ 0xff4f4f4c,

    /* selBg     */ 0xffe0dbcd,
    /* selInk    */ 0xff26251f,

    /* ledOnBg   */ 0xffb4472e,
    /* ledOnInk  */ 0xfff5efe2,
    /* ledOnEdge */ 0xff8a3221,

    /* tabBg     */ 0xffbdb8aa,
    /* tabOnBg   */ 0xff26251f,
    /* tabOnInk  */ 0xffe8e3d6,
    /* tabOnEdge */ 0xff26251f,

    /* knobTrack   */ 0xffa8a294,
    /* knobFill    */ 0xffb4472e,
    /* knobCap1    */ 0xff56544c,
    /* knobCap2    */ 0xff2b2a25,
    /* knobCap3    */ 0xff191815,
    /* knobPointer */ 0xffe8e3d6,
};

// Phosphor Mk II: the old CRT identity, but amber owns live state so the
// hierarchy reads. Screens and panel share a ground here -- the whole surface
// is the screen, which is the point of the look.
const Palette kPhosphor = {
    /* bg        */ 0xff04120c,
    /* card      */ 0xff07190f,
    /* edge      */ 0xff154a30,
    /* highlight */ 0x00000000,
    /* grain     */ 0x00000000,

    /* ink       */ 0xff7df0ad,
    /* dim       */ 0xff4fb37c,
    /* faint     */ 0xff3d8e63,

    /* accent    */ 0xff7df0ad,
    /* live      */ 0xffffb340,

    /* screenBg    */ 0xff020c07,
    /* screenTrace */ 0xff7df0ad,
    /* screenDim   */ 0xff4a9a69,
    /* screenGrid  */ 0xff0e3521,

    /* selBg     */ 0xff020c07,
    /* selInk    */ 0xff7df0ad,

    /* ledOnBg   */ 0xff0d3520,
    /* ledOnInk  */ 0xffffb340,
    /* ledOnEdge */ 0xff7a5a20,

    /* tabBg     */ 0xff04120c,
    /* tabOnBg   */ 0xff0b2a1a,
    /* tabOnInk  */ 0xffb6ffd6,
    /* tabOnEdge */ 0xff2b7a51,

    /* knobTrack   */ 0xff0e3521,
    /* knobFill    */ 0xff7df0ad,
    /* knobCap1    */ 0xff0a2416,
    /* knobCap2    */ 0xff04120c,
    /* knobCap3    */ 0xff020a06,
    /* knobPointer */ 0xffffb340,
};

/** One sRGB channel, linearised, per the WCAG definition. */
double linearise (double c) noexcept
{
    return c <= 0.03928 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
}

double luminance (std::uint32_t argb) noexcept
{
    const double r = linearise (((argb >> 16) & 0xff) / 255.0);
    const double g = linearise (((argb >>  8) & 0xff) / 255.0);
    const double b = linearise (( argb        & 0xff) / 255.0);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

} // namespace

const Palette& paletteFor (ThemeId id) noexcept
{
    return id == ThemeId::PhosphorMkII ? kPhosphor : kPanel1985;
}

const char* themeName (ThemeId id) noexcept
{
    return id == ThemeId::PhosphorMkII ? "Phosphor Mk II" : "Panel 1985";
}

double contrastRatio (std::uint32_t a, std::uint32_t b) noexcept
{
    const double la = luminance (a), lb = luminance (b);
    const double hi = la > lb ? la : lb;
    const double lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

} // namespace pdtheme
