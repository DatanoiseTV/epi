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

    // What the key lands on and returns to: the rail cloth. Stock is the
    // calibrated punching; fresh felt is quieter and deeper; leather gives
    // the deep soft thock of an older action; worn-through cloth is the
    // loud bright clack of bare wood under the punching. Scales and tilts
    // the thud only -- the knock is the hammer mechanism, not the bed.
    void setBed (int bed)
    {
        switch (bed < 0 ? 0 : bed > 3 ? 3 : bed)
        {
            default: bedLevel = 1.0;  bedCut = 1.0;  bedDecay = 1.0;  break;
            case 1:  bedLevel = 0.6;  bedCut = 0.85; bedDecay = 1.15; break;
            case 2:  bedLevel = 0.85; bedCut = 0.65; bedDecay = 1.35; break;
            case 3:  bedLevel = 1.7;  bedCut = 1.6;  bedDecay = 0.7;  break;
        }
    }

    // A key going down. `tine` is which key, `reg` its place on the keyboard,
    // 0 at the bottom, 1 at the top.
    void strike (int tine, double reg, double velocity)
    {
        const double vel = std::clamp (velocity, 0.0, 1.0);
        const double r = std::clamp (reg, 0.0, 1.0);
        Voice& v = alloc();
        v.tine = tine;

        // The knock: the hammer's pivot and the escapement letting go. A
        // RESONANT tick, not filtered noise -- broadband noise for three
        // milliseconds per strike reads as rustling paper, and was heard as
        // exactly that. A small wooden part struck once rings briefly at its
        // own frequency; the randomness belongs in WHICH frequency, since no
        // two keys' parts are identical, not in the waveform. The scatter is
        // seeded from the key index, so key forty's tick is always key
        // forty's.
        const double scatter = 1.0 + 0.12 * ((((unsigned) tine * 2654435761u & 1023u) / 1023.0) - 0.5);
        v.knockEnv = vel * vel * (0.16 + 0.10 * r);
        v.knockFreq = (1400.0 + 900.0 * vel) * (1.0 + 0.9 * r) * scatter;
        v.knockDecay = std::exp (-1.0 / ((0.0025 - 0.0012 * r) * fs));
        v.knockPhase = 0.0;

        // The thud: the key bottoming out on its front rail felt. Heavier and
        // deeper toward the bass, where the lever and hammer are largest --
        // but only somewhat: measured on the reference recordings, the
        // mechanical residual is roughly uniform across the compass, and the
        // earlier weighting put the bass thump within five decibels of the
        // note itself.
        v.thudEnv = vel * (0.95 - 0.35 * r) * bedLevel;
        v.thudCut = (260.0 + 260.0 * r) * bedCut;
        v.thudDecay = std::exp (-1.0 / ((0.0150 - 0.0070 * r) * bedDecay * fs));
    }

    // And coming back up: the damper arm dropping onto the tine and the key
    // returning to its rest felt. It does not scale with how hard the note
    // was played -- a released key returns under its own spring -- but it
    // does scale with the weight of what returns.
    void release (int tine, double reg)
    {
        const double r = std::clamp (reg, 0.0, 1.0);
        Voice& v = alloc();
        v.tine = tine;
        v.knockEnv = 0.0;
        v.thudEnv = 0.30 * (1.05 - 0.5 * r) * bedLevel;
        v.thudCut = (280.0 + 260.0 * r) * bedCut;
        v.thudDecay = std::exp (-1.0 / (0.0110 * bedDecay * fs));
    }

    bool isActive() const
    {
        for (const auto& v : voices)
            if (v.knockEnv > 1.0e-6 || v.thudEnv > 1.0e-6) return true;
        return false;
    }

    // One sample. Each voice's force goes to ITS OWN key's tine -- the
    // mechanism is bolted under that key, and its impulse reaches that tine's
    // clamp first and hardest. Routing the noise through the frame's modal
    // sum instead played it through six discrete resonances, and six pinged
    // resonances at 143 to 418 hertz are a set of chimes, which is what it
    // was heard as. A tine forced at its clamp responds broadband -- the
    // forced response follows the force, and only the tine's own note rings.
    //
    // Returns how much (if anything) was written; forces[] must hold one slot
    // per tine and is NOT cleared here.
    int tick (double amount, Rng& rng, double* forces, int numTines)
    {
        if (amount <= 0.0) return 0;

        int wrote = 0;
        const double n = static_cast<double> (rng.next());   // -1..1

        for (auto& v : voices)
        {
            if (v.knockEnv <= 1.0e-7 && v.thudEnv <= 1.0e-7) continue;

            double out = 0.0;
            if (v.knockEnv > 1.0e-7)
            {
                v.knockPhase += v.knockFreq / fs;
                out += std::sin (2.0 * kPiD * v.knockPhase) * v.knockEnv;
                v.knockEnv *= v.knockDecay;
            }

            // The thud is a BAND, not a lowpass: lowpassed noise reaches DC.
            v.thudState += (v.thudCut / fs) * (n - v.thudState);
            v.thudFloor += (0.30 * v.thudCut / fs) * (n - v.thudFloor);
            out += (v.thudState - v.thudFloor) * v.thudEnv;
            v.thudEnv *= v.thudDecay;

            if (v.knockEnv < 1.0e-7) v.knockEnv = 0.0;
            if (v.thudEnv < 1.0e-7) v.thudEnv = 0.0;

            if (v.tine >= 0 && v.tine < numTines)
            {
                forces[v.tine] += kForce * amount * out;
                ++wrote;
            }
        }
        return wrote;
    }

private:
    struct Voice
    {
        double knockEnv = 0.0, thudEnv = 0.0;
        double knockState = 0.0, thudState = 0.0, thudFloor = 0.0;
        double knockFreq = 2000.0, knockPhase = 0.0, thudCut = 220.0;
        double knockDecay = 0.0, thudDecay = 0.0;
        int tine = -1;
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

    // A coupling gain into the tine's clamp, calibrated by measurement
    // against the note that made it: at the control's default the mechanism
    // sits about 25 dB under a bass note and 40 under a mid one, which is
    // where a key thump sits on a direct feed from a real instrument, and the
    // spectrum peaks in the tock band rather than ringing anything.
    static constexpr double kForce = 7.0;

    double fs = 48000.0;
    double bedLevel = 1.0, bedCut = 1.0, bedDecay = 1.0;
    std::array<Voice, kVoices> voices {};
    int next = 0;
};

} // namespace epi
