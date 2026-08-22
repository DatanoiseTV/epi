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
#include "PickupMagnetic.h"
#include "ModalCore.h"

#include <type_traits>

namespace epi
{

// ---------------------------------------------------------------------------
// One Wurlitzer 200A note: a solder-loaded steel reed and an electrostatic
// pickup.
//
// Everything here executes docs/wurlitzer-implementation-plan.md against the
// measurements in docs/research/wurlitzer-200a.md. The finding that decides
// the architecture: the reed is ONE mode -- real 200A spectra are harmonic to
// within 0.1 cent out to the 20th partial, which no cantilever mode series can
// be -- and the harmonics come from the pickup's 1/(gap - y) capacitance law.
// A modal bank at harmonic ratios would double-count. Same shape as the
// Rhodes: the resonator supplies frequency and envelope, the transducer the
// timbre; the field table is swapped for a capacitance law.
//
// What the voice carries:
//
//   - ONE polarisation. The reed is a flat strip (width/thickness 4.8-7.5),
//     so the edgewise fundamental sits 5-7x above flapwise -- no
//     near-degenerate pair to beat -- and edgewise motion runs along the
//     symmetric slot edges where dC/dy vanishes by symmetry.
//   - THREE modes: the fundamental and mechanical modes 2-3 for the attack
//     chirp, heavily suppressed by the contact; plus a slow gain modulation
//     of the transduced output for the measured 2.4 Hz AM (mechanism decided
//     by the reference suite's own K2 row -- see the class comment below).
//   - The tuning IS the solder: the tip-mass ratio mu is solved per note from
//     the pitch, exactly as a tech adds or files solder. Master tune and bend
//     re-solve mu; nothing applies a frequency offset to fixed geometry.
//   - The pickup is electrostatic: i = u0 * dC/dt with C = C0/(1 - y). The
//     asymmetry of y/(1-y) IS the bark, and driving it with displacement
//     relative to the physical gap is what makes the bass bark and the treble
//     not, from one law, with no per-key hand-tuning.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The reed's eigenvalue problem: a clamped-free beam with a point mass (the
// solder blob) at the free end. The characteristic equation is
//
//   1 + cos(b)cosh(b) + b*mu*(cos(b)sinh(b) - sin(b)cosh(b)) = 0
//
// with mu the tip mass in beam masses. Solved fresh for this model --
// openwurli's beta2 table is 18% high at mu = 0.5 and its solver does not
// converge to the clamped-pinned limit. Verified both ways: mu = 0.5 gives
// f2/f1 = 8.382, and as mu grows without bound beta2 tends to 3.9266, the
// clamped-pinned root.
//
// Tip mass pushes the partials AWAY from harmonic (f2/f1 rises from 6.267
// toward 8-14): no value of mu makes a reed harmonic, which is the
// architecture's licence for the one-mode claim above.
// ---------------------------------------------------------------------------
struct WurliReed
{
    // How much of the catalog length disappears under the reed-bar screw.
    //
    // The catalog ruler law and a uniform 0.020 in tongue cannot both be
    // taken at face value: a 74.9 mm tongue rings bare near 74 Hz (fine for
    // A1 = 55 Hz) but a 25.4 mm tongue rings bare near 640 Hz and C7 =
    // 2093 Hz is unreachable by any mu >= 0. Part of the catalog length is
    // clamped. One global clamp offset is the calibration length the plan
    // names (order 10 mm); 13 mm is the smallest round value that keeps the
    // whole A1-C7 compass reachable at the thin 0.020 in tongue -- the
    // binding notes are around reed 50, where the ruler law's fixed step is
    // most generous relative to pitch -- so every real reed carries solder,
    // as real reeds do.
    static constexpr double kClampOffset = 13.0e-3;   // m
    static constexpr double kMinFreeLen  = 8.0e-3;    // m, extrapolation floor

    // Tongue thickness, early vs late 200A reeds. The tipMass control maps
    // between them with mu re-solved per note: pitch stays put, and what
    // moves is the attack chirp, the strike mass and the contact -- the real
    // difference between reed generations, not a fake.
    static constexpr double kThicknessEarly = 0.020 * 0.0254;   // m
    static constexpr double kThicknessLate  = 0.026 * 0.0254;   // m

    struct Solve
    {
        double mu     = 0.0;    // solder tip mass, beam masses
        double beta1  = 1.8751040687119611;
        double beta2  = 4.6940911329741746;
        double beta3  = 7.8547574382376126;
        double r2     = 6.267;  // f2/f1
        double r3     = 17.548; // f3/f1
        double f0     = 0.0;    // Hz, what the solved reed actually rings at
        double freeLen  = 0.0;  // m
        double beamMass = 0.0;  // kg, rho*A*L of the bare tongue
        double fBare  = 0.0;    // Hz, the tongue with no solder at all
        bool   ground = false;  // pitch above the bare tongue: solder removed
                                // entirely and the tongue ground to pitch
    };

    static double charEq (double b, double mu)
    {
        return 1.0 + std::cos (b) * std::cosh (b)
             + b * mu * (std::cos (b) * std::sinh (b) - std::sin (b) * std::cosh (b));
    }

