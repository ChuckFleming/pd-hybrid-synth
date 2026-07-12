#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

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
        setColour (juce::Slider::textBoxTextColourId,       juce::Colour (0xff4be08a));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff000000));
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colour (0xff17402c));

        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff000000));
        setColour (juce::ComboBox::textColourId,       juce::Colour (0xff4be08a));
        setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff2b6b46));
        setColour (juce::ComboBox::arrowColourId,      juce::Colour (0xff4be08a));

        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff030503));
        setColour (juce::PopupMenu::textColourId,                  juce::Colour (0xff37b06e));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff123322));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour (0xff4be08a));

        setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff000000));
        setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff4be08a));
        setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff4be08a));
    }

    static juce::Font mono (float h, bool bold = false)
    {
        return juce::Font (juce::Font::getDefaultMonospacedFontName(), h,
                           bold ? juce::Font::bold : juce::Font::plain);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override          { return mono (11.5f); }
    juce::Font getPopupMenuFont () override                        { return mono (12.0f); }
    juce::Font getLabelFont (juce::Label& l) override              { return mono (l.getFont().getHeight()); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return mono (11.5f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds();
        g.setColour (down ? juce::Colour (0xff0d2a1c)
                          : (over ? juce::Colour (0xff09190f) : juce::Colour (0xff000000)));
        g.fillRect (r);
        g.setColour (juce::Colour (0xff2b6b46));
        g.drawRect (r, 1);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override
    {
        const juce::Colour ring    (0xff2b6b46);
        const juce::Colour face    (0xff04140c);
        const juce::Colour pointer (0xff4be08a);

        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float r  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
        const float ang = startAngle + pos * (endAngle - startAngle);

        g.setColour (face);
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour (ring);
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.2f);

        const float px = cx + (r - 2.0f) * std::sin (ang);
        const float py = cy - (r - 2.0f) * std::cos (ang);
        g.setColour (pointer);
        g.drawLine (cx, cy, px, py, 2.0f);
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

        const juce::Colour accent (0xff4be08a);
        const juce::Colour track  (0xff173a29);

        const float cyf = (float) y + (float) height * 0.5f;
        const float x0  = minSliderPos;
        const float x1  = maxSliderPos;

        g.setColour (track);
        g.fillRect (x0, cyf - 1.5f, juce::jmax (1.0f, x1 - x0), 3.0f);
        g.setColour (accent);
        g.fillRect (x0, cyf - 1.5f, juce::jmax (0.0f, sliderPos - x0), 3.0f);
        g.fillRect (sliderPos - 1.5f, cyf - 7.0f, 3.0f, 14.0f);
    }
};
