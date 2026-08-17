/*
  How much of a chord's sound is intermodulation, and where it comes from.

  A Rhodes has one pickup per tine, wired in series. Faraday and the coil's
  resonance are linear, so applying them to the summed flux is exactly right --
  linear operations commute with summing. Saturation does not. Each core
  saturates on its OWN tine's flux, and the electromotive forces add afterwards.

  Anything nonlinear applied to the sum makes every note distort against every
  other note, which is a sound the instrument cannot make. The preamp is
  genuinely shared and genuinely does this; the coils are not and must not.

  Measured by rendering each note alone and together, and comparing the chord
  against the sum of its parts. Whatever is left is intermodulation.

  Build: c++ -std=c++20 -O2 -Isrc -Itests tools/probe_imd.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_imd
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace epi;
namespace an = epianalysis;

static constexpr double kFs = 48000.0;

static std::vector<double> render (const std::vector<int>& notes, float coilSat,
                                   float preampDrive, double seconds)
{
    const int N = static_cast<int> (kFs * seconds);
    const int block = 512;
    EpiEngine e;
    e.prepare (kFs, block);

    EngineParams p;
    p.tremDepth = 0.0f; p.spaceMix = 0.0f; p.cabMix = 0.0f;
    p.coilSat = coilSat;
    if (const char* v = std::getenv ("EPI_NONLIN")) p.nonlinAmt = (float) atof (v);
    if (const char* v = std::getenv ("EPI_NOISE")) p.strikeNoise = (float) atof (v);
    p.preampDrive = preampDrive;

    std::vector<NoteEvent> evs;
    for (int n : notes) evs.push_back ({ 0, NoteEvent::noteOn, n, 0.9f });

    std::vector<float> L (static_cast<std::size_t> (N), 0.0f);
    std::vector<float> R (static_cast<std::size_t> (N), 0.0f);
    for (int i = 0; i < N; i += block)
    {
        const int n = std::min (block, N - i);
        e.process (L.data() + i, R.data() + i, n, p,
                   i == 0 ? evs.data() : nullptr,
                   i == 0 ? static_cast<int> (evs.size()) : 0);
    }
    std::vector<double> x (static_cast<std::size_t> (N));
    for (int i = 0; i < N; ++i) x[static_cast<std::size_t> (i)] = L[static_cast<std::size_t> (i)];
    return x;
}

int main()
{
    // A fifth low down, where the tines swing furthest and any shared
    // nonlinearity has the most to work with.
    const std::vector<int> a { 40 }, b { 47 }, both { 40, 47 };
    const double secs = 1.5;

    std::printf ("intermodulation on a low fifth (E2 + B2), chord against the sum of its parts\n\n");
    std::printf ("  %-14s %-14s  %s\n", "coil sat", "preamp drive", "IMD rel. chord peak");

    struct Case { float sat; float drive; const char* note; };
    const Case cases[] = {
        { 0.25f, 0.30f, "as shipped" },
        { 0.00f, 0.30f, "coil saturation off" },
        { 0.25f, 0.00f, "preamp drive at zero" },
        { 0.00f, 0.00f, "both off (should be near nothing)" },
        { 1.00f, 0.30f, "coil saturation at maximum" },
        { 1.00f, 0.00f, "coil max, preamp zero (what the test uses)" },
    };

    for (const Case& c : cases)
    {
        const std::vector<double> xa = render (a, c.sat, c.drive, secs);
        const std::vector<double> xb = render (b, c.sat, c.drive, secs);
        const std::vector<double> xab = render (both, c.sat, c.drive, secs);

        double d = 0.0, ref = 0.0;
        for (std::size_t i = 0; i < xab.size(); ++i)
        {
            d = std::max (d, std::abs (xab[i] - (xa[i] + xb[i])));
            ref = std::max (ref, std::abs (xab[i]));
        }
        const double db = 20.0 * std::log10 (std::max (1.0e-12, d / std::max (1.0e-12, ref)));
        std::printf ("  %-14.2f %-14.2f  %+8.1f dB   %s\n", c.sat, c.drive, db, c.note);
    }
    return 0;
}
