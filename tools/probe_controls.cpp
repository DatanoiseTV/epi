/*
  What each control is worth, and whether turning it does anything.

  Two different questions, and a control can fail either one:

    "effect"  -- set before the first note, so the engine is guaranteed to have
                 picked the value up. This is how much the control is worth at
                 all.
    "live"    -- moved while the instrument is already running, which is the
                 only way a player ever moves it. A control whose value is only
                 read when something ELSE triggers a reconfigure scores full
                 marks on the first and nothing on the second.

  Build: c++ -std=c++20 -O2 -Isrc tools/probe_controls.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_controls
*/

#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace epi;

static constexpr double kFs = 48000.0;

namespace
{
struct Knob { const char* name; float EngineParams::*f; float lo, hi; };

const Knob kKnobs[] = {
    { "tune",        &EngineParams::tuneCents, -50.0f, 50.0f },
    { "velCurve",    &EngineParams::velCurve,    0.0f, 1.0f },
    { "hammerHard",  &EngineParams::hammerHard,  0.0f, 1.0f },
    { "hammerMass",  &EngineParams::hammerMass,  0.0f, 1.0f },
    { "escapement",  &EngineParams::escapement,  0.0f, 1.0f },
    { "strikeNoise", &EngineParams::strikeNoise, 0.0f, 1.0f },
    { "damperGrip",  &EngineParams::damperGrip,  0.0f, 1.0f },
    { "tipMass",     &EngineParams::tipMass,     0.0f, 1.0f },
    { "resDamp",     &EngineParams::resDamp,     0.0f, 1.0f },
    { "barCouple",   &EngineParams::barCouple,   0.0f, 1.0f },
    { "barTune",     &EngineParams::barTune,    -1.0f, 1.0f },
    { "bodyMix",     &EngineParams::bodyMix,     0.0f, 1.0f },
    { "nonlinAmt",   &EngineParams::nonlinAmt,   0.0f, 1.0f },
    { "pickupPos",   &EngineParams::pickupPos,  -0.6f, 0.1f },
    { "pickupDist",  &EngineParams::pickupDist,  0.0f, 1.0f },
    { "coilFreq",    &EngineParams::coilFreq,    0.0f, 1.0f },
    { "coilQ",       &EngineParams::coilQ,       0.0f, 1.0f },
    { "coilSat",     &EngineParams::coilSat,     0.0f, 1.0f },
    { "preampDrive", &EngineParams::preampDrive, 0.0f, 1.0f },
    { "bass",        &EngineParams::bassDb,    -12.0f, 12.0f },
    { "treble",      &EngineParams::trebleDb,  -12.0f, 12.0f },
    { "tremRate",    &EngineParams::tremRate,    0.5f, 9.0f },
    { "tremDepth",   &EngineParams::tremDepth,   0.0f, 1.0f },
    { "cabMix",      &EngineParams::cabMix,      0.0f, 1.0f },
    { "spaceMix",    &EngineParams::spaceMix,    0.0f, 1.0f },
    { "spaceSize",   &EngineParams::spaceSize,   0.0f, 1.0f },
};

EngineParams base()
{
    // Everything downstream switched ON, or a control that only modulates
    // something disabled reads as dead when it is merely idle.
    EngineParams p;
    p.tremDepth = 0.5f;
    p.spaceMix  = 0.3f;
    return p;
}

// changeAt < 0 means "set it from the start". Otherwise the parameter starts
// at `from` and moves to `to` at that many seconds in, which is what a player
// turning a knob during a held note actually does.
// A note is struck at 0, released at 0.5 s (so the damper is exercised), the
// knob is moved at 0.6 s if asked, and a SECOND note is struck at 0.9 s.
// Controls that shape the strike can only show up on that second note, and
// judging them on the first is what makes a working control look dead.
std::vector<double> run (const Knob& k, float from, float to, double changeAt)
{
    const int N = static_cast<int> (kFs * 2.2);
    const int block = 128;
    EpiEngine e;
    e.prepare (kFs, block);

    EngineParams p = base();
    p.*(k.f) = changeAt < 0.0 ? to : from;

    std::vector<float> L (static_cast<std::size_t> (N), 0.0f);
    std::vector<float> R (static_cast<std::size_t> (N), 0.0f);

    const int offAt    = static_cast<int> (0.5 * kFs);
    const int switchAt = static_cast<int> (changeAt * kFs);
    const int againAt  = static_cast<int> (0.9 * kFs);

    for (int i = 0; i < N; i += block)
    {
        if (changeAt >= 0.0 && i >= switchAt) p.*(k.f) = to;
        const int n = std::min (block, N - i);

        NoteEvent ev[2];
        int ne = 0;
        if (i == 0)                                  ev[ne++] = { 0, NoteEvent::noteOn,  60, 0.75f };
        if (i <= offAt   && offAt   < i + n)         ev[ne++] = { offAt - i,   NoteEvent::noteOff, 60, 0.0f };
        if (i <= againAt && againAt < i + n)         ev[ne++] = { againAt - i, NoteEvent::noteOn,  60, 0.75f };

        e.process (L.data() + i, R.data() + i, n, p, ne ? ev : nullptr, ne);
    }
    std::vector<double> out (static_cast<std::size_t> (N));
    for (int i = 0; i < N; ++i) out[static_cast<std::size_t> (i)] = L[static_cast<std::size_t> (i)];
    return out;
}

// Peak difference between two renders, in dB relative to the first's peak,
// looking only after `fromS` so a mid-note change is judged on the part of the
// note that follows it.
double diffDb (const std::vector<double>& a, const std::vector<double>& b,
               double fromS, double toS)
{
    const std::size_t i0 = static_cast<std::size_t> (fromS * kFs);
    const std::size_t i1 = static_cast<std::size_t> (toS * kFs);
    double d = 0.0, ref = 0.0;
    for (std::size_t i = i0; i < i1 && i < a.size() && i < b.size(); ++i)
    {
        d = std::max (d, std::abs (a[i] - b[i]));
        ref = std::max (ref, std::abs (a[i]));
    }
    if (ref <= 0.0) return -300.0;
    return 20.0 * std::log10 (std::max (1.0e-12, d / ref));
}
}

int main()
{
    std::printf ("%-13s %-10s %-10s %-10s\n", "control", "effect", "sustained", "next note");
    std::printf ("%-13s %-10s %-10s %-10s\n", "", "dB", "dB", "dB");
    std::printf ("  --------------------------------------------------------------\n");

    for (const Knob& k : kKnobs)
    {
        // Effect: two whole renders, each with the value fixed from the start.
        const std::vector<double> a = run (k, k.lo, k.lo, -1.0);
        const std::vector<double> b = run (k, k.hi, k.hi, -1.0);
        const double effect = diffDb (a, b, 0.0, 2.2);

        // Turned at 0.6 s. Judged twice: on the note that is still ringing
        // (0.7 to 0.9 s), and on the fresh note struck at 0.9 s.
        const std::vector<double> c = run (k, k.lo, k.lo, 0.6);
        const std::vector<double> d = run (k, k.lo, k.hi, 0.6);
        const double sustained = diffDb (c, d, 0.7, 0.9);
        const double nextNote  = diffDb (c, d, 0.95, 2.2);

        const char* verdict = "";
        if (effect < -60.0)                                verdict = "  DEAD";
        else if (nextNote < -60.0)                         verdict = "  DEAD WHEN TURNED";
        else if (effect < -30.0)                           verdict = "  weak";

        std::printf ("%-13s %-10.1f %-10.1f %-10.1f%s\n",
                     k.name, effect, sustained, nextNote, verdict);
    }
    return 0;
}
