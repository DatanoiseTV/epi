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
// ---------------------------------------------------------------------------
class Room
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate)
    {
        fs = sampleRate;

        // Mutually prime lengths, spread over about a 4:1 range, scaled from a
        // 48 kHz reference so the room keeps its size at any sample rate. Prime
        // lengths matter: any common factor between two delays puts their
        // echoes on top of each other and the result rings at that period.
        static constexpr int kPrime48[kLines] =
            { 1327, 1613, 1949, 2273, 2617, 2939, 3271, 3593 };

        const double scale = fs / 48000.0;
        for (int i = 0; i < kLines; ++i)
        {
            const int n = std::max (16, static_cast<int> (std::lround (kPrime48[i] * scale)));
            line[i].assign (static_cast<std::size_t> (n), 0.0f);
            length[i] = n;
            pos[i] = 0;
        }

        // A short pre-delay: the instrument reaches the listener before the
        // walls do, and without the gap the tail smears into the attack.
        preLen = std::max (1, static_cast<int> (std::lround (0.012 * fs)));
        pre.assign (static_cast<std::size_t> (preLen), 0.0f);
        prePos = 0;

        reset();
        setSize (0.4f);
    }

    void reset()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        for (auto& d : damp) d = 0.0f;
        prePos = 0;
        for (int i = 0; i < kLines; ++i) pos[i] = 0;
    }

    // Size and decay are one control on the panel, because they are one thing
    // in a room: a bigger space rings longer. 0.6 s to about 4 s.
    void setSize (float sizeNorm)
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

    // Returns the wet pair. The caller mixes; keeping the dry path untouched
    // means the mix control cannot colour the instrument at zero.
    void process (double inL, double inR, double& wetL, double& wetR)
    {
        const float in = static_cast<float> (0.5 * (inL + inR));

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

private:
    double fs = 48000.0;

    std::array<std::vector<float>, kLines> line;
    std::array<int, kLines> length {};
    std::array<int, kLines> pos {};
    std::array<float, kLines> gain {};
    std::array<float, kLines> damp {};
    float dampCoef = 0.5f;

    std::vector<float> pre;
    int preLen = 1, prePos = 0;
};

} // namespace epi
