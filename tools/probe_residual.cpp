/*
  Where does the inharmonic energy at 300 ms actually live?

  Build: c++ -std=c++20 -O2 -Isrc -Itests tools/probe_residual.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_residual
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <vector>

using namespace epi;
namespace an = epianalysis;

static constexpr double kFs = 48000.0;

int main (int argc, char** argv)
{
    const int note = argc > 1 ? atoi (argv[1]) : 40;
    const double vel = argc > 2 ? atof (argv[2]) : 0.8;
    const double at  = argc > 3 ? atof (argv[3]) : 0.30;

    const int N = static_cast<int> (kFs * 2.0);
    EpiEngine e;
    e.prepare (kFs, 512);

    EngineParams p;
    p.tremDepth = 0.0f; p.spaceMix = 0.0f; p.cabMix = 0.0f;
    p.preampDrive = 0.0f; p.coilSat = 0.0f;

    std::vector<float> L (N, 0.0f), R (N, 0.0f);
    NoteEvent ev { 0, NoteEvent::noteOn, note, static_cast<float> (vel) };
    for (int i = 0; i < N; i += 512)
    {
        const int n = std::min (512, N - i);
        e.process (L.data() + i, R.data() + i, n, p, i == 0 ? &ev : nullptr, i == 0 ? 1 : 0);
    }

    std::vector<double> x (N);
    for (int i = 0; i < N; ++i) x[i] = 0.5 * (L[i] + R[i]);

    const double f0 = an::refineF0 (x, kFs, 440.0 * std::pow (2.0, (note - 69) / 12.0));
    std::printf ("note %d  vel %.2f  f0 %.3f Hz  window at %.0f ms\n",
                 note, vel, f0, at * 1000.0);

    // High-resolution spectrum of a long window starting at `at`.
    const std::size_t n = 32768;
    const std::size_t start = static_cast<std::size_t> (at * kFs);
    std::vector<an::cplx> a (n, an::cplx (0.0, 0.0));
    for (std::size_t i = 0; i < n && start + i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * an::kPi * i / (n - 1));
        a[i] = x[start + i] * w;
    }
    an::fft (a);

    double peak = 0.0;
    for (std::size_t k = 1; k < n / 2; ++k) peak = std::max (peak, std::abs (a[k]));

    std::printf ("\n  %-11s %-9s %-9s %s\n", "freq (Hz)", "level dB", "f/f0", "");
    // Local maxima above -70 dB.
    for (std::size_t k = 2; k < n / 2 - 1; ++k)
    {
        const double m = std::abs (a[k]);
        if (m < std::abs (a[k - 1]) || m < std::abs (a[k + 1])) continue;
        const double db = 20.0 * std::log10 (std::max (1.0e-300, m / peak));
        if (db < -70.0) continue;
        const double f = static_cast<double> (k) * kFs / static_cast<double> (n);
        if (f > 12000.0) break;
        const double r = f / f0;
        const double devCents = 1200.0 * std::log2 (r / std::max (1.0, std::round (r)));
        std::printf ("  %10.2f  %+8.1f  %8.3f  %s\n", f, db, r,
                     std::abs (devCents) < 12.0 ? "harmonic" : "INHARMONIC");
    }

    // And how much total power is not explained by an exact harmonic series,
    // as a function of how many harmonics the model is allowed.
    std::printf ("\n  harmonics fitted -> residual rel. loudest harmonic\n");
    for (int K : { 8, 16, 32, 64, 128 })
    {
        const an::HarmonicFit f = an::fitHarmonics (x, kFs, f0, start,
                                                    an::residualWindow (kFs, f0, 24), K);
        if (! f.valid) { std::printf ("    K=%-4d  (fit failed)\n", K); continue; }
        std::printf ("    K=%-4d  %+7.1f dB   (top harmonic %.0f Hz)\n",
                     K, f.residRelMaxDb(), K * f0);
    }
    return 0;
}
