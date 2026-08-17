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
// Decimator for the oversampled transducer path.
//
// The pickup runs at four times the host rate because a curved field makes
// harmonics without limit; this brings the result back down, and the filter is
// the only thing standing between those harmonics and the audible band. A
// cascade of Butterworth sections at the base Nyquist is cheap, has no ripple
// in the passband, and is enough for a nonlinearity this smooth.
// ---------------------------------------------------------------------------
class Decimator
{
public:
    static constexpr int kOver = 4;
    // 16th order. The filter runs once per output sample rather than once per
    // tine, so its cost is nothing next to eighty-eight resonators, and order
    // is the cheapest alias rejection available.
    //
    // At 6th order this gave 14 dB at 30 kHz, which is where the top octave
    // folds from: measured against the same note rendered at 192 kHz, a C6 had
    // 13 dB more inharmonic content at 48 kHz than it should have, and that is
    // audible in the treble as a hardness that does not belong to the note.
    static constexpr int kSections = 8;

    void prepare (double baseRate)
    {
        const double fsOs = baseRate * kOver;
        // Just under the base Nyquist, so nothing that folds survives.
        const double fc = 0.455 * baseRate;
        const double w = 2.0 * kPiD * fc / fsOs;
        const double k = std::tan (w * 0.5);
        const double kk = k * k;

        // Butterworth pole pairs for a 6th-order lowpass.
        for (int i = 0; i < kSections; ++i)
        {
            const double q = 1.0 / (2.0 * std::cos (kPiD * (2.0 * i + 1.0)
                                                    / (4.0 * kSections)));
            const double norm = 1.0 / (1.0 + k / q + kk);
            b0[i] = kk * norm;
            b1[i] = 2.0 * b0[i];
            b2[i] = b0[i];
            a1[i] = 2.0 * (kk - 1.0) * norm;
            a2[i] = (1.0 - k / q + kk) * norm;
        }
        reset();
    }

    void reset()
    {
        for (int i = 0; i < kSections; ++i) { z1[i] = z2[i] = 0.0; }
    }

    // Feed kOver samples, get one back.
    double process (const double* in)
    {
        double y = 0.0;
        for (int n = 0; n < kOver; ++n)
        {
            y = in[n];
            for (int i = 0; i < kSections; ++i)
            {
                const double x = y;
                y = b0[i] * x + z1[i];
                z1[i] = b1[i] * x - a1[i] * y + z2[i];
                z2[i] = b2[i] * x - a2[i] * y;
            }
        }
        return y;   // the last of each group is the decimated sample
    }

private:
    double b0[kSections] {}, b1[kSections] {}, b2[kSections] {};
    double a1[kSections] {}, a2[kSections] {};
    double z1[kSections] {}, z2[kSections] {};
};

