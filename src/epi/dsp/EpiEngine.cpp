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

// Loudness alignment. Three instruments, three signal chains, one fader
// convention: the same four-note forte chord measures the same RMS through
// each, calibrated at -18 dBFS so a forte performance leaves honest
// headroom at unity. The trims sit at the very end of each path -- after
// every nonlinearity -- so no operating point moves, only the meter.
static constexpr float kTrimRhodes = 0.66f;
static constexpr float kTrimCP70   = 3.94f;
static constexpr float kTrimWurli  = 3.85f;

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
    if (tines.size() != static_cast<std::size_t> (kNumTines))
        tines.resize (static_cast<std::size_t> (kNumTines));
    for (int i = 0; i < kNumTines; ++i)
    {
        tines[i].prepare (sampleRate, &field);
        tines[i].setNote (kLoNote + i, cfg);
    }

    // The stages that clip run at the oversampled rate; the decimators and the
    // panner are the only things that know about the base rate.
    const double fsOs = sampleRate * Decimator::kOver;
    if (cp70.size() != static_cast<std::size_t> (kNumTines))
        cp70.resize (static_cast<std::size_t> (kNumTines));
    for (int i = 0; i < kNumTines; ++i)
    {
        cp70[i].prepare (sampleRate, &field);
        cp70[i].setNote (kLoNote + i, CP70Voice::Config {});
    }
    cp70CfgVersion.fill (0);
    cp70Preamp.prepare (sampleRate);

    if (wurli.size() != static_cast<std::size_t> (kNumTines))
        wurli.resize (static_cast<std::size_t> (kNumTines));
    for (int i = 0; i < kNumTines; ++i)
    {
        wurli[i].prepare (sampleRate, &field);
        wurli[i].setNote (kLoNote + i, WurliVoice::Config {});
    }
    wurliCfgVersion.fill (0);
    wurliBus.prepare (sampleRate * WurliVoice::kOver);
    wurliPre.prepare (sampleRate * WurliVoice::kOver);
    wurliTrem.prepare (sampleRate);
    cabinetBL.prepare (sampleRate);
    cabinetBR.prepare (sampleRate);
    airL.set (0.0, sampleRate);
    airR.set (0.0, sampleRate);

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

    // Everything that exists to avoid redundant re-application must forget
    // what it knew, because none of it is applied any more. prepare() rebuilds
    // the tines with a DEFAULT configuration; if the caches survive, the first
    // block's parameters compare equal to the stale values, nothing re-applies,
    // and the instrument comes back from a sample-rate or buffer-size change
    // with default tines, the core saturation off and the room at the wrong
    // size -- wrong in exactly the way that only shows after the host changes
    // something, which is the hardest kind of wrong to trace.
    lastCfg = RhodesVoice::Config {};
    lastCfg.hammerHardness = -1.0e30;   // guarantee the first comparison differs
    cfgVersion = 0;
    tineCfgVersion.fill (0);
    lastCoilSat = -1.0f;
    lastSpaceSize = -1.0f;
}

void EpiEngine::reset()
{
    for (auto& v : tines) v.reset();
    for (auto& v : cp70) { v.reset(); }
    cp70Preamp.reset();
    wurliBus.reset();
    wurliPre.reset();
    wurliTrem.reset();
    cabinetBL.reset();
    cabinetBR.reset();
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
    c.transducer = static_cast<double> (p.transducer);
    return c;
}

CP70Voice::Config EpiEngine::cp70Config (const EngineParams& p) const
{
    CP70Voice::Config c;
    c.hammerHardness = p.hammerHard;
    c.hammerMassNorm = p.hammerMass;
    c.escapementNorm = p.escapement;
    c.damperGrip     = p.damperGrip;
    // The honest CP meanings of the shared knobs: the "tuning spring" becomes
    // the unison spread, the tine damping trim becomes a global loss trim.
    // Everything magnetic is simply not read on this path.
    c.detuneSpread   = p.tipMass;
    c.dampTrim       = p.resDamp;
    c.detuneCents    = static_cast<double> (p.tuneCents)
                     + 100.0 * static_cast<double> (bendSemis);
    c.transducer    = static_cast<double> (p.transducer);
    // The point pickups gain the coordinate the bridge never had: position
    // along the string from the height knob, gap from the gap knob.
    c.pickupPosNorm = 0.5 + 0.5 * static_cast<double> (p.pickupPos);
    c.gapNorm       = static_cast<double> (p.pickupDist);
    return c;
}

