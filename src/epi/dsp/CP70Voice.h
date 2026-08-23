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
#include "OutputChain.h"

namespace epi
{

// ---------------------------------------------------------------------------
// One CP-70 note: one or two stiff strings on a rigid piezo bridge.
//
// Everything here executes docs/cp70-implementation-plan.md against the
// measurements in docs/research/cp70-measured.md, and the shape of the thing
// is the opposite of the Rhodes in every load-bearing way:
//
//   - MANY modes (a bass string carries every partial under fs/pi -- over a
//     hundred), where the tine carries eight.
//   - NO nonlinearity anywhere in the voice. The measured beat nulls reach
//     -42 dB, which only pure superposition produces, so the voice output is
//     a linear functional of the modal state: no field, no saturation, no
//     oversampling, no SAV terms. The pickup law and the preamp make the
//     timbre.
//   - NO coupling between the two strings of a bichord, and none to any
//     frame. The research proved the rigid-bridge limit directly: the C4
//     fundamental resolves into two independent pairs with no symmetric/
//     antisymmetric splitting. Sympathetic resonance on this instrument
//     would be an invented feature contradicted by the data. The one moment
//     the unison strings interact is the millisecond they share the hammer.
//
// The pickup is the point. A piezo under the bridge reads the transverse
// FORCE at the termination, F = T * dy/dx at x=0, so mode k's readout weight
// is exactly T*(k*pi/L): a +6 dB per octave tilt applied as an explicit
// modal weighting, not an EQ. It is why the CP is bright and fundamental-poor
// before any tone stack -- at D#1 the measured fundamental sits 20.6 dB below
// the fourth partial.
// ---------------------------------------------------------------------------

// The measured inharmonicity anchors, log-interpolated. The model consumes
// the DATA; the two-segment law in the research is only a summary of it and
// overshoots the measured treble points by about 30%.
struct CP70Inharmonicity
{
    static constexpr int kN = 22;
    static constexpr double kMidi[kN] = { 27, 34, 35, 42, 46, 50, 53, 57, 60, 63,
                                          65, 68, 72, 76, 80, 82, 85, 88, 91, 95,
                                          97, 102 };
    static constexpr double kB[kN] = { 1.22e-3, 5.78e-4, 5.18e-4, 2.18e-4, 2.50e-4,
                                       2.55e-4, 2.48e-4, 3.31e-4, 3.44e-4, 4.08e-4,
                                       4.67e-4, 6.39e-4, 7.11e-4, 1.17e-3, 1.84e-3,
                                       2.04e-3, 2.77e-3, 3.24e-3, 4.37e-3, 5.88e-3,
                                       7.42e-3, 9.59e-3 };