    // One root of the characteristic equation inside a bracket where the sign
    // changes. The brackets below are chosen from the limits: as mu runs
    // 0 -> infinity, beta1 falls from 1.8751 toward 0, beta2 from 4.6941 to
    // the clamped-pinned 3.9266, beta3 from 7.8548 to 7.0686.
    static double root (double lo, double hi, double mu)
    {
        double flo = charEq (lo, mu);
        for (int i = 0; i < 90; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            const double fm = charEq (mid, mu);
            if ((fm > 0.0) == (flo > 0.0)) { lo = mid; flo = fm; }
            else                             hi = mid;
        }
        return 0.5 * (lo + hi);
    }

    // Catalog reed length, from the exact ruler law the parts tables follow:
    // reed 1 = 2 19/20 in, each 1/20 in shorter to reed 20 = 2 in; reed 21 =
    // 1 43/44 in, each 1/44 in shorter to reed 64 = 1 in. Extrapolated at the
    // same pitch-per-step outside the real A1-C7 compass, because the engine
    // keeps its full A0-C8 note array as the other instruments do.
    static double catalogLengthM (int reedIndex)
    {
        double inches;
        if (reedIndex <= 20)      inches = 2.95 - (reedIndex - 1) / 20.0;
        else                      inches = (1.0 + 43.0 / 44.0) - (reedIndex - 21) / 44.0;
        return std::max (0.55, inches) * 0.0254;
    }

    // Reed width, 0.151 in bass to 0.096 in treble, linear over the compass.
    // Width sets the beam mass, never the frequency.
    static double widthM (int reedIndex)
    {
        const double t = std::clamp ((reedIndex - 1) / 63.0, 0.0, 1.0);
        return (0.151 + (0.096 - 0.151) * t) * 0.0254;
    }

    // Solve one reed: geometry from the ruler law, then the solder mass from
    // the pitch -- the tech's add-or-file-solder move, done by bisection.
    // f(mu) = fBare * (beta1(mu)/beta1(0))^2 is strictly decreasing in mu,
    // so the bracket is trivial. Where the target sits above the bare tongue
    // (possible only in the extrapolated top range), the solder is zero and
    // the tongue is taken as ground to pitch, which is also what the factory
    // did to thin the treble.
    static Solve solve (int midiNote, double targetHz, double thicknessM)
    {
        Solve s;
        const int idx = midiNote - 32;   // reed 1 at MIDI 33 (A1)
        s.freeLen = std::max (kMinFreeLen, catalogLengthM (idx) - kClampOffset);

        // f1 = beta1^2/(2 pi L^2) * sqrt(EI/(rho A)), I/A = t^2/12 for the
        // rectangular section, so f = beta1^2 * t * sqrt(E/rho) / (4 pi sqrt(3) L^2).
        const double cSteel = std::sqrt (static_cast<double> (kSpringSteel.youngs)
                                       / static_cast<double> (kSpringSteel.density));
        const double b0 = 1.8751040687119611;
        s.fBare = b0 * b0 * thicknessM * cSteel
                / (4.0 * kPiD * std::sqrt (3.0) * s.freeLen * s.freeLen);
        s.beamMass = static_cast<double> (kSpringSteel.density)
                   * widthM (idx) * thicknessM * s.freeLen;

        if (! (targetHz > 0.0)) return s;

        if (s.fBare <= targetHz)
        {
            s.ground = true;
            s.mu = 0.0;
            s.f0 = targetHz;
        }
        else
        {
            // Bisect mu. 12 beam masses is far beyond any real solder blob;
            // the extrapolated bottom octave needs about 1.2.
            double lo = 0.0, hi = 12.0;
            for (int i = 0; i < 70; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                const double b1 = root (1.0e-3, b0 + 1.0e-9, mid);
                const double f = s.fBare * (b1 * b1) / (b0 * b0);
                if (f > targetHz) lo = mid;
                else              hi = mid;
            }
            s.mu = 0.5 * (lo + hi);
            s.beta1 = root (1.0e-3, b0 + 1.0e-9, s.mu);
            s.f0 = s.fBare * (s.beta1 * s.beta1) / (b0 * b0);
        }

        s.beta2 = root (3.9266 + 1.0e-6, 4.6940911329741746 + 1.0e-9, s.mu);
        s.beta3 = root (7.0686 + 1.0e-6, 7.8547574382376126 + 1.0e-9, s.mu);
        s.r2 = (s.beta2 * s.beta2) / (s.beta1 * s.beta1);
        s.r3 = (s.beta3 * s.beta3) / (s.beta1 * s.beta1);
        return s;
    }
};

// ---------------------------------------------------------------------------
// The measured decay, consumed as anchors rather than as a law (the CP-70
// rule). Nine unlooped single-note recordings put Q near 1000 flat across the
// keyboard -- hysteretic material damping plus clamp loss -- with real
// per-reed scatter (567-2130). Log-interpolated through the anchors;
// extrapolated at constant Q beyond them, continuously, since constant Q is
// dB/s proportional to f.
// ---------------------------------------------------------------------------
inline double wurliDecayDbPerS (double f)
{
    static constexpr int kN = 7;
    static constexpr double kF[kN] = { 55.0, 124.0, 165.0, 277.0, 553.0, 787.0, 1664.0 };
    static constexpr double kD[kN] = { 1.11, 3.40, 7.94, 7.39, 14.4, 25.4, 21.3 };

    if (f <= kF[0])      return kD[0] * (f / kF[0]);
    if (f >= kF[kN - 1]) return kD[kN - 1] * (f / kF[kN - 1]);
    for (int i = 0; i < kN - 1; ++i)
        if (f <= kF[i + 1])
        {
            const double t = std::log (f / kF[i]) / std::log (kF[i + 1] / kF[i]);
            return kD[i] * std::pow (kD[i + 1] / kD[i], t);
        }
    return kD[kN - 1];
}

class WurliVoice
{
public:
    // [0] fundamental, [1] mode 2, [2] mode 3. The sustained sound needs one
    // mode; the attack carries a brief inharmonic chirp (nothing above
    // -45 dB between 3x and 25x f0 in any measured sustain), and modes above
    // the fs/pi budget are refused by setMode. Worst case 64 x 3 = 192
    // modes, a tenth of the Rhodes pedal-down.
    //
    // The plan drafted a fourth slot: a second mechanical component at
    // f0 + 2.4 Hz for the measured AM, flagged as open question 4 (two
    // beating components vs global gain modulation). This suite's own K2 row
    // arbitrated it: a -12 dB mechanical partner drives the pooled
    // inharmonic residual to -9 dB through the pickup's nonlinearity --
    // binomial sideband combs at k*delta, one-sided, k of them per harmonic
    // -- where every real sustain measures below -45 dB with the AM plainly
    // present. No launch level is both audible and clean. (A gap-breathing
    // hybrid measured the same way: constant spacing, but its depth grows
    // with harmonic index, and at a deep swing the modulation index of the
    // high harmonics exceeds one and Bessel-spreads the clusters -- the same
    // flood.) The real recordings' sidebands are SYMMETRIC with constant
    // spacing, the signature of the plan's mechanism (b), global gain
    // modulation: the AM ships as a 2.4 Hz modulation of the transduced
    // output -- +/-delta sidebands of one constant depth on every harmonic,
    // pooled residual clean.
    static constexpr int kReedModes = 3;
    using System = SavModalSystem<kReedModes, 2>;   // SAV slots reserved, none
                                                    // active: the spectra show
                                                    // no amplitude-dependent
                                                    // pitch structure

