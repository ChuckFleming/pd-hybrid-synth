#include "PluginEditor.h"

namespace {
constexpr int kKnobW    = 90;
constexpr int kKnobH    = 96;
constexpr int kPad      = 12;
constexpr int kHeaderH  = 22;
constexpr int kTitleH   = 40;
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
    // --- Oscillator ---
    Section osc;
    osc.title = "Oscillator";
    osc.knobs = { &addKnob ("amount", "PD Amount") };

    // --- Filter (with type selector) ---
    filterTypeLabel.setText ("Filter Type", juce::dontSendNotification);
    filterTypeLabel.setJustificationType (juce::Justification::centredLeft);
    filterTypeLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    addAndMakeVisible (filterTypeLabel);

    filterTypeBox.addItemList ({ "Ladder", "PD Resonator", "Comb", "Allpass" }, 1);
    addAndMakeVisible (filterTypeBox);
    filterTypeAttachment = std::make_unique<ComboBoxAttachment> (proc.apvts, "filterType", filterTypeBox);

    Section filter;
    filter.title = "Filter";
    filter.knobs = { &addKnob ("cutoff",    "Cutoff"),
                     &addKnob ("resonance", "Resonance"),
                     &addKnob ("filterMorph", "Morph") };

    // --- Overdrive ---
    Section drive;
    drive.title = "Overdrive";
    drive.knobs = { &addKnob ("drive", "Drive"),
                    &addKnob ("bias",  "Bias"),
                    &addKnob ("gain",  "Gain") };

    // --- Envelope ---
    Section envelope;
    envelope.title = "Amp Envelope";
    envelope.knobs = { &addKnob ("attack",  "Attack"),
                       &addKnob ("decay",   "Decay"),
                       &addKnob ("sustain", "Sustain"),
                       &addKnob ("release", "Release") };

    sections = { osc, filter, drive, envelope };

    setSize (4 * (kKnobW + kPad) + 2 * kPad,
             kTitleH + 2 * (kHeaderH + kKnobH + kPad) + kPad);
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
}

void PDHybridEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (kTitleH);

    auto layoutRow = [this] (Section& s, juce::Rectangle<int> row)
    {
        s.bounds = row;
        auto header = row.removeFromTop (kHeaderH);

        // Filter section: put the type selector on its header row (right side).
        if (s.title == "Filter")
        {
            auto box = header.removeFromRight (220).reduced (4, 2);
            filterTypeBox.setBounds (box.removeFromRight (140));
            filterTypeLabel.setBounds (box);
        }

        auto content = row.reduced (kPad, kPad);
        for (auto* k : s.knobs)
        {
            auto cell = content.removeFromLeft (kKnobW + kPad).reduced (kPad / 2, 0);
            k->label.setBounds  (cell.removeFromTop (18));
            k->slider.setBounds (cell.removeFromTop (kKnobH));
        }
    };

    const int rowH = kHeaderH + kKnobH + kPad;

    auto top = area.removeFromTop (rowH);
    layoutRow (sections[0], top.removeFromLeft (2 * (kKnobW + kPad)));  // Oscillator
    layoutRow (sections[1], top);                                      // Filter

    auto bottom = area.removeFromTop (rowH);
    layoutRow (sections[2], bottom.removeFromLeft (3 * (kKnobW + kPad))); // Overdrive
    layoutRow (sections[3], bottom);                                      // Envelope

    repaint();
}
