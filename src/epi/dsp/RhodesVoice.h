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
#include "ModalCore.h"
#include "PickupMagnetic.h"

#ifdef EPI_NAN_TRACE
 #include <cstdio>
#endif

#include <algorithm>
#include <complex>
#include <type_traits>

namespace epi
{

// ---------------------------------------------------------------------------
// The tonebar.
//
// Half of Harold Rhodes' patented asymmetric tuning fork. The tine is the
// lower, thin prong; the tonebar is the upper, much heavier steel one, and the
// two are bolted together through an aluminium block.
//
// The received wisdom is that the two are tuned alike. They are not. Muenster
// & Pfeifle struck tonebars with an instrumented hammer and measured their own
// fundamentals against the note the assembly produces (ISMA 2014, Table 1):
// tonebar 33 rings at 105 Hz while the note is 263 Hz, and the gap widens with
// pitch until a top-octave bar sits well over an octave below its own note.
//
// What happens instead is that the tine, being far lighter and far less
// damped, drives the tonebar at ITS frequency: the bar is enslaved, and shows
// no motion of its own beyond the first few milliseconds. Its job is not to
// sound the note. It is to be a large lossy mass hung off the tine, which
// does two things: it adds the glockenspiel clang to the attack, and, because
// the fork as a whole is balanced about its mounting, it stops the note
// leaking away into the harp and so extends the sustain.
//
// So the bar is modelled as a real resonator at its own measured frequency,
// joined to the tine by a stiff spring. Nothing in the code forces it to
// follow the tine; it does that on its own, for the same reason the real one
// does.
// ---------------------------------------------------------------------------
struct TonebarTable
{
    // Measured pairs from ISMA 2014, Table 1: the note the assembly sounds,
    // and the tonebar's own fundamental struck in isolation. The series is not
    // monotonic because the bars are made in size groups, and the treble bars
    // are short and stiff and start the progression over.
    static constexpr int kN = 9;
    static constexpr double kNoteHz[kN] = {  79.0, 118.0, 176.0,  263.0,  393.0,
                                            588.0, 880.0, 1316.0, 1969.0 };
    static constexpr double kBarHz[kN]  = {  51.0,  69.0,  79.0,  105.0,  138.0,
                                            183.0, 140.0,  145.0,  222.0 };

    static double barFrequency (double noteHz)
    {
        const double f = std::clamp (noteHz, kNoteHz[0], kNoteHz[kN - 1]);
        for (int i = 0; i < kN - 1; ++i)
            if (f <= kNoteHz[i + 1])
            {
                // Interpolated in log frequency, which is how the bar sizes
                // actually progress across the keyboard.
                const double t = std::log (f / kNoteHz[i]) / std::log (kNoteHz[i + 1] / kNoteHz[i]);
                return kBarHz[i] * std::pow (kBarHz[i + 1] / kBarHz[i], t);
            }
        return kBarHz[kN - 1];
    }
};

// ---------------------------------------------------------------------------
// One Rhodes note, from the key down to the coil.
//
//   hammer -> tine (two polarisations) -> tonebar -> harp
//                    |
//                    +-> magnetic field -> coil -> output
//
// The tine is carried as two sets of modes, one per transverse direction. The
// hammer only drives the vertical set, but the two do not stay independent: at
// the deflections a Rhodes tine actually reaches -- millimetres, on a wire well
// under two millimetres thick -- bending the beam also STRETCHES it, and that
// stretching stiffens it in both directions at once. Pfeifle carries this as a
// Kirchhoff-type term (DAFx-17, Eq. 2-3),
//
//     rho u_tt + [EI u_xx]_xx - (1/2) EA u_xx K(u) = F,
//     K(u) = INTEGRAL (u_x^H)^2 + (u_x^V)^2 dx
//
// and the high-speed camera shows the consequence: the tip does not swing in a
// plane, it traces an ellipse that slowly rotates (ISMA 2014, Figure 4).
//
// That term has potential (EA/8) K^2 -- a constant times a square, so
// non-negative for every possible state. It is therefore admissible as a
// quadratised (SAV) nonlinearity with no conditions attached, and carrying it
// that way makes it provably incapable of adding energy at any amplitude. This
// matters more than it sounds: measured directly, the same term applied as an
// explicit force diverges within nine milliseconds at a realistic fortissimo.
//
// The tine-to-tonebar joint is quadratised too, for a different reason. It is
// a bolted aluminium block, effectively rigid, and a stiffness that large in
// the explicit part of the scheme imposes its own time-step limit -- measured
// at around 6e4 N/m for a plausible tine and bar, orders of magnitude below
// the real joint. Carried as a quadratised term with a constant gradient, the
// joint stiffness stops constraining the time step at all.
// ---------------------------------------------------------------------------
class RhodesVoice
{
public:
    static constexpr int kTineModes = 8;
    static constexpr int kBarModes  = 6;
    static constexpr int kV0 = 0;                       // vertical tine modes
    static constexpr int kH0 = kTineModes;              // horizontal tine modes
    static constexpr int kB0 = 2 * kTineModes;          // tonebar modes
    static constexpr int kNumModes = kB0 + kBarModes;   // 22

    static constexpr int kTermStretch = 0;   // the tine's own geometric stiffening
    static constexpr int kTermJoint   = 1;   // tine-to-tonebar

    using System = SavModalSystem<kNumModes, 4>;

    struct Config
    {
        // Action
        double hammerHardness = 0.5;
        double hammerMassNorm = 0.5;
        double escapementNorm = 0.4;
        double damperGrip     = 0.6;
        // The damper felt's condition, 0 stock. Fresh grips faster; worn
        // lazily; hardened has lost its compliance and fails to seat on the
        // fine ripple, so the high partials escape it -- the zing of an old
        // damper. Two grip bands, split at 1.2 kHz.
        double damperFelt     = 0.0;
        // The hammer's covering, 0 stock: soft felt, hard felt, lacquered,
        // leather, wood -- a point in the contact law's parameter space
        // relative to this instrument's own stock covering.
        double hammerMat      = 0.0;

        // Resonator
        double tuningSpring   = 0.5;
        double damping        = 0.35;
        double barCoupling    = 0.6;
        double barTuneSemis   = 0.0;
        double nonlinearity   = 0.5;

        // Transducer
        double pickupOffset   = -0.35;   // in pole half-widths
        double pickupGapNorm  = 0.35;

        // Master tuning and pitch bend together, in cents. Applied the way the
        // instrument is tuned: by moving the spring, not by re-cutting the
        // tine. So the geometry below stays fixed to the nominal note and only
        // the frequencies move -- which is also why a bend does not change how
        // the note is struck.
        double detuneCents    = 0.0;
        // 0 Magnetic, 1 Native (= magnetic here), 2 Electro, 3 Contact.
        double transducer     = 1.0;
        double material       = 0.0;  // index into kMaterials; 0 = stock
    };

    // Compared byte-for-byte by the engine to decide whether the instrument
    // needs rebuilding, so it must have no padding for that comparison to mean
    // what it says.
    static_assert (std::is_trivially_copyable<Config>::value, "Config must be memcmp-able");
    static_assert (sizeof (Config) == 16 * sizeof (double), "Config has padding");

    void prepare (double sampleRate, const MagneticPickup* sharedField)
    {
        fs = sampleRate;
        field = sharedField;
        sys.prepare (sampleRate);
        sys.setNumModes (kNumModes);
        hammer.prepare (sampleRate);
        buildStretchMatrix();
        reset();
    }

    void reset()
    {
        sys.clear();
        sys.setNumModes (kNumModes);
        hammer.reset();
        sounding = held = false;
        pedalAmt = 0.0; damperEff = damperFactor;
        controlCounter = 0;
        fadeLeft = 0;
        pendingNote = -1;
        reduced = false;
        configured = false;
        // The transduction's running state. It belongs to the note that was
        // sounding, so it cannot outlive a reset: swingEnv is a peak-hold
        // that chooses the linear path for a quiet tine, and the last tip
        // values are where that note happened to be when the music stopped.
        swingEnv = 0.0;
        lastTipV = lastTipH = 0.0;
        // The oversampler's interpolation history, which is three tip
        // positions of the note that was sounding. A strike from rest seeds
        // it; a strike onto a voice that is still ringing deliberately does
        // not, because the metal really is still moving. A reset is neither:
        // the metal has been stopped, so the history it left behind has to
        // go with it or the next note's first samples are interpolated
        // through the last one's.
        for (int i = 0; i < 3; ++i) { vHist[i] = 0.0; hHist[i] = 0.0; }
        // And the iron goes back to where a freshly built voice has it, with
        // the next application snapped rather than swept in. The glide exists
        // so a knob turned UNDER a ringing note does not click; after a reset
        // there is no note to click, and carrying the last session's operating
        // point through is how a prepared instrument ends up sounding
        // different from a fresh one -- measured on the tine at -27.5 dB
        // against the signal, for any note struck before the host
        // reconfigured. The engine invalidates its own coilSat cache in
        // prepare(), so the real value lands on the first block after this.
        satAmt = satAmtT = 0.0;
        satOn = false;
        updateRestSaturation();
        snapTransduction = true;
    }

    bool isSounding() const { return sounding; }
    bool isHeld()     const { return held; }
    int  noteNumber() const { return note; }
    // What the collision was actually configured as, so the compromises in it
    // can be measured rather than reasoned about. The stiffness cap in
    // particular is invisible from outside: it silently softens the treble
    // hammer, and whether it is binding at a given note is not something that
    // can be worked out reliably on paper.
    struct Collision
    {
        double tineLength    = 0.0;   // m
        double modalMass     = 0.0;   // kg
        double effTineMass   = 0.0;   // kg, at the strike point
        double hammerMass    = 0.0;   // kg
        double stiffnessWant = 0.0;   // N/m^alpha, before the stability cap
        double stiffnessUsed = 0.0;   // N/m^alpha, after it
        bool   capBinding    = false;
    };
    const Collision& collision() const { return diag; }
    int divergedCount() const { return diverged; }
    bool isLockedOut() const { return lockedOut; }

    double tipDisplacement() const { return lastTipV; }
    double tipHorizontal()   const { return lastTipH; }

    // A window of the tine's actual motion, decimated so the stored span
    // covers a few cycles whatever the note. The interface draws the tine from
    // this rather than from a wobble of its own invention -- at eighty hertz a
    // sixty-hertz telemetry tick cannot carry the waveform, so sending the
    // instantaneous value and animating between them shows nothing real.
    static constexpr int kTraceLen = 128;
    float traceAt (int i) const { return trace[i & (kTraceLen - 1)]; }
    int   traceHead() const { return traceIdx; }
    double soundingHz() const { return noteHz; }

    // For tests and for the retirement threshold below to be chosen from data.
    double modalEnergy() const { return sys.energy(); }
    bool   justStruck() const { return strikeFlag; }
    void   clearStrikeFlag() { strikeFlag = false; }
    int  contactSamples() const { return hammer.contactDurationSamples(); }

    // ---- note lifecycle ---------------------------------------------------
    // Struck while this voice is still ringing.
    //
    // If it is the SAME note, the state is kept. A hammer meeting a tine that
    // is already moving is what actually happens when a player repeats a note,
    // and it is why a repeated note on a real instrument reinforces or fights
    // the one before it depending on where in the cycle it lands. Zeroing the
    // state instead puts a step discontinuity straight into the output -- an
    // audible click on every repeated note, which is exactly what this did.
    //
    // If it is a DIFFERENT note the state cannot be kept: the modes are about
    // to be retuned, and energy sitting in them would slide to the new pitch.
    // So the voice is faded first, over a couple of milliseconds, and the
    // strike happens at the end of that. Two milliseconds is below anything a
    // player can feel and well above what it takes to avoid a step.
    // Give this tine its note. Called once, when the instrument is built --
    // a tine is cut for one note and stays there.
    void setNote (int midiNote, const Config& cfg)
    {
        note = midiNote;
        noteHz = 440.0 * std::pow (2.0, (static_cast<double> (midiNote) - 69.0) / 12.0)
               / (geoLen * geoLen);
        traceDecim = std::max (1, static_cast<int> (fs / (noteHz * kTraceLen / 4.0)));
        configure (cfg);
    }

    // The workshop: this tine's own steel, as the two numbers a modder can
    // change. The DIAMETER trim swaps the wire gauge -- the tine is then
    // re-cut for its nominal note, so at any gauge the pitch stands and what
    // moves is everything downstream of the geometry: modal mass, the shear
    // that pulls the overtones flat, how hard the hammer can drive it. The
    // LENGTH trim is applied after that cut, and pitch follows the beam
    // equation's 1/L^2 -- a two-and-a-half-percent trim is a quarter tone.
    // Both default to one, which is the instrument as shipped.
    void setGeometryTrim (double lenScale, double diaScale)
    {
        geoLen = std::clamp (lenScale, 0.5, 2.0);
        geoDia = std::clamp (diaScale, 0.4, 2.5);
    }

