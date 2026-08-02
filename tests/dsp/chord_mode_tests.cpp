#include <catch2/catch_test_macros.hpp>

#include "dsp/ChordMode.h"

#include <algorithm>
#include <vector>

using pdhybrid::ChordMode;

namespace {

std::vector<int> intervalsOf (int quality)
{
    int buf[ChordMode::kMaxChordNotes] = { 0 };
    const int n = ChordMode::qualityIntervals (quality, buf);
    return std::vector<int> (buf, buf + n);
}

} // namespace

TEST_CASE ("Quality table matches the spec", "[chord]")
{
    REQUIRE (intervalsOf (0)  == std::vector<int> { 0, 4, 7 });        // maj
    REQUIRE (intervalsOf (1)  == std::vector<int> { 0, 3, 7 });        // min
    REQUIRE (intervalsOf (2)  == std::vector<int> { 0, 4, 7, 10 });    // 7
    REQUIRE (intervalsOf (3)  == std::vector<int> { 0, 3, 7, 10 });    // m7
    REQUIRE (intervalsOf (4)  == std::vector<int> { 0, 4, 7, 11 });    // maj7
    REQUIRE (intervalsOf (5)  == std::vector<int> { 0, 4, 7, 9 });     // 6
    REQUIRE (intervalsOf (6)  == std::vector<int> { 0, 3, 6, 10 });    // m7b5
    REQUIRE (intervalsOf (7)  == std::vector<int> { 0, 3, 6, 9 });     // dim7
    REQUIRE (intervalsOf (8)  == std::vector<int> { 0, 4, 8 });        // aug
    REQUIRE (intervalsOf (9)  == std::vector<int> { 0, 2, 7 });        // sus2
    REQUIRE (intervalsOf (10) == std::vector<int> { 0, 5, 7 });        // sus4
    REQUIRE (intervalsOf (11) == std::vector<int> { 0, 3, 7, 9 });     // m6
}

TEST_CASE ("Quality index is clamped, never out of bounds", "[chord]")
{
    int buf[ChordMode::kMaxChordNotes] = { 0 };
    REQUIRE (ChordMode::qualityIntervals (-5, buf) > 0);
    REQUIRE (ChordMode::qualityIntervals (99, buf) > 0);
}
