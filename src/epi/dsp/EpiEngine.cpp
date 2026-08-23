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

// Loudness alignment. Five instruments, five signal chains, one fader
// convention: the same mezzo-forte chord measures the same RMS through
// each, benched at -18 dBFS within a decibel -- raised from -24 because
// the plugin sat quiet against other instruments in a session. Peaks
// above the bench land in the soft output rail, which bounds at 1.0 by
// construction: fortissimo attacks (the grand's especially, with its real
// 22 dB crest) get the transient rounding of a console, never a hard
// clip.
// The trims sit at the very end of each path, after every nonlinearity,
// so no operating point moves, only the meter.
static constexpr float kTrimRhodes = 1.32f;
static constexpr float kTrimCP70   = 7.88f;
static constexpr float kTrimWurli  = 12.76f;
static constexpr float kTrimGrand  = 150.0f;   // mic-pair units are small. Deliberately ~6 dB under the electrics' bench: a close-miked grand carries a 22 dB attack crest, and matching RMS exactly would put every mf attack into the output rail
static constexpr float kTrimClav   = 0.292f;  // matched to the -24 dBFS mf bench like the other four

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

    if (grand.size() != static_cast<std::size_t> (kNumTines))
        grand.resize (static_cast<std::size_t> (kNumTines));
    grandBoard.prepare (sampleRate);
    for (int i = 0; i < kNumTines; ++i)
    {
        grand[i].prepare (sampleRate);
        GrandRadiator::panGains (kLoNote + i, grandPanL[i], grandPanR[i]);
    }
    grandCfgVersion.fill (0);
    grandRad.prepare (sampleRate);
    grandMics.prepare (sampleRate);

    if (clav.size() != static_cast<std::size_t> (kNumTines))
        clav.resize (static_cast<std::size_t> (kNumTines));
    for (int i = 0; i < kNumTines; ++i)
        clav[static_cast<std::size_t> (i)].prepare (sampleRate, &field);
    clavCfgVersion.fill (0);
    // The stack and preamp run at the oversampled rate, as calibrated:
    // the saturation's harmonics must exist above base Nyquist BEFORE the
    // decimator, or they fold.
    clavTone.prepare (sampleRate * ClavinetVoice::kOver);
    clavPre.prepare (sampleRate * ClavinetVoice::kOver);
    clavKnock.prepare (sampleRate);

    wurliBus.prepare (sampleRate * WurliVoice::kOver);
    wurliPre.prepare (sampleRate * WurliVoice::kOver);
    wurliTrem.prepare (sampleRate);
    cabinetBL.prepare (sampleRate);
    cabinetBR.prepare (sampleRate);
    cp70Frame.prepare (sampleRate);
    wurliFrame.prepare (sampleRate);
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
    cp70Frame.reset();
    wurliFrame.reset();
    for (auto& v : grand) v.reset();
    grandBoard.prepare (fs);
    grandRad.prepare (fs);
    grandMics.prepare (fs);
    for (auto& v : clav) v.reset();
    clavTone.prepare (fs * ClavinetVoice::kOver);
    clavPre.prepare (fs * ClavinetVoice::kOver);
    clavKnock.reset();
    keyDown.fill (false);
    harp.reset();
    action.reset();
    room.reset();
    pedalDown = false;
    pedalAmount = 0.0;
    unaCorda = false;
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
    c.material   = static_cast<double> (p.material);
    c.damperFelt = static_cast<double> (p.damperFelt);
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
    c.material      = static_cast<double> (p.material);
    c.damperFelt    = static_cast<double> (p.damperFelt);
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
    c.material   = static_cast<double> (p.material);
    c.damperFelt = static_cast<double> (p.damperFelt);
    return c;
}