    // The transducer runs at four times the mechanics for the same reason the
    // Rhodes field does: the reed's motion is band-limited (one mode), but
    // y/(1-y) -- slope up to 1/(1-0.94)^2 = 278 at the knee -- makes
    // harmonics without practical limit, and on a 55 Hz bark the folded
    // content lands mid-band.
    static constexpr int kOver = 4;

    struct Config
    {
        // Action
        double hammerHardness = 0.5;
        double hammerMassNorm = 0.5;
        double escapementNorm = 0.4;
        double damperGrip     = 0.6;

        // Resonator
        double tipMassNorm    = 0.5;   // tongue thickness 0.020 -> 0.026 in, mu re-solved
        double dampTrim       = 0.5;   // clamp loss: filing the knife edge, Q x0.5..1.5

        // Transducer
        double pickupCentring = 0.25;  // rest offset in the slot, gap units (-0.5..0.5):
                                       // the manual's own voicing move, and the
                                       // manual implies factory reeds sit off
                                       // centre ("check to see if the reed ...
                                       // is slightly off center in the pickup")
        double gapMm          = 0.5;   // rest gap; floored at 0.3 so the
                                       // no-back-action claim stays true
                                       // (softening goes as 1/d^3)

        // Master tune and pitch bend together, in cents. Applied by
        // re-solving mu -- the tech's solder move -- not by an offset.
        double detuneCents    = 0.0;
        // 0 Magnetic, 1 Native (= electrostatic here), 2 Electro, 3 Contact.
        double transducer     = 1.0;
    };

    // Compared byte-for-byte by the engine to decide whether the instrument
    // needs rebuilding, so it must have no padding for that comparison to
    // mean what it says.
    static_assert (std::is_trivially_copyable<Config>::value, "Config must be memcmp-able");
    static_assert (sizeof (Config) == 10 * sizeof (double), "Config has padding");

    void prepare (double sampleRate, const MagneticPickup* sharedField = nullptr)
    {
        field = sharedField;
        fs = sampleRate;
        sys.prepare (sampleRate);
        sys.setNumModes (kReedModes);
        hammer.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        sys.clear();
        sys.setNumModes (kReedModes);
        hammer.reset();
        sounding = held = pedal = false;
        controlCounter = 0;
        peakEnergy = 1.0e-30;
        beatPhase = 0.0;
        reduced = false;
        configured = false;
        for (int i = 0; i < 3; ++i) tipHist[i] = 0.0;
    }

    // The 2.4 Hz AM's voicing. Every measured harmonic carries ~2.4 Hz
    // symmetric AM sidebands -- far below the tremolo -- and a static sine
    // sounds dead without it. It cannot be a polarisation beat (section 1.2
    // of the plan), and the K2 row ruled out a second mechanical component
    // (see the class comment), so it is a slow gain modulation of the
    // transduced output. Depth is the fractional gain swing; 0.08 is
    // ~1.4 dB peak-to-peak on every harmonic.
    void setBeat (double rateHz, double depth)
    {
        beatDelta = rateHz;
        beatDepth = std::clamp (depth, 0.0, 0.5);
    }

