#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "Theme.h"

/**
    "CZ Terminal" look for the PD Hybrid Synth editor: pure-black background with
    green phosphor monospace text, wireframe rotary knobs (outlined circle + a
    line indicator), and thin green-outlined combo boxes / buttons / value boxes.
*/
class SynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SynthLookAndFeel()
    {
        setTheme (theme_);
    }

    /** Installs a skin. Call `sendLookAndFeelChange()` on the editor afterwards
        so every child repaints. */
    void setTheme (const pdui::Theme& t)
    {
        theme_ = t;
        theme_.applyTo (*this);
    }

    const pdui::Theme& theme() const noexcept { return theme_; }

    static juce::Font mono (float h, bool bold = false)
    {
        return juce::Font (juce::Font::getDefaultMonospacedFontName(), h,
                           bold ? juce::Font::bold : juce::Font::plain);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override          { return mono (11.5f); }
    juce::Font getPopupMenuFont () override                        { return mono (12.0f); }
    // Respect whatever face the label was explicitly given (pdui::labelFont /
    // pdui::valueFont pick sans or mono per theme); forcing mono here would
    // silently undo that split for every juce::Label in the editor.
    juce::Font getLabelFont (juce::Label& l) override              { return l.getFont(); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return mono (11.5f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        const bool lit = b.getToggleState();
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = theme_.traits.cornerRadius;

        auto fill = lit ? findColour (pdui::ledOnBg) : findColour (pdui::fieldBg);
        if (down)      fill = fill.darker (0.25f);
        else if (over) fill = fill.brighter (0.12f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, rad);
        g.setColour (lit ? findColour (pdui::ledOnEdge) : findColour (pdui::panelEdge));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    /** The editor tags each slider with a "knobSize" property (0 small,
        1 normal, 2 large). The slider's *bounds* stay a full layout cell so its
        value box never truncates; the drawn rotary is what scales, which is how
        the panel gets a size hierarchy without disturbing the grid. */
    /** The editor tags each slider with a "knobSize" property (0 small,
        1 normal, 2 large) and a "modded" flag when at least one modulation
        route lands on it. Those signals are unchanged by the retheme; only
        the colours and cap rendering are trait-driven now. */
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        const int sizeTag = (int) s.getProperties().getWithDefault ("knobSize", 1);
        const bool large  = (sizeTag == 2);
        const bool modded = (bool) s.getProperties().getWithDefault ("modded", false);
        const float scale = sizeTag == 0 ? 0.70f : (large ? 1.0f : 0.85f);

        const auto boundsFull = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
        const float rFull = juce::jmin (boundsFull.getWidth(), boundsFull.getHeight()) * 0.5f;
        const auto  c     = boundsFull.getCentre();
        const float r     = rFull * scale;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // The value arc, outside the cap. Same in both skins.
        const float arcR = r - 1.0f;
        juce::Path track, value;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle,    true);
        g.setColour (findColour (pdui::knobTrack));
        g.strokePath (track, juce::PathStrokeType (large ? 2.5f : 2.0f));
        g.setColour (findColour (pdui::knobFill));
        g.strokePath (value, juce::PathStrokeType (large ? 2.5f : 2.0f));

        const float capR = r - 4.5f;
        if (capR <= 1.0f)
            return;

        if (theme_.traits.physicalKnobs)
        {
            // A moulded cap, lit from the upper left.
            juce::ColourGradient grad (findColour (pdui::knobCap1),
                                       c.x - capR * 0.35f, c.y - capR * 0.45f,
                                       findColour (pdui::knobCap3),
                                       c.x + capR * 0.5f,  c.y + capR * 0.6f, true);
            grad.addColour (0.62, findColour (pdui::knobCap2));
            g.setGradientFill (grad);
            g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);
        }
        else
        {
            g.setColour (findColour (pdui::knobCap2));
            g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
            g.setColour (findColour (pdui::panelEdge));
            g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);
        }

        // Pointer.
        juce::Path p;
        p.addRoundedRectangle (-1.0f, -capR + 1.5f, 2.0f, capR * 0.62f, 1.0f);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        g.setColour (findColour (pdui::knobPointer));
        g.fillPath (p);

        // Amber ring: at least one modulation-matrix route lands on this knob.
        // This is what stops the matrix being write-only — the panel itself
        // shows where modulation goes.
        if (modded)
        {
            const float mr = r + (large ? 6.0f : 4.0f);
            g.setColour (findColour (pdui::liveCol));
            g.drawEllipse (c.x - mr, c.y - mr, mr * 2.0f, mr * 2.0f, 1.4f);
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                    minSliderPos, maxSliderPos, style, s);
            return;
        }

        const auto accent = findColour (pdui::knobFill);
        const auto track  = findColour (pdui::knobTrack);

        const float cyf = (float) y + (float) height * 0.5f;
        const float x0  = minSliderPos;
        const float x1  = maxSliderPos;

        g.setColour (track);
        g.fillRect (x0, cyf - 1.5f, juce::jmax (1.0f, x1 - x0), 3.0f);
        g.setColour (accent);
        g.fillRect (x0, cyf - 1.5f, juce::jmax (0.0f, sliderPos - x0), 3.0f);
        g.fillRect (sliderPos - 2.0f, cyf - 7.0f, 4.0f, 14.0f);
    }

private:
    pdui::Theme theme_ = pdui::Theme::fromId (pdtheme::ThemeId::Panel1985);
};
