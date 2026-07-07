#include "PluginEditor.h"

namespace {
constexpr int kKnobW    = 90;
constexpr int kKnobH    = 96;
constexpr int kPad      = 12;
constexpr int kHeaderH  = 22;
constexpr int kTitleH   = 40;
constexpr int kMatrixRowH = 30;
constexpr int kComboW   = 130;

const juce::StringArray kOscTypeNames { "Phase Distortion", "Saw", "Square", "Triangle", "Pulse" };
const juce::StringArray kPdWaveNames  { "Sawtooth", "Square", "Pulse", "Double Sine",
                                        "Saw-Pulse", "Resonant I", "Resonant II", "Resonant III" };

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

juce::ComboBox& PDHybridEditor::addCombo (const juce::String& paramId,
                                          const juce::StringArray& items)
{
    auto box = std::make_unique<juce::ComboBox>();
    box->addItemList (items, 1);
    addAndMakeVisible (*box);
    comboAttachments.push_back (
        std::make_unique<ComboBoxAttachment> (proc.apvts, paramId, *box));
    combos.push_back (std::move (box));
    return *combos.back();
}

PDHybridEditor::PDHybridEditor (PDHybridAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    // --- Oscillator A ---
    oscATypeBox = &addCombo ("oscAType", kOscTypeNames);
    oscAWaveBox = &addCombo ("oscAWave", kPdWaveNames);
    Section oscA;
    oscA.title  = "Osc A";
    oscA.combos = { oscATypeBox, oscAWaveBox };
    oscA.knobs  = { &addKnob ("oscAAmount", "PD Amount"),
                    &addKnob ("oscAPulseWidth", "Pulse Width"),
                    &addKnob ("oscAOctave", "Octave"),
                    &addKnob ("oscASemi", "Semitone"),
                    &addKnob ("oscAFine", "Fine") };

    // --- Oscillator B ---
    oscBTypeBox = &addCombo ("oscBType", kOscTypeNames);
    oscBWaveBox = &addCombo ("oscBWave", kPdWaveNames);
    Section oscB;
    oscB.title  = "Osc B";
    oscB.combos = { oscBTypeBox, oscBWaveBox };
    oscB.knobs  = { &addKnob ("oscBAmount", "PD Amount"),
                    &addKnob ("oscBPulseWidth", "Pulse Width"),
                    &addKnob ("oscBOctave", "Octave"),
                    &addKnob ("oscBSemi", "Semitone"),
                    &addKnob ("oscBFine", "Fine") };

    // --- Mixer ---
    Section mixer;
    mixer.title = "Mixer";
    mixer.knobs = { &addKnob ("oscALevel", "Osc A"),
                    &addKnob ("oscBLevel", "Osc B"),
                    &addKnob ("noiseLevel", "Noise") };

    // --- Filter (with type selector) ---
    filterTypeBox = &addCombo ("filterType",
        { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" });
    Section filter;
    filter.title  = "Filter";
    filter.combos = { filterTypeBox };
    filter.knobs  = { &addKnob ("cutoff", "Cutoff"),
                      &addKnob ("resonance", "Resonance"),
                      &addKnob ("filterMorph", "Morph"),
                      &addKnob ("keyTrack", "Key Track"),
                      &addKnob ("filterEnvAmount", "Env Amt") };

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
    lfoWaveBox = &addCombo ("lfoWave", { "Sine", "Triangle", "Square", "Saw" });
    Section lfo;
    lfo.title  = "LFO";
    lfo.combos = { lfoWaveBox };
    lfo.knobs  = { &addKnob ("lfoRate", "Rate") };

    // --- Mod Envelope ---
    Section modEnv;
    modEnv.title = "Mod Envelope";
    modEnv.knobs = { &addKnob ("modEnvA", "Attack"),
                     &addKnob ("modEnvD", "Decay"),
                     &addKnob ("modEnvS", "Sustain"),
                     &addKnob ("modEnvR", "Release") };

    // --- Filter Envelope ---
    Section filterEnv;
    filterEnv.title = "Filter Envelope";
    filterEnv.knobs = { &addKnob ("filterEnvA", "Attack"),
                        &addKnob ("filterEnvD", "Decay"),
                        &addKnob ("filterEnvS", "Sustain"),
                        &addKnob ("filterEnvR", "Release") };

    // --- Stereo ---
    Section stereo;
    stereo.title = "Stereo";
    stereo.knobs = { &addKnob ("pan", "Pan"),
                     &addKnob ("panSpread", "Spread") };

    sections = { oscA, oscB, mixer, filter, drive, envelope, lfo, modEnv, filterEnv, stereo };

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

    const int u    = kKnobW + kPad;
    const int rowH = kHeaderH + 18 + kKnobH + kPad;
    setSize (10 * u + 4 * kPad,
             kTitleH + 5 * rowH + kHeaderH + 4 * kMatrixRowH + 2 * kPad);
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

    auto layoutRow = [] (Section& s, juce::Rectangle<int> row)
    {
        s.bounds = row;
        auto header = row.removeFromTop (kHeaderH);

        for (auto it = s.combos.rbegin(); it != s.combos.rend(); ++it)
            (*it)->setBounds (header.removeFromRight (kComboW).reduced (4, 3));

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

    auto placeRow = [&] (Section& a, int wa, Section& b, int wb)
    {
        auto row = area.removeFromTop (rowH);
        row.removeFromLeft (kPad);
        layoutRow (a, row.removeFromLeft (wa * u));
        row.removeFromLeft (kPad);
        layoutRow (b, row.removeFromLeft (wb * u));
    };

    placeRow (sections[0], 5, sections[2], 3);   // Osc A     | Mixer
    placeRow (sections[1], 5, sections[3], 5);   // Osc B     | Filter (+ key track / env amt)
    placeRow (sections[4], 3, sections[5], 4);   // Overdrive | Amp Envelope
    placeRow (sections[8], 4, sections[7], 4);   // Filter Env| Mod Envelope
    placeRow (sections[6], 2, sections[9], 2);   // LFO       | Stereo

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
