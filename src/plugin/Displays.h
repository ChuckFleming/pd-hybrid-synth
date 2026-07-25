#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

/**
    Small green-phosphor readouts used throughout the editor: an ADSR curve, an
    LFO waveform with a running playhead, and the signal-routing diagram.

    All three poll their parameters on a timer rather than listening to them, so
    automation and preset loads move them without any audio-thread callbacks,
    and none of them ever touch a parameter (they are read-only displays).
*/
namespace pdui {

inline const juce::Colour kBg     (0xff020805);
inline const juce::Colour kEdge   (0xff1c3a2b);
inline const juce::Colour kGrid   (0xff123322);
inline const juce::Colour kTrace  (0xff4be08a);
inline const juce::Colour kDim    (0xff37b06e);
inline const juce::Colour kAmber  (0xffe8a54b);

inline juce::Font monoF (float h)
{
    return juce::Font (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain);
}

/** Draws a labelled frame and returns the drawable interior. */
inline juce::Rectangle<float> drawFrame (juce::Graphics& g, juce::Rectangle<float> r,
                                         const juce::String& caption, juce::Colour edge = kEdge)
{
    g.setColour (kBg);   g.fillRect (r);
    g.setColour (edge);  g.drawRect (r, 1.0f);
    if (caption.isNotEmpty())
    {
        g.setFont (monoF (8.0f));
        g.setColour (kDim.withAlpha (0.75f));
        g.drawText (caption, r.reduced (5.0f, 3.0f), juce::Justification::topLeft);
    }
    return r.reduced (5.0f, 6.0f);
}

//==============================================================================
/** ADSR curve. Segment widths follow the real times, with a fixed sustain hold
    so the sustain level is always visible. */
class EnvelopeCurve : public juce::Component,
                      private juce::Timer
{
public:
    EnvelopeCurve (juce::String caption, juce::Colour colour = kTrace)
        : caption_ (std::move (caption)), colour_ (colour)
    {
        setInterceptsMouseClicks (false, false);
    }
    ~EnvelopeCurve() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s,
                 const juce::String& a, const juce::String& d,
                 const juce::String& sus, const juce::String& r)
    {
        a_ = s.getParameter (a); d_ = s.getParameter (d);
        s_ = s.getParameter (sus); r_ = s.getParameter (r);
        startTimerHz (12);
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), caption_);
        if (a_ == nullptr) return;

        const float A = a_->convertFrom0to1 (a_->getValue());
        const float D = d_->convertFrom0to1 (d_->getValue());
        const float S = juce::jlimit (0.0f, 1.0f, s_->convertFrom0to1 (s_->getValue()));
        const float R = r_->convertFrom0to1 (r_->getValue());

        // The held section is a fixed slice of the width; the three timed
        // sections share the rest in proportion to their real durations.
        const float hold  = 0.22f;
        const float timed = juce::jmax (1.0e-4f, A + D + R);
        const float sc    = (1.0f - hold) / timed;

        const float x0 = in.getX(), w = in.getWidth();
        const float yb = in.getBottom(), h = in.getHeight();
        const float xA = x0 + w * A * sc;
        const float xD = xA + w * D * sc;
        const float xS = xD + w * hold;

        juce::Path p;
        p.startNewSubPath (x0, yb);
        p.lineTo (xA, yb - h);              // attack to full
        p.lineTo (xD, yb - h * S);          // decay to sustain
        p.lineTo (xS, yb - h * S);          // hold
        p.lineTo (in.getRight(), yb);       // release

        auto fill = p;
        fill.lineTo (in.getRight(), yb);
        fill.lineTo (x0, yb);
        fill.closeSubPath();
        g.setColour (colour_.withAlpha (0.12f));
        g.fillPath (fill);

        g.setColour (kGrid);
        g.fillRect (x0, yb - h * S, w, 1.0f);

        g.setColour (colour_.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setColour (colour_);
        g.strokePath (p, juce::PathStrokeType (1.5f));

        // Breakpoint markers at the three corners.
        g.setColour (kBg);
        for (auto pt : { juce::Point<float> (xA, yb - h),
                         juce::Point<float> (xD, yb - h * S),
                         juce::Point<float> (xS, yb - h * S) })
        {
            g.setColour (kBg);     g.fillRect (pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
            g.setColour (colour_); g.drawRect (juce::Rectangle<float> (pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f), 1.0f);
        }
    }

private:
    void timerCallback() override
    {
        if (a_ == nullptr) return;
        const float now[4] { a_->getValue(), d_->getValue(), s_->getValue(), r_->getValue() };
        for (int i = 0; i < 4; ++i)
            if (! juce::approximatelyEqual (now[i], last_[i])) { last_[i] = now[i]; repaint(); }
    }

    juce::String caption_;
    juce::Colour colour_;
    juce::RangedAudioParameter *a_ = nullptr, *d_ = nullptr, *s_ = nullptr, *r_ = nullptr;
    float last_[4] { -1.0f, -1.0f, -1.0f, -1.0f };
};

