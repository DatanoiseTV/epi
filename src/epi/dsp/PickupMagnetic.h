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

#include <array>

namespace epi
{

// ---------------------------------------------------------------------------
// The Rhodes electromagnetic pickup.
//
// This is not a tone control. It is where the sound comes from.
//
// Muenster & Pfeifle tracked a Rhodes tine with a high-speed camera at 38 kfps
// and found something that decides the whole architecture of this model: after
// a transient of ten to fourteen milliseconds the tine moves as a PURE SINE.
// No overtones of its own, in either polarisation, at any playing strength
// (ISMA 2014, sections 3.1.2 and 3.2.2). Yet the signal at the output jack is
// full of harmonics, and it growls in the bass when you dig in.
//
// All of that harmonic content is manufactured here. A sine goes in and a
// complicated waveform comes out, because the magnetic field the tine is
// moving through is strongly non-uniform, and the tine samples a different
// part of it at every instant of its swing. Model the field wrongly and no
// amount of work on the metal will make the instrument sound right.
//
// Geometry, in the two coordinates that matter:
//
//   v  across the magnet's wedge -- the direction the tine actually vibrates,
//      since the hammer hits it from below. v = 0 is the wedge centreline.
//   g  along the magnet's axis -- the tine-to-magnet gap.
//
// The pole piece is wedge-shaped, so its surface recedes from the tine as you
// move away from the centreline: g_surface(v) is smallest at v = 0. Following
// Pfeifle (DAFx-17, section 5.4) the field is computed from a fictitious
// magnetic charge spread over that surface, integrating
//
//   B(v', g') = sigma * INTEGRAL  (g' - g_s(v)) / [ (v'-v)^2 + (g'-g_s(v))^2 ]^(3/2)  dv
//
// which is the standard Coulombian approach used for guitar pickups and which
// Pfeifle notes reproduces measured pickup fields well. The integral is
// evaluated once at prepare() into a table; at audio rate this is two
// interpolated reads.
//
// What falls out of that field shape, without being programmed in:
//
//  - With the tine on the centreline the field is symmetric about its peak, so
//    a symmetric swing crosses the peak twice per cycle. The output is
//    dominated by EVEN harmonics -- the bell-like Rhodes tone.
//  - Moving the tine off the centreline makes the swing asymmetric and the
//    fundamental returns. The service manual describes this exact adjustment,
//    and ISMA 2014 (section 2.2) records the same thing: toward the middle of
//    the wedge, more upper partials; toward the edge, more fundamental.
//  - A large swing runs the tine off the ends of the wedge, where the field
//    collapses. That clipping of the field, not any distortion in the
//    amplifier, is the growl -- which is why it appears in the bass, where the
//    tines move furthest, and only when played hard.
//
// The output is the electromotive force, which by Faraday's law is the rate of
// change of flux, so the field value is differentiated rather than used
// directly.
// ---------------------------------------------------------------------------
class MagneticPickup
{
public:
    struct Geometry
    {
        // All in metres. Defaults are a Rhodes pickup: a pole piece a few
        // millimetres across whose tip is ground to a wedge, sitting about a
        // millimetre and a half off the tine.
        float halfWidth   = 3.0e-3f;   // half the pole face, centreline to edge
        float flatHalf    = 0.5e-3f;   // half-width of the flat at the wedge tip
        float wedgeDepth  = 1.6e-3f;   // how far the surface recedes at the edge
        float nominalGap  = 1.5e-3f;   // tine-to-wedge-tip distance at rest
    };

    static constexpr int kNv = 257;   // across the wedge
    static constexpr int kNg = 33;    // along the gap

    // Table span. The tine's swing is allowed to run well past the pole face,
    // because on a hard bass note it does.
    static constexpr float kVSpan = 4.0f;   // in units of halfWidth
    static constexpr float kGMin  = 0.35f;  // in units of nominalGap
    static constexpr float kGMax  = 3.5f;

    void prepare (const Geometry& geo)
    {
        g = geo;
        vMax = kVSpan * g.halfWidth;
        gLo  = kGMin * g.nominalGap;
        gHi  = kGMax * g.nominalGap;

        for (int j = 0; j < kNg; ++j)
        {
            const float gg = gLo + (gHi - gLo) * static_cast<float> (j) / static_cast<float> (kNg - 1);
            for (int i = 0; i < kNv; ++i)
            {
                const float vv = -vMax + 2.0f * vMax * static_cast<float> (i)
                                       / static_cast<float> (kNv - 1);
                field[static_cast<size_t> (j) * kNv + static_cast<size_t> (i)] = integrate (vv, gg);
            }
        }

        // Normalise to the value at the centreline at the nominal gap, so the
        // table is a shape and the overall level lives in the coil model.
        const float ref = sample (0.0f, g.nominalGap);
        const float inv = std::abs (ref) > 1.0e-12f ? 1.0f / ref : 1.0f;
        for (auto& f : field) f *= inv;
    }

    // Flux seen by the coil for a tine tip at vertical offset `v` from the
    // wedge centreline and gap `gap`.
    float flux (float v, float gap) const { return sample (v, gap); }

    float nominalGap() const { return g.nominalGap; }
    float halfWidth()  const { return g.halfWidth; }
    float span()       const { return vMax; }

private:
    // Surface of the wedge: flat across the tip, then receding linearly to the
    // full depth at the edge of the pole face, then flat again (past the pole
    // piece there is nothing close to the tine at all).
    float surfaceGap (float v, float gap) const
    {
        const float a = std::abs (v);
        if (a <= g.flatHalf) return gap;
        const float t = std::min (1.0f, (a - g.flatHalf) / std::max (1.0e-9f, g.halfWidth - g.flatHalf));
        return gap + g.wedgeDepth * t;
    }

