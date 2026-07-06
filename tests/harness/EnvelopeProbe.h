#pragma once

#include <vector>

namespace harness {

// Renders `numSamples` from any generator exposing `float processSample()`
// (envelope, LFO, ...). The basis of timing/level assertions.
template <typename Gen>
std::vector<float> render (Gen& gen, int numSamples)
{
    std::vector<float> out (static_cast<std::size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<std::size_t> (i)] = gen.processSample();
    return out;
}

// Sample index corresponding to a time in seconds.
inline int sampleAt (double seconds, double sampleRate)
{
    return static_cast<int> (seconds * sampleRate);
}

} // namespace harness
