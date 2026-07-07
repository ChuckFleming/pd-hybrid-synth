#include "PluginEditor.h"

namespace {
constexpr int kKnobW    = 90;
constexpr int kKnobH    = 96;
constexpr int kPad      = 12;
constexpr int kHeaderH  = 22;
constexpr int kTitleH   = 40;
constexpr int kMatrixRowH = 30;

const juce::StringArray kSrcNames { "None", "Mod Env", "LFO", "Velocity", "Pressure",
                                    "Timbre", "Pitch Bend", "Key Track", "Mod Wheel" };
const juce::StringArray kDstNames { "None", "Pitch", "PD Amount", "Pulse Width", "Cutoff",
                                    "Resonance", "Morph", "Drive", "Amplitude" };
}

PDHybridEditor::LabeledKnob& PDHybridEditor::addKnob (const juce::String& paramId,
                                                      const juce::String& text)
{
    auto knob = std::make_unique<LabeledKnob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW, 18);
    addAndMakeVisible (knob->slider);

    knob->label.setText (text, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setFont (juce::Font (13.0f, juce::Font::bold));
    addAndMakeVisible (knob->label);

    knob->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, knob->slider);

    knobs.push_back (std::move (knob));
    return *knobs.back();
}

PDHybridEditor::PDHybridEditor (PDHybridAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    // --- Oscillator (with type selector) ---
    oscTypeBox.addItemList ({ "Phase Distortion", "Saw", "Square", "Triangle", "Pulse" }, 1);
    addAndMakeVisible (oscTypeBox);
    oscTypeAttachment = std::make_unique<ComboBoxAttachment> (proc.apvts, "oscType", oscTypeBox);

    Section osc;
    osc.title = "Oscillator";
    osc.knobs = { &addKnob ("amount", "PD Amount"),
                  &addKnob ("pulseWidth", "Pulse Width") };

    // --- Filter (with type selector) ---
    filterTypeBox.addItemList ({ "Ladder", "PD Resonator", "Comb", "Allpass" }, 1);
    addAndMakeVisible (filterTypeBox);
    filterTypeAttachment = std::make_unique<ComboBoxAttachment> (proc.apvts, "filterType", filterTypeBox);

    Section filter;
    filter.title = "Filter";
    filter.knobs = { &addKnob ("cutoff", "Cutoff"),
                     &addKnob ("resonance", "Resonance"),
                     &addKnob ("filterMorph", "Morph") };

    // --- Overdrive ---
    Section drive;
    drive.title = "Overdrive";
    drive.knobs = { &addKnob ("drive", "Drive"),
                    &addKnob ("bias", "Bias"),
                    &addKnob ("gain", "Gain") };

    // --- Amp Envelope ---
    Section envelope;
    envelope.title = "Amp Envelope";
    envelope.knobs = { &addKnob ("attack", "Attack"),
                       &addKnob ("decay", "Decay"),
                       &addKnob ("sustain", "Sustain"),
                       &addKnob ("release", "Release") };

    // --- LFO (with waveform selector) ---
    lfoWaveBox.addItemList ({ "Sine", "Triangle", "Square", "Saw" }, 1);
    addAndMakeVisible (lfoWaveBox);
    lfoWaveAttachment = std::make_unique<ComboBoxAttachment> (proc.apvts, "lfoWave", lfoWaveBox);

    Section lfo;
    lfo.title = "LFO";
    lfo.knobs = { &addKnob ("lfoRate", "Rate") };

    // --- Mod Envelope ---
    Section modEnv;
    modEnv.title = "Mod Envelope";
    modEnv.knobs = { &addKnob ("modEnvA", "Attack"),
                     &addKnob ("modEnvD", "Decay"),
                     &addKnob ("modEnvS", "Sustain"),
                     &addKnob ("modEnvR", "Release") };

    sections = { osc, filter, drive, envelope, lfo, modEnv };

    // --- Modulation matrix rows ---
    for (int i = 0; i < 4; ++i)
    {
        const auto s = juce::String (i + 1);

        modSrcBox[i].addItemList (kSrcNames, 1);
        modDestBox[i].addItemList (kDstNames, 1);
        addAndMakeVisible (modSrcBox[i]);
        addAndMakeVisible (modDestBox[i]);

        modSrcAtt[i]  = std::make_unique<ComboBoxAttachment> (proc.apvts, "mod" + s + "Source", modSrcBox[i]);
        modDestAtt[i] = std::make_unique<ComboBoxAttachment> (proc.apvts, "mod" + s + "Dest",   modDestBox[i]);

        modDepthSlider[i].setSliderStyle (juce::Slider::LinearHorizontal);
        modDepthSlider[i].setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
        addAndMakeVisible (modDepthSlider[i]);
        modDepthAtt[i] = std::make_unique<SliderAttachment> (proc.apvts, "mod" + s + "Depth", modDepthSlider[i]);
    }

    const int rowH = kHeaderH + 18 + kKnobH + kPad;
    setSize (7 * (kKnobW + kPad) + 3 * kPad,
             kTitleH + 3 * rowH + kHeaderH + 4 * kMatrixRowH + 2 * kPad);
}

void PDHybridEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff20242b));

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("PD Hybrid Synth", getLocalBounds().removeFromTop (kTitleH),
                juce::Justification::centred);

    g.setFont (juce::Font (14.0f, juce::Font::bold));
    for (const auto& s : sections)
    {
        auto header = s.bounds.withHeight (kHeaderH);
        g.setColour (juce::Colour (0xff2c3440));
        g.fillRect (header);
        g.setColour (juce::Colour (0xff8fb7ff));
        g.drawText (" " + s.title, header, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff2c3440));
        g.drawRect (s.bounds, 1);
    }

    // Modulation matrix header + labels.
    auto mh = matrixBounds.withHeight (kHeaderH);
    g.setColour (juce::Colour (0xff2c3440));
    g.fillRect (mh);
    g.setColour (juce::Colour (0xff8fb7ff));
    g.drawText (" Modulation Matrix   (Source -> Destination x Depth)", mh,
                juce::Justification::centredLeft);
}

void PDHybridEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (kTitleH);

    auto layoutRow = [this] (Section& s, juce::Rectangle<int> row)
    {
        s.bounds = row;
        auto header = row.removeFromTop (kHeaderH);

        if (s.title == "Filter")
            filterTypeBox.setBounds (header.removeFromRight (150).reduced (4, 3));
        else if (s.title == "Oscillator")
            oscTypeBox.setBounds (header.removeFromRight (150).reduced (4, 3));
        else if (s.title == "LFO")
            lfoWaveBox.setBounds (header.removeFromRight (120).reduced (4, 3));

        auto content = row.reduced (kPad / 2, 0);
        for (auto* k : s.knobs)
        {
            auto cell = content.removeFromLeft (kKnobW + kPad).reduced (kPad / 2, 0);
            k->label.setBounds  (cell.removeFromTop (18));
            k->slider.setBounds (cell.removeFromTop (kKnobH));
        }
    };

    const int u    = kKnobW + kPad;
    const int rowH = kHeaderH + 18 + kKnobH + kPad;

    auto row1 = area.removeFromTop (rowH);
    row1.removeFromLeft (kPad);
    layoutRow (sections[0], row1.removeFromLeft (2 * u));   // Oscillator
    row1.removeFromLeft (kPad);
    layoutRow (sections[1], row1.removeFromLeft (3 * u));   // Filter

    auto row2 = area.removeFromTop (rowH);
    row2.removeFromLeft (kPad);
    layoutRow (sections[2], row2.removeFromLeft (3 * u));   // Overdrive
    row2.removeFromLeft (kPad);
    layoutRow (sections[3], row2.removeFromLeft (4 * u));   // Amp Envelope

    auto row3 = area.removeFromTop (rowH);
    row3.removeFromLeft (kPad);
    layoutRow (sections[4], row3.removeFromLeft (2 * u));   // LFO
    row3.removeFromLeft (kPad);
    layoutRow (sections[5], row3.removeFromLeft (4 * u));   // Mod Envelope

    // Modulation matrix.
    matrixBounds = area.reduced (kPad, 0).withTrimmedBottom (kPad);
    auto rows = matrixBounds;
    rows.removeFromTop (kHeaderH);
    for (int i = 0; i < 4; ++i)
    {
        auto r = rows.removeFromTop (kMatrixRowH).reduced (2, 2);
        modSrcBox[i].setBounds  (r.removeFromLeft (180));
        r.removeFromLeft (kPad);
        modDestBox[i].setBounds (r.removeFromLeft (180));
        r.removeFromLeft (kPad);
        modDepthSlider[i].setBounds (r);
    }
}
