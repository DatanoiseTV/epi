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
// convention. It used to be an RMS convention: the same mezzo-forte chord
// measuring the same RMS through each, benched at -18 dBFS. That is not
// achievable and never was, and the rail was hiding it. These instruments
// do not share a crest factor -- measured on the bank's own phrase, the
// reed and the grand run 22 to 24.5 dB between RMS and peak while the
// tine, the electric grand and the clav run 14.6 to 16.3 -- so matching
// RMS necessarily puts the high-crest pair six to nine decibels into the
// output rail. It did: forty-three presets were touching the rail and the
// reed bank sat 13 dB inside it, soft-clipping every bark it played, at
// mezzo-forte, with 1.3 percent of all samples limited.
//
// So the bench is on PEAKS now, which is what a recording engineer would
// have done in the first place: each instrument is trimmed so its own
// loudest presets land just under the rail's knee, and the loudness that
// results is whatever its crest factor says it should be. A reed reads
// quieter on a meter than a tine and always did -- the difference was
// being spent on the limiter instead of being shown. Nothing is louder
// than it was; several things are honest for the first time.
//
// The trims sit at the very end of each path, after every nonlinearity,
// so no operating point moves, only the meter.
static constexpr float kTrimRhodes = 1.0485f;   // -2.0 dB
static constexpr float kTrimCP70   = 4.1834f;   // -5.5 dB
static constexpr float kTrimWurli  = 2.6968f;   // -13.5 dB, the reed's 22 dB crest
static constexpr float kTrimGrand  = 56.376f;   // -8.5 dB, the grand's 24 dB crest
// The action layer's force into the grand's frame. Calibrated by the
// engine row 12.4: a mezzo-forte key press sits far under the note it
// belongs to, and is plainly there when the note is not.
static constexpr double kGrandActionGain = 0.6;
static constexpr float kTrimClav   = 0.2190f;  // -2.5 dB, on the same peak bench

// Per-note tuning folded into the configuration a voice is BUILT to.
//
// It goes exactly where the workshop's length trim goes -- into the geometry
// solve, through the same detuneCents the TUNE knob and the wheel already
// travel on -- so the tuner's offset and the modder's shorter tine compose
// instead of fighting: cents add, and the length trim's own 1/L^2 retune is
// applied first by setGeometryTrim. At zero the configuration is returned
// untouched, which is what keeps an instrument nobody has retuned bit-exact.
template <typename Cfg>
static Cfg withNoteTune (Cfg c, float cents)
{
    if (cents != 0.0f) c.detuneCents += static_cast<double> (cents);
    return c;
}

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
    // prepare() leaves the plate at stock, so the cache that decides
    // whether the body bench needs re-applying has to forget what it
    // thought was current -- otherwise the next block compares the
    // player's setting against itself, finds no change, and the bench
    // is silently lost. Same for the mic stage, whose allpass cascade
    // prepare() rebuilds at unity spread while the level line survives.
    lastBoardCfg = GrandBoard::Config { -1.0e30 };
    micDirty.store (true, std::memory_order_release);
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
    // ...and the host's two continuous controllers, for the same reason
    // reset() returns them: a prepared instrument has to be the instrument a
    // freshly constructed one is, and rows 16.6x say so.
    expression = 1.0f;
    bendSemis = 0.0f;
}

