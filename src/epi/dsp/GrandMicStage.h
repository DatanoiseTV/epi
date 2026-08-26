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

#include "GrandRadiator.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace epi
{

// ---------------------------------------------------------------------------
// The grand's microphone stage: up to five freely positionable virtual mics
// around the instrument, or the shipped calibrated pair.
//
// Mode 0 (Classic Pair) IS the shipped chain, byte for byte: board readout
// plus radiator summed per channel, then GrandMicPair's pure-allpass
// interchannel phase. Mode 1 (Stage) replaces that fixed sampling of the
// field with real geometry. The sources are the ones the engine already
// computes -- the radiator's sections, each sitting at the bridge position
// its own scatter encodes (GrandRadiator::slotX and the note at the top of
// that header), and the sub-1.3 kHz low branch (modal board readout plus the
// radiator's direct branch) collapsed to one extended source at mid-bridge,
// because its wavelengths are of the order of the bridge itself and no mic
// position resolves where along it the force entered.
//
// Per mic, per source, the free-field pair every acoustics text gives:
//   - amplitude 1/r, referenced to the calibrated pair's notional seat --
//     kSeatZ = 1.2 m out from the rim and kSeatH = 0.6 m above the board, so
//     the reference radius is seatR() = 1.34 m -- and every path is gauged to
//     unity there, so the default mic lands at the calibrated level;
//   - arrival delay r/c, c = 343 m/s (dry air, 20 C), through a shared
//     fractional-delay bus per source -- the interchannel phase that
//     GrandMicPair fakes with allpasses in mode 0 falls out of the delay
//     DIFFERENCES between mics here, which is the physically honest version
//     of the same decorrelation, so the allpass cascade is mode 0 only.
//
// Directivity. A soundboard in its case is a baffled plate. Below
// coincidence (f_c = c^2 / (1.8 c_L t) ~ 343^2 / (1.8 * 5000 * 0.009)
// ~ 1.45 kHz for 9 mm spruce, c_L ~ 5000 m/s -- conveniently at the
// engine's own 1.3 kHz band split) the bending waves are acoustically slow
// and the board radiates as a dipole about its own plane: pressure follows
// cos(theta) = h/r and INVERTS under the board, which is why a mic slid
// below the rim hears the low branch in opposite polarity. A small monopole
// leak (kMonoLeak) stands in for the case volume and rim gap that keep a
// real grand from having a perfect null in the board plane. The gain is
// gauge-fixed to 1 at the calibrated pair's seat so the default Stage mic
// keeps the calibrated tonal balance. Above coincidence the radiation goes
// hemispherical off the open face -- the section band therefore carries no
// dipole sign, only 1/r and delay.
//
// The lid: a raised lid is a large, nearly specular reflector at the
// section band's wavelengths (< 26 cm against a ~1.4 m panel), so it is
// modelled the way image theory says: ONE image of the high-band ensemble,
// above the board and toward the open (treble) side, with reflection
// coefficient kLidRefl and its own r/c delay. A mic on the open side gets
// direct plus the slightly-delayed lid image -- the few-dB 2-6 kHz lift
// that makes the classic jazz position bright -- while the mirrored
// closed-side position gets almost none of it. One image for the whole
// ensemble, not one per section: the lid is large against the section
// spacing, so all the images sit within centimetres of each other.
//
// Near field. The dipole above is only its FAR field. The complete dipole
// pressure at radius r is
//
//     p(r) = (A cos(theta) / r) (1 + 1/(j k r)) e^{-j k r},   k = 2 pi f / c
//
// and the bracket -- the term every far-field treatment drops -- is what a
// close microphone lives inside. Read against frequency rather than against
// distance, the bracket IS a first-order filter, exactly:
//
//     N(s) = (s + c/r) / s
//
// with one zero at the near-field corner f_nf = c / (2 pi r) (the frequency
// where k r = 1) and one pole at DC. Unity above the corner; below it, a
// rise of 6 dB/octave as the frequency falls, and a phase that runs from 0
// to -90 degrees, both of them exact, not fitted. The corner is a pure
// reading of distance -- 273 Hz at the closest mic the stage allows
// (kMinR = 0.2 m), 40.7 Hz at the calibrated seat's 1.34 m, 18.2 Hz at 3 m
// -- so a mic hears the near field of exactly those notes whose wavelengths
// dwarf its own distance, and of nothing else.
//
// What that does to THIS instrument follows from the board's own radiation
// collapse. Below GrandBoard::kRadFcHz = 200 Hz the model rolls the
// radiated amplitude off at 12 dB/octave, which is the far-field law of a
// source small against the wavelength -- for the dipole the board is below
// coincidence, the same f^2 rise. Multiply the two: below BOTH corners the
// pressure at a close mic falls at 12 - 6 = 6 dB/octave, not 12. And the
// distance law steepens with it, because 1/r times 1/(k r) is 1/r^2: deep
// in the near field a halved distance is worth 12 dB, not 6.
//
// A0's fundamental at 27.5 Hz sits 2.9 octaves under the collapse and, at
// the seat, 0.57 of an octave under the near-field corner. The Classic pair
// carries none of this, because the Classic pair has no distance in it at
// all -- it is a fixed gain law, and a near field is a function of k r.
// That is the named half of the grand suite's row S1 (an A0 fundamental
// 35 dB under its strongest partial where the close-mic'd reference reads
// -20 .. -30): not a tuning error, a missing mechanism, and one that only a
// path with real geometry can hold.
//
// Three things the near-field term does NOT touch, each for a reason:
//   - the monopole leak (kMonoLeak). A monopole's pressure is A/r at EVERY
//     k r -- its near field is entirely in the phase, and the propagation
//     phase is already the delay bus. So the low path splits at the source:
//     the cos(theta) dipole share goes through N, the leak share does not.
//     A mic in the board plane (h = 0), where the dipole share is
//     identically zero, therefore measures the same pure 1/r it measured
//     before any of this -- which is the geometry rows MS2 and MS3 stand on.
//   - the section and lid buses. Above coincidence the radiation is
//     monopole-ish off the open face, and in any case k r >= 4.8 at the
//     1.3 kHz band edge for the closest permitted mic: |N| is +0.18 dB
//     there and falls as 1/f.
//   - the far field itself. |N| -> 1 as k r -> infinity exactly, so nothing
//     above a mic's own corner moves: at 3 m the whole band above 100 Hz is
//     within 0.15 dB of the pure 1/r it had before.
//
// The DC pole is honest physics -- a dipole's near field is an
// incompressible flow field, which does not vanish at zero frequency -- but
// a bad digital filter, so it is placed at kNfFloorHz instead of at zero.
// The gauge does not move: seatR / r is still the level reference, so the
// seat mic still reads the calibrated chain wherever N is unity, which is
// everything above ~100 Hz. What it now additionally reads, below that, is
// the seat's own near field.
//
// Air: ISO 9613-1 absorption at 20 C, 50% RH is ~0.11 dB/m at 10 kHz,
// growing ~f^2 below the relaxation peak. One one-pole per mic, cutoff
// placed where the accumulated loss over that mic's distance reaches 3 dB:
// fc = 10 kHz * sqrt(3 / (0.11 r)). At r <= 5 m that is at or above the
// audio band -- air is nearly transparent at mic distances, and the filter
// says so honestly instead of faking "distance = dark".
//
// Every parameter change (setMic, setMode) is picked up on the audio thread
// and ridden in over a short raised-cosine crossfade between the previous
// and the new rendering, so nothing clicks mid-note. Changes arriving while
// a fade runs coalesce and are picked up when it completes.
// ---------------------------------------------------------------------------
class GrandMicStage
{
public:
    static constexpr int kMaxMics = 5;

    struct Mic
    {
        bool   on      = false;
        double x       = 0.0;   // metres along the bridge axis, -2..2, 0 = mid-bridge
        double z       = 1.2;   // metres out horizontally from the rim, 0.2..5
        double h       = 0.6;   // metres above the soundboard plane, -1..2 (negative = under)
        double gainDb  = 0.0;   // -24..+12
        double pan     = 0.0;   // -1..1 into the stereo mix
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        pair.prepare (fs);
        fadeLen = std::max (1, static_cast<int> (fs * kFadeS));
        // Ring size: power of two covering the longest possible path
        // (largest mic box corner plus the lid image, ~8 m -> 24 ms) with
        // headroom. Only reallocates when the size actually changes, so the
        // engine's audio-thread reset paths stay allocation-free.
        int n = 1;
        while (n < static_cast<int> (fs * kMaxDelayS) + 4) n <<= 1;
        if (n != ringN)
        {
            ringN = n;
            ring.assign (static_cast<std::size_t> (kBuses)
                             * static_cast<std::size_t> (ringN), 0.0);
        }
        clear();
        rebuildAll();
    }

    void clear()
    {
        std::fill (ring.begin(), ring.end(), 0.0);
        wp = 0;
        busAge = 0;
        pair.clear();
        for (auto& t : setA) t.airY = 0.0;
        for (auto& t : setB) t.airY = 0.0;
        fadePos = fadeLen;   // steady
    }

    // 0 = Classic Pair (the shipped chain), 1 = Stage.
    void setMode (int m)
    {
        pendMode.store (m == 1 ? 1 : 0, std::memory_order_relaxed);
        dirty.store (true, std::memory_order_release);
    }

    // Click-safe: the change rides a short crossfade on the audio thread.
    void setMic (int idx, const Mic& m)
    {
        if (idx < 0 || idx >= kMaxMics) return;
        auto& p = pend[static_cast<std::size_t> (idx)];
        p.on.store (m.on, std::memory_order_relaxed);
        p.x.store (std::clamp (m.x, -2.0, 2.0), std::memory_order_relaxed);
        p.z.store (std::clamp (m.z, 0.2, 5.0), std::memory_order_relaxed);
        p.h.store (std::clamp (m.h, -1.0, 2.0), std::memory_order_relaxed);
        p.gainDb.store (std::clamp (m.gainDb, -24.0, 12.0), std::memory_order_relaxed);
        p.pan.store (std::clamp (m.pan, -1.0, 1.0), std::memory_order_relaxed);
        dirty.store (true, std::memory_order_release);
    }

    // The classic pair's spread control (mode 0 only, by construction: in
    // Stage mode the interchannel phase is the mics' real delay geometry).
    void setSpread (double spread) { pair.setSpread (spread); }

    // Once per engine sample, after every push into the radiator and after
    // board.tick(). The stage owns the radiator's per-sample readout so the
    // right variant (classic sums / spatial buses / both during a mode
    // crossfade) runs on ONE state advance. In mode 0 the body of this
    // function is exactly the shipped seam:
    //   rad.tick(tl, tr); out = board + t; pair.tick(out).
    void tick (GrandRadiator& rad, double boardL, double boardR,
               double& outL, double& outR)
    {
        if (fadePos >= fadeLen && dirty.exchange (false, std::memory_order_acq_rel))
            pickup();

        const bool fading = fadePos < fadeLen;
        if (! fading)
        {
            if (mode == 0)
            {
                double tl = 0.0, tr = 0.0;
                rad.tick (tl, tr);
                double l = boardL + tl;
                double r = boardR + tr;
                pair.tick (l, r);
                outL = l;
                outR = r;
                return;
            }
            double slots[GrandRadiator::kStageSlots];
            double low = 0.0;
            rad.tickStage (slots, low);
            low += 0.5 * (boardL + boardR);
            writeBuses (slots, low);
            double l = 0.0, r = 0.0;
            renderSet (setA, l, r);
            outL = l;
            outR = r;
            return;
        }

        // Mode or mic crossfade: both configurations rendered from one
        // radiator state advance, mixed by a raised cosine.
        double slots[GrandRadiator::kStageSlots];
        double low = 0.0, cl = 0.0, cr = 0.0;
        rad.tickDual (cl, cr, slots, low);
        low += 0.5 * (boardL + boardR);
        writeBuses (slots, low);
        double classicL = boardL + cl;
        double classicR = boardR + cr;
        pair.tick (classicL, classicR);

        double aL = 0.0, aR = 0.0, bL = 0.0, bR = 0.0;
        if (modeA == 0) { aL = classicL; aR = classicR; }
        else            renderSet (setA, aL, aR);
        if (modeB == 0) { bL = classicL; bR = classicR; }
        else            renderSet (setB, bL, bR);

        ++fadePos;
        const double w = 0.5 - 0.5 * std::cos (kPiD * fadePos / fadeLen);
        outL = bL + w * (aL - bL);
        outR = bR + w * (aR - bR);
        if (fadePos >= fadeLen)
            mode = modeA;
    }

private:
    // ---- geometry constants ------------------------------------------------
    static constexpr double kSpeedOfSound = 343.0;  // m/s, dry air at 20 C
    // The calibrated pair's notional seat: every path's gain is referenced
    // to unity HERE, so a Stage mic parked at the seat reproduces the
    // calibrated chain's mono level and band balance (fenced by row MS9),
    // and positions away from it read as honest 1/r excursions.
    static constexpr double kSeatZ = 1.2, kSeatH = 0.6;
    static constexpr double kMinR     = 0.2;   // a mic cannot enter the board
    static constexpr double kMonoLeak = 0.15;  // case volume + rim gap: no
                                               // perfect dipole null in the
                                               // board plane on a real grand
    static constexpr double kLidRefl  = 0.6;   // varnished lid, mostly
                                               // specular at 2-6 kHz
    static constexpr double kLidX     = 0.3;   // the image ensemble's centre:
    static constexpr double kLidH     = 1.1;   // above the board, toward the
                                               // open (treble) side
    // The near-field shelf's DC pole, standing in for the dipole term's true
    // pole at zero (see the header). Derived from the lowest frequency this
    // instrument radiates -- A0's fundamental at 27.5 Hz: the floor costs
    // 20 log10 sqrt(1 + (kNfFloorHz / f)^2) of the closed form there, which
    // at 4 Hz is 0.09 dB, and it is the SAME error at every distance
    // (the zero cancels), so one number bounds the whole stage. It also
    // bounds the shelf: its DC gain is f_nf / kNfFloorHz <= 68 (+37 dB) at
    // the closest permitted mic, against a low bus that is already 68 dB
    // down at 4 Hz behind the board's 200 Hz second-order collapse.
    static constexpr double kNfFloorHz = 4.0;
    static constexpr double kAirDbPerM10k = 0.11; // ISO 9613-1, 20 C, 50% RH
    static constexpr double kMaxDelayS = 0.040;
    static constexpr double kFadeS     = 0.010;
    // The section-band gauge, measured: at one mic the two half-step-offset
    // banks (near-identical resonators that Classic keeps in SEPARATE
    // channels) sum coherently, which left the seat mic's section band
    // +2.4..+3.0 dB above the calibrated chain's band energy (notes 36/60,
    // 1.3-4 k and 4-10 k). One constant, because both chains are LTI: the
    // ratio is a fixed transfer-function property, not a note property
    // (residual note-to-note spread is its ripple sampled at sparse partial
    // frequencies). Pinned so the seat mic lands on the chain's band energy
    // (row MS9).
    static constexpr double kSecGauge = 0.75;   // -2.5 dB

    static constexpr int kBuses  = GrandRadiator::kStageSlots + 2;
    static constexpr int kBusLow = GrandRadiator::kStageSlots;
    static constexpr int kBusLid = GrandRadiator::kStageSlots + 1;

    static double seatR()
    {
        return std::sqrt (kSeatZ * kSeatZ + kSeatH * kSeatH);
    }

    // The dipole gain at the calibrated seat: with the low path divided by
    // dipRef()/seatR(), the seat mic reads the low bus at exactly unity.
    static double dipRef()
    {
        return (1.0 - kMonoLeak) * (kSeatH / seatR()) + kMonoLeak;
    }

    // ---- per-mic derived taps ---------------------------------------------
    struct Taps
    {
        bool   on = false;
        double panL = 0.0, panR = 0.0;      // equal-power pan times gain
        double gain[kBuses] {};
        double del[kBuses] {};              // samples
        // The low bus's dipole share and its near-field shelf N(s), applied
        // ONLY to that share (the monopole leak stays in gain[kBusLow]).
        double nfGain = 0.0;
        double nfB0 = 1.0, nfB1 = 0.0, nfA1 = 0.0;
        double nfX1 = 0.0, nfY1 = 0.0;
        double airA = 1.0;                  // one-pole coefficient
        double airY = 0.0;                  // one-pole state
    };

    void buildTaps (Taps& t, const Mic& m) const
    {
        t.on = m.on;
        // An off mic keeps its shelf at rest: the pole is at 4 Hz, so a
        // state left over from the last time it was on would take a quarter
        // of a second to die and the mic would fade in on top of it.
        if (! m.on) { t.nfX1 = t.nfY1 = 0.0; return; }
        const double g = std::pow (10.0, m.gainDb / 20.0);
        const double a = (m.pan + 1.0) * kPiD / 4.0;
        t.panL = g * std::cos (a);
        t.panR = g * std::sin (a);

        // The section buses: point sources on the bridge line, seat-gauged.
        for (int s = 0; s < GrandRadiator::kStageSlots; ++s)
        {
            const double dx = m.x - GrandRadiator::slotX (s);
            const double r = std::max (kMinR,
                std::sqrt (dx * dx + m.z * m.z + m.h * m.h));
            t.gain[s] = kSecGauge * seatR() / r;
            t.del[s]  = r / kSpeedOfSound * fs;
        }

        // The low bus: mid-bridge extended source with the below-coincidence
        // dipole sign, cos(theta) = h/r about the board plane (see header).
        const double r0 = std::max (kMinR,
            std::sqrt (m.x * m.x + m.z * m.z + m.h * m.h));
        const double gauge = (seatR() / r0) / dipRef();
        // The leak is a monopole: A/r at every k r, no near-field term.
        t.gain[kBusLow] = gauge * kMonoLeak;
        // The dipole share, which carries the near field. At h = 0 this is
        // identically zero and the whole shelf drops out of the arithmetic.
        t.nfGain        = gauge * (1.0 - kMonoLeak) * (m.h / r0);
        t.del[kBusLow]  = r0 / kSpeedOfSound * fs;

        // N(s) = (s + c/r) / (s + 2 pi kNfFloorHz), bilinear at s = 2 fs
        // (1 - z^-1)/(1 + z^-1). No prewarping: both critical frequencies
        // are below 300 Hz against a >= 44.1 kHz rate, where the warp is
        // about 0.01%, and the untapered map is exactly unity at Nyquist --
        // which is the property the far field depends on.
        {
            const double wz = kSpeedOfSound / r0;             // = 2 pi f_nf
            const double wp = 2.0 * kPiD * kNfFloorHz;
            const double k2 = 2.0 * fs;
            const double d  = 1.0 / (k2 + wp);
            t.nfB0 = (k2 + wz) * d;
            t.nfB1 = (wz - k2) * d;
            t.nfA1 = (wp - k2) * d;
        }

        // The lid image: reflected high band, gated by how much of the
        // reflector's exit cone the mic sits in (open side +x), and by line
        // of sight -- a mic under the board plane cannot see the lid.
        const double dxl = m.x - kLidX;
        const double dhl = m.h - kLidH;
        const double rl = std::max (kMinR,
            std::sqrt (dxl * dxl + m.z * m.z + dhl * dhl));
        const double rh = std::max (kMinR, std::sqrt (m.x * m.x + m.z * m.z));
        double open = std::clamp (0.5 + 1.5 * m.x / rh, 0.0, 1.0);
        if (m.h < 0.0)
            open *= std::clamp (1.0 + m.h / kMinR, 0.0, 1.0);
        // Same gauge as the sections: the lid reflects section-band content.
        t.gain[kBusLid] = kSecGauge * kLidRefl * open * seatR() / rl;
        t.del[kBusLid]  = rl / kSpeedOfSound * fs;

        for (int b = 0; b < kBuses; ++b)
            t.del[b] = std::clamp (t.del[b], 1.0, static_cast<double> (ringN - 4));

        // Air loss over this mic's distance (figure cited in the header).
        double fc = 10000.0 * std::sqrt (3.0 / (kAirDbPerM10k * std::max (r0, kMinR)));
        fc = std::clamp (fc, 2000.0, 0.45 * fs);
        t.airA = 1.0 - std::exp (-2.0 * kPiD * fc / fs);
    }

    void rebuildAll()
    {
        for (int i = 0; i < kMaxMics; ++i)
        {
            cur[static_cast<std::size_t> (i)] = readPending (i);
            buildTaps (setA[static_cast<std::size_t> (i)],
                       cur[static_cast<std::size_t> (i)]);
        }
        mode = modeA = modeB = pendMode.load (std::memory_order_relaxed);
    }

    Mic readPending (int i) const
    {
        const auto& p = pend[static_cast<std::size_t> (i)];
        Mic m;
        m.on     = p.on.load (std::memory_order_relaxed);
        m.x      = p.x.load (std::memory_order_relaxed);
        m.z      = p.z.load (std::memory_order_relaxed);
        m.h      = p.h.load (std::memory_order_relaxed);
        m.gainDb = p.gainDb.load (std::memory_order_relaxed);
        m.pan    = p.pan.load (std::memory_order_relaxed);
        return m;
    }

    // Audio thread, steady state only: fold every pending change into a new
    // active set and start the crossfade from a snapshot of the old one.
    void pickup()
    {
        const int newMode = pendMode.load (std::memory_order_relaxed);
        bool micsChanged = false;
        Mic next[kMaxMics];
        for (int i = 0; i < kMaxMics; ++i)
        {
            next[i] = readPending (i);
            const Mic& c = cur[static_cast<std::size_t> (i)];
            micsChanged = micsChanged
                || next[i].on != c.on || next[i].x != c.x || next[i].z != c.z
                || next[i].h != c.h || next[i].gainDb != c.gainDb
                || next[i].pan != c.pan;
        }
        const bool modeChanged = newMode != mode;
        if (! modeChanged && ! micsChanged)
            return;
        // Mic geometry is inaudible while the classic pair renders: apply
        // silently, no fade.
        if (! modeChanged && mode == 0)
        {
            for (int i = 0; i < kMaxMics; ++i)
            {
                cur[static_cast<std::size_t> (i)] = next[i];
                buildTaps (setA[static_cast<std::size_t> (i)], next[i]);
            }
            return;
        }
        setB = setA;
        modeB = mode;
        for (int i = 0; i < kMaxMics; ++i)
        {
            cur[static_cast<std::size_t> (i)] = next[i];
            buildTaps (setA[static_cast<std::size_t> (i)], next[i]);
        }
        modeA = newMode;
        // Entering Stage rendering from a cold classic period: the buses
        // were not being written, so start them from silence -- the fade-in
        // covers the r/c fill time.
        if (mode == 0)
        {
            std::fill (ring.begin(), ring.end(), 0.0);
            busAge = 0;
            for (auto& t : setA) t.airY = 0.0;
        }
        fadePos = 0;
    }

    // ---- the delay buses ---------------------------------------------------
    void writeBuses (const double* slots, double low)
    {
        if (busAge < 1 << 30) ++busAge;
        wp = (wp + 1) & (ringN - 1);
        double hi = 0.0;
        double* r = ring.data();
        for (int s = 0; s < GrandRadiator::kStageSlots; ++s)
        {
            r[static_cast<std::size_t> (s * ringN + wp)] = slots[s];
            hi += slots[s];
        }
        r[static_cast<std::size_t> (kBusLow * ringN + wp)] = low;
        r[static_cast<std::size_t> (kBusLid * ringN + wp)] = hi;
    }

    // Fractional read. The fraction is computed BEFORE the integer index
    // wraps: computing it after can hand back fr == +N when rp rounds to
    // exactly N, and the interpolation then extrapolates by N samples.
    double readTap (int bus, double d) const
    {
        const double* b = ring.data()
            + static_cast<std::size_t> (bus) * static_cast<std::size_t> (ringN);
        double rp = static_cast<double> (wp) - d;
        if (rp < 0.0) rp += ringN;
        int ri = static_cast<int> (rp);
        const double fr = rp - ri;
        if (ri >= ringN) ri -= ringN;
        int r2 = ri + 1;
        if (r2 >= ringN) r2 -= ringN;
        return b[ri] + fr * (b[r2] - b[ri]);
    }

    void renderSet (std::array<Taps, kMaxMics>& set, double& outL, double& outR)
    {
        // The arrival ramp: when the buses start from silence (entering
        // Stage rendering cold), a mic at distance r hears nothing until
        // r/c has elapsed, and the wavefront would otherwise land as a hard
        // step -- for any mic beyond c * fade time the step falls AFTER the
        // mode crossfade has finished and nothing masks it. So each tap
        // fades itself in over kFadeS starting at its own arrival: the
        // field builds up mic by mic at the speed of sound, and no tap ever
        // steps. Steady state pays one compare per tap.
        const double age = static_cast<double> (busAge);
        for (auto& t : set)
        {
            if (! t.on) continue;
            double acc = 0.0;
            for (int b = 0; b < kBuses; ++b)
            {
                const double s0 = readTap (b, t.del[b]);
                double v = t.gain[b] * s0;
                if (b == kBusLow)
                {
                    // The dipole share through the near-field shelf. Ticked
                    // unconditionally on this bus so its state stays in step
                    // with the delay line even where nfGain is zero.
                    const double y = t.nfB0 * s0 + t.nfB1 * t.nfX1 - t.nfA1 * t.nfY1;
                    t.nfX1 = s0;
                    t.nfY1 = y;
                    v += t.nfGain * y;
                }
                if (age < t.del[b] + fadeLen)
                {
                    const double u = std::clamp ((age - t.del[b]) / fadeLen, 0.0, 1.0);
                    v *= u * u * (3.0 - 2.0 * u);
                }
                acc += v;
            }
            t.airY += t.airA * (acc - t.airY);
            outL += t.panL * t.airY;
            outR += t.panR * t.airY;
        }
    }

    // ---- state -------------------------------------------------------------
    struct PendMic
    {
        std::atomic<bool>   on { false };
        std::atomic<double> x { 0.0 }, z { 1.2 }, h { 0.6 };
        std::atomic<double> gainDb { 0.0 }, pan { 0.0 };
    };

    double fs = 48000.0;
    GrandMicPair pair;

    std::array<PendMic, kMaxMics> pend;
    std::atomic<int>  pendMode { 0 };
    std::atomic<bool> dirty { false };

    Mic cur[kMaxMics];
    std::array<Taps, kMaxMics> setA, setB;   // active / fading-out
    int mode = 0, modeA = 0, modeB = 0;
    int fadeLen = 480, fadePos = 480;

    std::vector<double> ring;
    int ringN = 0;
    int wp = 0;
    int busAge = 0;   // samples since the buses last started from silence
};

} // namespace epi
