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
// One Clavinet D6 note: a struck-and-held string read by two bar pickups.
//
// Everything here executes docs/clavinet-implementation-plan.md against the
// measurements in docs/research/clavinet-measured.md. The finding that decides
// the architecture: the rubber tangent pins the string to a metal anvil for
// the WHOLE note — strike and fret are the same object — so the resonator
// carries a full harmonic series and, for once in this plugin, the resonator
// makes the timbre while the transducer adds colour. The two sounds that say
// "Clavinet" are both geometry, not effects:
//
//   - the position combs: each pickup reads the string at distance d from the
//     tailpiece termination, so mode k's readout weight is sin(k pi d/L) —
//     zero wherever k d/L is an integer. For the analyzed A2 geometry that is
//     a notch at every 5th partial [M, EURASIP Fig. 3], and the modal readout
//     tracks the stretched partials exactly, which the paper's delay-line
//     comb could not afford to (+25% cost, dropped) [R, EURASIP 3.3];
//   - the release drop: the tangent lets go, the yarn-wrapped dead length
//     rejoins the speaking length, and the pitch falls three semitones
//     everywhere on the keyboard [M, EURASIP Fig. 5]. Modelled as an in-place
//     retune of every mode by 2^(-1/4) with the state kept — the measured
//     spectrogram shows the partials GLIDING down through the release, which
//     is what retune-in-place produces. The dead length is never simulated
//     as a second system [D]: while held it is yarn-damped and silent; at
//     release its only audible effect is the length it adds, which the
//     retune is.
//
// Modal (SavModalSystem), not the paper's waveguide [D]: the CP-70's
// stiff-string bank already carries strings of this order, and the modal form
// buys dispersion-exact combs, directly-set per-partial decay, and a release
// retune that is a coefficient update rather than a delay-line splice.
//
// The voice is linear after the strike (beat nulls and 1-2 cent f0 stability
// are measured [M]); the two SAV slots are reserved, none active. The only
// nonlinearities are the Hunt-Crossley tangent contact (strike only) and the
// pickup flux polynomial at readout.
//
// On top of the papers' instrument sits the practitioner report (research 12,
// [P]): the working owner/tech's account of what the papers' mint-condition
// analysis does not carry. Five mechanisms, all built at voice level:
//
//   - the CASE ("the hard thing to simulate... a big resonator that
//     involuntarily lands in the pickup via structure-borne sound"): a modal
//     plywood box (ClavinetCase below), driven feedforward by the string
//     termination forces and by the key-bottoming thump, and SUBTRACTED from
//     the string displacement at the pickup — the bars are bolted to the
//     case, and a magnetic pickup senses string-minus-bar RELATIVE motion, so
//     case vibration reaches the DI without radiating a thing. That is how
//     this claim and EURASIP's measured "minimal energy transfer to the
//     body / feeble acoustic output" are BOTH true: the paper measured the
//     airborne path, the practitioner hears the structure-borne one;
//   - per-key CONTACT SCATTER ("stamped from a long sheet-metal strip...
//     the rubbers are only crimped into the holders and some sit crooked"):
//     deterministic per-note scatter of the contact stiffness, exponent and
//     compliance corner (configure);
//   - WEAR NOTCHES ("the rubbers get notches and the string catches in them
//     and clicks — different for every key"): Config::wearAmount, a per-key
//     usage profile, a stick-slip release catch and a low-velocity seat
//     roughness (doRelease / process), all exactly zero at wearAmount 0;
//   - MARGINAL SEATING ("playing quietly it gets absolutely adventurous...
//     every string does its own thing"): the stamped bracket's compliance is
//     a real series spring under the tangent (TangentContact below), and at
//     low tangent velocity the tip chatters on it — multiple measured
//     contact episodes from the contact DYNAMICS, no injected noise.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The string workshop: what is known about the D6's sixty strings, and how
// the gaps are bridged. All of it consumed as data, CP70Inharmonicity-style.
// ---------------------------------------------------------------------------
struct ClavinetScale
{
    static constexpr int kLowKey  = 29;   // F1
    static constexpr int kHighKey = 88;   // E6
    // EURASIP counts 23 wound strings, so the wound/plain break falls between
    // MIDI 51 (D#3) and 52 (E3), at the ~150 Hz timbre discontinuity they
    // measured. (VV's gauge chart implies 24 — one key of disagreement,
    // research 11.4; the B step below follows EURASIP since B is theirs.)
    static constexpr int kLastWound = 51;

    // Inharmonicity, from the six measured anchors (EURASIP Table 1),
    // log-interpolated in two segments that meet in a step at the wound/plain
    // break. The paper prints "D3" twice; the two rows bracket the break, so
    // the second is read as the plain side (research 11 explains). Outside
    // the compass the end segments' log-slopes continue — invention beyond
    // the real instrument, consistent with the other voices [D].
    static double inharmonicity (double midi)
    {
        // Wound segment: F1, A1, D3. Plain segment: E3, F5, E6.
        static constexpr double kWm[3] = { 29.0, 33.0, 50.0 };
        static constexpr double kWb[3] = { 5.0e-4, 2.0e-4, 9.0e-5 };
        static constexpr double kPm[3] = { 52.0, 77.0, 88.0 };
        static constexpr double kPb[3] = { 1.0e-4, 9.0e-5, 8.0e-5 };

        auto seg = [] (double m, const double* km, const double* kb)
        {
            if (m <= km[0])
            {
                const double s = std::log (kb[1] / kb[0]) / (km[1] - km[0]);
                return kb[0] * std::exp (s * (m - km[0]));
            }
            if (m >= km[2])
            {
                const double s = std::log (kb[2] / kb[1]) / (km[2] - km[1]);
                return kb[2] * std::exp (s * (m - km[2]));
            }
            const int i = m <= km[1] ? 0 : 1;
            const double t = (m - km[i]) / (km[i + 1] - km[i]);
            return kb[i] * std::pow (kb[i + 1] / kb[i], t);
        };
        return midi <= kLastWound + 0.5 ? seg (midi, kWm, kWb) : seg (midi, kPm, kPb);
    }

    // Speaking length. One anchor exists in any source: 67.8 cm at 161 Hz
    // [M, DAFx-12 3.1], a string at the wound/plain break, giving wave speed
    // c = 2 L f0 = 218 m/s there. The plan drafted piecewise-constant c per
    // gauge tier; that construction is unbuildable — requiring L monotone
    // through the tier boundaries forces the bass past 2.3 m on a ~1.2 m
    // instrument, because a constant-c tier halves its length per octave.
    // What real short-scale basses do instead is saturate the length and let
    // the winding carry the pitch, so the reconstruction follows the CP-70
    // wound-bass pattern [D, open question 1]:
    //   - plain tier (E3 up): constant c = 218 m/s from the anchor, so
    //     L = c/(2 f0) — constant tension per gauge, "relatively slack"
    //     (16 N on .009 wire) as the manuals say;
    //   - wound tier: log-interpolated from the break down to 0.95 m at F1,
    //     a case-width bass length. 0.95 is invented; it is THE open
    //     question 1 number, and the comb rows carry tolerance because of it.
    static constexpr double kWaveSpeed = 218.3;   // m/s, 2 * 0.678 * 161
    static constexpr double kBassLen   = 0.95;    // m at F1 — open question 1

    static double lengthM (double midi)
    {
        const double breakL = kWaveSpeed / (2.0 * noteHz (kLastWound + 1));
        if (midi >= kLastWound + 1)
            return kWaveSpeed / (2.0 * noteHz (midi));
        const double t = (midi - kLowKey) / static_cast<double> (kLastWound + 1 - kLowKey);
        // Extrapolated below F1 along the same log law, floored near the case.
        return std::min (1.15, kBassLen * std::pow (breakL / kBassLen, t));
    }

    // Gauge tiers from the Vintage Vibe / Mapes replacement set (research 2),
    // with VV's own .011 addition excluded — the original Hohner set ran
    // plain .009 from the break up. Inches to metres.
    static double gaugeM (int midi)
    {
        if (midi <= 38) return 0.032 * 0.0254;             // F1-D2
        if (midi <= 43) return 0.028 * 0.0254;             // D#2-G2
        if (midi <= kLastWound) return 0.022 * 0.0254;     // G#2-D#3
        return 0.009 * 0.0254;                             // plain
    }

