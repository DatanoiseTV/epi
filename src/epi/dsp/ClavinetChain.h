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

namespace epi
{

// ---------------------------------------------------------------------------
// The Clavinet's shared electrical path: the four tone rockers, the
// transformer-coupled two-transistor preamp, and the tangent knock. Everything
// sixty strings share lives here; the per-string physics lives in
// ClavinetVoice.h.
//
// Sources: docs/clavinet-implementation-plan.md section 4 against
// docs/research/clavinet-measured.md section 9 — the D6/E7 schematics
// (Schaltbild 710 314 / St.-Nr. 801 111, identical component values) and
// EURASIP 2013:103, whose Table 3 gives the exact bilinear H_i(z) for each
// rocker branch and whose Fig. 14 validates the cascade against SPICE.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The four tone rockers, as the real passive branches.
//
// Each engaged rocker switches an R/RC/RLC branch between the two BC 550C
// stages [R, both schematics]:
//
//   Soft      (tief)   R = 30 kOhm, C = 0.1 uF     Z = R/(1+sRC)
//   Medium    (mittel) R = 10 kOhm, C = 15 nF      Z = R/(1+sRC)
//   Treble    (hoch)   L = 2 H, C = 4.7 nF         Z = (R_w+sL)/(1+sR_wC+s^2LC)
//   Brilliant (scharf) L = 0.6 H                   Z = sL
//
// Each Z_i(s) goes through the bilinear transform and the engaged H_i(z) are
// cascaded — EURASIP's own recipe, matched against SPICE in their Fig. 14, so
// the shortcut is pre-validated for the combinations they showed. The full
// loaded-divider circuit stays open question 5 of the plan.
//
// Two deviations from the paper's Table 3, both stated:
//
//  - The Treble branch as printed is a LOSSLESS LC: its bilinear poles sit
//    exactly on the unit circle, and a lossless resonator driven at resonance
//    grows without bound. A real inductor is not lossless — the schematic
//    prints the winding: 700 turns of 0.09 mm CuL, roughly 22 m of wire,
//    about 60 ohms of copper. That series resistance goes into Z(s) before
//    the transform, which damps the pole (Q ~ 350 at the 1.6 kHz branch
//    resonance) and makes the branch passive, as the physical part is.
//  - The H_i carry impedance units, so the cascade's absolute level is
//    arbitrary. One shared reference — |Z_medium| at 1 kHz — scales every
//    stage, so Medium alone is near unity at 1 kHz and the RELATIVE loudness
//    between rockers stays what the branch impedances say [D].
//
// Hohner's own rule ships: at least one rocker down or the instrument is
// silent [R, both manuals] — enforced by falling back to Medium when the
// caller clears all four, because a DAW parameter that silences the plugin is
// a support ticket, not authenticity [D].
// ---------------------------------------------------------------------------
class ClavinetToneStack
{
public:
    // Component values, identical in the D6 and E7 schematics and in EURASIP
    // Table 3.
    static constexpr double kSoftR = 30.0e3, kSoftC = 0.1e-6;
    static constexpr double kMedR  = 10.0e3, kMedC  = 15.0e-9;
    static constexpr double kTrebL = 2.0,    kTrebC = 4.7e-9;
    static constexpr double kTrebRw = 60.0;   // 700 turns 0.09 mm CuL, computed
    static constexpr double kBrilL = 0.6;
    // The rocker network does not hang off an ideal source: it divides
    // against the pickup coil's own impedance, and that division is what
    // tames the branch resonances in the real instrument. Driven ideally,
    // the Treble branch's LC computes to Q ~ 340 and was heard as exactly
    // that -- a heavy 1.6 kHz resonance under the funk registration. With
    // the source in place the working Q is a handful, and Brilliant's bare
    // inductor becomes the ~1.5 kHz high-pass it must physically be rather
    // than a raw differentiator. 5.5 kOhm is the single-coil class typical;
    // the exact coil is the research doc's open contradiction and owns the
    // number.
    static constexpr double kSourceR = 5.5e3;

    // |D_medium(j 2 pi 1000)| — the shared level reference: the medium
    // branch's divider gain at 1 kHz, so Medium alone stays near its old
    // level and the RELATIVE rocker balance follows the circuit.
    static double zRef()
    {
        const double w = 2.0 * kPiD * 1000.0;
        const double tau = kSourceR * kMedR * kMedC / (kMedR + kSourceR);
        const double g0 = kMedR / (kMedR + kSourceR);
        return g0 / std::sqrt (1.0 + w * w * tau * tau);
    }

