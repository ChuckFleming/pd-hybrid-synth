#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/OscillatorUnit.h"
#include "dsp/FilterUnit.h"
#include "dsp/GlobalEq.h"
#include "dsp/Waveshaper.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

/**
    Small green-phosphor readouts used throughout the editor: an ADSR curve, an
    LFO waveform with a running playhead, and the signal-routing diagram.

    They poll their parameters on a timer rather than listening to them, so
    automation and preset loads move them without any audio-thread callbacks.
    All are read-only except EnvelopeCurve, whose breakpoints can be dragged --
    it writes through proper begin/endChangeGesture pairs so a host records the
    move as automation.
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
    so the sustain level is always visible.

    The breakpoints are draggable: the attack corner sets A, the sustain corner
    sets D and S together, the plateau end sets S, and the midpoint of the
    release ramp sets R. As in the 8-stage editor, the horizontal time base is
    frozen for the duration of a gesture -- otherwise editing a time rescales
    the x axis under the cursor and the drag fights itself. */
class EnvelopeCurve : public juce::Component,
                      private juce::Timer
{
public:
    EnvelopeCurve (juce::String caption, juce::Colour colour = kTrace)
        : caption_ (std::move (caption)), colour_ (colour)
    {
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

    /** Maps a raw stage time to the time actually being played. Set by the
        editor to the tempo-sync snap, so that while SYNC is on the curve only
        moves when the division changes -- turning a knob within a division's
        snap zone alters nothing audible and must not animate the display. */
    void setTimeMapper (std::function<double (double)> fn)
    {
        timeMap_ = std::move (fn);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), caption_);
        if (a_ == nullptr) return;

        const auto geo = geometry (in);

        juce::Path p;
        p.startNewSubPath (in.getX(), geo.yb);
        p.lineTo (geo.xA, geo.yb - geo.h);        // attack to full
        p.lineTo (geo.xD, geo.yS);                // decay to sustain
        p.lineTo (geo.xS, geo.yS);                // hold
        p.lineTo (geo.xR, geo.yb);                // release

        auto fill = p;
        fill.lineTo (geo.xR, geo.yb);
        fill.lineTo (in.getX(), geo.yb);
        fill.closeSubPath();
        g.setColour (colour_.withAlpha (0.12f));
        g.fillPath (fill);

        g.setColour (kGrid);
        g.fillRect (in.getX(), geo.yS, in.getWidth(), 1.0f);

        g.setColour (colour_.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setColour (colour_);
        g.strokePath (p, juce::PathStrokeType (1.5f));

        // Breakpoint handles. The hovered or dragged one lights up amber so it
        // reads as grabbable rather than as decoration.
        for (int i = 0; i < kNumNodes; ++i)
        {
            const auto pt  = nodePos (geo, i);
            const bool lit = (i == dragNode_) || (i == hoverNode_);
            const float rad = lit ? 3.5f : 2.5f;
            g.setColour (kBg);
            g.fillRect (pt.x - rad, pt.y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (lit ? kAmber : colour_);
            g.drawRect (juce::Rectangle<float> (pt.x - rad, pt.y - rad, rad * 2.0f, rad * 2.0f), 1.0f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitNode (e.position);
        if (h != hoverNode_)
        {
            hoverNode_ = h;
            setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor
                                   : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverNode_ != -1) { hoverNode_ = -1; repaint(); }
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (a_ == nullptr) return;

        dragNode_ = hitNode (e.position);
        if (dragNode_ < 0) return;

        // Freeze the time base for the gesture (see the class comment).
        const auto geo = geometry (interior());
        dragScale_ = geo.sc;

        for (auto* p : gestureParams (dragNode_))
            if (p != nullptr) p->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragNode_ < 0) return;

        const auto in = interior();
        const float w  = in.getWidth();
        const float yb = in.getBottom(), h = juce::jmax (1.0f, in.getHeight());

        // Time in seconds from a horizontal position, using the frozen scale.
        auto timeAt = [&] (float x, float startX)
        {
            const float dx = juce::jmax (0.0f, x - startX);
            return dragScale_ > 1.0e-6f ? dx / (w * dragScale_) : 0.0f;
        };
        auto setLevel = [&] (juce::RangedAudioParameter* p, float y)
        {
            const float v = juce::jlimit (0.0f, 1.0f, (yb - y) / h);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        auto setTime = [&] (juce::RangedAudioParameter* p, float seconds)
        {
            const auto range = p->getNormalisableRange();
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (range.start, range.end, seconds)));
        };

        const float A = a_->convertFrom0to1 (a_->getValue());

        switch (dragNode_)
        {
            case 0:   // attack corner: x -> A
                setTime (a_, timeAt (e.position.x, in.getX()));
                break;

            case 1:   // sustain corner: x -> D (measured from the attack peak), y -> S
            {
                const float xA = in.getX() + w * A * dragScale_;
                setTime (d_, timeAt (e.position.x, xA));
                setLevel (s_, e.position.y);
                break;
            }

            case 2:   // plateau end: y -> S only (its x is the fixed hold width)
                setLevel (s_, e.position.y);
                break;

            case 3:   // release handle sits at the ramp's midpoint, so x -> 2 * R
            {
                const float D  = d_->convertFrom0to1 (d_->getValue());
                const float xS = in.getX() + w * (A + D) * dragScale_ + w * kHold;
                setTime (r_, 2.0f * timeAt (e.position.x, xS));
                break;
            }

            default: break;
        }

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragNode_ < 0) return;
        for (auto* p : gestureParams (dragNode_))
            if (p != nullptr) p->endChangeGesture();
        dragNode_ = -1;
        repaint();
    }

private:
    static constexpr int   kNumNodes = 4;
    static constexpr float kHold     = 0.22f;   // fixed slice given to the sustain hold
    static constexpr float kHitR     = 6.0f;

    struct Geometry
    {
        float sc = 0.0f;                  // seconds -> fraction of width
        float xA = 0.0f, xD = 0.0f, xS = 0.0f, xR = 0.0f;
        float yb = 0.0f, yS = 0.0f, h = 0.0f;
    };

    juce::Rectangle<float> interior() const
    {
        // Mirrors drawFrame's inset so hit-testing lines up with what is drawn.
        return getLocalBounds().toFloat().reduced (5.0f, 6.0f);
    }

    Geometry geometry (juce::Rectangle<float> in) const
    {
        Geometry g;
        if (a_ == nullptr) return g;

        const float A = mapTime (a_->convertFrom0to1 (a_->getValue()));
        const float D = mapTime (d_->convertFrom0to1 (d_->getValue()));
        const float S = juce::jlimit (0.0f, 1.0f, s_->convertFrom0to1 (s_->getValue()));
        const float R = mapTime (r_->convertFrom0to1 (r_->getValue()));

        // The held section is a fixed slice of the width; the three timed
        // sections share the rest in proportion to their real durations.
        const float timed = juce::jmax (1.0e-4f, A + D + R);
        g.sc = (1.0f - kHold) / timed;

        const float w = in.getWidth();
        g.yb = in.getBottom();
        g.h  = in.getHeight();
        g.yS = g.yb - g.h * S;
        g.xA = in.getX() + w * A * g.sc;
        g.xD = g.xA + w * D * g.sc;
        g.xS = g.xD + w * kHold;
        g.xR = in.getRight();
        return g;
    }

    juce::Point<float> nodePos (const Geometry& g, int i) const
    {
        switch (i)
        {
            case 0:  return { g.xA, g.yb - g.h };
            case 1:  return { g.xD, g.yS };
            case 2:  return { g.xS, g.yS };
            default: return { (g.xS + g.xR) * 0.5f, (g.yS + g.yb) * 0.5f };
        }
    }

    int hitNode (juce::Point<float> p) const
    {
        if (a_ == nullptr) return -1;
        const auto g = geometry (interior());
        for (int i = 0; i < kNumNodes; ++i)
            if (nodePos (g, i).getDistanceFrom (p) <= kHitR)
                return i;
        return -1;
    }

    std::array<juce::RangedAudioParameter*, 2> gestureParams (int node) const
    {
        switch (node)
        {
            case 0:  return { a_, nullptr };
            case 1:  return { d_, s_ };
            case 2:  return { s_, nullptr };
            default: return { r_, nullptr };
        }
    }

    float mapTime (float seconds) const
    {
        return timeMap_ ? static_cast<float> (timeMap_ (seconds)) : seconds;
    }

    void timerCallback() override
    {
        if (a_ == nullptr) return;

        // Compare the *drawn* values, not the raw parameter values: with sync
        // on, a knob can move a long way inside one division without changing
        // the shape, and repainting then would show motion that is not there.
        const float now[4] { mapTime (a_->convertFrom0to1 (a_->getValue())),
                             mapTime (d_->convertFrom0to1 (d_->getValue())),
                             s_->getValue(),
                             mapTime (r_->convertFrom0to1 (r_->getValue())) };
        for (int i = 0; i < 4; ++i)
            if (! juce::approximatelyEqual (now[i], last_[i])) { last_[i] = now[i]; repaint(); }
    }

    juce::String caption_;
    juce::Colour colour_;
    juce::RangedAudioParameter *a_ = nullptr, *d_ = nullptr, *s_ = nullptr, *r_ = nullptr;
    float last_[4] { -1.0f, -1.0f, -1.0f, -1.0f };

    int   dragNode_  = -1;
    int   hoverNode_ = -1;
    float dragScale_ = 0.0f;   // x scale frozen for the duration of a drag
    std::function<double (double)> timeMap_;   // raw seconds -> played seconds
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
/** One cycle of an oscillator's real output.

    Runs an actual `OscillatorUnit` on the message thread, configured from the
    same parameters the voices use, and plots a single settled period. For a
    phase-distortion synth this is the display that matters most: the waveform
    genuinely cannot be predicted from the control values. */
class WaveCyclePreview : public juce::Component,
                         private juce::Timer
{
public:
    WaveCyclePreview() { setInterceptsMouseClicks (false, false); }
    ~WaveCyclePreview() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& prefix)
    {
        apvts_ = &s;
        prefix_ = prefix;
        osc_.setSampleRate (kSr);
        startTimerHz (8);
        rebuild();
    }

    /** Point the preview at the loaded wavetable and redraw. Called when a new
        table is imported; the parameter timer cannot see that change. */
    void setWavetable (std::shared_ptr<pdhybrid::WavetableOscillator::WavetableSet> t)
    {
        osc_.setWavetable (std::move (t));
        if (apvts_ != nullptr)
            rebuild();
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "CYCLE");
        const float cy = in.getCentreY(), amp = in.getHeight() * 0.44f;

        g.setColour (kGrid);
        g.fillRect (in.getX(), cy, in.getWidth(), 1.0f);

        juce::Path p;
        for (int i = 0; i < kN; ++i)
        {
            const float x = in.getX() + (float) i / (float) (kN - 1) * in.getWidth();
            const float y = cy - juce::jlimit (-1.0f, 1.0f, cycle_[(size_t) i]) * amp;
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        g.setColour (kTrace.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setColour (kTrace);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

private:
    float raw (const juce::String& id) const
    {
        auto* p = apvts_->getParameter (prefix_ + id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    void rebuild()
    {
        using namespace pdhybrid;
        osc_.setType (static_cast<OscType> (juce::roundToInt (raw ("Type"))));
        osc_.setPdWave  (static_cast<PdWave> (juce::roundToInt (raw ("Wave"))));
        osc_.setPdWaveB (static_cast<PdWave> (juce::roundToInt (raw ("Wave2"))));
        osc_.setPdCombine (raw ("Combine") > 0.5f);
        osc_.setAmount      (raw ("Amount"));
        osc_.setPulseWidth  (raw ("PulseWidth"));
        osc_.setEngineParam (raw ("Engine"));
        osc_.setExcite (juce::roundToInt (raw ("Excite")));
        osc_.setEq (0.0, 0.0, 0.0);
        osc_.setTuning (0, 0, 0.0);
        osc_.setBaseFrequency (kSr / (double) kN);   // exactly one cycle in kN samples
        osc_.reset();
        osc_.excite();

        // Discard two periods so anything with internal state has settled.
        for (int i = 0; i < kN * 2; ++i)
            osc_.processSample();

        float peak = 1.0e-6f;
        for (int i = 0; i < kN; ++i)
        {
            const float s = osc_.processSample();
            cycle_[(size_t) i] = std::isfinite (s) ? s : 0.0f;
            peak = juce::jmax (peak, std::abs (cycle_[(size_t) i]));
        }
        for (auto& s : cycle_) s /= peak;   // normalise: this shows shape, not level
        repaint();
    }

    void timerCallback() override
    {
        static const char* ids[] = { "Type", "Wave", "Wave2", "Combine",
                                     "Amount", "PulseWidth", "Engine", "Excite" };
        bool changed = false;
        for (int i = 0; i < 8; ++i)
        {
            const float v = raw (ids[i]);
            if (! juce::approximatelyEqual (v, last_[i])) { last_[i] = v; changed = true; }
        }
        if (changed) rebuild();
    }

    static constexpr int    kN  = 256;      // samples per displayed cycle
    static constexpr double kSr = 48000.0;

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    juce::String prefix_;
    pdhybrid::OscillatorUnit osc_;
    std::vector<float> cycle_ { std::vector<float> (kN, 0.0f) };
    float last_[8] { -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f };
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
        bool         handle = false;   // draw a slider handle rather than a fill
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
                if (row.handle)
                {
                    // Slider look: a filled track up to the value, then a marker.
                    const int w = juce::roundToInt (std::abs (v) * plot.getWidth());
                    g.fillRect (plot.getX(), plot.getCentreY() - 1, juce::jmax (1, w), 2);
                    g.fillRect (plot.getX() + w - 1, plot.getY() - 1, 3, plot.getHeight() + 2);
                }
                else if (row.bipolar)
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
/** A filter's magnitude response, measured rather than modelled.

    Runs a real `FilterUnit` of the selected type, fires an impulse through it
    and transforms the result, so all five filter types — including the comb,
    allpass and PD resonator, which have no textbook curve — plot correctly from
    the same code. */
class FilterResponse : public juce::Component,
                       private juce::Timer
{
public:
    FilterResponse() { setInterceptsMouseClicks (false, false); }
    ~FilterResponse() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& type,
                 const juce::String& cutoff, const juce::String& reso, const juce::String& morph)
    {
        apvts_ = &s;
        type_ = type; cutoff_ = cutoff; reso_ = reso; morph_ = morph;
        startTimerHz (12);
        rebuild();
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "RESPONSE");
        if (apvts_ == nullptr) return;

        // Decade gridlines across 20 Hz .. 20 kHz.
        g.setColour (kGrid);
        for (float f : { 100.0f, 1000.0f, 10000.0f })
            g.fillRect (in.getX() + xForHz (f) * in.getWidth(), in.getY(), 1.0f, in.getHeight());
        g.fillRect (in.getX(), in.getCentreY(), in.getWidth(), 1.0f);

        juce::Path p;
        const int px = juce::jmax (2, (int) in.getWidth());
        for (int i = 0; i < px; ++i)
        {
            const float x  = in.getX() + (float) i;
            const float hz = 20.0f * std::pow (1000.0f, (float) i / (float) (px - 1));
            const float y  = in.getBottom() - yFor (magAtHz (hz)) * in.getHeight();
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }

        auto fill = p;
        fill.lineTo (in.getRight(), in.getBottom());
        fill.lineTo (in.getX(),     in.getBottom());
        fill.closeSubPath();
        g.setColour (kTrace.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (kTrace.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setColour (kTrace);
        g.strokePath (p, juce::PathStrokeType (1.5f));

        // Cutoff marker, labelled with the frequency the knob is actually set to.
        if (auto* c = apvts_->getParameter (cutoff_))
        {
            const float hz = c->convertFrom0to1 (c->getValue());
            const float mx = in.getX() + xForHz (hz) * in.getWidth();
            g.setColour (kAmber.withAlpha (0.7f));
            for (float y = in.getY(); y < in.getBottom(); y += 5.0f)
                g.fillRect (mx, y, 1.0f, 2.5f);
            g.setFont (monoF (8.0f));
            g.drawText (c->getCurrentValueAsText(),
                        juce::Rectangle<float> (mx + 3.0f, in.getY(), 52.0f, 11.0f),
                        juce::Justification::topLeft);
        }
    }

private:
    static float xForHz (float hz)
    { return juce::jlimit (0.0f, 1.0f, std::log (hz / 20.0f) / std::log (1000.0f)); }
    static float yFor (float db)
    { return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 66.0f); }   // -48 .. +18 dB

    float magAtHz (float hz) const
    {
        const float bin = hz * (float) kN / (float) kSr;
        const int   i   = juce::jlimit (0, kN / 2 - 1, (int) bin);
        return mag_[(size_t) i];
    }

    float raw (const juce::String& id) const
    {
        auto* p = apvts_->getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    void rebuild()
    {
        pdhybrid::FilterUnit f;
        f.setSampleRate (kSr);
        f.setType (static_cast<pdhybrid::FilterType> (juce::roundToInt (raw (type_))));
        f.configure (raw (cutoff_), raw (reso_), raw (morph_));
        f.reset();

        std::array<float, 2 * kN> buf {};
        for (int i = 0; i < kN; ++i)
            buf[(size_t) i] = f.processSample (i == 0 ? 1.0f : 0.0f);
        fft_.performFrequencyOnlyForwardTransform (buf.data());

        for (int i = 0; i < kN / 2; ++i)
            mag_[(size_t) i] = juce::Decibels::gainToDecibels (buf[(size_t) i] + 1.0e-7f);
        repaint();
    }

    void timerCallback() override
    {
        if (apvts_ == nullptr) return;
        const float now[4] { raw (type_), raw (cutoff_), raw (reso_), raw (morph_) };
        bool changed = false;
        for (int i = 0; i < 4; ++i)
            if (! juce::approximatelyEqual (now[i], last_[i])) { last_[i] = now[i]; changed = true; }
        if (changed) rebuild();
    }

    static constexpr int    kOrder = 10;
    static constexpr int    kN     = 1 << kOrder;
    static constexpr double kSr    = 48000.0;

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    juce::String type_, cutoff_, reso_, morph_;
    juce::dsp::FFT fft_ { kOrder };
    std::array<float, kN / 2> mag_ {};
    float last_[4] { -1e9f, -1e9f, -1e9f, -1e9f };
};

//==============================================================================
/** The overdrive's input-to-output transfer curve, so the nine distortion types
    stop being names in a list. Uses the same Waveshaper the voices use. */
class TransferCurve : public juce::Component,
                      private juce::Timer
{
public:
    TransferCurve() { setInterceptsMouseClicks (false, false); }
    ~TransferCurve() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& curve,
                 const juce::String& drive, const juce::String& bias)
    {
        apvts_ = &s; curve_ = curve; drive_ = drive; bias_ = bias;
        startTimerHz (12);
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "TRANSFER");
        if (apvts_ == nullptr) return;

        pdhybrid::Waveshaper sh;
        sh.setCurve (static_cast<pdhybrid::ShaperCurve> (juce::roundToInt (raw (curve_))));
        sh.setDrive (raw (drive_));
        sh.setBias  (raw (bias_));

        g.setColour (kGrid);
        g.fillRect (in.getCentreX(), in.getY(), 1.0f, in.getHeight());
        g.fillRect (in.getX(), in.getCentreY(), in.getWidth(), 1.0f);
        // Unity reference: what "no shaping" would look like.
        g.setColour (kEdge);
        g.drawLine (in.getX(), in.getBottom(), in.getRight(), in.getY(), 1.0f);

        juce::Path p;
        const int steps = 128;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / (float) steps;
            const float y = juce::jlimit (-1.2f, 1.2f, sh.process (x));
            const float px = in.getX() + (x * 0.5f + 0.5f) * in.getWidth();
            const float py = in.getCentreY() - y * in.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (px, py);
            else        p.lineTo (px, py);
        }
        g.setColour (kTrace.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setColour (kTrace);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

private:
    float raw (const juce::String& id) const
    {
        auto* p = apvts_->getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }
    void timerCallback() override
    {
        if (apvts_ == nullptr) return;
        const float now[3] { raw (curve_), raw (drive_), raw (bias_) };
        for (int i = 0; i < 3; ++i)
            if (! juce::approximatelyEqual (now[i], last_[i])) { last_[i] = now[i]; repaint(); }
        for (int i = 0; i < 3; ++i) last_[i] = now[i];
    }

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    juce::String curve_, drive_, bias_;
    float last_[3] { -1e9f, -1e9f, -1e9f };
};

//==============================================================================
/** The global EQ's four-band response, measured through the real GlobalEq so
    the curve is the filter, not an approximation of it. */
class EqResponse : public juce::Component,
                   private juce::Timer
{
public:
    EqResponse() { setInterceptsMouseClicks (false, false); }
    ~EqResponse() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s)
    {
        apvts_ = &s;
        startTimerHz (12);
        rebuild();
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "RESPONSE");
        if (apvts_ == nullptr) return;

        g.setColour (kGrid);
        for (float f : { 100.0f, 1000.0f, 10000.0f })
            g.fillRect (in.getX() + xForHz (f) * in.getWidth(), in.getY(), 1.0f, in.getHeight());
        g.setColour (kEdge);
        g.fillRect (in.getX(), in.getCentreY(), in.getWidth(), 1.0f);   // 0 dB

        juce::Path p;
        const int px = juce::jmax (2, (int) in.getWidth());
        for (int i = 0; i < px; ++i)
        {
            const float hz  = 20.0f * std::pow (1000.0f, (float) i / (float) (px - 1));
            const int   bin = juce::jlimit (0, kN / 2 - 1, (int) (hz * (float) kN / (float) kSr));
            const float y   = in.getCentreY() - juce::jlimit (-24.0f, 24.0f, mag_[(size_t) bin])
                                                  * in.getHeight() / 48.0f;
            if (i == 0) p.startNewSubPath (in.getX() + (float) i, y);
            else        p.lineTo (in.getX() + (float) i, y);
        }
        g.setColour (kTrace.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setColour (kTrace);
        g.strokePath (p, juce::PathStrokeType (1.6f));

        // A marker per band, at its frequency and gain.
        static const char* fq[4] { "geLowFreq", "geMid1Freq", "geMid2Freq", "geHighFreq" };
        static const char* gn[4] { "geLowGain", "geMid1Gain", "geMid2Gain", "geHighGain" };
        g.setFont (monoF (7.5f));
        for (int b = 0; b < 4; ++b)
        {
            const float hz = raw (fq[b]), db = raw (gn[b]);
            const float mx = in.getX() + xForHz (hz) * in.getWidth();
            const float my = in.getCentreY() - juce::jlimit (-24.0f, 24.0f, db) * in.getHeight() / 48.0f;
            g.setColour (kBg);    g.fillRect (mx - 3.0f, my - 3.0f, 6.0f, 6.0f);
            g.setColour (kTrace); g.drawRect (juce::Rectangle<float> (mx - 3.0f, my - 3.0f, 6.0f, 6.0f), 1.0f);
            g.setColour (kDim.withAlpha (0.7f));
            g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (mx - 18.0f, in.getBottom() - 10.0f, 36.0f, 10.0f),
                        juce::Justification::centred);
        }
    }

private:
    static float xForHz (float hz)
    { return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (20.0f, hz) / 20.0f) / std::log (1000.0f)); }

    float raw (const juce::String& id) const
    {
        auto* p = apvts_->getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    void rebuild()
    {
        pdhybrid::GlobalEq eq;
        eq.setSampleRate (kSr);
        static const char* fq[4] { "geLowFreq", "geMid1Freq", "geMid2Freq", "geHighFreq" };
        static const char* gn[4] { "geLowGain", "geMid1Gain", "geMid2Gain", "geHighGain" };
        for (int b = 0; b < 4; ++b)
            eq.setBand (b, raw (fq[b]), raw (gn[b]));

        std::array<float, 2 * kN> buf {};
        for (int i = 0; i < kN; ++i)
            buf[(size_t) i] = eq.processSample (i == 0 ? 1.0f : 0.0f);
        fft_.performFrequencyOnlyForwardTransform (buf.data());
        for (int i = 0; i < kN / 2; ++i)
            mag_[(size_t) i] = juce::Decibels::gainToDecibels (buf[(size_t) i] + 1.0e-7f);
        repaint();
    }

    void timerCallback() override
    {
        if (apvts_ == nullptr) return;
        static const char* ids[8] { "geLowFreq", "geLowGain", "geMid1Freq", "geMid1Gain",
                                    "geMid2Freq", "geMid2Gain", "geHighFreq", "geHighGain" };
        bool changed = false;
        for (int i = 0; i < 8; ++i)
        {
            const float v = raw (ids[i]);
            if (! juce::approximatelyEqual (v, last_[i])) { last_[i] = v; changed = true; }
        }
        if (changed) rebuild();
    }

    static constexpr int    kOrder = 10;
    static constexpr int    kN     = 1 << kOrder;
    static constexpr double kSr    = 48000.0;

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    juce::dsp::FFT fft_ { kOrder };
    std::array<float, kN / 2> mag_ {};
    float last_[8] { -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f };
};

//==============================================================================
/** Compressor gain reduction, read live off the audio thread. Amber, because
    it is the one output stage that moves on its own. */
class GainReductionMeter : public juce::Component,
                           private juce::Timer
{
public:
    GainReductionMeter() { setInterceptsMouseClicks (false, false); }
    ~GainReductionMeter() override { stopTimer(); }

    void setReader (std::function<float()> r) { read_ = std::move (r); startTimerHz (24); }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "GAIN REDUCTION");
        in = in.withTrimmedTop (9.0f);

        g.setColour (juce::Colour (0xff0e2116));
        g.fillRect (in);

        // Scale runs 0 dB at the right to -24 dB at the left, so reduction grows
        // leftwards from rest the way a hardware meter does.
        g.setColour (kGrid);
        for (int db = -6; db > -24; db -= 6)
            g.fillRect (in.getRight() + (float) db / 24.0f * in.getWidth(), in.getY(), 1.0f, in.getHeight());

        const float amt = juce::jlimit (0.0f, 24.0f, -gr_);
        const float w   = amt / 24.0f * in.getWidth();
        g.setColour (kAmber);
        g.fillRect (in.getRight() - w, in.getY(), w, in.getHeight());

        g.setFont (monoF (8.0f));
        g.setColour (amt > 0.05f ? kAmber : kDim.withAlpha (0.6f));
        g.drawText (juce::String (gr_, 1) + " dB", getLocalBounds().reduced (5, 2),
                    juce::Justification::topRight);
    }