    // Coulombian line integral over the pole face. The charge is spread over
    // the face only: outside it there is no source, which is what makes the
    // field collapse when the tine swings clear of the magnet.
    float integrate (float vTine, float gap) const
    {
        constexpr int kSteps = 192;
        const float lo = -g.halfWidth, hi = g.halfWidth;
        const float dv = (hi - lo) / static_cast<float> (kSteps);

        float sum = 0.0f;
        for (int k = 0; k < kSteps; ++k)
        {
            const float v  = lo + (static_cast<float> (k) + 0.5f) * dv;
            const float gs = surfaceGap (v, gap) - gap;   // depth below the tip plane
            const float dz = gap + gs;                    // distance along the axis
            const float dvv = vTine - v;
            const float r2 = dvv * dvv + dz * dz;
            const float r  = std::sqrt (r2);
            sum += dz / std::max (1.0e-15f, r2 * r) * dv;
        }
        return sum;
    }

    float sample (float v, float gap) const
    {
        const float fv = (std::clamp (v, -vMax, vMax) + vMax) / (2.0f * vMax)
                       * static_cast<float> (kNv - 1);
        const float fg = (std::clamp (gap, gLo, gHi) - gLo) / std::max (1.0e-12f, gHi - gLo)
                       * static_cast<float> (kNg - 1);

        const int i0 = std::min (kNv - 2, static_cast<int> (fv));
        const int j0 = std::min (kNg - 2, static_cast<int> (fg));
        const float ti = fv - static_cast<float> (i0);
        const float tj = fg - static_cast<float> (j0);

        const size_t r0 = static_cast<size_t> (j0) * kNv;
        const size_t r1 = r0 + kNv;
        const float a = field[r0 + static_cast<size_t> (i0)];
        const float b = field[r0 + static_cast<size_t> (i0) + 1];
        const float c = field[r1 + static_cast<size_t> (i0)];
        const float d = field[r1 + static_cast<size_t> (i0) + 1];

        const float top = a + ti * (b - a);
        const float bot = c + ti * (d - c);
        return top + tj * (bot - top);
    }

    Geometry g;
    float vMax = 12.0e-3f, gLo = 0.5e-3f, gHi = 5.0e-3f;
    std::array<float, static_cast<size_t> (kNv) * kNg> field {};
};

// ---------------------------------------------------------------------------
// The coil, and the cable hanging off it.
//
// A pickup coil is not a wire: it is an inductor with several thousand turns,
// its own winding capacitance, and whatever the cable and the amplifier's
// input add to that. The result is a second-order lowpass with a resonant peak
// somewhere in the presence region, and that peak is a large part of why one
// pickup sounds unlike another. Ignoring it and calling the difference "EQ"
// gets the shape wrong at both ends.
//
//   R_dc  in series with  L,  loaded by  C  and the eddy-current loss R_p
//
// giving  f0 = 1/(2 pi sqrt(LC))  and a Q set by the ratio of the series
// resistance to the reactance. The iron in the pole piece also saturates, so
// the flux is soft-limited before it is differentiated -- a second and quite
// separate nonlinearity from the spatial one above.
// ---------------------------------------------------------------------------
class PickupCoil
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset() { s1 = s2 = 0.0f; fluxPrev = 0.0f; }

    // `freqHz` is the resonant peak, `q` its sharpness, `saturation` how hard
    // the core clips (0 = linear iron, 1 = a pole piece run into saturation).
    void setResponse (float freqHz, float q, float saturation)
    {
        const float f = std::clamp (freqHz, 200.0f, 0.45f * fs);
        gTan = std::tan (kPi * f / fs);
        qInv = 1.0f / std::clamp (q, 0.35f, 12.0f);
        sat  = std::clamp (saturation, 0.0f, 1.0f);
    }

    // Flux in, electromotive force out.
    float process (float fluxIn)
    {
        // Core saturation acts on the flux itself, before Faraday. A tanh with
        // its knee set by the saturation control: at zero the iron is
        // effectively linear over the range the tine drives it.
        float phi = fluxIn;
        if (sat > 0.0f)
        {
            const float k = 1.0f + 7.0f * sat;
            phi = std::tanh (k * phi) / k;
        }

        // Faraday: the coil responds to the RATE of flux change, not the flux.
        // A plain difference is the right differentiator here because the
        // resonant lowpass immediately after it removes the noise gain that
        // would otherwise make that a bad idea.
        const float emf = (phi - fluxPrev) * fs;
        fluxPrev = phi;

        // Topology-preserving-transform state variable filter, lowpass output:
        // stable at any cutoff and any Q, and the coefficients can be changed
        // between samples without a click.
        const float g1 = gTan;
        const float d  = 1.0f / (1.0f + g1 * (g1 + qInv));
        const float hp = (emf - (g1 + qInv) * s1 - s2) * d;
        const float bp = g1 * hp + s1;
        s1 = g1 * hp + bp;
        const float lp = g1 * bp + s2;
        s2 = g1 * bp + lp;

        return lp;
    }

private:
    float fs = 48000.0f;
    float gTan = 0.1f, qInv = 1.0f, sat = 0.0f;
    float s1 = 0.0f, s2 = 0.0f, fluxPrev = 0.0f;
};

} // namespace epi
