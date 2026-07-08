#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

/**
    Custom look for the PD Hybrid Synth editor:
      - rotary knobs drawn as a "tick ring" (lit ticks up to the value) with a
        centre indicator line, matching the chosen style;
      - horizontal linear sliders drawn as a "notched" slider (filled track with
        a rectangular notch thumb), used by the modulation-matrix depths;
      - dark combo box / popup menu / value-box colours to fit the panel.
*/
class SynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SynthLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId,       juce::Colour (0xff9fb0c6));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff191d23));
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);

        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20242b));
        setColour (juce::ComboBox::textColourId,       juce::Colour (0xffc7cedb));
        setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff47515f));
        setColour (juce::ComboBox::arrowColourId,      juce::Colour (0xff8fb7ff));

        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff20242b));
        setColour (juce::PopupMenu::textColourId,                  juce::Colour (0xffc7cedb));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2c3440));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour (0xffe7edf6));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override
    {
        const juce::Colour accent  (0xff8fb7ff);
        const juce::Colour tickOff (0xff3f4855);
        const juce::Colour body    (0xff2b313a);
        const juce::Colour hub     (0xff333b45);

        auto bounds = juce::Rectangle<float> (x, y, width, height).reduced (3.0f);
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float r  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
        const float ang = startAngle + pos * (endAngle - startAngle);

        auto px = [cx] (float a, float rr) { return cx + rr * std::sin (a); };
        auto py = [cy] (float a, float rr) { return cy - rr * std::cos (a); };

        const int N = 24;
        for (int i = 0; i <= N; ++i)
        {
            const float t = (float) i / (float) N;
            const float a = startAngle + t * (endAngle - startAngle);
            const bool  on = t <= pos + 1.0e-4f;
            g.setColour (on ? accent : tickOff);
            g.drawLine (px (a, r - 3.0f), py (a, r - 3.0f),
                        px (a, r + 2.5f), py (a, r + 2.5f), 2.2f);
        }

        const float hr = juce::jmax (2.0f, r - 7.0f);
        g.setColour (body);
        g.fillEllipse (cx - hr, cy - hr, hr * 2.0f, hr * 2.0f);

        g.setColour (accent);
        g.drawLine (cx, cy, px (ang, r - 6.0f), py (ang, r - 6.0f), 2.8f);

        g.setColour (hub);
        g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
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

        const juce::Colour accent (0xff8fb7ff);
        const juce::Colour track  (0xff39424f);
        const juce::Colour thumb  (0xffdfe6f2);
        const juce::Colour edge   (0xff4a5462);

        const float cyf = (float) y + (float) height * 0.5f;
        const float x0  = minSliderPos;
        const float x1  = maxSliderPos;

        g.setColour (track);
        g.fillRoundedRectangle (x0, cyf - 2.0f, juce::jmax (1.0f, x1 - x0), 4.0f, 2.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (x0, cyf - 2.0f, juce::jmax (0.0f, sliderPos - x0), 4.0f, 2.0f);

        g.setColour (thumb);
        g.fillRoundedRectangle (sliderPos - 4.0f, cyf - 9.0f, 8.0f, 18.0f, 3.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (sliderPos - 4.0f, cyf - 9.0f, 8.0f, 18.0f, 3.0f, 1.0f);
    }
};
