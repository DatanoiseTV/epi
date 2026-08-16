/*
  Spectral centroid against time, per note and velocity, plus where the energy
  sits in the first few milliseconds.

  Build: c++ -std=c++20 -O2 -Isrc -Itests tools/probe_bright.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_bright
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <vector>

using namespace epi;
namespace an = epianalysis;

static constexpr double kFs = 48000.0;

static std::vector<double> render (int note, double vel, double seconds)
{
    const int N = static_cast<int> (kFs * seconds);
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
    return x;
}

int main (int argc, char** argv)
{
    const int note = argc > 1 ? atoi (argv[1]) : 52;
    const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    std::printf ("note %d  f0 %.1f Hz\n\n", note, f0);

    std::printf ("  %-6s", "vel");
    const double times[] = { 0.003, 0.010, 0.030, 0.080, 0.200, 0.400, 0.800, 1.500 };
    for (double t : times) std::printf ("%8.0fms", t * 1000.0);
    std::printf ("   %8s\n", "peak");

    for (double vel : { 0.10, 0.25, 0.50, 0.75, 0.95 })
    {
        const std::vector<double> x = render (note, vel, 2.0);
        std::printf ("  %-6.2f", vel);
        for (double t : times)
        {
            // A window of four fundamental periods everywhere, so the same
            // amount of the waveform is seen at every point.
            const std::size_t w = static_cast<std::size_t> (4.0 * kFs / f0);
            const double c = an::spectralCentroid (x, kFs, static_cast<std::size_t> (t * kFs), w);
            std::printf ("%10.0f", c);
        }
        double pk = 0.0;
        for (double s : x) pk = std::max (pk, std::abs (s));
        std::printf ("   %8.4f\n", pk);
    }

    // Note on reading this: a magnitude-weighted centroid sums thousands of
    // bins, so a broadband floor tens of decibels down still moves it a long
    // way. A centroid that sits near the tenth harmonic on a note whose second
    // harmonic is twenty decibels down is not a bright note -- it is a floor,
    // and probe_residual will show what is in it.
    return 0;
}