    // The pickup workshop: this pickup's own voicing-screw errors, riding as
    // offsets on the panel's height and gap in the panel's normalised units.
    // The winding scale is applied by the engine at the flux sum -- turns on
    // a coil scale what it contributes, not what the magnet sees.
    void setPickupTrim (double heightOffset, double gapOffset)
    {
        pkHOff = std::clamp (heightOffset, -0.75, 0.75);
        pkGOff = std::clamp (gapOffset, -0.5, 0.5);
    }

    // Strike it. The state is NEVER cleared: a hammer meeting a tine that is
    // already moving is what happens when a player repeats a note, and it is
    // why a repeated note reinforces or fights the one before it depending on
    // where in the cycle it lands. There is also nothing to steal here -- every
    // note has its own tine, as the instrument does -- so the whole class of
    // bug that voice allocation brings simply does not arise.
    void noteOn (int midiNote, double velocity, const Config& cfg, std::uint32_t seed)
    {
        (void) midiNote;
        strikeNow (note, velocity, cfg, seed, false);
    }

    void strikeNow (int midiNote, double velocity, const Config& cfg,
                    std::uint32_t seed, bool freshState)
    {
        lockedOut = false;   // a strike is permission to try again
        note = midiNote;
        held = true;
        sounding = true;
        rng = Rng (seed | 1u);

        if (reduced) { sys.setNumModes (kNumModes); reduced = false; }

        if (freshState)
        {
            sys.clear();
            sys.setNumModes (kNumModes);
            for (int i = 0; i < 3; ++i) { vHist[i] = 0.0; hHist[i] = 0.0; }
            configure (cfg);
        }

        // A Rhodes hammer arrives somewhere between a fraction of a metre per
        // second and about six. The exponent is the key's own leverage, not a
        // taste curve -- but it was too steep at 2, which pushed everything a
        // player does below mezzo-forte into the part of the magnet's field
        // that is nearly flat, and the instrument came out as a sine bank with
        // a bark that only appeared at the very top of the velocity range.
        // The floor is not a taste setting either: it is the speed at let-off
        // that still lands the hammer on the longest tine after the free
        // flight below has taxed it. It moved with the let-off graduation --
        // the manual's line is shorter than the ramp this used to carry at
        // the reference bass note, so the same arrival speed needs less at
        // let-off: sqrt(0.195^2 - 2 g 0.42 (4.65 - 4.08) mm) = 0.183.
        const double v = 0.183 + 5.6 * std::pow (std::clamp (velocity, 0.0, 1.0), 1.7);

        const double escMm = escapementMm (midiNote, cfg.escapementNorm);
        // After escapement the hammer is in free flight UPWARD through the
        // let-off gap, and gravity taxes it a fixed energy: v' = sqrt(v^2 -
        // 2 g h). The height that matters is the CENTRE OF GRAVITY's rise,
        // a bit under half the tip's let-off through the pivot arc. A fortissimo
        // blow pays a fraction of a percent; a pianissimo one loses a
        // seventh of its speed across a bass let-off, which is why soft
        // playing on the real instrument stays inside the pole's flat and
        // clean of growl -- and, played softly enough with the let-off
        // opened right up, the hammer fails to arrive, as a real key can.
        const double vEsc2 = 2.0 * 9.81 * 0.42 * escMm * 1.0e-3;
        const double vEff = std::sqrt (std::max (0.0, v * v - vEsc2));
        if (vEff <= 1.0e-3) return;   // the hammer never reached the tine
        hammer.strike (vEff, escMm * 1.0e-3);

        noteHz = 440.0 * std::pow (2.0, (static_cast<double> (midiNote) - 69.0) / 12.0);
        traceDecim = std::max (1, static_cast<int> (fs / (noteHz * kTraceLen / 4.0)));
        traceCount = 1;
        traceIdx = 0;
        for (int i = 0; i < kTraceLen; ++i) trace[i] = 0.0f;
        strikeFlag = true;
    }

    // Escapement: the gap left between tip and tine with the key fully down.
    // It exists because "the whipping action of the Tine ... increases as it
    // becomes longer toward the Bass end" -- a bass tine struck hard swings
    // back down past where a closer hammer would still be sitting, and gets
    // struck a second time on its own downstroke.
    //
    // The service manual graduates it, and Figure 4-2 gives it as a BAND at
    // three points on the rail rather than one line: 6.35-9.53 mm at the
    // extreme bass, 1.59-3.18 mm at tone bar 41, 0.79-2.38 mm at the extreme
    // treble. Two things follow that this used to get wrong. The graduation
    // is not a straight line in tone-bar number -- a straight line would put
    // bar 41 at 3.6 mm where the figure reads 1.6 -- so it is interpolated
    // in log between the documented points, one segment either side of bar
    // 41, which is how the striking line and the tine lengths progress too.
    // And the band is the whole of the service adjustment, so the control
    // spans it and nothing beyond: at the default the instrument is set a
    // little tight of centre, which is a regulated instrument rather than
    // one at the edge of tolerance. The ramp this replaces sat 12% BELOW
    // the band at both ends and above it in the middle.
    static double escapementMm (int note, double norm)
    {
        struct Point { double note, lo, hi; };
        constexpr Point bass  {  21.0, 6.350, 9.525 };   // tone bar 1, A0
        constexpr Point mid   {  61.0, 1.588, 3.175 };   // tone bar 41
        constexpr Point treb  { 108.0, 0.794, 2.381 };   // tone bar 88, C8
        const double n = std::clamp (static_cast<double> (note), bass.note, treb.note);
        const Point& a = n <= mid.note ? bass : mid;
        const Point& b = n <= mid.note ? mid  : treb;
        const double t  = (n - a.note) / (b.note - a.note);
        const double lo = a.lo * std::pow (b.lo / a.lo, t);
        const double hi = a.hi * std::pow (b.hi / a.hi, t);
        return lo * std::pow (hi / lo, std::clamp (norm, 0.0, 1.0));
    }

    void noteOff() { held = false; }
    // Half-pedal: CC64 arrives as a continuous value, and the damper is a
    // contact damping term, so felt pressure scales the DECAY RATE. Pedal
    // travel lifts the rail linearly, which reduces the felt COMPRESSION
    // linearly -- but felt pushes back as compression^2.5 (the same
    // hammer-felt exponent class as Chaigne-Askenfelt), and without that
    // curve the whole audible half-pedal zone crushes into the last
    // quarter of travel. Fully down the exponent is zero and the damper
    // vanishes; fully up it is the measured grip.
    void setPedal (double amount)
    {
        pedalAmt = std::clamp (amount, 0.0, 1.0);
        const double x = std::pow (1.0 - pedalAmt, 2.5);
        damperEff   = std::pow (damperFactor, feltWLo * x);
        damperEffHi = std::pow (damperFactor, feltWHi * x);
    }

    // ---- audio ------------------------------------------------------------
    // Advances the mechanics one sample and writes kOver oversampled flux
    // values for the caller to sum and decimate.
    //
    // Why the transducer runs faster than the mechanics: the tine's motion is
    // band-limited -- its highest live mode is well below Nyquist by
    // construction -- but the FIELD it moves through is strongly curved, and a
    // curved function of a sine makes harmonics without limit. At an 82 Hz
    // note the field is still producing meaningful content at its three
    // hundredth harmonic, and every one of those above Nyquist folds back to a
    // frequency that has nothing to do with the note. That folded content is
    // not distortion in any musical sense; it is the buzz and the "weird
    // harmonics" of an aliased nonlinearity, and no amount of work on the
    // magnet's shape removes it.
    //
    // So the mechanics are stepped once and the tip's path between this sample
    // and the last is reconstructed -- legitimately, because it is band-limited
    // -- at kOver points, the field is evaluated at each, and the caller
    // decimates. The modal system, which is the expensive part, still runs at
    // the host rate.
    static constexpr int kOver = 4;

#ifdef EPI_NAN_TRACE
    // Test scaffolding: says WHICH step first produced a non-finite state,
    // which is the only question worth asking about a one-block blow-up.
    const char* nanStage = nullptr;
    void checkNan (const char* where)
    {
        if (nanStage) return;
        for (int i = 0; i < kNumModes; ++i)
            if (! std::isfinite (sys.displacement (i))) { nanStage = where; return; }
        if (! std::isfinite (sys.energy())) { nanStage = where; return; }
    }
 #define EPI_CHK(x) checkNan (x)
#else
 #define EPI_CHK(x) ((void) 0)
#endif

