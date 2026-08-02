#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SynthEngine.h"
#include "dsp/Compressor.h"
#include "dsp/Delay.h"
#include "dsp/Chorus.h"
#include "dsp/Reverb.h"
#include "dsp/Arpeggiator.h"
#include "dsp/ChordMode.h"
#include "dsp/GlobalEq.h"
#include "dsp/MonoBass.h"
#include "dsp/MasterStage.h"
#include "PresetManager.h"
#include <vector>

/**
    Polyphonic hybrid synth: drives the headless SynthEngine (PD osc -> ladder
    filter -> overdrive -> multi-stage envelope, per voice). MIDI is rendered
    sample-accurately by splitting each block at event boundaries. Per-note
    expression (pitch bend / pressure / CC74 timbre) is keyed by MIDI channel,
    which covers both MPE and legacy controllers. The editor is a generic,
    auto-generated parameter panel for now.
*/
class PDHybridAudioProcessor : public juce::AudioProcessor
{
public:
    PDHybridAudioProcessor();
    ~PDHybridAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PD Hybrid Synth"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override
    { return reverbOn_ ? 8.0 : pdhybrid::Delay::kMaxDelaySeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    PresetManager& getPresetManager() noexcept { return presets; }

    // All-notes-off "panic", triggered from the editor (thread-safe).
    void triggerPanic() noexcept { panic_.store (true); }

    // Lock-free scope tap: the audio thread pushes the (mono) output into a small
    // ring; the editor copies the most recent `num` samples for its display.
    static constexpr int kScopeSize = 2048;   // power of two
    void readScope (float* dest, int num) const noexcept;

    // Live modulation-source levels for the editor's meters. Written once per
    // block on the audio thread, read on the message thread; relaxed atomics
    // because a metering display neither needs ordering nor exact coherence.
    static constexpr int kNumModSources = static_cast<int> (pdhybrid::ModSource::Count);
    void readModLevels (float* dest, int num) const noexcept;

    /** Root currently held in chord mode, or -1. For the editor's display. */
    int chordHeldRoot() const noexcept { return chordRoot_.load (std::memory_order_relaxed); }
    /** Copies the sounding chord out for the display. Returns the count. */
    int chordVoicedNotes (int* out, int maxOut) const noexcept;

    /** Tempo currently driving sync (host BPM, or the internal one). */
    double currentBpm() const noexcept { return currentBpm_.load (std::memory_order_relaxed); }

    /** Current compressor gain reduction in dB (<= 0), for the Out page meter. */
    float gainReductionDb() const noexcept { return gainReduction_.load (std::memory_order_relaxed); }

    /** Loads a wavetable from a WAV/AIFF file. Message thread only. Returns
        false (leaving the current table in place) if the file cannot be read. */
    bool loadWavetable (const juce::File& file);
    /** Path of the loaded wavetable, or empty for the built-in default set. */
    juce::String wavetableName() const { return wavetableName_; }
    /** The analysed table, for the editor's cycle preview. */
    std::shared_ptr<pdhybrid::WavetableOscillator::WavetableSet> wavetableSet() const
    { return wavetable_; }

private:
    void publishModLevels() noexcept;
    std::atomic<float> modLevels_[kNumModSources] {};
    std::atomic<float> gainReduction_ { 0.0f };
    std::atomic<double> currentBpm_ { 120.0 };

    void pushScope (const float* left, const float* right, int n) noexcept;
    float scopeBuf_[kScopeSize] = { 0.0f };
    std::atomic<int> scopeWrite_ { 0 };

    std::atomic<bool> panic_ { false };
    PresetManager presets { apvts };   // constructed after apvts (declaration order)

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void pushParams();
    // `toPoly` / `toBass` let the arpeggiator pass held notes straight to
    // whichever layer it is not driving.
    void handleMidiMessage (const juce::MidiMessage& msg,
                            bool toPoly = true, bool toBass = true);
    /** Routes ChordMode output: isRoot events to the bass, the rest to poly. */
    void dispatchChordEvents (const pdhybrid::ChordMode::Event* ev, int n, int channel,
                              bool toPoly, bool toBass);
    void publishChordState() noexcept;
    /** Writes a key-driven quality latch back into the chordQuality parameter. */
    void syncChordQualityParam();
    void renderSegment (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    void applyGlobalModulation (juce::AudioBuffer<float>& buffer, int numSamples);

    pdhybrid::SynthEngine engine;
    pdhybrid::Compressor  compressor;           // global output compressor
    pdhybrid::Chorus      chorus;               // global chorus / ensemble
    pdhybrid::Delay       delay;                // global ducking delay
    pdhybrid::Reverb      reverb;               // global reverb
    pdhybrid::GlobalEq    globalEq;             // final master EQ
    pdhybrid::MonoBass    monoBass;             // monophonic sub-bass layer
    pdhybrid::MasterStage master;               // output level + soft limiter
    std::vector<float>    scratchL, scratchR;   // stereo render buffers
    std::vector<float>    scratchBass;          // mono bass render buffer
    double                pitchBendRangeSemis = 2.0;

    // Global modulation pass (processor-level sources -> global FX destinations).
    pdhybrid::Lfo         globalLfo;
    pdhybrid::ModMatrix   globalMatrix;
    double                macro1_ = 0.0, macro2_ = 0.0, modWheel_ = 0.0;
    int                   velCurve_ = 0;   // 0=Linear,1=Soft,2=Hard,3=Fixed
    double                delayMixBase_ = 0.0, delayFbBase_ = 0.30;
    double                chorusDepthBase_ = 0.5, reverbMixBase_ = 0.3;
    double                eqHighFreqBase_ = 8000.0, eqHighGainBase_ = 0.0;
    bool                  compOn_ = true, delayOn_ = true, globalEqOn_ = true;
    bool                  chorusOn_ = false, reverbOn_ = false;
    int                   fxRouting_ = 0;   // 0=Delay->Reverb, 1=Reverb->Delay, 2=Reverb,DryDelay
    std::vector<float>    fxScratchL_, fxScratchR_;   // parallel-routing scratch

    // FX-send routing. When any voice sends less than fully, the chorus/delay/
    // reverb group is fed from a send bus instead of being a plain insert:
    //   out = (dry - send) + chain(send)
    // With every voice at send 1 the send bus equals the dry bus, the residue
    // is zero and the result is exactly the old insert chain -- which is what
    // keeps existing presets sounding identical.
    std::vector<float>    sendL_, sendR_;      // per-block accumulated send bus
    std::vector<float>    compGain_;           // compressor gain, applied to both buses
    bool                  fxSendActive_ = false;
    // Wavetable, swapped on the message thread. `retiredWavetables_` holds every
    // superseded set for the life of the instance: voices copy the shared_ptr
    // into their params each block, so without this the last reference could be
    // dropped -- and the memory freed -- on the audio thread.
    std::shared_ptr<pdhybrid::WavetableOscillator::WavetableSet> wavetable_;
    std::vector<std::shared_ptr<pdhybrid::WavetableOscillator::WavetableSet>> retiredWavetables_;
    juce::String          wavetableName_;
    juce::String          wavetablePath_;

    pdhybrid::ChordMode   chord_;
    bool                  chordOn_ = false, chordWasOn_ = false;
    // Members, not function-local statics: a static would be shared across
    // plugin instances and two instances would flush each other's notes.
    int                   chordSplitCached_ = 60, chordLastSplit_ = 60;
    int                   chordQualitySeen_ = 0;   // last value pushed from the parameter
    // Live chord state, written on the audio thread and read by the editor.
    std::atomic<int>      chordRoot_ { -1 };
    std::atomic<int>      chordNoteCount_ { 0 };
    std::atomic<int>      chordNotes_[pdhybrid::ChordMode::kMaxChordNotes] { };

    pdhybrid::Arpeggiator arp_;
    bool                  arpOn_ = false, arpWasOn_ = false;
    int                   arpTarget_ = 0, arpWasTarget_ = 0;   // 0=Both 1=Poly 2=Bass

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PDHybridAudioProcessor)
};
