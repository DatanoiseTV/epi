/*
  How much of what the pickup generates is above the base Nyquist, and
  therefore how much of it folds back as aliasing.

  The pickup is a static nonlinearity applied to the tine's position, run at
  four times the sample rate. That is only enough if what it produces has
  effectively died before the oversampled Nyquist, because the decimator is a
  6th-order Butterworth and everything past it comes back somewhere in the
  audio band -- not as a harmonic of the note, but at whatever frequency the
  fold happens to put it, which is why aliasing reads as metallic and does not
  move sensibly with pitch.

  This looks at the flux the voice produces at the oversampled rate, before the
  decimator, and reports how the energy is distributed relative to where the
  fold happens.

  Build: c++ -std=c++20 -O2 -Isrc -Itests tools/probe_alias.cpp -o /tmp/probe_alias
*/

#include "EpiAnalysis.h"
#include "epi/dsp/RhodesVoice.h"

#include <cstdio>
#include <vector>

using namespace epi;
namespace an = epianalysis;

int main (int argc, char** argv)
{
    const double fs = argc > 1 ? atof (argv[1]) : 48000.0;
    const double fsOs = fs * RhodesVoice::kOver;

    MagneticPickup field;
    field.prepare (MagneticPickup::Geometry {});

    std::printf ("flux spectrum at the oversampled rate, %.0f Hz "
                 "(%dx of %.0f)\n", fsOs, RhodesVoice::kOver, fs);
    std::printf ("anything above %.0f Hz folds back into the audio band\n\n", fs * 0.5);

    std::printf ("  %-6s %-6s  %-10s %-10s %-10s %-10s  %s\n",
                 "note", "vel", "0-Nyq", "1-2x Nyq", "2-4x Nyq", ">4x Nyq", "worst fold");

    for (int note : { 28, 40, 52, 64, 76, 88 })
    {
        for (double vel : { 0.4, 0.95 })
        {
            RhodesVoice v;
            v.prepare (fs, &field);
            RhodesVoice::Config cfg;
            v.setNote (note, cfg);
            v.noteOn (note, vel, cfg, 0x1234u);

            // Skip the strike, then capture the steady part at the full
            // oversampled rate.
            const std::size_t n = 1u << 16;
            std::vector<an::cplx> a (n, an::cplx (0.0, 0.0));
            double flux[RhodesVoice::kOver];

            const long skip = static_cast<long> (fs * 0.15);
            for (long i = 0; i < skip; ++i) v.process (cfg, flux);

            const double rest = v.restingFlux();
            std::size_t w = 0;
            while (w < n)
            {
                v.process (cfg, flux);
                for (int k = 0; k < RhodesVoice::kOver && w < n; ++k, ++w)
                {
                    const double win = 0.5 - 0.5 * std::cos (2.0 * an::kPi * w / (n - 1));
                    a[w] = (flux[k] - rest) * win;
                }
            }
            an::fft (a);

            double band[4] = { 0, 0, 0, 0 };
            double worst = 0.0, worstF = 0.0, peak = 0.0;
            for (std::size_t k = 1; k < n / 2; ++k)
            {
                const double f = static_cast<double> (k) * fsOs / static_cast<double> (n);
                const double p = std::norm (a[k]);
                peak = std::max (peak, p);
                const double r = f / (0.5 * fs);
                if (r < 1.0)      band[0] += p;
                else if (r < 2.0) band[1] += p;
                else if (r < 4.0) band[2] += p;
                else              band[3] += p;

                // The loudest single component that will fold, and where it
                // lands after folding.
                if (r >= 1.0 && p > worst) { worst = p; worstF = f; }
            }

            auto db = [peak] (double p) { return 10.0 * std::log10 (std::max (1.0e-300, p / peak)); };

            // Where the worst offender ends up: reflect about multiples of fs.
            double folded = std::fmod (worstF, fs);
            if (folded > 0.5 * fs) folded = fs - folded;

            std::printf ("  %-6d %-6.2f  %-10.1f %-10.1f %-10.1f %-10.1f  %.0f Hz -> %.0f Hz at %.1f dB\n",
                         note, vel, db (band[0]), db (band[1]), db (band[2]), db (band[3]),
                         worstF, folded, db (worst));
        }
    }
    return 0;
}
