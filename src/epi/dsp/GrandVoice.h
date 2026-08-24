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
#include "GrandBoard.h"
#include "ModalCore.h"

#include <type_traits>

namespace epi
{

// ---------------------------------------------------------------------------
// One acoustic-grand note: one to three stiff strings whose terminations all
// land on ONE finite-admittance bridge feeding the shared modal soundboard.
// Executes docs/grand-implementation-plan.md against the Salamander C5
// measurements; structurally the CP-70's sibling, and its exact opposite in
// the load-bearing way: the CP proved the rigid-termination limit (pure
// superposition, -42 dB beat nulls), the grand is defined by NOT being that.
// Two-stage decay, the polarisation split, sympathetic resonance and the
// unison knees are all one mechanism -- the shared moving termination -- and
// none of them is parameterised directly.
//
// THE TWO-PORT, AND WHY IT IS A STIFFNESS COUPLING.
//
// The plan (section 3.3) derives the coupling by substituting
// y = u_b(1 - x/L) + sum q_k sin(k pi x/L) into the string Lagrangian: the
// coupling lands entirely in the mass matrix (Gram, PSD, passive at any
// strength) and the discrete form exchanges termination force against bridge
// acceleration. The step-1 probe measured that discrete form at worst-case
// polyphony and REJECTED it: composed explicitly, the acceleration exchange
// lags one sample, and with the loop weight w_k * (w_k/omega_k^2) = T*mu the
// same for every partial, the 15 kHz partials -- where one sample is two
// radians of phase -- turn the reactive coupling into a generator (measured
// +3.5e8 relative energy in 10 s, lossless; band-limiting alone made it
// worse). The plan's named escalation is an implicit/SAV-shaped joint; the
// probe led one step further, to the same physics in coordinates that need
// no implicit solve at all:
//
// In ABSOLUTE modal coordinates p_k = q_k + (2/k pi) u_b -- using the exact
// sine expansion of the ramp, (1 - x/L) = sum (2/k pi) sin(k pi x/L) -- the
// kinetic energy is diagonal and the entire coupling moves to the potential
//
//   V = (T/2L) u_b^2 + sum_k (K_k/2) (p_k - c_k u_b)^2 ,
//       K_k = T (k pi)^2 / 2L,  c_k = 2/(k pi),  K_k c_k = T k pi / L = w_k.
//
// A non-negative quadratic form: passive by construction, same argument as
// the plan's Gram matrix, and read and written through the SAME w_k family
// (string mode k is forced by +w_k u_b; the board is forced by
// sum_k w_k p_k - Ku u_b through the same Phi it is read through). Because
// this is a pure stiffness coupling, exchanging the off-diagonal forces at
// time n composes the two leapfrogs into the monolithic leapfrog of the full
// linear system -- no delay error, symplectic structure kept. The probe
// measured the result at the full census: drift -2.9e-10/sample over 10 s
// lossless (exactly the imposed T60, i.e. none of it the scheme's), bounded
// excursion +1.7e-7.
//
// The exchange is still band-limited to the board's own band (< 1.3 kHz):
// above it the board has no modes by construction (section 4.1) and a string
// partial sees only a featureless mean resistance, which is folded into its
// T60 as the measured grand-minus-CP80 drain. The truncation drops whole
// squares from V, so it cannot break passivity, and it is symmetric -- the
// same partials are read and forced.
// ---------------------------------------------------------------------------

// The 24-anchor measured inharmonicity table (plan section 2), interpolated
// log-linearly. The classic grand V: minimum 7.4e-5 at the wound/plain break
// (F#2), rising toward both ends -- a real long bass, not the CP's x8 excess.
struct GrandInharmonicity
{
    static constexpr int kN = 23;
    static constexpr double kMidi[kN] = { 21, 24, 27, 33, 36, 39, 42, 45, 48, 51, 54,
                                          57, 63, 66, 69, 72, 75, 81, 84, 87, 93, 96, 99 };
    static constexpr double kB[kN] = { 2.40e-4, 1.79e-4, 1.50e-4, 9.32e-5, 9.51e-5,
                                       8.17e-5, 7.37e-5, 7.87e-5, 1.09e-4, 1.27e-4,
                                       1.70e-4, 2.16e-4, 3.68e-4, 5.06e-4, 6.26e-4,
                                       8.17e-4, 1.10e-3, 1.83e-3, 2.15e-3, 2.92e-3,
                                       5.29e-3, 8.14e-3, 9.27e-3 };

