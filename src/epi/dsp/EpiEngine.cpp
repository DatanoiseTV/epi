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

#include <algorithm>

namespace epi
{

void EpiEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    field.prepare ({});
    for (auto& v : voices) v.prepare (sampleRate, &field);

    coil.prepare (static_cast<float> (sampleRate));
    decimator.prepare (sampleRate);
    preamp.prepare (sampleRate);
    vibrato.prepare (sampleRate);
    cabinet.prepare (sampleRate);

    publishField();
    reset();
}

void EpiEngine::reset()
{
    for (auto& v : voices) v.reset();
    voiceAge.fill (0);
    ageCounter = 0;
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
    return c;
}

// Steal the oldest voice that is no longer held, and only then the oldest
// held one. A piano player leans on the sustain pedal, so the common case is
// a great many voices ringing and none of them held.
int EpiEngine::allocateVoice (int note)
{
    // An already-sounding copy of the same note is retaken, as a real key does.
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[i].isSounding() && voices[i].noteNumber() == note)
            return i;

    for (int i = 0; i < kMaxVoices; ++i)
        if (! voices[i].isSounding())
            return i;

    int best = 0, bestAge = voiceAge[0];
    bool foundReleased = false;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const bool released = ! voices[i].isHeld();
        if (released && ! foundReleased) { foundReleased = true; best = i; bestAge = voiceAge[i]; continue; }
        if (released == foundReleased && voiceAge[i] < bestAge) { best = i; bestAge = voiceAge[i]; }
    }
    return best;
}

void EpiEngine::handleEvent (const NoteEvent& e, const EngineParams& p)
{
    switch (e.type)
    {
        case NoteEvent::noteOn:
        {
            const int v = allocateVoice (e.note);

            // Velocity curve. At 0.5 the response is linear; below it the
            // instrument gives more for a light touch, above it the player has
            // to work for the top of the range.
            const float shape = 0.35f + 1.9f * std::clamp (p.velCurve, 0.0f, 1.0f);
            const float vel = std::pow (std::clamp (e.velocity, 0.0f, 1.0f), shape)
                            * std::clamp (expression, 0.0f, 2.0f);

            voices[v].noteOn (e.note, vel, rhodesConfig (p), seed);
            voices[v].setPedal (pedalDown);
            seed = seed * 1664525u + 1013904223u;
            voiceAge[v] = ++ageCounter;
            break;
        }

        case NoteEvent::noteOff:
            for (int i = 0; i < kMaxVoices; ++i)
                if (voices[i].isSounding() && voices[i].isHeld()
                    && voices[i].noteNumber() == e.note)
                    voices[i].noteOff();
            break;

        case NoteEvent::allNotesOff:
            for (auto& v : voices) v.noteOff();
            break;

        case NoteEvent::sustainOn:
            pedalDown = true;
            for (auto& v : voices) v.setPedal (true);
            break;

        case NoteEvent::sustainOff:
            pedalDown = false;
            for (auto& v : voices) v.setPedal (false);
            break;
    }
}

void EpiEngine::process (float* outL, float* outR, int numSamples,
                         const EngineParams& p,
                         const NoteEvent* events, int numEvents)
{
    const auto cfg = rhodesConfig (p);

    // Reconfiguring a voice re-solves its beam geometry and its tuning trim,
    // so it only happens when a parameter that changes them actually moves.
    const bool geometryMoved =
           std::abs (p.pickupPos  - lastPickupPos)  > 1.0e-4f
        || std::abs (p.pickupDist - lastPickupDist) > 1.0e-4f
        || std::abs (p.tipMass    - lastTipMass)    > 1.0e-4f
        || std::abs (p.resDamp    - lastResDamp)    > 1.0e-4f
        || std::abs (p.barCouple  - lastBarCouple)  > 1.0e-4f;
    if (geometryMoved)
    {
        lastPickupPos = p.pickupPos;   lastPickupDist = p.pickupDist;
        lastTipMass   = p.tipMass;     lastResDamp    = p.resDamp;
        lastBarCouple = p.barCouple;
        for (auto& v : voices) v.refresh (cfg);
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
        // for the whole instrument rather than once per voice.
        double os[Decimator::kOver] { 0.0, 0.0, 0.0, 0.0 };
        double voiceFlux[RhodesVoice::kOver];
        int active = 0;
        for (auto& v : voices)
        {
            if (! v.isSounding()) continue;
            v.process (cfg, voiceFlux);
            const double rest = v.restingFlux();
            for (int k = 0; k < Decimator::kOver; ++k) os[k] += voiceFlux[k] - rest;
            ++active;
            lastTip = static_cast<float> (v.tipDisplacement());
        }

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

    vTip.store (lastTip, std::memory_order_relaxed);
    vFlux.store (lastFlux, std::memory_order_relaxed);
    vOffset.store (p.pickupPos, std::memory_order_relaxed);
    vVibL.store (lastVibL, std::memory_order_relaxed);
    vVibR.store (lastVibR, std::memory_order_relaxed);
}

} // namespace epi
