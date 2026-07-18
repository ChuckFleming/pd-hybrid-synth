#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <cmath>

/**
    A small green-phosphor oscilloscope for the master output. It pulls the most
    recent samples from a lock-free tap (via the supplied reader), zero-crossing
    triggers them so the trace stays put, and draws it as a glowing waveform. It
    lives under the CRT overlay, so the scanlines fall over it for free.
*/
class ScopeDisplay : public juce::Component,
                     private juce::Timer
{
public:
    explicit ScopeDisplay (std::function<void (float*, int)> reader)
        : read_ (std::move (reader))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour bg     (0xff020805);
        const juce::Colour edge   (0xff1c3a2b);
        const juce::Colour grid   (0xff123322);
        const juce::Colour trace  (0xff4be08a);

        auto r = getLocalBounds().toFloat();
        g.setColour (bg);
        g.fillRect (r);

        auto inner = r.reduced (2.0f);
        // Graticule: centre line + a couple of verticals.
        g.setColour (grid);
        g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
        g.drawVerticalLine   ((int) (inner.getX() + inner.getWidth() * 0.5f), inner.getY(), inner.getBottom());
        g.setColour (grid.withAlpha (0.5f));
        g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * 0.25f), inner.getY(), inner.getBottom());
        g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * 0.75f), inner.getY(), inner.getBottom());

        float buf[kBufN];
        read_ (buf, kBufN);

        // Rising zero-crossing trigger in the first half so the trace is stable.
        int trig = 0;
        for (int i = 1; i < kBufN - kDrawN; ++i)
            if (buf[i - 1] <= 0.0f && buf[i] > 0.0f) { trig = i; break; }

        const float amp = inner.getHeight() * 0.45f;
        const float cy  = inner.getCentreY();
        juce::Path path;
        for (int i = 0; i < kDrawN; ++i)
        {
            const float x = inner.getX() + (float) i / (kDrawN - 1) * inner.getWidth();
            const float s = std::isfinite (buf[trig + i]) ? buf[trig + i] : 0.0f;
            const float y = cy - juce::jlimit (-1.2f, 1.2f, s) * amp;
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }

        // Phosphor glow: a soft wide pass under a crisp bright pass.
        g.setColour (trace.withAlpha (0.25f));
        g.strokePath (path, juce::PathStrokeType (3.0f));
        g.setColour (trace);
        g.strokePath (path, juce::PathStrokeType (1.4f));

        g.setColour (edge);
        g.drawRect (r, 1.0f);
    }

private:
    void timerCallback() override { repaint(); }

    std::function<void (float*, int)> read_;
    static constexpr int kBufN  = 2048;   // samples pulled per frame
    static constexpr int kDrawN = 1024;   // samples actually drawn (after trigger)
};
