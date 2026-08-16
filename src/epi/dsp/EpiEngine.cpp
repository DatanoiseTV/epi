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

#include "EpiEngine.h"

#include <cstring>

#include <algorithm>

namespace epi
{

void EpiEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    field.prepare ({});
    harp.prepare (sampleRate);
    action.prepare (sampleRate);
    room.prepare (sampleRate);

    // Cut every tine for its own note, once. Nothing is reconfigured per
    // strike after this unless a parameter that changes the geometry moves.
    const auto cfg = RhodesVoice::Config{};
    for (int i = 0; i < kNumTines; ++i)
    {
        tines[i].prepare (sampleRate, &field);
        tines[i].setNote (kLoNote + i, cfg);
    }

    coil.prepare (static_cast<float> (sampleRate));
    decimator.prepare (sampleRate);
    preamp.prepare (sampleRate);
    vibrato.prepare (sampleRate);
    cabinet.prepare (sampleRate);

    publishField();
    reset();
}

void EpiEngine::retuneAll (const RhodesVoice::Config& cfg)
{
    for (int i = 0; i < kNumTines; ++i) tines[i].setNote (kLoNote + i, cfg);
}

void EpiEngine::reset()
{
    for (auto& v : tines) v.reset();
    keyDown.fill (false);
    harp.reset();
    action.reset();
    room.reset();
    pedalDown = false;
    coil.reset();
    decimator.reset();
    preamp.reset();
    vibrato.reset();
    cabinet.reset();
    numActive.store (0, std::memory_order_relaxed);
}

// The field the tine is actually moving through, sampled across the pole face
// for the UI to draw.
void EpiEngine::publishField()
{
    const float span = field.span();
    for (int i = 0; i < kFieldPoints; ++i)
    {
        const float v = -span + 2.0f * span * static_cast<float> (i)
                              / static_cast<float> (kFieldPoints - 1);
        vField[i].store (field.flux (v, field.nominalGap()), std::memory_order_relaxed);
    }
}

RhodesVoice::Config EpiEngine::rhodesConfig (const EngineParams& p) const
{
    RhodesVoice::Config c;
    c.hammerHardness = p.hammerHard;
    c.hammerMassNorm = p.hammerMass;
    c.escapementNorm = p.escapement;
    c.damperGrip     = p.damperGrip;
    c.tuningSpring   = p.tipMass;
    c.damping        = p.resDamp;
    c.barCoupling    = p.barCouple;
    c.barTuneSemis   = p.barTune;
    c.nonlinearity   = p.nonlinAmt;
    c.pickupOffset   = p.pickupPos;
    c.pickupGapNorm  = p.pickupDist;
    // Master tuning and the wheel are the same adjustment as far as a tine is
    // concerned, so they arrive as one number.
    c.detuneCents    = static_cast<double> (p.tuneCents)
                     + 100.0 * static_cast<double> (bendSemis);
    return c;
}

void EpiEngine::handleEvent (const NoteEvent& e, const EngineParams& p)
{
    switch (e.type)
    {
        case NoteEvent::noteOn:
        {
            const int i = e.note - kLoNote;
            if (i < 0 || i >= kNumTines) break;   // outside the instrument

            // Velocity curve. At 0.5 the response is linear; below it the
            // instrument gives more for a light touch, above it the player has
            // to work for the top of the range.
            const float shape = 0.35f + 1.9f * std::clamp (p.velCurve, 0.0f, 1.0f);
            const float vel = std::pow (std::clamp (e.velocity, 0.0f, 1.0f), shape)
                            * std::clamp (expression, 0.0f, 2.0f);

            keyDown[i] = true;
            tines[i].setPedal (pedalDown);
            tines[i].noteOn (e.note, vel, rhodesConfig (p), seed);
            // The mechanism knocks whether or not the tine is heard. It goes
            // into the frame, not into the output -- see ActionNoise.
            action.strike (vel, noiseRng);
            seed = seed * 1664525u + 1013904223u;
            break;
        }

        case NoteEvent::noteOff:
        {
            const int i = e.note - kLoNote;
            if (i < 0 || i >= kNumTines) break;
            keyDown[i] = false;
            tines[i].noteOff();
            action.release();
            break;
        }

        case NoteEvent::allNotesOff:
            keyDown.fill (false);
            for (auto& v : tines) v.noteOff();
            break;

        case NoteEvent::sustainOn:
            pedalDown = true;
            for (auto& v : tines) v.setPedal (true);
            break;

        case NoteEvent::sustainOff:
            pedalDown = false;
            for (auto& v : tines) v.setPedal (false);
            break;
    }
}

