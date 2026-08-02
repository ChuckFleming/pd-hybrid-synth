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

    int latchedQuality() const noexcept { return quality_; }
    int heldRoot()       const noexcept { return heldRoot_; }

private:
    /** Fills `out` with the voiced chord for `root`. Returns the note count. */
    int buildVoicing (int root, int* out) const noexcept;
    /** Diffs `newNotes` against what is sounding and emits only the changes. */
    int emitChange (const int* newNotes, int newCount, int root,
                    Event* out, int maxOut) noexcept;

    bool   enabled_ = false;
    int    split_   = 60;
    int    quality_ = 0;
    int    voicing_ = VoiceLed;
    double spread_  = 0.4;
    int    octave_  = 0;
    bool   dirty_   = false;

    int    heldRoot_ = -1;
    float  heldVel_  = 1.0f;
    int    curRoot_  = -1;                    // note currently sounding on the bass
    int    cur_[kMaxChordNotes] = { 0 };      // chord notes currently sounding
    int    curCount_ = 0;
};

} // namespace pdhybrid