    // Mass per length: solid wire for the plain tier; wound tiers as the
    // solid-steel equivalent of the outer gauge with a 0.85 packing factor.
    static double massPerLength (int midi, const Material& mat)
    {
        const double d = gaugeM (midi);
        const double solid = static_cast<double> (mat.density) * kPiD * d * d / 4.0;
        return midi <= kLastWound ? 0.85 * solid : solid;
    }

    // Pickup distances from the tailpiece termination [M, DAFx-12 2]:
    // bridge constant 4 cm; center 18.5 cm (lowest string) to 6.5 cm
    // (highest), linear across the sixty keys, clamped outside.
    static double bridgeDistM (int) { return 0.04; }
    static double centerDistM (int midi)
    {
        const double t = std::clamp ((midi - kLowKey) / static_cast<double> (kHighKey - kLowKey), 0.0, 1.0);
        return 0.185 - (0.185 - 0.065) * t;
    }

    // Base per-partial decay in dB/s. Measured targets, not a loss-filter
    // shape [M, EURASIP 2.3.3]: low/mid sustain T60 of 20 s or more (2.8 dB/s
    // at A2's fundamental gives 21 s), high notes shorter, T60 falling with
    // partial number, the lowest 2-4 partials ringing markedly longest. The
    // two-term curve is a fit to those constraints; refit against a reference
    // recording set is open question 6.
    static double alphaDbPerS (double f)
    {
        const double x = f / 4000.0;
        return 2.4 + f / 280.0 + 8.0 * x * x;
    }

    static double noteHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
};

// ---------------------------------------------------------------------------
// The case: the plywood box the whole instrument is built into, sensed by the
// pickups as structure-borne sound [P, research 12]. Sixty strings terminate
// on rails screwed to it, every key bottoms out on it, and the pickup bars
// are bolted to it — so its motion appears in the DI as the moving REFERENCE
// FRAME of a relative-motion transducer, while radiating almost nothing
// (EURASIP's "feeble acoustic output" stands untouched).
//
// The mode ladder is derived, not fitted: the case is roughly 100 x 40 x
// 12 cm [P-class dimension, a D6 measured across the keyboard]; its main
// panel is treated as a simply-supported rectangular plate 1.00 x 0.40 m of
// 12 mm plywood — the shallow box's walls stiffen the edges toward
// simply-supported, the classic thin-box treatment. Constants assumed and
// cited: E_eff 9 GPa (cross-laminated birch veneer: 13 GPa along grain
// [kBodyMaterials birch ply] averaged against the ~5 GPa across-grain
// plies), rho 680 kg/m^3, nu 0.3, so
//
//   D = E h^3 / (12 (1 - nu^2)) = 1424 N m,  rho h = 8.16 kg/m^2,
//   f_mn = (pi/2) sqrt(D/(rho h)) ((m/Lx)^2 + (n/Ly)^2) = 20.75 (m^2 + 6.25 n^2)
//
// which puts the first six modes at 150-600 Hz — the low-mid band the
// practitioner describes. Modal mass rho h Lx Ly / 4 = 0.816 kg per mode
// (simply-supported plate, every mode), i.e. KILOGRAMS against a string's
// fraction of a gram: the case's back-reaction on a string is smaller than
// the drive by that mass ratio, which is why the drive below is honest
// FEEDFORWARD (the project law for high-Q banks) and not a two-way spring.
// Losses: eta 0.03 (birch-ply internal 0.015 [kBodyMaterials] plus joint and
// mounting losses of the same order), T60 = 6.9078/(pi f eta).
//
// Drive and readout are DIFFERENT physical ports of the same plate — the
// tailpiece rail takes the force, the pickup mount is read — so no
// reciprocity pairing binds them (the reciprocity rule guards a single
// coupling port sensed and forced through one transformation; a feedforward
// path with separate in/out ports has no loop to destabilize and no damping
// identity to break). The key-bottoming thump enters at a third, per-key
// point along the keyboard.
//
// ---- THE FIT SURFACE ------------------------------------------------------
// The practitioner has offered isolation recordings (key presses, string
// pull-offs) of the real instrument. When they arrive, refit HERE and only
// here: kHz (mode frequencies), kT60 (decay), and the voice-side levels
// kCaseSense / kThumpForceN (ClavinetVoice). Everything else consumes these.
// ---------------------------------------------------------------------------
class ClavinetCase
{
public:
    static constexpr int kModes = 6;

    // (m,n) = {1,1} {2,1} {3,1} {4,1} {1,2} {2,2} of the plate above.
    static constexpr double kHz[kModes]  = { 150.5, 212.7, 316.5, 461.7, 539.6, 601.8 };
    static constexpr double kT60[kModes] = { 0.49, 0.34, 0.23, 0.16, 0.14, 0.12 };
    static constexpr double kModalMass   = 0.816;   // kg, rho h Lx Ly / 4

    // Port positions in plate fractions (xi along the 1.0 m keyboard axis,
    // eta across the 0.4 m depth). Tailpiece rail and pickup mount both sit
    // toward the treble end; the exact points are geometry estimates and the
    // read/drive weights follow from the mode shapes, not from tuning.
    static constexpr double kDriveXi = 0.85, kDriveEta = 0.42;
    static constexpr double kReadXi  = 0.72, kReadEta  = 0.38;

    // sin(m pi xi) sin(n pi eta) for the six (m,n) pairs.
    static void pointShape (double xi, double eta, double* w)
    {
        static constexpr int kM[kModes] = { 1, 2, 3, 4, 1, 2 };
        static constexpr int kN[kModes] = { 1, 1, 1, 1, 2, 2 };
        for (int m = 0; m < kModes; ++m)
            w[m] = std::sin (kM[m] * kPiD * xi) * std::sin (kN[m] * kPiD * eta);
    }

    void prepare (double sampleRate)
    {
        sys.prepare (sampleRate);
        sys.setNumModes (kModes);
        pointShape (kReadXi, kReadEta, shapeRead);
        applyBody();
        reset();
    }

    // Material and size, the shared kBodyMaterials physics (the Harp's
    // pattern): frequencies scale with sqrt(E/rho)/s, drive with
    // 1/(rho s^3), internal loss adds on rates; index 0 at size 0.5 is
    // bit-exact stock. State is kept across retunes so a live sweep
    // re-pitches the ring instead of cutting it.
    void setBody (int materialIndex, double sizeNorm)
    {
        const double s = std::pow (1.43, 2.0 * std::clamp (sizeNorm, 0.0, 1.0) - 1.0);
        if (materialIndex == bodyMat && std::abs (s - bodySize) < 1.0e-9) return;
        bodyMat = materialIndex;
        bodySize = s;
        applyBody();
    }

    void reset() { sys.clear(); sys.setNumModes (kModes); }

    void addForceAt (const double* w, double f)
    {
        f *= forceScale;
        for (int m = 0; m < kModes; ++m) sys.addForce (m, f * w[m]);
    }

    void tick() { sys.tick(); }
    double displacement() const { return sys.displacementAt (shapeRead); }
    double modeHz (int m) const { return (m >= 0 && m < kModes) ? sys.frequency (m) : 0.0; }
    double energy() const { return sys.energy(); }

private:
    void applyBody()
    {
        const BodyScalers sc = bodyScalers (bodyMat);
        const double fScale = sc.freq / bodySize;
        forceScale = 1.0 / (sc.mass * bodySize * bodySize * bodySize);
        for (int m = 0; m < kModes; ++m)
        {
            const double f = kHz[m] * fScale;
            const double sigma = 6.9078 / kT60[m] + kPiD * f * sc.etaAdd;
            if (sys.frequency (m) > 0.0)
                sys.retuneKeepingState (m, f, 6.9078 / sigma);
            else
                sys.setMode (m, f, 6.9078 / sigma, kModalMass);
        }
    }

    double forceScale = 1.0;
    int    bodyMat = 0;
    double bodySize = 1.0;
    SavModalSystem<kModes, 1> sys;
    double shapeRead[kModes] {};
};

