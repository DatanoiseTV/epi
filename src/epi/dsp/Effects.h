/*
  Epi — physically modeled electric pianos
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#pragma once

#include "EpiModel.h"

namespace epi
{

// ---------------------------------------------------------------------------
// Phaser.
//
// Not part of the instrument, and worth saying so in the same breath as saying
// why it is here: no Rhodes has one inside it. A Rhodes through a Small Stone
// is also one of the two or three sounds the instrument is known for, so it
// belongs in the box the way the room does -- after the speaker, clearly an
// effect, not pretending to be physics.
//
// Six first-order all-pass sections with feedback, which is the topology every
// pedal of that era used. Each section sweeps from 0 to -180 degrees, passing
// -90 AT its corner, so the cascade covers 1080 degrees and the sum with the
// dry signal cancels at -180, -540 and -900: THREE notches, the usual
// stages-over-two. Measured on this filter with the sweep parked at its
// 800 Hz centre and no feedback, they land at 212 Hz, 794 Hz and 2.9 kHz.
// The feedback sharpens them into resonances rather than dips, and it is most
// of what separates a phaser that sounds like anything from one that sounds
// like a tone control being wobbled. (At mix = 1 there is no dry half left in
// the sum, so the notches vanish and the output is pure all-pass; the control
// is a blend for a physical reason.)
//
// The sweep coefficient uses the bilinear map without prewarping, a = (1-w)
// / (1+w). For a notch that is moving anyway the frequency error is inaudible,
// and it keeps a tangent out of the sample loop.
//
// One instance per channel. Offsetting the right channel's phase is what makes
// it wide, and it is the whole reason to run two.
// ---------------------------------------------------------------------------
class Phaser
{
public:
    static constexpr int kStages = 6;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        for (int i = 0; i < kStages; ++i) z[i] = 0.0;
        last = 0.0;
        phase = phaseOffset;
    }

    void setPhaseOffset (double cycles) { phaseOffset = cycles; }

    void setParams (double rateHz, double depthNorm, double feedback, double mixNorm)
    {
        inc   = std::clamp (rateHz, 0.02, 8.0) / fs;
        // Depth and mix are TARGETS. Both are audio-path quantities -- depth
        // sets the allpass coefficient every sample, mix scales the wet
        // difference -- so applying either at block rate steps the signal,
        // and a host writing automation in chunks was measured clicking at
        // -14 dBFS on the mix. They glide instead, at about 20 ms, which is
        // faster than any sweep reads as lag and slower than any block
        // boundary. The first call snaps: a fresh chain must start AT its
        // setting, or the opening milliseconds of every note ramp in and the
        // superposition rows read a chain that is not linear yet.
        depthT = std::clamp (depthNorm, 0.0, 1.0);
        mixT   = std::clamp (mixNorm, 0.0, 1.0);
        if (! primed) { depth = depthT; mix = mixT; primed = true; }
        // Held below the point where the loop would run away on its own. A
        // phaser that self-oscillates is a different instrument.
        fb    = std::clamp (feedback, 0.0, 0.9);
    }

    double process (double in)
    {
        const double glide = 1.0 - std::exp (-1.0 / (0.020 * fs));
        depth += glide * (depthT - depth);
        mix   += glide * (mixT - mix);

        phase += inc;
        if (phase >= 1.0) phase -= 1.0;

        // Triangle. A sine sweeps too slowly through the ends of its travel and
        // the notches sit still there; the triangle is what the pedals used and
        // it is why the motion reads as continuous.
        const double lfo = 4.0 * std::abs (phase - 0.5) - 1.0;

        // Centred at 800 Hz, up to two octaves either way, so at full depth the
        // notches run from 200 Hz to 3.2 kHz -- across the part of the spectrum
        // a Rhodes actually occupies.
        const double f = 800.0 * std::exp2 (depth * 2.0 * lfo);
        const double w = kPiD * f / fs;
        const double a = (1.0 - w) / (1.0 + w);

        double x = in + fb * last;
        for (int i = 0; i < kStages; ++i)
        {
            const double y = -a * x + z[i];
            z[i] = x + a * y;
            x = y;
        }
        // The feedback path must never carry a non-finite value forward: once
        // one is in the loop it stays. The engine's own containment would
        // catch it a stage later, but a guard at the loop is the difference
        // between losing one sample of wet signal and rebuilding the whole
        // output chain. The original this was ported from had this line, and
        // dropping it in the port was a mistake.
        if (! std::isfinite (x)) { x = 0.0; reset(); }
        last = x;

        return in + mix * (x - in);
    }

private:
    double fs = 48000.0;
    double z[kStages] {};
    double last = 0.0, phase = 0.0, phaseOffset = 0.0;
    double inc = 0.0, depth = 0.7, fb = 0.4, mix = 0.0;
    double depthT = 0.7, mixT = 0.0;
    bool   primed = false;
};

} // namespace epi