void EpiEngine::process (float* outL, float* outR, int numSamples,
                         const EngineParams& p,
                         const NoteEvent* events, int numEvents)
{
    const auto cfg = rhodesConfig (p);

    // Reconfiguring a voice re-solves its beam geometry and its tuning trim, so
    // it only happens when something it depends on has actually moved.
    //
    // This used to be a hand-written list of five fields against a Config of
    // eleven, and it had gone stale exactly as that arrangement always does:
    // master tuning, the hammer's hardness and mass, the damper's grip, the
    // tonebar's tuning and the bloom amount were all read here and none of them
    // were watched, so turning any of those knobs did nothing at all until some
    // OTHER knob happened to move and drag the value in with it. Comparing the
    // whole struct cannot go stale when a field is added, which is the only
    // property worth having here.
    //
    // Measured cost of the worst case -- a knob swept continuously, so this
    // fires every block and re-solves all eighty-eight tines -- is 0.41 ms
    // against a 2.67 ms budget at 128 samples.
    if (std::memcmp (&cfg, &lastCfg, sizeof cfg) != 0)
    {
        lastCfg = cfg;
        retuneAll (cfg);
    }

    // Recomputing eight decay coefficients and a damping cutoff is cheap, but
    // not per sample, and the size control has no reason to be smoothed: it
    // changes the room, and rooms do not change during a note.
    if (std::abs (p.spaceSize - lastSpaceSize) > 1.0e-4f)
    {
        lastSpaceSize = p.spaceSize;
        room.setSize (p.spaceSize);
    }

    // Coil resonance: a pickup wound to a few thousand turns, loaded by its own
    // capacitance and the cable, peaks somewhere in the presence region. Where
    // exactly is what separates one pickup from another.
    coil.setResponse (900.0f + 5600.0f * std::clamp (p.coilFreq, 0.0f, 1.0f),
                      0.55f + 5.0f * std::clamp (p.coilQ, 0.0f, 1.0f),
                      p.coilSat);
    preamp.setTone (p.bassDb, p.trebleDb, p.preampDrive);
    vibrato.setRate (p.tremRate);
    vibrato.setDepth (p.tremDepth);
    cabinet.setMix (p.cabMix);

    int nextEvent = 0;
    float pL = 0.0f, pR = 0.0f;
    RhodesVoice* loudestVoice = nullptr;
    float loudest = -1.0f;

    // How hard the tines are tied to the frame. Too little and the instrument
    // is eighty-eight separate notes; too much and a struck tine detunes its
    // neighbours instead of exciting them.
    const double harpCoupling = 900.0 * std::clamp (p.bodyMix, 0.0f, 1.0f);
    float lastTip = 0.0f, lastFlux = 0.0f, lastVibL = 1.0f, lastVibR = 1.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        while (nextEvent < numEvents && events[nextEvent].offset <= n)
            handleEvent (events[nextEvent++], p);

        // Every note's pickup is wired onto one bus, so the fluxes sum BEFORE
        // the coil and the preamp. The intermodulation that follows is shared,
        // which is why a chord is not the sum of its notes.
        //
        // The sum happens at the oversampled rate, so the decimator runs once
        // for the whole instrument rather than once per tine.
        double os[Decimator::kOver] { 0.0, 0.0, 0.0, 0.0 };
        double voiceFlux[RhodesVoice::kOver];
        int active = 0;

        // ---- the harp -------------------------------------------------------
        // One spring per tine, between its clamp and the frame, applied equal
        // and opposite. A struck tine pushes the frame; the frame pushes every
        // other tine back. That is the whole sympathetic path, and because it
        // is a spring it is passive however the energy goes round the loop.
        const double harpU = harp.displacement();
        double harpReaction = 0.0;

        for (int i = 0; i < kNumTines; ++i)
        {
            auto& t = tines[i];

            // A tine with no energy, no hammer on it and a damper resting on it
            // cannot do anything. With the pedal down the dampers are off and
            // the whole instrument is in play, which is the expensive case and
            // also the real one.
            const bool free = pedalDown || keyDown[i];
            const bool live = t.isRinging() || (free && harpCoupling > 0.0
                                                     && std::abs (harpU) > 1.0e-12);
            if (! live) continue;

            if (harpCoupling > 0.0 && free)
            {
                const double f = harpCoupling * (harpU - t.clampDisplacement());
                t.addClampForce (f);
                harpReaction -= f;
                t.setSounding (true);   // the harp has woken it
            }

            t.process (cfg, voiceFlux);
            const double rest = t.restingFlux();
            for (int k = 0; k < Decimator::kOver; ++k) os[k] += voiceFlux[k] - rest;
            ++active;

            const float amp = static_cast<float> (std::abs (t.tipDisplacement()));
            if (amp >= loudest) { loudest = amp; loudestVoice = &t; }
            lastTip = static_cast<float> (t.tipDisplacement());
            if (t.justStruck()) { t.clearStrikeFlag(); ++strikeCount; }
        }

        harp.addForce (harpReaction + action.tick (p.strikeNoise, noiseRng));
        harp.tick();

        const double flux = decimator.process (os);
        lastFlux = static_cast<float> (flux);

        double y = coil.process (static_cast<float> (flux));
        y = preamp.process (y);

        double l = 0.0, r = 0.0;
        vibrato.process (y, l, r);
        lastVibL = static_cast<float> (l / std::max (1.0e-9, std::abs (y)) * (y >= 0 ? 1 : 1));
        lastVibR = static_cast<float> (r / std::max (1.0e-9, std::abs (y)) * (y >= 0 ? 1 : 1));

        l = cabinet.process (l);
        r = cabinet.process (r);

        // The room goes after the speaker, because that is where it is.
        if (p.spaceMix > 0.0f)
        {
            double wl = 0.0, wr = 0.0;
            room.process (l, r, wl, wr);
            const double mix = std::clamp (static_cast<double> (p.spaceMix), 0.0, 1.0);
            l += mix * wl;
            r += mix * wr;
        }

        const float fl = static_cast<float> (l) * p.outGainLin;
        const float fr = static_cast<float> (r) * p.outGainLin;
        outL[n] = fl;
        outR[n] = fr;

        pL = std::max (pL, std::abs (fl));
        pR = std::max (pR, std::abs (fr));

        if (n == numSamples - 1) numActive.store (active, std::memory_order_relaxed);
    }

    while (nextEvent < numEvents) handleEvent (events[nextEvent++], p);

    auto bump = [] (std::atomic<float>& a, float v)
    {
        const float cur = a.load (std::memory_order_relaxed);
        if (v > cur) a.store (v, std::memory_order_relaxed);
    };
    bump (peakL, pL);
    bump (peakR, pR);

    // Publish the loudest voice's waveform, unrolled so the interface reads it
    // oldest-first and does not have to know about the ring buffer.
    if (loudestVoice != nullptr)
    {
        const int head = loudestVoice->traceHead();
        for (int i = 0; i < RhodesVoice::kTraceLen; ++i)
            vTrace[i].store (loudestVoice->traceAt (head + 1 + i), std::memory_order_relaxed);
        vNoteHz.store (static_cast<float> (loudestVoice->soundingHz()), std::memory_order_relaxed);
    }
    vStrikes.store (strikeCount, std::memory_order_relaxed);

    vTip.store (lastTip, std::memory_order_relaxed);
    vFlux.store (lastFlux, std::memory_order_relaxed);
    vOffset.store (p.pickupPos, std::memory_order_relaxed);
    vVibL.store (lastVibL, std::memory_order_relaxed);
    vVibR.store (lastVibR, std::memory_order_relaxed);
}

} // namespace epi
