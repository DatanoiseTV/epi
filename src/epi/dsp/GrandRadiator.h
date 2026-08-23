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

#include "GrandBoard.h"
#include "ModalCore.h"

namespace epi
{

// ---------------------------------------------------------------------------
// The soundboard's tail: everything the strings radiate above the modal
// board's 1.3 kHz band edge, per docs/grand-implementation-plan.md section 4.
//
// Above ~1.1 kHz the measured board stops being a plate: the ribs confine
// half-wavelengths, the mobility becomes a featureless mean (Ege), and a
// string partial up there sees pure loss -- which is already folded into the
// string T60s as the measured grand-minus-CP80 drain. What is left to model
// is the TRANSDUCTION: bridge force in, radiated sound out, with no feedback.
// Bank's "fully modal" radiator option does exactly this shape -- a fixed
// parallel bank of second-order sections, poles on a log grid, damping from
// the board's own loss factor -- and it is zero-latency and in-idiom (the
// same resonator arithmetic as everything else, outside the loop, so it
// cannot affect stability at all).
//
// Numbers:
//   - 128 sections per channel, log grid 1.2..15 kHz. At eta = 2% each
//     section's -3 dB width is ~0.02 f and the log grid spacing is ~0.02 f:
//     the bank tiles the band, which is what "featureless mean with dense
//     overlap" means concretely.
//   - Section output amplitudes follow the measured mean-mobility fall above
//     the band edge (impedance rising toward the treble step) as a gentle
//     (f_edge/f)^0.5, with a deterministic per-section +/- scatter split
//     anti-symmetrically between L and R: that scatter is what decorrelates
//     the channels. With gL = a(1+s h), gR = a(1-s h), h = +/-1, the
//     band-averaged interchannel coherence is (1-s^2)/(1+s^2); s = 0.5 lands
//     the measured 0.5..0.65 above 200 Hz.
//   - Stereo image: the tail is radiated from near the note's own bridge
//     point, so the feed is panned per note along the measured ILD line
//     (Salamander mic pair: A1 +3.2 dB toward L, C5 -4.7 dB toward R).
//     Two independently-driven banks make that per-note pan possible; the
//     pole grid is shared.
// ---------------------------------------------------------------------------
class GrandRadiator
{
public:
    static constexpr int kSections = 128;
    static constexpr double kLoHz = 1200.0;
    static constexpr double kHiHz = 15000.0;

    // Second-order allpass, unit magnitude everywhere; the building block of
    // the direct branch's dispersion and the mic pair's interchannel phase.
    struct Ap2
    {
        // H(z) = (r^2 - 2 r cos t z^-1 + z^-2) / (1 - 2 r cos t z^-1 + r^2 z^-2)
        double a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
        void set (double fs, double fc, double bwFrac)
        {
            fc = std::min (fc, 0.45 * fs);
            const double r = std::exp (-kPiD * bwFrac * fc / fs);
            a1 = -2.0 * r * std::cos (2.0 * kPiD * fc / fs);
            a2 = r * r;
            z1 = z2 = 0.0;
        }
        double tick (double x)
        {
            const double y = a2 * x + z1;
            z1 = a1 * x - a1 * y + z2;
            z2 = x - a2 * y;
            return y;
        }
    };

    // Second-order Butterworth low-pass, the direct branch's band limit.
    struct Lp2
    {
        void prepare (double fs, double fc)
        {
            const double w = std::tan (kPiD * fc / fs);
            const double n = 1.0 / (1.0 + std::sqrt (2.0) * w + w * w);
            b0 = n * w * w; b1 = 2.0 * b0; b2 = b0;
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

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        buildSections (1.0, 0.0);
        clear();
    }

