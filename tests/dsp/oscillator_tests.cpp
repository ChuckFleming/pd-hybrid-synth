#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/PhaseDistortionOscillator.h"
#include "harness/Spectrum.h"
#include "harness/SignalStats.h"
#include "harness/Invariance.h"

#include <cmath>
#include <vector>

using pdhybrid::PhaseDistortionOscillator;
using pdhybrid::PdWave;
using Catch::Approx;
using namespace harness;

namespace {

std::vector<float> renderOsc (double freqHz, double amount, double sampleRate,
                              int numSamples, int blockSize,
                              PdWave wave = PdWave::Sawtooth)
{
    PhaseDistortionOscillator osc;
    osc.setSampleRate (sampleRate);
    osc.setFrequency (freqHz);
    osc.setAmount (amount);
    osc.setWave (wave);
    osc.reset();
    return renderInBlocks ([&] (float* out, int n) { osc.processBlock (out, n); },
                           numSamples, blockSize);
}

// Magnitude-weighted mean frequency (spectral centroid) -- a robust "brightness"
// measure that, for the resonant waves, tracks the moving resonant peak.
double spectralCentroid (const Spectrum& s)
{
    double num = 0.0, den = 0.0;
    for (std::size_t b = 1; b < s.magnitude.size(); ++b)   // skip DC
    {
        num += s.frequencyOfBin (b) * s.magnitude[b];
        den += s.magnitude[b];
    }
    return den > 0.0 ? num / den : 0.0;
}

const PdWave kAllWaves[] = {
    PdWave::Sawtooth, PdWave::Square, PdWave::Pulse, PdWave::DoubleSine,
    PdWave::SawPulse, PdWave::ResonantI, PdWave::ResonantII, PdWave::ResonantIII
};

} // namespace

TEST_CASE ("PD oscillator at amount 0 is a near-pure sine", "[oscillator][pd]")
{
    const double sr = 48000.0, freq = 440.0;
    const int    n  = 16384;

    auto buf = renderOsc (freq, 0.0, sr, n, 512);
    REQUIRE_FALSE (hasBadValues (buf));

    auto spec = computeSpectrum (buf, sr);
    REQUIRE (totalHarmonicDistortion (spec, freq) < 0.02); // < 2% THD
}

TEST_CASE ("PD oscillator fundamental tracks the set frequency", "[oscillator][pd]")
{
    const double sr = 48000.0;
    const int    n  = 16384;

    for (double freq : { 110.0, 220.0, 440.0, 880.0 })
    {
        auto spec     = computeSpectrum (renderOsc (freq, 0.3, sr, n, 512), sr);
        auto measured = spec.peakFrequency();
        REQUIRE (std::abs (measured - freq) < 1.0); // within 1 Hz
    }
}

TEST_CASE ("PD distortion amount increases harmonic content", "[oscillator][pd]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;

    auto thdAt = [&] (double amount)
    {
        return totalHarmonicDistortion (computeSpectrum (renderOsc (freq, amount, sr, n, 512), sr),
                                        freq);
    };

    const double t0 = thdAt (0.0);
    const double t1 = thdAt (0.40);
    const double t2 = thdAt (0.85);

    REQUIRE (t0 < t1);
    REQUIRE (t1 < t2);
}

TEST_CASE ("PD oscillator output stays bounded at full distortion", "[oscillator][pd]")
{
    auto buf = renderOsc (440.0, 1.0, 48000.0, 8192, 128);
    REQUIRE_FALSE (hasBadValues (buf));
    // The core sine is bounded by 1, but the anti-aliasing decimation FIR adds a
    // few percent of intersample ringing on the steep phase-distortion edges.
    REQUIRE (peakAbs (buf) <= 1.05f);
}

TEST_CASE ("Every PD waveform stays finite and bounded", "[oscillator][pd][waves]")
{
    for (auto wave : kAllWaves)
        for (double amount : { 0.0, 0.3, 0.7, 1.0 })
        {
            auto buf = renderOsc (220.0, amount, 48000.0, 8192, 256, wave);
            REQUIRE_FALSE (hasBadValues (buf));
            REQUIRE (peakAbs (buf) <= 1.05f);
        }
}

TEST_CASE ("Phase-distortion waves add harmonics as amount rises", "[oscillator][pd][waves]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;

    // The five DCW (phase-remap) waves should grow their harmonic content with
    // the distortion amount; the resonant waves are checked separately.
    for (auto wave : { PdWave::Sawtooth, PdWave::Square, PdWave::Pulse,
                       PdWave::DoubleSine, PdWave::SawPulse })
    {
        auto thdAt = [&] (double amount)
        {
            return totalHarmonicDistortion (
                computeSpectrum (renderOsc (freq, amount, sr, n, 512, wave), sr), freq);
        };

        REQUIRE (thdAt (0.2) < thdAt (0.85));
    }
}

TEST_CASE ("Resonant waves sweep their resonant peak upward with amount", "[oscillator][pd][waves]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;

    for (auto wave : { PdWave::ResonantI, PdWave::ResonantII, PdWave::ResonantIII })
    {
        const double lo = spectralCentroid (computeSpectrum (renderOsc (freq, 0.15, sr, n, 512, wave), sr));
        const double hi = spectralCentroid (computeSpectrum (renderOsc (freq, 0.85, sr, n, 512, wave), sr));
        REQUIRE (hi > lo * 1.5);   // resonant peak clearly climbs
    }
}