void EpiEngine::reset()
{
    for (auto& v : tines) v.reset();
    for (auto& v : cp70) { v.reset(); }
    // The reed bank, which this forgot: every other bank was reset here and
    // the Wurlitzer's was not, so a reed caught mid-ring kept ringing
    // through a host's reset and arrived in the next render as an
    // uninvited note (measured -3.8 dB against its own attack, and 8.6 dB
    // of variation between supposedly identical strikes afterwards).
    for (auto& v : wurli) { v.reset(); }
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
    // prepare() leaves the plate at stock, so the cache that decides
    // whether the body bench needs re-applying has to forget what it
    // thought was current -- otherwise the next block compares the
    // player's setting against itself, finds no change, and the bench
    // is silently lost. Same for the mic stage, whose allpass cascade
    // prepare() rebuilds at unity spread while the level line survives.
    lastBoardCfg = GrandBoard::Config { -1.0e30 };
    micDirty.store (true, std::memory_order_release);
    grandRad.prepare (fs);
    grandMics.prepare (fs);
    for (auto& v : clav) v.reset();
    clavTone.prepare (fs * ClavinetVoice::kOver);
    clavPre.prepare (fs * ClavinetVoice::kOver);
    clavKnock.reset();
    keyDown.fill (false);
    // Per-note tuning is the host's state, not the instrument's: a reset
    // returns every string to what the TUNE knob alone says, and marks the
    // ones that were carrying an offset for a re-cut so none is left tuned to
    // something nothing is asking for any more. Strings at nominal are not
    // touched, so a reset on an instrument nobody retuned does nothing.
    for (int i = 0; i < kNumTines; ++i)
        if (noteCents[static_cast<std::size_t> (i)] != 0.0f)
        {
            noteCents[static_cast<std::size_t> (i)] = 0.0f;
            invalidateNoteBuild (i);
        }
    harp.reset();
    action.reset();
    room.reset();
    // The two free-running generators, which this left where they were.
    // Every strike takes its per-note randomisation from `seed`, and the
    // action layer walks `noiseRng` continuously, so a reset that did not
    // return them left the NEXT strike drawing from a different point in
    // the sequence: the instrument did not repeat across a reset, which is
    // precisely what a bounce is supposed to be. Restored to the values a
    // freshly constructed engine starts on.
    seed = 0x2545f491u;
    noiseRng = Rng { 0x51ed270bu };
    // And the block-rate smoothers, which carry the previous session's
    // drive, tone and gain across a reset and start the next note gliding
    // from wherever the last one left them. Returned to their sentinel so
    // the first block after a reset snaps to its parameters, exactly as the
    // first block after construction does.
    sm = SmoothedParams {};
    // And the tine bank's own record of the configuration it was last cut
    // to, which prepare() leaves as a sentinel so the first block re-cuts
    // all eighty-eight and reset() did not. Without it the first block
    // after a reset compares the player's parameters against the ones
    // still standing from before it, finds no change, and leaves the bank
    // holding whatever the tines were carrying -- so the instrument did
    // not repeat across a reset, which is exactly what a bounce is.
    lastCfg = RhodesVoice::Config {};
    lastCfg.hammerHardness = -1.0e30;
    // ...and the five banks' records of which version they were last cut
    // at, which prepare() zeroes and this did not, so half the staleness
    // bookkeeping came back from a reset holding the previous session's
    // numbers while the other half started from zero.
    cfgVersion = 0;
    tineCfgVersion.fill (0);
    cp70CfgVersion.fill (0);
    wurliCfgVersion.fill (0);
    grandCfgVersion.fill (0);
    clavCfgVersion.fill (0);
    // ...and the two single-value caches, for the same reason and with the
    // same failure mode: a value the engine believes it has already handed
    // out, against a bank that has just been returned to its built state,
    // is a setting silently lost until something else moves it. prepare()
    // has always invalidated these; reset() did not, and the moment the
    // voices started returning their transduction to rest it showed up as
    // the core saturation going missing after every reset.
    lastCoilSat = -1.0f;
    lastSpaceSize = -1.0f;
    pedalDown = false;
    pedalAmount = 0.0;
    softAmount = 0.0;
    sostenutoDown = false;
    // The two continuous controllers, which this left where the last
    // session put them while clearing every other one beside them. They are
    // the host's state, not the instrument's: a reset means the host is
    // discarding what it had, and a bounce that inherits the previous take's
    // swell pedal or wheel is not a bounce. Measured before this: CC11 left
    // at its bottom made the next strike 26.9 dB quiet against a fresh
    // engine, and a wheel left at +2 semitones brought the whole instrument
    // back a whole tone sharp -- 199.8 cents, on a keyboard that has no
    // wheel of its own.
    expression = 1.0f;
    bendSemis = 0.0f;
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
    c.hammerMat  = static_cast<double> (p.hammerMat);
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
    c.hammerMat     = static_cast<double> (p.hammerMat);
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
    c.hammerMat  = static_cast<double> (p.hammerMat);
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
    c.hammerMat      = static_cast<double> (p.hammerMat);
    // One pedal, one state, and the MODE decides which mechanism it drives
    // -- read here, every block, for both of them. The shift used to be
    // latched at the CC67 event instead, against whatever SOFT MODE said at
    // that instant, and the two then disagreed for as long as the pedal
    // stayed down: switching rail -> shift under a held pedal left NEITHER
    // engaged and the left pedal did nothing at all (measured identical to
    // no pedal, peak 0.407 against the 0.267 the shift owes), while shift
    // -> rail left BOTH engaged at once and took the note 2 dB below either
    // mechanism alone. A pedal that is down is down; the switch above it
    // chooses what it is connected to.
    c.unaCorda       = p.softMode == 0 && softAmount > 0.5;
    c.halfBlow       = p.softMode == 1 ? softAmount : 0.0;
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
    // The case: the shared BODY knob is the case-sense level (0.25 default
    // lands on the calibrated 1.0), and the body bench re-makes the box.
    c.caseAmount     = std::clamp (4.0 * static_cast<double> (p.bodyMix), 0.0, 2.0);
    c.caseBodyMat    = static_cast<double> (p.bodyMat);
    c.caseBodySize   = static_cast<double> (p.bodySize);
    c.wearAmount     = static_cast<double> (p.wearAmount);
    // The panel's KEY NOISE reaches this instrument as the key-bottom
    // thump into the case; the shared default 0.3 is the calibrated 1.0.
    c.keyNoise       = std::clamp (static_cast<double> (p.strikeNoise) / 0.3, 0.0, 2.0);
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

            // Velocity curve shape. Linear sits at about 0.34 on the knob
            // (shape = 1.0); the 0.5 default gives shape 1.30, which stacked
            // on the launch law's own exponent is the measured-piano response
            // (velocity-curves-measured.md) -- below it a light touch gives
            // more, above it the player works for the top.
            const float shape = 0.35f + 1.9f * std::clamp (p.velCurve, 0.0f, 1.0f);
            const float vel = std::pow (velMapEval (std::clamp (e.velocity, 0.0f, 1.0f)), shape)
                            * std::clamp (expression, 0.0f, 2.0f);

            keyDown[i] = true;
            // The tuner's instruction for this string, latched at the strike.
            // Only here: a change on the way in retunes the string BEFORE the
            // hammer lands, which is a tuning pin turned between two notes;
            // there is no path by which a later change reaches a string that
            // is already ringing, because nothing else reads this field. The
            // banks that are not sounding go stale with it, so switching
            // instrument later finds the same tuning rather than nominal.
            if (noteCents[static_cast<std::size_t> (i)] != e.tuneCents)
            {
                noteCents[static_cast<std::size_t> (i)] = e.tuneCents;
                invalidateNoteBuild (i);
            }

            // A tine that has been waiting its turn is built before it is
            // hit -- and only when the tine piano is the one being played,
            // which is what every other bank below already does.
            //
            // Unconditional, this was a way to cut one tine permanently to a
            // configuration the panel does not show. The version counter is
            // shared by the five banks, so a voice marked stale for a reason
            // the tine path did not cause -- a workshop edit, a per-note
            // tuning change -- would be re-cut by a note struck on ANOTHER
            // instrument, using whatever the tine-only controls happened to
            // read at that moment, and then marked current at the shared
            // version. Put those controls back and nothing notices: the
            // tine path's own record still holds the original value, the
            // whole-struct comparison finds no change, the version never
            // advances, and that one voice is never re-cut. Measured, it
            // rendered bit-identically to an instrument whose PICKUP POS had
            // been left at the away value. Gated, a stale voice simply stays
            // stale while another instrument is playing, and the idle sweep
            // in the tine path -- which is already gated the same way --
            // picks it up on the way back.
            if (p.instrument == 0
                && (tineCfgVersion[i] != cfgVersion
                    || tineMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire)))
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
                    rebuildClav (i, clavConfig (p));
                    clavCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
                }
                clav[static_cast<std::size_t> (i)].noteOn (
                    e.note, vel,
                    withNoteTune (clavConfig (p), noteCents[static_cast<std::size_t> (i)]), seed);
                clavKnock.strike (i, vel);
            }
            else if (p.instrument == 3)
            {
                if (grandCfgVersion[static_cast<std::size_t> (i)] != cfgVersion
                    || grandMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire))
                {
                    rebuildGrandString (i, grandConfig (p));
                    grandCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
                }
                grand[static_cast<std::size_t> (i)].noteOn (
                    e.note, vel,
                    withNoteTune (grandConfig (p), noteCents[static_cast<std::size_t> (i)]),
                    grandBoard, seed);
            }
            else if (p.instrument == 2)
            {
                if (wurliCfgVersion[static_cast<std::size_t> (i)] != cfgVersion)
                {
                    rebuildWurli (i, wurliConfig (p));
                    wurliCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
                }
                wurli[static_cast<std::size_t> (i)].noteOn (
                    e.note, vel,
                    withNoteTune (wurliConfig (p), noteCents[static_cast<std::size_t> (i)]), seed);
            }
            else if (p.instrument == 1)
            {
                if (cp70CfgVersion[i] != cfgVersion
                    || stringMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire))
                {
                    rebuildString (i, cp70Config (p));
                    cp70CfgVersion[i] = cfgVersion;
                }
                cp70[i].noteOn (e.note, vel,
                                withNoteTune (cp70Config (p), noteCents[static_cast<std::size_t> (i)]), seed);
            }
            else
                tines[i].noteOn (e.note, vel,
                                 withNoteTune (rhodesConfig (p), noteCents[static_cast<std::size_t> (i)]), seed);
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
            // FALLS are caught; releasing it frees them all.
            //
            // On the edge, not on the message. A host repeats controller
            // state -- on transport start, on a punch-in, from any
            // controller that sends its position continuously -- and acting
            // on every CC66 that says "down" re-ran the catch against
            // whatever was held right then. Measured: a note struck after
            // the pedal was already down, followed by one repeated CC66,
            // came out 53 dB up on the same note unpedalled, which is a
            // note hanging until the player thinks to release a pedal they
            // are already holding. The tabs are under the lifted levers
            // once; they cannot take another bite without dropping first.
            const bool down = e.velocity > 0.5f;
            if (down == sostenutoDown) break;
            sostenutoDown = down;
            for (int i = 0; i < kNumTines; ++i)
                grand[static_cast<std::size_t> (i)].setSostenuto (down && keyDown[i]);
            break;
        }

        case NoteEvent::soft:
            // The left pedal has two real mechanisms; the SOFT MODE choice
            // picks which one CC67 drives. Shift moves the action sideways
            // (near-binary, as the real shift is); the rail tilts the
            // hammers closer -- continuous, an upright's half-blow.
            softAmount = std::clamp (static_cast<double> (e.velocity), 0.0, 1.0);
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
    tines[static_cast<std::size_t> (i)].setNote (kLoNote + i,
                                                 withNoteTune (cfg, noteCents[static_cast<std::size_t> (i)]));
}

