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

#include <type_traits>

namespace epi
{

// ---------------------------------------------------------------------------
// The grand's soundboard: the low band of a 2 m grand's board as a shared
// modal system, per docs/grand-implementation-plan.md section 3.2. Only this
// band -- up to ~1.3 kHz, where Ege & Boutillon measure the board behaving as
// one orthotropic plate -- sits inside the string feedback loop. Above it the
// board's mobility is featureless (inter-rib waveguides, ~100% modal overlap),
// which a string partial experiences as pure per-mode loss; that part is
// folded into the string T60s and never simulated here.
//
// The numbers, all from the research file:
//   - 72 modes, first at 75 Hz, modal density rising to the measured
//     n ~ 0.058 modes/Hz -- 72 modes is exactly 75..1300 Hz at that density.
//   - loss factor eta = 2%: per-mode T60 = 2.2/(eta f). This loss IS the
//     prompt sound's energy drain; the strings' own T60s are never shortened
//     by hand to fake it.
//   - modal masses M/4 = 2.25 kg (Ege's 9 kg board).
//   - per-note bridge shapes Phi sampled from rectangular-orthotropic-plate
//     mode shapes along a bridge arc: deterministic, sign-varying, continuous
//     in note number -- which is what makes neighbouring notes couple to
//     correlated board motion and sympathetic response selective. The exact
//     shapes of any one board are unknowable; the defensible content is the
//     statistics (Skudrzyk mean-value law), which the amplitude below pins.
//
// The mode ladder and the shapes come from one construction: half-wave
// counts (p, r) on an equivalent orthotropic plate with f_pr = a p^2 + b r^2,
// where a*b sets the modal density (n = (pi/4)/sqrt(ab)) and a+b the first
// mode -- a board much denser in p (along the grain) than r, which is the
// real anisotropy. a and the arc constants below were fit, by a random
// search plus anneal over exactly these free parameters, so that the
// per-note conductance pattern reproduces the MEASURED per-note decay
// pattern at the fundamentals AND at the second partials, converted to
// single-string bridge rates through alpha = 8.686 (T/L) G(f) (the
// mode-independent loop weight): C4 and C5 fundamentals near mobility peaks
// (coupled fast components 23.3 and 33.6 dB/s), A3's in a dip (its
// trichord's fastest normal mode only 9.4 dB/s), D#2 gentle, C3's
// fundamental modest (its own early rate is just -3.96 dB/s), the second
// partials of C4/C5/C3 SLOW (measured 4.3 / 13.1 / 4.8 dB/s -- the first
// fit ignored them, left C4's P2 band 20 dB/s hot, and the radiated
// envelopes fell to the aftersound a second early across the mid compass),
// and D#1/A0's low partials slow enough to carry their measured 7.6 / 6.7 s
// -20 dB times. The REACTIVE pull of each tested fundamental stays within a
// real instrument's couple of cents (an early search maximised G by
// stacking antinode modes on one side of C4 and pulled it seven cents flat,
// which no measured Railsback shows). The exact values are [D]; the
// constraint set they satisfy is [M] -- which note lands on a peak or a dip
// is precisely the "real mobility fluctuation" the plan says owns the
// per-note variance.
// ---------------------------------------------------------------------------
// Second-order Butterworth high-pass: the radiation-efficiency collapse
// below the favoured band, shared by the board readout and the radiator's
// band-edge guard.
struct GrandRadiationHp
{
    void prepare (double fs, double fc)
    {
        const double w = std::tan (kPiD * fc / fs);
        const double n = 1.0 / (1.0 + std::sqrt (2.0) * w + w * w);
        b0 = n; b1 = -2.0 * n; b2 = n;
        a1 = 2.0 * n * (w * w - 1.0);
        a2 = n * (1.0 - std::sqrt (2.0) * w + w * w);
        z1 = z2 = 0.0;
    }
    double tick (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

class GrandBoard
{
public:
    static constexpr int kModes = 72;
    static constexpr int kThunkLen = 864;   // 18 ms at 48 kHz
    using System = SavModalSystem<kModes, 2>;

    // The band edge: above this the board is out of the loop entirely.
    static constexpr double kBandHz = 1300.0;

    // Engine contract: the engine builds one Config per block and calls
    // board.configure(cfg) when it changed; the radiator's modal tail stands
    // in for the SAME board above the band edge, so after configuring the
    // board the engine must forward the body to it as
    //     radiator.setBody (board.bodyFreqScale(), board.bodyEtaAdd());
    // (setBody keeps section states, so a live change does not click, and
    // setBody(1, 0) is bit-exactly the stock radiator).
    struct Config
    {
        // gV: the one global scalar the plan lets absorb the residual between
        // the a-priori mobility (Ege's 1.3e-3 s/kg) and the measured decay
        // knees. Mapped from the "bodyMix" control x0.5..2 by the engine.
        double couplingTrim = 1.0;
        // Index into kBodyMaterials; 0 = stock, the calibrated board exactly.
        double bodyMaterial = 0.0;
        // Uniform linear size scale, 0..1 with 0.5 = stock: s = 1.43^(2u-1),
        // i.e. s in [1/1.43, 1.43] ~ [0.70, 1.43] and s(0.5) = 1 exactly.
        double bodySize     = 0.5;
    };

    static_assert (std::is_trivially_copyable<Config>::value, "Config must be memcmp-able");
    static_assert (sizeof (Config) == 3 * sizeof (double), "Config has padding");

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        sys.prepare (sampleRate);
        buildLadder();
        configure (Config {});
        clear();
    }

    void clear() { sys.clear(); }

    void configure (const Config& cfg)
    {
        // Shape amplitude from the Skudrzyk identity: the frequency-mean of
        // the driving-point conductance of a modal bank with masses Mm and
        // density n is G = n <Phi^2> / (4 Mm). The plate shapes carry
        // <sin^2 sin^2> = 1/4, hence the factor 2 on the amplitude. Setting
        // G to Ege's measured 1.3e-3 s/kg (times the trim) fixes |Phi|; the
        // sign pattern and the per-note fluctuation then come from the plate
        // shapes, which is where the measured +/-10..15 dB envelope and the
        // per-note decay variance live.
        const double gV = std::clamp (cfg.couplingTrim, 0.05, 8.0);
        phiAmp = 2.0 * std::sqrt (4.0 * kModalMass * 1.3e-3 * gV / 0.058);

        // The thunk's attachment shape, once: an interior frame point away
        // from every bridge seat, so the thump excites the plate broadly
        // without impersonating any note.
        for (int m = 0; m < kModes; ++m)
            thunkPhi[m] = phiAmp * std::sin (modeP[m] * kPiD * 0.31)
                                 * std::sin (modeR[m] * kPiD * 0.21);

        // ---- the body: material and size, relative to the calibrated stock --
        // A uniform size scale s and a material against the stock spruce:
        //   - mode frequencies scale by freq/s (a plate's f goes as t/L^2,
        //     1/s with every dimension scaled; sqrt(E/rho) for the material),
        //   - modal masses by mass * s^3,
        //   - added internal loss on a rate basis, sigma += pi f etaAdd.
        // The driving-point mobility then scales by the infinite-plate law
        // WITHOUT a separate factor: the Skudrzyk mean G = n <Phi^2>/(4 Mm)
        // carries n ~ 1/freqScale and Mm ~ massScale, and
        //   1/(freqScale * massScale) = 1/((f/s) * rho * s^3)
        //                             = 1/(s^2 sqrt(E rho / (E rho)_stock)),
        // exactly Y ~ 1/(t^2 sqrt(E rho)). Folding it into phiAmp as well
        // would count it twice, and leaving phiAmp to couplingTrim alone
        // keeps the bodyMix knob's meaning unchanged at stock. All three
        // scalers are exactly {1, 1, 0} at index 0 / size 0.5: the stock
        // board is bit-identical by construction.
        const BodyScalers bs = bodyScalers (static_cast<int> (cfg.bodyMaterial));
        const double s = std::pow (1.43, 2.0 * std::clamp (cfg.bodySize, 0.0, 1.0) - 1.0);
        bodyFScale = bs.freq / s;
        bodyMScale = bs.mass * s * s * s;
        bodyEta    = bs.etaAdd;

        sys.setNumModes (kModes);
        for (int m = 0; m < kModes; ++m)
        {
            const double f = modeF[m] * bodyFScale;
            sys.setMode (m, f, 2.2 / ((kEta + bodyEta) * f), kModalMass * bodyMScale);
        }

        // The radiation collapse corner is set by the source's size against
        // the acoustic wavelength, so it moves with 1/s and is indifferent
        // to the material. Re-prepare (which zeroes the filter state) only
        // when the corner actually moved: at stock this never fires and the
        // readout chain stays bit-identical; on a live size change the two
        // biquads restart from silence once, at the moment everything else
        // about the board moves anyway.
        const double radFc = kRadFcHz / s;
        if (radFc != appliedRadFc)
        {
            hpL.prepare (fs, radFc);
            hpR.prepare (fs, radFc);
            appliedRadFc = radFc;
        }
    }

    // The body scalers the out-of-loop radiator tail must follow (see the
    // engine contract at Config). freqScale already contains the size.
    double bodyFreqScale() const { return bodyFScale; }
    double bodyEtaAdd()    const { return bodyEta; }

    // The board's mode amplitudes at one note's bridge point: 72 weights,
    // deterministic and continuous along the compass. The bridge runs on an
    // arc through the plate; bass notes sit near the wide end.
    void fillBridgeShape (double midiNote, double* phi) const
    {
        const double t = std::clamp ((midiNote - 21.0) / 87.0, 0.0, 1.0);
        const double x = 0.049 + 0.574 * t;
        const double y = 0.112 + 0.085 * t - 0.171 * t * (1.0 - t);
        for (int m = 0; m < kModes; ++m)
            phi[m] = phiAmp * std::sin (modeP[m] * kPiD * x)
                            * std::sin (modeR[m] * kPiD * y);
    }

    // The pedal thunk. The trapwork's end-of-travel thump enters the plate
    // through the frame beams -- a short low-frequency force at an interior
    // point, not at any bridge seat. The exact attachment is not a
    // measurable quantity; the point is fixed, the LEVEL is calibrated
    // (engine row 12.2 holds it against an mf note), and the spectrum falls
    // out of the plate's own low modes. A raised-cosine force over 18 ms:
    // no step at either end, nothing above the pulse's own bandwidth.
    void pedalThunk (double strength)
    {
        thunkAmp = strength;
        thunkPos = 0;
    }

    // The action's own noise, arriving the same way the trapwork's thump
    // does: through the frame, at a point that is not any note's bridge
    // seat. Key-bottom, hammer return and damper lift are all mechanism
    // against wood, and on a grand they are loud -- a real instrument is
    // audibly a machine. Instantaneous rather than shaped: the shaping is
    // the ActionNoise layer's own, this is only where it enters the plate.
    void frameForce (double f)
    {
        double* d = sys.driveData();
        for (int m = 0; m < kModes; ++m) d[m] += thunkPhi[m] * f;
    }

    // ---- the two-port, board side ---------------------------------------
    // Read the bridge displacement under a note (before anyone ticks), and
    // accumulate the note's net force through the same shape. The voice owns
    // the string side and the force law; see GrandVoice.
    double bridgeDisplacement (const double* phi) const
    {
        // Four independent accumulators: a single-accumulator reduction
        // serialises on the FMA latency chain (this dot runs once per voice
        // per sample, close to ninety times a sample with the pedal down,
        // and was the grand's largest remaining line). Deterministic -- the
        // association is fixed by hand, no fast-math involved.
        const double* q = sys.displacementData();
        double u0 = 0.0, u1 = 0.0, u2 = 0.0, u3 = 0.0;
        double u4 = 0.0, u5 = 0.0, u6 = 0.0, u7 = 0.0;
        static_assert (kModes % 8 == 0);
        for (int m = 0; m < kModes; m += 8)
        {
            u0 += phi[m]     * q[m];
            u1 += phi[m + 1] * q[m + 1];
            u2 += phi[m + 2] * q[m + 2];
            u3 += phi[m + 3] * q[m + 3];
            u4 += phi[m + 4] * q[m + 4];
            u5 += phi[m + 5] * q[m + 5];
            u6 += phi[m + 6] * q[m + 6];
            u7 += phi[m + 7] * q[m + 7];
        }
        return ((u0 + u1) + (u2 + u3)) + ((u4 + u5) + (u6 + u7));
    }

    void addBridgeForce (const double* phi, double force)
    {
        double* d = sys.driveData();
        for (int m = 0; m < kModes; ++m) d[m] += phi[m] * force;
    }

    // Once per engine sample, after every voice has exchanged. The stereo
    // readout is computed here so the radiation high-pass (below) carries
    // its state sample-synchronously.
    void tick()
    {
        if (thunkPos < kThunkLen)
        {
            const double t = thunkPos / double (kThunkLen);
            const double f = thunkAmp * 0.5 * (1.0 - std::cos (2.0 * kPiD * t));
            double* d = sys.driveData();
            for (int m2 = 0; m2 < kModes; ++m2) d[m2] += thunkPhi[m2] * f;
            ++thunkPos;
        }
        sys.tick();
        outL = hpL.tick (sys.velocityAt (listenL));
        outR = hpR.tick (sys.velocityAt (listenR));
    }

    // What a listener hears of the low band: the board's velocity through two
    // readout vectors, one per channel of the reference mic pair. Three
    // physical facts are folded in, each measured:
    //
    //   - RADIATION EFFICIENCY COLLAPSE below ~200 Hz (Wogram's favoured
    //     200-2000 Hz band; F&R: bass notes are carried by their upper
    //     partials). Amplitude falls as f^2/(f^2+fc^2) -- the second-order
    //     rise of a source small against the wavelength -- applied as an
    //     output high-pass at kRadFcHz because what matters is the
    //     frequency of VIBRATION: a first cut weighted each MODE by its own
    //     natural frequency, and a 27.5 Hz partial then escaped the
    //     collapse entirely by riding the 75 Hz modes' quasi-static skirts.
    //     The low bass fundamentals all but vanish from the RADIATED sound
    //     while still ringing on the string (rows S1, T1, W4-C3).
    //   - Each channel mixes a COMMON listening point with a SIDE point of
    //     its own (bass side left, treble side right, the player's image):
    //     the side responses pick up the modal sign scatter between the two
    //     positions. The frequency-dependent interchannel PHASE of a real
    //     mic pair -- each region of the board at its own distance and angle
    //     from each mic -- is GrandMicPair's job (GrandRadiator.h), applied
    //     to the summed radiated pair.
    //   - A note's OWN bridge point lies nearer the bass side point at the
    //     bass end of the arc and nearer the treble one at the top, so the
    //     board's share of the channel level difference tracks register
    //     through shared-mode correlation; the radiator's per-note pan
    //     (GrandRadiator::panGains) carries the rest of the measured
    //     +/-3..7 dB ILD line.
    double outputL() const { return outL; }
    double outputR() const { return outR; }

    // The radiation collapse corner. Below it the radiated amplitude falls
    // as (f/fc)^2 -- the second-order rise of a source small against the
    // wavelength -- applied as an output high-pass because what matters is
    // the frequency of VIBRATION: a 27.5 Hz string partial forcing a 75 Hz
    // board mode quasi-statically still radiates like 27.5 Hz.
    static constexpr double kRadFcHz = 200.0;

    double energy() const { return sys.energy(); }
    double modeFrequency (int m) const { return (m >= 0 && m < kModes) ? modeF[m] * bodyFScale : 0.0; }

    // Driving-point receptance u/F at a bridge point, for the voice's tuning
    // pass: the coupled fundamental is pulled by the bridge reactance, and a
    // real piano is tuned AFTER stringing -- the tuner sets the sounding
    // pitch, absorbing the pull. The voice pre-compensates the same way.
    void receptance (const double* phi, double f, double& re, double& im) const
    {
        const double w = 2.0 * kPiD * f;
        double hr = 0.0, hi = 0.0;
        for (int m = 0; m < kModes; ++m)
        {
            const double wm = 2.0 * kPiD * (modeF[m] * bodyFScale);
            const double a = wm * wm - w * w;
            const double b = (kEta + bodyEta) * wm * w;
            const double den = (kModalMass * bodyMScale) * (a * a + b * b);
            hr += phi[m] * phi[m] * a / den;
            hi -= phi[m] * phi[m] * b / den;
        }
        re = hr;
        im = hi;
    }


private:
    static constexpr double kModalMass = 2.25;   // kg, M/4 of Ege's 9 kg board
    static constexpr double kEta = 0.02;         // measured loss factor
    static constexpr double kListenScale = 0.18;
    static constexpr double kMidMix  = 0.62;     // common component
    static constexpr double kSideMix = 0.50;     // per-channel side component

    void buildLadder()
    {
        // Enumerate (p, r), keep the lowest 72 by frequency.
        struct E { double f; int p, r; };
        E e[kModes * 4];
        int n = 0;
        for (int p = 1; p <= 40 && n < kModes * 4; ++p)
            for (int r = 1; r <= 8 && n < kModes * 4; ++r)
            {
                const double f = 2.318 * p * p + 74.73 * r * r;
                if (f < 1600.0) e[n++] = { f, p, r };
            }
        std::sort (e, e + n, [] (const E& a, const E& b) { return a.f < b.f; });
        for (int m = 0; m < kModes; ++m)
        {
            modeF[m] = e[m].f;
            modeP[m] = e[m].p;
            modeR[m] = e[m].r;
        }
        // The mic pair: a common point plus one side point per channel, all
        // off the bridge arc and clear of low-mode nodes. The side points sit
        // over the bass and treble ends of the arc so a note's register reads
        // as level difference; the common:side ratio sets the mid-band
        // coherence at the measured 0.5..0.65. The scale folds the
        // board-velocity-to-output level into a number that lands the low
        // band against the radiator tail.
        auto shapeAt = [this] (int m, double x, double y)
        {
            return std::sin (modeP[m] * kPiD * x) * std::sin (modeR[m] * kPiD * y);
        };
        for (int m = 0; m < kModes; ++m)
        {
            const double mid = shapeAt (m, 0.33, 0.40);
            const double bassSide = shapeAt (m, 0.13, 0.32);
            const double trebSide = shapeAt (m, 0.55, 0.48);
            listenL[m] = kListenScale * (kMidMix * mid + kSideMix * bassSide);
            listenR[m] = kListenScale * (kMidMix * mid + kSideMix * trebSide);
        }
        hpL.prepare (fs, kRadFcHz);
        hpR.prepare (fs, kRadFcHz);
        appliedRadFc = kRadFcHz;
        outL = outR = 0.0;
    }

    double fs = 48000.0;
    double phiAmp = 0.0;
    double thunkPhi[kModes] {};
    double thunkAmp = 0.0;
    int    thunkPos = kThunkLen;
    double bodyFScale = 1.0, bodyMScale = 1.0, bodyEta = 0.0;
    double appliedRadFc = kRadFcHz;
    double modeF[kModes] {};
    int    modeP[kModes] {}, modeR[kModes] {};
    double listenL[kModes] {}, listenR[kModes] {};
    GrandRadiationHp hpL, hpR;
    double outL = 0.0, outR = 0.0;
    System sys;
};

} // namespace epi
