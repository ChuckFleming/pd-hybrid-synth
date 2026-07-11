#include <catch2/catch_test_macros.hpp>

#include "dsp/Arpeggiator.h"

#include <vector>

using pdhybrid::Arpeggiator;

namespace {

// Collect all note-on events over `blocks` blocks of `blockLen` samples.
std::vector<int> collectOnsets (Arpeggiator& a, int blocks, int blockLen)
{
    std::vector<int> notes;
    Arpeggiator::Event ev[128];
    for (int b = 0; b < blocks; ++b)
    {
        const int n = a.generate (blockLen, ev, 128);
        for (int i = 0; i < n; ++i)
            if (ev[i].noteOn)
                notes.push_back (ev[i].note);
    }
    return notes;
}

} // namespace

TEST_CASE ("Arpeggiator steps through held notes in order", "[arp]")
{
    Arpeggiator a;
    a.reset();
    a.setStepSamples (1000.0);
    a.setMode (Arpeggiator::Up);
    a.setOctaves (1);
    a.setGate (0.5);

    a.noteOn (60, 1.0f);
    a.noteOn (64, 1.0f);
    a.noteOn (67, 1.0f);

    // 6 steps at 1000 samples each -> render 6000 samples.
    const auto notes = collectOnsets (a, 6, 1000);
    REQUIRE (notes.size() >= 6);
    REQUIRE (notes[0] == 60);
    REQUIRE (notes[1] == 64);
    REQUIRE (notes[2] == 67);
    REQUIRE (notes[3] == 60);   // wraps
}

TEST_CASE ("Arpeggiator octave range extends the pattern", "[arp]")
{
    Arpeggiator a;
    a.reset();
    a.setStepSamples (500.0);
    a.setMode (Arpeggiator::Up);
    a.setOctaves (2);

    a.noteOn (60, 1.0f);
    const auto notes = collectOnsets (a, 4, 500);
    REQUIRE (notes.size() >= 2);
    REQUIRE (notes[0] == 60);
    REQUIRE (notes[1] == 72);   // +1 octave
}

TEST_CASE ("Arpeggiator note-off follows each note-on", "[arp]")
{
    Arpeggiator a;
    a.reset();
    a.setStepSamples (1000.0);
    a.setGate (0.5);
    a.noteOn (60, 1.0f);

    Arpeggiator::Event ev[128];
    const int n = a.generate (4000, ev, 128);
    int ons = 0, offs = 0;
    for (int i = 0; i < n; ++i) (ev[i].noteOn ? ons : offs)++;
    REQUIRE (ons >= 3);
    REQUIRE (offs >= 3);   // every note is released
}

TEST_CASE ("Arpeggiator is silent with no held notes", "[arp]")
{
    Arpeggiator a;
    a.reset();
    a.setStepSamples (500.0);
    const auto notes = collectOnsets (a, 4, 500);
    REQUIRE (notes.empty());
}