//==============================================================================
/** One cycle of an LFO's waveform with a playhead that runs at its rate. */
class LfoCurve : public juce::Component,
                 private juce::Timer
{
public:
    explicit LfoCurve (juce::String caption = {}) : caption_ (std::move (caption))
    {
        setInterceptsMouseClicks (false, false);
    }
    ~LfoCurve() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s,
                 const juce::String& wave, const juce::String& rate)
    {
        wave_ = s.getParameter (wave);
        rate_ = s.getParameter (rate);
        startTimerHz (30);
    }

    /** Supply the LFO's real output and the card shows a live trace over the
        (dimmed) shape, instead of a free-running playhead. */
    void setLiveReader (std::function<float()> reader)
    {
        live_ = std::move (reader);
        history_.assign (kHist, 0.0f);
    }

    /** Sample the waveform at phase 0..1, returning -1..1. */
    static float shape (int wave, float ph)
    {
        switch (wave)
        {
            case 0:  return std::sin (ph * juce::MathConstants<float>::twoPi);       // sine
            case 1:  return 4.0f * std::abs (ph - 0.5f) - 1.0f;                      // triangle
            case 2:  return ph < 0.5f ? 1.0f : -1.0f;                                // square
            case 3:  return 2.0f * ph - 1.0f;                                        // saw up
            case 4:  return 1.0f - 2.0f * ph;                                        // ramp down
            case 5:  {   // sample & hold: eight fixed steps, so the picture is stable
                static const float st[8] { 0.6f, -0.3f, 0.9f, -0.8f, 0.2f, -0.55f, 0.75f, -0.1f };
                return st[juce::jlimit (0, 7, (int) (ph * 8.0f))];
            }
            case 6:  {   // smooth random: the same steps, interpolated
                static const float st[8] { 0.6f, -0.3f, 0.9f, -0.8f, 0.2f, -0.55f, 0.75f, -0.1f };
                const float f = ph * 8.0f;
                const int   i = juce::jlimit (0, 7, (int) f);
                const float t = f - (float) i;
                return st[i] + (st[(i + 1) & 7] - st[i]) * (t * t * (3.0f - 2.0f * t));
            }
            default: return 2.0f * std::exp (-3.0f * ph) - 1.0f;                     // exponential
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), caption_);
        if (wave_ == nullptr) return;

        const int wv = juce::roundToInt (wave_->convertFrom0to1 (wave_->getValue()));
        const float cy = in.getCentreY(), amp = in.getHeight() * 0.42f;

        g.setColour (kGrid);
        g.fillRect (in.getX(), cy, in.getWidth(), 1.0f);

        juce::Path p;
        const int N = 96;
        for (int i = 0; i <= N; ++i)
        {
            const float ph = (float) i / (float) N;
            const float x  = in.getX() + ph * in.getWidth();
            const float y  = cy - shape (wv, ph) * amp;
            // Square and S&H step rather than ramp, so draw the vertical edge.
            if (i == 0)                    p.startNewSubPath (x, y);
            else if (wv == 2 || wv == 5)   { p.lineTo (x, p.getCurrentPosition().y); p.lineTo (x, y); }
            else                            p.lineTo (x, y);
        }

        // With a live reader the shape becomes a dim reference and the real
        // output is drawn over it; without one, the shape is the whole story.
        const bool hasLive = (live_ != nullptr);
        g.setColour (kTrace.withAlpha (hasLive ? 0.16f : 0.22f));
        g.strokePath (p, juce::PathStrokeType (hasLive ? 1.2f : 3.0f));
        if (! hasLive)
        {
            g.setColour (kTrace);
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }

        if (hasLive)
        {
            juce::Path lp;
            for (int i = 0; i < kHist; ++i)
            {
                const float x = in.getX() + (float) i / (float) (kHist - 1) * in.getWidth();
                const float y = cy - juce::jlimit (-1.0f, 1.0f, history_[(size_t) i]) * amp;
                if (i == 0) lp.startNewSubPath (x, y);
                else        lp.lineTo (x, y);
            }
            g.setColour (kTrace.withAlpha (0.25f));
            g.strokePath (lp, juce::PathStrokeType (3.0f));
            g.setColour (kTrace);
            g.strokePath (lp, juce::PathStrokeType (1.4f));

            // Current value, marked at the leading edge of the trace.
            const float y = cy - juce::jlimit (-1.0f, 1.0f, history_.back()) * amp;
            g.fillEllipse (in.getRight() - 2.4f, y - 2.4f, 4.8f, 4.8f);
        }
        else
        {
            const float px = in.getX() + phase_ * in.getWidth();
            const float py = cy - shape (wv, phase_) * amp;
            g.setColour (kTrace);
            g.fillEllipse (px - 2.4f, py - 2.4f, 4.8f, 4.8f);
        }
    }

