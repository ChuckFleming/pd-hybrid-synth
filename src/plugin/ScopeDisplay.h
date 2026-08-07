#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Theme.h"
#include <array>
#include <functional>
#include <cmath>

/**
    A small green-phosphor oscilloscope + spectrum analyser for the master
    output. It pulls the most recent samples from a lock-free tap (via the
    supplied reader), zero-crossing triggers them so the trace stays put, and
    draws the waveform over a log-frequency magnitude spectrum. It lives under
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
        const auto bg    = findColour (pdui::screenBg);
        const auto edge  = findColour (pdui::panelEdge);
        const auto grid  = findColour (pdui::screenGrid);
        const auto trace = findColour (pdui::screenTrace);

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

        // --- spectrum, drawn behind the trace -----------------------------
        {
            std::array<float, 2 * kFftN> fft {};
            for (int i = 0; i < kFftN; ++i)
            {
                const float s = std::isfinite (buf[i]) ? buf[i] : 0.0f;
                // Hann window, so the bins are not smeared by the block edges.
                fft[(size_t) i] = s * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                               * (float) i / (float) (kFftN - 1)));
            }
            fft_.performFrequencyOnlyForwardTransform (fft.data());

            const int    bins   = kFftN / 2;
            const float  loBin  = 2.0f;                       // skip DC / near-DC
            const float  ratio  = std::log ((float) bins / loBin);
            const int    cols   = juce::jmax (1, (int) inner.getWidth() / 4);

            g.setColour (trace.withAlpha (0.20f));
            for (int c = 0; c < cols; ++c)
            {
                // Log frequency axis: each column covers a constant ratio of bins.
                const float f0 = loBin * std::exp (ratio * (float) c       / (float) cols);
                const float f1 = loBin * std::exp (ratio * (float) (c + 1) / (float) cols);
                float peak = 0.0f;
                for (int b = (int) f0; b <= juce::jmin (bins - 1, (int) f1); ++b)
                    peak = juce::jmax (peak, fft[(size_t) b]);

                const float dB = juce::Decibels::gainToDecibels (peak / (float) bins + 1.0e-9f);
                const float mag = juce::jlimit (0.0f, 1.0f, (dB + 84.0f) / 84.0f);
                // Slew the columns so the display settles instead of flickering.
                spec_[(size_t) juce::jmin (c, kMaxCols - 1)] =
                    juce::jmax (mag, spec_[(size_t) juce::jmin (c, kMaxCols - 1)] * 0.82f);

                const float h = spec_[(size_t) juce::jmin (c, kMaxCols - 1)] * inner.getHeight();
                const float x = inner.getX() + (float) c / (float) cols * inner.getWidth();
                g.fillRect (x, inner.getBottom() - h, inner.getWidth() / (float) cols - 1.0f, h);
            }
        }

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

        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
        g.setColour (findColour (pdui::screenTrace).withAlpha (0.75f));
        g.drawText ("OUT " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " SPECTRUM",
                    r.reduced (5.0f, 3.0f), juce::Justification::topLeft);
    }

private:
    void timerCallback() override { repaint(); }

    std::function<void (float*, int)> read_;
    static constexpr int kBufN    = 2048;   // samples pulled per frame
    static constexpr int kDrawN   = 1024;   // samples actually drawn (after trigger)
    static constexpr int kFftOrder = 10;
    static constexpr int kFftN     = 1 << kFftOrder;
    static constexpr int kMaxCols  = 512;

    juce::dsp::FFT fft_ { kFftOrder };
    std::array<float, kMaxCols> spec_ {};   // decayed column peaks
};
