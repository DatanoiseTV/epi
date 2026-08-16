/*
  Epi — how much of a core the instrument costs, as a measured number.

  A tine that is neither ringing nor under a hammer is skipped, so the cost
  depends entirely on how many are alive. The two cases that matter are a busy
  passage with the pedal up, and a held chord with the pedal down -- which is
  the expensive one, because with the pedal down every tine on the instrument
  is coupled to the harp and answering.

  Build: c++ -std=c++20 -O2 -Isrc tools/epi_bench.cpp src/epi/dsp/EpiEngine.cpp -o epi_bench
*/

#include "epi/dsp/EpiEngine.h"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <vector>

using namespace epi;

namespace
{
struct Result { double realtime; int voices; };

Result run (const char* name, int numNotes, bool pedal, double seconds, double fs)
{
    const int block = 128;
    const int N = static_cast<int> (fs * seconds);

    EpiEngine e;
    e.prepare (fs, block);

    EngineParams p;                       // stock, everything in the path
    if (const char* v = std::getenv ("EPI_COIL_SAT")) p.coilSat = (float) atof (v);
    std::vector<float> L (static_cast<std::size_t> (block));
    std::vector<float> R (static_cast<std::size_t> (block));

    std::vector<NoteEvent> ev;
    if (pedal) ev.push_back ({ 0, NoteEvent::sustainOn, 0, 0.0f });
    // Spread across the compass rather than a cluster, so the cost is not
    // dominated by one register.
    for (int i = 0; i < numNotes; ++i)
        ev.push_back ({ 0, NoteEvent::noteOn, 28 + (i * 61) / std::max (1, numNotes), 0.9f });

    const auto t0 = std::chrono::steady_clock::now();
    int peakVoices = 0;
    for (int i = 0; i < N; i += block)
    {
        const int n = std::min (block, N - i);
        e.process (L.data(), R.data(), n, p,
                   i == 0 ? ev.data() : nullptr,
                   i == 0 ? static_cast<int> (ev.size()) : 0);
        peakVoices = std::max (peakVoices, e.activeVoices());
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double wall = std::chrono::duration<double> (t1 - t0).count();
    const double rt = seconds / wall;
    std::printf ("  %-34s %3d notes  %5.1fx realtime   %3d live   %5.1f%% of a core\n",
                 name, numNotes, rt, peakVoices, 100.0 / rt);
    return { rt, peakVoices };
}
}

int main (int argc, char** argv)
{
    const double fs = argc > 1 ? atof (argv[1]) : 48000.0;
    std::printf ("Epi cost at %.0f Hz, 128-sample blocks\n\n", fs);

    double worst = 1.0e9;
    worst = std::min (worst, run ("single note",            1, false, 4.0, fs).realtime);
    worst = std::min (worst, run ("four-note chord",        4, false, 4.0, fs).realtime);
    worst = std::min (worst, run ("ten notes",             10, false, 4.0, fs).realtime);
    worst = std::min (worst, run ("ten notes, pedal down", 10, true,  4.0, fs).realtime);
    worst = std::min (worst, run ("twenty, pedal down",    20, true,  4.0, fs).realtime);
    worst = std::min (worst, run ("forty, pedal down",     40, true,  4.0, fs).realtime);

    std::printf ("\n  worst case %.1fx realtime (%.0f%% of one core)\n", worst, 100.0 / worst);
    return worst > 1.0 ? 0 : 1;
}