private:
    void timerCallback() override
    {
        if (live_ != nullptr)
        {
            std::rotate (history_.begin(), history_.begin() + 1, history_.end());
            history_.back() = live_();
        }
        else if (rate_ != nullptr)
        {
            const float hz = rate_->convertFrom0to1 (rate_->getValue());
            phase_ += juce::jlimit (0.0f, 0.5f, hz / 30.0f);   // timer runs at 30 Hz
            while (phase_ >= 1.0f) phase_ -= 1.0f;
        }
        repaint();
    }

    static constexpr int kHist = 128;

    juce::String caption_;
    juce::RangedAudioParameter *wave_ = nullptr, *rate_ = nullptr;
    std::function<float()> live_;
    std::vector<float> history_ { std::vector<float> (kHist, 0.0f) };
    float phase_ = 0.0f;
};

//==============================================================================
/** Live modulation-source meters.

    Each row is a real reading of what the DSP is currently producing for that
    source: the per-voice ones come from the newest sounding voice, the global
    ones from the processor. Rows marked as traces keep a short scrolling
    history so an LFO or an envelope is legible as motion, not just a number.

    One component draws the whole list, so there are no child widgets to keep in
    sync with the painted rows. */
class SourceMeters : public juce::Component,
                     private juce::Timer
{
public:
    struct Row
    {
        int          srcIndex;     // index into ModSource / the editor's name table
        juce::String name;
        bool         trace;        // scrolling history rather than a single bar
        bool         bipolar;      // value swings around zero
    };

    std::function<void (int)> onRowClicked;

    SourceMeters() { startTimerHz (30); }
    ~SourceMeters() override { stopTimer(); }

    /** `reader` fills a caller-sized array with the current level of every source. */
    void setReader (std::function<void (float*, int)> reader) { read_ = std::move (reader); }

    void setRows (std::vector<Row> rows)
    {
        rows_ = std::move (rows);
        history_.assign (rows_.size(), std::vector<float> (kHist, 0.0f));
        rowBounds_.assign (rows_.size(), {});
        repaint();
    }

    /** Which sources a matrix route actually uses, and which one is selected. */
    void setUsage (std::vector<bool> used, int selected)
    {
        used_ = std::move (used);
        selected_ = selected;
    }

    int preferredHeight() const
    {
        int h = 0;
        for (const auto& r : rows_) h += r.trace ? kTraceH : kBarH;
        return h;
    }

    void resized() override
    {
        auto r = getLocalBounds();
        for (std::size_t i = 0; i < rows_.size(); ++i)
            rowBounds_[i] = r.removeFromTop (rows_[i].trace ? kTraceH : kBarH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (std::size_t i = 0; i < rowBounds_.size(); ++i)
            if (rowBounds_[i].contains (e.getPosition()) && onRowClicked)
            {
                onRowClicked (rows_[i].srcIndex);
                return;
            }
    }

    void paint (juce::Graphics& g) override
    {
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            const auto& row = rows_[i];
            auto b = rowBounds_[i];
            const bool used = i < used_.size() && used_[i];
            const bool sel  = row.srcIndex == selected_;

            if (sel)
            {
                g.setColour (kAmber.withAlpha (0.10f));
                g.fillRect (b);
            }

            g.setFont (monoF (8.5f));
            g.setColour (used ? kAmber : kDim.withAlpha (0.65f));
            g.drawText (row.name.toUpperCase(), b.withWidth (kNameW).reduced (3, 0),
                        juce::Justification::centredLeft);

            // The plot stops short of the value column so the two never overlap.
            auto plot = b.withTrimmedLeft (kNameW).withTrimmedRight (kValueW)
                         .reduced (2, row.trace ? 3 : 5);
            auto value = history_[i].empty() ? 0.0f : history_[i].back();

            g.setColour (juce::Colour (0xff0e2116));
            g.fillRect (plot);

            const auto col = used ? kTrace : kTrace.withAlpha (0.5f);

            if (row.trace)
            {
                // Scrolling history, oldest at the left.
                const float mid = row.bipolar ? plot.getCentreY() : (float) plot.getBottom();
                const float amp = row.bipolar ? plot.getHeight() * 0.45f : plot.getHeight() * 0.92f;
                if (row.bipolar)
                {
                    g.setColour (kGrid);
                    g.fillRect ((float) plot.getX(), mid, (float) plot.getWidth(), 1.0f);
                }

                juce::Path p;
                for (int s = 0; s < kHist; ++s)
                {
                    const float x = plot.getX() + (float) s / (float) (kHist - 1) * plot.getWidth();
                    const float y = mid - juce::jlimit (-1.0f, 1.0f, history_[i][(size_t) s]) * amp;
                    if (s == 0) p.startNewSubPath (x, y);
                    else        p.lineTo (x, y);
                }
                g.setColour (col.withAlpha (0.25f));
                g.strokePath (p, juce::PathStrokeType (2.6f));
                g.setColour (col);
                g.strokePath (p, juce::PathStrokeType (1.2f));
            }
            else
            {
                const float v = juce::jlimit (-1.0f, 1.0f, value);
                g.setColour (col);
                if (row.bipolar)
                {
                    const int mid = plot.getCentreX();
                    const int w   = juce::roundToInt (std::abs (v) * plot.getWidth() * 0.5f);
                    g.fillRect (v >= 0.0f ? mid : mid - w, plot.getY(), juce::jmax (1, w), plot.getHeight());
                }
                else
                {
                    g.fillRect (plot.getX(), plot.getY(),
                                juce::jmax (1, juce::roundToInt (std::abs (v) * plot.getWidth())),
                                plot.getHeight());
                }
            }

            g.setFont (monoF (8.0f));
            g.setColour (kDim);
            g.drawText (juce::String (value, 2), b.removeFromRight (kValueW).reduced (2, 0),
                        juce::Justification::centredRight);
        }
    }

private:
    void timerCallback() override
    {
        if (! read_ || rows_.empty()) return;

        float levels[64] {};
        read_ (levels, 64);
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            auto& h = history_[i];
            std::rotate (h.begin(), h.begin() + 1, h.end());
            h.back() = levels[juce::jlimit (0, 63, rows_[i].srcIndex)];
        }
        repaint();
    }

    static constexpr int kHist   = 96;
    static constexpr int kTraceH = 26;
    static constexpr int kBarH   = 15;
    static constexpr int kNameW  = 62;   // source-name column
    static constexpr int kValueW = 34;   // numeric readout column

    std::function<void (float*, int)> read_;
    std::vector<Row> rows_;
    std::vector<std::vector<float>> history_;
    std::vector<juce::Rectangle<int>> rowBounds_;
    std::vector<bool> used_;
    int selected_ = -1;
};