TEST_CASE ("Non-resonant PD waves track the set pitch", "[oscillator][pd][waves]")
{
    const double sr = 48000.0;
    const int    n  = 16384;

    for (auto wave : { PdWave::Sawtooth, PdWave::Square, PdWave::Pulse, PdWave::SawPulse })
        for (double freq : { 110.0, 220.0, 440.0 })
        {
            auto spec = computeSpectrum (renderOsc (freq, 0.5, sr, n, 512, wave), sr);
            REQUIRE (std::abs (spec.peakFrequency() - freq) < 1.0);
        }
}

TEST_CASE ("Oversampling keeps the aliasing floor low on a bright wave", "[oscillator][pd][aliasing]")
{
    // Exact-bin fundamental so that harmonics land on bins and any aliases fold
    // off-grid (same technique as the amp/analog-oscillator aliasing tests).
    const double sr = 48000.0;
    const int    n  = 16384;
    const int    k0 = 683;                     // fundamental bin (~2000 Hz)
    const double f0 = k0 * sr / n;

    auto buf  = renderOsc (f0, 0.9, sr, n, 512, PdWave::SawPulse);
    auto spec = computeSpectrum (buf, sr, /*hann=*/false);

    // Energy that is NOT on a harmonic bin of the fundamental is aliasing.
    double harmonic = 0.0, alias = 0.0;
    for (std::size_t b = 1; b < spec.magnitude.size(); ++b)
    {
        const double e = spec.magnitude[b] * spec.magnitude[b];
        if (b % static_cast<std::size_t> (k0) == 0) harmonic += e;
        else                                        alias    += e;
    }
    REQUIRE (alias / (harmonic + alias) < 0.05);
}

TEST_CASE ("PD wave combine alternates waveforms per cycle", "[oscillator][combine]")
{
    const double sr = 48000.0, freq = 220.0;
    const int    n  = 16384;

    // Plain square vs square combined with a sawtooth every other cycle.
    PhaseDistortionOscillator plain;
    plain.setSampleRate (sr); plain.setFrequency (freq); plain.setAmount (0.6);
    plain.setWave (PdWave::Square); plain.reset();
    std::vector<float> a (n);
    plain.processBlock (a.data(), n);

    PhaseDistortionOscillator comb;
    comb.setSampleRate (sr); comb.setFrequency (freq); comb.setAmount (0.6);
    comb.setWave (PdWave::Square); comb.setWaveB (PdWave::Sawtooth);
    comb.setCombine (true); comb.reset();
    std::vector<float> b (n);
    comb.processBlock (b.data(), n);

    REQUIRE_FALSE (hasBadValues (b));

    double diff = 0.0;
    for (int i = 0; i < n; ++i) diff += std::abs (a[i] - b[i]);
    REQUIRE (diff > 1.0);                     // combine changes the tone

    // Alternating cycles halve the effective period -> energy appears at f/2.
    auto spec = computeSpectrum (b, sr);
    REQUIRE (spec.magnitudeNearHz (freq * 0.5) > spec.magnitudeNearHz (freq) * 0.1);
}

TEST_CASE ("Phase-mod input is a no-op at zero offset", "[oscillator][crossmod]")
{
    const double sr = 48000.0;
    auto a = renderOsc (330.0, 0.6, sr, 4096, 128, PdWave::Sawtooth);

    PhaseDistortionOscillator osc;
    osc.setSampleRate (sr); osc.setFrequency (330.0); osc.setAmount (0.6);
    osc.setWave (PdWave::Sawtooth); osc.reset();
    std::vector<float> b (4096);
    for (int i = 0; i < 4096; ++i) { osc.setPhaseMod (0.0); b[i] = osc.processSample(); }

    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]);          // bit-identical when PM offset is 0
}

TEST_CASE ("Hard sync locks the slave to the master period", "[oscillator][crossmod]")
{
    const double sr = 48000.0;
    const double fMaster = 220.0, fSlave = 337.0;   // slave detuned higher

    PhaseDistortionOscillator master, slave;
    for (auto* o : { &master, &slave }) { o->setSampleRate (sr); o->setAmount (0.5); o->setWave (PdWave::Sawtooth); o->reset(); }
    master.setFrequency (fMaster);
    slave.setFrequency (fSlave);

    const int n = 16384;
    std::vector<float> synced (n);
    for (int i = 0; i < n; ++i)
    {
        const float m = master.processSample();   // advances + flags wrap
        (void) m;
        if (master.wrapped())
            slave.syncReset();
        synced[i] = slave.processSample();
    }

    REQUIRE_FALSE (hasBadValues (synced));
    // The sync-reset forces the slave's period to the master's, so all the
    // energy lands on the master's harmonic series (n * 220 Hz). A free-running
    // slave would instead peak near 337 Hz, which is not a 220 Hz harmonic.
    auto spec = computeSpectrum (synced, sr);
    const double ratio = spec.peakFrequency() / fMaster;
    REQUIRE (std::abs (ratio - std::round (ratio)) < 0.12);   // peak is a master harmonic
    REQUIRE (std::round (ratio) >= 1.0);
}

TEST_CASE ("PD oscillator is block-size invariant", "[oscillator][invariance]")
{
    const double sr = 48000.0, freq = 330.0, amount = 0.6;
    const int    n  = 8192;

    auto a = renderOsc (freq, amount, sr, n, 64);
    auto b = renderOsc (freq, amount, sr, n, 512);

    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == b[i]); // bit-identical
}