    static double at (double midi)
    {
        if (midi <= kMidi[0])
            return kB[0] * std::exp (-0.09081 * (midi - kMidi[0]));   // bass law
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

// The factory stretch table's shape, as per-note cents on f0. Yamaha's own
// alignment procedure; with B this large, equal temperament against the
// reference recordings simply reads as out of tune.
inline double cp70StretchCents (int midi)
{
    static constexpr double kM[] = { 21, 28, 42, 60, 88, 100, 108 };
    static constexpr double kC[] = { -23.0, -13.7, -4.6, 0.0, 7.2, 20.0, 35.0 };
    const double m = static_cast<double> (midi);
    if (m <= kM[0]) return kC[0];
    for (int i = 0; i < 6; ++i)
        if (m <= kM[i + 1])
            return kC[i] + (kC[i + 1] - kC[i]) * (m - kM[i]) / (kM[i + 1] - kM[i]);
    return kC[6];
}

// Per-partial decay in dB/s, from 979 accepted fits on the reference set.
// Below a kilohertz the polynomial is the median of a fast/slow mixture and
// fits neither; the fast component is used there and the slow polarisation
// carries the rest, which is what the measurements prescribe.
//
// The 0.6 factor on the polynomial is a direct measurement correcting a
// population statistic: heterodyne slopes fitted over each reference note's
// full length put C5's partials four through six at 11.7, 11.5 and 19.7
// dB/s where the polynomial says 17, 22 and 26 -- the population median
// mixes registers (a low note's high partial dies faster than a high note's
// at the same frequency), and the audible cost of trusting it was an attack
// band that fell away twice too fast.
// `plainWire` selects the string construction the mode lives on. The 6.5
// dB/s floor below 800 Hz is the fast component of the WOUND-bass mixture:
// every reference partial under 800 Hz comes from a wound string (the lowest
// plain fundamental is D#4 at 311 Hz, and plain notes put their measured
// sub-kHz energy only in their own fundamentals), and a winding dissipates
// by inter-turn friction that a plain wire simply does not have. Applied to
// a plain treble fundamental the floor doubled the decay: the reference C5
// FF's -20 dB time is 2.60 s, which after the attack crest needs the 524 Hz
// fundamental near 3 dB/s -- the polynomial's own value there -- where the
// wound floor forced 6.5 and the model died at 1.40 s. Plain wire therefore
// takes the polynomial at every frequency; the floor and its crossfade
// belong to the wound strings that produced them.
inline double cp70AlphaFast (double f, bool plainWire = false)
{
    const double poly = 0.6 * (0.393 + 9.23e-3 * f - 1.275e-7 * f * f);
    if (plainWire) return std::max (0.5, poly);
    if (f >= 1200.0) return std::max (0.5, poly);
    if (f <= 800.0) return 6.5;
    const double t = (f - 800.0) / 400.0;
    return 6.5 + (std::max (0.5, poly) - 6.5) * t;
}

class CP70Voice
{
public:
    static constexpr int kMaxModes = 132;   // E1 needs 129 at 48 kHz
    using System = SavModalSystem<kMaxModes, 2>;

    struct Config
    {
        double hammerHardness = 0.5;
        double hammerMassNorm = 0.5;
        double escapementNorm = 0.4;
        double damperGrip     = 0.6;
        // Damper felt condition, 0 stock: fresh grips faster, worn lazily,
        // hardened lets the high partials escape -- two bands split at 1.2 kHz.
        double damperFelt     = 0.0;

        double detuneSpread   = 0.5;    // "tipMass" knob: unison beat 0..~0.4 Hz (cents scale as 1/f0)
        double dampTrim       = 0.5;    // "resDamp": global alpha trim x0.7..1.5
        double detuneCents    = 0.0;    // master tune + bend
        // The transducer swap. 0 Magnetic, 1 Native (= the piezo bridge),
        // 2 Electro, 3 Contact (which IS the piezo -- it reads the mount
        // force; selecting it is selecting native). A non-native pickup is
        // a POINT sensor, so it gains the coordinate the bridge never had:
        // a position along the string.
        double transducer     = 1.0;
        double material       = 0.0;  // index into kMaterials; 0 = stock
        double pickupPosNorm  = 0.5;    // fraction of L, mapped 0.08..0.50
        double gapNorm        = 0.35;   // transverse gap for the point pickups
    };

    void prepare (double sampleRate, const MagneticPickup* sharedField = nullptr)
    {
        fs = sampleRate;
        field = sharedField;
        pdec.prepare (sampleRate);
        for (auto& s : str) s.sys.prepare (sampleRate);
        hammer.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& s : str) { s.sys.clear(); s.sys.setNumModes (0); }
        hammer.reset();
        sounding = held = false;
        pedalAmt = 0.0; damperEff = damperFactor;
        controlCounter = 0;
        sinceStrike = 1.0e9;
        peakEnergy = 1.0e-30;
        configured = false;
    }

    // Cut this voice's strings for their note. Called once at prepare and on
    // parameter changes, exactly like RhodesVoice::setNote.
    void setNote (int midiNote, const Config& cfg)
    {
        note = midiNote;
        configure (cfg);
    }

    // The string workshop: this course's own steel. LENGTH re-cuts at the
    // same tension, so pitch follows a string's 1/L and the inharmonicity
    // rises as 1/L^2. GAUGE swaps the wire and re-tensions to pitch, as a
    // tech would: the pitch stands, the tension grows with the cross
    // section, and what moves is B -- proportional to d^2 at constant pitch
    // -- and the mass the hammer meets. Length is the microtonality lane,
    // gauge is the bell-or-thud lane.
    void setGeometryTrim (double lenScale, double diaScale)
    {
        geoLen = std::clamp (lenScale, 0.5, 2.0);
        geoDia = std::clamp (diaScale, 0.4, 2.5);
    }

    void noteOn (int midiNote, double velocity, const Config& cfg, std::uint32_t seed)
    {
        if (! configured || midiNote != note) setNote (midiNote, cfg);
        (void) seed;

        const double vel = std::clamp (velocity, 0.0, 1.0);
        // Same launch law as the Rhodes for now; the plan flags recalibration
        // against the sample set's four velocity layers.
        const double v = 0.18 + 5.6 * std::pow (vel, 1.7);
        const double reg = std::clamp ((note - 28.0) / 72.0, 0.0, 1.0);
        const double escMm = (6.4 - 5.6 * reg) * (0.4 + 1.2 * cfg.escapementNorm);
        hammer.strike (v, escMm * 1.0e-3);
        for (auto& s : str) s.sys.setNumModes (s.kV);   // full set on strike
        sinceStrike = 0.0;
        held = true;
        sounding = true;
        struckAlive = true;
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

    bool isSounding() const { return sounding; }
    bool isRinging() const { return sounding || hammer.isActive(); }
    int  noteNumber() const { return note; }
    int  hammerContactSamples() const { return hammer.contactDurationSamples(); }
    double tipDisplacement() const
    {
        return numStrings > 0 ? str[0].sys.displacement (0) : 0.0;
    }
    double modalEnergy() const
    {
        double e = 0.0;
        for (int s = 0; s < numStrings; ++s) e += str[s].sys.energy();
        return e;
    }

    // One sample: hammer, tick, bridge force. Returns the summed n-weighted
    // termination force -- the piezo's charge, up to the shared electrical
    // chain the engine owns.
    double process (const Config& cfg)
    {
        (void) cfg;
        if (! sounding && ! hammer.isActive()) return 0.0;

        // -- hammer: meets the mean of the strings' patch displacement, and
        // the force splits equally. The only moment the unison pair interact,
        // it is physical (they really do share the tip for a millisecond),
        // and it vanishes at separation.
        if (hammer.isActive())
        {
            double u = 0.0, v = 0.0;
            for (int s = 0; s < numStrings; ++s)
            {
                u += str[s].sys.displacementAt (str[s].strikeShape);
                v += str[s].sys.velocityAt (str[s].strikeShape);
            }
            u /= numStrings; v /= numStrings;
            const double f = hammer.tick (u, v, hammerCfg);
            if (f != 0.0)
            {
                const double each = f / numStrings;
                for (int s = 0; s < numStrings; ++s)
                    for (int m = 0; m < str[s].sys.numModes(); ++m)
                        str[s].sys.addForce (m, each * str[s].strikeShape[m]);
            }
        }

        double force = 0.0;
        if (trans == 1)
        {
            for (int s = 0; s < numStrings; ++s)
            {
                str[s].sys.tick();
                force += str[s].sys.displacementAt (str[s].outShape);
            }
        }
        else
        {
            // The point pickup: displacement at xp, reconstructed at four
            // subsamples (the staircase lesson), pushed through the chosen
            // law, and decimated back to the base rate inside the voice.
            double yp = 0.0;
            for (int s = 0; s < numStrings; ++s)
            {
                str[s].sys.tick();
                yp += str[s].sys.displacementAt (pshape[s]);
            }
            double os[Decimator::kOver];
            for (int k = 0; k < Decimator::kOver; ++k)
            {
                const double t = static_cast<double> (k + 1) / Decimator::kOver;
                double vv = hermiteP (ypHist[0], ypHist[1], ypHist[2], yp, t);
                if (trans == -1)
                    os[k] = 0.0;
                else if (trans == 0)
                    os[k] = (field != nullptr
                              ? (field->flux (static_cast<float> (kMagOffP + vv),
                                              static_cast<float> (pgap)) - magRestP)
                              : 0.0) * kMagOutP * magScaleP;
                else
                {
                    vv = std::min (vv, 0.85 * pgap);
                    os[k] = kElecOutP * (vv / (pgap - vv));
                }
            }
            ypHist[0] = ypHist[1]; ypHist[1] = ypHist[2]; ypHist[2] = yp;
            force = pdec.process (os);
        }

        if (++controlCounter >= 32)
        {
            controlCounter = 0;
            sinceStrike += 32.0 / fs;
            controlTick();
        }

        return force;
    }

    // ---- the frame path: sympathetic resonance --------------------------
    // The strings all terminate on one bridge bolted to one frame. The
    // measured -42 dB beat nulls BOUND this coupling, they do not forbid
    // it: a weak linear spring through the frame preserves superposition
    // exactly (linearity is what the P3 row tests) and stays under the
    // measured pull. Read and write go through the same termination weights
    // -- the reciprocity rule -- so the coupling cannot manufacture energy.
    // The coupling shape is the termination weight family NORMALISED to
    // unit length: reciprocity needs the same shape both ways, and the
    // spring constant should mean the same thing on every voice -- the raw
    // weights carry the piezo's output scaling, which squared through the
    // loop made the spring four hundred times stiffer than intended and
    // rang the coupling itself.
    // The coupling goes through the STRIKE shapes: order-one magnitudes,
    // the same family a hammer's force already travels, and reciprocal by
    // construction. (The first attempt normalised the piezo readout weights
    // and computed the norm before they were filled -- a guard against
    // division by zero then turned into a billion-fold amplifier on both
    // sides of the spring. Order-one shapes need no normalising at all.)
    double clampDisplacement() const
    {
        double u = 0.0;
        for (int s = 0; s < numStrings; ++s)
            u += str[s].sys.displacementAt (str[s].strikeShape);
        return u;
    }

    void addClampForce (double f)
    {
        for (int s = 0; s < numStrings; ++s)
        {
            const int n = str[s].sys.numModes();
            for (int m = 0; m < n; ++m)
                str[s].sys.addForce (m, f * str[s].strikeShape[m]);
        }
    }

    // Woken by the frame, not by a hammer: the voice carries only its
    // sympathetic mode set -- the low modes a frame can actually drive --
    // so a pedalled wash costs a tenth of a struck note. A real strike
    // restores the full set on its way in.
    bool isStruckVoice() const { return struckAlive && (sounding || hammer.isActive()); }

    void wakeSympathetic()
    {
        if (sounding || hammer.isActive()) return;
        struckAlive = false;
        for (int s = 0; s < numStrings; ++s)
            str[s].sys.setNumModes (std::min (str[s].kSymp, str[s].kV));
        sounding = true;
    }

    void applyDamperIfDue()
    {
        // Dampers stop at A6 = MIDI 93 on the real instrument: the top octave
        // rings free, exactly as on an acoustic grand. Key-up above that line
        // changes nothing; the note ends by energy retirement.
        if (note > 93) return;
        if (held || pedalAmt >= 1.0) return;
        for (int s = 0; s < numStrings; ++s)
            for (int m = 0; m < str[s].sys.numModes(); ++m)
                if (str[s].sys.displacement (m) != 0.0)
                    str[s].sys.scaleMode (m, str[s].sys.frequency (m) > 1200.0 ? damperEffHi : damperEff);
    }

private:
    struct Str
    {
        System sys;
        int kV = 0, kH = 0;                       // vertical / horizontal counts
        int kSymp = 6;                            // frame-drivable low modes
        double strikeShape[kMaxModes] {};
        double outShape[kMaxModes] {};
    };

    void controlTick()
    {
        // Deterministic top-down shrink: mode k has died 90 dB down at
        // t = 90/alpha(f_k) after the strike, so the live count is a pure
        // function of elapsed time -- no state inspection, O(1) amortised.
        for (int s = 0; s < numStrings; ++s)
        {
            int k = str[s].sys.numModes();
            while (k > str[s].kH + 2 && sinceStrike > 90.0 / alphaOfMode[s][k - 1])
                --k;
            if (k != str[s].sys.numModes()) str[s].sys.setNumModes (k);
        }

        // Retirement is RELATIVE to this voice's own strike, not absolute:
        // an 87-gram bass string at inaudible amplitude still carries orders
        // of magnitude more energy than a tine ever holds. A fortissimo D3
        // under pedal legitimately runs the better part of a minute -- that
        // is the instrument (its broadband T60 measured 62 seconds), and the
        // shrink schedule above is what makes it affordable.
        const double e = modalEnergy();
        if (! std::isfinite (e)) { for (auto& s : str) s.sys.clear(); sounding = false; return; }
        if (e > peakEnergy) peakEnergy = e;
        if (! hammer.isActive() && e < peakEnergy * 1.0e-10)
        { sounding = false; struckAlive = false; }
    }

    void configure (const Config& cfg)
    {
        const double f0Nom = 440.0 * std::pow (2.0, (note - 69.0) / 12.0)
                           / geoLen;   // a string's pitch follows 1/L
        const double stretch = cp70StretchCents (note);
        const double f0 = f0Nom * std::pow (2.0, (stretch + cfg.detuneCents) / 1200.0);
        // The workshop's trims move B as the string physics says: B is
        // proportional to d^2 at constant pitch (the re-tension carries the
        // rest) and to 1/L^2 through the re-cut.
        // Material: at fixed pitch and gauge the tension re-solves, and what
        // survives is B proportional to E/rho -- bronze halves the steel
        // curve, nylon nearly flattens it. Index 0 is the measured music
        // wire exactly.
        const Material& mat = kMaterials[std::clamp (static_cast<int> (cfg.material), 0, kNumMaterials - 1)];
        const double matB = (static_cast<double> (mat.youngs) / static_cast<double> (mat.density))
                          / (static_cast<double> (kMusicWire.youngs) / static_cast<double> (kMusicWire.density));
        const double B = CP70Inharmonicity::at (note) * (geoDia * geoDia) / (geoLen * geoLen) * matB;

        // ---- geometry, from the measurements --------------------------------
        // Plain wire from D#4 up: the closed length form (R^2 > 0.999) and the
        // parts-list gauge map. Wound bass: length log-interpolated from
        // 568.6 mm at D#4 down to 679 mm at E1 -- the CP-70's own E1, the
        // identification that resolved the buildability contradiction -- and
        // mass from T = 4 f0^2 L^2 mu at the corrected 525 N.
        double L, mu;
        if (note >= 63)
        {
            L = 0.6652 * std::pow (2.0, -(note - 60.0) / 13.16) * geoLen;
            const double d = wireDiameter (note) * geoDia;
            mu = static_cast<double> (mat.density) * kPiD * d * d / 4.0;
        }
        else
        {
            const double t = std::clamp ((63.0 - note) / (63.0 - 28.0), 0.0, 1.2);
            L = 0.5686 * std::pow (0.679 / 0.5686, t) * geoLen;
            const double T = 525.0;               // wound-bass band 450-600 N
            // The gauge trim thickens the winding: mass per length grows with
            // the cross section, and the re-tension below carries the pitch.
            mu = T / (4.0 * f0 * f0 * L * L) * geoDia * geoDia;
        }
        const double T = 4.0 * f0 * f0 * L * L * mu;
        const double modalMass = 0.5 * mu * L;    // pinned-pinned, every mode

        numStrings = note >= 43 ? 2 : 1;

        // Unison spread: a constant BEAT RATE across the compass, not constant
        // cents. A tuner pulls a unison until the beat is slow enough, and the
        // beat is heard in hertz -- so the cents between the pair shrink as
        // 1/f0. The measured detunes say exactly that: A#2 1.93 c (0.13 Hz),
        // D3 1.05 c (0.089 Hz), F3 ~1.5 c (0.15 Hz), A3 ~0.5 c (0.064 Hz) --
        // near-constant tenths of a hertz while the cents fall by 4x. The
        // anchor is the reference D3's directly measured ~9.5 s fundamental
        // beat period (0.105 Hz, 1.24 cents). The earlier flat ~1 cent law
        // held that in the bass but put C5's pair 1.17 c apart -- a 0.35 Hz
        // beat whose first null at 1.41 s punched the broadband envelope
        // through -20 dB and halved the measured -20 dB time against the
        // reference C5 FF sample, which shows no such early null.
        const double spreadCents = 4.0 * cfg.detuneSpread * cfg.detuneSpread
                                 * (0.7 + 0.6 * (((note * 2654435761u) & 255u) / 255.0))
                                 * (1731.2 * 0.105 / f0);   // cents of a 0.105 Hz beat at f0

        // Mode budget: everything under fs/pi vertically; horizontal only
        // below 1.3 kHz where the measured double decay lives.
        const double fMax = std::min (System::kModeBudget * fs, 15279.0);
        const double trim = 0.7 + 0.8 * std::clamp (cfg.dampTrim, 0.0, 1.0);
        const double rr = note <= 27 ? 4.5
                        : note >= 42 ? 7.5
                        : 4.5 + 3.0 * (note - 27) / 15.0;

        for (int s = 0; s < numStrings; ++s)
        {
            const double fs0 = f0 * std::pow (2.0, (s == 1 ? spreadCents : 0.0) / 1200.0);

            int kV = 0;
            for (int k = 1; k <= kMaxModes; ++k)
            {
                const double fk = k * fs0 * std::sqrt (1.0 + B * k * k);
                if (fk >= fMax || kV + 1 >= kMaxModes) break;
                ++kV;
            }
            int kH = 0;
            for (int k = 1; k <= kV; ++k)
            {
                const double fk = k * fs0 * std::sqrt (1.0 + B * k * k);
                if (fk < 1300.0) kH = k;
            }
            kH = std::max (1, kH);
            // Layout: vertical block then horizontal block.
            if (kV + kH > kMaxModes) kH = kMaxModes - kV;

            Str& S = str[s];
            S.kV = kV + kH;   // total modes carried
            S.kH = kH;
            {
                int ks = 0;
                for (int k = 1; k <= kV; ++k)
                    if (k * fs0 * std::sqrt (1.0 + B * k * k) < 1500.0 && ks < 10) ++ks;
                S.kSymp = std::max (1, ks);
            }
            S.sys.setNumModes (kV + kH);

            for (int k = 1; k <= kV; ++k)
            {
                const double fk = k * fs0 * std::sqrt (1.0 + B * k * k);
                // Vertical (fast) polarisation: the plain-wire law above
                // D#4, the wound mixture below it -- the same construction
                // boundary the geometry branch already draws.
                // Material loss on a STRING acts only on the bending share
                // of the energy -- the tension's restoring force is
                // geometric and lossless, which is why a nylon guitar
                // string rings for seconds while a nylon rod clunks. The
                // bending fraction of mode k is the inharmonicity term
                // B k^2 / (1 + B k^2): nearly nothing for low partials,
                // growing with k -- so nylon keeps its fundamental and
                // sheds its highs, the warm pluck-into-sustain of the real
                // material. Cantilevers keep the full loss, because a
                // cantilever's restoring force IS bending.
                const double bendFrac = (B * k * k) / (1.0 + B * k * k);
                const double aV = trim * cp70AlphaFast (fk, note >= 63)
                                + 8.686 * kPiD * fk * bendFrac
                                  * std::max (0.0, static_cast<double> (mat.lossEta) - static_cast<double> (kMusicWire.lossEta));
                S.sys.setMode (k - 1, fk, 60.0 / aV, modalMass);
                alphaOfMode[s][k - 1] = aV;

                // The piezo reads termination force: T * k*pi/L, the +6 dB
                // per octave tilt as an exact modal weight. The strike shape
                // is the pinned-string sine at beta with the fixed 12 mm
                // urethane patch folded in.
                // The bridge foot has width. The piezo reads the force
                // integrated over the foot's contact length, not at a point,
                // so partials whose half-wavelength approaches the footprint
                // self-cancel across it -- a physical taper on exactly the
                // high components that otherwise arrive phase-aligned at the
                // strike and stack into an attack spike the real recordings
                // do not show.
                const double footArg = k * kPiD * (kBridgeFoot / 2.0) / L;
                const double foot = std::abs (footArg) < 1e-9 ? 1.0
                                  : std::sin (footArg) / footArg;
                S.outShape[k - 1] = T * k * kPiD / L * foot * kOutScale;
                const double beta = strikeBeta();
                const double patch = std::min (0.45, 0.006 / L);
                const double z = k * kPiD * patch;
                const double w = std::abs (z) < 1e-9 ? 1.0 : std::sin (z) / z;
                // The tip is compliant. A urethane pad cannot follow -- or
                // push -- string ripple above its own contact resonance, so
                // the coupling shape itself rolls off second-order above
                // kContactHz. Sensing and forcing share the weight (the
                // reciprocity rule), which keeps the coupled system passive
                // and also removes the one-sample-delayed point coupling's
                // spurious drive near Nyquist: without this the launch
                // carried partials at five kilohertz twenty decibels above
                // the recordings, whatever the contact stiffness, because
                // the rigid point contact was chasing ripple no real tip
                // can feel.
                const double comp = 1.0 / (1.0 + (fk / kContactHz) * (fk / kContactHz));
                S.strikeShape[k - 1] = std::sin (k * kPiD * beta) * w * comp;
            }
            for (int k = 1; k <= kH; ++k)
            {
                // The slow polarisation: +0.75 cents, r times slower, reached
                // only through the hammer's slight skew.
                const double fk = k * fs0 * std::pow (2.0, 0.75 / 1200.0)
                                * std::sqrt (1.0 + B * k * k);
                // The slow polarisation keeps the wound-mixture law on every
                // note: the doublet ratio r and the slow-member rates are
                // direct fits of measured slow components at these
                // frequencies (all on wound strings -- above C5 the research
                // found no separable slow member at all), so the plain-wire
                // correction to the FAST law must not stretch them; launched
                // at tp^2 they are inaudibly far down on plain notes anyway.
                const double aH = trim * cp70AlphaFast (fk) / rr
                                + 8.686 * kPiD * fk * ((B * k * k) / (1.0 + B * k * k))
                                  * std::max (0.0, static_cast<double> (mat.lossEta) - static_cast<double> (kMusicWire.lossEta));
                const int idx = kV + k - 1;
                S.sys.setMode (idx, fk, 60.0 / aH, modalMass);
                alphaOfMode[s][idx] = aH;
                const double footArgH = k * kPiD * (kBridgeFoot / 2.0) / L;
                const double footH = std::abs (footArgH) < 1e-9 ? 1.0
                                   : std::sin (footArgH) / footArgH;
                S.outShape[idx] = T * k * kPiD / L * footH * kOutScale;
                const double beta = strikeBeta();
                // The slow polarisation's launch level follows the
                // measurements: the D3 doublet members are within a decibel
                // of each other while C4's slow member sits 9 dB down, and
                // above C5 the fast component alone reproduces the measured
                // envelope times. A 0.89 launch is that one decibel; the
                // square on the taper is what puts C4 nine down. The earlier
                // 0.5 prefactor contradicted the very measurement quoted
                // above it, and the audible result was a bass whose
                // fundamental died at the fast polarisation's 6.5 dB/s when
                // the recordings sustain at three -- the slow member IS the
                // bass sustain, and it was launched six decibels short.
                const double tp = std::clamp (1.0 - (note - 50.0) / 26.0, 0.12, 1.0);
                const double skew = 0.89 * tp * tp;
                const double compH = 1.0 / (1.0 + (fk / kContactHz) * (fk / kContactHz));
                S.strikeShape[idx] = skew * std::sin (k * kPiD * beta) * compH;
            }
            for (int i = S.kV; i < kMaxModes; ++i)
            { S.outShape[i] = 0.0; S.strikeShape[i] = 0.0; }
        }

        // ---- the point pickup, when one is fitted ---------------------------
        // A magnetic or electrostatic pickup is a POINT sensor at xp along
        // the string -- the coordinate the rigid piezo bridge never had.
        // Its readout weight for mode k is sin(k pi xp) with the aperture's
        // sinc folded in: the comb voicing every guitarist knows, obtained
        // from the same modal state, not from a filter.
        {
            const int t = static_cast<int> (cfg.transducer + 0.5);
            trans = (t == 3) ? 1 : t;       // contact IS the piezo here
            // Transducer facts of the selected material: a bare-conductor
            // string speaks only faintly through a magnetic pickup (eddy
            // currents -- the bronze-acoustic-string fact), and nylon cannot
            // be the moving plate of an electrostatic one at all. The piezo
            // reads force and does not care. The string keeps vibrating
            // either way, and the panel says what the pickup can hear.
            {
                const Material& mm = kMaterials[std::clamp (static_cast<int> (cfg.material), 0, kNumMaterials - 1)];
                magScaleP = magneticCoupling (mm);
                if (trans == 2 && ! mm.conductive)
                    trans = -1;   // an insulator transduces nothing here
            }
            const double xp = 0.08 + 0.42 * std::clamp (cfg.pickupPosNorm, 0.0, 1.0);
            pgap = 0.8e-3 + 3.2e-3 * std::clamp (cfg.gapNorm, 0.0, 1.0);
            for (int si = 0; si < 2; ++si)
                for (int m = 0; m < kMaxModes; ++m) pshape[si][m] = 0.0;
            for (int si = 0; si < numStrings; ++si)
            {
                Str& S = str[si];
                const int kV = S.kV - S.kH;
                for (int k = 1; k <= kV; ++k)
                {
                    const double ap = k * kPiD * 0.004 / 0.6;   // ~8 mm aperture
                    const double w = std::abs (ap) < 1e-9 ? 1.0 : std::sin (ap) / ap;
                    pshape[si][k - 1] = std::sin (k * kPiD * xp) * w;
                }
            }
            magRestP = (field != nullptr)
                     ? field->flux (static_cast<float> (kMagOffP), static_cast<float> (pgap)) : 0.0;
        }

        // ---- hammer: urethane over leather, not felt ------------------------
        const double reg = std::clamp ((note - 28.0) / 72.0, 0.0, 1.0);
        hammerCfg.alpha = 2.2;
        // Softer than the plan's first numbers by a factor of six, and the
        // change is data-driven, not taste: with the stiffer law the model's
        // C4 came out with its THIRD harmonic loudest where the real
        // recording has the fundamental on top by 11 dB, and C5 carried
        // partials six through ten only 11 dB down -- heard as "very bright,
        // missing lows and mids, sharp". The hammer's contact time is the
        // lowpass that shapes the launch spectrum, and the per-partial A/B
        // against the reference samples is what sets it now.
        hammerCfg.stiffness = 1.3e8 * std::pow (1.2e9 / 1.3e8, reg)
                            * std::pow (12.0, cfg.hammerHardness - 0.5);
        hammerCfg.lambda = 0.6;
        hammerCfg.mass = 0.011 * std::pow (4.0 / 11.0, reg)
                       * (0.6 + 0.8 * cfg.hammerMassNorm);
        {
            // Same explicit-contact stability cap as the Rhodes; the plan
            // expects it to bind above ~C6 and names the quadratised contact
            // as the escalation if the capped treble reads soft.
            const double phi = std::sin (kPiD * strikeBeta());
            const double effM = modalMass / std::max (1.0e-6, phi * phi);
            const double mRed = 1.0 / (1.0 / hammerCfg.mass + 1.0 / effM);
            const double wMax = 2.0 * kPiD * 0.06 * fs;
            const double kMax = mRed / 2.0 * std::pow (wMax / 1.5305, 3.0);
            if (hammerCfg.stiffness > kMax) hammerCfg.stiffness = kMax;
        }

        const double grip = std::clamp (cfg.damperGrip, 0.0, 1.0);
        const double damperT60 = (0.30 - 0.24 * grip) * (1.0 + 1.4 * (1.0 - reg));
        damperFactor = std::exp (-3.0 * std::log (10.0) / (damperT60 * fs));
        {
            const int f = std::clamp (static_cast<int> (cfg.damperFelt + 0.5), 0, 3);
            feltWLo = (f == 1) ? 1.25 : (f == 2) ? 0.55 : 1.0;
            feltWHi = (f == 1) ? 1.25 : (f == 2) ? 0.55 : (f == 3) ? 0.12 : 1.0;
        }
        setPedal (pedalAmt);

        configured = true;
    }

    double strikeBeta() const
    {
        // Near 1/8 through the compass, rising toward 1/6 at the top where
        // the physical strike distance stays put while L collapses. Not
        // EXACTLY 1/8: the reference D3's launch spectrum shows partial
        // eight at a healthy -24 dB where an exact one-eighth strike puts a
        // null -- the measured spectrum has no deep strike nulls anywhere,
        // so the ratio is held a little off the integer division.
        if (note <= 88) return 0.118;
        return 0.118 + (1.0 / 6.0 - 0.118) * std::min (1.0, (note - 88.0) / 12.0);
    }

    static double wireDiameter (int note)
    {
        // The parts-list gauge map (CP-80 numbering in MIDI), interpolated.
        static constexpr double kM[] = { 63, 65, 72, 78, 84, 92, 97, 101, 105 };
        static constexpr double kD[] = { 1.025e-3, 0.975e-3, 0.950e-3, 0.925e-3,
                                         0.900e-3, 0.875e-3, 0.850e-3, 0.825e-3, 0.800e-3 };
        const double m = note;
        if (m <= kM[0]) return kD[0];
        for (int i = 0; i < 8; ++i)
            if (m <= kM[i + 1])
                return kD[i] + (kD[i + 1] - kD[i]) * (m - kM[i]) / (kM[i + 1] - kM[i]);
        return kD[8];
    }

    // One scale for the whole instrument, folding the piezo charge constant
    // and the preamp input into a number that lands the output near the
    // Rhodes' level. Calibrated by the level test row.
    static constexpr double kOutScale = 8.0e-3;
    static constexpr double kBridgeFoot = 0.040;   // m, the piezo's integration length
    static constexpr double kContactHz = 1500.0;   // tip compliance corner, calibrated on the launch spectra

    double fs = 48000.0;
    double geoLen = 1.0, geoDia = 1.0;      // the workshop's trims
    // The point-pickup machinery for the transducer swap: readout shape at
    // a position along the string, a hermite history for the oversampled
    // laws, and a private decimator so the voice still returns one base-rate
    // sample to the bus.
    static double hermiteP (double a, double b, double c, double d, double t)
    {
        const double m0 = 0.5 * (c - a), m1 = 0.5 * (d - b);
        const double t2 = t * t, t3 = t2 * t;
        return (2*t3 - 3*t2 + 1) * b + (t3 - 2*t2 + t) * m0
             + (-2*t3 + 3*t2) * c + (t3 - t2) * m1;
    }
    const MagneticPickup* field = nullptr;
    Decimator pdec;
    int trans = 1;                          // resolved: 1/3 piezo, 0 mag, 2 electro
    double pshape[2][kMaxModes] {};
    double ypHist[3] {};
    double pgap = 1.5e-3;
    double magRestP = 0.0;
    static constexpr double kMagOffP   = -0.9e-3;   // m, off the pole centre
    static constexpr double kMagOutP   = 0.53;    // level-matched by probe
    static constexpr double kElecOutP  = 8.0e-2;
    int note = 60;
    bool struckAlive = false;
    int numStrings = 1;
    Str str[2];
    HuntCrossleyHammer hammer;
    HuntCrossleyHammer::Config hammerCfg;
    double alphaOfMode[2][kMaxModes] {};
    double damperFactor = 1.0;
    double damperEff = 1.0, damperEffHi = 1.0;
    double feltWLo = 1.0, feltWHi = 1.0;
    double pedalAmt = 0.0;
    double magScaleP = 1.0;   // eddy-scaled magnetic coupling of the material
    double sinceStrike = 1.0e9;
    double peakEnergy = 1.0e-30;
    int controlCounter = 0;
    bool sounding = false, held = false, configured = false;
};

} // namespace epi
