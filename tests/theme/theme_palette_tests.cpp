#include <catch2/catch_test_macros.hpp>

#include "theme/ThemePalette.h"

#include <string>

using namespace pdtheme;

namespace {

// Every pair that must stay readable, named so a failure says what broke.
struct Pair { const char* what; std::uint32_t Palette::* ink; std::uint32_t Palette::* ground; };

const Pair kPairs[] = {
    { "body text on panel",      &Palette::ink,        &Palette::bg },
    { "body text on card",       &Palette::ink,        &Palette::card },
    { "dim label on card",       &Palette::dim,        &Palette::card },
    // Labels land on the bare panel as often as on a card -- the strip and the
    // page background are both `bg`. Only the card pairing was checked here,
    // and Panel 1985's dim sat at 4.21 on the panel while passing at 4.80 on a
    // card, so the floor this file claims to enforce was not enforced.
    { "dim label on panel",      &Palette::dim,        &Palette::bg },
    { "value on card",           &Palette::ink,        &Palette::card },
    { "dropdown text on field",  &Palette::selInk,     &Palette::selBg },
    { "screen caption on well",  &Palette::screenDim,  &Palette::screenBg },
    { "trace on well",           &Palette::screenTrace,&Palette::screenBg },
    { "lit LED text on LED",     &Palette::ledOnInk,   &Palette::ledOnBg },
    { "active tab text on tab",  &Palette::tabOnInk,   &Palette::tabOnBg },
    { "knob pointer on cap",     &Palette::knobPointer,&Palette::knobCap2 },
};

} // namespace

TEST_CASE ("Every theme keeps its text legible", "[theme]")
{
    // 4.5:1 is the WCAG AA floor for body text. These are small labels on a
    // control surface, so anything below it is a real defect -- both mockup
    // bugs (dark text on a dark field, a trace the colour of its background)
    // would have failed here.
    for (int t = 0; t < kNumThemes; ++t)
    {
        const auto id = static_cast<ThemeId> (t);
        const auto& p = paletteFor (id);

        for (const auto& pair : kPairs)
        {
            const double ratio = contrastRatio (p.*(pair.ink), p.*(pair.ground));
            INFO (themeName (id) << " -- " << pair.what << " ratio " << ratio);
            REQUIRE (ratio >= 4.5);
        }
    }
}

TEST_CASE ("Faint text stays above the large-text floor", "[theme]")
{
    // `faint` is deliberately quiet -- unlit LED captions, footer chrome. It is
    // held to 3:1 rather than 4.5:1, but it must not vanish.
    for (int t = 0; t < kNumThemes; ++t)
    {
        const auto& p = paletteFor (static_cast<ThemeId> (t));
        INFO (themeName (static_cast<ThemeId> (t)) << " faint on card");
        REQUIRE (contrastRatio (p.faint, p.card) >= 3.0);
        INFO (themeName (static_cast<ThemeId> (t)) << " faint on panel");
        REQUIRE (contrastRatio (p.faint, p.bg) >= 3.0);
    }
}

TEST_CASE ("Accent and live colours are distinguishable from each other", "[theme]")
{
    // The accent means "a control" and live means "a value moving". If they are
    // near-identical the signalling collapses, which is the core complaint about
    // the old all-green look.
    for (int t = 0; t < kNumThemes; ++t)
    {
        const auto& p = paletteFor (static_cast<ThemeId> (t));
        INFO (themeName (static_cast<ThemeId> (t)) << " accent vs live");
        REQUIRE (contrastRatio (p.accent, p.live) >= 1.25);
    }
}

TEST_CASE ("Theme ids map to stable names", "[theme]")
{
    REQUIRE (std::string (themeName (ThemeId::Panel1985))    == "Panel 1985");
    REQUIRE (std::string (themeName (ThemeId::PhosphorMkII)) == "Phosphor Mk II");
    REQUIRE (kNumThemes == 2);
}

TEST_CASE ("contrastRatio matches known values", "[theme]")
{
    REQUIRE (contrastRatio (0xffffffff, 0xff000000) > 20.9);   // white on black = 21
    REQUIRE (contrastRatio (0xff000000, 0xff000000) < 1.01);   // identical = 1
}
