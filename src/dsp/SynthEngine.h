#pragma once

#include "Voice.h"
#include "SynthParams.h"
#include <array>
#include <cstdint>
#include <vector>

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
    void setParams     (const SynthParams& params) noexcept
    {
        // Switching voice mode live would otherwise strand held voices (e.g. a
        // Poly chord when dropping into Mono). Clear the keyboard first.
        if (params.voiceMode != params_.voiceMode)
            allNotesOff();
        params_ = params;
    }

    void noteOn  (int note, float velocity, int noteId = 0);
    void noteOff (int note, int noteId = 0);
    void allNotesOff();

    // Sustain pedal (MIDI CC64): while down, released keys keep sounding.
    void setSustain (bool down) noexcept;

    void setNotePitchBend (int noteId, double semitones) noexcept;
    void setNotePressure  (int noteId, double pressure01) noexcept;
    void setNoteTimbre    (int noteId, double timbre01) noexcept;
    void setModWheel      (double modWheel01) noexcept { modWheel_ = modWheel01; }

    // Renders `numSamples` of summed stereo output (overwrites `left`/`right`).
    void renderBlock (float* left, float* right, int numSamples);

    int activeVoiceCount() const noexcept;

private:
    struct HeldNote { int note; float velocity; int noteId; };

    double noteHz (int note) const noexcept;   // note -> Hz with master tune + transpose
    int  allocateVoice (int limit) noexcept;
    void polyNoteOn  (int note, float velocity, int noteId);
    void polyNoteOff (int note, int noteId);
    void monoNoteOn  (int note, float velocity, int noteId);
    void monoNoteOff (int note, int noteId);
    void updateMono  ();                 // recompute the sounding mono note
    const HeldNote* selectHeld() const noexcept;   // priority-selected held note
    void removeHeld  (int note, int noteId) noexcept;
    void startVoice  (int v, int note, float velocity, int noteId,
                      double spread, double fromHz, double glideSamples) noexcept;

    std::array<Voice, kMaxVoices>    voices_;
    std::array<int, kMaxVoices>      voiceNote_   { };
    std::array<int, kMaxVoices>      voiceId_     { };
    std::array<std::uint64_t, kMaxVoices> voiceAge_ { };
    std::array<bool, kMaxVoices>     voiceHeld_   { };
    std::array<bool, kMaxVoices>     voiceSustained_ { };   // held by sustain pedal only
    std::array<double, kMaxVoices>   voiceBend_   { };
    std::array<double, kMaxVoices>   voicePressure_ { };
    std::array<double, kMaxVoices>   voiceTimbre_ { };

    // Monophonic modes: held-key stack + the voices carrying the current note.
    std::vector<HeldNote>            heldStack_;
    std::array<int, 6>               monoVoices_ { };
    int                              monoVoiceN_ = 0;
    int                              monoNote_   = -1;

    bool          sustainDown_ = false;
    SynthParams   params_;
    std::uint64_t ageCounter_ = 0;
    double        sampleRate_ = 44100.0;
    double        modWheel_   = 0.0;
    double        lastNoteHz_ = 0.0;   // previous note's pitch, for glide
};

} // namespace pdhybrid