    bool isSounding() const { return sounding; }
    bool isRinging() const { return sounding || hammer.isActive(); }

    // ---- the reed-bar path: sympathetic resonance -----------------------
    // Sixty-four reeds bolt to one bar. A struck reed shakes it, and it
    // shakes the rest; with the dampers lifted the bar's neighbours answer
    // quietly, which is what a pedalled 200A actually does. Same shape both
    // directions, so the coupling is a spring and cannot make energy.
    double clampDisplacement() const { return sys.displacementAt (shapeCl) * clampInv; }
    void addClampForce (double f)
    {
        for (int m = 0; m < kReedModes; ++m) sys.addForce (m, f * shapeCl[m] * clampInv);
    }
    bool isStruckVoice() const { return struckAlive && (sounding || hammer.isActive()); }
    void wakeSympathetic()
    {
        if (hammer.isActive() || sounding) return;
        struckAlive = false;
        sounding = true;
    }
    bool isHeld()     const { return held; }
    int  noteNumber() const { return note; }
    int  contactSamples() const { return hammer.contactDurationSamples(); }
    double modalEnergy() const { return sys.energy(); }
    double tipDisplacement() const { return tipHist[2]; }
    double soundingHz() const { return reed.f0; }
    double c0Pf() const { return c0; }
    const WurliReed::Solve& solved() const { return reed; }

    // Give this reed its note. Called once when the instrument is built --
    // a reed is soldered for one note and stays there.
    void setNote (int midiNote, const Config& cfg)
    {
        note = midiNote;
        configure (cfg);
    }

    void noteOn (int midiNote, double velocity, const Config& cfg, std::uint32_t seed)
    {
        struckAlive = true;
        (void) seed;
        if (! configured || midiNote != note) setNote (midiNote, cfg);
        held = true;
        sounding = true;
        if (reduced) { sys.setNumModes (kReedModes); reduced = false; }

        // Same launch law as the Rhodes: the exponent is the key's leverage.
        // The 200A action's own geometry (manual pp. 16-19): blow distance
        // 30.95 mm, let-off 3.18 mm, key dip 9.53 mm -- lever ratio ~3.25,
        // with the hammer free-flying the last 3.18 mm.
        const double v = 0.18 + 5.6 * std::pow (std::clamp (velocity, 0.0, 1.0), 1.7);
        const double escMm = 3.18 * (0.4 + 1.2 * std::clamp (cfg.escapementNorm, 0.0, 1.0));
        hammer.strike (v, escMm * 1.0e-3);
    }

    void noteOff() { held = false; }
    void setPedal (bool down) { pedal = down; }

    // One sample of mechanics, four subsamples of transduction. Writes kOver
    // capacitance perturbations DC_n = C0n * y/(1-y) in picofarads, relative
    // to the resting value so a silent voice contributes exactly zero. The
    // caller sums all voices onto one bus -- the real reed bar hangs all 64
    // gaps on ONE 240 pF node, so superposition at the node is exact and
    // intermodulation belongs to the preamp, where the circuit puts it.
    void process (const Config& cfg, double* dcOut)
    {
        (void) cfg;
        if (! sounding && ! hammer.isActive())
        {
            for (int k = 0; k < kOver; ++k) dcOut[k] = 0.0;
            return;
        }

        if (hammer.isActive())
        {
            const double u = sys.displacementAt (shapeStrike);
            const double v = sys.velocityAt (shapeStrike);
            const double f = hammer.tick (u, v, hammerCfg);
            if (f != 0.0)
            {
                sounding = true;
                for (int m = 0; m < kReedModes; ++m)
                    sys.addForce (m, f * shapeStrike[m]);
            }
        }

        sys.tick();

        if (! held && ! pedal) applyDamper();

        if (++controlCounter >= kControlDecim)
        {
            controlCounter = 0;
            const double e = sys.energy();

            // A reed that has gone non-finite is silenced and the state
            // cleared; a non-finite tip position would otherwise reach the
            // capacitance law's divide.
            if (! std::isfinite (e)) { recover (dcOut); return; }

            if (e > peakEnergy) peakEnergy = e;

            // Retirement is RELATIVE to this voice's own strike (the CP-70
            // practice): -100 dB under the post-strike peak retires, -80 dB
            // drops to the fundamental alone (the chirp modes died in tens
            // of ms; their state is frozen, not cleared, so a strike picks
            // them straight back up). A pedalled A1 legitimately runs the
            // better part of a minute -- T60 up to ~54 s is the instrument,
            // not a leak.
            if (! hammer.isActive() && e < peakEnergy * 1.0e-8)
            {
                if (! reduced) { sys.setNumModes (1); reduced = true; }
            }
            else if (reduced && (hammer.isActive() || e >= peakEnergy * 1.0e-8))
            {
                sys.setNumModes (kReedModes);
                reduced = false;
            }
            if (! hammer.isActive() && e < peakEnergy * 1.0e-10)
            {
                sounding = false;
                struckAlive = false;
            }
        }

        const double tip = sys.displacementAt (shapeTip);
        if (! std::isfinite (tip)) { recover (dcOut); return; }

        // The 2.4 Hz AM (see setBeat): one gain on the transduced output,
        // advanced per host sample -- at 2.4 Hz the intra-frame variation is
        // nothing.
        beatPhase += 2.0 * kPiD * beatDelta / fs;
        if (beatPhase > 2.0 * kPiD) beatPhase -= 2.0 * kPiD;
        const double outMod = 1.0 + beatDepth * std::sin (beatPhase);
        const double cfNow = (trans == 3) ? sysDisplacementContact() : 0.0;
        if (trans == 0 && field != nullptr && magRest == 0.0)
            magRest = field->flux (static_cast<float> (kMagOffset), kMagGap);

        // The tip's path between this sample and the last is band-limited --
        // one live mode plus a dying chirp -- so reconstructing it at kOver
        // points is legitimate, and the curved capacitance law is evaluated
        // at each. Holding one value across the frame would make the
        // upsampled waveform a staircase (the Rhodes lesson).
        for (int k = 0; k < kOver; ++k)
        {
            const double t = static_cast<double> (k + 1) / kOver;
            const double vv = hermite (tipHist[0], tipHist[1], tipHist[2], tip, t);
            // The swappable transducer: the native electrostatic law, a
            // magnetic coil's flux with the tip riding the field table, or
            // the linear force into the clamp. Same reconstructed path for
            // all three -- the Rhodes staircase lesson holds regardless of
            // which law is looking.
            if (trans == 2)
                dcOut[k] = deltaCPf (vv) * outMod;
            else if (trans == 0)
                dcOut[k] = (field != nullptr
                             ? (field->flux (static_cast<float> (kMagOffset + vv), kMagGap) - magRest)
                             : 0.0) * kMagOut * outMod;
            else
                dcOut[k] = kContactOut
                         * hermite (cfHist[0], cfHist[1], cfHist[2], cfNow, t) * outMod;
        }
        tipHist[0] = tipHist[1]; tipHist[1] = tipHist[2]; tipHist[2] = tip;
    }

