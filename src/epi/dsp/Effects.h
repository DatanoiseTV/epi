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
// pedal of that era used. Each section contributes 180 degrees of phase shift
// at its corner, so six of them put five notches into the sum with the dry
// signal; the feedback sharpens them into resonances rather than dips, and it
// is most of what separates a phaser that sounds like anything from one that
// sounds like a tone control being wobbled.
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
        depth = std::clamp (depthNorm, 0.0, 1.0);
        // Held below the point where the loop would run away on its own. A
        // phaser that self-oscillates is a different instrument.
        fb    = std::clamp (feedback, 0.0, 0.9);
        mix   = std::clamp (mixNorm, 0.0, 1.0);
    }

    double process (double in)
    {
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
};

} // namespace epi