private:
    void timerCallback() override
    {
        if (! read_) return;
        const float v = read_();
        if (! juce::approximatelyEqual (v, gr_)) { gr_ = v; repaint(); }
    }

    std::function<float()> read_;
    float gr_ = 0.0f;
};

//==============================================================================
/** Delay taps: the echo pattern the current time, feedback and mode produce. */
class DelayTaps : public juce::Component,
                  private juce::Timer
{
public:
    DelayTaps() { setInterceptsMouseClicks (false, false); }
    ~DelayTaps() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s) { apvts_ = &s; startTimerHz (10); }

    /** Maps a raw tap time to the time actually used, so the pattern holds
        still while SYNC is on and a knob moves inside one division. */
    void setTimeMapper (std::function<double (double)> fn)
    { timeMap_ = std::move (fn); repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "TAPS");
        if (apvts_ == nullptr) return;

        const float tL  = mapTime (raw ("delayTimeL")), tR = mapTime (raw ("delayTimeR"));
        const float fb  = juce::jlimit (0.0f, 0.95f, raw ("delayFeedback"));
        const int   mod = juce::roundToInt (raw ("delayMode"));   // 0 mono 1 stereo 2 ping-pong
        const float span = juce::jmax (0.05f, juce::jmax (tL, tR) * 6.0f);

        g.setColour (kGrid);
        g.fillRect (in.getX(), in.getCentreY(), in.getWidth(), 1.0f);

        // Dry hit, then successive echoes decaying by the feedback amount.
        float level = 1.0f;
        for (int n = 0; n < 12 && level > 0.02f; ++n)
        {
            const float t = (mod == 0 ? tL : (n % 2 == 0 ? tL : tR)) * (float) n;
            const float x = in.getX() + juce::jlimit (0.0f, 1.0f, t / span) * in.getWidth();
            const float h = level * in.getHeight() * 0.46f;
            // Ping-pong alternates sides; the others stay centred.
            const float up = (mod == 2 && n % 2 == 1) ? -1.0f : 1.0f;
            g.setColour (n == 0 ? kTrace : kTrace.withAlpha (0.35f + 0.6f * level));
            if (mod == 2) g.fillRect (x, in.getCentreY() - (up > 0 ? h : 0.0f), 1.6f, h);
            else          g.fillRect (x, in.getCentreY() - h, 1.6f, h * 2.0f);
            level *= fb;
            if (t > span) break;
        }
    }