    // The capacitance perturbation for a tip displacement, in pF, relative to
    // rest. Public so the pickup rows can drive the exact law the voice uses
    // with a synthetic trajectory.
    double deltaCPf (double tipMetres) const
    {
        return c0 * capLaw (tipMetres * invGap + centring) - dcRest;
    }

    // The capacitance modulation law, bounded through the plate plane.
    //
    // y/(1-y) is the parallel-plate limit and it expires before the plate:
    // in the slotted comb the reed passes THROUGH the plate plane, and the
    // FEM capacitance rises toward the plane, turns over, and falls beyond
    // (DAFx-17 computes C by FEM precisely because the parallel-plate form
    // fails there). The plan's first stand-in -- a soft knee saturating at
    // y = 0.98 -- keeps the divergence out but keeps the law MONOTONE, and a
    // fortissimo bass swing then rides the flat top for half its cycle: a
    // square wave whose fundamental dominates, measured bark stuck near
    // +5 dB where the real A1 reaches +26.7 with the fundamental 18 dB UNDER
    // harmonics 2-7. That fundamental collapse is the turnover's signature:
    // past the plane the capacitance falls again, so each crossing makes a
    // narrow C pulse and a deep swing produces TWO pulses per cycle --
    // frequency-doubled energy, fundamental cancelled. So the stand-in is
    // the turnover itself:
    //
    //   g(y) = y / sqrt((1 - y)^2 + w^2)
    //
    // which matches y/(1-y) through second order at small y (the P2
    // asymmetry and the small-signal law are untouched), peaks smoothly at
    // the plane with height ~1/w, and falls beyond. w = 0.10 puts the peak
    // at 10x the rest capacitance -- the slot's lateral clearance (tens of
    // microns against the half-millimetre gap) standing in for the vanished
    // vertical gap -- and is a V1-calibration constant: the measured ff bark
    // grows with 1/w and this value is where the growth flattens out.
    // Plate CONTACT stays a fault, not an operating point: the law is smooth
    // and bounded everywhere, and its maximum slope is ~1/w^2, far below the
    // knee's 278, which also eases the aliasing the oversampling has to
    // remove.
    static double capLaw (double y)
    {
        constexpr double w = 0.10;
        const double d = 1.0 - y;
        return y / std::sqrt (d * d + w * w);
    }

    void refresh (const Config& cfg) { configure (cfg); }

private:
    static constexpr int kControlDecim = 32;

    // Felt crown contact width: smaller than the Rhodes neoprene tip, and
    // fixed, so the covered fraction grows toward the treble as the reeds
    // shorten -- the graduation a piano gets by grading its hammers.
    static constexpr double kTipWidth = 0.006;   // m

    // Where the blow lands, as a fraction of the free length. The strike is
    // against the underside of the reed; no primary figure exists, so this is
    // a named calibration variable owned by the attack-spectrum row, like the
    // CP-70's strike ratio.
    static constexpr double kStrikeBeta = 0.20;

