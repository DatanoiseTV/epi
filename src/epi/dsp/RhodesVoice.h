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

        // Resonator
        double tuningSpring   = 0.5;
        double damping        = 0.35;
        double barCoupling    = 0.6;
        double barTuneSemis   = 0.0;
        double nonlinearity   = 0.5;

        // Transducer
        double pickupOffset   = -0.35;   // in pole half-widths
        double pickupGapNorm  = 0.35;
    };

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
        sounding = held = pedal = false;
        controlCounter = 0;
        configured = false;
    }

    bool isSounding() const { return sounding; }
    bool isHeld()     const { return held; }
    int  noteNumber() const { return note; }
    double tipDisplacement() const { return lastTipV; }
    double tipHorizontal()   const { return lastTipH; }
    int  contactSamples() const { return hammer.contactDurationSamples(); }

    // ---- note lifecycle ---------------------------------------------------
    void noteOn (int midiNote, double velocity, const Config& cfg, std::uint32_t seed)
    {
        note = midiNote;
        held = true;
        sounding = true;
        rng = Rng (seed | 1u);

        sys.clear();
        sys.setNumModes (kNumModes);
        for (int i = 0; i < 3; ++i) { vHist[i] = 0.0; hHist[i] = 0.0; }
        configure (cfg);

        // A Rhodes hammer arrives somewhere between about a tenth of a metre
        // per second and four. The square law is the key's own leverage, not a
        // taste curve.
        const double v = 0.12 + 3.9 * velocity * velocity;

        // Escapement: the gap left between tip and tine with the key fully
        // down. The service manual specifies a quarter to three eighths of an
        // inch in the bass falling to a thirty-second in the treble, because a
        // long bass tine whips far enough to strike a closer hammer twice.
        const double reg = registerPosition();
        const double escMm = (6.4 - 5.6 * reg) * (0.4 + 1.2 * cfg.escapementNorm);
        hammer.strike (v, escMm * 1.0e-3);
    }

    void noteOff() { held = false; }
    void setPedal (bool down) { pedal = down; }

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

    void process (const Config& cfg, double* fluxOut)
    {
        if (! sounding)
        {
            for (int k = 0; k < kOver; ++k) fluxOut[k] = restFlux;
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

        updateStretchTerm();
        sys.tick();

        if (! held && ! pedal) applyDamper();

        if (++controlCounter >= kControlDecim)
        {
            controlCounter = 0;
            if (! hammer.isActive() && sys.energy() < 1.0e-18)
            {
                sounding = false;
                sys.clear();
                sys.setNumModes (kNumModes);
            }
        }

        // -- transduction, oversampled ----------------------------------------
        const double vNow = sys.displacementAt (shapeTipV);
        const double hNow = sys.displacementAt (shapeTipH);

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
            const double gap = std::max (0.25e-3, staticGap + hh - arc);

            fluxOut[k] = field != nullptr ? field->flux (staticOffset + vv, gap) : 0.0;
        }

        vHist[0] = vHist[1]; vHist[1] = vHist[2]; vHist[2] = vNow;
        hHist[0] = hHist[1]; hHist[1] = hHist[2]; hHist[2] = hNow;

        lastTipV = vNow;
        lastTipH = hNow;
    }

    // The flux with the tine at rest. Subtracted downstream so the
    // differentiated signal is not a tiny modulation riding on a large
    // constant, which in a narrower type would throw away most of its bits.
    double restingFlux() const { return restFlux; }

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
        const double f0 = 440.0 * std::pow (2.0, (static_cast<double> (note) - 69.0) / 12.0);
        const double reg = registerPosition();

        // ---- tine geometry --------------------------------------------------
        // Solved from the beam equation for the note wanted, using the real
        // material and a wire gauge that tapers toward the treble as the later
        // instruments' swaged tines do. A single gauge across the whole compass
        // would put the top octave at an impossible few millimetres.
        // Later Rhodes tines are ground to a taper toward the treble, but not
        // steeply: taking too much off makes the top octaves so light that the
        // hammer overwhelms them.
        const double radius = (0.95 - 0.30 * reg) * 1.0e-3;
        tineLength = CantileverModes::lengthForFrequency (f0, radius, kSpringSteel);
        const double area = kPiD * radius * radius;
        const double modalMass = std::max (1.0e-8, kSpringSteel.density * area * tineLength * 0.25);

        // Where the tuning spring sits, as a fraction of the free length, and
        // how heavy it is. Toward the tip it mostly transposes; back toward the
        // clamp it starts pulling the overtones about, because it then sits at
        // a different fraction of each mode's shape.
        const double springPos = 0.55 + 0.42 * std::clamp (cfg.tuningSpring, 0.0, 1.0);
        const double mu        = 0.05 + 0.30 * (1.0 - std::clamp (cfg.tuningSpring, 0.0, 1.0));

        // Shear matters more for the short thick treble tines than the long
        // bass ones, which is the physical reason the top of a Rhodes is less
        // clangy than plain beam theory predicts.
        const double shearNumber = 0.0016 + 0.0090 * reg;

        // Steel loses almost nothing internally; what actually ends a Rhodes
        // note is energy walking out through the clamp into the bar and the
        // harp, and that path is far more open to the higher modes. So the
        // fundamental rings for seconds while the clang is gone inside one.
        const double damp = std::clamp (cfg.damping, 0.0, 1.0);
        const double t60Base = (7.0 - 4.5 * reg) * (1.35 - 0.9 * damp);

        // Where the hammer lands, as a fraction of the free length.
        //
        // This one number sets the whole scale of the instrument, and it is
        // easy to get badly wrong. A clamped-free mode shape is almost flat
        // near the clamp, so the effective mass the hammer meets there is the
        // modal mass divided by the square of a very small number -- kilograms,
        // against a hammer of a few grams. Move the strike point out to a
        // quarter of the length and that effective mass falls by two orders of
        // magnitude, the hammer throws the tine into a fifteen-millimetre
        // swing, and every downstream number is wrong: the pickup field
        // saturates and the stretching nonlinearity pulls the bass a fourth
        // sharp. A real Rhodes tine moves one to three millimetres at forte,
        // and the hammer strikes it close in to the block, which is what makes
        // that so.
        // The hammer rail is straight while the tines shorten toward the
        // treble, so a short tine is struck at a LARGER fraction of its own
        // length than a long one. That matters: it is what keeps the effective
        // mass the hammer meets in the same range from one end of the compass
        // to the other.
        const double strikeAt = 0.13 + 0.13 * reg;
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
        const double barF0  = TonebarTable::barFrequency (f0) * std::pow (2.0, cfg.barTuneSemis / 12.0);
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
            tineFreq[m] = f0 * CantileverModes::ratio (m, mu, springPos, shearNumber);
            tineT60[m]  = t60Base / (1.0 + 2.6 * m + 0.55 * m * m);
        }

        double trim = 1.0;
        if (ks > 0.0)
        {
            const double rk = std::sqrt (ks);
            for (int pass = 0; pass < 4; ++pass)
            {
                const double got = assembledFundamental (tineFreq, modalMass, rk, trim);
                if (! (got > 0.0)) break;
                trim *= f0 / got;
            }
        }

        for (int m = 0; m < kTineModes; ++m)
        {
            sys.setMode (kV0 + m, tineFreq[m] * trim, tineT60[m], modalMass);
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
            sys.setMode (kH0 + m, tineFreq[m] * trim * 1.004, tineT60[m] * 0.85, modalMass);
        }

        // ---- tonebar --------------------------------------------------------
        const double barF = TonebarTable::barFrequency (f0) * std::pow (2.0, cfg.barTuneSemis / 12.0);
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
        for (int m = 0; m < kTineModes; ++m)
        {
            const double z = 0.5 * CantileverModes::betaL (m) * patchW;
            const double w = std::abs (z) < 1.0e-9 ? 1.0 : std::sin (z) / z;

            shapeTipV[kV0 + m]    = shapeMode[m][kTip];
            shapeTipH[kH0 + m]    = shapeMode[m][kTip];
            shapeStrikeV[kV0 + m] = shapeMode[m][kStrike] * w;
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
        // Graduated against the tine rather than against the note number. The
        // instrument's hammers are graduated too, and for the same reason: a
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
        hammerCfg.mass = std::clamp (0.30 * effTineMass, 0.00060, 0.0060)
                       * (0.6 + 0.8 * cfg.hammerMassNorm);
        hammerCfg.alpha     = 1.85 + 0.5 * cfg.hammerHardness;
        // Deliberately NOT graduated across the keyboard. A Rhodes hammer tip
        // is the same neoprene at every note -- the hammers are graduated in
        // mass, not in hardness -- and making the treble tips stiffer as well
        // collapsed the contact to a third of a millisecond up there. A
        // contact that short is a broadband impulse: it excites every mode the
        // beam has, and the top two octaves came out with erratic
        // multi-millimetre swings instead of the fraction of a millimetre they
        // should have.
        hammerCfg.stiffness = 6.0e6 * std::pow (12.0, cfg.hammerHardness - 0.5);
        hammerCfg.lambda    = 2.4 - 1.6 * cfg.hammerHardness;

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
        {
            const double mRed = 1.0 / (1.0 / hammerCfg.mass + 1.0 / effTineMass);
            const double vRef = 2.0;                       // a firm blow, m/s
            const double wMax = 2.0 * kPiD * 0.06 * fs;    // contact resonance ceiling
            const double kMax = mRed / vRef * std::pow (wMax / 1.5305, 3.0);
            if (hammerCfg.stiffness > kMax) hammerCfg.stiffness = kMax;
        }



        // ---- damper -------------------------------------------------------------
        // Graduated across the keyboard on the real instrument: long wide felts
        // in the bass, because a long tine carries far more energy.
        const double grip = std::clamp (cfg.damperGrip, 0.0, 1.0);
        const double damperT60 = (0.30 - 0.24 * grip) * (1.0 + 1.4 * (1.0 - reg));
        damperFactor = std::exp (-3.0 * std::log (10.0) / (damperT60 * fs));

        // ---- pickup placement -----------------------------------------------------
        const double halfWidth = field != nullptr ? field->halfWidth() : 3.0e-3;
        staticOffset = std::clamp (cfg.pickupOffset, -1.0, 1.0) * halfWidth;
        staticGap    = 0.6e-3 + 4.4e-3 * std::clamp (cfg.pickupGapNorm, 0.0, 1.0);
        restFlux     = field != nullptr ? field->flux (staticOffset, staticGap) : 0.0;

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
        constexpr double kAxialRestraint = 0.05;
        stretchEA   = kAxialRestraint * kSpringSteel.youngs * area
                    / std::max (1.0e-4, tineLength);
        stretchGain = std::clamp (cfg.nonlinearity, 0.0, 1.0);

        configured = true;
    }

    // psi = sqrt(EA/L)/2 * K, g = sqrt(EA/L) * (Ghat q). No division and no
    // singularity at rest: the 1/sqrt(2V) that quadratisation would normally
    // introduce cancels identically, because this potential is a perfect
    // square.
    // Lowest root of  1 + SUM (c_i^2/m_i)/(w_i^2 - lambda) = 0  for the tine
    // modes joined through the block. The root lies strictly between w_0^2 and
    // w_1^2 (eigenvalue interlacing), so a bisection on that bracket always
    // converges and can never wander off to another mode.
    double assembledFundamental (const double* freq, double modalMass,
                                 double rootKs, double trim) const
    {
        double c[kTineModes], w2[kTineModes];
        for (int m = 0; m < kTineModes; ++m)
        {
            c[m]  = rootKs * shapeMode[m][kBlock];
            const double w = 2.0 * kPiD * freq[m] * trim;
            w2[m] = w * w;
        }

        auto secular = [&] (double lambda)
        {
            double v = 1.0;
            for (int m = 0; m < kTineModes; ++m)
                v += (c[m] * c[m] / modalMass) / (w2[m] - lambda);
            return v;
        };

        // Bracket: just above w_0^2, just below w_1^2. The function goes to
        // -infinity at the lower end and +infinity at the upper, so there is
        // exactly one sign change.
        const double span = w2[1] - w2[0];
        double lo = w2[0] + 1.0e-9 * span;
        double hi = w2[1] - 1.0e-9 * span;
        if (! (hi > lo)) return freq[0] * trim;

        for (int i = 0; i < 60; ++i)
        {
            // The secular function runs from -infinity at the bottom of the
            // bracket to +infinity at the top, so the half containing the sign
            // change is the one whose midpoint is still negative.
            const double mid = 0.5 * (lo + hi);
            if (secular (mid) < 0.0) lo = mid; else hi = mid;
        }
        return std::sqrt (0.5 * (lo + hi)) / (2.0 * kPiD);
    }

    void updateStretchTerm()
    {
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
            sys.setTerm (kTermStretch, stretchGrad, 0.5 * root * kk, true);
        }

        if (jointActive)
        {
            double eta = 0.0;
            for (int i = 0; i < kNumModes; ++i) eta += jointGrad[i] * sys.displacement (i);
            sys.setTerm (kTermJoint, jointGrad, eta, true);
        }
        else
        {
            sys.disableTerm (kTermJoint);
        }
    }

    void applyDamper()
    {
        // Applied to the state directly. The factor is below one by
        // construction, so a damper can never become a source.
        for (int i = 0; i < kNumModes; ++i)
        {
            const double u = sys.displacement (i);
            if (u != 0.0) sys.scaleMode (i, damperFactor);
        }
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
    double stretchGrad[kNumModes] {}, jointGrad[kNumModes] {};

    double stretchEA = 0.0, stretchGain = 0.5;
    bool   jointActive = false;
    double damperFactor = 1.0;
    double tineLength = 0.1;
    double staticOffset = 0.0, staticGap = 1.5e-3, restFlux = 0.0;

    int  note = 60;
    bool sounding = false, held = false, pedal = false, configured = false;
    double lastTipV = 0.0, lastTipH = 0.0;
    double vHist[3] {}, hHist[3] {};
    int  controlCounter = 0;
    Rng  rng { 0x12345u };
};

} // namespace epi
