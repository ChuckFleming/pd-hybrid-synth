#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    A transparent, click-through overlay that dresses the editor up as an old CRT
    monitor to complete the "CZ Terminal" look:

      * a faint green phosphor wash so even black areas read as a lit screen,
      * static horizontal scanlines (a small cached tile, fillable at any size),
      * a soft corner vignette (cached, rebuilt on resize).

    The effect is entirely static — it paints only on resize or when toggled, so
    it costs nothing while idle. The overlay never intercepts the mouse, so every
    control beneath keeps working.
*/
class CrtOverlay : public juce::Component
{
public:
    CrtOverlay();

    // Turn the whole effect on/off. When off the overlay clears itself so
    // nothing is drawn over the editor.
    void setEffectEnabled (bool shouldBeEnabled);
    bool isEffectEnabled() const noexcept { return enabled_; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuildScanlineTile();
    void rebuildVignette();

    bool enabled_ = true;

    juce::Image scanlineTile_;    // small tile, fillable/tiled at any size
    juce::Image vignette_;        // full-size, rebuilt on resize

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrtOverlay)
};
