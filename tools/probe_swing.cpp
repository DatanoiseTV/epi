/*
  How far each tine actually swings, across the compass and across velocity.

  A real Rhodes tine moves one to three millimetres at the bottom at forte and
  well under a tenth of that at the top -- a range past fifty to one. That
  gradient is not cosmetic: it is what puts a loud bass note deep into the
  curved part of the pickup's field while a treble note stays in the straight
  part, and so it is what makes the bass growl, what suppresses the bass
  fundamental at high velocity, and what makes it climb back as the note
  decays.

  Build: c++ -std=c++20 -O2 -Isrc tools/probe_swing.cpp \
         src/epi/dsp/EpiEngine.cpp -o /tmp/probe_swing
*/

#include "epi/dsp/RhodesVoice.h"

#include <cstdio>
#include <vector>

using namespace epi;

static constexpr double kFs = 48000.0;

int main()
{
    MagneticPickup field;
    field.prepare (MagneticPickup::Geometry {});

    std::printf ("peak tip swing in mm, and swing as a fraction of the pole half-width\n");
    std::printf ("(the pole half-width is what decides whether the field is curved there)\n\n");
    std::printf ("  %-6s %-9s", "note", "f0 Hz");
    const double vels[] = { 0.15, 0.5, 0.95 };
    for (double v : vels) std::printf ("  %8s%.2f", "v", v);
    std::printf ("   %10s\n", "v0.95/half");

    for (int note : { 28, 33, 40, 45, 52, 57, 64, 71, 78, 85, 92, 99 })
    {
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        std::printf ("  %-6d %-9.1f", note, f0);

        double loud = 0.0;
        for (double vel : vels)
        {
            RhodesVoice v;
            v.prepare (kFs, &field);
            RhodesVoice::Config cfg;
            v.setNote (note, cfg);
            v.noteOn (note, static_cast<float> (vel), cfg, 0x1234u);

            double peak = 0.0;
            double flux[8];
            const long n = static_cast<long> (kFs * 0.5);
            for (long i = 0; i < n; ++i)
            {
                v.process (cfg, flux);
                peak = std::max (peak, std::abs (v.tipDisplacement()));
            }
            std::printf ("  %12.4f", peak * 1000.0);
            loud = peak;
        }
        std::printf ("   %10.3f", loud / 1.6e-3);
        {
            RhodesVoice v;
            v.prepare (kFs, &field);
            RhodesVoice::Config cfg;
            v.setNote (note, cfg);
            const RhodesVoice::Collision& c = v.collision();
            std::printf ("   L %5.1fmm  Meff %8.2fmg  ham %6.2fmg  k %8.2e%s",
                         c.tineLength * 1000.0, c.effTineMass * 1.0e6,
                         c.hammerMass * 1.0e6, c.stiffnessUsed,
                         c.capBinding ? "  CAPPED" : "");
        }
        std::printf ("\n");
    }
    return 0;
}