// ---------------------------------------------------------------------------
// The tangent with its stamped bracket. The rubber tip does not ride the key
// rigidly: it is crimped into a holder stamped from sheet metal and bent up,
// "somewhat flexible" [P, research 12]. That flex is a series compliance
// between the moving key mass and the contact, and it is what makes quiet
// playing "adventurous": at low tangent velocity the tip's bracket resonance
// gets several periods inside the d/v crossing time and the tip CHATTERS —
// separate measured contact episodes — where a forte stroke punches through
// in one. No noise is injected anywhere in this class; the chatter is the
// two-mass contact dynamics.
//
// Bracket stiffness estimate, stated so it can be re-fitted: a bent-up tab
// of 0.7 mm sheet steel, ~12 mm free length, ~10 mm wide, as a cantilever
// k = 3EI/L^3 with I = w t^3 / 12 = 2.86e-13 m^4 gives k ~ 1.0e5 N/m. With
// the tip's share of the moving mass (~1 g at mid compass) that puts the
// bracket resonance near 1.5 kHz: period ~0.65 ms against a pp crossing time
// of 2.6 ms (chatter room) and an ff crossing of 0.66 ms (one clean shove) —
// exactly the registration the practitioner describes.
//
// Both coordinates are integrated semi-implicitly (velocity first), the same
// symplectic scheme as the free hammer; the contact force law and its
// clamping are HuntCrossleyHammer's, applied between the TIP and the string.
// The read (string displacement/velocity at the strike shape) and the write
// (force through the same shape) stay symmetric — the reciprocity rule.
// ---------------------------------------------------------------------------
class TangentContact
{
public:
    struct Config
    {
        double mass       = 0.004;   // kg, total moving mass at the tip
        double stiffness  = 2.0e8;   // N/m^alpha, Hunt-Crossley
        double alpha      = 2.3;
        double lambda     = 1.0;     // s/m, hysteretic loss
        double bracketK   = 1.0e5;   // N/m, stamped-holder cantilever
        double bracketZeta= 0.10;    // its loss ratio (crimped joint)
        double tipFrac    = 0.35;    // share of the mass riding the bracket
    };

    void prepare (double sampleRate) { fs = sampleRate; reset(); }

    void reset()
    {
        keyPos = keyVel = tipRel = tipRelV = 0.0;
        inFlight = contacted = inContact = false;
        contactSamples = touchedSamples = nEpisodes = 0;
    }

    void strike (double speed, double gap)
    {
        keyPos = -std::max (0.0, gap);
        keyVel = std::max (0.01, speed);
        tipRel = tipRelV = 0.0;
        inFlight = true;
        contacted = inContact = false;
        contactSamples = touchedSamples = nEpisodes = 0;
    }

    bool isActive() const { return inFlight; }
    bool hasTouched() const { return contacted; }
    int  contactDurationSamples() const { return contactSamples; }
    // Samples since first touch: the seat clock. The anvil arrives d/v after
    // the launch whether or not the tip is chattering, so the seat is keyed
    // on elapsed time, not on in-contact time.
    int  sinceTouchSamples() const { return touchedSamples; }
    int  episodes() const { return nEpisodes; }
    void retire() { inFlight = false; }

    double tick (double surfaceDisplacement, double surfaceVelocity, const Config& cfg)
    {
        if (! inFlight) return 0.0;

        const double dt   = 1.0 / fs;
        const double mTip = std::max (1.0e-6, cfg.tipFrac * cfg.mass);
        const double mKey = std::max (1.0e-6, (1.0 - cfg.tipFrac) * cfg.mass);
        const double cBr  = 2.0 * cfg.bracketZeta * std::sqrt (cfg.bracketK * mTip);

        const double compression = (keyPos + tipRel) - surfaceDisplacement;
        double force = 0.0;
        if (compression > 0.0)
        {
            if (! inContact) { inContact = true; ++nEpisodes; }
            contacted = true;
            ++contactSamples;
            const double rate = (keyVel + tipRelV) - surfaceVelocity;
            force = cfg.stiffness * std::pow (compression, cfg.alpha)
                  * (1.0 + cfg.lambda * rate);
            force = std::max (0.0, force);   // contact cannot pull
        }
        else
        {
            inContact = false;
        }
        if (contacted) ++touchedSamples;

        // Bracket force on the key, positive toward the string when the tip
        // leads (tipRel > 0); minus that on the tip, minus the contact force.
        const double fBr = cfg.bracketK * tipRel + cBr * tipRelV;
        tipRelV += (-fBr * (1.0 / mTip + 1.0 / mKey) - force / mTip) * dt;
        keyVel  += (fBr / mKey) * dt;
        tipRel  += tipRelV * dt;
        keyPos  += keyVel * dt;

        // Turned around without ever touching: cannot reach the string.
        if (! contacted && keyVel <= 0.0) inFlight = false;
        return force;
    }

private:
    double fs = 48000.0;
    double keyPos = 0.0, keyVel = 0.0;     // key ram, absolute
    double tipRel = 0.0, tipRelV = 0.0;    // tip relative to the key
    bool   inFlight = false, contacted = false, inContact = false;
    int    contactSamples = 0, touchedSamples = 0, nEpisodes = 0;
};

class ClavinetVoice
{
public:
    // Worst case F1 at 48 kHz carries ~121 partials under the fs/pi budget
    // [C, plan 1.4]; one more slot holds the beat partner. Shared constant
    // with the CP-70, which needs 129.
    static constexpr int kMaxModes = 132;
    using System = SavModalSystem<kMaxModes, 2>;   // SAV slots reserved, none
                                                   // active: the measured tone
                                                   // is linear in the string

    // The transducer runs at four times the mechanics — the house pattern.
    // The plan drafted base-rate processing gated by the alias row; the
    // oversampled path costs two 4th-order Horner evaluations per subsample
    // and passes that gate with margin instead of at it.
    static constexpr int kOver = 4;

    enum { tapCenter = 0, tapBridge = 1 };

    struct Config
    {
        // Action
        double hammerHardness = 0.5;
        double hammerMassNorm = 0.5;
        double escapementNorm = 0.4;   // tangent-string rest distance, the d of
                                       // EURASIP Eq. 7
        double damperGrip     = 0.6;   // yarn efficacy: 1 = mint wool, low =
                                       // aged/compressed (slow release, drop
                                       // exposed) [R, plan 5]

        // Transducer
        double gapNorm        = 0.5;   // string-pickup rest distance: the
                                       // operating point on the flux curve,
                                       // the knob DAFx-12 demonstrated
        double pickupSel      = 0.0;   // 0 center, 1 bridge, 2 sum in phase,
                                       // 3 sum anti-phase; center is the
                                       // switch-down rest position [R]

        // Resonator
        double dampTrim       = 0.5;   // global alpha trim x0.7..1.5
        double detuneCents    = 0.0;   // master tune + bend

        // 0 Magnetic, 1 Native (= the twin bar pickups), 2 Electro, 3 Contact.
        double transducer     = 1.0;
        double material       = 0.0;   // index into kMaterials; 0 = stock

        // The practitioner layer (research 12), appended so older layouts
        // keep their field offsets.
        double wearAmount     = 0.0;   // tangent-rubber notching: 0 = new
                                       // rubbers (the papers' instrument, and
                                       // bit-compatible by construction),
                                       // 1 = gigged-hard
        double caseAmount     = 1.0;   // case-into-pickup sense level; 1 is
                                       // the calibrated point (row C2), 0
                                       // removes the structure-borne path
        double caseBodyMat    = 0.0;   // kBodyMaterials index for the case
        double caseBodySize   = 0.5;   // case size norm; 0.5 = stock
    };

    // Compared byte-for-byte by the engine to decide whether the instrument
    // needs rebuilding, so it must have no padding for that comparison to
    // mean what it says.
    static_assert (std::is_trivially_copyable<Config>::value, "Config must be memcmp-able");
    static_assert (sizeof (Config) == 14 * sizeof (double), "Config has padding");

    void prepare (double sampleRate, const MagneticPickup* sharedField = nullptr)
    {
        fs = sampleRate;
        field = sharedField;
        sys.prepare (sampleRate);
        hammer.prepare (sampleRate);
        kase.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        sys.clear();
        sys.setNumModes (0);
        hammer.reset();
        kase.reset();
        sounding = held = released = false;
        pedalAmt = 0.0;
        yarnRamp = 0.0;
        yarnEff = 1.0;
        controlCounter = 0;
        sinceStrike = 1.0e9;
        peakEnergy = 1.0e-30;
        configured = false;
        for (int i = 0; i < 3; ++i) { cHist[i] = bHist[i] = 0.0; }
        fluxPrev[0] = fluxPrev[1] = 0.0;
        fluxPrimed = false;
        thumpRemaining = thumpTotal = 0;
        thumpAmp = 0.0;
        caseRingRemaining = 0;
        stickRemaining = -1;
        slipRemaining = 0;
        slipAmp = 0.0;
        roughAmt = 0.0;
        roughLp = 0.0;
    }

