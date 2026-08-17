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
inline double cp70AlphaFast (double f)
{
    const double poly = 0.393 + 9.23e-3 * f - 1.275e-7 * f * f;
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
        double detuneSpread   = 0.5;    // "tipMass" knob: unison spread 0..5 cents
        double dampTrim       = 0.5;    // "resDamp": global alpha trim x0.7..1.5
        double detuneCents    = 0.0;    // master tune + bend
    };

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
        sounding = held = pedal = false;
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
    }

    void noteOff() { held = false; }
    void setPedal (bool down) { pedal = down; }

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
        for (int s = 0; s < numStrings; ++s)
        {
            str[s].sys.tick();
            force += str[s].sys.displacementAt (str[s].outShape);
        }

        if (++controlCounter >= 32)
        {
            controlCounter = 0;
            sinceStrike += 32.0 / fs;
            controlTick();
        }

        return force;
    }

    void applyDamperIfDue()
    {
        // Dampers stop at A6 = MIDI 93 on the real instrument: the top octave
        // rings free, exactly as on an acoustic grand. Key-up above that line
        // changes nothing; the note ends by energy retirement.
        if (note > 93) return;
        if (held || pedal) return;
        for (int s = 0; s < numStrings; ++s)
            for (int m = 0; m < str[s].sys.numModes(); ++m)
                if (str[s].sys.displacement (m) != 0.0)
                    str[s].sys.scaleMode (m, damperFactor);
    }

private:
    struct Str
    {
        System sys;
        int kV = 0, kH = 0;                       // vertical / horizontal counts
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
            sounding = false;
    }

    void configure (const Config& cfg)
    {
        const double f0Nom = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
        const double stretch = cp70StretchCents (note);
        const double f0 = f0Nom * std::pow (2.0, (stretch + cfg.detuneCents) / 1200.0);
        const double B = CP70Inharmonicity::at (note);

        // ---- geometry, from the measurements --------------------------------
        // Plain wire from D#4 up: the closed length form (R^2 > 0.999) and the
        // parts-list gauge map. Wound bass: length log-interpolated from
        // 568.6 mm at D#4 down to 679 mm at E1 -- the CP-70's own E1, the
        // identification that resolved the buildability contradiction -- and
        // mass from T = 4 f0^2 L^2 mu at the corrected 525 N.
        double L, mu;
        if (note >= 63)
        {
            L = 0.6652 * std::pow (2.0, -(note - 60.0) / 13.16);
            const double d = wireDiameter (note);
            mu = 7850.0 * kPiD * d * d / 4.0;
        }
        else
        {
            const double t = std::clamp ((63.0 - note) / (63.0 - 28.0), 0.0, 1.2);
            L = 0.5686 * std::pow (0.679 / 0.5686, t);
            const double T = 525.0;               // wound-bass band 450-600 N
            mu = T / (4.0 * f0 * f0 * L * L);
        }
        const double T = 4.0 * f0 * f0 * L * L * mu;
        const double modalMass = 0.5 * mu * L;    // pinned-pinned, every mode

        numStrings = note >= 43 ? 2 : 1;

        // Unison spread: nominal 1.2 cents at the default knob, deterministic
        // per-note scatter -- a well-maintained CP sits at the low end of the
        // measured 0.5..5 cent range.
        const double spreadCents = 5.0 * cfg.detuneSpread * cfg.detuneSpread
                                 * (0.7 + 0.6 * (((note * 2654435761u) & 255u) / 255.0));

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
            S.sys.setNumModes (kV + kH);

            for (int k = 1; k <= kV; ++k)
            {
                const double fk = k * fs0 * std::sqrt (1.0 + B * k * k);
                const double aV = trim * cp70AlphaFast (fk);
                S.sys.setMode (k - 1, fk, 60.0 / aV, modalMass);
                alphaOfMode[s][k - 1] = aV;

                // The piezo reads termination force: T * k*pi/L, the +6 dB
                // per octave tilt as an exact modal weight. The strike shape
                // is the pinned-string sine at beta with the fixed 12 mm
                // urethane patch folded in.
                S.outShape[k - 1] = T * k * kPiD / L * kOutScale;
                const double beta = strikeBeta();
                const double patch = std::min (0.45, 0.006 / L);
                const double z = k * kPiD * patch;
                const double w = std::abs (z) < 1e-9 ? 1.0 : std::sin (z) / z;
                S.strikeShape[k - 1] = std::sin (k * kPiD * beta) * w;
            }
            for (int k = 1; k <= kH; ++k)
            {
                // The slow polarisation: +0.75 cents, r times slower, reached
                // only through the hammer's slight skew.
                const double fk = k * fs0 * std::pow (2.0, 0.75 / 1200.0)
                                * std::sqrt (1.0 + B * k * k);
                const double aH = trim * cp70AlphaFast (fk) / rr;
                const int idx = kV + k - 1;
                S.sys.setMode (idx, fk, 60.0 / aH, modalMass);
                alphaOfMode[s][idx] = aH;
                S.outShape[idx] = T * k * kPiD / L * kOutScale;
                const double beta = strikeBeta();
                // The slow polarisation's launch level follows the
                // measurements: the D3 doublet members are within a decibel
                // of each other while C4's slow member sits 9 dB down, and
                // above C5 the fast component alone reproduces the measured
                // envelope times -- so the skew tapers with register.
                const double skew = 0.5 * std::clamp (1.0 - (note - 50.0) / 26.0, 0.12, 1.0);
                S.strikeShape[idx] = skew * std::sin (k * kPiD * beta);
            }
            for (int i = S.kV; i < kMaxModes; ++i)
            { S.outShape[i] = 0.0; S.strikeShape[i] = 0.0; }
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

        configured = true;
    }

    double strikeBeta() const
    {
        // 1/8 through the compass, rising to 1/6 at the top where the physical
        // strike distance stays put while L collapses. The one parameter with
        // no evidence behind it -- named as the calibration variable, and the
        // fundamental-poor-bass row is its designated target.
        if (note <= 88) return 1.0 / 8.0;
        return 1.0 / 8.0 + (1.0 / 6.0 - 1.0 / 8.0) * std::min (1.0, (note - 88.0) / 12.0);
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
    static constexpr double kOutScale = 2.7e-3;

    double fs = 48000.0;
    int note = 60;
    int numStrings = 1;
    Str str[2];
    HuntCrossleyHammer hammer;
    HuntCrossleyHammer::Config hammerCfg;
    double alphaOfMode[2][kMaxModes] {};
    double damperFactor = 1.0;
    double sinceStrike = 1.0e9;
    double peakEnergy = 1.0e-30;
    int controlCounter = 0;
    bool sounding = false, held = false, pedal = false, configured = false;
};

} // namespace epi