    // Miessner US 2,932,231: contact lasts "three fourths to one cycle of
    // vibration at its fundamental frequency". The midpoint is both the
    // hammer-stiffness calibration target and the dwell suppression's cycle
    // count -- a ~0.9-cycle contact is itself most of why the upper modes
    // stay quiet.
    static constexpr double kDwellCycles = 0.875;
    static constexpr double kDwellShape  = 0.15;

    // The per-note y-scale law (see the transducer section of configure()),
    // calibrated against rows V1 and V2 -- the plan names this calibration
    // as the owner of the undocumented gap (open question 3).
    //
    // The measured bark does NOT fall smoothly with register: B2 and E3 at
    // forte still carry +6.4/+6.6 dB while Db4, nine semitones up, is at
    // -3.6 -- a ten-decibel cliff -- and the treble then falls gently to
    // Db5's -12. The mechanics deliver near-identical tip swings across that
    // span, so the cliff must live in the effective gap: the bass and mid
    // reeds voiced close, the upper-mid opened up sharply, exactly the
    // per-reed feeler-gauge voicing a factory does. It ships as a smooth
    // sigmoid cliff on a flat bass shelf with a gentle treble slope, four
    // constants fitted to the four measured bark anchors (A1 pp, E3/Db4/Db5
    // f). Whether real gaps trace this curve is open question 3's
    // measurement; the shape is the data's, not a per-key table.
    static constexpr double kYBase       = 0.587;
    static constexpr double kCliffPos    = 0.40;   // register position of the cliff
    static constexpr double kCliffWidth  = 0.05;
    static constexpr double kCliffDepth  = 1.10;   // natural log units
    static constexpr double kTrebleSlope = 2.50;   // ln per register unit past the cliff

    double yScaleFor (double reg) const
    {
        const double t = (reg - kCliffPos) / kCliffWidth;
        const double sig = 0.5 * (1.0 + std::tanh (t));
        const double soft = kCliffWidth * std::log1p (std::exp (std::min (30.0, t)));
        return kYBase * std::exp (-kCliffDepth * sig - kTrebleSlope * soft);
    }

    static double hermite (double y0, double y1, double y2, double y3, double t)
    {
        const double c0h = y1;
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0h;
    }

    double registerPosition() const
    {
        return std::clamp ((static_cast<double> (note) - 33.0) / 63.0, 0.0, 1.0);
    }

    // One reference blow against a scratch copy of the configured reed, for
    // the stiffness calibration above. The copy is a value copy of the tiny
    // four-mode system; its state is cleared, its modes kept.
    double simulateContactSeconds (double stiffness) const
    {
        System scratch (sys);
        scratch.clear();
        scratch.setNumModes (kReedModes);
        HuntCrossleyHammer h;
        h.prepare (fs);
        auto c = hammerCfg;
        c.stiffness = stiffness;
        h.strike (2.0, 0.5e-3);
        const int limit = std::min (40000, static_cast<int> (5.0 * fs / std::max (20.0, reed.f0)) + 600);
        for (int n = 0; n < limit && h.isActive(); ++n)
        {
            const double u = scratch.displacementAt (shapeStrike);
            const double v = scratch.velocityAt (shapeStrike);
            const double f = h.tick (u, v, c);
            if (f != 0.0)
                for (int m = 0; m < kReedModes; ++m)
                    scratch.addForce (m, f * shapeStrike[m]);
            scratch.tick();
        }
        return h.contactDurationSamples() / fs;
    }

    void recover (double* dcOut)
    {
        sys.clear();
        sounding = false;
        for (int i = 0; i < 3; ++i) tipHist[i] = 0.0;
        for (int k = 0; k < kOver; ++k) dcOut[k] = 0.0;
    }

    void applyDamper()
    {
        for (int m = 0; m < sys.numModes(); ++m)
            if (sys.displacement (m) != 0.0)
                sys.scaleMode (m, damperFactor);
    }

