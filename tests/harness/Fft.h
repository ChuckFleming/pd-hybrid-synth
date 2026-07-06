#pragma once

#include <complex>
#include <vector>
#include <cstddef>

namespace harness {

bool   isPowerOfTwo   (std::size_t n) noexcept;
std::size_t nextPowerOfTwo (std::size_t n) noexcept;

// In-place iterative radix-2 Cooley-Tukey FFT. `data.size()` must be a power of two.
void fft (std::vector<std::complex<double>>& data);

} // namespace harness
