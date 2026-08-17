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

#include <array>

namespace epi
{

// ---------------------------------------------------------------------------
// The noise the action makes, and where it is allowed to go.
//
// A key going down on a Rhodes is not silent. The hammer's pivot knocks, the
// key felt bottoms out, and on release the damper arm drops back onto the
// tine. None of that reaches the output directly, and this is the part that
// decides how it has to be modelled: the pickup is a coil round a magnet, and
// a magnet cannot hear a wooden key. It can only see steel moving in front of
// it. So the mechanism's noise gets out exactly one way -- it shakes the
// frame, the frame shakes every tine bolted to it, and the pickups hear the
// tines.
//
// Each strike is its own event, in its own voice, because each key is its own
// mechanism. One shared envelope -- which is what this used to be -- meant a
// chord's key noises piled into a single lump whose level belonged to no note
// in particular, and a soft note landing just after a loud one inherited the
// loud one's thump.
//
// The character follows the key. A bass key carries the heaviest hammer on
// the longest lever, so its bottoming-out is deeper and harder; a treble key
// is a short light lever whose noise is a higher, quicker tick. The knock
// brightens with velocity as well as loudening, because a mechanism moved
// gently has nothing in it travelling fast enough to rattle.
// ---------------------------------------------------------------------------
class ActionNoise
{
public:
    // Enough for both hands landing at once plus overlapping releases; the
    // oldest is recycled beyond that, which on this signal -- a thump lasting
    // tens of milliseconds -- is inaudible.
    static constexpr int kVoices = 12;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = {};
        next = 0;
    }

    // A key going down. `reg` is the key's place on the keyboard, 0 at the
    // bottom, 1 at the top.
    void strike (double reg, double velocity)
    {
        const double vel = std::clamp (velocity, 0.0, 1.0);
        const double r = std::clamp (reg, 0.0, 1.0);
        Voice& v = alloc();

        // The knock: the hammer's pivot and the escapement letting go. Faster
        // strikes rattle brighter as well as louder, and the smaller treble
        // mechanism rings higher.
        v.knockEnv = vel * vel * (0.9 + 0.5 * r);
        v.knockCut = (700.0 + 2400.0 * vel) * (1.0 + 1.1 * r);
        v.knockDecay = std::exp (-1.0 / ((0.0030 - 0.0016 * r) * fs));

        // The thud: the key bottoming out on its front rail felt. Heavier and
        // deeper toward the bass, where the lever and hammer are largest --
        // but only somewhat: measured on the reference recordings, the
        // mechanical residual is roughly uniform across the compass, and the
        // earlier weighting put the bass thump within five decibels of the
        // note itself.
        v.thudEnv = vel * (0.95 - 0.35 * r);
        v.thudCut = 260.0 + 260.0 * r;
        v.thudDecay = std::exp (-1.0 / ((0.0150 - 0.0070 * r) * fs));
    }

    // And coming back up: the damper arm dropping onto the tine and the key
    // returning to its rest felt. It does not scale with how hard the note
    // was played -- a released key returns under its own spring -- but it
    // does scale with the weight of what returns.
    void release (double reg)
    {
        const double r = std::clamp (reg, 0.0, 1.0);
        Voice& v = alloc();
        v.knockEnv = 0.0;
        v.thudEnv = 0.30 * (1.05 - 0.5 * r);
        v.thudCut = 280.0 + 260.0 * r;
        v.thudDecay = std::exp (-1.0 / (0.0110 * fs));
    }

    bool isActive() const
    {
        for (const auto& v : voices)
            if (v.knockEnv > 1.0e-6 || v.thudEnv > 1.0e-6) return true;
        return false;
    }

    // One sample of force into the frame, all voices summed.
    double tick (double amount, Rng& rng)
    {
        if (amount <= 0.0) return 0.0;

        double out = 0.0;
        const double n = static_cast<double> (rng.next());   // -1..1

        for (auto& v : voices)
        {
            if (v.knockEnv <= 1.0e-7 && v.thudEnv <= 1.0e-7) continue;

            v.knockState += (v.knockCut / fs) * (n - v.knockState);
            out += v.knockState * v.knockEnv;
            v.knockEnv *= v.knockDecay;

            // The thud is a BAND, not a lowpass. A lowpassed noise reaches
            // down to DC, and a force with energy at 47 and 88 hertz lands
            // squarely on the frame's two lowest modes -- what came back was
            // not a key bottoming out, it was the harp rung like a marimba
            // bar, and it was reported as exactly that. The difference of two
            // onepoles passes the tock and starves the modes.
            v.thudState += (v.thudCut / fs) * (n - v.thudState);
            v.thudFloor += (0.30 * v.thudCut / fs) * (n - v.thudFloor);
            out += (v.thudState - v.thudFloor) * v.thudEnv;
            v.thudEnv *= v.thudDecay;

            if (v.knockEnv < 1.0e-7) v.knockEnv = 0.0;
            if (v.thudEnv < 1.0e-7) v.thudEnv = 0.0;
        }

        return kForce * amount * out;
    }

private:
    struct Voice
    {
        double knockEnv = 0.0, thudEnv = 0.0;
        double knockState = 0.0, thudState = 0.0, thudFloor = 0.0;
        double knockCut = 2000.0, thudCut = 220.0;
        double knockDecay = 0.0, thudDecay = 0.0;
    };

    Voice& alloc()
    {
        // Prefer a silent slot; otherwise recycle round-robin.
        for (auto& v : voices)
            if (v.knockEnv <= 1.0e-7 && v.thudEnv <= 1.0e-7) return v;
        Voice& v = voices[static_cast<std::size_t> (next)];
        next = (next + 1) % kVoices;
        return v;
    }

    // A coupling gain into the frame, not a force in newtons -- the harp's
    // mass is a lumped modal figure, so the scale here is only meaningful
    // against it. Calibrated by measurement: at the control's maximum the
    // mechanism sits about 30 dB under the note that made it, which is where
    // a key thump sits on a direct feed from a real one.
    static constexpr double kForce = 220000.0;

    double fs = 48000.0;
    std::array<Voice, kVoices> voices {};
    int next = 0;
};

} // namespace epi
