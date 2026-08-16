/*
  Epi — full-compass sweep and analysis.

  Renders every note at several velocities and dumps a table of measurements to
  stdout as CSV, so a spectral analysis can look for the places where the model
  stops behaving like an instrument.

  Build: clang++ -std=c++20 -O2 -Isrc tools/epi_sweep.cpp src/epi/dsp/EpiEngine.cpp -o epi_sweep
*/

#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace epi;

int main (int argc, char** argv)
{
    const int fs = 48000;
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const double seconds = argc > 2 ? atof (argv[2]) : 3.0;

    // Every velocity layer a player can actually produce, plus the extremes.
    const double vels[] = { 0.08, 0.2, 0.35, 0.5, 0.65, 0.8, 0.92, 1.0 };
    const int nv = (int) (sizeof (vels) / sizeof (vels[0]));

    const int N = (int) (fs * seconds);
    std::vector<float> L (N), R (N);

    // One raw file per (note, velocity): float32 mono, the left channel.
    FILE* index = std::fopen ((outDir + "/index.csv").c_str(), "w");
    std::fprintf (index, "note,vel,f0,file,peak\n");

    for (int note = 0; note < 128; ++note)
    {
        for (int vi = 0; vi < nv; ++vi)
        {
            EpiEngine e;
            e.prepare (fs, 512);

            EngineParams p;
            p.tremDepth = 0.0f;         // no modulation, so the spectrum is the model's
            p.spaceMix  = 0.0f;

            NoteEvent ev { 0, NoteEvent::noteOn, note, (float) vels[vi] };
            std::fill (L.begin(), L.end(), 0.0f);
            std::fill (R.begin(), R.end(), 0.0f);
            e.process (L.data(), R.data(), N, p, &ev, 1);

            float peak = 0.0f;
            bool bad = false;
            for (int i = 0; i < N; ++i)
            {
                if (! std::isfinite (L[i])) bad = true;
                peak = std::max (peak, std::abs (L[i]));
            }

            char name[64];
            std::snprintf (name, sizeof name, "n%03d_v%02d.f32", note, vi);
            FILE* f = std::fopen ((outDir + "/" + name).c_str(), "wb");
            std::fwrite (L.data(), 4, (size_t) N, f);
            std::fclose (f);

            const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            std::fprintf (index, "%d,%.4f,%.4f,%s,%.6f%s\n",
                          note, vels[vi], f0, name, peak, bad ? ",NONFINITE" : "");
        }
        if ((note % 16) == 0) { std::printf ("  note %d\n", note); std::fflush (stdout); }
    }
    std::fclose (index);
    std::printf ("done: 128 notes x %d velocities\n", nv);
    return 0;
}
