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

// How many tines may be rebuilt in one block. Overridable so the bound itself
// can be tested rather than assumed.
#ifndef EPI_RETUNE_BUDGET
 #define EPI_RETUNE_BUDGET 24
#endif
namespace { constexpr int kRetuneBudget = EPI_RETUNE_BUDGET; }

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

    // The stages that clip run at the oversampled rate; the decimators and the
    // panner are the only things that know about the base rate.
    const double fsOs = sampleRate * Decimator::kOver;
    coil.prepare (static_cast<float> (fsOs));
    preamp.prepare (fsOs);
    cabinetL.prepare (fsOs);
    cabinetR.prepare (fsOs);
    decimL.prepare (sampleRate);
    decimR.prepare (sampleRate);
    vibrato.prepare (sampleRate);
    phaserL.prepare (sampleRate);
    phaserR.prepare (sampleRate);
    // A quarter cycle apart, which is what makes the notches move across the
    // pair rather than in step.
    phaserR.setPhaseOffset (0.25);

    publishField();
    reset();
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
    decimL.reset();
    decimR.reset();
    preamp.reset();
    vibrato.reset();
    cabinetL.reset();
    cabinetR.reset();
    phaserL.reset();
    phaserR.reset();
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
            // A tine that has been waiting its turn is built before it is hit.
            if (tineCfgVersion[i] != cfgVersion)
            {
                tines[i].setNote (kLoNote + i, rhodesConfig (p));
                tineCfgVersion[i] = cfgVersion;
            }
            tines[i].setPedal (pedalDown);
            tines[i].noteOn (e.note, vel, rhodesConfig (p), seed);
            // The mechanism knocks whether or not the tine is heard. It goes
            // into the frame, not into the output -- see ActionNoise.
            action.strike (static_cast<double> (i) / (kNumTines - 1), vel);
            vLastNote.store (e.note, std::memory_order_relaxed);
            seed = seed * 1664525u + 1013904223u;
            break;
        }

        case NoteEvent::noteOff:
        {
            const int i = e.note - kLoNote;
            if (i < 0 || i >= kNumTines) break;
            keyDown[i] = false;
            tines[i].noteOff();
            action.release (static_cast<double> (i) / (kNumTines - 1));
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
        ++cfgVersion;
    }

    // Rebuilt in priority order, and bounded.
    //
    // Rebuilding a tine re-solves its beam geometry and its tuning trim, and
    // doing all eighty-eight the moment anything moves costs 0.41 ms. That is
    // affordable once; it is not affordable every block, which is what a knob
    // being turned asks for. Measured while sweeping one control with eight
    // notes held under the pedal, blocks reached 3.59 ms against a 2.67 ms
    // budget and 1.7 percent of them missed the deadline -- audible as
    // crackling, and reported as exactly that.
    //
    // A tine that is sounding has to be right now, because it is being heard.
    // A silent one only has to be right before it is next struck, and noteOn
    // brings it up to date on the way past. So the sounding ones go first and
    // the rest fill in behind them, a slice at a time, with a ceiling on the
    // whole job so no single block can be made late by it.
    {
        int budget = kRetuneBudget;
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (tineCfgVersion[i] == cfgVersion) continue;
            if (! (tines[i].isRinging() || pedalDown || keyDown[i])) continue;
            tines[i].setNote (kLoNote + i, cfg);
            tineCfgVersion[i] = cfgVersion;
            --budget;
        }
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (tineCfgVersion[i] == cfgVersion) continue;
            tines[i].setNote (kLoNote + i, cfg);
            tineCfgVersion[i] = cfgVersion;
            --budget;
        }
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
                      0.55f + 5.0f * std::clamp (p.coilQ, 0.0f, 1.0f));

    // The iron each tine has its own of. Kept out of the Config that drives a
    // rebuild, because changing it needs no geometry re-solved -- only a
    // coefficient handed to every voice.
    if (std::abs (p.coilSat - lastCoilSat) > 1.0e-4f)
    {
        lastCoilSat = p.coilSat;
        for (auto& t : tines) t.setCoreSaturation (p.coilSat);
    }
    preamp.setTone (p.bassDb, p.trebleDb, p.preampDrive);
    vibrato.setRate (p.tremRate);
    vibrato.setDepth (p.tremDepth);
    vibrato.setStereo (p.tremStereo);
    phaserL.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    phaserR.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    cabinetL.setMix (p.cabMix);
    cabinetR.setMix (p.cabMix);

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
                // Read BEFORE the tine is stepped, so a tine that diverged on
                // the previous sample would hand its infinity to the frame --
                // and the frame hands it to all eighty-seven others. The voice
                // puts itself back a moment later, by which time it is too
                // late. One test here contains it to the tine it started in.
                double f = harpCoupling * (harpU - t.clampDisplacement());
                if (! std::isfinite (f)) f = 0.0;
                t.addClampForce (f);
                harpReaction -= f;
                t.setSounding (true);   // the harp has woken it
            }

            t.process (cfg, voiceFlux);
            const double rest = t.restingFlux();
            for (int k = 0; k < Decimator::kOver; ++k) os[k] += voiceFlux[k] - rest;
            ++active;

            const float amp = static_cast<float> (std::abs (t.tipDisplacement()));
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
            if (amp >= loudest) { loudest = amp; loudestVoice = &t; }
            lastTip = static_cast<float> (t.tipDisplacement());
            if (t.justStruck()) { t.clearStrikeFlag(); ++strikeCount; }
        }

        harp.addForce (harpReaction + action.tick (p.strikeNoise, noiseRng));
        harp.tick();
        if (! std::isfinite (harp.displacement())) harp.reset();

        // Everything nonlinear stays at the oversampled rate, and the whole
        // chain is decimated once, at the end.
        //
        // Decimating first -- which is what this did -- antialiases the tine's
        // own nonlinearity properly and then feeds the result straight into two
        // more nonlinearities running at the base rate: the preamp's asymmetric
        // clipper and the cabinet's excursion limit. Both of those meet a
        // signal that already carries harmonics to 20 kHz, and everything they
        // make above Nyquist folds back at whatever frequency the fold happens
        // to put it. Measured on an E5, the inharmonic content went from -60 dB
        // with the drive down to -26 dB with it up: 34 dB of noise that appears
        // for no reason other than the drive being raised. A nonlinearity with
        // enough room adds harmonics, not noise.
        //
        // Raising the coil's resonant peak made it worse, for the same reason:
        // it lifts exactly the high harmonics that then have nowhere to go.
        lastFlux = static_cast<float> (os[Decimator::kOver - 1]);

        // The panner is a slow modulation and is resolved once per output
        // sample, then held across the frame. Advancing its oscillator four
        // times per sample would run it four times too fast.
        double gainL = 0.0, gainR = 0.0;
        vibrato.process (1.0, gainL, gainR);
        lastVibL = static_cast<float> (gainL);
        lastVibR = static_cast<float> (gainR);

        double osL[Decimator::kOver], osR[Decimator::kOver];
        for (int k = 0; k < Decimator::kOver; ++k)
        {
            double y = coil.process (static_cast<float> (os[k]));
            y = preamp.process (y);
            // One cabinet per channel. Running both through a single instance
            // interleaves two signals in its filters, which is not a stereo
            // image of anything.
            osL[k] = cabinetL.process (y * gainL);
            osR[k] = cabinetR.process (y * gainR);
        }

        double l = decimL.process (osL);
        double r = decimR.process (osR);

        // The phaser sits between the speaker and the room, which is where a
        // pedal in front of an amp effectively lands once the amp is modelled.
        if (p.phaserMix > 0.0f)
        {
            l = phaserL.process (l);
            r = phaserR.process (r);
        }

        // The room goes after the speaker, because that is where it is.
        if (p.spaceMix > 0.0f)
        {
            double wl = 0.0, wr = 0.0;
            room.process (l, r, wl, wr);
            const double mix = std::clamp (static_cast<double> (p.spaceMix), 0.0, 1.0);
            l += mix * wl;
            r += mix * wr;
        }

        // The last line of defence. Everything downstream of the tines holds
        // state -- the coil, the preamp, the phaser's feedback loop, the room
        // -- and a single non-finite sample entering any of them stays there
        // for good. Nothing should ever get this far; if it does, the chain is
        // rebuilt and one sample is lost, which is a click rather than a
        // permanently dead instrument.
        if (! std::isfinite (l) || ! std::isfinite (r))
        {
            coil.reset(); preamp.reset(); cabinetL.reset(); cabinetR.reset();
            decimL.reset(); decimR.reset(); phaserL.reset(); phaserR.reset();
            room.reset();
            l = r = 0.0;
            ++recoveries;
        }

        const float fl = static_cast<float> (l) * p.outGainLin;
        const float fr = static_cast<float> (r) * p.outGainLin;
        outL[n] = fl;
        outR[n] = fr;

        pL = std::max (pL, std::abs (fl));
        pR = std::max (pR, std::abs (fr));

        if (n == numSamples - 1) numActive.store (active, std::memory_order_relaxed);
    }

    // Hand the whole harp to the interface, and reset the accumulator. A tine
    // that was not live this block gets zero, which is what it is doing.
    for (int i = 0; i < kNumTines; ++i)
    {
        vTineTip[i].store (tineBlockPeak[i], std::memory_order_relaxed);
        tineBlockPeak[i] = 0.0f;
    }
    for (int w = 0; w < kKeyWords; ++w)
    {
        std::uint32_t bits = 0;
        for (int b = 0; b < 32; ++b)
        {
            const int i = w * 32 + b;
            if (i < kNumTines && keyDown[i]) bits |= (1u << b);
        }
        vKeys[w].store (bits, std::memory_order_relaxed);
    }
    vPedal.store (pedalDown, std::memory_order_relaxed);

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