private:
    float raw (const juce::String& id) const
    {
        auto* p = apvts_->getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }
    float mapTime (float seconds) const
    { return timeMap_ ? static_cast<float> (timeMap_ (seconds)) : seconds; }
    void timerCallback() override
    {
        if (apvts_ == nullptr) return;
        // Compare the drawn times, not the raw ones: with sync on a knob can
        // move a long way inside one division without changing the pattern.
        const float now[4] { mapTime (raw ("delayTimeL")), mapTime (raw ("delayTimeR")),
                             raw ("delayFeedback"), raw ("delayMode") };
        for (int i = 0; i < 4; ++i)
            if (! juce::approximatelyEqual (now[i], last_[i])) { last_[i] = now[i]; repaint(); }
    }

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    std::function<double (double)> timeMap_;   // raw seconds -> played seconds
    float last_[4] { -1e9f, -1e9f, -1e9f, -1e9f };
};

//==============================================================================
/** Reverb decay envelope, from size and damping. */
class ReverbDecay : public juce::Component,
                    private juce::Timer
{
public:
    ReverbDecay() { setInterceptsMouseClicks (false, false); }
    ~ReverbDecay() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s) { apvts_ = &s; startTimerHz (10); }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "DECAY");
        if (apvts_ == nullptr) return;

        const float size = juce::jlimit (0.0f, 1.0f, raw ("reverbSize"));
        const float damp = juce::jlimit (0.0f, 1.0f, raw ("reverbDamp"));
        // Longer tail with size; damping shortens it and rounds the front.
        const float k = 1.0f / (0.12f + size * 2.2f) * (1.0f + damp * 1.4f);

        juce::Path p;
        p.startNewSubPath (in.getX(), in.getBottom());
        const int px = juce::jmax (2, (int) in.getWidth());
        for (int i = 0; i < px; ++i)
        {
            const float t = (float) i / (float) (px - 1) * 3.0f;
            const float a = std::exp (-k * t) * (1.0f - std::exp (-t * 26.0f));
            p.lineTo (in.getX() + (float) i, in.getBottom() - a * in.getHeight() * 0.94f);
        }

        auto fill = p;
        fill.lineTo (in.getRight(), in.getBottom());
        fill.closeSubPath();
        g.setColour (kTrace.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (kTrace.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setColour (kTrace);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

private:
    float raw (const juce::String& id) const
    {
        auto* p = apvts_->getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }
    void timerCallback() override
    {
        if (apvts_ == nullptr) return;
        const float now[2] { raw ("reverbSize"), raw ("reverbDamp") };
        for (int i = 0; i < 2; ++i)
            if (! juce::approximatelyEqual (now[i], last_[i])) { last_[i] = now[i]; repaint(); }
    }

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    float last_[2] { -1e9f, -1e9f };
};

//==============================================================================
/** What the velocity curve setting actually does to incoming velocity, drawn
    against a linear reference. Mirrors the mapping in handleMidi exactly. */
class VelocityCurve : public juce::Component,
                      private juce::Timer
{
public:
    VelocityCurve() { setInterceptsMouseClicks (false, false); }
    ~VelocityCurve() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    { apvts_ = &s; id_ = id; startTimerHz (10); }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "VELOCITY CURVE");
        if (apvts_ == nullptr) return;

        auto* p = apvts_->getParameter (id_);
        const int mode = p != nullptr ? juce::roundToInt (p->convertFrom0to1 (p->getValue())) : 0;

        g.setColour (kGrid);
        g.fillRect (in.getCentreX(), in.getY(), 1.0f, in.getHeight());
        g.fillRect (in.getX(), in.getCentreY(), in.getWidth(), 1.0f);
        g.setColour (kEdge);   // linear reference
        g.drawLine (in.getX(), in.getBottom(), in.getRight(), in.getY(), 1.0f);

        juce::Path path;
        for (int i = 0; i <= 64; ++i)
        {
            const float v = (float) i / 64.0f;
            float out = v;
            switch (mode)
            {
                case 1: out = std::sqrt (v); break;   // Soft
                case 2: out = v * v;         break;   // Hard
                case 3: out = 1.0f;          break;   // Fixed
                default: break;                       // Linear
            }
            const float x = in.getX() + v * in.getWidth();
            const float y = in.getBottom() - out * in.getHeight();
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
        g.setColour (kTrace.withAlpha (0.25f));
        g.strokePath (path, juce::PathStrokeType (3.0f));
        g.setColour (kTrace);
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

private:
    void timerCallback() override
    {
        if (apvts_ == nullptr) return;
        auto* p = apvts_->getParameter (id_);
        const float v = p != nullptr ? p->getValue() : 0.0f;
        if (! juce::approximatelyEqual (v, last_)) { last_ = v; repaint(); }
    }
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    juce::String id_;
    float last_ = -1.0f;
};

//==============================================================================
/** Per-pitch-class cents deviation from equal temperament, so "Just Intonation"
    stops being a word in a menu. Reads the same table the voices tune from. */
class ScaleOffsets : public juce::Component,
                     private juce::Timer
{
public:
    ScaleOffsets() { setInterceptsMouseClicks (false, false); }
    ~ScaleOffsets() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    { apvts_ = &s; id_ = id; startTimerHz (10); }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "SCALE OFFSET (CENTS)");
        if (apvts_ == nullptr) return;

        auto* p = apvts_->getParameter (id_);
        const int scale = p != nullptr ? juce::roundToInt (p->convertFrom0to1 (p->getValue())) : 0;

        in = in.withTrimmedBottom (10.0f);
        const float cy = in.getCentreY();
        g.setColour (kEdge);
        g.fillRect (in.getX(), cy, in.getWidth(), 1.0f);

        static const char* names[12] { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        const float w = in.getWidth() / 12.0f;
        g.setFont (monoF (7.5f));
        for (int pc = 0; pc < 12; ++pc)
        {
            const float cents = (float) pdhybrid::tuningCentsOffset (scale, pc);
            const float h = juce::jlimit (-1.0f, 1.0f, cents / 20.0f) * in.getHeight() * 0.42f;
            const float x = in.getX() + (float) pc * w;

            g.setColour (std::abs (cents) < 0.05f ? kDim.withAlpha (0.4f) : kTrace);
            g.fillRect (x + w * 0.25f, h >= 0.0f ? cy - h : cy,
                        w * 0.5f, juce::jmax (1.0f, std::abs (h)));

            g.setColour (kDim.withAlpha (0.7f));
            g.drawText (names[pc], juce::Rectangle<float> (x, (float) getHeight() - 12.0f, w, 10.0f),
                        juce::Justification::centred);
        }
    }

private:
    void timerCallback() override
    {
        if (apvts_ == nullptr) return;
        auto* p = apvts_->getParameter (id_);
        const float v = p != nullptr ? p->getValue() : 0.0f;
        if (! juce::approximatelyEqual (v, last_)) { last_ = v; repaint(); }
    }
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    juce::String id_;
    float last_ = -1.0f;
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

        in = in.withTrimmedTop (10.0f).withTrimmedBottom (12.0f);
        const float bw = 46.0f, bh = 17.0f;
        const float cy = in.getCentreY();
        const float dy = 20.0f;          // vertical offset of the parallel lanes

        auto block = [&] (float bx, float by, float w, const juce::String& t, bool lit)
        {
            juce::Rectangle<float> b (bx, by - bh * 0.5f, w, bh);
            g.setColour (lit ? kTrace : kEdge);
            g.drawRect (b, 1.0f);
            g.setFont (monoF (8.0f));
            g.setColour (lit ? kTrace : kDim.withAlpha (0.45f));
            g.drawText (t, b, juce::Justification::centred);
        };
        static const float dashes[] { 3.0f, 3.0f };
        auto wire = [&] (float x0, float y0, float x1, float y1, bool active)
        {
            g.setColour (active ? kTrace : kDim.withAlpha (0.3f));
            if (active)
                g.drawLine (x0, y0, x1, y1, 1.1f);
            else
                g.drawDashedLine (juce::Line<float> (x0, y0, x1, y1), dashes, 2, 1.0f);
        };
        auto arrow = [&] (float x1, float y1)
        {
            juce::Path h;
            h.addTriangle (x1, y1, x1 - 4.0f, y1 - 3.0f, x1 - 4.0f, y1 + 3.0f);
            g.setColour (kTrace);
            g.fillPath (h);
        };

        float x = in.getX();
        block (x, cy, bw, "OSC MIX", true);
        x += bw;

        if (drivePre)
        {
            wire (x, cy, x + 14.0f, cy, true); arrow (x + 14.0f, cy);
            x += 14.0f;
            block (x, cy, bw, "DRIVE", drive);
            x += bw;
        }

        // Filters. Series runs left to right on the centre line; parallel splits
        // into two lanes that rejoin, so the topology reads at a glance.
        const float fx1 = x + 14.0f;
        wire (x, cy, fx1, cy, true); arrow (fx1, cy);

        if (routing == 2)
        {
            const float top = cy - dy, bot = cy + dy;
            wire (fx1, cy, fx1, top, true);
            wire (fx1, cy, fx1, bot, true);
            block (fx1, top, bw, "FLT 1", true);
            block (fx1, bot, bw, "FLT 2", true);
            const float join = fx1 + bw + 14.0f;
            wire (fx1 + bw, top, join, top, true);
            wire (fx1 + bw, bot, join, bot, true);
            wire (join, top, join, bot, true);
            wire (join, cy, join + 10.0f, cy, true);
            x = join + 10.0f;
        }
        else
        {
            block (fx1, cy, bw, "FLT 1", true);
            x = fx1 + bw;
            const float second = x + 14.0f;
            const bool  on = (routing == 1);
            wire (x, cy, second, cy, on);
            if (on) arrow (second, cy);
            block (second, cy, bw, "FLT 2", on);
            x = second + bw;
        }

        if (! drivePre)
        {
            wire (x, cy, x + 14.0f, cy, true); arrow (x + 14.0f, cy);
            x += 14.0f;
            block (x, cy, bw, "DRIVE", drive);
            x += bw;
        }

        wire (x, cy, x + 14.0f, cy, true); arrow (x + 14.0f, cy);
        x += 14.0f;
        block (x, cy, bw - 8.0f, "AMP", true);
        x += bw - 8.0f;

        static const char* fxText[3] { "DLY>REV", "REV>DLY", "REV+DLY" };
        wire (x, cy, x + 14.0f, cy, true); arrow (x + 14.0f, cy);
        x += 14.0f;
        block (x, cy, bw + 12.0f, fxText[juce::jlimit (0, 2, fx)], true);

        g.setFont (monoF (8.0f));
        g.setColour (kDim.withAlpha (0.6f));
        g.drawText (juce::String (routing == 0 ? "single filter"
                                : routing == 1 ? "filters in series" : "filters in parallel")
                        + (drivePre ? "  |  drive pre-filter" : "  |  drive post-filter")
                        + "  |  dashed = inactive",
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

//==============================================================================
/** The chord-mode keyboard: quality zone, root zone, and what is sounding.

    Draws a real piano layout -- seven white keys per octave with black keys only
    between C-D, D-E, F-G, G-A and A-B, each 62% of a white key's width and 62%
    of its height, centred on the white-key boundary. Anything looser than that
    reads as "not a keyboard" at a glance.

    The quality names are printed on the twelve quality-zone keys, so the mapping
    needs no separate legend. The latched quality lights green and the held root
    amber -- the colour this panel already uses for "this is live". */
class ChordKeyboard : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kOctaves     = 3;
    static constexpr int kWhitePerOct = 7;
    static constexpr int kNumWhite    = kOctaves * kWhitePerOct;

    ChordKeyboard() { setInterceptsMouseClicks (false, false); }
    ~ChordKeyboard() override { stopTimer(); }

    void attach (juce::AudioProcessorValueTreeState& s,
                 std::function<int()> heldRoot,
                 std::function<int (int*, int)> voiced)
    {
        apvts_    = &s;
        heldRoot_ = std::move (heldRoot);
        voiced_   = std::move (voiced);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto in = drawFrame (g, getLocalBounds().toFloat(), "KEYBOARD");
        if (apvts_ == nullptr) return;

        static const char* kQualityNames[12] = { "maj", "min", "7", "m7", "maj7", "6",
                                                 "m7b5", "dim7", "aug", "sus2", "sus4", "m6" };
        static const int kWhiteSemi[7]  = { 0, 2, 4, 5, 7, 9, 11 };
        static const int kBlackAfter[5] = { 0, 1, 3, 4, 5 };   // C, D, F, G, A
        static const int kBlackSemi[5]  = { 1, 3, 6, 8, 10 };

        const int split   = juce::roundToInt (raw ("chordSplit"));
        const int quality = juce::roundToInt (raw ("chordQuality"));
        const int zoneLow = split - 12;
        const int root    = heldRoot_ ? heldRoot_() : -1;

        int sounding[8] = { 0 };
        const int nSounding = voiced_ ? voiced_ (sounding, 8) : 0;
        auto isSounding = [&] (int midi)
        {
            for (int i = 0; i < nSounding; ++i) if (sounding[i] == midi) return true;
            return false;
        };

        // Start from the octave holding the quality zone so it is always shown.
        const int   firstMidi = (zoneLow / 12) * 12;
        const float w = in.getWidth() / (float) kNumWhite;
        const float h = in.getHeight();

        g.setFont (monoF (7.5f));

        for (int i = 0; i < kNumWhite; ++i)
        {
            const int midi = firstMidi + (i / 7) * 12 + kWhiteSemi[i % 7];
            const juce::Rectangle<float> r (in.getX() + i * w, in.getY(), w, h);

            const bool inZone = (midi >= zoneLow && midi < split);
            const bool isSel  = inZone && (midi - zoneLow) == quality;
            const bool isRoot = (midi == root);

            g.setColour (isSel  ? juce::Colour (0xff1d5c3c)
                       : isRoot ? juce::Colour (0xff3a2a0c)
                       : inZone ? juce::Colour (0xff0b2618)
                                : juce::Colour (0xff07130d));
            g.fillRect (r);

            if (isSounding (midi) && ! isRoot)
            {
                g.setColour (kTrace.withAlpha (0.30f));
                g.fillRect (r.reduced (2.0f));
            }

            g.setColour (isSel ? kTrace : (isRoot ? kAmber : kEdge));
            g.drawRect (r, 1.0f);

            if (inZone)
            {
                g.setColour (isSel ? juce::Colour (0xffbdf5d6) : kDim);
                g.drawText (kQualityNames[midi - zoneLow],
                            r.withTrimmedBottom (2.0f), juce::Justification::centredBottom);
            }
        }

        const float bw = w * 0.62f, bh = h * 0.62f;
        for (int oct = 0; oct < kOctaves; ++oct)
            for (int b = 0; b < 5; ++b)
            {
                const int   midi     = firstMidi + oct * 12 + kBlackSemi[b];
                const float boundary = in.getX() + (oct * 7 + kBlackAfter[b] + 1) * w;
                const juce::Rectangle<float> r (boundary - bw * 0.5f, in.getY(), bw, bh);

                const bool inZone = (midi >= zoneLow && midi < split);
                const bool isSel  = inZone && (midi - zoneLow) == quality;
                const bool isRoot = (midi == root);

                g.setColour (isSel  ? juce::Colour (0xff1d5c3c)
                           : isRoot ? juce::Colour (0xff3a2a0c)
                           : inZone ? juce::Colour (0xff061a10)
                                    : juce::Colour (0xff010402));
                g.fillRect (r);

                if (isSounding (midi) && ! isRoot)
                {
                    g.setColour (kTrace.withAlpha (0.30f));
                    g.fillRect (r.reduced (1.5f));
                }

                g.setColour (isSel ? kTrace : (isRoot ? kAmber : kEdge));
                g.drawRect (r, 1.0f);
            }

        // Split marker on the real zone boundary.
        const int idx = splitWhiteIndex (firstMidi, split);
        if (idx >= 0 && idx <= kNumWhite)
        {
            g.setColour (kAmber);
            g.fillRect (in.getX() + idx * w - 1.0f, in.getY() - 2.0f, 2.0f, h + 4.0f);
        }
    }

private:
    /** How many white keys sit below `split`, counting from `firstMidi`. */
    static int splitWhiteIndex (int firstMidi, int split)
    {
        static const int kWhiteSemi[7] = { 0, 2, 4, 5, 7, 9, 11 };
        int idx = 0;
        for (int i = 0; i < kNumWhite; ++i)
        {
            const int midi = firstMidi + (i / 7) * 12 + kWhiteSemi[i % 7];
            if (midi < split) idx = i + 1;
        }
        return idx;
    }

    float raw (const juce::String& id) const
    {
        auto* p = apvts_->getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    void timerCallback() override { repaint(); }

    juce::AudioProcessorValueTreeState* apvts_ = nullptr;
    std::function<int()> heldRoot_;
    std::function<int (int*, int)> voiced_;
};

} // namespace pdui