    // The beating below E4 [M, EURASIP 2.3.4]: 0.5-2 Hz, up to 15 dB p-p,
    // intermittent, mechanism unknown even to the source ("still not
    // understood"). Shipped as a seeded beat-partner mode at f0 + delta,
    // modest depth, keys up to E4 only — PHENOMENOLOGICAL, same status as the
    // Wurlitzer's 2.4 Hz partner, retired by the same measurement (sideband
    // spacing vs harmonic index) when someone takes it. Depth is the
    // partner's launch level relative to the fundamental; 0.25 is ~4.4 dB
    // p-p on the fundamental.
    void setBeat (double depth) { beatDepth = std::clamp (depth, 0.0, 0.7); }

    bool isSounding() const { return sounding; }
    bool isRinging() const { return sounding || hammer.isActive() || caseRingRemaining > 0; }
    bool isHeld() const { return held; }
    int  noteNumber() const { return note; }
    int  contactSamples() const { return std::max (lastContactSamples, hammer.contactDurationSamples()); }
    double modalEnergy() const { return sys.energy(); }
    double soundingHz() const { return f0Speaking; }
    double strikeVelocity() const { return lastTipV; }

    // Telemetry for the reference rows: the voice's own geometry and modal
    // recipe, so the suite can assert on tap weights and per-mode decay
    // without rendering (rows G1/T2).
    int    partialCount() const { return numPartials; }
    double stringLengthM() const { return strLen; }
    double pickupDistOverL (int tap) const { return tap == tapBridge ? dBridge / strLen : dCenter / strLen; }
    double tapWeight (int tap, int k) const
    {
        if (k < 0 || k >= numPartials) return 0.0;
        return tap == tapBridge ? shapeB[k] : shapeC[k];
    }
    double modeFrequency (int k) const { return (k >= 0 && k < numPartials) ? speakFreq[k] : 0.0; }
    double modeT60 (int k) const { return (k >= 0 && k < numPartials) ? 60.0 / alphaOfMode[k] : 0.0; }

    // Practitioner-layer telemetry, so the suite can assert on the scatter,
    // the case ladder and the seating without rendering.
    double caseModeHz (int m) const { return kase.modeHz (m); }
    double contactStiffness() const { return hammerCfg.stiffness; }
    double contactAlpha() const { return hammerCfg.alpha; }
    int    contactEpisodes() const { return hammer.episodes(); }
    double wearOfKey() const { return wearEff; }

    // The resonator's own motion at the center tap, for rows that measure
    // decay without the derivative's tilt.
    double centerDisplacement() const { return sys.displacementAt (shapeC); }

    void setNote (int midiNote, const Config& cfg)
    {
        note = midiNote;
        configure (cfg);
    }

    void noteOn (int midiNote, double velocity, const Config& cfg, std::uint32_t seed)
    {
        (void) seed;   // per-note determinism hashes the note number directly
        if (! configured || midiNote != note) setNote (midiNote, cfg);

        // A re-struck string is re-clamped first: the tangent pins it back to
        // the anvil, so the modes return to the speaking-length frequencies
        // with whatever state still rings carried along.
        if (released) reClamp();

        held = true;
        sounding = true;
        sinceStrike = 0.0;
        sys.setNumModes (numModes);

        // Tangent velocity: 1-4 m/s mapped linearly over the velocity range
        // [M, EURASIP 3.2]. The flight distance is the tangent-string rest
        // gap, the d of their Eq. 7.
        const double vel = std::clamp (velocity, 0.0, 1.0);
        lastTipV = 1.0 + 3.0 * vel;
        const double gapMm = 3.0 * (0.4 + 1.2 * std::clamp (cfg.escapementNorm, 0.0, 1.0));
        hammer.strike (lastTipV, gapMm * 1.0e-3);

        // The anvil seat. A free hammer against a string is stopped by the
        // string's wave impedance on a time m/(2 Z0) — 5 ms on a mid string,
        // measured on this very model — but the tangent is not stopped by
        // the string: it punches through and SEATS on the metal anvil, which
        // takes the rest of the momentum. The contact force therefore lasts
        // the tangent's crossing time d/v — exactly the excitation-pulse
        // length EURASIP Eq. 7 prescribes (N = fs d/v) — and the seat is the
        // plan's "contact clamps and stays": once seated, the termination is
        // the anvil and no further tangent work is done. Harder hits seat
        // sooner, which is where "a heavier touch enhances the proportion of
        // overtones" [R, E7 manual] comes from.
        seatSamples = std::max (8, static_cast<int> (fs * gapMm * 1.0e-3 / lastTipV));
        lastContactSamples = 0;

        // Wear, per strike: the worn seat lands in the notch unevenly at low
        // velocity — roughness on the contact force, full at the 1 m/s bottom
        // of the tangent range, gone by 2 m/s [P, research 12.3]. The noise
        // is deterministic per key and only shapes a force that is already a
        // bounded-time drive; depth <= 0.85 keeps the force nonnegative.
        stickRemaining = -1;
        slipRemaining = 0;
        roughAmt = std::min (0.85, 1.7 * wearEff * std::clamp (2.0 - lastTipV, 0.0, 1.0));
        roughRng = Rng (noteHash ^ 0x7f4a7c15u);
        roughLp = 0.0;

        // The key bottoms on the keybed: a raised-cosine force impulse into
        // the case, velocity-scaled — "the case gets extremely excited just
        // by pressing the keys... like someone knocking on wood" [P,
        // research 12.5]. It reaches the DI only through the case-to-pickup
        // path below, which is exactly how the real one gets out.
        startThump (kThumpForceN * (0.25 + 0.75 * vel));
    }

    void noteOff()
    {
        // The key returns against its rest rail: a weaker second knock.
        if (held) startThump (kThumpForceN * kThumpReturn * (0.25 + 0.75 * std::clamp ((lastTipV - 1.0) / 3.0, 0.0, 1.0)));
        held = false;
    }

    // The real instrument has no pedal; ours holds the tangent state [D,
    // plan 5] — keys stay clamped, the retune and the yarn happen when the
    // pedal lifts. Half-pedal: pedal travel lifts the yarn rail linearly, the
    // wool pushes back as compression^2.5 (the felt-class exponent), so the
    // damping RATE scales as damperFactor^((1-pedal)^2.5).
    void setPedal (double amount)
    {
        pedalAmt = std::clamp (amount, 0.0, 1.0);
        yarnEff = std::pow (effectiveDamper(), yarnRamp);
    }

    // The yarn, due when the tangent has left and the pedal is not holding
    // it. The first due sample performs the release itself: every mode
    // retunes in place by 2^(-1/4) — the measured three-semitone drop, fixed
    // geometry, not a parameter — and the damping ramps in over a few tens
    // of ms as the freed string settles into the wool.
    void applyDamperIfDue()
    {
        if (held || pedalAmt >= 1.0) return;
        if (! released)
        {
            // A worn notch catches the string before the yarn takes it [P,
            // research 12.3]: the tangent-end termination holds a few ms
            // longer. The counter is armed and run in process() (this method
            // is also called by the engine, and a second decrement per
            // sample would halve the hold); at wear 0 it arms to zero and
            // the mint release is bit-identical.
            if (stickRemaining != 0) return;
            doRelease();
        }
        if (yarnEff >= 1.0) return;
        for (int m = 0; m < sys.numModes(); ++m)
            if (sys.displacement (m) != 0.0)
                sys.scaleMode (m, yarnEff);
    }