WurliVoice::Config EpiEngine::wurliConfig (const EngineParams& p) const
{
    WurliVoice::Config c;
    c.hammerHardness = p.hammerHard;
    c.hammerMassNorm = p.hammerMass;
    c.escapementNorm = p.escapement;
    c.damperGrip     = p.damperGrip;
    // The honest Wurlitzer meanings of the shared knobs: the "spring" knob
    // becomes the tongue thickness (mu re-solved, the tech's solder move in
    // reverse), the damping trim becomes the clamp loss -- filing the knife
    // edge. The pickup pair maps to the manual's own voicing moves: height
    // is the reed's rest offset in the slot, gap is the slot clearance.
    c.tipMassNorm    = p.tipMass;
    c.dampTrim       = p.resDamp;
    c.pickupCentring = 0.5 * static_cast<double> (p.pickupPos);
    c.gapMm          = 0.3 + 1.2 * static_cast<double> (p.pickupDist);
    c.detuneCents    = static_cast<double> (p.tuneCents)
                     + 100.0 * static_cast<double> (bendSemis);
    c.transducer = static_cast<double> (p.transducer);
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
            if (tineCfgVersion[i] != cfgVersion
                || tineMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire))
            {
                rebuildTine (i, rhodesConfig (p));
                tineCfgVersion[i] = cfgVersion;
            }
            tines[i].setPedal (pedalDown);
            cp70[i].setPedal (pedalDown);
            wurli[static_cast<std::size_t> (i)].setPedal (pedalDown);
            if (p.instrument == 2)
            {
                if (wurliCfgVersion[static_cast<std::size_t> (i)] != cfgVersion)
                {
                    wurli[static_cast<std::size_t> (i)].setNote (kLoNote + i, wurliConfig (p));
                    wurliCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
                }
                wurli[static_cast<std::size_t> (i)].noteOn (e.note, vel, wurliConfig (p), seed);
            }
            else if (p.instrument == 1)
            {
                if (cp70CfgVersion[i] != cfgVersion
                    || stringMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire))
                {
                    rebuildString (i, cp70Config (p));
                    cp70CfgVersion[i] = cfgVersion;
                }
                cp70[i].noteOn (e.note, vel, cp70Config (p), seed);
            }
            else
                tines[i].noteOn (e.note, vel, rhodesConfig (p), seed);
            // The mechanism knocks whether or not the tine is heard. It goes
            // into the frame, not into the output -- see ActionNoise.
            action.strike (i, static_cast<double> (i) / (kNumTines - 1), vel);
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
            cp70[i].noteOff();
            wurli[static_cast<std::size_t> (i)].noteOff();
            action.release (i, static_cast<double> (i) / (kNumTines - 1));
            break;
        }

        case NoteEvent::allNotesOff:
            // The transport stopping is the classic sender. If the sustain
            // pedal was down at that moment the host rarely follows with a
            // CC64 release -- and all-notes-off with the pedal still engaged
            // releases every key into a pedal that keeps them all ringing:
            // the hanging-notes report, verbatim. Stop means stop.
            keyDown.fill (false);
            pedalDown = false;
            for (auto& v : tines) { v.setPedal (false); v.noteOff(); }
            for (auto& v : cp70)  { v.setPedal (false); v.noteOff(); }
            for (auto& v : wurli) { v.setPedal (false); v.noteOff(); }
            break;

        case NoteEvent::sustainOn:
            pedalDown = true;
            for (auto& v : tines) v.setPedal (true);
            for (auto& v : cp70) v.setPedal (true);
            for (auto& v : wurli) v.setPedal (true);
            break;

        case NoteEvent::sustainOff:
            pedalDown = false;
            for (auto& v : tines) v.setPedal (false);
            for (auto& v : cp70) v.setPedal (false);
            for (auto& v : wurli) v.setPedal (false);
            break;
    }
}

