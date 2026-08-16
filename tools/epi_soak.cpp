/*
  Epi — a long, dense performance, checked for state that decays over time.

  The failure this is looking for is one that cannot be heard while playing:
  something in the model drifting, denormalising or going non-finite after
  minutes of continuous use. Live nobody holds a passage long enough to meet
  it; an offline bounce renders the whole track and meets it every time, and
  what comes out is silence from the point it happened, with a click where it
  turned over.

  Build: c++ -std=c++20 -O2 -Isrc tools/epi_soak.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/epi_soak
*/

#include "epi/dsp/EpiEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace epi;

int main (int argc, char** argv)
{
    const double fs = argc > 1 ? atof (argv[1]) : 48000.0;
    const double minutes = argc > 2 ? atof (argv[2]) : 5.0;
    const int block = 128;

    EpiEngine e;
    e.prepare (fs, block);
    EngineParams p;                       // stock: reverb, vibrato, everything

    std::vector<float> L (static_cast<std::size_t> (block));
    std::vector<float> R (static_cast<std::size_t> (block));

    const long total = static_cast<long> (fs * 60.0 * minutes);
    std::uint32_t rng = 0x1234567u;
    auto next = [&rng] { rng = rng * 1664525u + 1013904223u; return rng; };

    std::printf ("soak: %.1f minutes at %.0f Hz, dense playing with the pedal\n\n",
                 minutes, fs);
    std::printf ("  %-8s %-12s %-12s %-10s %s\n",
                 "minute", "peak", "rms", "live", "");

    long t = 0;
    long nextNote = 0;
    bool pedal = false;
    double peakWin = 0.0, rmsWin = 0.0;
    long winN = 0;
    int reportedMinute = -1;
    long nonFinite = -1;

    while (t < total)
    {
        std::vector<NoteEvent> evs;

        // A note every 90 ms or so, spread over the compass, and the pedal
        // going up and down every few seconds.
        while (nextNote < t + block)
        {
            const int note = 28 + static_cast<int> (next() % 61);
            const float vel = 0.15f + 0.85f * (static_cast<float> (next() % 1000) / 1000.0f);
            evs.push_back ({ static_cast<int> (nextNote - t), NoteEvent::noteOn, note, vel });
            // Let go of it a moment later, most of the time.
            if ((next() % 4) != 0)
                evs.push_back ({ std::min (block - 1, static_cast<int> (nextNote - t) + 60),
                                 NoteEvent::noteOff, note, 0.0f });
            nextNote += static_cast<long> (fs * 0.09);
        }
        if ((t / static_cast<long> (fs * 3.0)) % 2 == 0 && ! pedal)
        { evs.push_back ({ 0, NoteEvent::sustainOn, 0, 0.0f }); pedal = true; }
        else if ((t / static_cast<long> (fs * 3.0)) % 2 == 1 && pedal)
        { evs.push_back ({ 0, NoteEvent::sustainOff, 0, 0.0f }); pedal = false; }

        e.process (L.data(), R.data(), block, p,
                   evs.empty() ? nullptr : evs.data(), static_cast<int> (evs.size()));

        for (int i = 0; i < block; ++i)
        {
            const double v = L[static_cast<std::size_t> (i)];
            if (! std::isfinite (v) && nonFinite < 0) nonFinite = t + i;
            peakWin = std::max (peakWin, std::abs (v));
            rmsWin += v * v;
            ++winN;
        }

        t += block;

        const int minute = static_cast<int> (t / (fs * 60.0));
        if (minute != reportedMinute && winN > 0)
        {
            reportedMinute = minute;
            std::printf ("  %-8d %-12.6f %-12.6f %-10d %s\n",
                         minute, peakWin, std::sqrt (rmsWin / static_cast<double> (winN)),
                         e.activeVoices(), nonFinite >= 0 ? "NON-FINITE" : "");
            peakWin = 0.0; rmsWin = 0.0; winN = 0;
        }
    }

    if (nonFinite >= 0)
    {
        std::printf ("\n  went non-finite at %.2f s\n", static_cast<double> (nonFinite) / fs);
        return 1;
    }
    std::printf ("\n  finite throughout\n");
    return 0;
}