    void process (const Config& cfg, double* fluxOut)
    {
        EPI_CHK ("on entry");
        // A steal in progress: wind this voice down, then strike the new note.
        if (fadeLeft > 0)
        {
            for (int i = 0; i < kNumModes; ++i) sys.scaleMode (i, fadeFactor);
            if (--fadeLeft == 0)
            {
                strikeNow (pendingNote, pendingVel, cfg, pendingSeed, true);
                pendingNote = -1;
            }
        }

        if (! sounding)
        {
            for (int k = 0; k < kOver; ++k) fluxOut[k] = satRestVal;
            return;
        }

        // -- hammer ---------------------------------------------------------
        if (hammer.isActive())
        {
            const double u = sys.displacementAt (shapeStrikeV);
            const double v = sys.velocityAt (shapeStrikeV);
            const double f = hammer.tick (u, v, hammerCfg);
            if (f != 0.0)
            {
                for (int m = 0; m < kTineModes; ++m)
                    sys.addForce (kV0 + m, f * shapeStrikeV[kV0 + m]);

                // A hammer tip is never perfectly square to the tine, so a
                // little of every blow goes sideways. This is what starts the
                // horizontal polarisation, and therefore what makes the tip
                // trace an ellipse rather than a line.
                const double skew = 0.05 * (0.6 + 0.8 * rng.nextUnipolar());
                for (int m = 0; m < kTineModes; ++m)
                    sys.addForce (kH0 + m, f * skew * shapeStrikeV[kV0 + m]);
            }
        }

        // -- the dormant tier -------------------------------------------------
        // A tine the harp is holding barely awake -- reduced to its
        // fundamental, energy below the reduction threshold, no hammer --
        // does not need the full transduction. Its swing is nanometres, so
        // the flux is exactly rest plus slope times displacement, and the
        // slope needs refreshing only at control rate as the pickup glides.
        // What is NOT skipped: the modal step (it is one mode), the damper,
        // the glide, retirement, and promotion back to the full path the
        // moment the energy grows. Before this tier existed, eighty
        // sympathetic tines at minus a hundred and forty decibels paid four
        // field evaluations a sample each to render silence -- at ninety-six
        // kilohertz that was most of the pedal's cost.
        if (reduced && ! hammer.isActive() && trans == 0)
        {
            glidePickup();
            sys.tick();
            if (! held && pedalAmt < 1.0 && sys.displacement (kV0) != 0.0)
                sys.scaleMode (kV0, damperEff);

            const double vNow = sys.displacement (kV0) * shapeTipV[kV0];
            if (! std::isfinite (vNow)) { recover (fluxOut); return; }

            if (++controlCounter >= kControlDecim)
            {
                controlCounter = 0;
                // Slope of the (saturated) flux about the current rest point.
                const double d = 0.25 * linearSwing;
                const double f0r = field != nullptr ? field->flux (staticOffset, staticGap) : 0.0;
                const double f1r = field != nullptr ? field->flux (staticOffset + d, staticGap) : 0.0;
                dormantSlope = (f1r - f0r) / d * (satOn ? satRestSlope : 1.0);

                const double e = sys.energy();
                if (e >= kReducedEnergy)
                {
                    // Promoted: wake the full mode set and hand the
                    // interpolation history a clean start.
                    sys.setNumModes (kNumModes);
                    reduced = false;
                    vHist[0] = vHist[1] = vHist[2] = vNow;
                    hHist[0] = hHist[1] = hHist[2] = 0.0;
                }
                if (! std::isfinite (e)) { recover (fluxOut); return; }
                if (e < quietEnergy * 0.1) sounding = false;
            }

            // Linear interpolation across the oversampled frame: the content
            // is one sine far below Nyquist, and a held value would print the
            // staircase comb this file has met before.
            for (int k = 0; k < kOver; ++k)
            {
                const double t = static_cast<double> (k + 1) / kOver;
                const double vv = dormantV + (vNow - dormantV) * t;
                fluxOut[k] = satRestVal + dormantSlope * vv;
            }
            dormantV = vNow;
            lastTipV = vNow;
            lastTipH = 0.0;
            if (--traceCount <= 0)
            {
                traceCount = traceDecim;
                traceIdx = (traceIdx + 1) & (kTraceLen - 1);
                trace[traceIdx] = static_cast<float> (vNow);
            }
            return;
        }

        glidePickup();      EPI_CHK ("glidePickup");
        updateStretchTerm(); EPI_CHK ("updateStretchTerm");
        sys.tick();         EPI_CHK ("sys.tick");

        if (! held && pedalAmt < 1.0) { applyDamper(); EPI_CHK ("applyDamper"); }

        if (++controlCounter >= kControlDecim)
        {
            controlCounter = 0;

            // How many modes are worth integrating.
            //
            // A tine answering sympathetically is being driven by the frame at
            // frequencies nowhere near its overtones, so it responds in its
            // fundamental and nothing else -- the upper modes of a tine ringing
            // at a hundredth of a millimetre are carrying nothing at all. Below
            // the quiet threshold only the fundamental is stepped, and the rest
            // are frozen rather than cleared, so a strike picks them straight
            // back up.
            //
            // Switching only happens below the threshold, where the modes being
            // parked hold nothing audible, so it cannot make a discontinuity.
            const double e = sys.energy();

            // A tine that has gone non-finite is put back, and the fact is
            // recorded rather than hidden.
            //
            // Nothing in a physical model should ever reach this, and the
            // scheme's linear part cannot: its per-mode coefficients are
            // bounded by construction, so the modal bank alone is stable at any
            // frequency and any damping. What is not bounded by construction is
            // the CONDITIONING of the rank-two solve that carries the two
            // quadratised terms, and driven to the far corner of the joint's
            // range -- a nearly rigid joint, a heavy tuning spring and the bar
            // tuned a fifth away, all at once -- it loses it and the state runs
            // to infinity inside a single block.
            //
            // That is a real defect and it is not fixed by this. But the
            // difference between one note dropping out for a moment and a
            // non-finite sample reaching the host is the difference between a
            // blemish and a dead session, and no amount of remaining doubt
            // about the cause justifies shipping the second one.
            if (! std::isfinite (e)) { recover (fluxOut); return; }

            // The threshold is set where the overtones have genuinely gone,
            // not where the whole tine has. At 1e-10 the note is some thirty
            // decibels down in amplitude, and by then the overtones -- whose Q
            // is twenty-five times lower than the fundamental's -- decayed
            // long ago; a sympathetically driven tine never had them at all.
            // The old threshold of 1e-13 only caught tines that were already
            // inaudible, so with the pedal down all eighty-eight ran their
            // full twenty-two modes to carry what one mode was holding.
            // Freezing rather than clearing the parked modes is what lets a
            // strike pick them straight back up.
            if (! hammer.isActive() && e < kReducedEnergy)
            {
                if (! reduced)
                {
                    sys.setNumModes (1);
                    reduced = true;
                    // The dormant tier owns the flux from here; give its
                    // interpolator a continuous starting point and take the
                    // quadratised terms down -- at these energies they carry
                    // nothing, and the resync discipline re-enables them on
                    // the way back up.
                    dormantV = lastTipV;
                    sys.disableTerm (kTermStretch);
                    sys.disableTerm (kTermJoint);
                    // The one live mode carries the assembled pitch itself
                    // while the joint is down; a dormant tine still rings at
                    // its note, not at the unbolted tine's.
                    if (jointTuned) { sys.setFrequency (kV0, assembledF0); jointTuned = false; }
                }
            }
            else if (reduced)
            {
                sys.setNumModes (kNumModes);
                reduced = false;
            }

            // Retired one decade below the point where the quadratised terms
            // stop being carried, which is measured at about 120 dB under the
            // note -- comfortably inaudible, and reached about half a second
            // after the damper lands. The old threshold of 1e-24 was 11 decades
            // further down: with the psi resync above it is reachable, and
            // without it, it never was. The state is not cleared, so the harp
            // can still wake this tine.
            if (! hammer.isActive() && e < quietEnergy * 0.1)
                sounding = false;
        }

        // -- transduction ------------------------------------------------------
        const double vNow = sys.displacementAt (shapeTipV);

        // Checked here, every sample, because this is the value that indexes
        // the field table. A non-finite tip position is not merely a bad sample
        // -- it converts to an integer index of whatever the hardware feels
        // like, reads outside the table, and takes the process with it. The
        // energy check above runs at control rate, which is too late for that.
        if (! std::isfinite (vNow)) { recover (fluxOut); return; }
        const double hNow = sys.displacementAt (shapeTipH);

        // -- alternative transducers --------------------------------------
        // The founding measurement says the transducer makes the timbre, so
        // the transducer is swappable. Electro: the tip is one plate of a
        // polarised capacitor, out follows y/(g-y) -- the reed piano's law
        // pointed at a tine. Contact: the inertial force into the mount,
        // linear, which is the harp heard through a contact microphone. Both
        // reuse the same hermite path reconstruction as the coil, and both
        // report a zero rest so the engine's operating-point bookkeeping
        // stays exact.
        if (trans == 2)
        {
            // The same insulator rule the other three voices carry: nylon
            // cannot be the moving plate of a polarised capacitor. Flagged
            // by the engine suite -- this path alone had no gate.
            const double gE = std::max (0.6e-3, staticGap);
            for (int k = 0; k < kOver; ++k)
            {
                const double t = static_cast<double> (k + 1) / kOver;
                double vv = hermite (vHist[0], vHist[1], vHist[2], vNow, t);
                vv = std::min (vv, 0.85 * gE);
                fluxOut[k] = matCond ? kElectroOut * (vv / (gE - vv)) : 0.0;
            }
            vHist[0] = vHist[1]; vHist[1] = vHist[2]; vHist[2] = vNow;
            hHist[0] = hHist[1]; hHist[1] = hHist[2]; hHist[2] = hNow;
            lastTipV = vNow; lastTipH = hNow;
            if (--traceCount <= 0)
            {
                traceCount = traceDecim;
                traceIdx = (traceIdx + 1) & (kTraceLen - 1);
                trace[traceIdx] = static_cast<float> (vNow);
            }
            return;
        }
        if (trans == 3)
        {
            const double cf = sys.displacementAt (shapeContact);
            for (int k = 0; k < kOver; ++k)
            {
                const double t = static_cast<double> (k + 1) / kOver;
                fluxOut[k] = kContactOut * hermite (cfHist[0], cfHist[1], cfHist[2], cf, t);
            }
            cfHist[0] = cfHist[1]; cfHist[1] = cfHist[2]; cfHist[2] = cf;
            vHist[0] = vHist[1]; vHist[1] = vHist[2]; vHist[2] = vNow;
            hHist[0] = hHist[1]; hHist[1] = hHist[2]; hHist[2] = hNow;
            lastTipV = vNow; lastTipH = hNow;
            if (--traceCount <= 0)
            {
                traceCount = traceDecim;
                traceIdx = (traceIdx + 1) & (kTraceLen - 1);
                trace[traceIdx] = static_cast<float> (vNow);
            }
            return;
        }

        // A tine swinging a small fraction of the pole's flat is sampling a
        // patch of the field that is straight. A straight function of a sine is
        // a sine: there are no harmonics to alias, so there is nothing for the
        // oversampling to protect. Most of the instrument is in this state most
        // of the time -- the eighty-odd tines answering sympathetically move by
        // microns -- and evaluating the field four times for each of them buys
        // exactly nothing.
        //
        // This is a statement about where the field is linear, not a shortcut
        // around the nonlinearity. The threshold is checked against the pole
        // geometry, so it moves with it.
        // Gated on the SWING, not the instantaneous position. Gating on
        // |vNow| looks equivalent and is a bug that was heard before it was
        // found: a ringing note passes through zero twice every cycle, so the
        // voice toggled between this linearised path and the full one at
        // twice the fundamental, and the approximation mismatch at each
        // toggle is a signal-correlated crackle -- reported as a rustling
        // that got worse as the gap closed, which is exactly where the field
        // curves hardest and the mismatch is largest. The envelope decays
        // slowly enough that a note leaves quiet mode once, near silence,
        // and not twice per cycle.
        swingEnv = std::max (std::abs (vNow), swingEnv * 0.9995);
        if (swingEnv < linearSwing)
        {
            // What is skipped here is the field lookup, NOT the geometry. The
            // tip's path still has to be interpolated across the oversampled
            // frame exactly as it is below.
            //
            // Holding one value across all four subsamples instead -- the
            // obvious shortcut, and what this did -- makes the upsampled
            // waveform a staircase, and the difference between that staircase
            // and the real path is a sawtooth at the note's own frequency. It
            // comes out as a flat comb of odd harmonics still at -29 dB past
            // the thirteenth, and it is loudest on the quietest notes, because
            // those are the ones that spend their lives near this threshold.
            // A soft note is most of what the instrument plays.
            const double arc = (vNow * vNow) / (2.0 * std::max (1.0e-4, tineLength));
            const double gap = std::max (0.25e-3, staticGap - arc);
            const double x0  = staticOffset + vNow;
            const double d   = 0.25 * linearSwing;
            const double f   = field != nullptr ? field->flux (x0, gap) : 0.0;
            // One extra lookup for the local slope. Over a span this small the
            // field is straight, so a line through it is not an approximation
            // that has to be justified -- it is the same claim that licenses
            // skipping the oversampling in the first place.
            const double s   = field != nullptr ? (field->flux (x0 + d, gap) - f) / d : 0.0;

            // The core's curve is linearised about the resting point for the
            // same reason the field is: over a swing this small it is a line,
            // and the two approximations have to agree or the join between the
            // paths becomes a discontinuity of its own.
            double fSat = f, sSat = s;
            if (satOn)
            {
                fSat = satRestVal + satRestSlope * (f - restFlux);
                sSat = s * satRestSlope;
            }
            for (int k = 0; k < kOver; ++k)
            {
                const double t  = static_cast<double> (k + 1) / kOver;
                const double vv = hermite (vHist[0], vHist[1], vHist[2], vNow, t);
                fluxOut[k] = fSat + sSat * (vv - vNow);
            }

            vHist[0] = vHist[1]; vHist[1] = vHist[2]; vHist[2] = vNow;
            hHist[0] = hHist[1]; hHist[1] = hHist[2]; hHist[2] = hNow;
            lastTipV = vNow;
            lastTipH = hNow;


            // A bare conductor speaks faintly through eddy currents; a
            // ferromagnet in full; an insulator not at all. Scaled about the
            // rest point so the engine's operating-point bookkeeping holds.
            if (magCouple != 1.0)
            {
                const double r0 = satRestVal;
                for (int k = 0; k < kOver; ++k)
                    fluxOut[k] = r0 + (fluxOut[k] - r0) * magCouple;
            }
            if (--traceCount <= 0)
            {
                traceCount = traceDecim;
                traceIdx = (traceIdx + 1) & (kTraceLen - 1);
                trace[traceIdx] = static_cast<float> (vNow);
            }
            return;
        }

        for (int k = 0; k < kOver; ++k)
        {
            // Catmull-Rom through the last four samples of the tip's path.
            const double t = static_cast<double> (k + 1) / kOver;
            const double vv = hermite (vHist[0], vHist[1], vHist[2], vNow, t);
            const double hh = hermite (hHist[0], hHist[1], hHist[2], hNow, t);

            // The tip is on the end of a beam, so a vertical swing also brings
            // it very slightly closer to the magnet: it travels on an arc, not
            // a straight line. Small, but it is why the waveform is not quite
            // symmetric even with the tine perfectly centred.
            const double arc = (vv * vv) / (2.0 * std::max (1.0e-4, tineLength));
            // The gap is modulated by the ARC alone, not by a second
            // polarisation.
            //
            // Every published model of this instrument reduces the tine to one
            // plane; Pfeifle & Bader tracked a real one with a high-speed
            // camera and say so directly -- approximately sinusoidal motion in
            // one plane, with the tip travelling on the arc of a circle about
            // its fixation. Letting a second oscillator a few cents away
            // modulate the gap instead puts a slow beat on the FUNDAMENTAL,
            // where the reference recordings show four tenths of a decibel and
            // this model had several. That is the instrument phasing against
            // itself, and it is what a listener flagged.
            //
            // The arc gives the geometric gap variation for free, at twice the
            // fundamental, from the one oscillator that is really there. The
            // horizontal set is kept because the tip does trace an ellipse and
            // the interface draws it, but it no longer reaches the magnet.
            const double gap = std::max (0.25e-3, staticGap - arc);

            const double phi = field != nullptr ? field->flux (staticOffset + vv, gap) : 0.0;
            fluxOut[k] = satOn ? saturate (phi) : phi;
        }


        // A bare conductor speaks faintly through eddy currents; a
        // ferromagnet in full; an insulator not at all. Scaled about the
        // rest point so the engine's operating-point bookkeeping holds.
        if (magCouple != 1.0)
        {
            const double r0 = satRestVal;
            for (int k = 0; k < kOver; ++k)
                fluxOut[k] = r0 + (fluxOut[k] - r0) * magCouple;
        }
        vHist[0] = vHist[1]; vHist[1] = vHist[2]; vHist[2] = vNow;
        hHist[0] = hHist[1]; hHist[1] = hHist[2]; hHist[2] = hNow;

        lastTipV = vNow;
        lastTipH = hNow;

        // Capture the waveform for the interface. The decimation is chosen so
        // the buffer always holds about four cycles, so a bass note and a
        // treble note both arrive as a drawable shape.
        if (--traceCount <= 0)
        {
            traceCount = traceDecim;
            traceIdx = (traceIdx + 1) & (kTraceLen - 1);
            trace[traceIdx] = static_cast<float> (vNow);
        }
    }