    static double at (double midi)
    {
        // Grand bass slope -0.055/semitone; treble the universal asymptote.
        if (midi <= kMidi[0])
            return kB[0] * std::exp (-0.055 * (midi - kMidi[0]));
        if (midi >= kMidi[kN - 1])
            return kB[kN - 1] * std::exp (0.0926 * (midi - kMidi[kN - 1]));
        for (int i = 0; i < kN - 1; ++i)
            if (midi <= kMidi[i + 1])
            {
                const double t = (midi - kMidi[i]) / (kMidi[i + 1] - kMidi[i]);
                return kB[i] * std::pow (kB[i + 1] / kB[i], t);
            }
        return kB[kN - 1];
    }
};

// The Salamander's own measured Railsback curve, as per-note cent offsets on
// equal temperament. Compass is the full A0..C8 -- the instrument really has
// 88 notes, no extrapolation.
inline double grandStretchCents (int midi)
{
    static constexpr double kM[] = { 21, 27, 48, 60, 72, 84, 96, 108 };
    static constexpr double kC[] = { -20.3, -12.1, -4.9, -0.9, 2.3, 13.5, 9.0, 2.0 };
    const double m = static_cast<double> (midi);
    if (m <= kM[0]) return kC[0];
    for (int i = 0; i < 7; ++i)
        if (m <= kM[i + 1])
            return kC[i] + (kC[i + 1] - kC[i]) * (m - kM[i]) / (kM[i + 1] - kM[i]);
    return kC[7];
}

// Intrinsic wire loss, dB/s: the CP-80's measured per-partial POLYNOMIAL --
// Yamaha wire of the same gauge family. The CP research's additional 6.5
// dB/s "fast floor" below a kilohertz is deliberately NOT carried over,
// and the grand's own measurements are the reason: its slow components sit
// on the raw polynomial (C4 slow 1.6 vs poly(261) = 2.8/r, C5 slow 4.8 vs
// poly(523) = 5.2, A1 fundamental 0.45 vs poly(55) = 0.9), while the floor
// would put every sub-kHz mode at >= 6.5 dB/s -- faster than the measured
// AFTERSOUND of half the compass. The floor was the CP's own bridge/frame
// drain riding on top of the wire loss; here that drain is exactly what the
// two-port provides emergently below 1.3 kHz, and keeping the floor would
// count it twice. Above 1.3 kHz the measured grand-minus-CP80 drain is
// added per mode (next function). Nothing is fit twice.
inline double grandAlphaIntr (double f)
{
    return std::max (0.5, 0.393 + 9.23e-3 * f - 1.275e-7 * f * f);
}

// Vertical:horizontal intrinsic loss ratio. Fit at both ends of the plan's
// own evidence: C2's single-string V/H decay ratio 4-17x (row P1, the
// CP-80's measured slow-polarisation range) and C4's measured aftersound of
// 1.6 dB/s = poly(261)/1.75. The taper is the calibration the plan's open
// question 5 schedules ("fit gH/r to the measured slow-component slopes").
inline double grandPolRatio (double midi)
{
    if (midi <= 36.0) return 4.5;
    if (midi >= 60.0) return 1.75;
    return 4.5 + (1.75 - 4.5) * (midi - 36.0) / 24.0;
}

// The soundboard's high-frequency drain: measured grand band medians minus
// the CP-80's, i.e. what the board takes from partials above its in-loop
// band. Large where the board radiates best (F&R's favoured 200-2000 Hz
// band), nearly nothing in the treble where the two instruments' measured
// rates coincide (41.9 vs 43.7 dB/s). Applied as band steps because band
// medians are what was measured; smoothing them would be invention.
inline double grandAlphaBoardHF (double f)
{
    if (f < GrandBoard::kBandHz) return 0.0;   // in-loop: the two-port owns it
    if (f < 1760.0) return 12.0;
    if (f < 3520.0) return 20.0;
    if (f < 7040.0) return 2.0;
    return 7.0;
}

class GrandVoice
{
public:
    // A0 carries ~161 vertical + ~40 horizontal at the 12 kHz cap; the
    // uncapped fs/pi census would need 238. The cap is the plan's named
    // mitigation, adopted by the CPU verdict: the step-1 probe measured the
    // uncapped worst case at 70% of a core bare and 104% coupled, which is
    // playable only with the cap and the sympathetic reduced set.
    static constexpr int kMaxModes = 220;
    static constexpr double kVerticalCapHz = 12000.0;
    using System = SavModalSystem<kMaxModes, 2>;

    struct Config
    {
        double hammerHardness = 0.5;
        double hammerMassNorm = 0.5;
        double escapementNorm = 0.4;
        double damperGrip     = 0.6;
        double detuneSpread   = 0.5;    // "tipMass" knob: unison spread 0..4 cents
        double dampTrim       = 0.5;    // "resDamp": intrinsic-alpha trim x0.7..1.5
        double detuneCents    = 0.0;    // master tune + bend
        double material       = 0.0;    // index into kMaterials; 0 = stock music wire
        // Damper felt condition, 0 stock: fresh grips faster, worn lazily,
        // hardened lets the high partials escape -- two bands split at 1.2 kHz.
        double damperFelt     = 0.0;
    // The hammer's covering, 0 stock: soft felt, hard felt, lacquered,
    // leather, wood -- relative to this instrument's own stock felt.
    double hammerMat      = 0.0;
        // CC67 in rail mode: the half-blow. The hammer rail tilts toward
        // the strings, shortening the stroke -- an upright's soft pedal.
        // Continuous 0..1; the shift/rail choice lives on the engine.
        double halfBlow       = 0.0;
        bool   unaCorda       = false;  // CC67: shifted action
    };

