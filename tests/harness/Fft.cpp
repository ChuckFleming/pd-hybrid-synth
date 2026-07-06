#include "Fft.h"
#include <cmath>
#include <utility>

namespace harness {

static constexpr double kPi = 3.14159265358979323846;

bool isPowerOfTwo (std::size_t n) noexcept
{
    return n != 0 && (n & (n - 1)) == 0;
}

std::size_t nextPowerOfTwo (std::size_t n) noexcept
{
    std::size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

void fft (std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    if (n <= 1)
        return;

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / static_cast<double> (len);
        const std::complex<double> wlen (std::cos (ang), std::sin (ang));

        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace harness