// One tine, built to the current configuration AND its own steel. The trim
// is applied first so the geometry solve inside setNote sees it; the dirty
// flag clears after, so a workshop edit racing this rebuild is picked up
// again next block rather than lost.
void EpiEngine::rebuildTine (int i, const RhodesVoice::Config& cfg)
{
    auto& m = tineMod[static_cast<std::size_t> (i)];
    tines[static_cast<std::size_t> (i)].setGeometryTrim (m.len.load (std::memory_order_relaxed),
                                                         m.dia.load (std::memory_order_relaxed));
    tines[static_cast<std::size_t> (i)].setPickupTrim (m.pkH.load (std::memory_order_relaxed),
                                                       m.pkG.load (std::memory_order_relaxed));
    m.dirty.store (false, std::memory_order_release);
    tines[static_cast<std::size_t> (i)].setNote (kLoNote + i, cfg);
}

void EpiEngine::process (float* outL, float* outR, int numSamples,
                         const EngineParams& p,
                         const NoteEvent* events, int numEvents)
{
    if (p.instrument == 1)
    {
        processCP70 (outL, outR, numSamples, p, events, numEvents);
        return;
    }
    if (p.instrument == 2)
    {
        processWurli (outL, outR, numSamples, p, events, numEvents);
        return;
    }

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
        auto stale = [this] (int i)
        {
            return tineCfgVersion[i] != cfgVersion
                || tineMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire);
        };
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (! stale (i)) continue;
            if (! (tines[i].isRinging() || pedalDown || keyDown[i])) continue;
            rebuildTine (i, cfg);
            tineCfgVersion[i] = cfgVersion;
            --budget;
        }
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (! stale (i)) continue;
            rebuildTine (i, cfg);
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
    {
        const float k = smoothK (numSamples);
        sm.step (sm.drive, p.preampDrive, k);
        sm.step (sm.bass, p.bassDb, k);
        sm.step (sm.treb, p.trebleDb, k);
        sm.step (sm.cabMix, p.cabMix, k);
        sm.step (sm.air, p.clarityDb, k);
        airL.set (sm.air, fs);
        airR.set (sm.air, fs);
    }
    preamp.setTone (sm.bass, sm.treb, sm.drive);
    vibrato.setRate (p.tremRate);
    vibrato.setDepth (p.tremDepth);
    vibrato.setStereo (p.tremStereo);
    phaserL.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    phaserR.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    cabinetL.setMix (sm.cabMix);
    cabinetR.setMix (sm.cabMix);
    cabinetBL.setMix (sm.cabMix);
    cabinetBR.setMix (sm.cabMix);
    if (cabDirty.exchange (false, std::memory_order_acq_rel))
    {
        const double b = cabBox.load (std::memory_order_relaxed);
        const double c = cabCone.load (std::memory_order_relaxed);
        const double d = cabDist.load (std::memory_order_relaxed);
        const double a = cabAngle.load (std::memory_order_relaxed);
        const double su = cabSusp.load (std::memory_order_relaxed);
        cabinetL.setVoicing (b, c, d, a, su);
        cabinetR.setVoicing (b, c, d, a, su);
        cabinetBL.setVoicing (b, c, d, a, su);
        cabinetBR.setVoicing (b, c, d, a, su);
    }

    int nextEvent = 0;
    // The output gain ramps linearly across the block: a fader move steps
    // once per block otherwise, and on a sustained chord that step is a tick.
    const float gain1 = p.outGainLin * kTrimRhodes;
    if (sm.gain < -0.5f) sm.gain = gain1;
    const float gain0 = sm.gain;
    sm.gain = gain1;

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

        // Each key's mechanical noise, as clamp force on its own tine.
        double noiseForce[kNumTines] = {};
        const bool anyNoise = action.tick (p.strikeNoise, noiseRng,
                                           noiseForce, kNumTines) > 0;

        for (int i = 0; i < kNumTines; ++i)
        {
            auto& t = tines[i];

            // A tine with no energy, no hammer on it and a damper resting on it
            // cannot do anything. With the pedal down the dampers are off and
            // the whole instrument is in play, which is the expensive case and
            // also the real one.
            const bool free = pedalDown || keyDown[i];
            // A tine that diverged stays out until its next strike. Without
            // this the harp wakes it on the very next sample, it diverges
            // again, and each recovery fires a step through the differentiating
            // coil: a diverge-wake-diverge cycle at a few hundred hertz, which
            // is a steady buzz from an instrument nobody is playing.
            const bool noisy = anyNoise && noiseForce[i] != 0.0;
            const bool live = ! t.isLockedOut()
                           && (t.isRinging() || noisy
                               || (free && harpCoupling > 0.0
                                        && std::abs (harpU) > 1.0e-12));
            if (! live) continue;

            if (noisy)
            {
                t.addClampForce (noiseForce[i]);
                t.setSounding (true);
            }

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
            // The winding scale: turns on this pickup's coil scale what it
            // contributes to the summed flux, applied about the rest point so
            // a mis-wound pickup never shifts anyone's operating point.
            const double sens = tineMod[static_cast<std::size_t> (i)].pkS.load (std::memory_order_relaxed);
            for (int k = 0; k < Decimator::kOver; ++k) os[k] += (voiceFlux[k] - rest) * sens;
            ++active;

            const float amp = static_cast<float> (std::abs (t.tipDisplacement()));
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
            if (amp >= loudest) { loudest = amp; loudestVoice = &t; }
            lastTip = static_cast<float> (t.tipDisplacement());
            if (t.justStruck()) { t.clearStrikeFlag(); ++strikeCount; }
        }

        harp.addForce (harpReaction);
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
            // A coil is magnetic hardware; the swapped transducers feed the
            // preamp directly, at their own calibrated level.
            double y = (p.transducer <= 1)
                     ? coil.process (static_cast<float> (os[k]))
                     : os[k];
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
        if (sm.air != 0.0f)
        {
            l = airL.process (l);
            r = airR.process (r);
        }

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

        const float g = gain0 + (gain1 - gain0) * (static_cast<float> (n + 1) / static_cast<float> (numSamples));
        const float fl = static_cast<float> (l) * g;
        const float fr = static_cast<float> (r) * g;
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

