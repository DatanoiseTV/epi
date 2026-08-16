/*
  Does changing a parameter click?

  The coil is a differentiator: emf = (phi - phiPrev) * fs. That is correct
  physics -- Faraday's law -- but it means any STEP in the flux arrives at the
  output multiplied by the sample rate. A parameter that shifts a tine's
  resting flux therefore cannot be applied instantly to a sounding note, no
  matter how small the shift is.

  Build: c++ -std=c++20 -O2 -Isrc tools/probe_clicks.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_clicks
*/

#include "epi/dsp/EpiEngine.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace epi;

static constexpr double kFs = 48000.0;

namespace
{
struct Knob { const char* name; float EngineParams::*f; float from, to; };

const Knob kKnobs[] = {
    { "tune",        &EngineParams::tuneCents,   0.0f, 1.0f },
    { "hammerHard",  &EngineParams::hammerHard,  0.50f, 0.52f },
    { "hammerMass",  &EngineParams::hammerMass,  0.50f, 0.52f },
    { "escapement",  &EngineParams::escapement,  0.40f, 0.42f },
    { "damperGrip",  &EngineParams::damperGrip,  0.60f, 0.62f },
    { "tipMass",     &EngineParams::tipMass,     0.50f, 0.52f },
    { "resDamp",     &EngineParams::resDamp,     0.35f, 0.37f },
    { "barCouple",   &EngineParams::barCouple,   0.60f, 0.62f },
    { "barTune",     &EngineParams::barTune,     0.00f, 0.02f },
    { "bodyMix",     &EngineParams::bodyMix,     0.25f, 0.27f },
    { "nonlinAmt",   &EngineParams::nonlinAmt,   0.50f, 0.52f },
    { "pickupPos",   &EngineParams::pickupPos,  -0.35f, -0.34f },
    { "pickupDist",  &EngineParams::pickupDist,  0.35f, 0.36f },
    { "coilFreq",    &EngineParams::coilFreq,    0.50f, 0.52f },
    { "coilQ",       &EngineParams::coilQ,       0.50f, 0.52f },
    { "coilSat",     &EngineParams::coilSat,     0.25f, 0.27f },
    { "preampDrive", &EngineParams::preampDrive, 0.30f, 0.32f },
    { "bass",        &EngineParams::bassDb,      0.0f, 0.5f },
    { "treble",      &EngineParams::trebleDb,    0.0f, 0.5f },
    { "tremDepth",   &EngineParams::tremDepth,   0.30f, 0.32f },
    { "cabMix",      &EngineParams::cabMix,      0.50f, 0.52f },
    { "spaceMix",    &EngineParams::spaceMix,    0.20f, 0.22f },
    { "spaceSize",   &EngineParams::spaceSize,   0.40f, 0.42f },
};
}

int main()
{
    std::printf ("a note is held; the parameter moves by 2%% at 1.0 s\n");
    std::printf ("a well-behaved control leaves no mark above the note itself\n\n");
    std::printf ("  %-13s %-12s %-12s %s\n", "control", "before dB", "at change", "jump");

    for (const Knob& k : kKnobs)
    {
        const int N = static_cast<int> (kFs * 2.0);
        const int block = 128;
        EpiEngine e;
        e.prepare (kFs, block);

        EngineParams p;
        p.*(k.f) = k.from;

        std::vector<float> L (static_cast<std::size_t> (N), 0.0f);
        std::vector<float> R (static_cast<std::size_t> (N), 0.0f);
        const int changeAt = static_cast<int> (1.0 * kFs);

        for (int i = 0; i < N; i += block)
        {
            if (i >= changeAt) p.*(k.f) = k.to;
            const int n = std::min (block, N - i);
            NoteEvent ev { 0, NoteEvent::noteOn, 52, 0.8f };
            e.process (L.data() + i, R.data() + i, n, p,
                       i == 0 ? &ev : nullptr, i == 0 ? 1 : 0);
        }

        auto peak = [&L] (int a, int b)
        {
            double m = 0.0;
            for (int i = a; i < b; ++i) m = std::max (m, std::abs (static_cast<double> (L[static_cast<std::size_t> (i)])));
            return m;
        };

        // The note just before the change, and the 10 ms straddling it.
        const double before = peak (changeAt - static_cast<int> (0.2 * kFs), changeAt - 1);
        const double at = peak (changeAt - block, changeAt + static_cast<int> (0.01 * kFs));
        const double jump = 20.0 * std::log10 (std::max (1e-12, at / std::max (1e-12, before)));

        std::printf ("  %-13s %-12.1f %-12.1f %+6.1f dB%s\n", k.name,
                     20.0 * std::log10 (std::max (1e-12, before)),
                     20.0 * std::log10 (std::max (1e-12, at)),
                     jump, jump > 6.0 ? "   CLICK" : "");
    }
    return 0;
}