    void configure (const Config& cfg)
    {
        const double target = 440.0 * std::pow (2.0, (static_cast<double> (note) - 69.0) / 12.0)
                            * std::pow (2.0, cfg.detuneCents / 1200.0);
        const double thick = WurliReed::kThicknessEarly
                           + (WurliReed::kThicknessLate - WurliReed::kThicknessEarly)
                             * std::clamp (cfg.tipMassNorm, 0.0, 1.0);
        reed = WurliReed::solve (note, target, thick);

        // Modal mass, tip-normalised: rho*A*L/4 for the bare tongue plus the
        // solder at the tip, which every tip-normalised mode sees in full.
        const double modalMass = reed.beamMass * (0.25 + reed.mu);

        // One Q, anchored per note, with the measured per-reed scatter: a
        // deterministic +/-0.2 natural-log jitter seeded from the note number
        // is truthful where a constant law is unnaturally even. The sustain
        // control is clamp loss -- filing the reed-bar knife edge changes
        // sustain by seconds -- scaling the anchored Q x0.5..1.5.
        const double jitterU = (((static_cast<std::uint32_t> (note) * 2654435761u) >> 8) & 1023u)
                             / 511.5 - 1.0;
        const double qTrim = (0.5 + std::clamp (cfg.dampTrim, 0.0, 1.0))
                           * std::exp (0.2 * jitterU);
        const double q0 = 27.2876 * reed.f0 / wurliDecayDbPerS (reed.f0) * qTrim;

        const double f2 = reed.f0 * reed.r2;
        const double f3 = reed.f0 * reed.r3;

        // Modes 2-3 have no measured Q -- below -45 dB everywhere in every
        // sustain window -- only the constraint that the chirp is GONE within
        // tens of milliseconds. A fixed fraction of the fundamental's Q (the
        // first attempt, following the Rhodes' overtone logic) cannot deliver
        // that: Q/15 on a bass reed still left mode 2 ringing half a second,
        // where it sat on the seventh harmonic and read as inharmonic hash in
        // the sustain rows. A short fixed T60 is what the observable actually
        // is: no higher mode is balanced by anything, each one drives the
        // clamp directly at high curvature, and the bolted bar eats it.
        sys.setMode (0, reed.f0, 2.1985 * q0 / reed.f0, modalMass);
        sys.setMode (1, f2, 0.040, modalMass);
        sys.setMode (2, f3, 0.040, modalMass);

        // ---- strike and readout shapes ----------------------------------
        // The contact patch: identical sinc weights for read and write --
        // the reciprocity rule this project already paid for. The uniform
        // clamped-free shapes stand in for the mass-loaded ones; the solved
        // betas carry the patch argument so the spatial average tracks the
        // real wavelengths.
        const double patchW = std::clamp (kTipWidth / reed.freeLen, 0.02, 0.5);
        const double betas[3] = { reed.beta1, reed.beta2, reed.beta3 };
        const int    shapeIdx[3] = { 0, 1, 2 };

        auto strikeWeight = [&] (int m, double ratio)
        {
            const double z = 0.5 * betas[m] * patchW;
            double w = std::abs (z) < 1.0e-9 ? 1.0 : std::sin (z) / z;
            // The dwell: a hammer resting on the reed for most of a cycle
            // cannot put energy into a mode oscillating six times faster --
            // the force reverses underneath it. Most of that suppression the
            // explicit Hunt-Crossley contact produces on its own; this term
            // carries the remainder, calibrated against the -43 dB the
            // reference implementation matched to recordings.
            const double dwell = ratio * kDwellCycles;
            w *= std::exp (-0.5 * dwell * dwell * kDwellShape);
            return CantileverModes::shape (shapeIdx[m], kStrikeBeta) * w;
        };

        shapeStrike[0] = strikeWeight (0, 1.0);
        shapeStrike[1] = strikeWeight (1, reed.r2);
        shapeStrike[2] = strikeWeight (2, reed.r3);

        shapeTip[0] = shapeTip[1] = shapeTip[2] = 1.0;   // tip-normalised

        // ---- hammer: felt, not neoprene ---------------------------------
        // 3-ply maple with mothproofed felt. alpha 2.6 is the low end of
        // felt's 2.5-3.5 for thin voiced felt; lambda 3.5 because felt
        // hysteresis exceeds neoprene's 1.6-2.4. Hunt-Crossley rather than
        // Stulov: DAFx-17 uses it for this very instrument, and the
        // observables are contact time and attack spectrum.
        const double reg = registerPosition();
        hammerCfg.alpha  = 2.6;
        hammerCfg.lambda = 3.5;
        // ~4 g bass to ~2 g treble -- small maple hammers, no primary figure,
        // flagged in the plan.
        hammerCfg.mass = (0.004 - 0.002 * reg) * (0.6 + 0.8 * std::clamp (cfg.hammerMassNorm, 0.0, 1.0));

        // Stiffness calibrated per note to the Miessner contact target: the
        // observable is contact time, not k. Every closed form tried put the
        // measured contact a factor of two to four long, because the
        // hysteretic term (lambda * v up to 20 at a fortissimo blow) both
        // brakes the loading stroke and leaves a long weak-force unloading
        // tail that no undamped linearisation carries. So the calibration is
        // the measurement itself: a bisection over log-stiffness, each probe
        // one simulated reference blow (v = 2 m/s) against a scratch copy of
        // this reed's own modal system -- correctness measured, not assumed.
        // Fourteen probes of a four-mode system for a few reed periods each;
        // the engine already treats instrument builds as offline work.
        // The hardness knob then scales k the house way, x12^(hardness-0.5).
        {
            const double tau = kDwellCycles / reed.f0;
            double lgLo = 3.0, lgHi = 12.0;
            for (int i = 0; i < 14; ++i)
            {
                const double mid = 0.5 * (lgLo + lgHi);
                if (simulateContactSeconds (std::pow (10.0, mid)) > tau) lgLo = mid;
                else                                                     lgHi = mid;
            }
            const double kWant = std::pow (10.0, 0.5 * (lgLo + lgHi));
            hammerCfg.stiffness = kWant * std::pow (12.0, std::clamp (cfg.hammerHardness, 0.0, 1.0) - 0.5);

            // Same explicit-contact stability principle as the other
            // instruments -- past a fraction of the sample rate the explicit
            // contact stops being a contact and becomes a generator -- but
            // written for THIS contact's exponent. The Rhodes' closed form is
            // the alpha = 2 special case and is dimensionally wrong for
            // felt's 2.6: applied here it capped the treble two decades below
            // the true stability limit and stretched C6's contact to 2.2
            // cycles. General form, from the linearised stiffness at peak
            // compression (k_eff = alpha k d^(alpha-1), d from the energy
            // balance at the reference blow):
            //
            //   kMax = [ m wMax^2 / alpha ]^((alpha+1)/2)
            //          * ( (alpha+1) m v^2 / 2 )^(-(alpha-1)/2)
            const double phi = CantileverModes::shape (0, kStrikeBeta);
            const double effM = modalMass / std::max (1.0e-9, phi * phi);
            const double mRed = 1.0 / (1.0 / hammerCfg.mass + 1.0 / effM);
            const double wMax = 2.0 * kPiD * 0.06 * fs;
            const double a = hammerCfg.alpha;
            const double kMax = std::pow (mRed * wMax * wMax / a, 0.5 * (a + 1.0))
                              * std::pow ((a + 1.0) * mRed * 2.0 * 2.0 * 0.5, -0.5 * (a - 1.0));
            if (hammerCfg.stiffness > kMax) hammerCfg.stiffness = kMax;
        }

        // ---- damper -----------------------------------------------------
        // Felt dampers on all 64 notes (no evidence of an undamped top range
        // was found; flagged as open question 8), grip graduated heavier in
        // the bass because a long reed carries more energy.
        const double grip = std::clamp (cfg.damperGrip, 0.0, 1.0);
        const double damperT60 = (0.30 - 0.24 * grip) * (1.0 + 1.4 * (1.0 - reg));
        damperFactor = std::exp (-3.0 * std::log (10.0) / (damperT60 * fs));

        // ---- transducer -------------------------------------------------
        // Per-reed rest capacitance, ~3.5 pF bass to ~2 pF treble, folded
        // into a per-note weight on the shared 240 pF node. Sanity anchor:
        // u0 * C0 / C_total = 147 * 3 / 240 = 1.84 V.
        c0 = 3.5 - 1.5 * reg;

        // The per-note y-scale, owned by the V1/V2 calibration (open
        // question 3: no absolute gap dimension exists in any primary
        // source). The tip swing the mechanics deliver is nearly flat from
        // E3 to Db5 while the measured bark falls off a ten-decibel cliff
        // there -- so the effective modulation per metre of tip travel must
        // carry the register law, exactly as a graduated per-reed gap (set
        // by a tech with a feeler gauge, reed by reed) would produce. See
        // yScaleFor() for the fitted shape.
        const double gap = std::clamp (cfg.gapMm, 0.3, 0.8) * 1.0e-3;
        invGap = yScaleFor (reg) / gap;
        centring = std::clamp (cfg.pickupCentring, -0.5, 0.5);
        dcRest = c0 * capLaw (centring);

        {
            const int t = static_cast<int> (cfg.transducer + 0.5);
            trans = (t == 1) ? 2 : t;   // native IS the electrostatic law
        }
        // Contact: the inertial force the reed puts into its clamp -- what
        // a contact microphone on the reed bar hears. Mode shapes at the
        // near-clamp station, weighted by omega squared and the modal mass.
        {
            const double fr[3] = { reed.f0, reed.f0 * reed.r2, reed.f0 * reed.r3 };
            double nrm = 0.0;
            for (int m = 0; m < kReedModes; ++m)
            {
                shapeCl[m] = CantileverModes::shape (m, 0.06);
                nrm += shapeCl[m] * shapeCl[m];
                const double w = 2.0 * kPiD * fr[m];
                shapeContact[m] = shapeCl[m] * w * w
                                * reed.beamMass * (0.25 + reed.mu);
            }
            clampInv = 1.0 / std::max (1.0e-9, std::sqrt (nrm));
        }
        configured = true;
    }

