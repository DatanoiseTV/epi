/*
  Aliasing, told apart from intermodulation, by changing the sample rate.

  A hard-driven nonlinearity makes new components out of everything already
  present, and on a signal that is not a single pure tone many of those land
  between the harmonics. That is real -- a Rhodes preamp pushed hard does it,
  and it does not care what rate the model runs at.

  Aliasing does care. Anything the chain makes above the internal Nyquist folds
  back, and where it lands depends entirely on the sample rate. So: render the
  same note at two rates and measure the inharmonic content of each. If the
  higher rate is cleaner, the difference is aliasing and nothing else.

  Build: c++ -std=c++20 -O2 -Isrc -Itests tools/probe_sr.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_sr
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace epi;
namespace an = epianalysis;

static std::vector<double> render (double fs, int note, float drive, float coilFreq)
{
    const int N = static_cast<int> (fs * 1.5);
    const int block = 512;
    EpiEngine e;
    e.prepare (fs, block);
    EngineParams p;
    p.tremDepth = 0.0f; p.spaceMix = 0.0f;
    p.preampDrive = drive;
    if (const char* v = std::getenv ("EPI_NOISE")) p.strikeNoise = (float) atof (v);
    p.coilFreq = coilFreq;

    std::vector<float> L (static_cast<std::size_t> (N), 0.0f);
    std::vector<float> R (static_cast<std::size_t> (N), 0.0f);
    NoteEvent ev { 0, NoteEvent::noteOn, note, 0.9f };
    for (int i = 0; i < N; i += block)
    {
        const int n = std::min (block, N - i);
        e.process (L.data() + i, R.data() + i, n, p, i == 0 ? &ev : nullptr, i == 0 ? 1 : 0);
    }
    std::vector<double> x (static_cast<std::size_t> (N));
    for (int i = 0; i < N; ++i) x[static_cast<std::size_t> (i)] = 0.5 * (L[i] + R[i]);
    return x;
}

// Inharmonic content measured only up to 20 kHz, so both rates are judged over
// the same band and the higher one is not credited for the extra room.
static double inharmTo20k (const std::vector<double>& x, double fs, double f0)
{
    const int K = static_cast<int> (20000.0 / f0);
    const int L = std::max (2, static_cast<int> (std::lround (fs / f0)));
    const int W = 3 * L;
    const std::size_t s0 = static_cast<std::size_t> (0.30 * fs - 0.5 * W);
    if (s0 + static_cast<std::size_t> (W) >= x.size()) return 0.0;

    std::vector<double> resid (x.begin() + static_cast<std::ptrdiff_t> (s0),
                               x.begin() + static_cast<std::ptrdiff_t> (s0 + static_cast<std::size_t> (W)));
    double maxAmp = 0.0;
    for (int k = 1; k <= K; ++k)
    {
        const double fk = static_cast<double> (k) * f0;
        const an::Envelope e = an::heterodyne (x, fs, fk, f0);
        if (e.z.empty()) continue;
        for (int n = 0; n < W; ++n)
        {
            const double tn = static_cast<double> (s0 + static_cast<std::size_t> (n)) / fs;
            const double pos = (tn - e.t0) * e.rate;
            if (pos < 0.0) continue;
            const std::size_t i = static_cast<std::size_t> (pos);
            if (i + 1 >= e.z.size()) continue;
            const double frac = pos - static_cast<double> (i);
            const an::cplx env = e.z[i] * (1.0 - frac) + e.z[i + 1] * frac;
            if (n == W / 2) maxAmp = std::max (maxAmp, std::abs (env));
            const double w = 2.0 * an::kPi * fk * tn;
            resid[static_cast<std::size_t> (n)] -= env.real() * std::cos (w) - env.imag() * std::sin (w);
        }
    }
    if (maxAmp <= 0.0) return 0.0;
    double acc = 0.0;
    for (double v : resid) acc += v * v;
    return 20.0 * std::log10 (std::max (1e-300, std::sqrt (acc / W) * std::sqrt (2.0) / maxAmp));
}

int main()
{
    std::printf ("inharmonic content below 20 kHz, same note, three sample rates\n");
    std::printf ("a rate-independent number is intermodulation; one that improves\n");
    std::printf ("with rate is aliasing, and the improvement is how much of it there is\n\n");
    std::printf ("  %-6s %-7s %-9s  %-10s %-10s %-10s %s\n",
                 "note", "drive", "coilPeak", "48 kHz", "96 kHz", "192 kHz", "aliasing");

    struct Case { int note; float drive; float coil; };
    const Case cases[] = {
        { 52, 0.0f, 0.5f }, { 52, 0.3f, 0.5f }, { 52, 1.0f, 0.5f },
        { 76, 0.0f, 0.5f }, { 76, 0.3f, 0.5f }, { 76, 1.0f, 0.5f },
        { 76, 0.3f, 1.0f }, { 76, 1.0f, 1.0f },
        { 88, 0.3f, 0.5f }, { 88, 1.0f, 1.0f },
    };

    for (const Case& c : cases)
    {
        const double f0 = 440.0 * std::pow (2.0, (c.note - 69) / 12.0);
        double v[3];
        const double rates[3] = { 48000.0, 96000.0, 192000.0 };
        for (int i = 0; i < 3; ++i)
        {
            const std::vector<double> x = render (rates[i], c.note, c.drive, c.coil);
            const double f0m = an::refineF0 (x, rates[i], f0);
            v[i] = inharmTo20k (x, rates[i], f0m);
        }
        std::printf ("  %-6d %-7.1f %-9.1f  %-10.1f %-10.1f %-10.1f %+.1f dB\n",
                     c.note, c.drive, c.coil, v[0], v[1], v[2], v[0] - v[2]);
    }
    return 0;
}