//==============================================================================
/** The audio path as blocks and arrows, driven by the three routing choices.
    This is what makes filterRouting / drivePos / fxRouting legible: the picture
    changes when they do. */
class RoutingDiagram : public juce::Component,
                       private juce::Timer
{
public:
    RoutingDiagram() { setInterceptsMouseClicks (false, false); }
    ~RoutingDiagram() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s)
    {
        filterRouting_ = s.getParameter ("filterRouting");
        drivePos_      = s.getParameter ("drivePos");
        fxRouting_     = s.getParameter ("fxRouting");
        driveOn_       = s.getParameter ("driveOn");
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "SIGNAL PATH");
        if (filterRouting_ == nullptr) return;

        const int  routing = juce::roundToInt (filterRouting_->convertFrom0to1 (filterRouting_->getValue()));
        const bool drivePre = juce::roundToInt (drivePos_->convertFrom0to1 (drivePos_->getValue())) == 1;
        const int  fx       = juce::roundToInt (fxRouting_->convertFrom0to1 (fxRouting_->getValue()));
        const bool drive    = driveOn_->getValue() > 0.5f;

        in = in.withTrimmedTop (10.0f);
        const float bw = 46.0f, bh = 17.0f;
        const float cy = in.getCentreY();
        float x = in.getX();

        auto block = [&] (juce::Rectangle<float> b, const juce::String& t, bool lit)
        {
            g.setColour (lit ? kTrace : kEdge);
            g.drawRect (b, 1.0f);
            g.setFont (monoF (8.0f));
            g.setColour (lit ? kTrace : kDim.withAlpha (0.5f));
            g.drawText (t, b, juce::Justification::centred);
        };
        auto arrow = [&] (float x0, float y0, float x1, float y1)
        {
            g.setColour (kTrace);
            g.drawLine (x0, y0, x1, y1, 1.0f);
            juce::Path head;
            head.addTriangle (x1, y1, x1 - 4.0f, y1 - 3.0f, x1 - 4.0f, y1 + 3.0f);
            g.fillPath (head);
        };

        // Source
        block ({ x, cy - bh * 0.5f, bw, bh }, "OSC", true);
        x += bw;

        if (drivePre)
        {
            arrow (x, cy, x + 12.0f, cy); x += 12.0f;
            block ({ x, cy - bh * 0.5f, bw, bh }, "DRIVE", drive);
            x += bw;
        }

        arrow (x, cy, x + 12.0f, cy); x += 12.0f;

        // Filters: single, in series, or in parallel.
        if (routing == 2)
        {
            const float dy = 13.0f;
            block ({ x, cy - dy - bh * 0.5f, bw, bh }, "FLT 1", true);
            block ({ x, cy + dy - bh * 0.5f, bw, bh }, "FLT 2", true);
            g.setColour (kTrace);
            g.drawLine (x - 6.0f, cy - dy, x - 6.0f, cy + dy, 1.0f);
            g.drawLine (x - 6.0f, cy - dy, x, cy - dy, 1.0f);
            g.drawLine (x - 6.0f, cy + dy, x, cy + dy, 1.0f);
            g.drawLine (x + bw, cy - dy, x + bw + 6.0f, cy - dy, 1.0f);
            g.drawLine (x + bw, cy + dy, x + bw + 6.0f, cy + dy, 1.0f);
            g.drawLine (x + bw + 6.0f, cy - dy, x + bw + 6.0f, cy + dy, 1.0f);
            x += bw + 6.0f;
        }
        else
        {
            block ({ x, cy - bh * 0.5f, bw, bh }, "FLT 1", true);
            x += bw;
            if (routing == 1)
            {
                arrow (x, cy, x + 12.0f, cy); x += 12.0f;
                block ({ x, cy - bh * 0.5f, bw, bh }, "FLT 2", true);
                x += bw;
            }
        }

        if (! drivePre)
        {
            arrow (x, cy, x + 12.0f, cy); x += 12.0f;
            block ({ x, cy - bh * 0.5f, bw, bh }, "DRIVE", drive);
            x += bw;
        }

        arrow (x, cy, x + 12.0f, cy); x += 12.0f;
        block ({ x, cy - bh * 0.5f, bw, bh }, "AMP", true);
        x += bw;

        // FX tail, named by the routing choice.
        static const char* fxText[3] { "DLY>REV", "REV>DLY", "REV+DLY" };
        arrow (x, cy, x + 12.0f, cy); x += 12.0f;
        block ({ x, cy - bh * 0.5f, bw + 14.0f, bh }, fxText[juce::jlimit (0, 2, fx)], true);

        g.setFont (monoF (8.0f));
        g.setColour (kDim.withAlpha (0.65f));
        g.drawText (routing == 0 ? "single filter" : (routing == 1 ? "filters in series" : "filters in parallel"),
                    getLocalBounds().reduced (6, 4), juce::Justification::bottomLeft);
    }

private:
    void timerCallback() override
    {
        if (filterRouting_ == nullptr) return;
        const float now[4] { filterRouting_->getValue(), drivePos_->getValue(),
                             fxRouting_->getValue(), driveOn_->getValue() };
        for (int i = 0; i < 4; ++i)
            if (! juce::approximatelyEqual (now[i], last_[i])) { last_[i] = now[i]; repaint(); }
    }

    juce::RangedAudioParameter *filterRouting_ = nullptr, *drivePos_ = nullptr,
                               *fxRouting_ = nullptr, *driveOn_ = nullptr;
    float last_[4] { -1.0f, -1.0f, -1.0f, -1.0f };
};

} // namespace pdui
