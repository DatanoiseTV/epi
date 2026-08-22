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

#include "ModalCore.h"

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
    using System = SavModalSystem<kModes, 2>;

    // The band edge: above this the board is out of the loop entirely.
    static constexpr double kBandHz = 1300.0;

    struct Config
    {
        // gV: the one global scalar the plan lets absorb the residual between
        // the a-priori mobility (Ege's 1.3e-3 s/kg) and the measured decay
        // knees. Mapped from the "bodyMix" control x0.5..2 by the engine.
        double couplingTrim = 1.0;
    };

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
        sys.setNumModes (kModes);
        for (int m = 0; m < kModes; ++m)
            sys.setMode (m, modeF[m], 2.2 / (kEta * modeF[m]), kModalMass);
    }

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

    // ---- the two-port, board side ---------------------------------------
    // Read the bridge displacement under a note (before anyone ticks), and
    // accumulate the note's net force through the same shape. The voice owns
    // the string side and the force law; see GrandVoice.
    double bridgeDisplacement (const double* phi) const
    {
        double u = 0.0;
        for (int m = 0; m < kModes; ++m) u += phi[m] * sys.displacement (m);
        return u;
    }

    void addBridgeForce (const double* phi, double force)
    {
        for (int m = 0; m < kModes; ++m) sys.addForce (m, phi[m] * force);
    }

    // Once per engine sample, after every voice has exchanged. The stereo
    // readout is computed here so the radiation high-pass (below) carries
    // its state sample-synchronously.
    void tick()
    {
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
    double modeFrequency (int m) const { return (m >= 0 && m < kModes) ? modeF[m] : 0.0; }

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
            const double wm = 2.0 * kPiD * modeF[m];
            const double a = wm * wm - w * w;
            const double b = kEta * wm * w;
            const double den = kModalMass * (a * a + b * b);
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
        outL = outR = 0.0;
    }

    double fs = 48000.0;
    double phiAmp = 0.0;
    double modeF[kModes] {};
    int    modeP[kModes] {}, modeR[kModes] {};
    double listenL[kModes] {}, listenR[kModes] {};
    GrandRadiationHp hpL, hpR;
    double outL = 0.0, outR = 0.0;
    System sys;
};

} // namespace epi
