#pragma once

namespace pdhybrid {

/**
    Chord-organ / Orchid-style chord mode.

    The keyboard splits into a one-octave "quality zone" whose twelve semitones
    latch a chord type, and a "root zone" above it whose keys set the root.
    Pressing a root emits a voiced chord; changing the latched quality re-voices
    whatever is sounding.

    Pure C++, no JUCE, fixed-capacity storage -> no audio-thread allocation.
    Shaped like Arpeggiator: it consumes note events and emits note events, and
    sits ahead of the arpeggiator and mono-bass tap-off in the processor.
*/
class ChordMode
{
public:
    static constexpr int kNumQualities    = 12;
    static constexpr int kMaxChordNotes   = 4;
    static constexpr int kQualityZoneSize = 12;   // exactly one octave
    static constexpr int kMaxEvents       = 16;   // 4 offs + 4 ons + root pair, with room

    /** `isRoot` events are for the mono-bass layer only; every other event is a
        chord note for the poly voices. Keeping them in one stream lets the
        processor route both from a single loop. */
    struct Event { bool noteOn; int note; float velocity; bool isRoot; };

    enum Voicing { VoiceLed = 0, RootPosition, Closed, Drop2, Shell };

    /** Semitone offsets of `quality` (0..11). Returns the note count. */
    static int qualityIntervals (int quality, int* out) noexcept;

    void setEnabled   (bool on) noexcept       { enabled_ = on; }
    void setSplitNote (int midiNote) noexcept;
    void setQuality   (int index) noexcept;
    void setVoicing   (int mode) noexcept      { if (mode != voicing_) { voicing_ = mode; dirty_ = true; } }
    void setSpread    (double spread01) noexcept;
    void setOctave    (int octaves) noexcept;

    int handleNoteOn  (int note, float vel, Event* out, int maxOut) noexcept;
    int handleNoteOff (int note, Event* out, int maxOut) noexcept;

    /** Emits any re-voice owed after a parameter change (quality, voicing,
        spread, octave). Call once per block; returns 0 when nothing is due. */
    int refresh (Event* out, int maxOut) noexcept;

    /** Releases everything sounding. Used when the split moves or chord mode is
        switched off, either of which would otherwise strand a held note. Also
        clears the voicing history, so the next chord starts in root position. */
    int flush (Event* out, int maxOut) noexcept;

    int latchedQuality() const noexcept { return quality_; }
    int heldRoot()       const noexcept { return heldRoot_; }

    /** True once after a quality *key* moved the latch, so the caller can write
        the new value back to its parameter. Without that write-back the next
        parameter push would overwrite the latch and undo the key press. Changes
        arriving through setQuality (the parameter itself) do not raise it. */
    bool consumeQualityChanged() noexcept
    { const bool c = qualityFromKey_; qualityFromKey_ = false; return c; }

    /** Copies the currently sounding chord notes out. Returns the count. */
    int voicedNotes (int* out, int maxOut) const noexcept;

private:
    /** Fills `out` with the voiced chord for `root`. Returns the note count.
        Not const: it carries the voicing centre forward for the next chord. */
    int buildVoicing (int root, int* out) noexcept;
    /** Diffs `newNotes` against what is sounding and emits only the changes. */
    int emitChange (const int* newNotes, int newCount, int root,
                    Event* out, int maxOut) noexcept;
    /** Rebuilds and re-emits the held chord. Shared by quality keys and refresh. */
    int revoice (Event* out, int maxOut) noexcept;

    bool   enabled_ = false;
    int    split_   = 60;
    int    quality_ = 0;
    int    voicing_ = VoiceLed;
    double spread_  = 0.4;
    int    octave_  = 0;
    bool   dirty_   = false;
    bool   qualityFromKey_ = false;   // latch moved by a key, not the parameter

    int    heldRoot_ = -1;
    float  heldVel_  = 1.0f;
    int    curRoot_  = -1;                    // note currently sounding on the bass
    int    cur_[kMaxChordNotes] = { 0 };      // chord notes currently sounding
    int    curCount_ = 0;

    // Voicing centre carried forward. This deliberately outlives the sounding
    // notes: releasing a chord and playing the next one is still a progression,
    // and it should lead from where the last one sat rather than jumping back
    // to root position every time a key is lifted.
    double lastCentre_ = 0.0;
    bool   hasHistory_ = false;
};

} // namespace pdhybrid
