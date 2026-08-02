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
};

} // namespace pdhybrid