    // The flux with the tine at rest. Subtracted downstream so the
    // differentiated signal is not a tiny modulation riding on a large
    // constant, which in a narrower type would throw away most of its bits.
    // The iron in this tine's OWN pole piece, and it has to be this tine's own.
    //
    // A Rhodes has one pickup per tine, wired in series. Faraday's law and the
    // coil's resonance are linear, so applying them once to the summed flux is
    // exactly right -- linear operations commute with summing. Saturation does
    // not commute with anything. Applied to the sum it makes every note distort
    // against every other note, which is a sound the instrument cannot make:
    // measured on a low fifth it put the intermodulation 3 dB under the chord
    // itself, and moving it here dropped that by more than 30 dB.
    //
    // It acts on the TOTAL flux, magnet bias included, because that is what the
    // iron sees. The bias is what makes it asymmetric, which is the useful part.
    void setCoreSaturation (double sat)
    {
        // Moved to, not jumped to -- the same Faraday argument as the
        // voicing screw: the knee's operating values shift the resting flux,
        // and a step there is a click through the differentiating coil.
        satAmtT = std::clamp (sat, 0.0, 1.0);
        // Snapped when the instrument is being set up rather than played:
        // before the first configure, and once after any reset. Both are
        // moments with no signal to protect, and gliding through them is
        // what made a re-prepared voice differ from a fresh one -- measured
        // on the tine at -27.5 dB against the signal, for any note that had
        // been struck before the host reconfigured.
        if (! configured || snapTransduction) { satAmt = satAmtT; snapTransduction = false; }
        satOn  = std::max (satAmt, satAmtT) > 1.0e-4;
        updateRestSaturation();
    }

    double restingFlux() const { return trans == 0 ? satRestVal : 0.0; }

    // The tine's displacement where it is bolted to the harp, and a force
    // applied there. This is the whole of the sympathetic path: a struck tine
    // shakes the harp through its own clamp, and the harp shakes every other
    // tine through theirs. Same shape vector both ways, so the coupling is
    // reciprocal and cannot manufacture energy.
    double clampDisplacement() const { return sys.displacementAt (shapeClamp); }

    void addClampForce (double f)
    {
        // A dormant tine carries one live mode; pushing force into parked
        // modes is work the integrator will never read.
        const int m1 = std::min (kTineModes, sys.numModes());
        for (int m = 0; m < m1; ++m) sys.addForce (kV0 + m, f * shapeClamp[kV0 + m]);
    }

    // Worth integrating at all? A tine with no energy and no hammer on it
    // contributes nothing, and there are eighty-eight of them.
    bool isRinging() const { return sounding || hammer.isActive(); }
    double energy() const { return sys.energy(); }
    void setSounding (bool s) { sounding = s; }

    void refresh (const Config& cfg) { if (sounding) configure (cfg); }

private:
    static constexpr int kControlDecim = 32;

