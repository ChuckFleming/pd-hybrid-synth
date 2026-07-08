#include "PluginEditor.h"

namespace {
constexpr int kKnobW    = 66;
constexpr int kKnobH    = 64;   // rotary + text box below
constexpr int kLabelH   = 14;
constexpr int kCellW    = 72;   // one knob cell (knob + gutter)
constexpr int kCellH    = kLabelH + kKnobH;
constexpr int kHeaderH  = 20;
constexpr int kComboRowH = 22;
constexpr int kPad      = 8;
constexpr int kTitleH   = 34;
constexpr int kMatrixRowH = 26;

const juce::StringArray kOscTypeNames { "Phase Distortion", "Saw", "Square", "Triangle", "Pulse" };
const juce::StringArray kPdWaveNames  { "Sawtooth", "Square", "Pulse", "Double Sine",
                                        "Saw-Pulse", "Resonant I", "Resonant II", "Resonant III" };
const juce::StringArray kSrcNames { "None", "Mod Env", "LFO", "Velocity", "Pressure",
                                    "Timbre", "Pitch Bend", "Key Track", "Mod Wheel", "LFO 2",
                                    "Multi Env" };
const juce::StringArray kLfoWaveNames { "Sine", "Triangle", "Square", "Saw", "Ramp Down",
                                        "Sample & Hold", "Smooth Random", "Exponential" };
const juce::StringArray kSyncNames { "Free", "1/1", "1/2", "1/4", "1/8", "1/16",
                                     "1/4.", "1/8.", "1/4T", "1/8T" };
const juce::StringArray kDstNames { "None", "Pitch", "PD Amount", "Pulse Width", "Cutoff",
                                    "Resonance", "Morph", "Drive", "Amplitude" };
}

PDHybridEditor::LabeledKnob& PDHybridEditor::addKnob (const juce::String& paramId,
                                                      const juce::String& text, int decimals)
{
    auto knob = std::make_unique<LabeledKnob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW, 14);
    knob->slider.setNumDecimalPlacesToDisplay (decimals);   // short display, still smooth
    addAndMakeVisible (knob->slider);

    knob->label.setText (text, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setFont (juce::Font (11.0f, juce::Font::bold));
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
    box->setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (*box);
    comboAttachments.push_back (
        std::make_unique<ComboBoxAttachment> (proc.apvts, paramId, *box));
    combos.push_back (std::move (box));
    return *combos.back();
}

PDHybridEditor::~PDHybridEditor()
{
    setLookAndFeel (nullptr);
}