// The Wurlitzer's and the Clavinet's banks have no workshop bench of their
// own, so these two exist only to give per-note tuning the single fold point
// the other three already have. Every path that re-cuts one of those voices
// goes through here, which is what keeps a background rebuild from quietly
// putting a retuned string back to nominal.
void EpiEngine::rebuildWurli (int i, const WurliVoice::Config& cfg)
{
    wurli[static_cast<std::size_t> (i)].setNote (kLoNote + i,
                                                 withNoteTune (cfg, noteCents[static_cast<std::size_t> (i)]));
}

void EpiEngine::rebuildClav (int i, const ClavinetVoice::Config& cfg)
{
    clav[static_cast<std::size_t> (i)].setNote (kLoNote + i,
                                                withNoteTune (cfg, noteCents[static_cast<std::size_t> (i)]));
}

// A note's tuning has moved, so every bank's copy of that note is stale.
// cfgVersion is only ever compared for equality, so any other value means
// "rebuild me": the sounding bank picks it up on the way into this very
// strike, and the four that are not sounding pick it up through their own
// priority loop if they are ever switched to.
void EpiEngine::invalidateNoteBuild (int i)
{
    const std::uint32_t stale = cfgVersion - 1u;
    const auto u = static_cast<std::size_t> (i);
    tineCfgVersion[u]  = stale;
    cp70CfgVersion[u]  = stale;
    wurliCfgVersion[u] = stale;
    grandCfgVersion[u] = stale;
    clavCfgVersion[u]  = stale;
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
            // prepare() leaves the plate at stock, so the cache that decides
            // whether the body bench needs re-applying has to forget what it
            // thought was current -- otherwise the next block compares the
            // player's setting against itself, finds no change, and the bench
            // is silently lost. Same for the mic stage, whose allpass cascade
            // prepare() rebuilds at unity spread while the level line survives.
            lastBoardCfg = GrandBoard::Config { -1.0e30 };
            micDirty.store (true, std::memory_order_release);
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
        switchLanded = true;
    }
    EngineParams pa = p;
    pa.instrument = activeInst;
    if (p.instrument == activeInst && numPending > 0)
    {
        // Parked notes belong to the instrument that was being switched TO,
        // and they are replayed only if that switch actually landed.
        //
        // They were also played live on the way past: processActive gets the
        // same event array every block, so a note struck during the fade
        // already sounded on the outgoing bank. That is correct while the
        // switch is going to complete -- the outgoing bank is on its way to
        // silence and the parked copy is what the player will hear. It is
        // not correct if the host puts the instrument back before the fade
        // finishes, which is what a mouse dragged across a selector or a
        // transport start does: the switch never happened, the note has
        // already sounded on the bank that is still active, and replaying it
        // strikes the same voice a second time.
        //
        // Measured on a one-block round trip at 128 samples: the note came
        // out 5.5 dB below the same note without the blip, and by a
        // different amount at 64 and 256 -- so the audible result of an
        // automation blip depended on the host's buffer size, which is the
        // one thing a plugin must never let the buffer decide.
        if (switchLanded)
            for (int i = 0; i < numPending; ++i)
                handleEvent (pendingNotes[i], pa);
        numPending = 0;
    }
    switchLanded = false;
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
        // Ceiling at -1 dBFS, not 0: a rail that tops at exactly full scale
        // reads as clipping on a DAW meter and leaves nothing for
        // inter-sample peaks downstream. Same knee shape, one decibel of
        // honest headroom.
        constexpr float a = 0.76f, s = 0.131f;
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
        // A voice that is SOUNDING is not rebuilt. You cannot change the
        // felt on a hammer that has already struck, and you cannot restring
        // a piano while the note is ringing -- so a configuration change
        // applies to the next strike of each voice, not retroactively to
        // the ones in the air. This is also the only way to make it
        // click-free: a rebuild re-solves the geometry and retunes the
        // modes under a sounding note, and a knob swept across a held
        // chord used to fire one of those per block. Measured on the grand,
        // sweeping MATERIAL under a held chord put a second difference of
        // 3.56 against the chord's own 0.018 -- an audible click, roughly
        // two hundred times the signal's own worst step. Idle voices
        // rebuild eagerly, so the change is heard the moment anything new
        // is played, and the note-on path rebuilds a stale voice before it
        // strikes.
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (! stale (i)) continue;
            if (tines[i].isRinging() || keyDown[i]) continue;
            rebuildTine (i, cfg);
            tineCfgVersion[i] = cfgVersion;
            --budget;
        }
    }

    // Recomputing eight decay coefficients and a damping cutoff is cheap, but
    // not per sample, and the size control has no reason to be smoothed: it
    // changes the room, and rooms do not change during a note.
    room.setProfile (p.roomProfile);   // idempotent: early-outs unchanged
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
        // The transducer swap, crossfaded rather than switched. A coil is a
        // resonant filter; taking it out of the path in one sample is a step
        // in both level and phase, and it measured as a click 1447 times the
        // signal's own worst second difference -- the loudest thing in the
        // instrument that a control could do. Nor is it something the
        // instrument can do: a pickup is bench hardware, like the felt and
        // the wire, and every other bench control here reaches the player at
        // the next strike rather than under the note already in the air. A
        // shared output stage has no next strike to wait for, so the honest
        // form of the same statement is the shortest ramp that manufactures
        // no transient of its own.
        sm.step (sm.coilMix, p.transducer <= 1 ? 1.0f : 0.0f, k);
        sm.step (sm.electroMix, (p.transducer == 1 || p.transducer == 2) ? 1.0f : 0.0f, k);
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
            // The coil runs either way, so its state never goes stale: a
            // filter switched back into a live path after sitting out is a
            // transient of its own. The blend is what the player hears.
            const double dry = os[k];
            const double wet = coil.process (static_cast<float> (dry));
            const double mix = static_cast<double> (sm.coilMix);
            double y = mix >= 1.0 ? wet : mix <= 0.0 ? dry : dry + mix * (wet - dry);
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
    // The board itself follows the same rule as the strings on it: a plate
    // is not re-made while it is ringing. Re-solving 72 modes under a live
    // board steps every one of them at once, which is louder than any
    // single string's rebuild -- measured at five times the signal's own
    // worst step when the body bench was swept under a held chord. The
    // pending configuration is applied at the first block where nothing on
    // the grand is sounding, which for a player is the gap between phrases.
    {
        const GrandBoard::Config want { std::pow (4.0, static_cast<double> (std::clamp (p.bodyMix, 0.0f, 1.0f))) * 0.707,
                                        static_cast<double> (p.bodyMat),
                                        static_cast<double> (p.bodySize) };
        if (std::memcmp (&want, &lastBoardCfg, sizeof want) != 0)
        {
            bool quiet = ! pedalDown;
            for (int i = 0; i < kNumTines && quiet; ++i)
                quiet = ! (grand[static_cast<std::size_t> (i)].isRinging() || keyDown[i]);
            if (quiet)
            {
                lastBoardCfg = want;
                grandBoard.configure (want);
                grandRad.setBody (grandBoard.bodyFreqScale(), grandBoard.bodyEtaAdd());
            }
        }
    }

    // Rebuilds, sounding first, bounded -- a grand voice is the heaviest
    // rebuild in the plugin (up to ~130 modes over three strings).
    {
        // A voice that is SOUNDING is not rebuilt. You cannot change the
        // felt on a hammer that has already struck, and you cannot restring
        // a piano while the note is ringing -- so a configuration change
        // applies to the next strike of each voice, not retroactively to
        // the ones in the air. This is also the only way to make it
        // click-free: a rebuild re-solves the geometry and retunes the
        // modes under a sounding note, and a knob swept across a held
        // chord used to fire one of those per block. Measured on the grand,
        // sweeping MATERIAL under a held chord put a second difference of
        // 3.56 against the chord's own 0.018 -- an audible click, roughly
        // two hundred times the signal's own worst step. Idle voices
        // rebuild eagerly, so the change is heard the moment anything new
        // is played, and the note-on path rebuilds a stale voice before it
        // strikes.
        int budget = 6;
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (grandCfgVersion[static_cast<std::size_t> (i)] == cfgVersion
                && ! grandMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire))
                continue;
            if (grand[static_cast<std::size_t> (i)].isRinging() || keyDown[i]) continue;
            rebuildGrandString (i, cfg);
            grandCfgVersion[static_cast<std::size_t> (i)] = cfgVersion;
            --budget;
        }
    }

    {
        const float k = smoothK (numSamples);
        sm.step (sm.air, p.clarityDb, k);
        sm.step (sm.bass, p.bassDb, k * 0.35f);
        sm.step (sm.treb, p.trebleDb, k * 0.35f);
        airL.set (sm.air, fs);
        airR.set (sm.air, fs);
        // The desk the mics feed: a channel-strip shelf pair, not part of
        // the instrument -- same honest framing as the effects. Gain-blend
        // shelves, gliding per block like the electrics' tone.
        grandBassGain = std::pow (10.0, std::clamp (sm.bass, -12.0f, 12.0f) / 20.0) - 1.0;
        grandTrebGain = std::pow (10.0, std::clamp (sm.treb, -12.0f, 12.0f) / 20.0) - 1.0;
    }
    phaserL.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    phaserR.setParams (p.phaserRate, p.phaserDepth, p.phaserFb, p.phaserMix);
    room.setProfile (p.roomProfile);   // idempotent: early-outs unchanged
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

    // The action layer: a grand's key noise is not a detail, it is half of
    // what makes a piano sound like a machine in a room. It enters through
    // the frame (GrandBoard::frameForce), so it inherits the board's own
    // colour and the body bench with it, and the keybed bench sets what the
    // keys land on.
    action.setBed (p.keyBed);

    // Sympathetic life, the grand way: with the pedal lifted the undamped
    // strings listen to the shared board through their coupled prefix --
    // openSympathetic IS that physics (the in-loop board stops at 1.3 kHz,
    // so the reduced set is what the coupling band can deliver). Woken once
    // per block, when the board actually carries something.
    // Gated on the same threshold the damper calls FREE, so the two agree
    // by construction: a string is opened to the board exactly when its own
    // damper has left it, and not before.
    //
    // This was keyed to pedalDown, which is true from a hundredth of a
    // pedal, while the damper does not begin to lift until 0.3 and is not
    // free until 0.7. So the whole harp was opened to the board with every
    // damper still seated -- and opening a string ADDS its coupling load
    // whatever its damping, so the drain into those still-damped strings
    // cost the struck note 16 dB. Measured on C4, a tenth of a pedal came
    // out quieter than no pedal at all, and a player sweeping the pedal
    // heard the note dip and recover. A sustain pedal cannot do that: the
    // half-pedal curve has to be monotone, which is what row P4 now pins.
    //
    // Opening at the knee instead of the touch is not enough on its own --
    // the step just moves to 0.3 -- because the model has no partial
    // coupling: a string is either in the two-port or not. Between the knee
    // and free, where a real damper rides the string, the honest choice is
    // the conservative one.
    if (pedalAmount >= GrandVoice::kPedalFree
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

        // Each key's mechanical noise, summed into the frame. The shared
        // layer already carries the strike/release events for every
        // instrument (handleEvent feeds it), so the grand only had to
        // consume it -- until now the panel's KEY NOISE control moved
        // nothing here at all.
        {
            double noiseForce[kNumTines] = {};
            if (action.tick (p.strikeNoise, noiseRng, noiseForce, kNumTines) > 0)
            {
                double sum = 0.0;
                for (int i = 0; i < kNumTines; ++i) sum += noiseForce[i];
                grandBoard.frameForce (kGrandActionGain * sum);
            }
        }

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
            const float amp = static_cast<float> (std::abs (v.strikeDisplacement()));
            if (amp > tineBlockPeak[i]) tineBlockPeak[i] = amp;
        }
        grandBoard.tick();
        // The mic stage owns the radiator's per-sample readout: in Classic
        // mode its body is exactly the shipped seam (radiator tick, board
        // plus tail per channel, the pair's allpass phase); in Stage mode
        // the same state advance feeds the positioned mics instead.
        double l = 0.0, r = 0.0;
        grandMics.tick (grandRad, grandBoard.outputL(), grandBoard.outputR(), l, r);
        // The desk's shelf pair (see the block above).
        if (grandBassGain != 0.0 || grandTrebGain != 0.0)
        {
            const double aB = 1.0 - std::exp (-2.0 * kPiD * 120.0 / fs);
            const double aT = 1.0 - std::exp (-2.0 * kPiD * 2500.0 / fs);
            deskLoL += (l - deskLoL) * aB; deskLoR += (r - deskLoR) * aB;
            deskHiL += (l - deskHiL) * aT; deskHiR += (r - deskHiR) * aT;
            l += grandBassGain * deskLoL + grandTrebGain * (l - deskHiL);
            r += grandBassGain * deskLoR + grandTrebGain * (r - deskHiR);
        }
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
            // prepare() leaves the plate at stock, so the cache that decides
            // whether the body bench needs re-applying has to forget what it
            // thought was current -- otherwise the next block compares the
            // player's setting against itself, finds no change, and the bench
            // is silently lost. Same for the mic stage, whose allpass cascade
            // prepare() rebuilds at unity spread while the level line survives.
            lastBoardCfg = GrandBoard::Config { -1.0e30 };
            micDirty.store (true, std::memory_order_release);
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
        // Sounding voices are left alone; see the note in the tine bank.
        int budget = 8;
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (clavCfgVersion[static_cast<std::size_t> (i)] == cfgVersion) continue;
            if (clav[static_cast<std::size_t> (i)].isRinging() || keyDown[i]) continue;
            rebuildClav (i, cfg);
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
    room.setProfile (p.roomProfile);   // idempotent: early-outs unchanged
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