    static double hermite (double y0, double y1, double y2, double y3, double t)
    {
        const double c0 = y1;
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    // Neoprene hammer tip, contact width. Falaize & Helie use 1 cm on a 7.83 cm
    // Rhodes tine; the tip does not change size across the keyboard, so the
    // fraction of the tine it covers grows toward the treble on its own.
    static constexpr double kTipWidth = 0.010;

    // The tuning spring's mass, in tine beam-masses. Calibrated on the two
    // measured bass lengths (Shear: 157 mm at A0; 128-135 mm at E1 on the
    // 73-key) through the length solve in configure(): at the default position
    // it multiplies the fundamental's generalised mass by 2.31 and so shortens
    // every tine to 0.811 of its bare-cantilever length. Constant in beam-masses rather than in grams
    // because the two measured notes ask for it -- a constant absolute mass
    // puts E1 at 125 mm, below its band, where a constant fraction puts it at
    // 130 mm inside it. The real springs are graduated with the tines.
    //
    // The POSITION the control sits in the middle of is not measured; nothing
    // on disk gives it, and the mass follows from it through the calibration
    // above.
    static constexpr double kSpringMassRatio = 0.487;
    static constexpr int kStrike = 0, kTip = 1, kBlock = 2;

    double registerPosition() const
    {
        return std::clamp ((static_cast<double> (note) - 28.0) / 60.0, 0.0, 1.0);
    }

    // Ghat_ml = INTEGRAL over the unit length of the product of mode slopes.
    // The physical matrix is this divided by the tine's length, so it is built
    // once and scaled per note.
    void buildStretchMatrix()
    {
        constexpr int kSteps = 512;
        double d[kTineModes][kSteps];
        for (int m = 0; m < kTineModes; ++m)
            for (int s = 0; s < kSteps; ++s)
                d[m][s] = CantileverModes::slope (m, (s + 0.5) / kSteps);

        for (int a = 0; a < kTineModes; ++a)
            for (int b = 0; b < kTineModes; ++b)
            {
                double v = 0.0;
                for (int s = 0; s < kSteps; ++s) v += d[a][s] * d[b][s];
                gHat[a][b] = v / kSteps;
            }
    }

    void configure (const Config& cfg)
    {
        EPI_CHK ("configure entry");
        // The note this tine was cut for, and the note it is currently tuned
        // to. They are not the same thing once master tuning or a bend is in
        // play, and keeping them apart is what makes a bend physical: the
        // steel does not change length, so the mass the hammer meets and the
        // point it strikes stay exactly where they were.
        const double f0Cut = 440.0 * std::pow (2.0, (static_cast<double> (note) - 69.0) / 12.0);
        // The note the trimmed steel actually plays: length trim retunes as
        // 1/L^2, the beam equation's own law. The gauge does not appear here
        // because a regauged tine is re-cut for its nominal note below.
        const double f0Nom = f0Cut / (geoLen * geoLen);
        const double f0 = f0Nom * std::pow (2.0, cfg.detuneCents / 1200.0);
        const double reg = registerPosition();

        // ---- tine geometry --------------------------------------------------
        // One gauge across the whole compass. Shear measured the tines of a
        // Mark I as plain cylindrical wire 1.5 mm across (thesis 2.1), and the
        // set is cut from one stock: what changes from note to note is the
        // length and the spring, not the wire. The taper this model used to
        // carry -- 1.9 mm at the bottom ground to 1.3 mm at the top -- was
        // invented to keep the treble lengths plausible, and it was paying for
        // a different error: the length solve below ignored the tuning
        // spring's mass entirely, so every tine came out as long as a BARE
        // cantilever of the same pitch, and the bass came out 40% too long.
        const double radius = 0.75e-3 * geoDia;
        // The selected material. Index 0 is the stock spring steel exactly
        // (same E and rho; the eta reference below makes its added loss
        // zero), so the calibrated instrument is untouched. Any other metal
        // re-solves the geometry at the same pitch: length goes as the
        // fourth root of E/rho, and the modal mass follows both the density
        // and the new length -- a light metal swings further for the same
        // strike and drives the field nonlinearity harder, which is
        // audible growl, not a filter.
        const Material& mat = kMaterials[std::clamp (static_cast<int> (cfg.material), 0, kNumMaterials - 1)];
        magCouple = magneticCoupling (mat);
        matCond   = mat.conductive;

        // Where the tuning spring sits, as a fraction of the free length, and
        // how heavy it is. Toward the tip it mostly transposes; back toward the
        // clamp it starts pulling the overtones about, because it then sits at
        // a different fraction of each mode's shape.
        //
        // The spring is ONE piece of hardware, so what the control moves is
        // where it sits, not what it weighs -- and it sits near the free end,
        // which is the only part of the tine a coil can be slid along and the
        // only place the factory tuning procedure ever puts it. Its mass is
        // fixed by the measured lengths below.
        const double springPos = 0.81 + 0.12 * std::clamp (cfg.tuningSpring, 0.0, 1.0);
        const double mu        = kSpringMassRatio;

        // The tine is SHORTER than a bare cantilever of the same pitch,
        // because the spring's mass is riding near its tip.
        //
        // This is the whole geometry, and the model used to leave it out. The
        // spring already loaded the OVERTONE ratios (`ratio` below divides the
        // fundamental's own load back out, so the note stayed put) but the
        // length came from the bare beam equation, which meant the solve was
        // answering a question about a tine with no spring on it. Measured
        // against the real set the error is large and one-sided: 220.9 mm at
        // A0 where Shear measured 157, 180.4 mm at E1 where the 73-key tine is
        // 128-135 mm with its spring fitted.
        //
        // A point mass of mu beam-masses at posOverL multiplies the
        // fundamental's generalised mass by (1 + 4 mu phi_1(pos)^2) and leaves
        // its stiffness alone, so the same note is reached at
        // (1 + 4 mu phi^2)^(-1/4) of the bare length. The mass is calibrated
        // to land the two measured bass lengths: 0.487 beam-masses at 87% of
        // the length multiplies the fundamental's generalised mass by 2.31,
        // which is a factor 0.811 on the length -- A0 solves to 159 mm against
        // 157 measured, E1 to 130 mm inside its measured band. In grams that is
        // a spring of 1.08 g at the bottom of the compass falling to 0.09 g at
        // the top, graduated with the tines the way the real springs are.
        const double phiSpring  = CantileverModes::shape (0, springPos);
        const double springLoad = 1.0 + 4.0 * mu * phiSpring * phiSpring;
        tineLength = CantileverModes::lengthForFrequency (f0Cut, radius, mat)
                   * std::pow (springLoad, -0.25) * geoLen;
        const double area = kPiD * radius * radius;
        // The spring rides on the tine, so the generalised mass carries it --
        // the same factor that shortened the steel. Leaving it out would make
        // the shortened tine lighter than the one it replaced AND lighter than
        // the real thing, which is the collision's scale.
        //
        // Per mode, because the load is mu*phi_m(pos)^2 and the modes do not
        // all see the spring alike: near the tip the fundamental is close to
        // its own tip value and carries the whole spring, while the fourth and
        // fifth modes have a node nearby and barely feel it. One shared mass
        // would put every mode at the fundamental's 2.31x, and the modes that
        // answer a knock at the clamp are exactly the high ones -- their
        // shapes at the block are two orders of magnitude larger than the
        // fundamental's.
        const double beamMass  = std::max (1.0e-8, mat.density * area * tineLength * 0.25);
        double modeMass[kTineModes];
        for (int m = 0; m < kTineModes; ++m)
        {
            const double ph = CantileverModes::shape (m, springPos);
            modeMass[m] = beamMass * (1.0 + 4.0 * mu * ph * ph);
        }
        const double modalMass = modeMass[0];

        // Shear matters more for the short thick treble tines than the long
        // bass ones, which is the physical reason the top of a Rhodes is less
        // clangy than plain beam theory predicts.
        // The register fit holds for the nominal geometry; the workshop trims
        // scale it as the physics does, with (r/L)^2. Re-cutting for a fatter
        // gauge lengthens the tine by root-gauge, so the net factor is
        // gauge / lengthTrim^2 -- a fat short tine pulls its overtones flat,
        // which is most of what the DIAMETER control is for.
        const double shearNumber = (0.0016 + 0.0090 * reg) * geoDia / (geoLen * geoLen);

        // Two constant-Q laws, not one ramp across the keyboard.
        //
        // The fundamental is privileged, and it is privileged for a reason: the
        // tine and its tonebar are a tuned fork, and a fork's balanced mode puts
        // almost no net force into what holds it, so it loses energy only to the
        // steel. Spring steel's internal loss is close to frequency-independent,
        // so that mode's Q is constant across the compass and the sustain falls
        // as 1/f. Measured on a Mark I -- 28 s at A2, 19 s at D4, 5.7 s at E5,
        // 1.7 s at C7 -- that works out at Q = 1400 to 2500.
        //
        // No higher mode is balanced by the bar. Each one drives the clamp block
        // directly, and a bolted joint into a heavy bar is a poor place to keep
        // energy, so their Q is lower by more than an order of magnitude. That
        // is the numerical form of ISMA's central observation: filmed at ten
        // thousand frames a second, the tine is a clean sine within about
        // fourteen milliseconds of being hit.
        const double damp = std::clamp (cfg.damping, 0.0, 1.0);
        const double qTrim        = 1.35 - 0.9 * damp;
        const double qOvertone    =   70.0 * qTrim;

        // The fundamental's privileged Q is CONDITIONAL, and the condition is
        // the bar. The fork is balanced about its mounting only when both
        // prongs are attached; unbolt the bar and the tine's fundamental
        // drives the block directly, exactly as its overtones always do, and
        // the note dies at their rate. So the coupling control owns the
        // sustain -- which is what that knob means on the instrument, and
        // which this model had silently dropped: it changed the attack clang
        // and nothing else, and was reported as doing nothing.
        //
        // Normalised so the default setting keeps the measured T60s (28 s at
        // A2 and the rest), full coupling sits at the top of the measured Q
        // range, and zero leaves the fundamental as unprotected as any other
        // mode.
        const double balance = (0.10 + 0.90 * std::clamp (cfg.barCoupling, 0.0, 1.0)) / 0.64;
        const double qFundamental = std::max (qOvertone, 1800.0 * qTrim * balance);

        // Where the hammer lands, as a fraction of the free length.
        //
        // This one number sets the whole scale of the instrument, and it is
        // easy to get badly wrong. A clamped-free mode shape is almost flat
        // near the clamp, so the effective mass the hammer meets there is the
        // modal mass divided by the square of a very small number -- kilograms,
        // against a hammer of a few grams. Move the strike point out by a
        // factor of two and that effective mass falls by more than an order
        // of magnitude, so the strike line and the hammer masses only mean
        // anything as a pair: dropping the manual's line onto the old
        // hammer graduation threw the bass into a fifteen-millimetre swing
        // -- the pickup field saturated and the stretching nonlinearity held
        // a fortissimo low E half a semitone sharp for seconds -- and the
        // two were recalibrated together against the reference suite. A real
        // Rhodes tine moves one to three millimetres at forte.
        // The service manual gives the hammer's contact point as a DISTANCE
        // from the tone generator: two and a quarter inches at the extreme
        // bass falling to an eighth of an inch at the extreme treble, set by
        // "the precise curve given to the Tone Bar Rail". As a fraction of
        // each tine's own free length that runs from about a third down to
        // about an eighth -- the OPPOSITE direction from the straight-rail
        // argument this model used to make. The distance is interpolated in
        // log, which is how the tine lengths themselves progress, and divided
        // by this tine's solved length, so the workshop trims move the strike
        // point exactly as regauging a real tine under a fixed rail would.
        // The manual's two endpoints are the ENDS OF THE RAIL -- A0 and C8 --
        // not the ends of the voicing register, so the interpolation runs over
        // the full compass. Anchoring it on the register position instead put
        // the extreme-treble eighth-inch on every note from E6 up, which
        // parked the top octave's strike so close to the clamp that the
        // attack collapsed below the reference's fastest measured onset.
        const double regRail = std::clamp ((static_cast<double> (note) - 21.0) / 87.0, 0.0, 1.0);
        const double strikeMm = 57.15 * std::pow (3.175 / 57.15, regRail);
        const double strikeAt = std::clamp (strikeMm * 1.0e-3 / tineLength, 0.05, 0.45);
        for (int m = 0; m < kTineModes; ++m)
        {
            shapeMode[m][kStrike] = CantileverModes::shape (m, strikeAt);
            shapeMode[m][kTip]    = CantileverModes::shape (m, 1.0);
            shapeMode[m][kBlock]  = CantileverModes::shape (m, 0.06);
        }

        // The joint stiffness, chosen RELATIVE to the tine's own stiffness
        // rather than as an absolute number.
        //
        // An absolute figure cannot work across the compass: the same spring
        // is a rounding error against a stiff treble tine and completely
        // dominates a bass one, so a fixed value either does nothing at the
        // top or drags the bottom of the keyboard a third out of tune. Scaling
        // it to the tine means "coupling" is the fraction of the tine's
        // stiffness that the joint represents, which is both what the control
        // should mean and what stays comparable from note to note.
        const double w0     = 2.0 * kPiD * f0;
        const double phiBlk = CantileverModes::shape (0, 0.06);
        // At zero the fork is not bolted together at all. That is not a
        // playing position, but it has to be reachable: it is the control that
        // isolates the tine from the bar when something needs to be measured
        // against the tine alone.
        const double frac   = 0.12 * std::clamp (cfg.barCoupling, 0.0, 1.0);

        // Scaled against BOTH ends of the joint, not just the tine.
        //
        // The joint's force is the stiffness times eta, the difference in
        // displacement across it -- and eta is not the tine's motion, it is the
        // tine's motion MINUS the bar's. The bar sits at a point where its own
        // mode shape is several times larger than the tine's is at the block,
        // so once the bar is moving at all it dominates eta completely. A
        // stiffness sized only against the tine then delivers a force hundreds
        // of times the tine's own restoring force, and the treble notes -- where
        // the tine is lightest relative to the bar -- were thrown out to a metre
        // of deflection.
        //
        // Taking the smaller of the two ratios means the joint is a modest
        // fraction of whichever party is weaker at that note, which is the only
        // form that stays sane from one end of the compass to the other.
        const double barF0  = TonebarTable::barFrequency (f0Nom) * std::pow (2.0, cfg.barTuneSemis / 12.0);
        const double barW0  = 2.0 * kPiD * barF0;
        const double barM0  = modalMass * 26.0;
        const double phiBar = CantileverModes::shape (0, 0.10);

        const double ksTine = frac * modalMass * w0 * w0 / std::max (1.0e-12, phiBlk * phiBlk);
        const double ksBar  = frac * barM0 * barW0 * barW0 / std::max (1.0e-12, phiBar * phiBar);
        const double ks     = std::min (ksTine, ksBar);

        // A free cantilever and a cantilever bolted into a tone bar do not ring
        // at the same pitch. The joint is a rank-one stiffness added across the
        // whole fork, so it does not simply shift one mode -- it shifts all of
        // them, and by an amount that depends on every other mode's position.
        // Left uncorrected the bass came out a third sharp; corrected with a
        // per-mode formula it came out most of a semitone flat, because that
        // formula cannot see the cross terms.
        //
        // The exact answer is cheap. Adding c*c^T to a diagonal modal system
        // shifts the eigenvalues to the roots of the secular equation
        //
        //     1 + SUM_i (c_i^2 / m_i) / (w_i^2 - lambda) = 0
        //
        // which is a scalar root-find, solved here in a few bisection steps.
        // The instrument has the same problem and solves it the same way: a
        // tine is cut and its spring set WITH the bar attached, so the
        // assembled fork lands on the note.
        double tineFreq[kTineModes], tineT60[kTineModes];
        for (int m = 0; m < kTineModes; ++m)
        {
            const double r = CantileverModes::ratio (m, mu, springPos, shearNumber);
            tineFreq[m] = f0 * r;

            // Damping from the mode's own FREQUENCY and its own Q, not from a
            // power of its index. Indexing by m looks equivalent and is not: a
            // clamped-free beam's frequencies grow roughly as (2m-1)^2, so a
            // polynomial in m that also grows quadratically cancels against them
            // and leaves the top modes barely damped -- by mode 7 the old rule
            // under-damped by two orders of magnitude against anything
            // defensible.
            //
            // T60 = 6.91 / (pi f / Q) = 2.20 Q / f.
            const double q = (m == 0 ? qFundamental : qOvertone);
            tineT60[m] = materialT60 (2.1985 * q / std::max (1.0, tineFreq[m]),
                                      tineFreq[m], mat, kSpringSteel.lossEta);
        }

        double trim = 1.0;
        if (ks > 0.0)
        {
            const double rk = std::sqrt (ks);
            for (int pass = 0; pass < 8; ++pass)
            {
                const double got = assembledFundamental (tineFreq, tineT60, modeMass,
                                                         barF0, barM0, damp, rk, trim);
                if (! (got > 0.0)) break;
                trim *= f0 / got;
            }
        }

        // The two values the fundamental alternates between. With the joint
        // carried as a live term the mode itself is set BARE and the joint
        // supplies the shift; every economy tier that takes the joint down
        // must hand the shift back to the mode, or the note falls flat by
        // the whole joint contribution -- up to fifty cents at the bottom of
        // the compass -- the moment it decays past the reduction threshold.
        // The fork never unbolts; a decaying note holds its pitch.
        bareF0      = tineFreq[0] * trim;
        assembledF0 = f0;
        jointTuned  = true;

        for (int m = 0; m < kTineModes; ++m)
        {
            sys.setMode (kV0 + m, tineFreq[m] * trim, tineT60[m], modeMass[m]);
            // The tine is round but not perfectly so, and it is clamped in a
            // block that is stiffer one way than the other. A few cents of
            // split between the polarisations is what makes the tip orbit
            // precess instead of closing on itself.
            //
            // It takes the same trim as the vertical set: it is the same piece
            // of steel, and the joint stiffens it too. Trimming only one of
            // them left the two polarisations nearly a semitone apart, which a
            // listener hears as the note beating against itself rather than as
            // the slow ellipse it should be.
            sys.setMode (kH0 + m, tineFreq[m] * trim * 1.004, tineT60[m] * 0.85, modeMass[m]);
        }

        // ---- tonebar --------------------------------------------------------
        const double barF = TonebarTable::barFrequency (f0Nom) * std::pow (2.0, cfg.barTuneSemis / 12.0);
        const double barMass = modalMass * 26.0;   // far heavier than the tine

        for (int m = 0; m < kBarModes; ++m)
        {
            barShape[m] = CantileverModes::shape (m, 0.10);

            const double r = CantileverModes::ratio (m, 0.0, 1.0, 0.004);
            // Bolted at both ends into a sprung rail: far more heavily
            // damped than the tine, which is why its contribution is an attack
            // transient and not a second note.
            const double t60 = (0.10 + 0.14 * (1.0 - damp)) / (1.0 + 1.8 * m);

            // Set at the frequency that was measured on the real bar. The
            // joint raises it a little, which is physical and is left in --
            // unlike the tine, the bar is not being asked to land on a note.
            sys.setMode (kB0 + m, barF * r, t60, barMass);
        }

        // ---- readout and drive vectors ---------------------------------------
        for (int i = 0; i < kNumModes; ++i)
        {
            shapeTipV[i] = shapeTipH[i] = shapeStrikeV[i] = 0.0;
            jointGrad[i] = 0.0;
        }
        // The contact patch.
        //
        // Not a refinement -- without it the model has no well-defined effective
        // mass at all.
        //
        // Struck near the clamp, a cantilever's mode shapes there go as
        // (beta_n x)^2, so the point-force compliance SUM phi_n(x0)^2/m_n has
        // terms growing with frequency and does not converge. Computed at
        // x0/L = 0.05 the effective mass runs 13557, 378, 12.3, 1.96, 0.585,
        // 0.249, 0.083, 0.036 beam-masses at 1, 2, 4, 6, 8, 10, 14 and 20 modes
        // -- still falling. A point strike near a clamp therefore has no
        // physical effective mass; it has a mode-count artefact. That is
        // precisely why the same collision reproduced with two modes measures
        // correctly while the full bank does not.
        //
        // A finite patch converges it. The tip is a compliant blob of neoprene
        // a centimetre across; it cannot resolve a mode whose half-wavelength is
        // shorter than itself, and the pressure it applies is spread over the
        // same area it senses. Spatially averaging a mode over a patch of width
        // W multiplies it by sinc(beta_n W / 2).
        //
        // Falaize & Helie use 1 cm on a 7.83 cm tine -- 12.8% of the length --
        // and Chabassier et al. grade a piano's from a few millimetres to nearly
        // two centimetres. A fixed tip diameter across a compass whose tines run
        // from 180 mm to 20 mm gives the same graduation for free.
        //
        // The weighting MUST be identical for reading and for pushing. Two
        // papers state this as a requirement for energy conservation, not as
        // advice. Measured on this collision, the energy a hammer injects goes
        // from 2.25x its own kinetic energy with symmetric point weights, to
        // 1.09x with a symmetric 20% patch, to 1.70x if read and write are
        // allowed to differ -- and to 3.72x with a temporal filter on the
        // velocity, which is the runaway this model already found the hard way.
        const double patchW = std::clamp (kTipWidth / std::max (1.0e-3, tineLength), 0.02, 0.35);

        // How long the tip rests on the tine, in cycles of the note. This is
        // the temporal half of the same argument the patch makes spatially: a
        // hammer in contact for a good fraction of a period cannot put energy
        // into a mode oscillating many times faster, because the force reverses
        // underneath it before the mode has finished a swing.
        const double dwellCycles = 0.9;

        for (int m = 0; m < kTineModes; ++m)
        {
            const double z = 0.5 * CantileverModes::betaL (m) * patchW;
            double w = std::abs (z) < 1.0e-9 ? 1.0 : std::sin (z) / z;

            // Together, the patch and the dwell are what actually keep the
            // inharmonic modes quiet.
            //
            // A point impulse on a uniform beam predicts the second mode
            // starting about sixteen decibels below the first. Measured on real
            // instruments it is twenty to forty decibels below THAT -- the tine
            // is not uniform, it carries a tuning spring part way along, and
            // the hammer is a soft blob rather than a point. In the one
            // published Rhodes simulation, no inharmonic partial anywhere
            // exceeds -46 dB.
            //
            // This is what makes a struck tine settle to a sinusoid within ten
            // to fourteen milliseconds, and it is NOT the damping. No decay
            // rate anyone deploys is fast enough to take a partial that starts
            // loud down to inaudible in that time. It has to have been quiet
            // from the start.
            const double dwell = CantileverModes::ratio (m, mu, springPos, shearNumber) * dwellCycles;
            w *= std::exp (-0.5 * dwell * dwell * 0.55);

            shapeTipV[kV0 + m]    = shapeMode[m][kTip];
            shapeTipH[kH0 + m]    = shapeMode[m][kTip];
            shapeStrikeV[kV0 + m] = shapeMode[m][kStrike] * w;
            shapeClamp[kV0 + m]   = shapeMode[m][kBlock];
        }

        // ---- the joint --------------------------------------------------------
        // eta = u_tine(block) - u_bar(end); the quadratised potential of a
        // linear spring is a perfect square, so psi = sqrt(Ks) * eta exactly and
        // the gradient is the constant sqrt(Ks) * c.
        jointActive = ks > 0.0;
        if (jointActive)
        {
            const double rk = std::sqrt (ks);
            for (int m = 0; m < kTineModes; ++m) jointGrad[kV0 + m] =  rk * shapeMode[m][kBlock];
            for (int m = 0; m < kBarModes;  ++m) jointGrad[kB0 + m] = -rk * barShape[m];
        }

        // ---- hammer ------------------------------------------------------------
        // Neoprene, heavier in the bass. The exponent near two is a rounded tip
        // on round wire. The stiffness is calibrated so the contact lasts the
        // 6.42 ms that was measured on a real instrument, which is what keeps
        // the tine's own motion sinusoidal -- a contact that long is a hard
        // lowpass on the strike, and it is the reason the overtones the beam
        // could support never actually get excited.
        // A Rhodes hammer is a small moulded arm with a neoprene tip, and what
        // matters is its effective mass at the tip after the pivot reduction.
        //
        // Graduated against the tine rather than against the note number: a
        // treble tine is a stub of wire weighing a fraction of a gram, and a
        // hammer that outweighs it by twenty to one does not excite it, it
        // simply knocks it out of the way -- contact collapses to a few tenths
        // of a millisecond, which is a broadband impulse, and the top two
        // octaves come out with erratic multi-millimetre swings. Tying the
        // hammer to the effective mass it actually meets at the strike point
        // keeps the collision in the same regime from one end of the keyboard
        // to the other.
        const double phiStrike = shapeMode[0][kStrike];
        const double effTineMass = modalMass / std::max (1.0e-6, phiStrike * phiStrike);
        // What is graduated is the MASS RATIO of the collision, and it is one
        // number and one shape: it rises across the bottom two-fifths of the
        // compass to 0.30 of whatever the tine presents at the strike point,
        // and stays there.
        //
        // With the tine geometry carrying its tuning spring the effective mass
        // itself only spans 35.6 g to 11.6 g -- close to the twofold the
        // service manual's rail curve is designed to produce -- so the ratio
        // is now the whole of the graduation, where under the old
        // bare-cantilever lengths it was a ceiling in kilograms fighting a
        // fifteenfold ramp (114 g down to 7.3 g) that the geometry error had
        // manufactured.
        //
        // It is a calibration and not a claim about the moulding: a real
        // Rhodes hammer is one part across the compass and the manual
        // graduates the TIPS. What the reference rows pin is the collision,
        // and with the geometry corrected they pin it from both sides with
        // room to spare -- the bass ratio holds fail=0 anywhere in 0.065 to
        // 0.080, where the old geometry left a window 0.3 g wide. Above that
        // E2's soft octave dominance (A3) crosses its -10 dB ceiling; below
        // it E2 and E3 settle too clean for the inharmonic floor (C2). The
        // value sits near the top of the window because the bass rise (B6)
        // grows monotonically with it -- +1.15 dB/s at 0.065 against
        // +1.59 at 0.080 -- and A3 still keeps 0.3 dB of margin here.
        //
        // The rise is quadratic in register rather than linear because a
        // straight ramp steep enough to reach the mid from a bass light
        // enough for A3 makes the third and fourth notes of the compass swing
        // further than the first.
        const double u     = std::min (1.0, reg / 0.60);
        const double ratio = 0.075 + 0.225 * u * u;
        hammerCfg.mass = std::max (0.00060, ratio * effTineMass)
                       * (0.6 + 0.8 * cfg.hammerMassNorm);
        hammerCfg.alpha     = 1.85 + 0.5 * cfg.hammerHardness;
        // Not graduated across the keyboard, and this is the one place the
        // model knowingly departs from the service manual rather than from a
        // guess. The manual's tip table graduates the TIPS -- Shore A 30 for
        // hammers 1 to 30, then 50, 70, 90, then wrapped -- which through
        // Gent's relation is an eighteenfold modulus span, and that, not the
        // moulding's weight, is where the real per-register voicing lives.
        //
        // It was measured here on the corrected geometry, and the shape it
        // buys is right: contact time stops being flat and falls with pitch,
        // 4.3 ms at E1 to 1.8 ms at E6, where this flat law runs 3.4 ms at E1
        // and 4.0 ms at E6 -- the wrong direction for any keyboard. What has
        // no source is the SCALE. The Gent ratio is a shape; which Shore band
        // keeps the calibrated stiffness below is a free constant, the 6.42 ms
        // contact figure is a single unweighted average over unstated notes
        // and cannot pin it, and swept against the suite only Shore A 58 to 61
        // holds fail=0 -- A4 at E3 above it, G4 below. A two-point window on a
        // twenty-point scale is not a calibration, and it costs the swing span
        // and G2 to buy a contact-time shape no row measures. Left flat until
        // the stiffness scale has its own measurement behind it; the numbers
        // are in docs/acoustic-checklist.md.
        hammerCfg.stiffness = 6.0e6 * std::pow (12.0, cfg.hammerHardness - 0.5);
        // MEASURED, AND KNOWINGLY LEFT: lambda is about ten times what
        // Hunt-Crossley's own 3 (1 - e) / (2 v) allows. The tip arrives at
        // 4.6 m/s at ff, so this 1.6 would need a restitution of -3.3. The
        // cost is real: (1 + lambda ddot(delta)) reaches -1.65 and 29% of
        // an ff contact's samples have their force clamped to zero by the
        // guard in HuntCrossleyHammer, against 4% at mf, so the tip's
        // dynamic response is partly shaped by that clamp rather than by
        // the neoprene. The grand carried the same error and correcting it
        // there bought an audible, measurable improvement.
        //
        // Here it is not a drop-in. The tine is calibrated AROUND this
        // lambda: pulling it toward physical costs 3 reference rows at
        // x0.4 and 7 at x0.15. Solving the term implicitly -- the grand's
        // other half -- does smooth the force, three force reversals per
        // contact down to one, but it does not touch the clamping, which
        // is lambda's doing; it moves the instrument by 22 dB of
        // difference and costs row C2 at E5, for a defect it does not fix.
        // So this needs its own campaign: lambda to its restitution value,
        // with the stiffness and the dwell shaping re-fitted alongside it
        // against the B/C/G rows, not one constant at a time. Until then
        // it stays exactly as measured.
        hammerCfg.lambda    = 2.4 - 1.6 * cfg.hammerHardness;
        {
            const HammerCover hc = hammerCover (std::clamp (static_cast<int> (cfg.hammerMat + 0.5), 0, 5));
            hammerCfg.stiffness *= hc.stiff;
            hammerCfg.alpha      = std::clamp (hammerCfg.alpha + hc.alphaAdd, 1.2, 3.5);
            hammerCfg.lambda    *= hc.lambda;
        }

        // Stability guard on the contact.
        //
        // The contact is integrated explicitly, which is safe only while the
        // collision's own resonance stays well below the sample rate. In the
        // bass it is nowhere near: a 6.4 ms contact is a resonance under a
        // hundred hertz. But the reduced mass of the collision collapses
        // toward the treble -- a top-octave tine has a modal mass of a
        // twentieth of a gram -- and the contact resonance climbs with the
        // inverse cube root of it. Past a fraction of the sample rate the
        // explicit contact stops being a contact and becomes a generator: one
        // note ran the tine out to over a metre of deflection.
        //
        // For a quadratic contact, energy balance gives peak compression
        // d = (3 m v^2 / 2k)^(1/3) and hence a contact resonance
        // w = (2^(2/3) 3^(1/3))^(1/2) * (k v / m)^(1/3), so capping w caps k.
        // The treble tips end up softer than the specification asks for, which
        // is audible as a slightly rounder attack up there and is stated in
        // the documentation rather than hidden.
        //
        // The principled fix is to carry the elastic part of the contact
        // through the quadratised path like the other nonlinearities -- its
        // potential k/(alpha+1) * d^(alpha+1) is non-negative and therefore
        // admissible -- at which point the time step stops constraining it at
        // all. The system has spare term slots for exactly that.
        diag.tineLength    = tineLength;
        diag.modalMass     = modalMass;
        diag.effTineMass   = effTineMass;
        diag.hammerMass    = hammerCfg.mass;
        diag.stiffnessWant = hammerCfg.stiffness;
        {
            const double mRed = 1.0 / (1.0 / hammerCfg.mass + 1.0 / effTineMass);
            const double vRef = 2.0;                       // a firm blow, m/s
            const double wMax = 2.0 * kPiD * 0.06 * fs;    // contact resonance ceiling
            const double kMax = mRed / vRef * std::pow (wMax / 1.5305, 3.0);
            diag.capBinding = hammerCfg.stiffness > kMax;
            if (hammerCfg.stiffness > kMax) hammerCfg.stiffness = kMax;
        }
        diag.stiffnessUsed = hammerCfg.stiffness;



        // ---- damper -------------------------------------------------------------
        // Graduated across the keyboard on the real instrument: long wide felts
        // in the bass, because a long tine carries far more energy.
        const double grip = std::clamp (cfg.damperGrip, 0.0, 1.0);
        const double damperT60 = (0.30 - 0.24 * grip) * (1.0 + 1.4 * (1.0 - reg));
        damperFactor = std::exp (-3.0 * std::log (10.0) / (damperT60 * fs));
        {
            const int f = std::clamp (static_cast<int> (cfg.damperFelt + 0.5), 0, 3);
            feltWLo = (f == 1) ? 1.25 : (f == 2) ? 0.55 : 1.0;
            feltWHi = (f == 1) ? 1.25 : (f == 2) ? 0.55 : (f == 3) ? 0.12 : 1.0;
        }
        setPedal (pedalAmt);   // recompute both grip bands

        // ---- pickup placement -----------------------------------------------------
        // Moved to, not jumped to.
        //
        // The coil differentiates: emf is the RATE of flux change, which is
        // Faraday's law and not negotiable. So a step in where the pole sits
        // arrives at the output multiplied by the sample rate, however small
        // the step is. Measured, moving the voicing screw the width of a preset
        // change put an 18 dB spike on top of a sounding note -- which is the
        // click heard when changing presets.
        const double halfWidth = field != nullptr ? field->halfWidth() : 3.0e-3;
        staticOffsetT = std::clamp (cfg.pickupOffset + pkHOff, -1.0, 1.0) * halfWidth;
        // The gap. Its scale was questioned and then measured, because the
        // reasoning against it was good: what decides whether the instrument
        // growls is the gap against the pole's own features -- a flat 0.28 mm
        // across, a wedge 0.95 mm deep -- and on that argument a default of
        // 2.1 mm sits out where the field is smooth, which is why a bass note's
        // spectrum has died by its eighth harmonic.
        //
        // Closing it does exactly what the argument predicts, and it is still
        // wrong. At 0.6 mm the hard-struck bass fundamental rises at 2.8 dB/s
        // against a measured 2.2 to 3.9, which is the row it was meant to fix.
        // It also breaks nine others: the tine is swinging two pole-widths, so
        // it leaves the field entirely twice a cycle, and the spike that makes
        // puts the inharmonic floor up by 45 dB, starts the fundamental beating
        // against itself, and collapses the middle-register attack to half a
        // millisecond. Swept against the whole suite the best voicing is within
        // a hair of this one.
        //
        // So the growl is not sitting behind this control, and the range stays
        // as it was. What the sweep is really saying is that the bass tine
        // swings too far for the pole it is crossing, which is upstream of the
        // pickup entirely.
        staticGapT   = 0.6e-3 + 4.4e-3 * std::clamp (cfg.pickupGapNorm + pkGOff, 0.0, 1.0);
        // The first time round there is nothing to glide from, so it snaps.
        if (! configured) { staticOffset = staticOffsetT; staticGap = staticGapT; }
        refreshPickup();

        // Where the field stops being usefully curved, and where the energy is
        // too small for the quadratised terms to matter.
        linearSwing  = 0.04 * halfWidth;
        quietEnergy  = 1.0e-13;

        // The stretching term, and why it is a small fraction of EA/L rather
        // than EA/L itself.
        //
        // The Kirchhoff term Pfeifle carries assumes bending the beam also
        // STRETCHES it, which is true of a string, or of a beam held at both
        // ends. A tine is held at one end and free at the other, and a free end
        // simply draws inward as the beam bends. It cannot sustain the axial
        // tension the full term assumes.
        //
        // Left at full strength the error is not subtle: it put a fortissimo
        // low E forty-six cents sharp at the strike, gliding back to pitch over
        // a second and a half as the note decayed. In a chord, where every note
        // glides by a different amount, that is heard as the whole instrument
        // phasing and detuning against itself -- which is exactly what it
        // sounded like.
        //
        // What is left is the residual constraint that is really there: the
        // tine is shrunk into an aluminium block rather than pinned, and it
        // carries a tuning spring part way along. That is a few percent of full
        // axial restraint, and it produces the few cents of downward glide a
        // real tine shows as it decays, rather than a quarter tone.
        //
        // (The larger nonlinearity of a truly inextensional cantilever is in
        // the curvature and the axial inertia, not the membrane, and for the
        // first mode it SOFTENS rather than stiffens. It is not modelled here;
        // this term is deliberately small enough that the distinction stays
        // below the level anybody could hear.)
        constexpr double kAxialRestraint = 0.035;
        stretchEA   = kAxialRestraint * mat.youngs * area
                    / std::max (1.0e-4, tineLength);
        // The knob spans zero to twice the nominal restraint, with the
        // default in the middle at the measured value -- the bass glide this
        // produces at the default is a few cents where the reference
        // instrument shows nine to twenty-nine, so the top half of the knob
        // is the honest range, not an exaggeration.
        stretchGain = 2.0 * std::clamp (cfg.nonlinearity, 0.0, 1.0);

        // Reconfiguring changes what the quadratised terms MEAN, so their
        // auxiliary variables no longer describe the system they belong to.
        //
        // psi is sqrt(2V) of the current state under the current potential.
        // This function has just changed the joint's stiffness, the tine's
        // stretching constant and every mode's frequency -- all of which
        // change V -- while psi kept the value it had under the old one. The
        // gradient then acts with the new definition against a stale psi, and
        // the term stops being the passive thing it was proved to be: measured,
        // sweeping the tuning spring, the joint stiffness and the bar's tuning
        // together ran the instrument to a non-finite state within ten seconds,
        // reliably, and no one of them did it alone.
        //
        // Disabling them is the resync: updateStretchTerm re-enables them on
        // the next sample and seeds psi from the state as it now is. Same
        // reason the damper does it, and the same lesson -- an auxiliary
        // variable is only valid against the definition it was seeded under.
        sys.disableTerm (kTermStretch);
        sys.disableTerm (kTermJoint);

        // The transducer this voice renders through. Native is the coil.
        {
            const int t = static_cast<int> (cfg.transducer + 0.5);
            trans = (t == 1 || t == 0) ? 0 : t;
        }
        // The contact transducer reads the inertial force the assembly puts
        // into its mount: sum of phi_clamp * omega^2 * modal mass over the
        // tine's vertical modes -- what a contact microphone bolted to the
        // harp actually hears.
        for (int m = 0; m < kNumModes; ++m) shapeContact[m] = 0.0;
        for (int m = 0; m < kTineModes; ++m)
        {
            const double w = 2.0 * kPiD * tineFreq[m];
            // Mode m's OWN generalised mass, not the fundamental's. The
            // spring loads each mode by its own shape at the spring, so the
            // masses differ by up to 2.3x across the set, and using the
            // fundamental's for all of them over-weighted the overtones
            // against it -- which a contact pickup hears directly, since the
            // high modes are what answer a knock at the clamp. Invisible to
            // every suite until the transducer is switched to contact, which
            // three shipped presets do.
            shapeContact[kV0 + m] = shapeClamp[kV0 + m] * w * w * modeMass[m];
        }

        configured = true;
        EPI_CHK ("configure exit");
    }

    // psi = sqrt(EA/L)/2 * K, g = sqrt(EA/L) * (Ghat q). No division and no
    // singularity at rest: the 1/sqrt(2V) that quadratisation would normally
    // introduce cancels identically, because this potential is a perfect
    // square.
    // The frequency the assembled fork actually sounds, for the SCHEME AS IT
    // RUNS -- not for the continuous system it approximates. Three things have
    // to be in the equation or the trim lands the note off pitch, each one
    // found by measuring the discrete system against the solve:
    //
    //   The BAR MODES. The joint's force is the stiffness times the
    //   difference across it, so the bar's compliance at its end of the
    //   spring sets how much of that stiffness the tine feels. Solved over
    //   the tine alone the shift comes out wrong by whatever the bar absorbs
    //   -- measured, minus four to plus five cents across the compass. The
    //   real instrument tunes the assembled fork for the same reason.
    //
    //   The MODES THE INTEGRATOR DROPS. setMode kills anything at or above
    //   fs/pi, so the block compliance those modes would have provided does
    //   not exist at run time and must not be counted here -- a treble note
    //   loses five of its eight beam modes to the budget, worth about a cent.
    //
    //   The DISCRETE RESPONSES. The joint is carried by the quadratised
    //   trapezoidal path, so what the fundamental meets is not c^2/(w^2 - l)
    //   but each mode's actual z-domain response, damping included -- and the
    //   damping is not decoration: at A5 a bar overtone with a tenth-second
    //   T60 sits sixty-five cents under the note, and treating that broad
    //   resonance as a sharp pole leaves the note three and a half cents
    //   flat. The characteristic equation of the stepped system, with theta
    //   the frequency in radians per sample, is
    //
    //     1 + cos^2(theta/2) k^2 SUM (c_i^2/m_i) r_i / D_i(theta) = 0,
    //     D_i = e^{i theta} - (cA_i - cK_i) + cB_i e^{-i theta},
    //
    //   with cA/cB/cK the leapfrog's own coefficients and cos^2(theta/2) the
    //   trapezoid's averaging of the joint force across the step. Its root is
    //   complex because the coupled mode decays; the sounding frequency is
    //   the real part, found by a bisection on the real axis (where the
    //   fundamental's pole still forces a sign change) polished by a few
    //   complex Newton steps. Verified against the running integrator: the
    //   root matches the rendered pitch to a thousandth of a cent at the
    //   note this was worst at.
    double assembledFundamental (const double* freq, const double* t60,
                                 const double* tineMass, double barF0, double barMass,
                                 double damp, double rootKs, double trim) const
    {
        constexpr int kAll = kTineModes + kBarModes;
        const double k = 1.0 / fs;

        // Per-mode step coefficients, exactly as cacheStep builds them.
        double thPole[kAll], cAK[kAll], cB[kAll], gain[kAll];
        int n = 0;
        auto addRow = [&] (double f, double t60s, double mass, double c)
        {
            if (! (f > 0.0) || f >= fs * System::kModeBudget) return;   // the budget kills it
            const double w  = 2.0 * kPiD * f;
            const double sT = (t60s > 1.0e-5 ? 6.9078 / t60s : 1.0e6) * k;
            const double a  = std::tanh (sT);
            const double r  = 1.0 / (1.0 + a);
            const double cK = (2.0 - 2.0 * std::cos (w * k) / std::cosh (sT)) * r;
            thPole[n] = w * k;
            cAK[n]    = 2.0 * r - cK;
            cB[n]     = (1.0 - a) * r;
            gain[n]   = k * k * (c * c / mass) * r;
            ++n;
        };
        for (int m = 0; m < kTineModes; ++m)
            addRow (freq[m] * trim, t60[m], tineMass[m], rootKs * shapeMode[m][kBlock]);
        // The bar's own modes, at the same frequencies, dampings and coupling
        // weights configure() gives them. The trim does not apply: the bar is
        // not being retuned when the tine is cut.
        for (int m = 0; m < kBarModes; ++m)
            addRow (barF0 * CantileverModes::ratio (m, 0.0, 1.0, 0.004),
                    (0.10 + 0.14 * (1.0 - damp)) / (1.0 + 1.8 * m),
                    barMass, rootKs * CantileverModes::shape (m, 0.10));
        if (n < 2) return freq[0] * trim;

        auto secular = [&] (std::complex<double> th)
        {
            const std::complex<double> ei = std::exp (std::complex<double> (0.0, 1.0) * th);
            const std::complex<double> cs = std::cos (0.5 * th);
            std::complex<double> v = 1.0;
            for (int m = 0; m < n; ++m)
                v += cs * cs * gain[m] / (ei - cAK[m] + cB[m] / ei);
            return v;
        };

        // Bracket on the real axis: just above the tine fundamental's pole,
        // just below the nearest pole above it -- usually the tine's second
        // mode, but a bar overtone can sit closer. The fundamental's own pole
        // is essentially undamped, so the real part still runs to -infinity
        // at the lower end; if damping has smoothed away the sign change at
        // the upper end the bracket is abandoned rather than trusted.
        const double thBase = thPole[0];
        const double lo0 = thBase * (1.0 + 1.0e-12);
        // The far wall of the bracket is usually the nearest pole above the
        // fundamental -- but a heavily damped bar overtone can sit just
        // above it (note 32: 0.7% up) and smooth the sign change away on
        // its near side. The assembled root still exists past that pole, so
        // the wall walks outward, pole by pole, to the first one whose near
        // side shows the sign change; only running out of poles abandons to
        // the bare frequency. Returning bare from the near-wall failure was
        // a shipped +44 cent mistune: the trim loop then tuned the BARE
        // mode to f0 and the live joint added its full shift on top.
        double cand[kAll + 1];
        int nc = 0;
        for (int m = 1; m < n; ++m)
            if (thPole[m] > thBase) cand[nc++] = thPole[m];
        cand[nc++] = kPiD;
        std::sort (cand, cand + nc);
        double lo = lo0, hi = -1.0, wall = kPiD;
        for (int i = 0; i < nc; ++i)
        {
            const double h = cand[i] - 1.0e-9 * (cand[i] - thBase);
            if (h > lo0 && secular ({ h, 0.0 }).real() > 0.0)
            {
                hi = h;
                wall = cand[i];
                break;
            }
        }
        if (hi < 0.0) return freq[0] * trim;

        for (int i = 0; i < 60; ++i)
        {
            // The half containing the sign change is the one whose midpoint
            // is still negative.
            const double mid = 0.5 * (lo + hi);
            if (secular ({ mid, 0.0 }).real() < 0.0) lo = mid; else hi = mid;
        }

        // Polish to the complex root; the imaginary part is the coupled
        // mode's decay and the real part is what sounds.
        std::complex<double> th (0.5 * (lo + hi), 0.0);
        for (int i = 0; i < 12; ++i)
        {
            const std::complex<double> f  = secular (th);
            const double h = 1.0e-9;
            const std::complex<double> df = (secular (th + std::complex<double> (h, 0.0))
                                           - secular (th - std::complex<double> (h, 0.0))) / (2.0 * h);
            if (! (std::abs (df) > 0.0)) break;
            const std::complex<double> step = f / df;
            th -= step;
            if (std::abs (step) < 1.0e-14) break;
        }
        // A Newton step that has wandered out of the bracket has found some
        // other root; the bisection value is the safe answer there.
        if (! std::isfinite (th.real()) || th.real() <= thBase || th.real() >= wall)
            th = { 0.5 * (lo + hi), 0.0 };
        return th.real() * fs / (2.0 * kPiD);
    }

    void updateStretchTerm()
    {
        // Both quadratised terms are quadratic in amplitude. On a tine moving
        // by microns they are below the precision of everything around them,
        // and carrying them costs the whole rank-two solve.
        if (sys.energy() < quietEnergy)
        {
            sys.disableTerm (kTermStretch);
            sys.disableTerm (kTermJoint);
            // With the joint down the mode must carry the assembled pitch
            // itself, or the note goes flat by the joint's whole shift.
            if (jointTuned) { sys.setFrequency (kV0, assembledF0); jointTuned = false; }
            return;
        }

        if (stretchGain <= 0.0 || ! configured)
        {
            sys.disableTerm (kTermStretch);
        }
        else
        {
            const double scale = stretchEA * stretchGain;
            const double root  = std::sqrt (scale);

            double gq[2][kTineModes];
            double kk = 0.0;
            for (int a = 0; a < kTineModes; ++a)
            {
                double sv = 0.0, sh = 0.0;
                for (int b = 0; b < kTineModes; ++b)
                {
                    sv += gHat[a][b] * sys.displacement (kV0 + b);
                    sh += gHat[a][b] * sys.displacement (kH0 + b);
                }
                gq[0][a] = sv; gq[1][a] = sh;
            }
            for (int a = 0; a < kTineModes; ++a)
                kk += sys.displacement (kV0 + a) * gq[0][a]
                    + sys.displacement (kH0 + a) * gq[1][a];

            for (int i = 0; i < kNumModes; ++i) stretchGrad[i] = 0.0;
            for (int a = 0; a < kTineModes; ++a)
            {
                stretchGrad[kV0 + a] = root * gq[0][a];
                stretchGrad[kH0 + a] = root * gq[1][a];
            }
#ifdef EPI_NAN_TRACE
            if (! std::isfinite (root) || ! std::isfinite (kk))
                std::printf ("    STRETCH bad: root %g  kk %g  stretchEA %g  gain %g\n",
                             root, kk, stretchEA, stretchGain);
#endif
            sys.setTerm (kTermStretch, stretchGrad, 0.5 * root * kk, true);
        }

        if (jointActive)
        {
            // The term is about to carry the joint again: give the shift back
            // to it, or it would be applied twice.
            if (! jointTuned) { sys.setFrequency (kV0, bareF0); jointTuned = true; }
            double eta = 0.0;
            for (int i = 0; i < kNumModes; ++i) eta += jointGrad[i] * sys.displacement (i);
#ifdef EPI_NAN_TRACE
            if (! std::isfinite (eta))
            {
                std::printf ("    JOINT bad: eta %g\n      grads:", eta);
                for (int i = 0; i < kNumModes; ++i)
                    if (! std::isfinite (jointGrad[i])) std::printf (" g[%d]=%g", i, jointGrad[i]);
                std::printf ("\n      disps:");
                for (int i = 0; i < kNumModes; ++i)
                    if (! std::isfinite (sys.displacement (i)))
                        std::printf (" q[%d]=%g", i, sys.displacement (i));
                std::printf ("\n      gHat non-finite:");
                for (int a = 0; a < kTineModes; ++a)
                    for (int b = 0; b < kTineModes; ++b)
                        if (! std::isfinite (gHat[a][b])) std::printf (" [%d][%d]=%g", a, b, gHat[a][b]);
                std::printf ("\n      stretchGrad non-finite:");
                for (int i = 0; i < kNumModes; ++i)
                    if (! std::isfinite (stretchGrad[i])) std::printf (" s[%d]=%g", i, stretchGrad[i]);
                std::printf ("\n");
            }
#endif
            sys.setTerm (kTermJoint, jointGrad, eta, true);
        }
        else
        {
            sys.disableTerm (kTermJoint);
        }

    }

    // Put a diverged tine back, and record that it happened.
    void recover (double* fluxOut)
    {
        sys.clear();
        sys.disableTerm (kTermStretch);
        sys.disableTerm (kTermJoint);
        if (jointTuned) { sys.setFrequency (kV0, assembledF0); jointTuned = false; }
        hammer.reset();
        sounding = false;
        lockedOut = true;   // stays out until the next strike
        ++diverged;
        for (int k = 0; k < kOver; ++k) fluxOut[k] = satRestVal;
    }

    void applyDamper()
    {
        // Applied to the state directly. The factor is below one by
        // construction, so a damper can never become a source.
        for (int i = 0; i < kNumModes; ++i)
        {
            const double u = sys.displacement (i);
            if (u != 0.0) sys.scaleMode (i, sys.frequency (i) > 1200.0 ? damperEffHi : damperEff);
        }

        // And the quadratised terms have to be told, because they carry their
        // own energy and it is not in the modal state.
        //
        // psi is defined as sqrt(2V) of the CURRENT state. Forcing the state
        // down without touching psi leaves the two describing different
        // systems, and since the reported energy counts psi^2/2, it leaves a
        // floor that never decays: measured on a released bass note, the energy
        // fell four orders of magnitude in a quarter second and then sat at
        // 1.2e-7 forever. Nothing retires below that, so every note ever played
        // stayed live and fully processed for the rest of the session, and the
        // cost climbed until the host could not keep up.
        //
        // Disabling them here is the resync: updateStretchTerm re-enables them
        // on the next sample and seeds psi from the state as it now is.
        sys.disableTerm (kTermStretch);
        sys.disableTerm (kTermJoint);
    }

    double fs = 48000.0;
    const MagneticPickup* field = nullptr;

    System sys;
    HuntCrossleyHammer hammer;
    HuntCrossleyHammer::Config hammerCfg;

    double gHat[kTineModes][kTineModes] {};
    double shapeMode[kTineModes][3] {};
    double barShape[kBarModes] {};

    double shapeTipV[kNumModes] {}, shapeTipH[kNumModes] {}, shapeStrikeV[kNumModes] {};
    double shapeClamp[kNumModes] {};
    double stretchGrad[kNumModes] {}, jointGrad[kNumModes] {};

    double stretchEA = 0.0, stretchGain = 0.5;
    bool   jointActive = false;
    // The fundamental's two tunings: bare while the joint term carries its
    // shift, assembled when an economy tier has taken the joint down.
    double bareF0 = 440.0, assembledF0 = 440.0;
    bool   jointTuned = true;
    double damperFactor = 1.0;
    double damperEff = 1.0, damperEffHi = 1.0;
    double feltWLo = 1.0, feltWHi = 1.0;
    double pedalAmt = 0.0;
    double tineLength = 0.1;
    double geoLen = 1.0, geoDia = 1.0;      // the workshop's trims
    // Resolved transducer: 0 magnetic, 2 electro, 3 contact (native folds in).
    int trans = 0;
    double magCouple = 1.0;   // ferro = 1; bare conductor = faint eddy signal
    bool   matCond   = true;  // an insulator cannot be an electrostatic plate
    double shapeContact[kNumModes] {};
    double cfHist[3] {};
    static constexpr double kElectroOut = 2.5e-2;   // level-matched by probe
    static constexpr double kContactOut = 3.2e-2;
    double pkHOff = 0.0, pkGOff = 0.0;      // the pickup workshop's offsets
    double staticOffset = 0.0, staticGap = 1.5e-3, restFlux = 0.0;
    double staticOffsetT = 0.0, staticGapT = 1.5e-3;

    // Recompute everything that depends on where the pole is sitting.
    void refreshPickup()
    {
        restFlux = field != nullptr ? field->flux (staticOffset, staticGap) : 0.0;
        // The most flux this pole can present, used as the floor for the knee.
        peakFlux = field != nullptr ? std::abs (field->flux (0.0, staticGap)) : 1.0;
        updateRestSaturation();
    }

    // One step of the glide, about twenty milliseconds to settle. Short enough
    // that a voicing move still feels immediate, long enough that the
    // derivative the coil takes of it stays inside the note.
    void glidePickup()
    {
        const double a = 1.0 - std::exp (-1.0 / (0.035 * fs));
        const double dO = staticOffsetT - staticOffset;
        const double dG = staticGapT - staticGap;
        const double dS = satAmtT - satAmt;
        if (std::abs (dO) < 1.0e-12 && std::abs (dG) < 1.0e-12
            && std::abs (dS) < 1.0e-9) return;
        staticOffset += a * dO;
        staticGap    += a * dG;
        satAmt       += a * dS;
        refreshPickup();
    }
    double satK = 0.0, satAmt = 0.0, satAmtT = 0.0, satNorm = 1.0, peakFlux = 1.0;
    bool   satOn = false;

    // The curve at the resting operating point, and its slope there. A tine
    // that is barely moving sits within a hair of that point for the whole of
    // its life, so over the range it actually visits the curve is a straight
    // line and the line can be found once instead of a hyperbolic tangent per
    // sample per tine. With the pedal down that is eighty-eight of them.
    double satRestVal = 0.0, satRestSlope = 1.0;

    // The knee is set RELATIVE to the magnet's own bias, and the curve is then
    // normalised to unity slope at that bias.
    //
    // Without both of those the control is not a saturation control at all, it
    // is a fader. The iron sees the magnet's static flux as well as the tine's,
    // and a fixed knee means turning the control up slides the operating point
    // down the curve into the flat part, where the slope -- which IS the
    // signal's gain -- goes to nothing. Measured, the instrument lost 32 dB
    // between the control's two ends and the tone simply went away.
    //
    // Pinning the knee to the bias keeps the operating point in the same place
    // on the curve however hard the curve is bent, and normalising by the slope
    // there keeps the small-signal gain at unity. What is left for the control
    // to do is the only thing it should have been doing: bending the curve, so
    // that a tine swinging far enough to move the flux by an appreciable
    // fraction of the bias meets a different gain on the way out than on the
    // way back.
    void updateRestSaturation()
    {
        if (! satOn)
        {
            satK = 0.0; satNorm = 1.0;
            satRestVal = restFlux; satRestSlope = 1.0;
            return;
        }

        // A floor tied to the field's own peak, so a pickup voiced right out at
        // the edge of the pole -- where the resting flux is small -- does not
        // send the knee to infinity.
        const double scale = std::max (std::abs (restFlux), 0.05 * std::max (1.0e-12, peakFlux));
        satK = (0.2 + 1.6 * satAmt) / scale;

        // Normalised by the AVERAGE slope over the range the tine actually
        // visits, not by the slope at the bias point. The bias sits on the
        // shoulder, so its slope is the LOWEST the swing meets; normalising
        // there hands every excursion toward zero a gain above unity and the
        // instrument gets louder as the control comes up -- measured, by 8 dB,
        // which is the same usability problem as before with the sign flipped.
        //
        // Over the swing 0 to 2*bias the mean of sech^2(k phi) integrates to
        // tanh(2u)/2u with u = k*bias, so dividing by that leaves the average
        // gain unchanged and the control free to do nothing but bend the curve.
        const double u = satK * std::abs (restFlux);
        satNorm = u > 1.0e-9 ? (2.0 * u) / std::tanh (2.0 * u) : 1.0;

        const double th = std::tanh (satK * restFlux);
        satRestSlope = (1.0 - th * th) * satNorm;
        satRestVal   = th / satK * satNorm;
    }

    double saturate (double phi) const { return std::tanh (satK * phi) / satK * satNorm; }
    Collision diag;
    // How many times this tine has had to be put back. Reported by the tests.
    int diverged = 0;
    bool lockedOut = false;
    static constexpr double kReducedEnergy = 1.0e-10;
    double swingEnv = 0.0;
    bool   snapTransduction = true;   // see reset(): set up, not played
    double dormantV = 0.0, dormantSlope = 0.0;
    double linearSwing = 1.0e-4, quietEnergy = 1.0e-13;
    bool   reduced = false;

    int  note = 60;
    bool sounding = false, held = false, configured = false;
    double lastTipV = 0.0, lastTipH = 0.0;
    double vHist[3] {}, hHist[3] {};
    float  trace[kTraceLen] {};
    int    traceIdx = 0, traceDecim = 8, traceCount = 1;
    double noteHz = 440.0;
    bool   strikeFlag = false;
    int  controlCounter = 0;
    int  fadeLeft = 0, pendingNote = -1;
    double fadeFactor = 1.0, pendingVel = 0.0;
    std::uint32_t pendingSeed = 0;
    Rng  rng { 0x12345u };
};

} // namespace epi