    // Compared byte-for-byte by the engine to decide whether the instrument
    // needs rebuilding. 8 doubles plus the bool's padded tail: the doubles
    // pack with no interior holes, and the engine zero-initialises whole
    // structs, so the tail bytes compare equal too.
    static_assert (std::is_trivially_copyable<Config>::value, "Config must be memcmp-able");
    static_assert (sizeof (Config) == 12 * sizeof (double), "Config has interior padding");

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& s : str) s.sys.prepare (sampleRate);
        hammer.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& s : str) { s.sys.clear(); s.sys.setNumModes (0); }
        hammer.reset();
        knockFeed = 0.0;
        sounding = held = false;
        sostenuto = sympathetic = unaCordaActive = false;
        pedalV = 0.0;
        controlCounter = 0;
        sinceStrike = 1.0e9;
        peakEnergy = 1.0e-30;
        configured = false;
    }

    void setNote (int midiNote, const Config& cfg, const GrandBoard& board)
    {
        note = midiNote;
        configure (cfg, board);
    }

    void noteOn (int midiNote, double velocity, const Config& cfg,
                 const GrandBoard& board, std::uint32_t seed)
    {
        if (! configured || midiNote != note || cfg.unaCorda != unaCordaActive)
            setNote (midiNote, cfg, board);
        (void) seed;
        sympathetic = false;
        sympDormant = false;
        grabEnv = 0.0;              // a fresh strike overrides a settling grab
        damperWasOff = true;        // key is down: damper off until release

        double vel = std::clamp (velocity, 0.0, 1.0);
        if (cfg.halfBlow > 0.0)
        {
            // The half-blow: the rail lifts the hammers toward the strings,
            // so the key accelerates them over a shorter arc. Under the
            // action's roughly constant torque the escapement velocity goes
            // as the square root of the remaining stroke; 0.45 is the rail's
            // full-pedal lift share (design constant, fenced by row 13.0).
            // And lifting the hammers off the jacks opens lost motion --
            // the "klapprig" feel: the jack takes up slack before it drives,
            // so quiet strikes land unevenly. Deterministic per-strike hash;
            // the looseness is largest where the player feels it, at pp.
            const double hb = std::min (cfg.halfBlow, 1.0);
            vel *= std::sqrt (1.0 - 0.45 * hb);
            rattleSeed = rattleSeed * 1664525u + 1013904223u
                       + static_cast<unsigned> (midiNote) * 2654435761u;
            const double j = (static_cast<double> (rattleSeed >> 8) / 8388608.0) - 1.0;
            vel *= 1.0 + 0.12 * hb * (1.0 - vel) * j;
            vel = std::clamp (vel, 0.0, 1.0);
        }
        halfBlowNow = std::min (cfg.halfBlow, 1.0);
        // Real hammers peak at 5-6 m/s; the curve is recalibrated against the
        // Salamander's 16 velocity layers in the calibration pass.
        const double v = 0.25 + 5.5 * std::pow (vel, 1.7);
        const double reg = std::clamp ((note - 21.0) / 87.0, 0.0, 1.0);
        const double escMm = (6.0 - 5.0 * reg) * (0.4 + 1.2 * cfg.escapementNorm);
        hammer.strike (v, escMm * 1.0e-3);
        for (auto& s : str) s.sys.setNumModes (s.kTotal);   // full set on strike
        sinceStrike = 0.0;
        held = true;
        sounding = true;
    }

    void noteOff() { held = false; }

    // CC64, continuous: 0 = dampers seated, 1 = fully lifted. The partial
    // range in between is the half-pedal map (applyDamperIfDue).
    // The workshop trims, the CP-70's pattern exactly: at constant tension
    // pitch follows 1/L (the microtonality lane), and B moves as d^2/L^2
    // (the bell-or-thud lane), with the hammer meeting the re-solved mass.
    void setGeometryTrim (double lenScale, double diaScale)
    {
        geoLen = std::clamp (lenScale, 0.5, 2.0);
        geoDia = std::clamp (diaScale, 0.4, 2.5);
    }

    void setPedal (double value) { pedalV = std::clamp (value, 0.0, 1.0); }

    // CC66: latched by the caller for voices held at the moment the pedal
    // went down; a latched voice keeps ringing as if held.
    void setSostenuto (bool latched) { sostenuto = latched; }

    // Sympathetic life: a string with its damper off receives energy through
    // the shared board even though no hammer has fallen -- and the in-loop
    // board stops at 1.3 kHz, so it can only receive its coupled prefix.
    // That is the plan's "sympathetic reduced set": setNumModes(kCoupled) is
    // not an approximation of the physics, it IS the physics of what the
    // coupling band can deliver. The full set returns when its own hammer
    // falls (noteOn always restores it).
    void openSympathetic (int midiNote, const Config& cfg, const GrandBoard& board)
    {
        if (! configured || midiNote != note) setNote (midiNote, cfg, board);
        // The FULL course rings sympathetically -- every string, both
        // coupled prefixes -- because the course's deliberate unison detune
        // is audible exactly here: strings a cent or two apart, driven by
        // the same bridge motion, beat against each other slowly, and that
        // shimmer is a real property of a pedal-down wash. (A one-string
        // representative was tried during the profiling campaign; it saved
        // little once the bridge exchange -- per VOICE, not per string --
        // was made to pipeline, and it silenced the intra-course beats.)
        // The reduced set is the coupled prefix per string: the board stops
        // at 1.3 kHz, so modes above it cannot receive anything through the
        // two-port -- setNumModes(kCoupled) is the physics, not a budget.
        for (int s = 0; s < numStrings; ++s)
            str[s].sys.setNumModes (str[s].kCoupled);
        sympathetic = true;
        sounding = true;
    }

    bool isSounding() const { return sounding; }
    bool isRinging() const { return sounding || hammer.isActive(); }
    int  noteNumber() const { return note; }
    int  hammerContactSamples() const { return hammer.contactDurationSamples(); }

    double modalEnergy() const
    {
        double e = 0.0;
        for (int s = 0; s < numStrings; ++s) e += str[s].sys.energy();
        return e;
    }

    // ---- energy telemetry, for the passivity row ------------------------
    // The subsystem energies alone miss the coupling's cross-potential, so a
    // per-sample energy check needs these: the coupled termination-force
    // read sum(w p), the stiffness load Ku, and the note's bridge shape. The
    // staggered cross energy is then -(F[n] u[n-1] + F[n-1] u[n])/2
    // + Ku u[n] u[n-1] / 2 per note, matching the integrator's own
    // q[n] K q[n-1] convention.
    const double* bridgeShape() const { return phi; }
    double couplingLoad() const { return Ku; }
    double coupledForceRead() const
    {
        double F = 0.0;
        for (int s = 0; s < numStrings; ++s)
        {
            const Str& S = str[s];
            const int nc = std::min (S.kCoupled, S.sys.numModes());
            for (int m = 0; m < nc; ++m)
                F += S.w[m] * S.sys.displacement (m);
        }
        return F;
    }

    // One sample: hammer, two-port exchange, tick. Returns the summed
    // full-band termination force; the knock share of the radiator feed is
    // read separately (knockOut) so the string-physics rows measure string
    // state and nothing else. The audible output of the coupled band is the
    // BOARD's plus the radiator's direct branch; the caller ticks the board
    // once per engine sample after every voice has exchanged.
    double process (const Config& cfg, GrandBoard& board)
    {
        (void) cfg;
        if (! sounding && ! hammer.isActive()) return 0.0;

        // The damper-grab chatter: a short feedforward force burst while
        // the felt settles, one-pole filtered to the contact bandwidth.
        // Runs a handful of control periods per release and only when a
        // live string was caught, so it costs nothing the rest of the time.
        if (grabEnv > 1.0e-4)
        {
            grabRng = grabRng * 1664525u + 1013904223u;
            const double w = (static_cast<double> (grabRng >> 8) / 8388608.0) - 1.0;
            grabLp += 0.35 * (w - grabLp);          // ~3 kHz felt bandwidth
            const double f = grabGain * grabEnv * grabLp;
            for (int s = 0; s < numStrings; ++s)
                for (int m = 0; m < str[s].sys.numModes(); ++m)
                    str[s].sys.addForce (m, f * str[s].strikeShape[m]);
            grabEnv *= kGrabDecay;
        }

        // The sympathetic fast lane: the full course at its coupled
        // prefixes, no hammer machinery. With the pedal down this path runs
        // for most of the eighty-eight voices every sample; the expensive
        // part is the per-voice bridge exchange, which is shared by the
        // whole course, so every string of the choir rings for the price of
        // its own prefix tick. Same two-port, same weights both ways.
        if (sympathetic && ! hammer.isActive())
        {
            const double uB = board.bridgeDisplacement (phi);
            double F = 0.0, out = 0.0;
            for (int s = 0; s < numStrings; ++s)
            {
                double Fs = 0.0;
                out += str[s].sys.tickCoupled (str[s].w, uB, str[s].readShape, Fs);
                F += Fs;
            }
            board.addBridgeForce (phi, F - Ku * uB);
            if (++controlCounter >= 32)
            {
                controlCounter = 0;
                sinceStrike += 32.0 / fs;
                controlTick();
            }
            return out;
        }

        // -- hammer: meets the mean of the struck strings' patch, and the
        // force splits equally -- the CP-70 bichord contact generalised to
        // three. The only direct string-to-string interaction there is.
        if (hammer.isActive())
        {
            // Una corda strikes a subset of the choir (numStruck); the
            // remaining string keeps a zero strike shape and is driven only
            // through the bridge, which is Weinreich's whole point: it
            // starts in antiphase, and its bridge force GROWS over the
            // first seconds while the struck pair's beat structure flattens.
            double u = 0.0, v = 0.0;
            for (int s = 0; s < numStruck; ++s)
            {
                u += str[s].sys.displacementAt (str[s].strikeShape);
                v += str[s].sys.velocityAt (str[s].strikeShape);
            }
            u /= numStruck; v /= numStruck;
            const double f = hammer.tick (u, v, hammerCfg);
            if (f != 0.0)
            {
                const double each = f / numStruck;
                for (int s = 0; s < numStruck; ++s)
                    for (int m = 0; m < str[s].sys.numModes(); ++m)
                        str[s].sys.addForce (m, each * str[s].strikeShape[m]);
                // The attack knock: a fraction of the hammer force reaches
                // the bridge through the action and the shanks, not only
                // through the strings. Bank's measured g ~ 0.2 feeds his
                // OUT-OF-LOOP radiator, and that is where the full share
                // goes here too (knockOut, consumed by the radiator's
                // direct and tail branches, in the same units as the
                // termination force -- the read shapes carry kOutScale, so
                // the knock share must too). Only a small share drives the
                // IN-LOOP board modes: the blow does shake the bridge -- the
                // 300-400 ms modal ring, velocity- and register-dependent
                // for free, felt by every open string through the two-port
                // -- but at full g the thump alone pumped a pedal-opened
                // neighbour to -16 dB of the struck note's own peak energy,
                // an order of magnitude past the sympathetic row's band.
                // External forces both: no bearing on passivity.
                board.addBridgeForce (phi, kKnockLoopGain * f);
                // Half-blow rattle: with lost motion in the action, more of
                // the blow's energy goes through the loose linkage into the
                // frame -- the knock share rises while the string tone
                // softens, which is exactly the "klapprig" sound.
                const double kn = 1.0 + 0.8 * halfBlowNow;
                knockFeed = kn * kKnockGain * kOutScale * f;
            }
        }

        // -- the two-port, before either side ticks: string mode k is forced
        // by +w_k u_b; the board receives sum w_k p_k - Ku u_b through the
        // same Phi it was read through. Same weights both ways -- the
        // sense-and-force rule is project law.
        const double uB = board.bridgeDisplacement (phi);
        double F = 0.0;
        for (int s = 0; s < numStrings; ++s)
        {
            Str& S = str[s];
            const int nc = std::min (S.kCoupled, S.sys.numModes());
            for (int m = 0; m < nc; ++m)
            {
                F += S.w[m] * S.sys.displacement (m);
                S.sys.addForce (m, S.w[m] * uB);
            }
        }
        board.addBridgeForce (phi, F - Ku * uB);

        double force = 0.0;
        for (int s = 0; s < numStrings; ++s)
        {
            str[s].sys.tick();
            force += str[s].sys.displacementAt (str[s].readShape);
        }

        if (++controlCounter >= 32)
        {
            controlCounter = 0;
            sinceStrike += 32.0 / fs;
            controlTick();
        }

        return force;
    }

    // The knock share of this sample's radiator feed. Read-and-clear, once
    // per sample, by whoever owns the radiator.
    double knockOut()
    {
        const double k = knockFeed;
        knockFeed = 0.0;
        return k;
    }

    void applyDamperIfDue()
    {
        // The damper line: G#6 is the last damped note; from A6 (93) up the
        // strings ring free, as on the CP-70 and most grands (the plan's row
        // G1 pins G6 damped, A6 free; open question 8 owns the exact note).
        // A note above the line ends by energy retirement.
        if (note > 92) return;
        if (held || sostenuto || pedalV >= kPedalFree)
        {
            damperWasOff = true;
            return;
        }
        if (damperWasOff)
        {
            damperWasOff = false;
            const double e = modalEnergy();
            if (e > 1.0e-10)
            {
                // Chatter force from the contact-moment energy: amplitude
                // goes as sqrt(e) (impact momentum tracks string velocity),
                // the constant calibrated so an mf single-note release sits
                // near -38 dB against its own attack peak (suite row).
                grabGain = kGrabForce * std::sqrt (e);
                grabEnv = 1.0;
                grabRng = 0x9e3779b9u * static_cast<unsigned> (note + 17);
            }
        }

        // Half-pedal: CC64 is continuous. The damper's grip interpolates
        // from seated (the register-dependent damperT60 behind damperFactor)
        // to free as the pedal rises, with the knee mapped so the 30..70%
        // span is the partial-damping playing range -- one mapping function,
        // measured by row G2 to sit strictly between seated and free and to
        // be monotone in the pedal value.
        double factor = damperFactor;
        if (pedalV > kPedalSeated)
        {
            // The damping RATE scales with the felt's residual grip, and the
            // grip collapses much faster than the lift: contact force and
            // contact area both fall toward liftoff, so the rate goes as
            // (1 - lift)^3. Measured on row G2: a half pedal then turns C4's
            // ~220 dB/s seated damping into ~27 dB/s -- audibly and
            // measurably between seated and free, where the quadratic form
            // still killed the note within the row's window and the linear
            // one was indistinguishable from seated.
            const double lift = (pedalV - kPedalSeated) / (kPedalFree - kPedalSeated);
            factor = std::pow (damperFactor, (1.0 - lift) * (1.0 - lift) * (1.0 - lift));
        }
        // The felt's two grip bands: hardened felt fails to seat on the
        // fine ripple and the high partials escape it, the zing of an old
        // damper. Stock is exactly uniform, and costs nothing extra.
        const double factorLo = (feltWLo == 1.0) ? factor : std::pow (factor, feltWLo);
        const double factorHi = (feltWHi == feltWLo) ? factorLo : std::pow (factor, feltWHi);
        for (int s = 0; s < numStrings; ++s)
            for (int m = 0; m < str[s].sys.numModes(); ++m)
                if (str[s].sys.displacement (m) != 0.0)
                    str[s].sys.scaleMode (m, str[s].sys.frequency (m) > 1200.0 ? factorHi : factorLo);
    }

    // ---- per-string telemetry, for the una corda row ---------------------
    // The coupled termination-force read of one string: |sum w q| is what
    // the third, un-struck string pushes into the bridge, and UC1 checks
    // that it rises over the first seconds.
    double stringForceRead (int s) const
    {
        if (s < 0 || s >= numStrings) return 0.0;
        const Str& S = str[s];
        const int nc = std::min (S.kCoupled, S.sys.numModes());
        double F = 0.0;
        for (int m = 0; m < nc; ++m) F += S.w[m] * S.sys.displacement (m);
        return F;
    }