PDHybridEditor::PDHybridEditor (PDHybridAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    // "Init" resets every parameter to its default value.
    addAndMakeVisible (initButton);
    initButton.onClick = [this]
    {
        for (auto* p : proc.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());
    };

    // --- Oscillator A (tuning + per-osc EQ) ---
    oscATypeBox = &addCombo ("oscAType", kOscTypeNames);
    oscAWaveBox = &addCombo ("oscAWave", kPdWaveNames);
    Section oscA;
    oscA.title  = "Osc A";
    oscA.combos = { oscATypeBox, oscAWaveBox };
    oscA.knobs  = { &addKnob ("oscAAmount", "PD Amt"),
                    &addKnob ("oscAPulseWidth", "Width"),
                    &addKnob ("oscAOctave", "Oct", 0),
                    &addKnob ("oscASemi", "Semi", 0),
                    &addKnob ("oscAFine", "Fine"),
                    &addKnob ("oscAEqLow", "EQ Lo"),
                    &addKnob ("oscAEqMid", "EQ Mid"),
                    &addKnob ("oscAEqHigh", "EQ Hi") };

    // --- Oscillator B ---
    oscBTypeBox = &addCombo ("oscBType", kOscTypeNames);
    oscBWaveBox = &addCombo ("oscBWave", kPdWaveNames);
    Section oscB;
    oscB.title  = "Osc B";
    oscB.combos = { oscBTypeBox, oscBWaveBox };
    oscB.knobs  = { &addKnob ("oscBAmount", "PD Amt"),
                    &addKnob ("oscBPulseWidth", "Width"),
                    &addKnob ("oscBOctave", "Oct", 0),
                    &addKnob ("oscBSemi", "Semi", 0),
                    &addKnob ("oscBFine", "Fine"),
                    &addKnob ("oscBEqLow", "EQ Lo"),
                    &addKnob ("oscBEqMid", "EQ Mid"),
                    &addKnob ("oscBEqHigh", "EQ Hi") };

    // --- Mixer (vertical strip) ---
    Section mixer;
    mixer.title = "Mixer";
    mixer.knobs = { &addKnob ("oscALevel", "Osc A"),
                    &addKnob ("oscBLevel", "Osc B"),
                    &addKnob ("noiseLevel", "Noise") };

    // --- Filter ---
    filterTypeBox = &addCombo ("filterType",
        { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" });
    Section filter;
    filter.title  = "Filter";
    filter.combos = { filterTypeBox };
    filter.knobs  = { &addKnob ("cutoff", "Cutoff"),
                      &addKnob ("resonance", "Reso"),
                      &addKnob ("filterMorph", "Morph"),
                      &addKnob ("keyTrack", "Key Trk"),
                      &addKnob ("filterEnvAmount", "Env Amt") };

    // --- Filter 2 (routing) ---
    auto& filterRoutingBox = addCombo ("filterRouting", { "Single", "Series", "Parallel" });
    auto& filter2TypeBox   = addCombo ("filter2Type",
        { "Ladder", "State Variable", "PD Resonator", "Comb", "Allpass" });
    Section filter2;
    filter2.title  = "Filter 2";
    filter2.combos = { &filterRoutingBox, &filter2TypeBox };
    filter2.knobs  = { &addKnob ("filter2Cutoff", "Cutoff"),
                       &addKnob ("filter2Res", "Reso"),
                       &addKnob ("filter2Morph", "Morph"),
                       &addKnob ("filter2EnvAmount", "Env Amt") };

    // --- Amp Envelope ---
    Section envelope;
    envelope.title = "Amp Env";
    envelope.knobs = { &addKnob ("attack", "Atk"),
                       &addKnob ("decay", "Dec"),
                       &addKnob ("sustain", "Sus"),
                       &addKnob ("release", "Rel") };

    // --- Filter Envelope ---
    Section filterEnv;
    filterEnv.title = "Filter Env";
    filterEnv.knobs = { &addKnob ("filterEnvA", "Atk"),
                        &addKnob ("filterEnvD", "Dec"),
                        &addKnob ("filterEnvS", "Sus"),
                        &addKnob ("filterEnvR", "Rel") };

    // --- Filter 2 Envelope ---
    Section filter2Env;
    filter2Env.title = "Filter 2 Env";
    filter2Env.knobs = { &addKnob ("filter2EnvA", "Atk"),
                         &addKnob ("filter2EnvD", "Dec"),
                         &addKnob ("filter2EnvS", "Sus"),
                         &addKnob ("filter2EnvR", "Rel") };

    // --- Mod Envelope ---
    Section modEnv;
    modEnv.title = "Mod Env";
    modEnv.knobs = { &addKnob ("modEnvA", "Atk"),
                     &addKnob ("modEnvD", "Dec"),
                     &addKnob ("modEnvS", "Sus"),
                     &addKnob ("modEnvR", "Rel") };

    // --- LFO ---
    lfoWaveBox = &addCombo ("lfoWave", kLfoWaveNames);
    auto& lfoSyncBox = addCombo ("lfoSync", kSyncNames);
    Section lfo;
    lfo.title  = "LFO";
    lfo.combos = { lfoWaveBox, &lfoSyncBox };
    lfo.knobs  = { &addKnob ("lfoRate", "Rate") };

    // --- LFO 2 ---
    auto& lfo2WaveBox = addCombo ("lfo2Wave", kLfoWaveNames);
    auto& lfo2SyncBox = addCombo ("lfo2Sync", kSyncNames);
    Section lfo2;
    lfo2.title  = "LFO 2";
    lfo2.combos = { &lfo2WaveBox, &lfo2SyncBox };
    lfo2.knobs  = { &addKnob ("lfo2Rate", "Rate") };

    // --- Overdrive ---
    Section drive;
    drive.title = "Overdrive";
    drive.knobs = { &addKnob ("drive", "Drive"),
                    &addKnob ("bias", "Bias"),
                    &addKnob ("gain", "Gain") };

    // --- Compressor ---
    Section comp;
    comp.title = "Compressor";
    comp.knobs = { &addKnob ("compThreshold", "Thr"),
                   &addKnob ("compRatio", "Ratio"),
                   &addKnob ("compAttack", "Atk"),
                   &addKnob ("compRelease", "Rel"),
                   &addKnob ("compMakeup", "Gain") };

    // --- Delay ---
    auto& delayModeBox = addCombo ("delayMode", { "Mono", "Stereo", "Ping-Pong" });
    Section delaySec;
    delaySec.title  = "Delay";
    delaySec.combos = { &delayModeBox };
    delaySec.knobs  = { &addKnob ("delayTimeL", "Time L"),
                        &addKnob ("delayTimeR", "Time R"),
                        &addKnob ("delayFeedback", "Fbk"),
                        &addKnob ("delayMix", "Mix"),
                        &addKnob ("delayDuck", "Duck") };

    // --- Glide ---
    auto& glideModeBox = addCombo ("glideMode", { "Off", "Always", "Legato" });
    Section glideSec;
    glideSec.title  = "Glide";
    glideSec.combos = { &glideModeBox };
    glideSec.knobs  = { &addKnob ("glideTime", "Time"),
                        &addKnob ("glideCurve", "Curve") };

    // --- Stereo / Drift ---
    Section stereo;
    stereo.title = "Stereo / Drift";
    stereo.knobs = { &addKnob ("pan", "Pan"),
                     &addKnob ("panSpread", "Spread"),
                     &addKnob ("drift", "Drift") };

    // --- Unison ---
    Section unison;
    unison.title = "Unison";
    unison.knobs = { &addKnob ("unisonVoices", "Voices", 0),
                     &addKnob ("unisonDetune", "Detune"),
                     &addKnob ("unisonWidth", "Width") };

    // --- CZ multi-stage envelope (8 rate + 8 level, laid out in aligned rows) ---
    Section multiEnvSec;
    multiEnvSec.title = "Multi-Stage Env (CZ)  ->  filter";
    for (int i = 1; i <= 8; ++i)
        multiEnvSec.knobs.push_back (&addKnob ("czRate" + juce::String (i), "R" + juce::String (i)));
    for (int i = 1; i <= 8; ++i)
        multiEnvSec.knobs.push_back (&addKnob ("czLevel" + juce::String (i), "L" + juce::String (i)));
    multiEnvSec.knobs.push_back (&addKnob ("czAmount", "Amt"));
    multiEnvSec.knobs.push_back (&addKnob ("czSustain", "Sus", 0));

    // Index order below is referenced by resized().
    sections = { oscA, oscB, mixer, filter, envelope, lfo, modEnv, filterEnv,   // 0..7
                 stereo, comp, delaySec, glideSec, lfo2, unison, filter2, drive, // 8..15
                 filter2Env, multiEnvSec };                                      // 16, 17

    // --- Modulation matrix (6 slots) ---
    for (int i = 0; i < kNumModRows; ++i)
    {
        const auto s = juce::String (i + 1);

        modSrcBox[i].addItemList (kSrcNames, 1);
        modDestBox[i].addItemList (kDstNames, 1);
        addAndMakeVisible (modSrcBox[i]);
        addAndMakeVisible (modDestBox[i]);

        modSrcAtt[i]  = std::make_unique<ComboBoxAttachment> (proc.apvts, "mod" + s + "Source", modSrcBox[i]);
        modDestAtt[i] = std::make_unique<ComboBoxAttachment> (proc.apvts, "mod" + s + "Dest",   modDestBox[i]);

        modDepthSlider[i].setSliderStyle (juce::Slider::LinearHorizontal);
        modDepthSlider[i].setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16);
        modDepthSlider[i].setNumDecimalPlacesToDisplay (2);
        addAndMakeVisible (modDepthSlider[i]);
        modDepthAtt[i] = std::make_unique<SliderAttachment> (proc.apvts, "mod" + s + "Depth", modDepthSlider[i]);
    }

    resized();   // computes bounds + window size
}

void PDHybridEditor::resized()
{
    // Height a section needs when laid out at the given pixel width.
    auto sectionHeight = [] (const Section& s, int widthPx)
    {
        const int cols = juce::jmax (1, widthPx / kCellW);
        const int rows = (static_cast<int> (s.knobs.size()) + cols - 1) / cols;
        int h = kHeaderH;
        if (! s.combos.empty()) h += kComboRowH;
        h += rows * kCellH + kPad;
        return h;
    };

    auto layoutSection = [] (Section& s, juce::Rectangle<int> bounds)
    {
        s.bounds = bounds;
        auto r = bounds;
        r.removeFromTop (kHeaderH);   // title painted separately

        if (! s.combos.empty())
        {
            auto comboRow = r.removeFromTop (kComboRowH).reduced (3, 2);
            const int cw = comboRow.getWidth() / static_cast<int> (s.combos.size());
            for (auto* c : s.combos)
                c->setBounds (comboRow.removeFromLeft (cw).reduced (2, 0));
        }

        auto content = r.reduced (kPad / 2, 0);
        const int cols = juce::jmax (1, bounds.getWidth() / kCellW);
        std::size_t i = 0;
        while (i < s.knobs.size())
        {
            auto row = content.removeFromTop (kCellH);
            for (int c = 0; c < cols && i < s.knobs.size(); ++c, ++i)
            {
                auto cell = row.removeFromLeft (kCellW);
                s.knobs[i]->label.setBounds  (cell.removeFromTop (kLabelH));
                s.knobs[i]->slider.setBounds (cell);
            }
        }
    };

    auto stackColumn = [&] (int x, int yTop, int widthPx, std::vector<int> idx) -> int
    {
        int y = yTop;
        for (int i : idx)
        {
            const int h = sectionHeight (sections[i], widthPx);
            layoutSection (sections[i], { x, y, widthPx, h });
            y += h + kPad;
        }
        return y;
    };

    const int w1 = 3 * kCellW, w2 = 4 * kCellW, w3 = 1 * kCellW, w4 = 4 * kCellW;
    int rightEdge = 0;

    // Band 1: Voice/Motion | Oscillators | Mix | Filters
    int x = kPad;
    const int y1 = kTitleH + kPad;
    int b1 = y1;
    b1 = juce::jmax (b1, stackColumn (x, y1, w1, { 11, 13, 8 }));  x += w1 + kPad;  // Glide, Unison, Stereo/Drift
    b1 = juce::jmax (b1, stackColumn (x, y1, w2, { 0, 1 }));       x += w2 + kPad;  // Osc A, Osc B
    b1 = juce::jmax (b1, stackColumn (x, y1, w3, { 2 }));          x += w3 + kPad;  // Mixer
    b1 = juce::jmax (b1, stackColumn (x, y1, w4, { 3, 14 }));      x += w4 + kPad;  // Filter, Filter 2
    rightEdge = juce::jmax (rightEdge, x);

    // Band 2: Envelopes | LFOs | Output/FX
    x = kPad;
    const int y2 = b1 + kPad;
    int b2 = y2;
    b2 = juce::jmax (b2, stackColumn (x, y2, w2, { 4, 7, 16, 6 })); x += w2 + kPad;  // Amp / Filter A / Filter B / Mod env
    b2 = juce::jmax (b2, stackColumn (x, y2, 2 * kCellW, { 5, 12 })); x += 2 * kCellW + kPad; // LFO, LFO 2
    b2 = juce::jmax (b2, stackColumn (x, y2, w4, { 15, 9, 10 }));  x += w4 + kPad;  // Overdrive, Compressor, Delay
    rightEdge = juce::jmax (rightEdge, x);

    // Band 3: CZ multi-stage envelope, 8 columns so rates/levels align in rows.
    const int b3 = stackColumn (kPad, b2 + kPad, 8 * kCellW, { 17 });
    rightEdge = juce::jmax (rightEdge, kPad + 8 * kCellW + kPad);

    // Band 4: Modulation matrix (2 columns x 3 rows).
    const int matW = rightEdge - kPad - kPad;
    matrixBounds = { kPad, b3 + kPad, matW, kHeaderH + 3 * kMatrixRowH + kPad };
    auto marea = matrixBounds;
    marea.removeFromTop (kHeaderH);
    for (int rowI = 0; rowI < 3; ++rowI)
    {
        auto row = marea.removeFromTop (kMatrixRowH).reduced (2, 2);
        const int half = row.getWidth() / 2;
        for (int colI = 0; colI < 2; ++colI)
        {
            const int idx = rowI * 2 + colI;
            auto cell = row.removeFromLeft (half).reduced (3, 0);
            modSrcBox[idx].setBounds  (cell.removeFromLeft (92));
            cell.removeFromLeft (4);
            modDestBox[idx].setBounds (cell.removeFromLeft (92));
            cell.removeFromLeft (4);
            modDepthSlider[idx].setBounds (cell.removeFromLeft (juce::jmin (cell.getWidth(), 140)));
        }
    }

    setSize (rightEdge + kPad, matrixBounds.getBottom() + kPad);

    initButton.setBounds (rightEdge - 68, (kTitleH - 22) / 2, 64, 22);
}

void PDHybridEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff20242b));

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawText ("PD Hybrid Synth", getLocalBounds().removeFromTop (kTitleH).reduced (kPad, 0),
                juce::Justification::centredLeft);

    g.setFont (juce::Font (12.0f, juce::Font::bold));
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

    auto mh = matrixBounds.withHeight (kHeaderH);
    g.setColour (juce::Colour (0xff2c3440));
    g.fillRect (mh);
    g.setColour (juce::Colour (0xff8fb7ff));
    g.drawText (" Modulation Matrix   (Source -> Destination x Depth)", mh,
                juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff2c3440));
    g.drawRect (matrixBounds, 1);
}
