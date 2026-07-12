#include "CrtOverlay.h"

namespace {
// --- CRT tuning. Strong enough that the whole screen always reads as an old
//     monitor, while staying readable; nudge these to taste. ---
constexpr int   kScanPeriod   = 3;      // scanline group repeats every N pixels
constexpr float kScanDark     = 0.30f;  // darkness of the hard scan line (0..1)
constexpr float kScanSoft     = 0.12f;  // darkness of the soft falloff row

constexpr float kTintAlpha    = 0.10f;  // faint green phosphor wash over everything
constexpr float kVignetteAlpha = 0.55f; // corner darkening at the very edge
constexpr float kVignetteInner = 0.38f; // fraction of the radius that stays clear

// Green cast shared by the scanlines, tint and vignette (phosphor look).
const juce::Colour kPhosphorDark { 0xff02120a };
const juce::Colour kPhosphorTint { 0xff0aff7a };
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

void CrtOverlay::paint (juce::Graphics& g)
{
    if (! enabled_)
        return;

    const auto clip = g.getClipBounds();

    // Faint green phosphor wash so even black areas read as a lit screen.
    g.setColour (kPhosphorTint.withAlpha (kTintAlpha));
    g.fillRect (clip);

    // Scanlines over the (possibly clipped) region only.
    if (scanlineTile_.isValid())
    {
        g.setTiledImageFill (scanlineTile_, 0, 0, 1.0f);
        g.fillRect (clip);
    }

    // Vignette (drawImageAt is clipped to the repaint region for free).
    if (vignette_.isValid())
        g.drawImageAt (vignette_, 0, 0);
}
