#pragma once

#include <vector>
#include <algorithm>

namespace harness {

/**
    Renders `totalSamples` by calling a block processor in chunks of `blockSize`.
    Used to prove block-size invariance: rendering the same source in 64- vs
    512-sample blocks must produce bit-identical output. Any state mishandled
    across a block boundary breaks this immediately.

    `processBlock` signature: void(float* out, int numSamples).
    Reset the engine before calling.
*/
template <typename Fn>
std::vector<float> renderInBlocks (Fn&& processBlock, int totalSamples, int blockSize)
{
    std::vector<float> out (static_cast<std::size_t> (totalSamples), 0.0f);
    int i = 0;
    while (i < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - i);
        processBlock (out.data() + i, n);
        i += n;
    }
    return out;
}

} // namespace harness
