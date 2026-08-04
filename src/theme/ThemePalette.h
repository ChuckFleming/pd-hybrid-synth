#pragma once

#include <cstdint>

namespace pdtheme {

/** Which skin. Appended to, never reordered: the value is persisted. */
enum class ThemeId { Panel1985 = 0, PhosphorMkII = 1 };
constexpr int kNumThemes = 2;

/**
    A skin's colours as plain 0xAARRGGBB integers.

    Deliberately free of JUCE so the tests -- which do not link JUCE -- can
    check contrast. Colour roles are named for the job they do, not for what
    they look like, so a light theme and a dark theme fill the same slots.

    The split between `bg / card / ink` (the plastic panel) and
    `screenBg / screenTrace / screenDim` (the display windows set into it) is
    the important one: on Panel 1985 the panel is light and the screens are
    still dark, exactly like the hardware.
*/
struct Palette
{
    // Panel surface
    std::uint32_t bg;            // the moulded panel itself
    std::uint32_t card;          // a raised section on the panel
    std::uint32_t edge;          // engraved section outlines
    std::uint32_t highlight;     // top-edge sheen on a raised section
    std::uint32_t grain;         // the fine vertical texture in the plastic

    // Panel text
    std::uint32_t ink;           // silkscreen legends and readouts
    std::uint32_t dim;           // secondary labels
    std::uint32_t faint;         // tertiary chrome

    // Signalling
    std::uint32_t accent;        // "this is a control" -- the legend stripe
    std::uint32_t live;          // "this value is moving right now"

    // Display windows
    std::uint32_t screenBg;
    std::uint32_t screenTrace;
    std::uint32_t screenDim;     // captions and grid inside a window
    std::uint32_t screenGrid;

    // Fields
    std::uint32_t selBg;
    std::uint32_t selInk;

    // Indicators
    std::uint32_t ledOnBg;
    std::uint32_t ledOnInk;
    std::uint32_t ledOnEdge;

    // Tabs
    std::uint32_t tabBg;
    std::uint32_t tabOnBg;
    std::uint32_t tabOnInk;
    std::uint32_t tabOnEdge;

    // Knobs
    std::uint32_t knobTrack;     // the unfilled part of the value arc
    std::uint32_t knobFill;      // the filled part
    std::uint32_t knobCap1;      // cap gradient: lit edge
    std::uint32_t knobCap2;      // cap gradient: body
    std::uint32_t knobCap3;      // cap gradient: shadowed edge
    std::uint32_t knobPointer;
};

const Palette& paletteFor (ThemeId id) noexcept;
const char*    themeName  (ThemeId id) noexcept;

/** WCAG relative-contrast ratio between two opaque ARGB colours, 1.0 .. 21.0. */
double contrastRatio (std::uint32_t a, std::uint32_t b) noexcept;

} // namespace pdtheme