    // One sample of string, kOver subsamples of transduction. Writes the
    // selected switch-matrix pickup voltage at the oversampled rate; the
    // caller owns the tone stack, preamp and decimation (ClavinetChain.h).
    void process (const Config& cfg, double* out)
    {
        if (! sounding && ! hammer.isActive() && caseRingRemaining <= 0)
        {
            for (int k = 0; k < kOver; ++k) out[k] = 0.0;
            return;
        }

        if (hammer.isActive())
        {
            const double u = sys.displacementAt (shapeStrike);
            const double v = sys.velocityAt (shapeStrike);
            double f = hammer.tick (u, v, hammerCfg);
            if (f != 0.0)
            {
                sounding = true;
                // The worn seat's roughness [P, research 12.3]: the notched
                // rubber modulates the transmitted force at low velocity.
                // Deterministic per key, depth-bounded so the force stays
                // positive; it shapes a bounded-time drive, nothing more.
                if (roughAmt > 0.0)
                {
                    roughLp += 0.35 * (static_cast<double> (roughRng.next()) - roughLp);
                    f *= 1.0 + roughAmt * roughLp;
                }
                // The rubber-cushioned seat taper (see below): a raised
                // cosine over the second half of the dwell. Monotone, once,
                // then the contact ends for good — it can only remove drive.
                // The dwell clock is samples since first touch: with the
                // bracket in the chain the tip may chatter at pp, but the
                // anvil still arrives d/v after the launch.
                const int cs = hammer.sinceTouchSamples();
                double w = 1.0;
                if (2 * cs > seatSamples)
                    w = 0.5 * (1.0 + std::cos (kPiD * (2.0 * cs - seatSamples)
                                               / static_cast<double> (seatSamples)));
                for (int m = 0; m < numModes; ++m)
                    sys.addForce (m, f * w * shapeStrike[m]);
            }
            lastContactSamples = std::max (lastContactSamples, hammer.contactDurationSamples());
            // Seated on the anvil (see noteOn): the stud takes the remaining
            // momentum and the tangent stops doing work on the string. The
            // seat is cushioned by the rubber itself — the measured
            // excitation is a SMOOTH triangle [M, EURASIP Fig. 4/12] — so
            // the force tapers over the second half of the dwell instead of
            // cutting; a hard cut is a force step, and its broadband splat
            // measured 20 dB of extra high-partial launch.
            if (hammer.sinceTouchSamples() >= seatSamples)
                hammer.retire();
        }

        // The worn-notch release catch: arm and run the hold here, once per
        // sample (applyDamperIfDue is also called by the engine).
        if (! held && pedalAmt < 1.0 && ! released)
        {
            if (stickRemaining < 0)
                stickRemaining = static_cast<int> (fs * kStickMaxS * wearEff);
            else if (stickRemaining > 0)
                --stickRemaining;
        }

        // The let-go slip: the notch was holding a share of the termination
        // force; when the string pops out, that force releases as ONE
        // short pulse through the tangent end — the same shape vector that
        // senses and forces there, tip-compliance band and all. A half-sine
        // of single polarity (a white-noise force was tried first and
        // random-walked to nothing audible), roughened by the notch surface.
        if (slipRemaining > 0)
        {
            const double ph = static_cast<double> (slipTotal - slipRemaining--) / slipTotal;
            const double f = slipAmp * std::sin (kPiD * ph)
                           * (0.7 + 0.3 * static_cast<double> (slipRng.next()));
            for (int m = 0; m < numModes; ++m)
                sys.addForce (m, f * shapeStrike[m]);
        }

        sys.tick();
        applyDamperIfDue();

        // The case, strictly feedforward [P, research 12.1]: the string's
        // termination force T dy/dx at the tailpiece drives the rail, the
        // key thump drives the keybed point, and what the pickup senses is
        // the case's motion under its own mount. Back-reaction on the string
        // is omitted on the mass ratio (kilograms of plate against a
        // fraction of a gram of string — see ClavinetCase).
        double cs = 0.0;
        if (caseOn)
        {
            kase.addForceAt (shapeCaseDrive, sys.displacementAt (shapeTerm));
            if (thumpRemaining > 0)
            {
                const int i = thumpTotal - thumpRemaining--;
                const double w = 0.5 * (1.0 - std::cos (2.0 * kPiD * i / thumpTotal));
                kase.addForceAt (shapeCaseThump, thumpAmp * w);
            }
            kase.tick();
            cs = caseGain * kase.displacement();
        }
        if (caseRingRemaining > 0) --caseRingRemaining;

        if (++controlCounter >= kControlDecim)
        {
            controlCounter = 0;
            sinceStrike += kControlDecim / fs;
            controlTick();
            if (! sounding && ! hammer.isActive())
            {
                for (int k = 0; k < kOver; ++k) out[k] = 0.0;
                return;
            }
        }

        // The two taps: displacement of the string at each pickup point, as
        // weighted modal sums, MINUS the case's motion under the pickup
        // mount — the bars are bolted to the case, so every mounted
        // transducer reads the RELATIVE coordinate string-minus-bar. That
        // subtraction is sensing only (it pushes on nothing), so it needs no
        // reciprocity partner; the contact swap lane keeps reading the
        // termination force and is untouched. Both paths between this sample
        // and the last are band-limited (fs/pi mode ceiling, 600 Hz case
        // ceiling), so reconstructing them at kOver points is legitimate —
        // the Rhodes staircase lesson — and the curved flux law is evaluated
        // at each.
        const double yc = sys.displacementAt (shapeC) - cs;
        const double yb = sys.displacementAt (shapeB) - cs;
        if (! std::isfinite (yc) || ! std::isfinite (yb)) { recover (out); return; }

        const double fsOs = fs * kOver;
        for (int k = 0; k < kOver; ++k)
        {
            const double t = static_cast<double> (k + 1) / kOver;
            double oc, ob;
            if (trans == 1)
            {
                // Native: per tap, displacement -> the published flux
                // polynomial -> time derivative -> switch matrix, in the
                // measured signal order (DAFx-12 Fig. 2). The polynomial is
                // evaluated per tap per subsample (Horner, 4 MACs) — cheaper
                // than a table and exactly the source's model. Only the
                // vertical axis exists: along-bar sensitivity measured
                // 25-30 dB down and modelled zero [M].
                const double dc = gap0 - hermite (cHist[0], cHist[1], cHist[2], yc, t);
                const double db = gap0 - hermite (bHist[0], bHist[1], bHist[2], yb, t);
                const double fc = fluxPoly (dc);
                const double fb = fluxPoly (db);
                if (! fluxPrimed) { fluxPrev[0] = fc; fluxPrev[1] = fb; fluxPrimed = true; }
                oc = kPickupOut * (fc - fluxPrev[0]) * fsOs;
                ob = kPickupOut * (fb - fluxPrev[1]) * fsOs;
                fluxPrev[0] = fc;
                fluxPrev[1] = fb;
                // A bare conductor reaches the magnets only as its eddy
                // signal; an insulator not at all -- the shared law.
                if (! matFerro) { oc *= magCoupleC; ob *= magCoupleC; }
            }
            else if (trans == 0)
            {
                // Transducer swap: a point magnetic pickup under the center
                // tap, riding the shared field table (transducers-and-chassis
                // 1.5's reverse of the Clavinet-under-a-tine trick).
                const double vv = hermite (cHist[0], cHist[1], cHist[2], yc, t);
                oc = (field != nullptr)
                       ? (field->flux (static_cast<float> (kMagOff + vv), static_cast<float> (gap0))
                          - magRest) * kMagOut * magCoupleC
                       : 0.0;
                ob = 0.0;
            }
            else if (trans == 2)
            {
                // Electrostatic plate at the center tap.
                double vv = hermite (cHist[0], cHist[1], cHist[2], yc, t);
                vv = std::min (vv, 0.85 * gap0);
                oc = matCond ? kElecOut * (vv / (gap0 - vv)) : 0.0;
                ob = 0.0;
            }
            else
            {
                // Contact: the transverse force at the tailpiece termination,
                // T * dy/dx at x = 0 — what a contact sensor on the rail
                // hears. Precomputed as shapeContact weights.
                const double cf = sys.displacementAt (shapeContact);
                oc = kContactOut * hermite (cfHist[0], cfHist[1], cfHist[2], cf, t);
                ob = 0.0;
                if (k == kOver - 1) { cfHist[0] = cfHist[1]; cfHist[1] = cfHist[2]; cfHist[2] = cf; }
            }

            // The 4-way switch, applied AFTER the nonlinearity [D]: the real
            // switch sums two coil voltages, i.e. two post-flux signals.
            // No renormalisation — anti-phase is quieter on the real
            // instrument and stays quieter here.
            switch (sel)
            {
                case 0:  out[k] = oc; break;
                case 1:  out[k] = ob; break;
                case 2:  out[k] = oc + ob; break;
                default: out[k] = oc - ob; break;
            }
        }
        cHist[0] = cHist[1]; cHist[1] = cHist[2]; cHist[2] = yc;
        bHist[0] = bHist[1]; bHist[1] = bHist[2]; bHist[2] = yb;
    }