    void prepare (double rate)
    {
        fs = rate;
        const double K = 2.0 * fs;
        const double inv = 1.0 / zRef();

        // Soft and Medium: the divider of the R-parallel-C branch against
        // the source, D = (R/(R+Rs)) / (1 + s RsRC/(R+Rs)), bilinear.
        auto firstOrder = [&] (Sec& s, double R, double C)
        {
            const double g0  = R / (R + kSourceR);
            const double tau = kSourceR * R * C / (R + kSourceR);
            const double kt  = K * tau;
            s.b0 = inv * g0 / (1.0 + kt);
            s.b1 = s.b0;
            s.b2 = 0.0;
            s.a1 = (1.0 - kt) / (1.0 + kt);
            s.a2 = 0.0;
        };
        firstOrder (sec[0], kSoftR, kSoftC);
        firstOrder (sec[1], kMedR, kMedC);

        // Treble: D = (Rw + sL) / ((Rw+Rs) + s (L + Rs Rw C) + s^2 Rs L C).
        {
            const double c0 = kTrebRw + kSourceR;
            const double c1 = kTrebL + kSourceR * kTrebRw * kTrebC;
            const double c2 = kSourceR * kTrebL * kTrebC;
            const double a0 = c0 + c1 * K + c2 * K * K;
            Sec& s = sec[2];
            s.b0 = inv * (kTrebRw + kTrebL * K) / a0;
            s.b1 = inv * 2.0 * kTrebRw / a0;
            s.b2 = inv * (kTrebRw - kTrebL * K) / a0;
            s.a1 = (2.0 * c0 - 2.0 * c2 * K * K) / a0;
            s.a2 = (c0 - c1 * K + c2 * K * K) / a0;
        }

        // Brilliant: D = sL / (Rs + sL) -- the high-pass the bare inductor
        // makes against the source, bilinear.
        {
            const double kl = K * kBrilL;
            const double a0 = kSourceR + kl;
            Sec& s = sec[3];
            s.b0 = inv * kl / a0;
            s.b1 = -s.b0;
            s.b2 = 0.0;
            s.a1 = (kSourceR - kl) / a0;
            s.a2 = 0.0;
        }
        reset();
    }

    void reset()
    {
        for (auto& s : sec) s.z1 = s.z2 = 0.0;
    }

    // The four rockers. All-up falls back to Medium (see the class comment).
    void setRockers (bool soft, bool medium, bool treble, bool brilliant)
    {
        on[0] = soft; on[1] = medium; on[2] = treble; on[3] = brilliant;
        if (! (soft || medium || treble || brilliant)) on[1] = true;
    }

    bool rockerEngaged (int i) const { return i >= 0 && i < 4 && on[i]; }

    double process (double x)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (! on[i]) continue;
            Sec& s = sec[i];
            const double y = s.b0 * x + s.z1;
            s.z1 = s.b1 * x - s.a1 * y + s.z2;
            s.z2 = s.b2 * x - s.a2 * y;
            x = y;
        }
        return x;
    }

private:
    struct Sec
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };
    Sec sec[4];
    bool on[4] { false, true, false, false };   // Medium is the rest default
    double fs = 48000.0;
};

// ---------------------------------------------------------------------------
// The preamp: what the amplifier does once the tone stack is taken out.
//
//  - Frequency response: SPICE on the real schematic puts the amplifier
//    minus tone stack near flat with a mild -3 dB low shelf at 130 Hz and a
//    +3 dB high shelf at 4 kHz [M, EURASIP 2.3.6]. Two first-order shelves.
//  - Nonlinearity: measured on the real amplifier with a 1 kHz sine — THD 1%
//    at 400 mV input (the maximum pickup level in normal polyphonic playing),
//    rising to 3.6% at fortissimo chord peaks [M, EURASIP 3.4]. The 1:15
//    input transformer and the 8.2 V zener rail mean the circuit runs out of
//    headroom from the top, not the bottom [R], so the waveshaper is
//    asymmetric-soft [D], and its drive constant is calibrated so THD reads
//    1% at exactly 0.4 V in (row E1 of the reference suite is the
//    calibration's measurement).
//
// The drive control scales into and past that calibration; below 1.0 it
// behaves as the volume pot it physically is (fixed makeup, so the clipped
// ceiling stays where the supply puts it — the Wurlitzer preamp's rule).
// ---------------------------------------------------------------------------
class ClavinetPreamp
{
public:
    void prepare (double rate)
    {
        input.setCutoff (10.0, rate);
        low.setCutoff (130.0, rate);
        high.setCutoff (4000.0, rate);
        reset();
    }

    void reset() { input.reset(); low.reset(); high.reset(); }

    // 1.0 is the circuit's own level; the calibrated THD numbers hold there.
    void setDrive (double d) { drive = std::clamp (d, 0.0, 4.0); }