// One CP-70 course, built to the configuration and its own steel.
void EpiEngine::rebuildString (int i, const CP70Voice::Config& cfg)
{
    auto& m = stringMod[static_cast<std::size_t> (i)];
    cp70[static_cast<std::size_t> (i)].setGeometryTrim (m.len.load (std::memory_order_relaxed),
                                                        m.dia.load (std::memory_order_relaxed));
    m.dirty.store (false, std::memory_order_release);
    cp70[static_cast<std::size_t> (i)].setNote (kLoNote + i, cfg);
}

// ---------------------------------------------------------------------------
// The CP-70 path. Base rate throughout: the voice is a linear functional of
// the modal state (the measured -42 dB beat nulls forbid anything else), and
// the only nonlinearity is the shared JFET stage, nearly clean at its
// default. No decimator, no coil, no field -- the chain is
// forces -> 12 Hz HP + scoop + JFET -> panner -> room.
// ---------------------------------------------------------------------------
void EpiEngine::processCP70 (float* outL, float* outR, int numSamples,
                             const EngineParams& p,
                             const NoteEvent* events, int numEvents)
{
    const auto cfg = cp70Config (p);

    if (std::memcmp (&cfg, &lastCP70Cfg, sizeof cfg) != 0)
    {
        lastCP70Cfg = cfg;
        ++cfgVersion;
    }
    {
        // Same priority rebuild as the Rhodes: sounding first, bounded.
        int budget = 12;   // a CP string rebuild is ~40 modes of setMode
        auto stale = [this] (int i)
        {
            return cp70CfgVersion[i] != cfgVersion
                || stringMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire);
        };
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (! stale (i)) continue;
            if (! (cp70[i].isRinging() || pedalDown || keyDown[i])) continue;
            rebuildString (i, cfg);
            cp70CfgVersion[i] = cfgVersion;
            --budget;
        }
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (! stale (i)) continue;
            rebuildString (i, cfg);
            cp70CfgVersion[i] = cfgVersion;
            --budget;
        }
    }

    {
        const float k = smoothK (numSamples);
        sm.step (sm.drive, p.preampDrive, k);
        sm.step (sm.bass, p.bassDb, k);
        sm.step (sm.treb, p.trebleDb, k);
        sm.step (sm.cabMix, p.cabMix, k);
        sm.step (sm.air, p.clarityDb, k);
        airL.set (sm.air, fs);
        airR.set (sm.air, fs);
    }
    cp70Preamp.setTone (sm.bass, sm.treb, sm.drive);
    vibrato.setRate (p.tremRate);
    vibrato.setDepth (p.tremDepth);
    vibrato.setStereo (p.tremStereo);
    phaserL.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    phaserR.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    cabinetL.setMix (sm.cabMix);
    cabinetR.setMix (sm.cabMix);
    cabinetBL.setMix (sm.cabMix);
    cabinetBR.setMix (sm.cabMix);
    if (cabDirty.exchange (false, std::memory_order_acq_rel))
    {
        const double b = cabBox.load (std::memory_order_relaxed);
        const double c = cabCone.load (std::memory_order_relaxed);
        const double d = cabDist.load (std::memory_order_relaxed);
        const double a = cabAngle.load (std::memory_order_relaxed);
        const double su = cabSusp.load (std::memory_order_relaxed);
        cabinetL.setVoicing (b, c, d, a, su);
        cabinetR.setVoicing (b, c, d, a, su);
        cabinetBL.setVoicing (b, c, d, a, su);
        cabinetBR.setVoicing (b, c, d, a, su);
    }
    if (std::abs (p.spaceSize - lastSpaceSize) > 1.0e-4f)
    {
        lastSpaceSize = p.spaceSize;
        room.setSize (p.spaceSize);
    }

    // The output gain ramps linearly across the block: a fader move steps
    // once per block otherwise, and on a sustained chord that step is a tick.
    const float gain1 = p.outGainLin * kTrimCP70;
    if (sm.gain < -0.5f) sm.gain = gain1;
    const float gain0 = sm.gain;
    sm.gain = gain1;

    float pL = 0.0f, pR = 0.0f;
    int nextEvent = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        while (nextEvent < numEvents && events[nextEvent].offset <= n)
            handleEvent (events[nextEvent++], p);

        double noiseForce[kNumTines] = {};
        const bool anyNoise = action.tick (p.strikeNoise, noiseRng,
                                           noiseForce, kNumTines) > 0;

        double bus = 0.0;
        int active = 0;
        for (int i = 0; i < kNumTines; ++i)
        {
            auto& v = cp70[i];
            if (! v.isRinging()) continue;
            bus += v.process (cfg);
            v.applyDamperIfDue();
            ++active;
            const float amp = static_cast<float> (std::abs (v.tipDisplacement()));
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
        }

        // The mechanism's thump goes to the output dry -- key knock travels
        // through the case on this instrument, not through string coupling,
        // and there is no frame path to carry it.
        if (anyNoise)
        {
            double nsum = 0.0;
            for (int i = 0; i < kNumTines; ++i) nsum += noiseForce[i];
            bus += nsum * 2.0e-5;
        }

        double y = cp70Preamp.process (bus);

        double gainL = 0.0, gainR = 0.0;
        vibrato.process (1.0, gainL, gainR);
        vVibL.store (static_cast<float> (gainL), std::memory_order_relaxed);
        vVibR.store (static_cast<float> (gainR), std::memory_order_relaxed);

        double l = cabinetBL.process (y * gainL);
        double r = cabinetBR.process (y * gainR);

        if (sm.air != 0.0f)
        {
            l = airL.process (l);
            r = airR.process (r);
        }

        if (p.phaserMix > 0.0f)
        {
            l = phaserL.process (l);
            r = phaserR.process (r);
        }
        if (p.spaceMix > 0.0f)
        {
            double wl = 0.0, wr = 0.0;
            room.process (l, r, wl, wr);
            const double mix = std::clamp (static_cast<double> (p.spaceMix), 0.0, 1.0);
            l += mix * wl;
            r += mix * wr;
        }

        if (! std::isfinite (l) || ! std::isfinite (r))
        {
            cp70Preamp.reset(); cabinetBL.reset(); cabinetBR.reset();
            phaserL.reset(); phaserR.reset(); room.reset();
            l = r = 0.0;
            ++recoveries;
        }

        const float g = gain0 + (gain1 - gain0) * (static_cast<float> (n + 1) / static_cast<float> (numSamples));
        const float fl = static_cast<float> (l) * g;
        const float fr = static_cast<float> (r) * g;
        outL[n] = fl;
        outR[n] = fr;
        pL = std::max (pL, std::abs (fl));
        pR = std::max (pR, std::abs (fr));

        if (n == numSamples - 1) numActive.store (active, std::memory_order_relaxed);
    }

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
}