    // The published 4th-order flux fit, flux against string-pickup distance
    // in metres (DAFx-12 Table 2 = EURASIP Table 2), Horner form. The
    // printed coefficients reproduce the paper's Fig. 13 shape (monotone
    // falling, decreasing slope) in tenths of tesla; the absolute unit is
    // absorbed by kPickupOut, the level calibration.
    static double fluxPoly (double dMetres)
    {
        const double d = std::clamp (dMetres, 0.3e-3, 20.0e-3);
        return (((1.817e5 * d - 9.508e3) * d + 1.818e2) * d - 1.544) * d + 0.7951;
    }

    void refresh (const Config& cfg) { configure (cfg); }

private:
    static constexpr int kControlDecim = 32;

    // The rubber tip's compliance corner: the tangent cannot follow — or
    // push — string ripple above its own contact resonance. A calibration
    // constant owned by the attack rows, per open question 2 (no tangent
    // rubber measurement exists).
    static constexpr double kContactHz = 2400.0;

    // Pickup bar aperture: each bar is 0.5 cm wide [M, DAFx-12 2], so the
    // readout is the sinc-averaged patch, not a point.
    static constexpr double kBarHalfWidth = 0.0025;   // m

    // The release retune: three semitones down, fixed geometry [M]. Not a
    // parameter.
    static constexpr double kDropRatio = 0.8408964152537145;   // 2^(-1/4)

    // Level calibration: one scale from d(flux)/dt to volts at the pickup
    // output, landing a forte mid note near the 0.2-0.4 V region the
    // measured "400 mV maximum in polyphonic playing" implies. Calibrated by
    // probe against row E1's drive point.
    static constexpr double kPickupOut = 0.12;

    // Swap-lane output constants, level-matched by probe to the native path.
    static constexpr double kMagOff     = -0.9e-3;
    static constexpr double kMagOut     = 4.0;
    static constexpr double kElecOut    = 0.35;
    static constexpr double kContactOut = 0.9;   // benched to the native twin-bar level by the engine suite (was 33 dB shy)

    static double hermite (double y0, double y1, double y2, double y3, double t)
    {
        const double c0h = y1;
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0h;
    }

    double effectiveDamper() const
    {
        return std::pow (damperFactor, std::pow (1.0 - pedalAmt, 2.5));
    }

    // The tangent has left: retune every mode in place by the measured drop,
    // keeping its state — the partials glide, exactly as the release
    // spectrogram shows. The yarn ramp starts from zero here and reaches the
    // full grip over kYarnRampS (controlTick).
    void doRelease()
    {
        released = true;
        yarnRamp = 0.0;
        yarnEff = 1.0;
        for (int m = 0; m < numModes; ++m)
            sys.setFrequency (m, speakFreq[m] * kDropRatio);

        // The worn notch lets go: a short rough transient through the
        // tangent end, scaled by wear times the string's own motion. The
        // force scale is physical: a string popping out of a groove exerts
        // Z0 * v on the termination (Z0 = sqrt(T mu), the wave impedance;
        // the grand damper-grab recipe with the impedance made explicit),
        // with v the string's velocity amplitude sqrt(2 E / m_modal) at the
        // moment of release. kSlipFrac is the dimensionless catch depth at
        // full wear. Zero at wear 0.
        if (wearEff > 0.0)
        {
            slipTotal = std::max (1, static_cast<int> (fs * kSlipS));
            slipRemaining = slipTotal;
            const double vAmp = std::sqrt (std::max (0.0, 2.0 * sys.energy() / modalMassM));
            slipAmp = kSlipFrac * wearEff * z0String * vAmp;
            slipRng = Rng (noteHash ^ 0x9e3779b9u);
        }
    }

    // A note on the seated equilibrium, for whoever calibrates the attack
    // against recordings (open question 2): once the tangent holds the
    // string at the anvil, the true rest shape is the ramp to the seated
    // anvil, and this model's modes ring about the original line instead —
    // the static component survives as extra fundamental-flavoured swing.
    // A post-hoc subtraction of the ramp-projected position at seat time was
    // tried and measured WORSE (at 1 ms in, the low modes hold velocity, not
    // position, so the fit misprojects); the correct treatment is a
    // deviation formulation driven by the tangent's acceleration, and it is
    // only worth its complexity if the attack A/B against real recordings
    // demands it.
    void reClamp()
    {
        released = false;
        yarnRamp = 0.0;
        yarnEff = 1.0;
        for (int m = 0; m < numModes; ++m)
            sys.setFrequency (m, speakFreq[m]);
    }

    void recover (double* out)
    {
        sys.clear();
        kase.reset();
        sounding = false;
        thumpRemaining = 0;
        caseRingRemaining = 0;
        slipRemaining = 0;
        for (int i = 0; i < 3; ++i) { cHist[i] = bHist[i] = cfHist[i] = 0.0; }
        fluxPrimed = false;
        for (int k = 0; k < kOver; ++k) out[k] = 0.0;
    }

    void startThump (double forceN)
    {
        if (! caseOn) return;
        thumpTotal = std::max (1, static_cast<int> (fs * kThumpS));
        thumpRemaining = thumpTotal;
        thumpAmp = forceN;
        // Keep the voice processing long enough for the knock-on-wood ring
        // to play out after the string is gone.
        caseRingRemaining = std::max (caseRingRemaining, static_cast<int> (fs * kCaseRingS));
    }

    void controlTick()
    {
        // Yarn ramp: the damping reaches full grip a few tens of ms after
        // release, as the freed string settles into the wool.
        if (released && yarnRamp < 1.0)
        {
            yarnRamp = std::min (1.0, yarnRamp + kControlDecim / (fs * kYarnRampS));
            yarnEff = std::pow (effectiveDamper(), yarnRamp);
        }

        // Deterministic top-down shrink (the CP-70 practice): mode k has died
        // 90 dB down at t = 90/alpha_k after the strike, so the live count is
        // a pure function of elapsed time.
        {
            int k = sys.numModes();
            while (k > 2 && sinceStrike > 90.0 / alphaOfMode[k - 1]) --k;
            if (k != sys.numModes()) sys.setNumModes (k);
        }

        const double e = sys.energy();
        if (! std::isfinite (e)) { sys.clear(); sounding = false; return; }
        if (e > peakEnergy) peakEnergy = e;

        // Retirement is RELATIVE to this voice's own strike: a 20-second
        // sustain is the instrument, not a leak.
        if (! hammer.isActive() && e < peakEnergy * 1.0e-10)
            sounding = false;
    }

