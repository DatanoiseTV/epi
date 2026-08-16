/*
  Per-partial decay, with the diagnostics needed to tell a real decay rate
  from one that has run into the floor.

  Build: c++ -std=c++20 -O2 -Isrc -Itests tools/probe_decay.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_decay
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
    const int note = argc > 1 ? atoi (argv[1]) : 52;
    const double seconds = 6.0;
    const int N = static_cast<int> (kFs * seconds);

    std::printf ("note %d\n", note);

    for (double vel : { 0.10, 0.20, 0.35, 0.55, 0.85 })
    {
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
        std::printf ("\n  vel %.2f   f0 %.2f Hz   inharmonic floor at 300 ms %.1f dB\n",
                     vel, f0, an::inharmonicDb (x, kFs, f0, 0.300));
        std::printf ("    %-4s %-10s %-12s %-12s %-9s %s\n",
                     "k", "level@0.3s", "slope dB/s", "ratio to H1", "resid dB", "span fitted");

        double base = 0.0;
        for (int k = 1; k <= 5; ++k)
        {
            const an::Envelope env = an::heterodyne (x, kFs, k * f0, f0);
            if (env.z.empty()) continue;
            const double lvl = env.dbAt (0.3);
            const an::LineFit f = an::fitDecay (env, 0.3, 5.5, lvl - 35.0);
            if (! f.valid) { std::printf ("    H%-3d %+9.1f  (no fit)\n", k, lvl); continue; }
            if (k == 1) base = f.slopeDbPerS;
            std::printf ("    H%-3d %+9.1f  %11.2f  %11.2f  %8.2f  %.0f dB\n",
                         k, lvl, f.slopeDbPerS,
                         base < -0.01 ? f.slopeDbPerS / base : 0.0,
                         f.residRmsDb, -f.slopeDbPerS * 5.2);
        }
    }
    return 0;
}