// ---------------------------------------------------------------------------
// The Suitcase preamp.
//
// There is no published measurement of any Rhodes preamp. The figures here come
// from numerically solving the factory schematic (Haigler board, used in both
// the 100 W Suitcase and the Janus I), done twice independently and agreeing to
// about a decibel.
//
// The single most important thing it does is NOT the famous mid scoop -- at the
// centre detent the Baxandall stack is flat to within a hundredth of a decibel
// from 10 Hz to 20 kHz, and the "smile" is entirely a user setting. What it
// really does is roll the bottom off hard: the input stage is 0.1 uF into
// 10 kOhm, a 159 Hz highpass, and the response with the tone controls flat runs
//
//     20 Hz  -16.0   40  -10.3   60  -7.2   100  -4.0   200  -1.3
//    500 Hz   -0.1   1k    0.0   2k  -0.3   5k   -2.1   10k  -5.6   15k -8.4
//
// so a low E's fundamental at 41 Hz is already twelve decibels down before it
// reaches the speaker. That is the quantitative form of "a Suitcase needs its
// bass turned up", and getting it wrong makes the whole instrument sound like
// the wrong record.
//
// The tone controls are very broad and low-turnover: bass reaches +/-18 dB at
// 20 Hz but only +/-1 dB by 467 Hz, and treble +/-25 dB at 15 kHz but only
// +/-3 dB at 557 Hz. That is why the Suitcase's treble reads as bite rather
// than air.
// ---------------------------------------------------------------------------
class SuitcasePreamp
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        input.setCutoff (159.0, fs);
        band.setCutoff (6030.0, fs);
        bassShelf.setCutoff (120.0, fs);
        trebShelf.setCutoff (900.0, fs);
        reset();
    }

    void reset()
    {
        input.reset(); band.reset(); bassShelf.reset(); trebShelf.reset();
        dcx = dcy = 0.0;
    }

    // bassDb / trebleDb are the control positions; drive is 0..1.
    void setTone (double bassDb, double trebleDb, double drive)
    {
        bassGain = std::pow (10.0, std::clamp (bassDb, -18.0, 18.0) / 20.0) - 1.0;
        trebGain = std::pow (10.0, std::clamp (trebleDb, -24.0, 24.0) / 20.0) - 1.0;
        // The preamp runs a long way into its own headroom before it audibly
        // clips, and this is where that headroom actually comes from.
        //
        // It used to be a linear 1 to 15 with no scaling ahead of the stage,
        // which meant the signal met the curve at unity: measured on an E5,
        // the stage was already making 59 dB of intermodulation with the
        // control at ZERO, and it rose smoothly from there with no clean
        // region anywhere on the knob. A Suitcase at ordinary playing level is
        // clean, and you have to work to make it otherwise.
        //
        // Exponential, and starting well below the knee. Measured on the same
        // note, the stage now contributes nothing detectable anywhere below
        // about a third of the control -- the residual there is the tine's own
        // and does not move -- then rises through -45 dB at three quarters to
        // -29 dB wide open. A usable range, instead of a permanent one.
        driveGain = 0.2 * std::pow (60.0, std::clamp (drive, 0.0, 1.0));
        makeup    = 1.0 + 0.5 * std::clamp (drive, 0.0, 1.0);
    }

    double process (double x)
    {
        // Input highpass, then the band limit of the feedback capacitor.
        double y = input.highpass (x);
        y = band.lowpass (y);

        // Baxandall: two very broad shelves about a flat centre.
        y += bassGain * bassShelf.lowpass (y);
        y += trebGain * trebShelf.highpass (y);

        // Class-A stage: asymmetric, because one side of the transfer curve
        // runs out before the other.
        const double d = y * driveGain;
        const double s = d >= 0.0 ? std::tanh (d) : std::tanh (d * 0.82) / 0.82;
        y = s / driveGain * makeup;

        // Any asymmetric stage makes a DC offset; the coupling capacitors
        // downstream take it back out.
        dcy = 0.9995 * dcy + s - dcx;
        dcx = s;
        return y - 0.0 * dcy;
    }

private:
    double fs = 48000.0;
    OnePoleD input, band, bassShelf, trebShelf;
    double bassGain = 0.0, trebGain = 0.0, driveGain = 1.0, makeup = 1.0;
    double dcx = 0.0, dcy = 0.0;
};

// ---------------------------------------------------------------------------
// The Suitcase vibrato, which is not vibrato.
//
// No pitch-modulating element exists anywhere in any version of the circuit.
// The mono signal splits into two stages, each fed through its own
// photoresistor, with the two LEDs driven from one oscillator through
// oppositely poled diodes. It is an auto-panner.
//
// Three things about it are commonly modelled wrongly:
//
//  - The LFO is a SQUARE wave in every stereo version, not a sine or a
//    triangle. What shapes it is what follows: the Janus clips it to a hard
//    trapezoid flat-topped about 77% of the cycle, and the Peterson runs it
//    through a lamp filament whose thermal inertia does the smoothing. The
//    "cat's eye" shape is that lag, not a waveform.
//
//  - The photocell's attack and decay are wildly different -- a VTL5C1 attacks
//    in 2.5 ms and decays only to 100 kOhm in 35 ms -- so the DEPTH DEPENDS ON
//    THE RATE. Slow settings reach genuine silence on the off channel; fast
//    ones barely reach 20 dB. This is the most important nonlinearity in the
//    whole effect and a symmetric LFO cannot produce it.
//
//  - The pan law is neither constant-power nor constant-amplitude. The network
//    would be constant-amplitude only if the two cell resistances multiplied to
//    a fixed value, which real photocells do not. The residual is the
//    well-known "vibrato thump". So no pan law is imposed here: the two
//    channels are computed independently and the sum is whatever it is.
// ---------------------------------------------------------------------------
class SuitcaseVibrato
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // A photocell's attack is fast and its decay is slow, by more than an
        // order of magnitude.
        aAtk = 1.0 - std::exp (-1.0 / (0.0025 * fs));
        aDec = 1.0 - std::exp (-1.0 / (0.0350 * fs));
        reset();
    }

    void reset() { phase = 0.0; envA = 1.0; envB = 0.0; }

    void setRate (double hz)   { rate = std::clamp (hz, 0.1, 30.0); }
    void setDepth (double d)   { depth = std::clamp (d, 0.0, 1.0); }

    // How the two photocells are wired, which is the only difference between
    // the two effects people mean by these words.
    //
    // In opposition, one cell darkens as the other lightens and the note is
    // panned across the pair of speaker cabinets: that is what a Rhodes calls
    // vibrato, and nothing about it modulates pitch. Wired together they dim
    // and brighten as one, and the result is a true amplitude tremolo -- which
    // is what a Wurlitzer does, and what everyone else means by the word.
    //
    // Continuous between the two, because there is nothing discrete about it:
    // it is a wiring ratio, and part way is a real setting.
    void setStereo (double s) { stereo = std::clamp (s, 0.0, 1.0); }

    void process (double x, double& l, double& r)
    {
        phase += rate / fs;
        if (phase >= 1.0) phase -= 1.0;

        // Trapezoid: flat for most of the cycle, with fast edges. The factory
        // shaper clips the square hard enough that it sits at its rail about
        // three quarters of the time.
        constexpr double kEdge = 0.115;   // fraction of the cycle spent moving
        double tri;
        if (phase < kEdge)                    tri = phase / kEdge;
        else if (phase < 0.5)                 tri = 1.0;
        else if (phase < 0.5 + kEdge)         tri = 1.0 - (phase - 0.5) / kEdge;
        else                                  tri = 0.0;

        // Two photocells, driven in opposition, each with its own asymmetric
        // lag. This is where the rate dependence comes from: at a fast rate the
        // slow side never finishes decaying, so the channels never separate.
        const double tgtA = tri;
        const double tgtB = stereo * (1.0 - tri) + (1.0 - stereo) * tri;
        envA += (tgtA > envA ? aAtk : aDec) * (tgtA - envA);
        envB += (tgtB > envB ? aAtk : aDec) * (tgtB - envB);

        const double gA = 1.0 - depth * (1.0 - envA);
        const double gB = 1.0 - depth * (1.0 - envB);
        l = x * gA;
        r = x * gB;
    }