    void configure (const Config& cfg)
    {
        const double f0 = ClavinetScale::noteHz (note)
                        * std::pow (2.0, cfg.detuneCents / 1200.0);
        f0Speaking = f0;

        // Material: B scales as E/rho at fixed pitch and gauge (the CP-70
        // derivation); mass per length follows the density; internal loss
        // adds on top of the fitted clamp-and-air curve.
        const Material& mat = kMaterials[std::clamp (static_cast<int> (cfg.material), 0, kNumMaterials - 1)];
        matFerro = mat.ferro;
        magCoupleC = magneticCoupling (mat);
        matCond = mat.conductive;
        const double matB = (static_cast<double> (mat.youngs) / static_cast<double> (mat.density))
                          / (static_cast<double> (kMusicWire.youngs) / static_cast<double> (kMusicWire.density));
        const double B = ClavinetScale::inharmonicity (note) * matB;

        strLen = ClavinetScale::lengthM (note);
        const double mu = ClavinetScale::massPerLength (note, mat);
        const double T = 4.0 * f0 * f0 * strLen * strLen * mu;
        const double modalMass = 0.5 * mu * strLen;   // pinned-pinned, every mode
        modalMassM = modalMass;
        z0String = std::sqrt (T * mu);   // the string's wave impedance, N s/m

        // In the extrapolated top range (beyond the real E6) the reconstructed
        // string gets shorter than the clamped pickup distances; a pickup
        // cannot read past the tailpiece, so the tap stays on the speaking
        // length. Inside the real compass this never binds (worst case is
        // E6's center pickup at 0.79 L).
        dCenter = std::min (ClavinetScale::centerDistM (note), 0.95 * strLen);
        dBridge = std::min (ClavinetScale::bridgeDistM (note), 0.95 * strLen);

        // ---- the modal recipe ------------------------------------------------
        // Everything under the fs/pi budget, capped at 18 kHz: above that no
        // measured target exists and the cost buys nothing audible.
        const double fMax = std::min (System::kModeBudget * fs, 18000.0);
        const double trim = 0.7 + 0.8 * std::clamp (cfg.dampTrim, 0.0, 1.0);

        // The T60 ripple across partials [M, EURASIP Fig. 8]: period 2-3 x f0
        // in frequency, depth a few seconds at the 15-20 s scale. The paper
        // randomizes its ripple filter per keystroke; here the rate is drawn
        // per NOTE from the measured range, seeded from the note number so
        // renders are reproducible [D, plan 2].
        const std::uint32_t h = static_cast<std::uint32_t> (note) * 2654435761u;
        noteHash = h;
        const double rippleRate = 2.0 + ((h >> 9) & 1023u) / 1023.0;   // 2..3 x f0
        constexpr double kRippleDepth = 0.22;

        // ---- per-key contact scatter [P, research 12.2] ---------------------
        // "Stamped from a long sheet-metal strip and bent up — somewhat
        // flexible; the rubbers are only crimped into the holders and some
        // sit crooked, not flat." Sixty hand-assembled contacts cannot share
        // one stiffness or one geometry: the stiffness scatters +/-30% (a
        // crooked crimp changes the loaded rubber volume by that order), the
        // Hunt-Crossley exponent scatters +/-0.3 around 2.3 (a tilted pad
        // moves the contact between flat-punch-like and edge-like geometry),
        // and the tip's compliance corner follows the stiffness as sqrt(k).
        // The bracket stiffness scatters with the same crimp logic. All
        // deterministic per note — key forty is always key forty.
        const double uS1 = ((h >> 3)  & 1023u) / 1023.0;
        const double uS2 = ((h >> 13) & 1023u) / 1023.0;
        const double uS3 = ((h >> 23) & 511u)  / 511.0;
        const double uW  = ((h >> 5)  & 1023u) / 1023.0;
        const double kScat = 1.0 + 0.30 * (2.0 * uS1 - 1.0);
        const double contactHzKey = kContactHz * std::sqrt (kScat);

        // ---- per-key wear [P, research 12.3] --------------------------------
        // "The rubbers get notches... different for every key depending on
        // use." Usage peaks around the middle of the keyboard: a smooth bump
        // centred near middle C (note 58, sigma 14 keys), times a per-key
        // depth jitter — two neighbours never wear alike. Exactly zero at
        // wearAmount 0, which is the papers' mint instrument.
        {
            const double z = (note - 58.0) / 14.0;
            wearEff = std::clamp (cfg.wearAmount, 0.0, 1.0)
                    * std::exp (-0.5 * z * z) * (0.7 + 0.3 * uW);
        }

        int kV = 0;
        for (int k = 1; k <= kMaxModes; ++k)
        {
            const double fk = k * f0 * std::sqrt (1.0 + B * k * k);
            if (fk >= fMax || kV + 2 >= kMaxModes) break;   // one slot spare
            ++kV;
        }
        numPartials = kV;

        for (int k = 1; k <= kV; ++k)
        {
            const double fk = k * f0 * std::sqrt (1.0 + B * k * k);
            const double ripple = 1.0 + kRippleDepth * std::cos (2.0 * kPiD * fk / (rippleRate * f0));
            // String rule: material loss acts on the bending share only --
            // B k^2/(1+B k^2) -- the tension is geometric and lossless.
            const double a = trim * ClavinetScale::alphaDbPerS (fk) * ripple
                           + 8.686 * kPiD * fk * ((B * k * k) / (1.0 + B * k * k))
                             * std::max (0.0, static_cast<double> (mat.lossEta) - static_cast<double> (kMusicWire.lossEta));
            sys.setMode (k - 1, fk, 60.0 / a, modalMass);
            speakFreq[k - 1] = fk;
            alphaOfMode[k - 1] = a;

            // Readout: sin(k pi d/L) at each pickup distance, with the bar's
            // 0.5 cm aperture folded in as a sinc. This IS the comb — no
            // comb filter, no delay line, and the notches track the
            // stretched partials exactly.
            auto tapW = [&] (double d)
            {
                const double ap = k * kPiD * kBarHalfWidth / strLen;
                const double w = std::abs (ap) < 1.0e-9 ? 1.0 : std::sin (ap) / ap;
                return std::sin (k * kPiD * d / strLen) * w;
            };
            shapeC[k - 1] = tapW (dCenter);
            shapeB[k - 1] = tapW (dBridge);

            // The strike shape: the tangent displaces the TERMINATION, not a
            // point along the string. Pressing the end down by delta puts
            // the segment in the quasi-static ramp profile delta * x/L, whose
            // modal projection is 2 (-1)^(k+1) / (k pi) — falling as 1/k.
            // This is the projection that reproduces the measured launch
            // spectrum: through the derivative's +6 dB/oct it makes partial
            // k launch in proportion to its tap weight alone, which is why
            // the second partial sits a few dB over the fundamental and the
            // third often over the second [M, Fig. 3], where a point strike
            // near the end came out +20 dB over-tilted (measured on this
            // model). Sensing and forcing share the shape — the reciprocity
            // rule this project already paid for — and the tip's compliance
            // rolloff caps what rubber can push.
            const double comp = 1.0 / (1.0 + (fk / contactHzKey) * (fk / contactHzKey));
            const double sgn = (k & 1) ? 1.0 : -1.0;
            shapeStrike[k - 1] = sgn * 2.0 / (k * kPiD) * comp;

            // The contact swap lane reads T * dy/dx at the tailpiece.
            shapeContact[k - 1] = T * k * kPiD / strLen * comp;

            // The case drive reads the same termination force UNFILTERED:
            // the rail feels the string exactly, only the rubber tip has a
            // compliance corner.
            shapeTerm[k - 1] = T * k * kPiD / strLen;
        }

        // ---- the beat partner (see setBeat) ---------------------------------
        // Keys up to E4 only, where the measurement puts it.
        numModes = kV;
        if (note <= 64 && beatDepth > 0.0 && kV >= 1)
        {
            const double delta = 0.5 + 1.5 * ((h >> 19) & 1023u) / 1023.0;   // 0.5..2 Hz
            const int p = kV;
            sys.setMode (p, f0 + delta, 60.0 / alphaOfMode[0], modalMass);
            speakFreq[p] = f0 + delta;
            alphaOfMode[p] = alphaOfMode[0];
            shapeC[p] = shapeC[0];
            shapeB[p] = shapeB[0];
            shapeContact[p] = shapeContact[0];
            shapeTerm[p] = shapeTerm[0];
            shapeStrike[p] = beatDepth * shapeStrike[0];
            numModes = kV + 1;
        }
        for (int i = numModes; i < kMaxModes; ++i)
        {
            shapeC[i] = shapeB[i] = shapeStrike[i] = shapeContact[i] = shapeTerm[i] = 0.0;
            speakFreq[i] = 0.0;
            alphaOfMode[i] = 1.0e9;
        }
        sys.setNumModes (numModes);

        // ---- the tangent: rubber over a key lever ---------------------------
        // No stiffness or damping measurement exists (open question 2); the
        // observables that own the calibration are the second-partial and
        // velocity-brightness rows. alpha 2.3 sits in rubber's Hertzian
        // range; lambda 1.0 for a lossy elastomer.
        const double reg = std::clamp ((note - 29.0) / 59.0, 0.0, 1.0);
        hammerCfg.alpha = 2.3 + 0.3 * (2.0 * uS2 - 1.0);   // per-key crook (above)
        hammerCfg.lambda = 1.0;
        hammerCfg.mass = (0.004 - 0.002 * reg)
                       * (0.6 + 0.8 * std::clamp (cfg.hammerMassNorm, 0.0, 1.0));
        // Stiff rubber over a metal anvil, calibrated so the mid-compass
        // contact lands near the ~1 ms excitation pulse EURASIP's Eq. 7
        // implies (fs d/v at the measured 1-4 m/s over a ~2.6 mm gap). The
        // first, softer guess (4e6) produced a 6 ms contact whose force-pulse
        // nulls buried the pickup comb — measured by the comb row, which owns
        // this constant together with the second-partial row. kScat is the
        // per-key crimp scatter.
        hammerCfg.stiffness = 2.0e8 * std::pow (30.0, reg)
                            * std::pow (12.0, std::clamp (cfg.hammerHardness, 0.0, 1.0) - 0.5)
                            * kScat
                            // A scattered exponent must be compared at a
                            // reference compression or it silently rescales
                            // the whole force curve: k delta^alpha is held
                            // invariant at delta = 3e-4 m — this model's own
                            // mf peak compression, (8 N / 1e9)^(1/2.3) — so
                            // the CURVE tilts under the crook while the
                            // mid-velocity contact stays comparable, and the
                            // stability cap below stops swinging x6 per 0.2
                            // of alpha (measured: without this, the cap
                            // clamped low-alpha keys to 0.24x and the
                            // +/-30%% crimp scatter was destroyed).
                            * std::pow (3.0e-4, 2.3 - hammerCfg.alpha);
        // The stamped bracket under the tip (see TangentContact): the 1.0e5
        // N/m cantilever estimate, scattered per key by the same crimp
        // logic. Its resonance with the tip mass sits near 1.5 kHz mid
        // compass — several periods inside a pp crossing, none inside ff.
        hammerCfg.bracketK = 1.0e5 * (1.0 + 0.30 * (2.0 * uS3 - 1.0));
        hammerCfg.bracketZeta = 0.10;
        hammerCfg.tipFrac = 0.35;
        {
            // The general explicit-contact stability cap, written for this
            // contact's exponent (the Wurlitzer derivation): past a fraction
            // of the sample rate the explicit contact stops being a contact
            // and becomes a generator. The colliding mass stays the FULL
            // moving mass: at the ~1 ms contact timescale the bracket
            // (resonant right there, 1.5 kHz) still couples the key's share,
            // and a tip-mass cap measured 0.24x clamps on mid keys — it
            // would flatten the crimp scatter the cap is not supposed to
            // own. N1's energy-monotone row across the compass at both
            // velocity extremes is the measured stability check.
            const double phi = std::abs (shapeStrike[0]) > 1.0e-9 ? std::abs (shapeStrike[0]) : 0.1;
            const double effM = modalMass / (phi * phi);
            const double mRed = 1.0 / (1.0 / hammerCfg.mass + 1.0 / effM);
            const double wMax = 2.0 * kPiD * 0.06 * fs;
            const double a = hammerCfg.alpha;
            const double kMax = std::pow (mRed * wMax * wMax / a, 0.5 * (a + 1.0))
                              * std::pow ((a + 1.0) * mRed * 4.0 * 0.5, -0.5 * (a - 1.0));
            if (hammerCfg.stiffness > kMax) hammerCfg.stiffness = kMax;
        }

        // ---- the yarn --------------------------------------------------------
        // "Short, at least with an instrument in mint condition" is the only
        // release figure [M]; the grip maps mint (fast, the drop a blip) down
        // to aged wool (slow, the drop exposed), which is what compressed
        // yarn does on real units [R]. Graduated heavier toward the bass,
        // where the string carries more energy.
        const double grip = std::clamp (cfg.damperGrip, 0.0, 1.0);
        const double yarnT60 = (0.40 - 0.34 * grip) * (1.0 + 0.8 * (1.0 - reg));
        damperFactor = std::exp (-3.0 * std::log (10.0) / (yarnT60 * fs));
        yarnEff = std::pow (effectiveDamper(), yarnRamp);

        // ---- the transducer --------------------------------------------------
        // String-pickup rest distance: the operating point on the flux curve.
        // DAFx-12 demonstrated exactly this knob; closer is louder and more
        // curved.
        gap0 = 1.5e-3 + 4.5e-3 * std::clamp (cfg.gapNorm, 0.0, 1.0);
        sel = std::clamp (static_cast<int> (cfg.pickupSel + 0.5), 0, 3);
        trans = std::clamp (static_cast<int> (cfg.transducer + 0.5), 0, 3);
        magRest = (field != nullptr)
                    ? field->flux (static_cast<float> (kMagOff), static_cast<float> (gap0)) : 0.0;
        fluxPrimed = false;

        // ---- the case [P, research 12.1/12.5] --------------------------------
        // Body material/size re-derive the ladder (state kept — a live sweep
        // re-pitches the ring); the drive port is the tailpiece rail, the
        // thump port the key's own spot along the keyboard, so each key
        // knocks the wood with its own coloration.
        kase.setBody (std::clamp (static_cast<int> (cfg.caseBodyMat), 0, kNumBodyMaterials - 1),
                      std::clamp (cfg.caseBodySize, 0.0, 1.0));
        caseGain = std::clamp (cfg.caseAmount, 0.0, 1.0) * kCaseSense;
        caseOn = caseGain > 0.0;
        ClavinetCase::pointShape (ClavinetCase::kDriveXi, ClavinetCase::kDriveEta, shapeCaseDrive);
        ClavinetCase::pointShape (0.10 + 0.72 * reg, kThumpEta, shapeCaseThump);

        configured = true;
    }

