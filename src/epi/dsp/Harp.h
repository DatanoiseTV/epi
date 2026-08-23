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
#include "EpiModel.h"

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
        for (int m = 0; m < kModes; ++m)
            shape[m] = 1.0 / (1.0 + 0.35 * m);   // higher modes move the rail less
        applyBody();
        reset();
    }

    // What the frame is made of and how big it is, relative to stock. Beam
    // and plate alike, a uniform size scale s moves every mode by 1/s (t/L^2
    // with all dimensions scaled) and the material by sqrt(E/rho); modal
    // mass follows rho s^3; the material's internal loss adds to the
    // calibrated structural loss on a rate basis, zero for stock. State is
    // kept across retunes, so a sweep re-pitches the ring instead of
    // cutting it.
    void setBody (int materialIndex, double sizeNorm)
    {
        // Exactly 1.0 at the default half: s = 1.43^(2x-1), spanning 0.7 to
        // 1.43 with the stock scale bit-exact at centre.
        const double s = std::pow (1.43, 2.0 * std::clamp (sizeNorm, 0.0, 1.0) - 1.0);
        if (materialIndex == bodyMat && std::abs (s - bodySize) < 1.0e-9) return;
        bodyMat = materialIndex;
        bodySize = s;
        applyBody();
    }

    void reset() { sys.clear(); sys.setNumModes (kModes); }

    double displacement() const { return sys.displacementAt (shape); }

    void addForce (double f)
    {
        f *= forceScale;
        for (int m = 0; m < kModes; ++m) sys.addForce (m, f * shape[m]);
    }

    void tick() { sys.tick(); }

    double energy() const { return sys.energy(); }

private:
    // Heavy, in tine terms: the frame is kilograms against a tine's gram, which
    // is why a single note barely moves it and a held chord does.
    static constexpr double kMass = 4.0;

    void applyBody()
    {
        // A cast frame in a tolex case. Low, closely spaced, and dead within a
        // fraction of a second -- the sustain a player hears with the pedal
        // down belongs to the tines, not to this. The body scalers move the
        // whole ladder together.
        static constexpr double kHz[kModes]  = { 47.0, 88.0, 143.0, 211.0, 305.0, 418.0 };
        static constexpr double kT60[kModes] = { 0.42, 0.33, 0.26,  0.20,  0.15,  0.11 };
        const BodyScalers sc = bodyScalers (bodyMat);
        const double fScale = sc.freq / bodySize;
        // Mass enters as a force scale: the response of a mode is F/(m w^2),
        // so dividing the drive by the mass ratio is exactly the heavier
        // frame, and it works under a live retune where the modal state must
        // be kept.
        forceScale = 1.0 / (sc.mass * bodySize * bodySize * bodySize);
        for (int m = 0; m < kModes; ++m)
        {
            const double f = kHz[m] * fScale;
            const double sigma = 6.9078 / kT60[m] + kPiD * f * sc.etaAdd;
            if (sys.frequency (m) > 0.0)
                sys.retuneKeepingState (m, f, 6.9078 / sigma);
            else
                sys.setMode (m, f, 6.9078 / sigma, kMass);
        }
    }

    double forceScale = 1.0;
    int    bodyMat = 0;
    double bodySize = 1.0;
    double fs = 48000.0;
    SavModalSystem<kModes, 1> sys;
    double shape[kModes] {};
};

} // namespace epi
