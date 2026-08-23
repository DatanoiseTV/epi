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
//   - amplitude 1/r, referenced to the calibrated pair's notional seat
//     (kRefDist = 1.2 m) so the default mic lands at the calibrated level;
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
    static constexpr double kRefDist  = 1.2;   // the calibrated pair's seat, m
    static constexpr double kMinR     = 0.2;   // a mic cannot enter the board
    static constexpr double kMonoLeak = 0.15;  // case volume + rim gap: no
                                               // perfect dipole null in the
                                               // board plane on a real grand
    static constexpr double kLidRefl  = 0.6;   // varnished lid, mostly
                                               // specular at 2-6 kHz
    static constexpr double kLidX     = 0.3;   // the image ensemble's centre:
    static constexpr double kLidH     = 1.1;   // above the board, toward the
                                               // open (treble) side
    static constexpr double kAirDbPerM10k = 0.11; // ISO 9613-1, 20 C, 50% RH
    static constexpr double kMaxDelayS = 0.040;
    static constexpr double kFadeS     = 0.010;

    static constexpr int kBuses  = GrandRadiator::kStageSlots + 2;
    static constexpr int kBusLow = GrandRadiator::kStageSlots;
    static constexpr int kBusLid = GrandRadiator::kStageSlots + 1;

    // The dipole gain at the calibrated seat (x=0, z=1.2, h=0.6): the gauge
    // that keeps the default Stage mic at the calibrated tonal balance.
    static double dipRef()
    {
        const double r = std::sqrt (kRefDist * kRefDist + 0.6 * 0.6);
        return (1.0 - kMonoLeak) * (0.6 / r) + kMonoLeak;
    }

    // ---- per-mic derived taps ---------------------------------------------
    struct Taps
    {
        bool   on = false;
        double panL = 0.0, panR = 0.0;      // equal-power pan times gain
        double gain[kBuses] {};
        double del[kBuses] {};              // samples
        double airA = 1.0;                  // one-pole coefficient
        double airY = 0.0;                  // one-pole state
    };

    void buildTaps (Taps& t, const Mic& m) const
    {
        t.on = m.on;
        if (! m.on) return;
        const double g = std::pow (10.0, m.gainDb / 20.0);
        const double a = (m.pan + 1.0) * kPiD / 4.0;
        t.panL = g * std::cos (a);
        t.panR = g * std::sin (a);

        // The section buses: point sources on the bridge line.
        for (int s = 0; s < GrandRadiator::kStageSlots; ++s)
        {
            const double dx = m.x - GrandRadiator::slotX (s);
            const double r = std::max (kMinR,
                std::sqrt (dx * dx + m.z * m.z + m.h * m.h));
            t.gain[s] = kRefDist / r;
            t.del[s]  = r / kSpeedOfSound * fs;
        }

        // The low bus: mid-bridge extended source with the below-coincidence
        // dipole sign, cos(theta) = h/r about the board plane (see header).
        const double r0 = std::max (kMinR,
            std::sqrt (m.x * m.x + m.z * m.z + m.h * m.h));
        const double dip = (1.0 - kMonoLeak) * (m.h / r0) + kMonoLeak;
        t.gain[kBusLow] = (kRefDist / r0) * dip / dipRef();
        t.del[kBusLow]  = r0 / kSpeedOfSound * fs;

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
        t.gain[kBusLid] = kLidRefl * open * kRefDist / rl;
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
            for (auto& t : setA) t.airY = 0.0;
        }
        fadePos = 0;
    }

    // ---- the delay buses ---------------------------------------------------
    void writeBuses (const double* slots, double low)
    {
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
        for (auto& t : set)
        {
            if (! t.on) continue;
            double acc = 0.0;
            for (int b = 0; b < kBuses; ++b)
                acc += t.gain[b] * readTap (b, t.del[b]);
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
};

} // namespace epi