GrandVoice::Config EpiEngine::grandConfig (const EngineParams& p) const
{
    GrandVoice::Config c;
    c.hammerHardness = p.hammerHard;
    c.hammerMassNorm = p.hammerMass;
    c.escapementNorm = p.escapement;
    c.damperGrip     = p.damperGrip;
    // The grand meanings of the shared resonator knobs: the "spring" knob is
    // the unison spread (the tuner's hairsbreadth between the three strings),
    // the damping trim scales the strings' intrinsic loss.
    c.detuneSpread   = p.tipMass;
    c.dampTrim       = p.resDamp;
    c.detuneCents    = static_cast<double> (p.tuneCents)
                     + 100.0 * static_cast<double> (bendSemis);
    c.material       = static_cast<double> (p.material);
    c.damperFelt     = static_cast<double> (p.damperFelt);
    c.unaCorda       = unaCorda;
    return c;
}

ClavinetVoice::Config EpiEngine::clavConfig (const EngineParams& p) const
{
    ClavinetVoice::Config c;
    c.hammerHardness = p.hammerHard;
    c.hammerMassNorm = p.hammerMass;
    // The tangent's rest distance from the string: the let-off knob's honest
    // Clavinet meaning (EURASIP's d).
    c.escapementNorm = p.escapement;
    // The yarn knob is the aging variable: the measured source says release
    // is short "with an instrument in mint condition, with an effective
    // yarn damper", so the shared default (0.6) must mean mint-ish here --
    // x^0.32 lands it at 0.85, where the release is the brief thup with a
    // hint of the three-semitone drop, and the bottom of the knob still
    // reaches compressed old wool with the drop hanging out, as on an aged
    // instrument.
    c.damperGrip     = std::pow (std::clamp (static_cast<double> (p.damperGrip), 0.0, 1.0), 0.32);
    // The shared knobs' DEFAULTS must land on the voice's calibrated
    // operating points (gap 0.5, damp trim 0.5), or the default patch
    // plays a different instrument than the one the suite verified --
    // measured as a +15 dB high-harmonic plateau from the tighter gap
    // driving the preamp knee. x^0.66 maps the shared default 0.35 onto
    // 0.5 and keeps both endpoints.
    c.gapNorm        = std::pow (std::clamp (static_cast<double> (p.pickupDist), 0.0, 1.0), 0.66);
    c.pickupSel      = static_cast<double> (p.clavSwitch);
    c.dampTrim       = std::pow (std::clamp (static_cast<double> (p.resDamp), 0.0, 1.0), 0.66);
    c.detuneCents    = static_cast<double> (p.tuneCents)
                     + 100.0 * static_cast<double> (bendSemis);
    c.transducer     = static_cast<double> (p.transducer);
    c.material       = static_cast<double> (p.material);
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
            tines[i].setPedal (pedalAmount);
            cp70[i].setPedal (pedalAmount);
            wurli[static_cast<std::size_t> (i)].setPedal (pedalAmount);
            grand[static_cast<std::size_t> (i)].setPedal (pedalAmount);
            clav[static_cast<std::size_t> (i)].setPedal (pedalAmount);
            if (p.instrument == 4)
            {
                if (clavCfgVersion[static_cast<std::size_t> (i)] != cfgVersion)
                {
                    clav[static_cast<std::size_t> (i)].setNote (kLoNote + i, clavConfig (p));
                    clavCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
                }
                clav[static_cast<std::size_t> (i)].noteOn (e.note, vel, clavConfig (p), seed);
                clavKnock.strike (i, vel);
            }
            else if (p.instrument == 3)
            {
                if (grandCfgVersion[static_cast<std::size_t> (i)] != cfgVersion)
                {
                    grand[static_cast<std::size_t> (i)].setNote (kLoNote + i, grandConfig (p), grandBoard);
                    grandCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
                }
                grand[static_cast<std::size_t> (i)].noteOn (e.note, vel, grandConfig (p), grandBoard, seed);
            }
            else if (p.instrument == 2)
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
            grand[static_cast<std::size_t> (i)].noteOff();
            clav[static_cast<std::size_t> (i)].noteOff();
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
            setPedalAmount (0.0);
            for (auto& v : tines) v.noteOff();
            for (auto& v : cp70)  v.noteOff();
            for (auto& v : wurli) v.noteOff();
            for (auto& v : grand) v.noteOff();
            for (auto& v : clav)  v.noteOff();
            break;

        case NoteEvent::sustainOn:  setPedalAmount (1.0); break;
        case NoteEvent::sustainOff: setPedalAmount (0.0); break;
        case NoteEvent::sustain:
            setPedalAmount (static_cast<double> (e.velocity));
            break;

        case NoteEvent::sostenuto:
        {
            // The piano rule: only the keys down at the moment the pedal
            // falls are caught; releasing it frees them all.
            const bool down = e.velocity > 0.5f;
            for (int i = 0; i < kNumTines; ++i)
                grand[static_cast<std::size_t> (i)].setSostenuto (down && keyDown[i]);
            break;
        }

        case NoteEvent::soft:
            unaCorda = e.velocity > 0.5f;
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
    // Switching the instrument mid-stream cuts between three unrelated
    // signal chains -- different levels, different DC states -- and the
    // seam is a click. So the switch goes through silence: fade the
    // running instrument out over a few milliseconds, clear its voices at
    // the bottom (so a switch back does not resurrect stale tails), then
    // fade the new one in.
    if (activeInst < 0)
        activeInst = p.instrument;   // first block: no fade, nothing to fade from

    // Park note events for the incoming instrument while the outgoing one
    // drains. Global events (pedal, all-notes-off) pass through -- they act
    // on every bank at once.
    if (p.instrument != activeInst && events != nullptr && numEvents > 0)
    {
        for (int i = 0; i < numEvents; ++i)
            if ((events[i].type == NoteEvent::noteOn || events[i].type == NoteEvent::noteOff)
                 && numPending < kMaxPending)
            {
                pendingNotes[numPending] = events[i];
                pendingNotes[numPending].offset = 0;
                ++numPending;
            }
    }

    if (p.instrument != activeInst && instGain <= 1.0e-3)
    {
        if (activeInst == 0) { for (auto& v : tines) v.reset(); harp.reset(); }
        if (activeInst == 1) { for (auto& v : cp70)  v.reset(); cp70Frame.reset(); }
        if (activeInst == 2) { for (auto& v : wurli) v.reset(); wurliFrame.reset(); }
        if (activeInst == 3)
        {
            for (auto& v : grand) v.reset();
            grandBoard.prepare (fs);
            grandRad.prepare (fs);
            grandMics.prepare (fs);
        }
        if (activeInst == 4)
        {
            for (auto& v : clav) v.reset();
            clavTone.prepare (fs * ClavinetVoice::kOver);
            clavPre.prepare (fs * ClavinetVoice::kOver);
            clavKnock.reset();
        }
        // The chain stages freeze mid-signal when their instrument stops
        // running -- the coil was measured thawing a two-second-old ring as
        // a half-scale burst on the way back in. Everything stateful between
        // the voices and the shared effects gets cleared while the output is
        // provably at silence. The room stays: it runs in every path, so its
        // tail is continuous and real.
        coil.reset(); preamp.reset();
        cabinetL.reset(); cabinetR.reset(); cabinetBL.reset(); cabinetBR.reset();
        cp70Preamp.reset();
        wurliBus.reset(); wurliPre.reset(); wurliTrem.reset();
        vibrato.reset();
        decimL.reset(); decimR.reset();
        phaserL.reset(); phaserR.reset();
        airL.reset(); airR.reset();
        activeInst = p.instrument;
    }
    EngineParams pa = p;
    pa.instrument = activeInst;
    if (p.instrument == activeInst && numPending > 0)
    {
        for (int i = 0; i < numPending; ++i)
            handleEvent (pendingNotes[i], pa);
        numPending = 0;
    }
    processActive (outL, outR, numSamples, pa, events, numEvents);
    const double target = (p.instrument == activeInst) ? 1.0 : 0.0;
    const double aRamp = 1.0 - std::exp (-1.0 / (0.0015 * fs));
    // The output rail: transparent below 0.85, bounded at 1.0. Every real
    // output stage has a supply ceiling; without one here, a fortissimo
    // bass bark -- rail-limited inside the Wurlitzer preamp exactly as the
    // circuit says -- gets carried past digital full scale by the loudness
    // trims and hard-clips in the host, which is nobody's circuit. The knee
    // starts far above every calibrated level, so it only touches the
    // extremes it exists for.
    auto rail = [] (float x) -> float
    {
        constexpr float a = 0.85f, s = 0.15f;
        const float ax = std::fabs (x);
        if (ax <= a) return x;
        const float y = a + s * std::tanh ((ax - a) / s);
        return x < 0.0f ? -y : y;
    };
    for (int n = 0; n < numSamples; ++n)
    {
        instGain += (target - instGain) * aRamp;
        const float g = static_cast<float> (instGain);
        outL[n] = rail (outL[n] * g);
        outR[n] = rail (outR[n] * g);
    }
}

