#include "CrtOverlay.h"

namespace {
// --- CRT tuning. The screen should read as an old monitor without washing out.
//     The tint is the brightness control and the scanlines are the contrast
//     control, and they pull against each other: a wash laid over everything
//     lifts the blacks, so turning it up makes the panel both brighter and
//     flatter. Keeping the wash low and the scanlines deep buys a darker tube
//     that still has bite. Nudge to taste. ---
constexpr int   kScanPeriod   = 3;      // scanline group repeats every N pixels
constexpr float kScanDark     = 0.44f;  // darkness of the hard scan line (0..1)
constexpr float kScanSoft     = 0.20f;  // darkness of the soft falloff row

constexpr float kTintAlpha    = 0.035f; // faint green phosphor wash over everything
constexpr float kVignetteAlpha = 0.68f; // corner darkening at the very edge
constexpr float kVignetteInner = 0.30f; // fraction of the radius that stays clear

// Green cast shared by the scanlines, tint and vignette (phosphor look).
// The tint is a dim phosphor rather than a bright one: at this alpha a
// saturated green reads as haze sitting in front of the panel instead of a
// tube the panel is being displayed on.
const juce::Colour kPhosphorDark { 0xff02120a };
const juce::Colour kPhosphorTint { 0xff0f8f52 };
}

CrtOverlay::CrtOverlay()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
    rebuildScanlineTile();
}

void CrtOverlay::setEffectEnabled (bool shouldBeEnabled)
{
    if (enabled_ == shouldBeEnabled)
        return;

    enabled_ = shouldBeEnabled;
    repaint();
}

void CrtOverlay::resized()
{
    rebuildVignette();
}

void CrtOverlay::setScope (pdui::Traits::Crt scope)
{
    if (scope_ == scope)
        return;

    scope_ = scope;
    repaint();
}

void CrtOverlay::setScreenAreas (const juce::Array<juce::Rectangle<int>>& areas)
{
    screens_ = areas;
    repaint();
}

void CrtOverlay::rebuildScanlineTile()
{
    // kScanPeriod tall: one hard dark row + one soft falloff row, the rest clear.
    // Tiling this over any rectangle reproduces evenly spaced scanlines.
    scanlineTile_ = juce::Image (juce::Image::ARGB, 4, kScanPeriod, true);
    juce::Graphics g (scanlineTile_);
    g.setColour (kPhosphorDark.withAlpha (kScanDark));
    g.fillRect (0, 0, 4, 1);
    g.setColour (kPhosphorDark.withAlpha (kScanSoft));
    g.fillRect (0, 1, 4, 1);
}

void CrtOverlay::rebuildVignette()
{
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0)
    {
        vignette_ = juce::Image();
        return;
    }

    vignette_ = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (vignette_);

    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float radius = std::sqrt (cx * cx + cy * cy);   // reach the corners

    juce::ColourGradient grad (juce::Colours::transparentBlack, cx, cy,
                               kPhosphorDark.withAlpha (kVignetteAlpha), cx, cy - radius,
                               true /* radial */);
    // Stay clear through the centre, then ramp up toward the edge.
    grad.addColour (kVignetteInner, juce::Colours::transparentBlack);
    g.setGradientFill (grad);
    g.fillRect (0, 0, w, h);
}

void CrtOverlay::paintInto (juce::Graphics& g, juce::Rectangle<int> clip)
{
    // Faint green phosphor wash so even black areas read as a lit screen.
    g.setColour (kPhosphorTint.withAlpha (kTintAlpha));
    g.fillRect (clip);

    // Scanlines over the (possibly clipped) region only.
    if (scanlineTile_.isValid())
    {
        g.setTiledImageFill (scanlineTile_, 0, 0, 1.0f);
        g.fillRect (clip);
    }
}

void CrtOverlay::paint (juce::Graphics& g)
{
    if (! enabled_ || scope_ == pdui::Traits::Crt::Never)
        return;

    if (scope_ == pdui::Traits::Crt::WholePanel)
    {
        paintInto (g, g.getClipBounds());
        if (vignette_.isValid())
            g.drawImageAt (vignette_, 0, 0);
        return;
    }

    // ScreensOnly: clip to each display window in turn. No vignette -- that is
    // a whole-tube effect and makes no sense inside a small window.
    for (const auto& area : screens_)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area);
        paintInto (g, area);
    }
}
