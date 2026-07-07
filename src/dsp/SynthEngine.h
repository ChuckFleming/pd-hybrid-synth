#pragma once

#include "Voice.h"
#include "SynthParams.h"
#include <array>
#include <cstdint>

namespace pdhybrid {

/**
    Polyphonic voice manager. Allocates voices, steals when full (prefers
    released voices, otherwise the oldest), and routes note events plus
    expression to the correct voice.

    Expression (pitch bend / pressure / timbre) is keyed by an integer `noteId`.
    The plugin uses the MIDI channel as the noteId, which makes MPE per-note
    expression and legacy channel-wide bend the same mechanism.

    Headless and JUCE-free; the MIDI/voicing harness drives it directly.
*/
class SynthEngine
{
public:
    static constexpr int kMaxVoices = 16;

    void setSampleRate (double sampleRate);
    void setParams     (const SynthParams& params) noexcept { params_ = params; }

    void noteOn  (int note, float velocity, int noteId = 0);
    void noteOff (int note, int noteId = 0);
    void allNotesOff();

    void setNotePitchBend (int noteId, double semitones) noexcept;
    void setNotePressure  (int noteId, double pressure01) noexcept;
    void setNoteTimbre    (int noteId, double timbre01) noexcept;
    void setModWheel      (double modWheel01) noexcept { modWheel_ = modWheel01; }

    // Renders `numSamples` of summed stereo output (overwrites `left`/`right`).
    void renderBlock (float* left, float* right, int numSamples);

    int activeVoiceCount() const noexcept;

private:
    int allocateVoice() noexcept;

    std::array<Voice, kMaxVoices>    voices_;
    std::array<int, kMaxVoices>      voiceNote_   { };
    std::array<int, kMaxVoices>      voiceId_     { };
    std::array<std::uint64_t, kMaxVoices> voiceAge_ { };
    std::array<bool, kMaxVoices>     voiceHeld_   { };
    std::array<double, kMaxVoices>   voiceBend_   { };
    std::array<double, kMaxVoices>   voicePressure_ { };
    std::array<double, kMaxVoices>   voiceTimbre_ { };

    SynthParams   params_;
    std::uint64_t ageCounter_ = 0;
    double        sampleRate_ = 44100.0;
    double        modWheel_   = 0.0;
    double        lastNoteHz_ = 0.0;   // previous note's pitch, for glide
};

} // namespace pdhybrid