    static constexpr double kYarnRampS = 0.030;

    // ---- the practitioner layer's constants [P, research 12] ---------------
    // Levels are calibration constants owned by suite rows; times and shares
    // are physical estimates stated where they are derived.
    static constexpr double kCaseSense   = 4.0;     // case-to-pickup sense gain: 1.0
                                                    // would be the bare plate under
                                                    // the mount; the x4 is the mount
                                                    // bracket's lever above the
                                                    // plate's mean motion. The ONE
                                                    // calibrated case constant,
                                                    // owned by row C2 (the -25..-45
                                                    // dB window); the string-driven
                                                    // path rides the same constant
    static constexpr double kThumpForceN = 5.0;     // key-bottom impulse peak, N: a
                                                    // ~50 g key landing at ~0.4 m/s
                                                    // stopped in the 3 ms cushion is
                                                    // m v / t ~ 7 N; 5 N with the
                                                    // felt taking a share
    static constexpr double kThumpReturn = 0.35;    // the key-return knock's share
    static constexpr double kThumpS      = 0.003;   // raised-cosine impulse length
    static constexpr double kCaseRingS   = 0.7;     // keep-alive for the case ring
    static constexpr double kStickMaxS   = 0.004;   // notch catch at full wear, s
    static constexpr double kSlipS       = 0.0008;  // let-go pulse length, s
    static constexpr double kSlipFrac    = 1.2;     // notch catch depth at full
                                                    // wear, dimensionless against
                                                    // Z0 * v; owned by row W1
    static constexpr double kThumpEta    = 0.62;    // keybed line across the case

    double fs = 48000.0;
    int note = 60;
    System sys;
    TangentContact hammer;
    TangentContact::Config hammerCfg;
    ClavinetCase kase;
    const MagneticPickup* field = nullptr;

    int numPartials = 0, numModes = 0;
    double f0Speaking = 0.0;
    double strLen = 0.6, dCenter = 0.15, dBridge = 0.04;
    double speakFreq[kMaxModes] {};
    double alphaOfMode[kMaxModes] {};
    double shapeC[kMaxModes] {};
    double shapeB[kMaxModes] {};
    double shapeStrike[kMaxModes] {};
    double shapeContact[kMaxModes] {};
    double shapeTerm[kMaxModes] {};

    // Case state (see the constants block above for the levels).
    double shapeCaseDrive[ClavinetCase::kModes] {};
    double shapeCaseThump[ClavinetCase::kModes] {};
    bool   caseOn = true;
    double caseGain = 0.0;
    int    thumpRemaining = 0, thumpTotal = 0;
    double thumpAmp = 0.0;
    int    caseRingRemaining = 0;

    // Wear and scatter state.
    std::uint32_t noteHash = 0;
    double wearEff = 0.0;
    double modalMassM = 1.0e-4;
    double z0String = 0.1;
    int    stickRemaining = -1;
    int    slipRemaining = 0, slipTotal = 1;
    double slipAmp = 0.0;
    double roughAmt = 0.0, roughLp = 0.0;
    Rng    roughRng { 1u }, slipRng { 1u };

    double cHist[3] {}, bHist[3] {}, cfHist[3] {};
    double fluxPrev[2] {};
    bool fluxPrimed = false;

    double gap0 = 3.75e-3;
    int sel = 0, trans = 1;
    double magRest = 0.0;
    bool matFerro = true, matCond = true;
    double magCoupleC = 1.0;

    double beatDepth = 0.25;
    double lastTipV = 0.0;
    int seatSamples = 1 << 30;
    int lastContactSamples = 0;

    double damperFactor = 1.0;
    double yarnEff = 1.0, yarnRamp = 0.0;
    double pedalAmt = 0.0;
    double sinceStrike = 1.0e9;
    double peakEnergy = 1.0e-30;
    int controlCounter = 0;
    bool sounding = false, held = false, released = false, configured = false;
};

} // namespace epi