    double fs = 48000.0;
    int note = 60;
    System sys;
    HuntCrossleyHammer hammer;
    HuntCrossleyHammer::Config hammerCfg;
    WurliReed::Solve reed;

    double shapeStrike[kReedModes] {};
    double shapeTip[kReedModes] {};
    double tipHist[3] {};
    double cfHist[3] {};
    double shapeContact[64] {};
    double shapeCl[kReedModes] {};
    double clampInv = 1.0;
    bool struckAlive = false;
    const MagneticPickup* field = nullptr;
    int trans = 2;
    double magRest = 0.0;
    static constexpr double kMagOffset = -1.0e-3;   // m, off the pole centre
    static constexpr float  kMagGap    = 1.8e-3f;
    static constexpr double kMagOut    = 0.043;       // level-matched by probe
    static constexpr double kContactOut= 2.5;
    double sysDisplacementContact()
    {
        double cf = sys.displacementAt (shapeContact);
        cfHist[0] = cfHist[1]; cfHist[1] = cfHist[2]; cfHist[2] = cf;
        return cf;
    }
    double c0 = 3.0, invGap = 2000.0, centring = 0.0, dcRest = 0.0;
    double damperFactor = 1.0;
    double beatDelta = 2.4, beatDepth = 0.08, beatPhase = 0.0;
    double peakEnergy = 1.0e-30;
    int controlCounter = 0;
    bool sounding = false, held = false, pedal = false, reduced = false, configured = false;
};

} // namespace epi