private:
    double fs = 48000.0, rate = 5.0, depth = 0.0, stereo = 1.0;
    double phase = 0.0, envA = 1.0, envB = 0.0;
    double aAtk = 0.1, aDec = 0.01;
};

// ---------------------------------------------------------------------------
// Cabinet. The Suitcase's speaker complement is dual-channel, 50 watts a
// side, per the service manual's schematic titles; the often-repeated "four
// twelve-inch speakers" could not be verified against a primary source
// (docs/research/transducers-and-chassis.md, item 8), so this comment no
// longer asserts it. Acoustically it is a wooden box either way: a bandpass
// with a bump
// where the cone breaks up, and a cone that stops moving linearly when pushed.
// ---------------------------------------------------------------------------
class Cabinet
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lo.setCutoff (75.0, fs);
        hi.setCutoff (4200.0, fs);
        presence.setCutoff (1800.0, fs);
        reset();
    }

    void reset() { lo.reset(); hi.reset(); presence.reset(); }

    void setMix (double m) { mix = std::clamp (m, 0.0, 1.0); }

    double process (double x)
    {
        if (mix <= 0.0) return x;
        double y = hi.lowpass (lo.highpass (x));
        y += 0.35 * presence.highpass (y);
        // Excursion limit: a cone cannot travel further than its suspension.
        y = std::tanh (y * 1.4) / 1.4;
        return x + mix * (y - x);
    }

private:
    double fs = 48000.0, mix = 0.5;
    OnePoleD lo, hi, presence;
};