    // The tail stands in for the SAME board above the band edge, so it must
    // follow the board's body: called by the engine with the board's own
    // scalers, radiator.setBody(board.bodyFreqScale(), board.bodyEtaAdd()),
    // whenever the board's Config changed (see GrandBoard::Config). Section
    // frequencies scale, damping takes the added internal loss on the same
    // rate basis, and each section's peak normalisation and mean-mobility
    // gain are recomputed for the pole it actually lands on. Section STATES
    // are kept, so a live change does not click; at (1, 0) every coefficient
    // recomputes to its stock value bit-exactly.
    void setBody (double freqScale, double etaAdd)
    {
        buildSections (freqScale, etaAdd);
    }

private:
    void buildSections (double freqScale, double etaAdd)
    {
        const double top = std::min (kHiHz, 0.45 * fs);
        auto hash01 = [] (unsigned v)
        {
            return ((v * 2654435761u) >> 8 & 65535u) / 65535.0;
        };
        // The R channel's grid sits half a step off the L channel's: no two
        // mic positions see the same peak structure, and up where the note
        // partials are denser than the grid this is most of the measured
        // interchannel decorrelation.
        for (int ch = 0; ch < 2; ++ch)
        {
            double* pa1 = ch == 0 ? a1L : a1R;
            double* pa2 = ch == 0 ? a2L : a2R;
            double* pg  = ch == 0 ? gL : gR;
            for (int i = 0; i < kSections; ++i)
            {
                const double t = (i + 0.5 + 0.5 * ch) / kSections;
                const double f = kLoHz * std::pow (top / kLoHz, t);
                // The body-scaled pole. A small stiff body can push the top
                // of the grid past what the sample rate carries; those
                // sections saturate at the ceiling rather than alias.
                const double fp = std::min (f * freqScale, 0.45 * fs);
                const double th = 2.0 * kPiD * fp / fs;
                const double r  = std::exp (-kPiD * (kEta + etaAdd) * fp / fs);
                pa1[i] = 2.0 * r * std::cos (th);
                pa2[i] = -r * r;
                // Peak-normalise: |1 - 2 r cos(th) e^{-jth} + r^2 e^{-2jth}|.
                const double c1 = std::cos (th), s1 = std::sin (th);
                const double c2 = std::cos (2.0 * th), s2 = std::sin (2.0 * th);
                const double re = 1.0 - 2.0 * r * c1 * c1 + r * r * c2;
                const double im = 2.0 * r * c1 * s1 - r * r * s2;
                const double norm = std::sqrt (re * re + im * im);

                // Mean-mobility fall above the band edge, and a deterministic
                // per-section amplitude scatter split anti-symmetrically
                // between the channels: the modal sign pattern the mics see.
                // The frequency-dependent interchannel phase lives in
                // GrandMicPair.
                // The fall is anchored to the grid position f, not the moved
                // pole fp: the band edge the tail hangs off moves with the
                // same body, so section i keeps its level relation to it.
                const double amp = norm * kScale * std::sqrt (GrandBoard::kBandHz / f);
                const double h = 2.0 * hash01 (static_cast<unsigned> (i) * 4u + 1u) - 1.0;
                pg[i] = amp * (1.0 + (ch == 0 ? kSideAmt : -kSideAmt) * h);
            }
        }
    }

public:
    void clear()
    {
        for (int i = 0; i < kSections; ++i)
            yL1[i] = yL2[i] = yR1[i] = yR2[i] = 0.0;
        inL = inR = 0.0;
        hpInL.prepare (fs, kInHpHz);
        hpInR.prepare (fs, kInHpHz);
        dirLpL.prepare (fs, kInHpHz);
        dirLpR.prepare (fs, kInHpHz);
        dirHpL.prepare (fs, GrandBoard::kRadFcHz);
        dirHpR.prepare (fs, GrandBoard::kRadFcHz);
        // The direct branch's dispersion: the transduction from bridge force
        // to sound at a microphone is anything but phase-linear (Bank's
        // fitted radiators are tens of thousands of taps long), and without
        // it the phase-aligned attack crest of the termination force -- a
        // crest the radiated recordings do not show -- rides straight into
        // the output and becomes the envelope's reference peak. A cascade of
        // allpasses over the low band smears the crest by the few tens of
        // milliseconds a real board-plus-room path does, and cannot touch
        // levels or decays.
        // The chains differ slightly between the channels (centres 13%
        // apart): the two mics' paths disperse differently, which is a good
        // part of the measured mid-band decorrelation, while below the
        // lowest centre both are phase-flat and the low band stays common.
        for (int i = 0; i < kDispStages; ++i)
        {
            const double fc = 150.0 * std::pow (1.62, i);
            dispL[i].set (fs, fc * 1.00, 0.30);
            dispR[i].set (fs, fc * 1.13, 0.30);
        }
    }

    // The measured ILD line: bass toward the left of the pair, treble to the
    // right (A1 +3.2 dB, C5 -4.7 dB), clamped inside the +/-3..7 dB band.
    static void panGains (int midiNote, double& l, double& r)
    {
        const double t = std::clamp ((midiNote - 21.0) / 87.0, 0.0, 1.0);
        const double ildDb = std::clamp (5.6 - 17.6 * t, -7.0, 7.0);
        const double g = std::pow (10.0, ildDb / 40.0);   // half on each side
        l = g;
        r = 1.0 / g;
    }

    // Accumulate one note's full-band termination force, panned.
    void push (double force, double gPanL, double gPanR)
    {
        inL += force * gPanL;
        inR += force * gPanR;
    }

