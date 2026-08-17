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

#include "ModalCore.h"

namespace epi
{

// ---------------------------------------------------------------------------
// The harp: the frame the whole tone generator is bolted to.
//
// Every tone bar in a Rhodes screws to a rail, and every rail to one cast
// frame. That frame is the reason the instrument sounds like an instrument
// rather than like eighty-eight separate notes: a struck tine shakes it, and
// it shakes every other tine back. With the damper pedal down and nothing
// stopping them, the rest of the keyboard answers -- which is most of what a
// pedalled Rhodes chord actually is.
//
// It is a few heavy, lossy modes. The frequencies are low and the damping
// substantial, because a cast frame in a wooden case is not a resonator anyone
// designed -- it is a large mass that rings briefly and dumps the rest.
//
// The coupling is a spring between each tine's clamp and the frame, applied
// equal and opposite. A spring is passive whatever the two ends do, so this can
// pass energy around the instrument all day and never make any. That matters
// more here than anywhere else in the model: this is the one place where every
// resonator is connected to every other, and a coupling that leaked would find
// its way round the loop and grow.
// ---------------------------------------------------------------------------
class Harp
{
public:
    static constexpr int kModes = 6;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        sys.prepare (sampleRate);
        sys.setNumModes (kModes);

        // A cast frame in a tolex case. Low, closely spaced, and dead within a
        // fraction of a second -- the sustain a player hears with the pedal
        // down belongs to the tines, not to this.
        static constexpr double kHz[kModes]  = { 47.0, 88.0, 143.0, 211.0, 305.0, 418.0 };
        static constexpr double kT60[kModes] = { 0.42, 0.33, 0.26,  0.20,  0.15,  0.11 };
        for (int m = 0; m < kModes; ++m)
        {
            sys.setMode (m, kHz[m], kT60[m], kMass);
            shape[m] = 1.0 / (1.0 + 0.35 * m);   // higher modes move the rail less
        }
        reset();
    }

    void reset() { sys.clear(); sys.setNumModes (kModes); }

    double displacement() const { return sys.displacementAt (shape); }

    void addForce (double f)
    {
        for (int m = 0; m < kModes; ++m) sys.addForce (m, f * shape[m]);
    }

    // The action's noise enters here, and through a different door. The keys
    // bottom out on the front rail of the key bed, not on the tone-bar rail
    // the tines hang from, and the two points sit very differently on the
    // frame's mode shapes: the lowest modes -- the whole frame swaying in its
    // case -- barely move at the front rail, while the shorter flexural modes
    // do. Driving the noise through the tone-bar weights instead put most of
    // it into the 47 and 88 hertz modes, which ring for half a second and sit
    // close enough to the bottom tines' own pitches to drive them resonantly:
    // what came out was a tenth of the note's level of low boom, reported as
    // an unnatural, loud thump. Measured after this: eighteen decibels less
    // of it, and the residue is a tock instead of a boom.
    void addNoiseForce (double f)
    {
        static constexpr double kNoiseShape[kModes] = { 0.06, 0.12, 0.45, 0.8, 1.0, 1.0 };
        for (int m = 0; m < kModes; ++m) sys.addForce (m, f * kNoiseShape[m] * shape[m]);
    }

    void tick() { sys.tick(); }

    double energy() const { return sys.energy(); }

private:
    // Heavy, in tine terms: the frame is kilograms against a tine's gram, which
    // is why a single note barely moves it and a held chord does.
    static constexpr double kMass = 4.0;

    double fs = 48000.0;
    SavModalSystem<kModes, 1> sys;
    double shape[kModes] {};
};

} // namespace epi
