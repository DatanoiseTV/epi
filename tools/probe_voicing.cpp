/*
  The voicing sweep: what the pickup's height and gap do to the properties the
  reference measures.

  The gap is the one that decides how much growl the instrument has. Far from
  the pole the field the tine crosses is broad and smooth, and a smooth
  function of a sinusoid has a spectrum that dies within a few harmonics. Close
  in, the wedge tip is nearly a cusp, and crossing it puts energy out past the
  twentieth -- which is what makes a hard bass note snarl rather than hum.

  Build: c++ -std=c++20 -O2 -Isrc -Itests tools/probe_voicing.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_voicing
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <vector>

using namespace epi;
namespace an = epianalysis;

static constexpr double kFs = 48000.0;

static std::vector<double> render (int note, double vel, double seconds,
                                   float dist, float pos)
{
    const int N = static_cast<int> (kFs * seconds);
    EpiEngine e;
    e.prepare (kFs, 512);
    EngineParams p;
    p.tremDepth = 0.0f; p.spaceMix = 0.0f; p.cabMix = 0.0f;
    p.preampDrive = 0.0f; p.coilSat = 0.0f;
    p.pickupDist = dist;
    p.pickupPos  = pos;

    std::vector<float> L (N, 0.0f), R (N, 0.0f);
    NoteEvent ev { 0, NoteEvent::noteOn, note, static_cast<float> (vel) };
    for (int i = 0; i < N; i += 512)
    {
        const int n = std::min (512, N - i);
        e.process (L.data() + i, R.data() + i, n, p, i == 0 ? &ev : nullptr, i == 0 ? 1 : 0);
    }
    std::vector<double> x (N);
    for (int i = 0; i < N; ++i) x[i] = 0.5 * (L[i] + R[i]);
    return x;
}

static double h2h1 (int note, double vel, float dist, float pos)
{
    const std::vector<double> x = render (note, vel, 2.0, dist, pos);
    const double f0 = an::refineF0 (x, kFs, 440.0 * std::pow (2.0, (note - 69) / 12.0));
    const an::Envelope e1 = an::heterodyne (x, kFs, f0);
    const an::Envelope e2 = an::heterodyne (x, kFs, 2.0 * f0, f0);
    if (e1.z.empty() || e2.z.empty()) return 0.0;
    return e2.dbAt (0.3) - e1.dbAt (0.3);
}

static double centroid (int note, double vel, float dist, float pos)
{
    const std::vector<double> x = render (note, vel, 2.0, dist, pos);
    return an::spectralCentroid (x, kFs, static_cast<std::size_t> (0.30 * kFs), 8192);
}

int main (int argc, char** argv)
{
    const float pos = argc > 1 ? static_cast<float> (atof (argv[1])) : -0.35f;

    std::printf ("pickup height %.2f half-widths\n\n", pos);
    std::printf ("  %-7s  %-9s %-9s %-9s  %-9s %-9s %-9s  %-8s %s\n",
                 "dist", "cent A1", "cent E5", "G2 ratio",
                 "H2-H1 E2", "soft E2", "A4 swing", "A5 C7", "B6 dB/s");
    std::printf ("  %-7s  %-9s %-9s %-9s  %-9s %-9s %-9s  %-8s %s\n",
                 "", "", "", "want 1-2.2", "want 6..24", "-40..-10", "want 16-37",
                 "< -6", "+2.2..3.9");

    for (float d : { 0.0f, 0.10f, 0.20f, 0.30f, 0.35f, 0.50f, 0.70f })
    {
        const double cA1 = centroid (33, 0.95, d, pos);
        const double cE5 = centroid (76, 0.95, d, pos);
        const double hard = h2h1 (40, 0.95, d, pos);
        const double soft = h2h1 (40, 0.15, d, pos);
        const double top  = h2h1 (96, 0.95, d, pos);

        const std::vector<double> bass = render (40, 0.95, 8.0, d, pos);
        const double f0 = an::refineF0 (bass, kFs, 82.41, 1.0, 5.0);
        const an::LineFit f = an::fitDecay (an::heterodyne (bass, kFs, f0), 0.5, 5.0);

        std::printf ("  %-7.2f  %9.0f %9.0f %9.2f  %+9.1f %+9.1f %9.1f  %+8.1f %+.2f\n",
                     d, cA1, cE5, cA1 > 0.0 ? cE5 / cA1 : 0.0,
                     hard, soft, hard - soft, top,
                     f.valid ? f.slopeDbPerS : 0.0);
    }
    return 0;
}
