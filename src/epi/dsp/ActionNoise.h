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
// The noise the action makes, and where it is allowed to go.
//
// A key going down on a Rhodes is not silent. The hammer's pivot knocks, the
// key felt bottoms out, and on release the damper arm drops back onto the
// tine. None of that reaches the output directly, and this is the part that
// decides how it has to be modelled: the pickup is a coil round a magnet, and
// a magnet cannot hear a wooden key. It can only see steel moving in front of
// it.
//
// So the mechanism's noise gets out exactly one way -- it shakes the frame,
// the frame shakes every tine bolted to it, and the pickups hear the tines.
// That is not an approximation made for convenience; it is why a Rhodes thumps
// rather than clicks, and why the thump is pitched by whatever happens to be
// free to ring. With the pedal down there is more of it, because there is more
// left undamped to answer, and that falls out of the model rather than being
// arranged.
//
// Injecting a noise burst straight into the output would have been a line of
// code and would have got all of that wrong.
//
// The burst itself is two decaying bands rather than one, because the two
// events are not the same. The hammer knock is the harder and shorter of them;
// the key bottoming out is softer, lower and a little later. Both are far
// above the frame's own modes, so what actually comes out is the frame's
// response to being hit, which is the point.
// ---------------------------------------------------------------------------
class ActionNoise
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        knockEnv = thudEnv = 0.0;
        knockState = thudState = 0.0;
    }

    // A key going down. Velocity sets both how hard and how bright, the way it
    // does on the instrument: a key pressed gently is quieter AND duller,
    // because nothing in the mechanism is travelling fast enough to rattle.
    void strike (double velocity, Rng& rng)
    {
        const double v = std::clamp (velocity, 0.0, 1.0);
        knockEnv += 1.0 * v * v;
        thudEnv  += 0.6 * v;
        knockCut = 900.0 + 2600.0 * v;
        (void) rng;
    }

    // And coming back up: the damper arm dropping onto the tine. Softer, and
    // it does not scale with how hard the note was played -- a released key
    // returns under its own spring however it was struck.
    void release()
    {
        thudEnv += 0.22;
    }

    bool isActive() const { return knockEnv > 1.0e-6 || thudEnv > 1.0e-6; }

    // One sample of force into the frame. Newtons, in the same units as the
    // tine reaction the harp already receives.
    double tick (double amount, Rng& rng)
    {
        if (amount <= 0.0 || ! isActive()) return 0.0;

        const double n = static_cast<double> (rng.next());   // -1..1

        // The knock: short and comparatively bright.
        const double ak = std::exp (-1.0 / (0.0022 * fs));
        knockState += (knockCut / fs) * (n - knockState);
        const double knock = knockState * knockEnv;
        knockEnv *= ak;

        // The thud: longer, and lower by more than an octave.
        const double at = std::exp (-1.0 / (0.0130 * fs));
        thudState += (260.0 / fs) * (n - thudState);
        const double thud = thudState * thudEnv;
        thudEnv *= at;

        if (knockEnv < 1.0e-7) knockEnv = 0.0;
        if (thudEnv  < 1.0e-7) thudEnv  = 0.0;

        // Scaled so the control's top end is audible against a played note
        // without ever being the loudest thing in the instrument.
        return kForce * amount * (knock + thud);
    }

private:
    // A coupling gain into the frame, not a force in newtons -- the harp's
    // mass is a lumped modal figure, so the scale here is only meaningful
    // against it. Calibrated by measurement rather than by ear: at the
    // control's maximum the mechanism sits about 30 dB under the note that
    // made it, which is where a key thump sits on a direct feed from a real
    // one.
    //
    // Recalibrated once the frame started resonating. It was set against a
    // harp whose modes had been wiped by clear(), which left it an integrator
    // rather than a resonator -- a force produced a permanent velocity, so it
    // gave far more displacement for far less force than the real thing.
    static constexpr double kForce = 220000.0;

    double fs = 48000.0;
    double knockEnv = 0.0, thudEnv = 0.0;
    double knockState = 0.0, thudState = 0.0;
    double knockCut = 2000.0;
};

} // namespace epi