void EpiEngine::processActive (float* outL, float* outR, int numSamples,
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
    if (p.instrument == 3)
    {
        processGrand (outL, outR, numSamples, p, events, numEvents);
        return;
    }
    if (p.instrument == 4)
    {
        processClav (outL, outR, numSamples, p, events, numEvents);
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
        // Tone gains move slower than the general smoothing: a gain-blend
        // step is an amplitude discontinuity, and at the 40 ms constant a
        // fast treble sweep still zipped at measurable level.
        sm.step (sm.bass, p.bassDb, k * 0.35f);
        sm.step (sm.treb, p.trebleDb, k * 0.35f);
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
    harp.setBody (p.bodyMat, p.bodySize);
    action.setBed (p.keyBed);
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


void EpiEngine::processGrand (float* outL, float* outR, int numSamples,
                              const EngineParams& p,
                              const NoteEvent* events, int numEvents)
{
    const auto cfg = grandConfig (p);
    if (std::memcmp (&cfg, &lastGrandCfg, sizeof cfg) != 0)
    {
        lastGrandCfg = cfg;
        ++cfgVersion;
    }
    // The board's coupling scalar (BODY knob, neutral at the default
    // quarter) plus the body bench: what the board is made of and how big
    // it is. The radiator's modal tail stands in for the same board above
    // its band edge, so it follows the same frequency scale and added loss.
    grandBoard.configure ({ std::pow (4.0, static_cast<double> (std::clamp (p.bodyMix, 0.0f, 1.0f))) * 0.707,
                            static_cast<double> (p.bodyMat),
                            static_cast<double> (p.bodySize) });
    grandRad.setBody (grandBoard.bodyFreqScale(), grandBoard.bodyEtaAdd());

    // Rebuilds, sounding first, bounded -- a grand voice is the heaviest
    // rebuild in the plugin (up to ~130 modes over three strings).
    {
        int budget = 6;
        for (int pass = 0; pass < 2 && budget > 0; ++pass)
            for (int i = 0; i < kNumTines && budget > 0; ++i)
            {
                if (grandCfgVersion[static_cast<std::size_t> (i)] == cfgVersion) continue;
                const bool live = grand[static_cast<std::size_t> (i)].isRinging() || pedalDown || keyDown[i];
                if (pass == 0 && ! live) continue;
                grand[static_cast<std::size_t> (i)].setNote (kLoNote + i, cfg, grandBoard);
                grandCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
                --budget;
            }
    }

    {
        const float k = smoothK (numSamples);
        sm.step (sm.air, p.clarityDb, k);
        airL.set (sm.air, fs);
        airR.set (sm.air, fs);
    }
    phaserL.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    phaserR.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    if (std::abs (p.spaceSize - lastSpaceSize) > 1.0e-4f)
    {
        lastSpaceSize = p.spaceSize;
        room.setSize (p.spaceSize);
    }

    const float gain1 = p.outGainLin * kTrimGrand;
    if (sm.gain < -0.5f) sm.gain = gain1;
    const float gain0 = sm.gain;
    sm.gain = gain1;

    if (micDirty.exchange (false, std::memory_order_acq_rel))
    {
        const double sp = std::clamp (static_cast<double> (micSpread.load (std::memory_order_relaxed)), 0.25, 2.0);
        const double bi = std::clamp (static_cast<double> (micBias.load (std::memory_order_relaxed)), -1.0, 1.0);
        for (int i = 0; i < kNumTines; ++i)
        {
            // The calibrated ILD line, scaled by the pair's spread and walked
            // by the balance -- the same law panGains encodes at sp=1, bi=0.
            const double t = static_cast<double> (i) / 87.0;
            const double ildDb = std::clamp ((5.6 - 17.6 * t) * sp, -7.0 * sp, 7.0 * sp)
                               + 5.0 * bi;
            const double g = std::pow (10.0, std::clamp (ildDb, -12.0, 12.0) / 40.0);
            grandPanL[static_cast<std::size_t> (i)] = g;
            grandPanR[static_cast<std::size_t> (i)] = 1.0 / g;
        }
        grandMics.setSpread (sp);
        micGL = std::clamp (static_cast<double> (micLvlL.load (std::memory_order_relaxed)), 0.25, 2.0);
        micGR = std::clamp (static_cast<double> (micLvlR.load (std::memory_order_relaxed)), 0.25, 2.0);
        micLidAmt = std::clamp (static_cast<double> (micDist.load (std::memory_order_relaxed)), 0.0, 1.0);
    }

    // Sympathetic life, the grand way: with the pedal lifted the undamped
    // strings listen to the shared board through their coupled prefix --
    // openSympathetic IS that physics (the in-loop board stops at 1.3 kHz,
    // so the reduced set is what the coupling band can deliver). Woken once
    // per block, when the board actually carries something.
    if (pedalDown
        && std::abs (grandBoard.outputL()) + std::abs (grandBoard.outputR()) > 1.0e-9)
    {
        for (int i = 0; i < kNumTines; ++i)
            if (! grand[static_cast<std::size_t> (i)].isRinging())
                grand[static_cast<std::size_t> (i)].openSympathetic (kLoNote + i, cfg, grandBoard);
    }

    float pL = 0.0f, pR = 0.0f;
    int nextEvent = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        while (nextEvent < numEvents && events[nextEvent].offset <= n)
            handleEvent (events[nextEvent++], p);

        int active = 0;
        for (int i = 0; i < kNumTines; ++i)
        {
            auto& v = grand[static_cast<std::size_t> (i)];
            if (! v.isRinging()) continue;
            // The calibrated render order from the grand suite: termination
            // force plus the action knock into the radiator; the board is
            // driven inside the voice.
            const double f = v.process (cfg, grandBoard) + v.knockOut();
            v.applyDamperIfDue();
            grandRad.push (f, grandPanL[static_cast<std::size_t> (i)],
                              grandPanR[static_cast<std::size_t> (i)]);
            ++active;
            const float amp = static_cast<float> (std::abs (f)) * 0.02f;
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
        }
        grandBoard.tick();
        double tl = 0.0, tr = 0.0;
        grandRad.tick (tl, tr);
        double l = grandBoard.outputL() + tl;
        double r = grandBoard.outputR() + tr;
        grandMics.tick (l, r);
        // The lid shadow: past the rim the board's high band falls off-axis.
        // A one-pole at 4 kHz crossfaded by distance -- about -6 dB at the
        // top of the range, which is the lid's own geometry, not a tone
        // control.
        if (micLidAmt > 0.0)
        {
            const double a = 1.0 - std::exp (-2.0 * kPiD * 4000.0 / fs);
            micLidL += (l - micLidL) * a;
            micLidR += (r - micLidR) * a;
            l += micLidAmt * (micLidL - l);
            r += micLidAmt * (micLidR - r);
        }
        l *= micGL;
        r *= micGR;

        if (p.clarityDb != 0.0f || sm.air != 0.0f)
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
            for (auto& v : grand) v.reset();
            grandBoard.prepare (fs);
            grandRad.prepare (fs);
            grandMics.prepare (fs);
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


void EpiEngine::processClav (float* outL, float* outR, int numSamples,
                             const EngineParams& p,
                             const NoteEvent* events, int numEvents)
{
    const auto cfg = clavConfig (p);
    if (std::memcmp (&cfg, &lastClavCfg, sizeof cfg) != 0)
    {
        lastClavCfg = cfg;
        ++cfgVersion;
    }
    {
        int budget = 8;
        for (int pass = 0; pass < 2 && budget > 0; ++pass)
            for (int i = 0; i < kNumTines && budget > 0; ++i)
            {
                if (clavCfgVersion[static_cast<std::size_t> (i)] == cfgVersion) continue;
                const bool live = clav[static_cast<std::size_t> (i)].isRinging() || keyDown[i];
                if (pass == 0 && ! live) continue;
                clav[static_cast<std::size_t> (i)].setNote (kLoNote + i, cfg);
                clavCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
                --budget;
            }
    }

    {
        const float k = smoothK (numSamples);
        sm.step (sm.drive, p.preampDrive, k);
        sm.step (sm.air, p.clarityDb, k);
        airL.set (sm.air, fs);
        airR.set (sm.air, fs);
    }
    clavTone.setRockers (p.clavSoft, p.clavMed, p.clavTreb, p.clavBrill);
    // Fitted through (0.30, 1.0) and (1.0, 4.0): the default knob sits at
    // the circuit's own calibrated level, full is four times it.
    clavPre.setDrive (0.552 * std::pow (7.25, static_cast<double> (std::clamp (sm.drive, 0.0f, 1.0f))));
    phaserL.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    phaserR.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    if (std::abs (p.spaceSize - lastSpaceSize) > 1.0e-4f)
    {
        lastSpaceSize = p.spaceSize;
        room.setSize (p.spaceSize);
    }

    const float gain1 = p.outGainLin * kTrimClav;
    if (sm.gain < -0.5f) sm.gain = gain1;
    const float gain0 = sm.gain;
    sm.gain = gain1;

    float pL = 0.0f, pR = 0.0f;
    int nextEvent = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        while (nextEvent < numEvents && events[nextEvent].offset <= n)
            handleEvent (events[nextEvent++], p);

        // All sixty gaps hang on one pickup bus, so superposition at the bus
        // is exact and intermodulation belongs to the preamp -- the same
        // discipline as the reed bar.
        double os[ClavinetVoice::kOver] = {};
        double vbuf[ClavinetVoice::kOver];
        int active = 0;
        for (int i = 0; i < kNumTines; ++i)
        {
            auto& v = clav[static_cast<std::size_t> (i)];
            if (! v.isRinging()) continue;
            v.process (cfg, vbuf);
            for (int k = 0; k < ClavinetVoice::kOver; ++k) os[k] += vbuf[k];
            v.applyDamperIfDue();
            ++active;
            const float amp = static_cast<float> (std::abs (v.centerDisplacement()));
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
        }
        // The knock reaches the pickups acoustically through the frame, not
        // through the string clamp: onto the bus pre tone stack (held across
        // the oversampled frames -- it lives below 1.2 kHz). Stack and
        // preamp run per oversampled frame, as calibrated, so the
        // saturation's harmonics exist before the decimator, not folded
        // under it.
        const double kn = clavKnock.process() * p.strikeNoise;
        for (int k = 0; k < ClavinetVoice::kOver; ++k)
            os[k] = clavPre.process (clavTone.process (os[k] + kn));
        double y = decimL.process (os);

        double l = y, r = y;
        if (p.clarityDb != 0.0f || sm.air != 0.0f)
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
            for (auto& v : clav) v.reset();
            clavTone.prepare (fs);
            clavPre.prepare (fs);
            decimL.reset();
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
        // Tone gains move slower than the general smoothing: a gain-blend
        // step is an amplitude discontinuity, and at the 40 ms constant a
        // fast treble sweep still zipped at measurable level.
        sm.step (sm.bass, p.bassDb, k * 0.35f);
        sm.step (sm.treb, p.trebleDb, k * 0.35f);
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
    cp70Frame.setBody (p.bodyMat, p.bodySize);
    action.setBed (p.keyBed);
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

        // The frame: every string terminates on one bridge on one frame, and
        // a weak spring through it is the sympathetic path. At full BODY a
        // struck C4 rings the octave string 19 dB down, the twelfth 26 dB
        // down, and a non-coincident string 38 dB down -- the partial-
        // coincidence hierarchy of a real harp, falling out of the string
        // tuning rather than being placed by hand. Linearity keeps the
        // superposition row exact whatever the strength.
        const double frameU = cp70Frame.displacement();
        const double frameK = 25000.0 * std::clamp (p.bodyMix, 0.0f, 1.0f);
        double frameReaction = 0.0;

        double bus = 0.0;
        int active = 0;
        for (int i = 0; i < kNumTines; ++i)
        {
            auto& v = cp70[i];
            const bool free = pedalDown || keyDown[i];
            const bool live = v.isRinging()
                           || (free && frameK > 0.0 && std::abs (frameU) > 1.0e-12);
            if (! live) continue;

            if (frameK > 0.0 && free)
            {
                // FEEDFORWARD, cleanly split: struck strings only PUSH the
                // frame, sympathetic strings only RECEIVE from it, and no
                // voice ever sits on both sides of the spring. String Q here
                // runs to tens of seconds, and any loop through the explicit
                // one-sample lag pumps exactly those modes -- the grand
                // plan's own probe rejected the two-way form, and its named
                // retreat (Bank's structurally stable feedforward) is what
                // this is.
                if (v.isStruckVoice())
                {
                    double f = frameK * v.clampDisplacement();
                    if (! std::isfinite (f)) f = 0.0;
                    frameReaction += f;
                }
                else
                {
                    v.addClampForce (frameK * frameU);
                    v.wakeSympathetic();
                }
            }

            bus += v.process (cfg);
            v.applyDamperIfDue();
            ++active;
            const float amp = static_cast<float> (std::abs (v.tipDisplacement()));
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
        }
        cp70Frame.addForce (frameReaction);
        cp70Frame.tick();
        if (! std::isfinite (cp70Frame.displacement())) cp70Frame.reset();

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
        // Tone gains move slower than the general smoothing: a gain-blend
        // step is an amplitude discontinuity, and at the 40 ms constant a
        // fast treble sweep still zipped at measurable level.
        sm.step (sm.bass, p.bassDb, k * 0.35f);
        sm.step (sm.treb, p.trebleDb, k * 0.35f);
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
    wurliFrame.setBody (p.bodyMat, p.bodySize);
    action.setBed (p.keyBed);
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

        // The reed bar as the sympathetic path: same passive spring as the
        // harp and the string frame. Reeds are inharmonic cantilevers, so
        // there is no partial-coincidence ladder here -- undamped neighbours
        // wake by bar-mode proximity instead, 24 to 31 dB under the struck
        // reed. A heavy casting keeps its wash subtler than the piano's.
        const double barU = wurliFrame.displacement();
        const double barK = 50000.0 * std::clamp (p.bodyMix, 0.0f, 1.0f);
        double barReaction = 0.0;

        double dcBus[WurliVoice::kOver] = {};
        double dc[WurliVoice::kOver];
        int active = 0;
        for (int i = 0; i < kNumTines; ++i)
        {
            auto& v = wurli[static_cast<std::size_t> (i)];
            const bool free = pedalDown || keyDown[i];
            const bool live = v.isRinging()
                           || (free && barK > 0.0 && std::abs (barU) > 1.0e-12);
            if (! live) continue;

            if (barK > 0.0 && free)
            {
                // Same feedforward split as the string frame.
                if (v.isStruckVoice())
                {
                    double f = barK * v.clampDisplacement();
                    if (! std::isfinite (f)) f = 0.0;
                    barReaction += f;
                }
                else
                {
                    v.addClampForce (barK * barU);
                    v.wakeSympathetic();
                }
            }

            v.process (cfg, dc);
            for (int k = 0; k < WurliVoice::kOver; ++k) dcBus[k] += dc[k];
            ++active;
            const float amp = static_cast<float> (std::abs (v.tipDisplacement()));
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
        }
        wurliFrame.addForce (barReaction);
        wurliFrame.tick();
        if (! std::isfinite (wurliFrame.displacement())) wurliFrame.reset();

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