    double process (double x)
    {
        // Amplifier-minus-tone-stack response [M]: -3 dB below 130 Hz,
        // +3 dB above 4 kHz.
        double y = x + kLowGain * low.lowpass (x);
        y += kHighGain * high.highpass (y);

        // The asymmetric stage. kDrive is the THD calibration: with the
        // headroom split below, a 0.4 V sine measures 1.0% THD and a 0.9 V
        // fortissimo chord peak reaches the 3.6% region.
        const double u = y * kDrive * drive;
        const double s = u >= 0.0 ? kSatPos * std::tanh (u / kSatPos)
                                  : kSatNeg * std::tanh (u / kSatNeg);
        // The output coupling capacitor, placed where the circuit's is: a
        // tangent-held string sits statically deflected as long as the key
        // is down (the flux polynomial makes DC of it), and the asymmetric
        // stage rectifies its own offset on top. Both belong to the cap.
        // 10 Hz keeps the measured 30 Hz shelf row inside its band.
        return input.highpass (s / kDrive);
    }

private:
    // 10^(-3/20) - 1 and 10^(+3/20) - 1: the shelf plateaus.
    static constexpr double kLowGain  = -0.2920542156158621;
    static constexpr double kHighGain = +0.4125375446227544;

    // Headroom ratio ~1:2.75: saturation (transformer + zener ceiling) bites
    // well before cutoff, the asymmetry the schematic implies. kDrive lands
    // THD(0.4 V) = 1.0% — measured by row E1, not assumed.
    static constexpr double kSatPos = 1.0;
    static constexpr double kSatNeg = 2.75;
    static constexpr double kDrive  = 0.87;

    OnePoleD input, low, high;
    double drive = 1.0;
};

// ---------------------------------------------------------------------------
// The tangent knock. The tangent/anvil impact rings the soundboard into the
// pickups; measured energy sits below ~1.2 kHz, and one knock serves all
// keys — its level does not track the note, which is why it reads loudest
// against high notes (E6's fundamental is above the whole knock band)
// [M, EURASIP 3.5]. Synthesized, not sampled [D]: a couple of board modes and
// a lowpassed thump, seeded per key so key forty's knock is always key
// forty's, with the source's own per-trigger cutoff scatter.
//
// The engine seat owns the mix level; kLevel here is a unit reference.
// ---------------------------------------------------------------------------
class ClavinetKnock
{
public:
    void prepare (double rate) { fs = rate; reset(); }

    void reset()
    {
        env = 0.0; thump = 0.0; ph1 = ph2 = 0.0; lp1 = lp2 = 0.0; noise = 22222u;
    }

    void strike (int key, double velocity)
    {
        const double vel = std::clamp (velocity, 0.0, 1.0);
        const std::uint32_t h = static_cast<std::uint32_t> (key) * 2654435761u;
        const double u1 = ((h >> 7) & 1023u) / 1023.0;
        const double u2 = ((h >> 17) & 1023u) / 1023.0;

        // Board modes under the measured 1.2 kHz ceiling, scattered per key.
        f1 = 170.0 * (1.0 + 0.15 * (u1 - 0.5));
        f2 = 640.0 * (1.0 + 0.20 * (u2 - 0.5));

        // The source lowpasses its knock with a slightly randomized cutoff
        // per trigger; the noise floor here gets the same treatment.
        noise = h | 1u;
        const double cut = 850.0 * (0.85 + 0.3 * u1);
        aCut = 1.0 - std::exp (-2.0 * kPiD * cut / fs);

        env   = kLevel * vel;
        thump = kLevel * vel * 0.8;
        dEnv   = std::exp (-1.0 / (0.035 * fs));
        dThump = std::exp (-1.0 / (0.012 * fs));
    }

    bool isActive() const { return env > 1.0e-7 || thump > 1.0e-7; }

    double process()
    {
        if (! isActive()) return 0.0;

        // Two rung modes plus a band of lowpassed noise; everything under
        // the measured ceiling by construction.
        ph1 += f1 / fs; if (ph1 >= 1.0) ph1 -= 1.0;
        ph2 += f2 / fs; if (ph2 >= 1.0) ph2 -= 1.0;
        double out = env * (0.7 * std::sin (2.0 * kPiD * ph1)
                          + 0.4 * std::sin (2.0 * kPiD * ph2));

        noise = noise * 1664525u + 1013904223u;
        const double n = static_cast<double> (static_cast<std::int32_t> (noise)) * (1.0 / 2147483648.0);
        lp1 += aCut * (n - lp1);
        lp2 += aCut * (lp1 - lp2);          // second order: band stays put
        lp0 += 0.25 * aCut * (lp2 - lp0);   // slow floor removes the DC drift
        out += thump * (lp2 - lp0);

        env *= dEnv;
        thump *= dThump;
        return out;
    }

private:
    static constexpr double kLevel = 0.05;   // V at the pickup bus, unit reference

    double fs = 48000.0;
    double env = 0.0, thump = 0.0, dEnv = 0.0, dThump = 0.0;
    double f1 = 170.0, f2 = 640.0, ph1 = 0.0, ph2 = 0.0;
    double aCut = 0.1, lp1 = 0.0, lp2 = 0.0, lp0 = 0.0;
    std::uint32_t noise = 22222u;
};

} // namespace epi