// ---------------------------------------------------------------------------
// The Wurlitzer path. One sample of mechanics per voice, four subsamples of
// transduction: every reed hangs its gap on the SAME 240 pF node, so the
// voices sum as capacitance perturbations and superposition at the node is
// exact -- the intermodulation belongs to the preamp, where the circuit puts
// it. Chain: reeds -> pickup bus (HP + bias) -> preamp (asymmetric clip) ->
// decimate -> tremolo (true AM, both channels alike) -> cabinet -> effects.
// ---------------------------------------------------------------------------
static constexpr double kWurliOutScale = 0.04;

void EpiEngine::processWurli (float* outL, float* outR, int numSamples,
                              const EngineParams& p,
                              const NoteEvent* events, int numEvents)
{
    static_assert (WurliVoice::kOver == Decimator::kOver,
                   "the reed writes the frames the decimator reads");
    const auto cfg = wurliConfig (p);

    if (std::memcmp (&cfg, &lastWurliCfg, sizeof cfg) != 0)
    {
        lastWurliCfg = cfg;
        ++cfgVersion;
    }

    {
        // Priority rebuild, tighter budget than the strings: a Wurlitzer
        // rebuild calibrates its hammer by simulated strikes -- bisection at
        // about a quarter millisecond per voice -- so four per block keeps
        // the worst case near one millisecond.
        int budget = 4;
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (wurliCfgVersion[static_cast<std::size_t> (i)] == cfgVersion) continue;
            if (! (wurli[static_cast<std::size_t> (i)].isRinging() || pedalDown || keyDown[i])) continue;
            wurli[static_cast<std::size_t> (i)].setNote (kLoNote + i, cfg);
            wurliCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
            --budget;
        }
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (wurliCfgVersion[static_cast<std::size_t> (i)] == cfgVersion) continue;
            wurli[static_cast<std::size_t> (i)].setNote (kLoNote + i, cfg);
            wurliCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
            --budget;
        }
    }

    // The polarizing rail as a physical drive control, on the saturation
    // knob: 100 to 200 volts spans a sagging supply to the Series 200's
    // hotter rail, with the 200A's nominal +150 in the middle.
    {
        const float k = smoothK (numSamples);
        sm.step (sm.drive, p.preampDrive, k);
        sm.step (sm.bass, p.bassDb, k);
        sm.step (sm.treb, p.trebleDb, k);
        sm.step (sm.cabMix, p.cabMix, k);
        sm.step (sm.sat, p.coilSat, k);
        sm.step (sm.air, p.clarityDb, k);
        airL.set (sm.air, fs);
        airR.set (sm.air, fs);
    }
    wurliBus.setBias (100.0 + 100.0 * std::clamp (sm.sat, 0.0f, 1.0f));
    wurliPre.setDrive (sm.drive);
    wurliPre.setTone (sm.bass, sm.treb);
    wurliTrem.setRate (p.tremRate);
    wurliTrem.setDepth (p.tremDepth);
    phaserL.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    phaserR.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    cabinetBL.setMix (sm.cabMix);
    cabinetBR.setMix (sm.cabMix);
    if (cabDirty.exchange (false, std::memory_order_acq_rel))
    {
        const double b = cabBox.load (std::memory_order_relaxed);
        const double c = cabCone.load (std::memory_order_relaxed);
        const double d = cabDist.load (std::memory_order_relaxed);
        const double a = cabAngle.load (std::memory_order_relaxed);
        const double su = cabSusp.load (std::memory_order_relaxed);
        cabinetL.setVoicing (b, c, d, a, su);
        cabinetR.setVoicing (b, c, d, a, su);
        cabinetBL.setVoicing (b, c, d, a, su);
        cabinetBR.setVoicing (b, c, d, a, su);
    }
    if (std::abs (p.spaceSize - lastSpaceSize) > 1.0e-4f)
    {
        lastSpaceSize = p.spaceSize;
        room.setSize (p.spaceSize);
    }

    // The output gain ramps linearly across the block: a fader move steps
    // once per block otherwise, and on a sustained chord that step is a tick.
    const float gain1 = p.outGainLin * kTrimWurli;
    if (sm.gain < -0.5f) sm.gain = gain1;
    const float gain0 = sm.gain;
    sm.gain = gain1;

    float pL = 0.0f, pR = 0.0f;
    int nextEvent = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        while (nextEvent < numEvents && events[nextEvent].offset <= n)
            handleEvent (events[nextEvent++], p);

        double noiseForce[kNumTines] = {};
        const bool anyNoise = action.tick (p.strikeNoise, noiseRng,
                                           noiseForce, kNumTines) > 0;

        double dcBus[WurliVoice::kOver] = {};
        double dc[WurliVoice::kOver];
        int active = 0;
        for (int i = 0; i < kNumTines; ++i)
        {
            auto& v = wurli[static_cast<std::size_t> (i)];
            if (! v.isRinging()) continue;
            v.process (cfg, dc);
            for (int k = 0; k < WurliVoice::kOver; ++k) dcBus[k] += dc[k];
            ++active;
            const float amp = static_cast<float> (std::abs (v.tipDisplacement()));
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
        }

        double os[Decimator::kOver];
        const bool electroChain = (p.transducer == 1 || p.transducer == 2);
        for (int k = 0; k < WurliVoice::kOver; ++k)
            os[k] = wurliPre.process (electroChain ? wurliBus.process (dcBus[k])
                                                   : dcBus[k] * 40.0);
        // The chain up to here speaks VOLTS -- the preamp's collector really
        // swings two volts into saturation -- and the engine's nominal
        // domain does not. Without this stage a bass fortissimo left the
        // preamp at twelve and the cabinet's excursion limit flattened it
        // against its rail, which was heard as exactly what it was: the low
        // notes far more distorted than the bark accounts for. The constant
        // is the volume pot at its calibrated spot: fortissimo bass lands
        // near half scale, level-matched to the other two instruments.
        double y = decimL.process (os) * kWurliOutScale;

        // The mechanism's thump travels through the case, dry, exactly as on
        // the CP-70 -- an electrostatic gap cannot hear a wooden key either.
        if (anyNoise)
        {
            double nsum = 0.0;
            for (int i = 0; i < kNumTines; ++i) nsum += noiseForce[i];
            y += nsum * 2.0e-5;
        }

        // True amplitude tremolo: one gain, both channels alike. This is the
        // control the Rhodes mislabels; here the word is honest.
        const double g = wurliTrem.gain();
        vVibL.store (static_cast<float> (g), std::memory_order_relaxed);
        vVibR.store (static_cast<float> (g), std::memory_order_relaxed);

        double l = cabinetBL.process (y * g);
        double r = cabinetBR.process (y * g);

        if (sm.air != 0.0f)
        {
            l = airL.process (l);
            r = airR.process (r);
        }

        if (p.phaserMix > 0.0f)
        {
            l = phaserL.process (l);
            r = phaserR.process (r);
        }
        if (p.spaceMix > 0.0f)
        {
            double wl = 0.0, wr = 0.0;
            room.process (l, r, wl, wr);
            const double mix = std::clamp (static_cast<double> (p.spaceMix), 0.0, 1.0);
            l += mix * wl;
            r += mix * wr;
        }

        if (! std::isfinite (l) || ! std::isfinite (r))
        {
            wurliBus.reset(); wurliPre.reset();
            cabinetBL.reset(); cabinetBR.reset();
            phaserL.reset(); phaserR.reset(); room.reset();
            l = r = 0.0;
            ++recoveries;
        }

        const float gOut = gain0 + (gain1 - gain0) * (static_cast<float> (n + 1) / static_cast<float> (numSamples));
        const float fl = static_cast<float> (l) * gOut;
        const float fr = static_cast<float> (r) * gOut;
        outL[n] = fl;
        outR[n] = fr;
        pL = std::max (pL, std::abs (fl));
        pR = std::max (pR, std::abs (fr));

        if (n == numSamples - 1) numActive.store (active, std::memory_order_relaxed);
    }

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
}

} // namespace epi
