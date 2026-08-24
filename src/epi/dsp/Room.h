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

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace epi
{

// ---------------------------------------------------------------------------
// The room the instrument is standing in.
//
// This is the one block in the chain that is not part of the instrument, and
// it is worth saying so plainly: no Rhodes has a reverb in it. A Suitcase has
// a power amp and two pairs of speakers in a wooden box, and everything past
// that is the room. It is here because a recorded Rhodes is always in one, and
// a dry one sounds like a measurement rather than a performance.
//
// A feedback delay network, which is the honest way to build a room rather
// than a bank of comb filters: eight delays whose lengths are mutually prime,
// mixed every pass through an orthogonal matrix so no energy is created and
// none of it lines up into a flutter. The matrix used is Householder,
// H = I - (2/N) 1 1^T, which is its own inverse and costs one sum for the whole
// mixing step rather than sixty-four multiplies.
//
// Orthogonality is what makes the decay controllable: with it, the loop gain
// is exactly the per-delay attenuation, so a target decay time can be SOLVED
// for rather than dialled in --
//
//     g_i = 10 ^ (-3 * L_i / (T60 * fs))
//
// -- and each delay contributes the same decay however long it is, which is
// what stops the short ones dying first and leaving a ringing tail on the
// long ones.
//
// The damping is a one-pole in each loop, because a real room absorbs treble
// far faster than bass: without it the tail turns into a hiss that outlasts
// the note, which is the single most recognisable sound of a bad reverb.
//
// Beyond the shipped room (profile 0, bit-exact), five profiles are derived
// from real shoebox geometries and published per-octave absorption tables:
// Eyring RT60 per band sets each line's absorptive one-pole, an image-source
// sweep (order <= 2) provides the early reflections, and the mean free path
// 4V/S sets the line lengths and the late field's entry delay. Every number
// is derived in docs/research/room-acoustics-measured.md; nothing here is a
// sampled impulse response.
// ---------------------------------------------------------------------------
class Room
{
public:
    static constexpr int kLines    = 8;
    static constexpr int kProfiles = 6;   // 0 Current, 1 Booth, 2 Studio, 3 Stage, 4 Hall, 5 Church
    static constexpr int kBands    = 6;   // octave bands 125 Hz .. 4 kHz
    static constexpr int kMaxTaps  = 24;  // image sources, order <= 2

    void prepare (double sampleRate)
    {
        fs = sampleRate;

        // Mutually prime lengths, spread over about a 4:1 range, scaled from a
        // 48 kHz reference so the room keeps its size at any sample rate. Prime
        // lengths matter: any common factor between two delays puts their
        // echoes on top of each other and the result rings at that period.
        // The buffers are allocated for the longest profile (Church at full
        // size needs 1.24x the shipped set) so a profile change never
        // allocates; each profile then uses an effective length within them.
        const double scale = fs / 48000.0;
        for (int i = 0; i < kLines; ++i)
        {
            const int n = std::max (16, static_cast<int> (std::lround (kPrime48[i] * scale)));
            const int alloc = std::max (16, static_cast<int> (std::lround (kPrime48[i] * scale * 1.25)) + 2);
            line[i].assign (static_cast<std::size_t> (alloc), 0.0f);
            len0[i] = n;
            length[i] = n;
            pos[i] = 0;
        }

        // A short pre-delay: the instrument reaches the listener before the
        // walls do, and without the gap the tail smears into the attack.
        preLen = std::max (1, static_cast<int> (std::lround (0.012 * fs)));
        pre.assign (static_cast<std::size_t> (preLen), 0.0f);
        prePos = 0;

        // Profile machinery: input history for the image-source taps (the
        // Church's latest kept image is 233 ms after the direct sound, x sqrt2
        // at full size -> 360 ms bounds it) and the late field's own
        // mean-free-path pre-delay (Church 44.7 ms x sqrt2).
        erBuf.assign (static_cast<std::size_t> (std::max (16, static_cast<int> (std::lround (0.36 * fs)))), 0.0f);
        erW = 0;
        preLate.assign (static_cast<std::size_t> (std::max (4, static_cast<int> (std::lround (0.075 * fs)))), 0.0f);
        preLateW = 0;

        fadeStep = 1.0f / static_cast<float> (std::max (1, static_cast<int> (std::lround (0.015 * fs))));
        prepared = true;

        reset();
        if (targetProfile == 0)
        {
            targetSize = 0.4f;
            applyConfigNow (0, 0.4f);
        }
        else
        {
            applyConfigNow (targetProfile, targetSize);
        }
        fadeGain = 1.0f;
        fadeTarget = 1.0f;
        pendingDirty = false;
    }

    void reset()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        std::fill (erBuf.begin(), erBuf.end(), 0.0f);
        std::fill (preLate.begin(), preLate.end(), 0.0f);
        for (auto& d : damp) d = 0.0f;
        for (auto& s : x1p) s = 0.0f;
        for (auto& t : taps) { t.state = 0.0f; t.x1 = 0.0f; }
        prePos = 0;
        erW = 0;
        preLateW = 0;
        for (int i = 0; i < kLines; ++i) pos[i] = 0;

        // Reset happens at provable silence, so a pending profile can land
        // without the fade: there is nothing to click through.
        if (pendingDirty)
        {
            applyConfigNow (pendingProfile, pendingSize);
            pendingDirty = false;
        }
        fadeGain = 1.0f;
        fadeTarget = 1.0f;
    }

    // Which room. {0 Current, 1 Booth, 2 Studio, 3 Stage, 4 Hall, 5 Church}.
    // Geometry cannot move continuously -- tap delays and line lengths are
    // integers -- so a change rides a 15 ms wet-path fade to silence, swaps at
    // the null, and fades back. Repeated calls mid-fade retarget the pending
    // configuration; the ramp itself is the rate limit.
    void setProfile (int profile)
    {
        targetProfile = std::clamp (profile, 0, kProfiles - 1);
        requestConfig();
    }

    // Size and decay are one control on the panel, because they are one thing
    // in a room: a bigger space rings longer. For the shipped room, 0.6 s to
    // about 4 s. For the profiles the knob scales the room's linear
    // dimensions by 2^(size - 0.5) about the surveyed geometry, and the
    // Eyring computation takes care of what that does to the decay.
    void setSize (float sizeNorm)
    {
        targetSize = sizeNorm;
        if (targetProfile == 0 && activeProfile == 0 && ! pendingDirty)
        {
            // The shipped path: instant and bit-exact, as it always was.
            setSizeCurrent (sizeNorm);
            activeSize = sizeNorm;
            return;
        }
        requestConfig();
    }

    // Returns the wet pair. The caller mixes; keeping the dry path untouched
    // means the mix control cannot colour the instrument at zero.
    void process (double inL, double inR, double& wetL, double& wetR)
    {
        const float in = static_cast<float> (0.5 * (inL + inR));

        // Input history is written in every mode, so a profile switch finds
        // the early reflections of notes that are already ringing.
        erBuf[static_cast<std::size_t> (erW)] = in;
        erW = erW + 1 < static_cast<int> (erBuf.size()) ? erW + 1 : 0;
        preLate[static_cast<std::size_t> (preLateW)] = in;
        preLateW = preLateW + 1 < static_cast<int> (preLate.size()) ? preLateW + 1 : 0;

        if (mode0)
        {
            pre[static_cast<std::size_t> (prePos)] = in;
            prePos = prePos + 1 < preLen ? prePos + 1 : 0;
            const float x = pre[static_cast<std::size_t> (prePos)];

            float v[kLines];
            float sum = 0.0f;
            for (int i = 0; i < kLines; ++i)
            {
                v[i] = line[i][static_cast<std::size_t> (pos[i])];
                sum += v[i];
            }

            // Householder: y_i = v_i - (2/N) * sum. Orthogonal, so the network
            // cannot gain, and one accumulate does the whole mix.
            const float corr = (2.0f / static_cast<float> (kLines)) * sum;

            for (int i = 0; i < kLines; ++i)
            {
                float y = (v[i] - corr) * gain[i];
                damp[i] += dampCoef * (y - damp[i]);
                y = damp[i];

                // Alternate the input polarity across the lines so the first pass
                // does not arrive as one in-phase thump.
                line[i][static_cast<std::size_t> (pos[i])] = y + ((i & 1) ? -x : x) * 0.35f;
                pos[i] = pos[i] + 1 < length[i] ? pos[i] + 1 : 0;
            }

            // Two decorrelated halves of the network make the stereo pair. Taking
            // alternate lines rather than splitting the array in half keeps the
            // two sides' delay lengths interleaved, so neither channel is
            // systematically the shorter room.
            double l = 0.0, r = 0.0;
            for (int i = 0; i < kLines; i += 2) l += v[i];
            for (int i = 1; i < kLines; i += 2) r += v[i];

            const double norm = 2.0 / kLines;
            wetL = l * norm;
            wetR = r * norm;
        }
        else
        {
            // Late field input: one mean free path after the direct sound,
            // which is when a real room's reflections stop being countable.
            int ri = preLateW - preLenEff;
            if (ri < 0) ri += static_cast<int> (preLate.size());
            const float x = preLate[static_cast<std::size_t> (ri)];

            float v[kLines];
            float sum = 0.0f;
            for (int i = 0; i < kLines; ++i)
            {
                v[i] = line[i][static_cast<std::size_t> (pos[i])];
                sum += v[i];
            }
            const float corr = (2.0f / static_cast<float> (kLines)) * sum;

            for (int i = 0; i < kLines; ++i)
            {
                // Absorptive first-order section per line, |H| fitted to the
                // Eyring g_i(f) = 10^(-3 L_i / (T60(f) fs)) at 125 / 500 Hz /
                // 4 kHz: a pole when the room darkens the tail, a zero when
                // it brightens it (the stage's panel absorbers eat bass).
                const float yf = v[i] - corr;
                const float yo = b0p[i] * yf + b1p[i] * x1p[i] + a1p[i] * damp[i];
                x1p[i] = yf;
                damp[i] = yo;
                line[i][static_cast<std::size_t> (pos[i])] = yo + ((i & 1) ? -x : x) * 0.35f;
                pos[i] = pos[i] + 1 < length[i] ? pos[i] + 1 : 0;
            }

            double l = 0.0, r = 0.0;
            for (int i = 0; i < kLines; i += 2) l += v[i];
            for (int i = 1; i < kLines; i += 2) r += v[i];
            const double norm = 2.0 / kLines;

            // Early reflections: image sources with the wall's band absorption
            // folded into a one-pole per tap.
            double eL = 0.0, eR = 0.0;
            for (int t = 0; t < erCount; ++t)
            {
                Tap& tp = taps[t];
                int idx = erW - 1 - tp.delay;
                if (idx < 0) idx += static_cast<int> (erBuf.size());
                const float s = erBuf[static_cast<std::size_t> (idx)];
                const float yo = tp.b0 * s + tp.b1 * tp.x1 + tp.a1 * tp.state;
                tp.x1 = s;
                tp.state = yo;
                eL += static_cast<double> (yo) * tp.gL;
                eR += static_cast<double> (yo) * tp.gR;
            }

            wetL = eL + l * norm * lateGain;
            wetR = eR + r * norm * lateGain;
        }

        // The profile-change fade. Inactive (and skipped, so the shipped room
        // stays bit-exact) whenever the gain sits at unity.
        if (fadeGain < 1.0f || fadeTarget < 1.0f)
        {
            if (fadeGain > fadeTarget)
                fadeGain = std::max (fadeTarget, fadeGain - fadeStep);
            else if (fadeGain < fadeTarget)
                fadeGain = std::min (fadeTarget, fadeGain + fadeStep);
            // The apply-at-null check sits OUTSIDE the down-ramp branch: a
            // request that lands while the gain already sits at the null
            // (any retarget arriving exactly at the fade grid -- a host
            // automating size at a block length dividing the fade does this
            // on the first fade) would otherwise leave pendingDirty set with
            // neither ramp branch true, and the wet path stayed silenced
            // until reset. Found by a fade-torture probe, not by ear.
            if (fadeGain <= 0.0f && pendingDirty)
            {
                applyConfigNow (pendingProfile, pendingSize);
                pendingDirty = false;
                fadeTarget = 1.0f;
            }
            wetL *= fadeGain;
            wetR *= fadeGain;
        }
    }

private:
    // -----------------------------------------------------------------------
    // Derived-profile data. Everything below is computed, never sampled; the
    // derivation with sources lives in docs/research/room-acoustics-measured.md.
    // -----------------------------------------------------------------------

    static constexpr int kPrime48[kLines] = { 1327, 1613, 1949, 2273, 2617, 2939, 3271, 3593 };

    // Mean length of the shipped prime set at 48 kHz, in seconds. Profile
    // line lengths scale the set so its mean matches the room's mean free
    // path time 4V/(S c).
    static constexpr double kRefLineTime = (19582.0 / 8.0) / 48000.0;

    static constexpr double kSpeedOfSound = 343.0;

    // Octave-band absorption coefficients, 125 Hz .. 4 kHz. Fetched published
    // chart (acoustic-supplies.com), matching Everest's appendix tables; see
    // the research doc for the per-row provenance.
    enum Material { matDrape = 0, matCarpet, matPlywood, matWoodFloor, matPlaster,
                    matBrick, matStone, matGlass, matGypsum, matSeats, matFiberglass };

    static constexpr float kMatAlpha[11][kBands] = {
        { 0.14f, 0.35f, 0.53f, 0.75f, 0.70f, 0.60f },  // heavy drapery, pleated
        { 0.01f, 0.02f, 0.06f, 0.15f, 0.25f, 0.45f },  // carpet on hard floor
        { 0.38f, 0.24f, 0.17f, 0.10f, 0.08f, 0.05f },  // plywood panel over air gap
        { 0.15f, 0.11f, 0.10f, 0.07f, 0.06f, 0.07f },  // wood flooring on joists
        { 0.01f, 0.02f, 0.02f, 0.03f, 0.04f, 0.05f },  // plaster on masonry
        { 0.03f, 0.03f, 0.03f, 0.04f, 0.05f, 0.07f },  // natural brick / rough stone
        { 0.01f, 0.01f, 0.01f, 0.01f, 0.02f, 0.02f },  // marble / dressed stone
        { 0.18f, 0.06f, 0.04f, 0.03f, 0.02f, 0.02f },  // plate glass 6 mm
        { 0.29f, 0.10f, 0.06f, 0.05f, 0.04f, 0.04f },  // plasterboard on studs
        { 0.60f, 0.74f, 0.88f, 0.96f, 0.93f, 0.85f },  // occupied upholstered seats
        { 0.53f, 0.99f, 0.99f, 0.99f, 0.99f, 0.99f },  // fiberglass board 75 mm
    };

    // Air intensity attenuation m [1/m] at 20 C, 50 % RH (ISO 9613-1 derived).
    static constexpr double kAirM[kBands] = { 0.0001, 0.0003, 0.00065, 0.00115, 0.00207, 0.00527 };

    static constexpr double kBandHz[kBands] = { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0 };

    // Up to three materials blended per wall, by area fraction.
    struct Wall { int m0; float f0; int m1; float f1; int m2; float f2; };

    // Wall order: x-low (behind the instrument), x-high, y-low, y-high,
    // floor, ceiling. Source is the instrument, near one end; the listener
    // sits out in the room with a small lateral offset.
    struct Spec
    {
        double lx, ly, lz;
        double sx, sy, sz;
        double rx, ry, rz;
        Wall w[6];
    };

    static const Spec& spec (int profile)
    {
        static constexpr Wall boothWall  { matDrape, 1.0f, -1, 0.0f, -1, 0.0f };
        static constexpr Wall studioWall { matGypsum, 0.65f, matDrape, 0.20f, matGlass, 0.15f };
        static constexpr Wall stageWall  { matPlywood, 0.80f, matDrape, 0.20f, -1, 0.0f };
        static constexpr Wall hallWall   { matPlaster, 0.70f, matPlywood, 0.30f, -1, 0.0f };
        static constexpr Wall churchWall { matBrick, 0.90f, matGlass, 0.10f, -1, 0.0f };

        static constexpr Spec kSpecs[kProfiles - 1] = {
            // 1 Booth: vocal-booth build.
            { 2.0, 1.5, 2.2,   0.5, 0.75, 1.1,   1.4, 0.85, 1.2,
              { boothWall, boothWall, boothWall, boothWall,
                { matCarpet, 1.0f, -1, 0.0f, -1, 0.0f },
                { matFiberglass, 1.0f, -1, 0.0f, -1, 0.0f } } },
            // 2 Studio live room.
            { 6.0, 5.0, 3.0,   1.2, 2.5, 1.0,   3.7, 2.8, 1.4,
              { studioWall, studioWall, studioWall, studioWall,
                { matWoodFloor, 1.0f, -1, 0.0f, -1, 0.0f },
                { matFiberglass, 0.6f, matGypsum, 0.4f, -1, 0.0f } } },
            // 3 Wooden stage with curtains.
            { 12.0, 9.0, 6.0,  3.0, 4.5, 1.0,   7.0, 4.0, 1.6,
              { stageWall, stageWall, stageWall, stageWall,
                { matWoodFloor, 1.0f, -1, 0.0f, -1, 0.0f },
                { matPlywood, 1.0f, -1, 0.0f, -1, 0.0f } } },
            // 4 Occupied concert hall.
            { 30.0, 20.0, 14.0, 4.0, 10.0, 1.5, 12.0, 9.0, 1.6,
              { hallWall, hallWall, hallWall, hallWall,
                { matSeats, 0.7f, matWoodFloor, 0.3f, -1, 0.0f },
                { matPlaster, 1.0f, -1, 0.0f, -1, 0.0f } } },
            // 5 Stone church, congregation present.
            { 40.0, 18.0, 20.0, 8.0, 9.0, 1.2, 16.0, 8.0, 1.6,
              { churchWall, churchWall, churchWall, churchWall,
                { matStone, 0.40f, matPlywood, 0.32f, matSeats, 0.28f },
                { matPlaster, 1.0f, -1, 0.0f, -1, 0.0f } } },
        };
        return kSpecs[profile - 1];
    }

    static double wallAlpha (const Wall& w, int band)
    {
        double a = 0.0;
        if (w.m0 >= 0) a += static_cast<double> (w.f0) * kMatAlpha[w.m0][band];
        if (w.m1 >= 0) a += static_cast<double> (w.f1) * kMatAlpha[w.m1][band];
        if (w.m2 >= 0) a += static_cast<double> (w.f2) * kMatAlpha[w.m2][band];
        return a;
    }

    // Solve a one-pole's coefficient so its 4 kHz / DC magnitude ratio hits
    // the target (ratio <= 1): (1-a)^2 = ratio^2 (1 - 2 a cos w4 + a^2). The
    // tangent fallback covers ratios below what the pole can reach at this
    // sample rate.
    static double solvePole (double ratio, double sampleRate)
    {
        ratio = std::clamp (ratio, 0.05, 1.0);
        const double w4 = 2.0 * kPiD * 4000.0 / sampleRate;
        const double R = ratio * ratio;
        const double A = 1.0 - R;
        if (A < 1.0e-9) return 0.0;
        const double B = 1.0 - R * std::cos (w4);
        const double disc = B * B - A * A;
        const double a = disc >= 0.0 ? (B - std::sqrt (disc)) / A   // exact ratio match
                                     : B / A;                        // tangent: closest reachable
        return std::clamp (a, 0.0, 0.9995);
    }

    static double firstOrderMag (double b0d, double z1, double p, double w)
    {
        const double c = std::cos (w), s = std::sin (w);
        const double zn = std::sqrt ((1.0 - z1 * c) * (1.0 - z1 * c) + z1 * z1 * s * s);
        const double pn = std::sqrt ((1.0 - p * c) * (1.0 - p * c) + p * p * s * s);
        return b0d * zn / pn;
    }

    // First-order section y = b0 x + b1 x' + a1 y' fitted to three band
    // gains: the 125 Hz / 4 kHz ratio picks a pole (tail darkens) or a
    // zero-plus-pole shelf (tail brightens: the stage's panel absorbers eat
    // bass), and the overall gain lands the 500 Hz point exactly -- that is
    // the anchor the engine suite measures against. The shelf matters: a
    // bare zero anchored at 500 Hz keeps rising past 4 kHz and puts the loop
    // gain above unity at Nyquist, and the network then self-oscillates. The
    // shelf's pole flattens the response above the band it was fitted to, so
    // its peak stays at ~g4k < 1 and the section is passive either way.
    static void fitFirstOrder (double g125, double g500, double g4k, double sampleRate,
                               float& b0, float& b1, float& a1)
    {
        const double w5 = 2.0 * kPiD * 500.0 / sampleRate;
        const double ratio = g125 > 1.0e-12 ? g4k / g125 : 1.0;
        if (ratio <= 1.0)
        {
            const double a = solvePole (ratio, sampleRate);
            b0 = static_cast<float> (g500 * std::sqrt (1.0 - 2.0 * a * std::cos (w5) + a * a));
            b1 = 0.0f;
            a1 = static_cast<float> (a);
            return;
        }

        // Rising shelf: pole fixed by a 1.2 kHz transition (complete well
        // below 4 kHz), zero solved by bisection so the realized
        // |H(4k)|/|H(0)| hits the target ratio.
        const double w4 = 2.0 * kPiD * 4000.0 / sampleRate;
        const double k  = std::min (ratio, 4.0);
        const double p  = std::exp (-2.0 * kPiD * 1200.0 / sampleRate);
        auto realized = [p, w4] (double kd)
        {
            const double q  = (1.0 - p) / ((1.0 + p) * kd);
            const double z1 = (1.0 - q) / (1.0 + q);
            return firstOrderMag (1.0, z1, p, w4) / firstOrderMag (1.0, z1, p, 0.0);
        };
        double lo = 1.0, hi = 8.0;
        for (int it = 0; it < 32; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            (realized (mid) < k ? lo : hi) = mid;
        }
        const double kd = 0.5 * (lo + hi);
        const double q  = (1.0 - p) / ((1.0 + p) * kd);
        const double z1 = (1.0 - q) / (1.0 + q);
        double g = g500 / firstOrderMag (1.0, z1, p, w5);

        // Loop-gain safety net: never let the section's peak (at Nyquist for
        // a rising shelf) reach unity. Untriggered by any surveyed profile.
        const double peak = g * (1.0 + z1) / (1.0 + p);
        if (peak > 0.99) g *= 0.99 / peak;

        b0 = static_cast<float> (g);
        b1 = static_cast<float> (-z1 * g);
        a1 = static_cast<float> (p);
    }

    void requestConfig()
    {
        if (! prepared) return;   // prepare() applies the stored target itself
        if (targetProfile == activeProfile
            && std::abs (targetSize - activeSize) < 1.0e-6f
            && ! pendingDirty)
            return;
        pendingProfile = targetProfile;
        pendingSize    = targetSize;
        pendingDirty   = true;
        fadeTarget     = 0.0f;
    }

    // The shipped room's size mapping, byte for byte.
    void setSizeCurrent (float sizeNorm)
    {
        const double s = std::clamp (static_cast<double> (sizeNorm), 0.0, 1.0);
        const double t60 = 0.6 + 3.4 * s * s;

        for (int i = 0; i < kLines; ++i)
            gain[i] = static_cast<float> (std::pow (10.0, -3.0 * length[i] / (t60 * fs)));

        // A larger room is a duller one: more air and more surface between the
        // source and each reflection.
        const double cut = 7000.0 - 4600.0 * s;
        dampCoef = static_cast<float> (1.0 - std::exp (-2.0 * kPiD * cut / fs));
    }

    // Swap in a configuration. Runs at the fade null (or at reset/prepare, on
    // silence): fixed-size arithmetic only, no allocation.
    void applyConfigNow (int profile, float sizeNorm)
    {
        activeProfile = profile;
        activeSize    = sizeNorm;

        // The network state belonged to the old geometry; the fade has
        // already taken the wet path to zero, so it clears rather than being
        // reinterpreted at the new lengths. The input history buffers are
        // kept: they are the room-independent past of the instrument.
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        for (auto& d : damp) d = 0.0f;
        for (auto& s : x1p) s = 0.0f;
        for (auto& t : taps) { t.state = 0.0f; t.x1 = 0.0f; }
        prePos = 0;
        for (int i = 0; i < kLines; ++i) pos[i] = 0;

        if (profile == 0)
        {
            for (int i = 0; i < kLines; ++i) length[i] = len0[i];
            erCount  = 0;
            lateGain = 1.0;
            mode0    = true;
            setSizeCurrent (sizeNorm);
            return;
        }

        const Spec& sp = spec (profile);

        // The size knob scales the surveyed room's linear dimensions.
        const double k  = std::pow (2.0, static_cast<double> (std::clamp (sizeNorm, 0.0f, 1.0f)) - 0.5);
        const double lx = sp.lx * k, ly = sp.ly * k, lz = sp.lz * k;
        const double V  = lx * ly * lz;
        const double area[6] = { ly * lz, ly * lz, lx * lz, lx * lz, lx * ly, lx * ly };
        const double S  = 2.0 * (ly * lz + lx * lz + lx * ly);

        // Eyring per band, air term included: T60 = 0.161 V / (-S ln(1-a) + 4mV).
        double t60[kBands];
        double abar500 = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            double sa = 0.0;
            for (int w = 0; w < 6; ++w) sa += area[w] * wallAlpha (sp.w[w], b);
            const double abar = sa / S;
            if (b == 2) abar500 = abar;
            const double A = -S * std::log (1.0 - abar);
            t60[b] = 0.161 * V / (A + 4.0 * kAirM[b] * V);
        }

        // Line lengths follow the mean free path 4V/S; the decay per band is
        // then solved into each line's absorptive one-pole.
        const double mfpT = 4.0 * V / (S * kSpeedOfSound);
        const double lineScale = mfpT / kRefLineTime;
        const double fsScale = fs / 48000.0;
        for (int i = 0; i < kLines; ++i)
        {
            const int want = static_cast<int> (std::lround (kPrime48[i] * fsScale * lineScale));
            length[i] = std::clamp (want, 16, static_cast<int> (line[i].size()));

            const double g125 = std::pow (10.0, -3.0 * length[i] / (t60[0] * fs));
            const double g500 = std::pow (10.0, -3.0 * length[i] / (t60[2] * fs));
            const double g4k  = std::pow (10.0, -3.0 * length[i] / (t60[5] * fs));
            fitFirstOrder (g125, g500, g4k, fs, b0p[i], b1p[i], a1p[i]);
        }

        preLenEff = std::clamp (static_cast<int> (std::lround (mfpT * fs)),
                                1, static_cast<int> (preLate.size()) - 1);

        // Image sources, order <= 2: per axis the source itself, one bounce
        // off either wall, and the double bounce across the pair.
        struct AxOpt { double c; int n; int w1, w2; };
        AxOpt opt[3][5];
        const double src[3] = { sp.sx * k, sp.sy * k, sp.sz * k };
        const double lis[3] = { sp.rx * k, sp.ry * k, sp.rz * k };
        const double dim[3] = { lx, ly, lz };
        for (int ax = 0; ax < 3; ++ax)
        {
            const int wlo = 2 * ax, whi = 2 * ax + 1;
            opt[ax][0] = { src[ax],                 0, -1, -1 };   // no bounce
            opt[ax][1] = { -src[ax],                1, wlo, -1 };  // low wall
            opt[ax][2] = { 2.0 * dim[ax] - src[ax], 1, whi, -1 };  // high wall
            opt[ax][3] = { src[ax] + 2.0 * dim[ax], 2, wlo, whi }; // across the pair
            opt[ax][4] = { src[ax] - 2.0 * dim[ax], 2, wlo, whi };
        }

        const double rd = std::sqrt ((src[0] - lis[0]) * (src[0] - lis[0])
                                   + (src[1] - lis[1]) * (src[1] - lis[1])
                                   + (src[2] - lis[2]) * (src[2] - lis[2]));

        erCount = 0;
        for (int ix = 0; ix < 5; ++ix)
        for (int iy = 0; iy < 5; ++iy)
        for (int iz = 0; iz < 5; ++iz)
        {
            const int order = opt[0][ix].n + opt[1][iy].n + opt[2][iz].n;
            if (order < 1 || order > 2 || erCount >= kMaxTaps) continue;

            const double dx = opt[0][ix].c - lis[0];
            const double dy = opt[1][iy].c - lis[1];
            const double dz = opt[2][iz].c - lis[2];
            const double r  = std::sqrt (dx * dx + dy * dy + dz * dz);

            const int delay = static_cast<int> (std::lround ((r - rd) / kSpeedOfSound * fs));
            if (delay < 0 || delay >= static_cast<int> (erBuf.size())) continue;

            const int wallIdx[6] = { opt[0][ix].w1, opt[0][ix].w2, opt[1][iy].w1,
                                     opt[1][iy].w2, opt[2][iz].w1, opt[2][iz].w2 };
            double amp[3] = { rd / r, rd / r, rd / r };   // bands 125 / 500 / 4k
            static constexpr int kFitBand[3] = { 0, 2, 5 };
            for (int w = 0; w < 6; ++w)
            {
                if (wallIdx[w] < 0) continue;
                for (int b = 0; b < 3; ++b)
                    amp[b] *= std::sqrt (1.0 - wallAlpha (sp.w[wallIdx[w]], kFitBand[b]));
            }

            Tap& tp = taps[erCount++];
            tp.delay = delay;
            tp.state = 0.0f;
            tp.x1 = 0.0f;
            fitFirstOrder (amp[0], amp[1], amp[2], fs, tp.b0, tp.b1, tp.a1);

            // Equal-power pan by the image's lateral angle at the listener,
            // who faces the instrument down the x axis.
            const double pan = std::clamp (dy / r, -1.0, 1.0);
            tp.gL = std::sqrt (0.5 * (1.0 - pan));
            tp.gR = std::sqrt (0.5 * (1.0 + pan));
        }

        // Early/late balance from the direct-to-reverberant relationship:
        // reverberant-to-direct pressure ratio sqrt(16 pi rd^2 / R) with room
        // constant R = A/(1-abar) at 500 Hz. 0.30 is the one calibration
        // constant, aligning the Studio's wet level with the shipped room.
        const double A500 = -S * std::log (1.0 - abar500) + 4.0 * kAirM[2] * V;
        const double Rc   = A500 / (1.0 - abar500);
        lateGain = 0.30 * std::sqrt (16.0 * kPiD * rd * rd / Rc);

        mode0 = false;
    }

    // -----------------------------------------------------------------------

    double fs = 48000.0;

    std::array<std::vector<float>, kLines> line;
    std::array<int, kLines> len0 {};      // profile 0 effective lengths
    std::array<int, kLines> length {};    // active effective lengths
    std::array<int, kLines> pos {};
    std::array<float, kLines> gain {};    // profile 0 broadband loop gain
    std::array<float, kLines> damp {};    // loop filter state (both modes)
    float dampCoef = 0.5f;

    std::array<float, kLines> b0p {};     // profile absorptive first-order sections
    std::array<float, kLines> b1p {};
    std::array<float, kLines> a1p {};
    std::array<float, kLines> x1p {};     // their input-history state

    std::vector<float> pre;               // profile 0 pre-delay, untouched
    int preLen = 1, prePos = 0;

    std::vector<float> preLate;           // profile late-field pre-delay
    int preLateW = 0, preLenEff = 1;

    struct Tap { int delay = 0; float b0 = 0.0f, b1 = 0.0f, a1 = 0.0f,
                 gL = 0.0f, gR = 0.0f, state = 0.0f, x1 = 0.0f; };
    std::vector<float> erBuf;             // shared input history for the taps
    int erW = 0;
    std::array<Tap, kMaxTaps> taps {};
    int erCount = 0;
    double lateGain = 1.0;

    bool  mode0 = true;
    bool  prepared = false;
    int   activeProfile = 0;
    float activeSize = 0.4f;
    int   targetProfile = 0;
    float targetSize = 0.4f;
    int   pendingProfile = 0;
    float pendingSize = 0.4f;
    bool  pendingDirty = false;
    float fadeGain = 1.0f, fadeTarget = 1.0f, fadeStep = 1.0f;
};

} // namespace epi