    // Once per engine sample, after every push.
    void tick (double& outL, double& outR)
    {
        // The band-edge guard: the tail owns >1.3 kHz only, but 128 section
        // skirts summed in phase pass real bass -- measured, an A0
        // fundamental leaking through at a level that defeated the board's
        // radiation collapse. Second-order high-pass on each input.
        const double xl = hpInL.tick (inL);
        const double xr = hpInR.tick (inR);
        // The DIRECT low branch: Bank's proven shape -- the radiated low
        // band follows the bridge-force spectrum through a smooth mean
        // transduction, on top of which the modal board readout adds the
        // dip/peak character and the knock. Without it a partial whose
        // bridge point sits in a mobility dip all but vanishes from the mix
        // (measured: A3's slow fundamental radiating 21 dB below its own
        // second partial, which no close-mic recording shows -- the mean
        // mobility the dip is a fluctuation AROUND still transduces).
        // Same band limit as the tail's guard (the tail owns the rest),
        // same radiation collapse as the board readout.
        double dl = kDirect * dirHpL.tick (dirLpL.tick (inL));
        double dr = kDirect * dirHpR.tick (dirLpR.tick (inR));
        for (int i = 0; i < kDispStages; ++i)
        {
            dl = dispL[i].tick (dl);
            dr = dispR[i].tick (dr);
        }
        double sl = dl, sr = dr;
        for (int i = 0; i < kSections; ++i)
        {
            const double l = a1L[i] * yL1[i] + a2L[i] * yL2[i] + xl;
            yL2[i] = yL1[i]; yL1[i] = l;
            sl += gL[i] * l;
            const double r = a1R[i] * yR1[i] + a2R[i] * yR2[i] + xr;
            yR2[i] = yR1[i]; yR1[i] = r;
            sr += gR[i] * r;
        }
        inL = inR = 0.0;
        outL = sl;
        outR = sr;
    }

private:
    static constexpr double kEta = 0.02;      // the board's own loss factor
    static constexpr double kSideAmt = 0.6;   // amplitude scatter, anti-symmetric
    static constexpr double kScale = 0.12;     // level against the board band
    static constexpr double kInHpHz = 1300.0; // band-edge guard on the input
    static constexpr double kDirect = 0.060;  // direct low-branch transduction

    double fs = 48000.0;
    double a1L[kSections] {}, a2L[kSections] {};
    double a1R[kSections] {}, a2R[kSections] {};
    double gL[kSections] {}, gR[kSections] {};
    GrandRadiationHp hpInL, hpInR;
    GrandRadiationHp dirHpL, dirHpR;
    Lp2 dirLpL, dirLpR;
    static constexpr int kDispStages = 6;
    Ap2 dispL[kDispStages], dispR[kDispStages];
    double yL1[kSections] {}, yL2[kSections] {};
    double yR1[kSections] {}, yR2[kSections] {};
    double inL = 0.0, inR = 0.0;
};

// ---------------------------------------------------------------------------
// The mic pair's interchannel phase. Each region of the board sits at its
// own distance and angle from each microphone, so the two channels of a real
// piano recording carry frequency-dependent relative phase -- which is most
// of what the measured band coherence (0.75..0.8 below 200 Hz, 0.5..0.65
// above) actually is. Realised as one cascade of second-order allpasses per
// channel with interleaved centre frequencies (constant-Q, so the phase
// transitions stay proportionate across the log axis): unit magnitude by
// construction, so it cannot touch levels, decays or ILD. Below the lowest
// centre both cascades are phase-flat and the low band stays coherent, with
// no separate mechanism. Applied to the summed radiated pair (board + tail);
// an allpass is LTI, so summing first changes nothing.
// ---------------------------------------------------------------------------
class GrandMicPair
{
public:
    static constexpr int kStages = 6;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int i = 0; i < kStages; ++i)
        {
            const double fL = kBaseHz * std::pow (kStep, i);
            const double fR = fL * std::pow (kStep, 0.45);
            set (stL[i], fL);
            set (stR[i], fR);
        }
    }

    void clear()
    {
        for (auto& a : stL) a.z1 = a.z2 = 0.0;
        for (auto& a : stR) a.z1 = a.z2 = 0.0;
    }

    // A wider pair decorrelates from lower down: the cascade's base scales
    // inversely with the spread. Unity is the calibrated pair.
    void setSpread (double spread)
    {
        const double s = std::clamp (spread, 0.25, 2.0);
        for (int i = 0; i < kStages; ++i)
        {
            const double fL = (kBaseHz / s) * std::pow (kStep, i);
            const double fR = fL * std::pow (kStep, 0.45);
            set (stL[i], fL);
            set (stR[i], fR);
        }
    }

    void tick (double& l, double& r)
    {
        for (auto& a : stL) l = a.tick (l);
        for (auto& a : stR) r = a.tick (r);
    }

private:
    static constexpr double kBaseHz = 300.0;
    static constexpr double kStep = 2.1;
    static constexpr double kBwFrac = 0.35;   // bandwidth as a fraction of fc

    void set (GrandRadiator::Ap2& a, double fc) { a.set (fs, fc, kBwFrac); }

    double fs = 48000.0;
    GrandRadiator::Ap2 stL[kStages], stR[kStages];
};

} // namespace epi