// One grand course, built to the configuration and its own bench trims.
void EpiEngine::rebuildGrandString (int i, const GrandVoice::Config& cfg)
{
    auto& m = grandMod[static_cast<std::size_t> (i)];
    grand[static_cast<std::size_t> (i)].setGeometryTrim (m.len.load (std::memory_order_relaxed),
                                                         m.dia.load (std::memory_order_relaxed));
    m.dirty.store (false, std::memory_order_release);
    grand[static_cast<std::size_t> (i)].setNote (kLoNote + i,
                                                withNoteTune (cfg, noteCents[static_cast<std::size_t> (i)]),
                                                grandBoard);
}

// One CP-70 course, built to the configuration and its own steel.
void EpiEngine::rebuildString (int i, const CP70Voice::Config& cfg)
{
    auto& m = stringMod[static_cast<std::size_t> (i)];
    cp70[static_cast<std::size_t> (i)].setGeometryTrim (m.len.load (std::memory_order_relaxed),
                                                        m.dia.load (std::memory_order_relaxed));
    m.dirty.store (false, std::memory_order_release);
    cp70[static_cast<std::size_t> (i)].setNote (kLoNote + i,
                                               withNoteTune (cfg, noteCents[static_cast<std::size_t> (i)]));
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
        // Same rule as the Rhodes: sounding strings are left alone and take
        // the new configuration at their next strike.
        int budget = 12;   // a CP string rebuild is ~40 modes of setMode
        auto stale = [this] (int i)
        {
            return cp70CfgVersion[i] != cfgVersion
                || stringMod[static_cast<std::size_t> (i)].dirty.load (std::memory_order_acquire);
        };
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (! stale (i)) continue;
            if (cp70[i].isRinging() || keyDown[i]) continue;
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
        // The transducer swap, crossfaded rather than switched. A coil is a
        // resonant filter; taking it out of the path in one sample is a step
        // in both level and phase, and it measured as a click 1447 times the
        // signal's own worst second difference -- the loudest thing in the
        // instrument that a control could do. Nor is it something the
        // instrument can do: a pickup is bench hardware, like the felt and
        // the wire, and every other bench control here reaches the player at
        // the next strike rather than under the note already in the air. A
        // shared output stage has no next strike to wait for, so the honest
        // form of the same statement is the shortest ramp that manufactures
        // no transient of its own.
        sm.step (sm.coilMix, p.transducer <= 1 ? 1.0f : 0.0f, k);
        sm.step (sm.electroMix, (p.transducer == 1 || p.transducer == 2) ? 1.0f : 0.0f, k);
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
    room.setProfile (p.roomProfile);   // idempotent: early-outs unchanged
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

        // The mechanism's thump reaches the output directly: on this
        // instrument the piezo sits under the bridge and reads the frame it
        // is bolted to, so key knock arrives without needing the strings to
        // carry it. The gain was measured, not chosen: at the old 2e-5 the
        // whole layer sat 81 dB under the note -- inaudible, which made the
        // panel's KEY NOISE control dead here. It now lands at -52 dB, the
        // same subordinate band the tine (-44) and the grand (-50) measure
        // in, and row 12.4 holds it there.
        if (anyNoise)
        {
            double nsum = 0.0;
            for (int i = 0; i < kNumTines; ++i) nsum += noiseForce[i];
            bus += nsum * 6.0e-4;
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
        // Sounding reeds are left alone; see the tine bank's note.
        int budget = 4;
        for (int i = 0; i < kNumTines && budget > 0; ++i)
        {
            if (wurliCfgVersion[static_cast<std::size_t> (i)] == cfgVersion) continue;
            if (wurli[static_cast<std::size_t> (i)].isRinging() || keyDown[i]) continue;
            rebuildWurli (i, cfg);
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
        // The transducer swap, crossfaded rather than switched. A coil is a
        // resonant filter; taking it out of the path in one sample is a step
        // in both level and phase, and it measured as a click 1447 times the
        // signal's own worst second difference -- the loudest thing in the
        // instrument that a control could do. Nor is it something the
        // instrument can do: a pickup is bench hardware, like the felt and
        // the wire, and every other bench control here reaches the player at
        // the next strike rather than under the note already in the air. A
        // shared output stage has no next strike to wait for, so the honest
        // form of the same statement is the shortest ramp that manufactures
        // no transient of its own.
        sm.step (sm.coilMix, p.transducer <= 1 ? 1.0f : 0.0f, k);
        sm.step (sm.electroMix, (p.transducer == 1 || p.transducer == 2) ? 1.0f : 0.0f, k);
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
    room.setProfile (p.roomProfile);   // idempotent: early-outs unchanged
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
        // Crossfaded for the same reason the tine's coil is: this bypasses
        // the whole electrostatic bus and substitutes a flat gain, so
        // switching it under a ringing chord is a step in level and in
        // filtering at once -- measured at 326 times the signal's own worst
        // second difference. The bus runs either way so it never re-enters
        // a live path cold.
        const double eMix = static_cast<double> (sm.electroMix);
        for (int k = 0; k < WurliVoice::kOver; ++k)
        {
            const double bus = wurliBus.process (dcBus[k]);
            const double flat = dcBus[k] * 40.0;
            os[k] = wurliPre.process (eMix >= 1.0 ? bus
                                    : eMix <= 0.0 ? flat
                                    : flat + eMix * (bus - flat));
        }
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
            y += nsum * 6.0e-4;
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