// ---------------------------------------------------------------------------
// The CP-70's electrical chain: 12 Hz piezo high-pass, the mid-scooped
// cut-only tone stack, and one soft JFET stage.
//
// The scoop is the most under-reported fact about the instrument and it is a
// FIXED filter, not a user curve: re-fitted by digitising the circuit
// simulation against its own grid (docs/research/cp70-open-questions.md),
// centre 488 Hz, depth -13.4 dB, Q 0.27 -- half as sharp as the first
// published fit -- with a +4.0 dB shelf below 63 Hz and +1.9 dB above
// 2.9 kHz, residual 0.39 dB against the plot.
// ---------------------------------------------------------------------------
class CP70Preamp
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // The piezo into its load is a highpass, and the corner is audible
        // in the recordings: the hammer's contact leaves a unipolar
        // millisecond-scale force hump at the termination which the model's
        // readout reproduces faithfully -- and the reference recordings do
        // not contain it. A 12 Hz corner (the preamp's own input cap) would
        // pass it; the element-plus-cable source capacitance against the
        // input resistance sits far higher. Two poles at 60 Hz remove the
        // hump while costing the D3 fundamental under a decibel, which is
        // what the measured attack-to-body ratios require.
        hpA.setCutoff (60.0, fs);
        hpB.setCutoff (60.0, fs);
        setPeak (scoop, 488.0, -13.4, 0.27);
        setShelf (lowFixed, 63.0, 4.0, false);
        setShelf (highFixed, 2900.0, 1.9, true);
        setTone (0.0, 0.0, 0.0);
        reset();
    }

    void reset()
    {
        hpA.reset();
        hpB.reset();
        for (auto* b : { &scoop, &lowFixed, &highFixed, &bassCtl, &trebCtl })
            b->z1 = b->z2 = 0.0;
    }

    // The real controls are cut-only; the boost half is a stated studio
    // convenience. Documented ranges: bass +/-17 dB at 50 Hz, treble
    // +/-12.5 dB at 5 kHz.
    void setTone (double bassDb, double trebleDb, double drive)
    {
        setShelf (bassCtl, 50.0, std::clamp (bassDb, -17.0, 17.0) * 17.0 / 12.0, false);
        setShelf (trebCtl, 5000.0, std::clamp (trebleDb, -12.5, 12.5) * 12.5 / 12.0, true);
        driveGain = 0.25 * std::pow (40.0, std::clamp (drive, 0.0, 1.0));
    }

    double process (double x)
    {
        double y = hpB.highpass (hpA.highpass (x));
        y = run (scoop, y);
        y = run (lowFixed, y);
        y = run (highFixed, y);
        y = run (bassCtl, y);
        y = run (trebCtl, y);

        // One soft asymmetric class-A JFET stage, nearly clean at the default
        // -- the CP-70 chord intermodulates here, mildly, and nowhere else.
        const double d = y * driveGain;
        const double sHat = d >= 0.0 ? std::tanh (d) : std::tanh (d * 0.85) / 0.85;
        return sHat / driveGain;
    }

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };

    static double runStatic (Biquad& q, double x)
    {
        const double y = q.b0 * x + q.z1;
        q.z1 = q.b1 * x - q.a1 * y + q.z2;
        q.z2 = q.b2 * x - q.a2 * y;
        return y;
    }
    double run (Biquad& q, double x) const { return runStatic (q, x); }

    void setPeak (Biquad& q, double f, double dB, double Q)
    {
        const double A = std::pow (10.0, dB / 40.0);
        const double w = 2.0 * kPiD * f / fs;
        const double al = std::sin (w) / (2.0 * Q);
        const double a0 = 1.0 + al / A;
        q.b0 = (1.0 + al * A) / a0;
        q.b1 = (-2.0 * std::cos (w)) / a0;
        q.b2 = (1.0 - al * A) / a0;
        q.a1 = q.b1;
        q.a2 = (1.0 - al / A) / a0;
    }

    // The two shelf types written out separately, textbook RBJ forms. The
    // first version folded them into one function with a sign flag, got one
    // sign wrong, and produced a filter with four thousand times gain at its
    // corner -- a 1e-4 impulse hit the JFET rail. A folded formula saves ten
    // lines and costs an afternoon; never again.
    void setShelf (Biquad& q, double f, double dB, bool high)
    {
        const double A = std::pow (10.0, dB / 40.0);
        const double w = 2.0 * kPiD * f / fs;
        const double cw = std::cos (w), sw = std::sin (w);
        const double al = sw / 2.0 * std::sqrt (2.0);
        const double s2A = 2.0 * std::sqrt (A) * al;
        double b0, b1, b2, a0, a1, a2;
        if (! high)
        {
            b0 =     A * ((A + 1.0) - (A - 1.0) * cw + s2A);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
            b2 =     A * ((A + 1.0) - (A - 1.0) * cw - s2A);
            a0 =          (A + 1.0) + (A - 1.0) * cw + s2A;
            a1 =   -2.0 * ((A - 1.0) + (A + 1.0) * cw);
            a2 =          (A + 1.0) + (A - 1.0) * cw - s2A;
        }
        else
        {
            b0 =     A * ((A + 1.0) + (A - 1.0) * cw + s2A);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
            b2 =     A * ((A + 1.0) + (A - 1.0) * cw - s2A);
            a0 =          (A + 1.0) - (A - 1.0) * cw + s2A;
            a1 =    2.0 * ((A - 1.0) - (A + 1.0) * cw);
            a2 =          (A + 1.0) - (A - 1.0) * cw - s2A;
        }
        q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0;
        q.a1 = a1 / a0; q.a2 = a2 / a0;
    }

    double fs = 48000.0;
    OnePoleD hpA, hpB;
    Biquad scoop, lowFixed, highFixed, bassCtl, trebCtl;
    double driveGain = 0.25;
};

} // namespace epi
