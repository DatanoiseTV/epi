/*
  Aliasing from the amplifier, not the pickup.

  The tine's own nonlinearity -- the magnetic field it swings through -- is
  evaluated four times per sample and decimated, and measured, what it puts
  above the base Nyquist is 50 to 100 dB down. The amplifier's is not: the
  preamp's asymmetric clipper and the cabinet's excursion limit both run at the
  base rate, on a signal that already carries harmonics to 20 kHz. Everything
  they make above Nyquist folds straight back into the audio band, at whatever
  frequency the fold happens to put it -- which is not a harmonic of the note,
  does not move sensibly with pitch, and is what aliasing sounds like.

  Measured here as inharmonic energy against drive, on a treble note where the
  harmonics run out of room soonest.

  Build: c++ -std=c++20 -O2 -Isrc -Itests tools/probe_chain_alias.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_chain_alias
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <vector>

using namespace epi;
namespace an = epianalysis;

static constexpr double kFs = 48000.0;

static std::vector<double> render (int note, float drive, float cab)
{
    const int N = static_cast<int> (kFs * 1.5);
    const int block = 512;
    EpiEngine e;
    e.prepare (kFs, block);
    EngineParams p;
    p.tremDepth = 0.0f; p.spaceMix = 0.0f;
    p.preampDrive = drive;
    p.cabMix = cab;

    std::vector<float> L (N, 0.0f), R (N, 0.0f);
    NoteEvent ev { 0, NoteEvent::noteOn, note, 0.9f };
    for (int i = 0; i < N; i += block)
    {
        const int n = std::min (block, N - i);
        e.process (L.data() + i, R.data() + i, n, p, i == 0 ? &ev : nullptr, i == 0 ? 1 : 0);
    }
    std::vector<double> x (N);
    for (int i = 0; i < N; ++i) x[i] = 0.5 * (L[i] + R[i]);
    return x;
}

int main()
{
    std::printf ("inharmonic energy against amplifier drive\n");
    std::printf ("a nonlinearity that is properly oversampled adds harmonics, not noise,\n");
    std::printf ("so this number should barely move as the drive comes up\n\n");
    std::printf ("  %-6s %-8s  %-14s %-14s %s\n",
                 "note", "cabMix", "drive 0.0", "drive 0.5", "drive 1.0");

    for (int note : { 76 })
    {
        for (float cab : { 0.0f })
        {
            double v[9];
            const float drives[9] = { 0.0f, 0.30f, 0.50f, 0.65f, 0.75f, 0.85f, 0.92f, 0.96f, 1.00f };
            for (int i = 0; i < 9; ++i)
            {
                const std::vector<double> x = render (note, drives[i], cab);
                const double f0 = an::refineF0 (x, kFs, 440.0 * std::pow (2.0, (note - 69) / 12.0));
                v[i] = an::inharmonicDb (x, kFs, f0, 0.30);
            }
            for (int i = 0; i < 9; ++i)
                std::printf ("  drive %-5.2f  inharmonic %.1f dB\n", drives[i], v[i]);
        }
    }
    return 0;
}