private:
    // Mode layout, chosen so both the coupling and the shrink are contiguous:
    //   [ V partials 1..kCplV   ]  <- coupled to the board
    //   [ H partials 1..kCplH   ]  <- coupled, through gH
    //   [ forced H (treble)     ]  <- at most one, uncoupled
    //   [ V partials kCplV+1..kV]  <- high partials: shrink eats from the top,
    //                                 which is exactly what dies first.
    struct Str
    {
        System sys;
        int kV = 0, kH = 0;        // vertical / horizontal counts
        int kCplV = 0, kCoupled = 0;
        int kTotal = 0;
        double w[kMaxModes] {};          // two-port weights, coupled prefix only
        double strikeShape[kMaxModes] {};
        double readShape[kMaxModes] {};
    };

    void controlTick()
    {
        // Deterministic top-down shrink, exactly the CP-70's: the layout puts
        // the fast-dying high verticals at the top, so the shrink actually
        // fires here -- the grand decays 2-6x faster than the CP and nothing
        // outlives ~25 s.
        for (int s = 0; s < numStrings; ++s)
        {
            const int floorK = str[s].kCplV + str[s].kH + 2;
            int k = str[s].sys.numModes();
            while (k > floorK && sinceStrike > 90.0 / alphaOfMode[s][k - 1])
                --k;
            if (k != str[s].sys.numModes()) str[s].sys.setNumModes (k);
        }

        const double e = modalEnergy();
        if (! std::isfinite (e)) { for (auto& s : str) s.sys.clear(); sounding = false; return; }
        if (e > peakEnergy) peakEnergy = e;

        // The sympathetic dormant tier, the tine bank's proven pattern:
        // with the pedal down all eighty-eight strings are open, but most
        // hold energies forty-plus decibels under the wash leaders -- they
        // were measured as the single largest line in the grand's profile
        // (58% of a core against the struck notes' 14%). A voice this quiet
        // shrinks to ONE coupled mode per string, which drops it into the
        // modal core's single-mode fast path; only that mode listens to the
        // board, so the voice can still be pumped awake, and the hysteresis
        // (a factor of a thousand in energy) keeps it from flapping. The
        // truncated modes hold energy below the dormancy threshold by
        // definition, so nothing audible is discarded.
        if (sympathetic && ! hammer.isActive())
        {
            // Thresholds referenced to audibility: struck-note energies run
            // 1e-3 to 1e-4 and the audible wash leaders 1e-7 to 1e-9, so a
            // voice under 1e-13 sits ninety-plus decibels below the notes --
            // the resonant partners that ARE the wash stay fully live.
            if (! sympDormant && e < 1.0e-13)
            {
                sympDormant = true;
                str[0].sys.setNumModes (1);
                for (int s = 1; s < numStrings; ++s) str[s].sys.setNumModes (0);
            }
            else if (sympDormant && e > 1.0e-11)
            {
                sympDormant = false;
                for (int s = 0; s < numStrings; ++s)
                    str[s].sys.setNumModes (str[s].kCoupled);
            }
        }
        // A sympathetically opened voice starts from zero energy on purpose;
        // retiring it against its own (empty) peak would close it 32 samples
        // after it opened. It stays live while its damper is off and retires
        // normally once the damper has taken the energy back out.
        const bool openSym = sympathetic && (held || sostenuto || pedalV > 0.05);
        if (! hammer.isActive() && ! openSym && e < peakEnergy * 1.0e-10)
            sounding = false;
    }

    void configure (const Config& cfg, const GrandBoard& board)
    {
        const double f0Nom = 440.0 * std::pow (2.0, (note - 69.0) / 12.0)
                           / geoLen;   // a string's pitch follows 1/L
        const double stretch = grandStretchCents (note);
        const double f0 = f0Nom * std::pow (2.0, (stretch + cfg.detuneCents) / 1200.0);

        // The string workshop's material swap, the CP-70's pattern exactly:
        // at fixed pitch, gauge and length the tension re-solves, and what
        // survives of the material is
        //   - B proportional to E/rho (bronze halves the steel curve, nylon
        //     nearly flattens it),
        //   - mu proportional to rho -- carried through the re-solved tension
        //     T = 4 f0^2 L^2 mu, so the two-port weights, the read shapes and
        //     the truncated stiffness load Ku all scale with it. That moves
        //     the string-to-board impedance ratio, and with it the decay
        //     through the bridge: physical and wanted,
        //   - internal loss on the BENDING share of each mode only (below).
        // Index 0 is the measured music wire bit-exactly: every ratio is one
        // and the added loss is zero.
        const Material& mat = kMaterials[std::clamp (static_cast<int> (cfg.material), 0, kNumMaterials - 1)];
        const double matB = (static_cast<double> (mat.youngs) / static_cast<double> (mat.density))
                          / (static_cast<double> (kMusicWire.youngs) / static_cast<double> (kMusicWire.density));
        const double matDEta = std::max (0.0, static_cast<double> (mat.lossEta)
                                            - static_cast<double> (kMusicWire.lossEta));
        const double B = GrandInharmonicity::at (note) * matB * (geoDia * geoDia) / (geoLen * geoLen);

        // ---- geometry: the Broadwood scale anchors --------------------------
        const double L = speakingLength (note) * geoLen;
        // A fatter wire at the same pitch and length takes proportionally
        // more tension (T = 4 f0^2 L^2 mu with mu ~ d^2), so the gauge trim
        // enters here, not only through B: the re-solved mass moves the
        // string's impedance into the bridge, and with it the level and the
        // decay through the board -- the workshop's bell-or-thud lane in
        // full, exactly as on the E-Grand.
        const double T = 700.0                   // mid of the 600-900 N band
                       * (static_cast<double> (mat.density) / static_cast<double> (kMusicWire.density))
                       * geoDia * geoDia;
        const double mu = T / (4.0 * f0 * f0 * L * L);
        const double modalMass = 0.5 * mu * L;   // pinned-pinned, every mode

        numStrings = note <= 39 ? 1 : note <= 51 ? 2 : 3;
        // Una corda: the action shifts so the hammer meets 2 of 3 strings
        // (trichord) or 1 of 2 (bichord); a monochord instead meets fresh,
        // softer felt (K x 0.7, open question 9's default).
        unaCordaActive = cfg.unaCorda;
        numStruck = ! unaCordaActive ? numStrings
                  : numStrings == 3 ? 2 : 1;

        // Unison spread: per-note deterministic scatter 0.5-2 c at the
        // default knob (measured splits 0.9-1.9 c; Kirk's preferred 1-2 c
        // maximum). The pattern is one-sided and clustered -- {0, +0.75,
        // +1.0} x unit -- because that is what the component fits resolve
        // (A3: +0 / +1.38 / +1.85 c; C4 pair: +1.24 c), and because three
        // near-commensurate pairwise beats from a symmetric {-, 0, +} layout
        // produce collective nulls the measured envelopes do not show.
        const double unit = 4.0 * cfg.detuneSpread * cfg.detuneSpread
                          * (0.5 + 1.5 * (((note * 2654435761u) & 255u) / 255.0));
        const double offs[3] = { 0.0, 0.75 * unit, 1.0 * unit };

        const double fMax = std::min (System::kModeBudget * fs, kVerticalCapHz);
        // x0.7..x1.5 log-mapped so the default knob position is x1.0: the
        // measured decay table IS the default voicing.
        const double trim = 0.7 * std::pow (1.5 / 0.7, std::clamp (cfg.dampTrim, 0.0, 1.0));

        board.fillBridgeShape (note, phi);
        Ku = 0.0;

        // Material loss on a STRING acts only on the bending share of the
        // energy -- the tension's restoring force is geometric and lossless,
        // which is why a nylon guitar string rings for seconds while a nylon
        // rod clunks (see CP70Voice.h). The bending fraction of mode k is
        // the inharmonicity term B k^2 / (1 + B k^2): nearly nothing for low
        // partials, growing with k -- so nylon keeps its fundamental and
        // sheds its highs. Rates add; exactly zero for the stock wire.
        auto bendLoss = [&] (int k, double fk)
        {
            return 8.686 * kPiD * fk * ((B * k * k) / (1.0 + B * k * k)) * matDEta;
        };

        // Hammer voicing is never perfectly even: crown wear and string
        // height differences make the felt launch the members of a choir at
        // measurably different levels, and that inequality is what keeps the
        // unison's beat minima from collapsing to nulls (equal launches
        // excite the members identically, and identical amplitudes cancel
        // completely at every crossover -- which the measured envelopes rule
        // out). About +/-1 dB, deterministic per note; calibrated jointly
        // with the polarisation offset against the null rows.
        static constexpr double kVoicing[3] = { 1.0, 0.90, 1.11 };
        const unsigned perm = (note * 2654435761u >> 4) % 3u;

        for (int s = 0; s < numStrings; ++s)
        {
            const double voicing = numStrings == 1 ? 1.0 : kVoicing[(s + perm) % 3u];
            Str& S = str[s];
            double fs0 = f0 * std::pow (2.0, offs[s] / 1200.0);
            auto fV = [&] (int k) { return k * fs0 * std::sqrt (1.0 + B * k * k); };
            auto fH = [&] (int k) { return k * fs0 * std::pow (2.0, kHOffsetCents / 1200.0)
                                         * std::sqrt (1.0 + B * k * k); };

            int kV = 0;
            while (kV + 1 <= kMaxModes && fV (kV + 1) < fMax) ++kV;
            int kCplV = 0;
            while (kCplV + 1 <= kV && fV (kCplV + 1) < GrandBoard::kBandHz) ++kCplV;
            int kCplH = 0;
            while (kCplH + 1 <= kV && fH (kCplH + 1) < GrandBoard::kBandHz) ++kCplH;
            int kH = std::max (1, kCplH);
            if (kV + kH > kMaxModes) kH = kMaxModes - kV;

            // The tuning pass. A coupled fundamental is pulled by the bridge
            // reactance -- the audible in-phase unison mode couples with
            // sqrt(N) w1, so the effective stiffness shift is
            // -N w1^2 Re[Heff(f1)] with Heff the board's loaded receptance
            // at this note's point -- and a real piano is tuned STRUNG, so
            // the tuner absorbs exactly this shift. Without the pass the
            // Railsback rows would read the pull twice: the measured stretch
            // table already contains the real instrument's own pull. The
            // board fit above bounds the pull to a couple of cents, so one
            // perturbative pass is exact enough; all N strings get the same
            // correction and the deliberate detunes survive.
            if (kCplV > 0)
            {
                const double f1 = fs0 * std::sqrt (1.0 + B);
                const double a = (2.0 * kPiD * f1) * (2.0 * kPiD * f1);
                const double w1 = T * kPiD / L;
                const double weff2 = numStrings * w1 * w1;
                const double kuN = numStrings * (T / L)
                                 * ((1.0 + 2.0 * kCplV) + kGH * kGH * (1.0 + 2.0 * kCplH));
                double hr = 0.0, hi = 0.0;
                board.receptance (phi, f1, hr, hi);
                const double dr = 1.0 + kuN * hr, di = kuN * hi;
                const double heffRe = (hr * dr + hi * di) / (dr * dr + di * di);
                double df = -f1 * weff2 * heffRe / (2.0 * modalMass * a);
                df = std::clamp (df, -0.0035 * f1, 0.0035 * f1);
                fs0 *= f1 / (f1 + df);
            }

            S.kV = kV; S.kH = kH;
            S.kCplV = kCplV;
            S.kCoupled = kCplV + kCplH;
            S.kTotal = kV + kH;
            S.sys.setNumModes (S.kTotal);

            const double beta = strikeBeta();
            const double patch = std::min (0.45, (0.005 - 0.002 * regOf (note)) / L);

            auto place = [&] (int idx, int k, double fk, double alpha,
                              bool horizontal, double cplGain)
            {
                S.sys.setMode (idx, fk, 60.0 / alpha, modalMass);
                alphaOfMode[s][idx] = alpha;
                S.w[idx] = idx < S.kCoupled ? cplGain * T * k * kPiD / L : 0.0;

                // Output: the full-band termination force, tapered by the
                // bridge foot's finite contact length as on the CP-70 -- the
                // partials whose half-wavelength approaches the footprint
                // self-cancel across it.
                const double footArg = k * kPiD * (kBridgeFoot / 2.0) / L;
                const double foot = std::abs (footArg) < 1e-9 ? 1.0
                                  : std::sin (footArg) / footArg;
                const double readGain = horizontal ? kGHRead : 1.0;
                S.readShape[idx] = readGain * T * k * kPiD / L * foot * kOutScale;

                const double z = k * kPiD * patch;
                const double wp = std::abs (z) < 1e-9 ? 1.0 : std::sin (z) / z;
                const double skew = horizontal ? kHammerSkew : 1.0;
                const double struck = s < numStruck ? 1.0 : 0.0;
                S.strikeShape[idx] = struck * voicing * skew * std::sin (k * kPiD * beta) * wp;
            };

            // Coupled verticals, then coupled horizontals, then the forced
            // uncoupled H (top octave only), then the high verticals.
            for (int k = 1; k <= kCplV; ++k)
                place (k - 1, k, fV (k),
                       trim * (grandAlphaIntr (fV (k)) + grandAlphaBoardHF (fV (k)))
                           + bendLoss (k, fV (k)),
                       false, 1.0);
            for (int k = 1; k <= std::min (kCplH, kH); ++k)
                place (kCplV + k - 1, k, fH (k),
                       trim * grandAlphaIntr (fH (k)) / grandPolRatio (note)
                           + bendLoss (k, fH (k)), true, kGH);
            for (int k = kCplH + 1; k <= kH; ++k)
                place (kCplV + k - 1, k, fH (k),
                       trim * (grandAlphaIntr (fH (k)) + grandAlphaBoardHF (fH (k))) / grandPolRatio (note)
                           + bendLoss (k, fH (k)),
                       true, 0.0);
            for (int k = kCplV + 1; k <= kV; ++k)
                place (kH + k - 1, k, fV (k),
                       trim * (grandAlphaIntr (fV (k)) + grandAlphaBoardHF (fV (k)))
                           + bendLoss (k, fV (k)),
                       false, 0.0);
            for (int i = S.kTotal; i < kMaxModes; ++i)
            { S.w[i] = 0.0; S.readShape[i] = 0.0; S.strikeShape[i] = 0.0; }

            // The truncated termination-stiffness load this string puts on
            // the board: (T/L)(1 + 2K) per polarisation, gH^2-scaled for H.
            Ku += (T / L) * ((1.0 + 2.0 * kCplV) + kGH * kGH * (1.0 + 2.0 * kCplH));
        }

        // ---- hammer: felt, Hall/Askenfelt anchors ---------------------------
        // K log-interpolated C2 4e8 -> C4 4.5e9 -> C7 1e10-effective (below),
        // alpha 2.3 -> 2.5 -> 3.0, mass 12 g (C1) -> 5 g (C8). Contact
        // targets ~4 ms at C2, ~2 ms at C4 (T/2, max efficiency), ~1 ms at
        // C7 -- and the CONTACT TIMES are the calibration authority, because
        // the published (K, alpha) pairs are not consistent with them inside
        // any compression-law contact model: K = 1e12 with alpha = 3 puts a
        // 6 g hammer's contact resonance at 17 krad/s and the ff contact at
        // 0.2 ms, five times shorter than the same authors' measured 1 ms
        // (that 1 ms is the string riding with the hammer through several
        // reflections, a compliance the quoted K never sees). Measured on
        // this model, the treble anchor that reproduces the measured contact
        // graduation is 1e10. Below C2 the graduation continues at the
        // C2->C4 log slope rather than clamping: a clamped K left A0's
        // contact SHORTER than C2's (3.4 vs 4 ms, backwards), and with it a
        // bass spectrum bright enough to put A0's strongest radiated partial
        // near 500 Hz, which no ff bass recording shows.
        {
            const double m = static_cast<double> (note);
            auto lerp3 = [&] (double a36, double a60, double a96, bool logI)
            {
                double t, lo, hi;
                if (m <= 60.0) { t = (m - 36.0) / 24.0; lo = a36; hi = a60; }
                else { t = std::min (1.0, (m - 60.0) / 36.0); lo = a60; hi = a96; }
                return logI ? lo * std::pow (hi / lo, t) : lo + (hi - lo) * t;
            };
            hammerCfg.alpha     = lerp3 (2.3, 2.5, 3.0, false);
            const HammerCover hcov = hammerCover (std::clamp (static_cast<int> (cfg.hammerMat + 0.5), 0, 5));
            hammerCfg.alpha     = std::clamp (hammerCfg.alpha + hcov.alphaAdd, 1.2, 3.5);
            hammerCfg.stiffness = hcov.stiff * lerp3 (4.0e8, 4.5e9, 1.0e10, true)
                                * std::pow (12.0, cfg.hammerHardness - 0.5)
                                * (unaCordaActive && numStrings == 1 ? 0.7 : 1.0);
            hammerCfg.lambda    = 1.0 * hcov.lambda;   // felt hysteresis stand-in
            hammerCfg.mass      = 0.012 * std::pow (5.0 / 12.0, (m - 24.0) / 84.0)
                                * (0.6 + 0.8 * cfg.hammerMassNorm);
        }
        {
            // Explicit-contact stability cap, derived for the felt's actual
            // exponent instead of reusing the CP-70's alpha ~ 2.2 form -- on
            // a grand that form over-caps the top octaves by two orders of
            // magnitude and the whole treble spectrum dies of the stretched
            // contact. The criterion is the same: the contact resonance at
            // worst-case compression stays below wMax = 2 pi 0.06 fs. With
            // F = K x^alpha and the peak compression of a v_max strike,
            //   x*^(alpha+1) = (alpha+1) mRed v^2 / (2K),
            //   wc^2 = alpha K x*^(alpha-1) / mRed,
            // solving wc = wMax for K gives the cap below. It still binds at
            // the very top with K = 1e12; the quadratised contact remains
            // the named fix if the capped treble reads soft.
            const double phi1 = std::sin (kPiD * strikeBeta());
            const double effM = modalMass / std::max (1.0e-6, phi1 * phi1);
            const double mRed = 1.0 / (1.0 / hammerCfg.mass + 1.0 / effM);
            const double wMax = 2.0 * kPiD * 0.06 * fs;
            const double vMax = 6.0;
            const double al = hammerCfg.alpha;
            const double kMax = std::pow (wMax * wMax * mRed / al, 0.5 * (al + 1.0))
                              * std::pow (0.5 * (al + 1.0) * mRed * vMax * vMax,
                                          -0.5 * (al - 1.0));
            if (hammerCfg.stiffness > kMax) hammerCfg.stiffness = kMax;
        }

        const double grip = std::clamp (cfg.damperGrip, 0.0, 1.0);
        const double damperT60 = (0.30 - 0.24 * grip) * (1.0 + 1.4 * (1.0 - regOf (note)));
        damperFactor = std::exp (-3.0 * std::log (10.0) / (damperT60 * fs));
        {
            const int fFelt = std::clamp (static_cast<int> (cfg.damperFelt + 0.5), 0, 3);
            feltWLo = (fFelt == 1) ? 1.25 : (fFelt == 2) ? 0.55 : 1.0;
            feltWHi = (fFelt == 1) ? 1.25 : (fFelt == 2) ? 0.55 : (fFelt == 3) ? 0.12 : 1.0;
        }

        configured = true;
    }

    static double regOf (int n) { return std::clamp ((n - 21.0) / 87.0, 0.0, 1.0); }

    static double speakingLength (double midi)
    {
        // Broadwood anchors (piano_table.jpg), log-interpolated; the bass
        // slope continues below C1. With T = 700 N the derived plain-wire
        // diameter comes out a smooth ~0.95-1.0 mm mid-compass -- the
        // built-in consistency check the plan demands.
        static constexpr double kM[] = { 24, 60, 72, 108 };
        static constexpr double kL[] = { 1.013, 0.639, 0.324, 0.051 };
        if (midi <= kM[0])
            return kL[0] * std::pow (kL[0] / kL[1], (kM[0] - midi) / (kM[1] - kM[0]));
        for (int i = 0; i < 3; ++i)
            if (midi <= kM[i + 1])
            {
                const double t = (midi - kM[i]) / (kM[i + 1] - kM[i]);
                return kL[i] * std::pow (kL[i + 1] / kL[i], t);
            }
        return kL[3];
    }

    double strikeBeta() const
    {
        // A grand's strike point FALLS with register -- 0.135 (C1) to 0.08
        // (C8), the Broadwood/Stulov graduation -- the opposite of the CP-70.
        static constexpr double kM[] = { 24, 60, 84, 108 };
        static constexpr double kBt[] = { 0.135, 0.125, 0.09, 0.08 };
        const double m = static_cast<double> (note);
        if (m <= kM[0]) return kBt[0];
        for (int i = 0; i < 3; ++i)
            if (m <= kM[i + 1])
                return kBt[i] + (kBt[i + 1] - kBt[i]) * (m - kM[i]) / (kM[i + 1] - kM[i]);
        return kBt[3];
    }

    // The vertical:horizontal admittance asymmetry. gH scales how strongly
    // the slow polarisation talks to the bridge (Weinreich: the bridge "gives
    // much more easily" vertically; JOS: |Y_v| >> |Y_h|); with the write and
    // the read both carrying gH, the H loop gain is gH^2 ~ 0.005 of V's, so
    // H keeps nearly its intrinsic slow decay -- the measured aftersound.
    static constexpr double kGH = 0.07;
    // What the listener hears of H relative to V through the board's
    // different directivity: out of the loop, so a transducer number, not a
    // coupling; set so the slow component starts near the measured -18 dB
    // (launch -6 dB from hammer skew, read -8 dB).
    static constexpr double kGHRead = 0.45;
    // Launch level of the slow polarisation relative to the fast: skew and
    // read gain together put the aftersound's start near the measured
    // -18/-14.5 dB, and the broadband knees (W4) hang on the same number --
    // the knee is where the prompt track crosses the aftersound band.
    static constexpr double kHammerSkew = 0.35;
    // The slow polarisation's offset from the fast, inside the measured
    // split band (the component fits resolve members 1.2-1.9 c apart across
    // the compass). The exact value is calibrated jointly with the voicing
    // spread below against the two null rows: at 1.8 c the prompt/aftersound
    // crossover interference fills C4's beat minimum to the measured
    // -10..-28 dB while A3's stays above -3 dB -- the open-question-5
    // calibration the plan schedules, done against its own rows.
    static constexpr double kHOffsetCents = 1.8;
    static constexpr double kBridgeFoot = 0.030;   // m, bridge contact length
    // Half-pedal knee: below 30% the dampers are seated, above 70% free;
    // the 30..70% span is the partial-damping playing range [D].
    static constexpr double kPedalSeated = 0.3;
    static constexpr double kPedalFree   = 0.7;
    // Grab-chatter force scale and its ~50 ms settle (per-sample at 48 k;
    // recomputed for fs in prepare if the base rate differs).
    static constexpr double kGrabForce = 1.35;
    static constexpr double kGrabDecay = 0.99958;
    static constexpr double kKnockGain = 0.2;      // hammer-force share, radiated
    static constexpr double kKnockLoopGain = 0.02; // hammer-force share, in-loop
    static constexpr double kOutScale = 2.0e-3;    // folds the radiator feed level

    double fs = 48000.0;
    int note = 60;
    int numStrings = 1;
    int numStruck = 1;
    Str str[3];
    HuntCrossleyHammer hammer;
    HuntCrossleyHammer::Config hammerCfg;
    double alphaOfMode[3][kMaxModes] {};
    double phi[GrandBoard::kModes] {};
    double Ku = 0.0;
    double damperFactor = 1.0;
    double feltWLo = 1.0, feltWHi = 1.0;
    double geoLen = 1.0, geoDia = 1.0;
    double knockFeed = 0.0;
    double sinceStrike = 1.0e9;
    double peakEnergy = 1.0e-30;
    int controlCounter = 0;
    double pedalV = 0.0;
    bool sounding = false, held = false, configured = false;
    bool sympDormant = false;

    // The damper grab. When the felt re-seats on a string that is still
    // ringing, the string chatters against it for the first few periods --
    // the release "shh" of a close-miked grand, and, summed over the whole
    // bank on a pedal lift, the classic damper-return wash. Modeled as a
    // feedforward force burst at the contact: deterministic noise, felt
    // bandwidth, amplitude from the string's energy at the moment of
    // contact, injected through the strike shape (both are near-end
    // contacts) so it radiates with the string's own coloration. Forces in,
    // never state feedback -- passivity untouched.
    bool   damperWasOff = true;
    double grabEnv = 0.0, grabGain = 0.0, grabLp = 0.0;
    unsigned grabRng = 1u;
    bool sostenuto = false, sympathetic = false, unaCordaActive = false;
    double halfBlowNow = 0.0;
    unsigned rattleSeed = 0x1234567u;
};

} // namespace epi
