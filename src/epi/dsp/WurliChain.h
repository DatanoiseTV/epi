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
// The 200A's shared electrical path: one reed-bar node, one highpass, a
// two-transistor preamp and an optocoupler amplitude tremolo. Everything a
// single reed cannot own lives here.
//
// The topology decides the factoring. All 64 reeds vibrate through cutouts in
// ONE comb plate, so all 64 gaps hang on one ~240 pF node behind one resistor
// network. One reed modulates that node by C0n/C_total ~ 1.3% at rest -- the
// node is a stiff voltage source to any single reed -- so the node equation
// linearises in the SIGNAL while keeping y exact:
//
//   v_out = -(u0 / C_total) * H(s) * SUM_n DC_n,   DC_n = C0n * y_n/(1 - y_n)
//   H(s)  = s R C_total / (1 + s R C_total)
//
// Per-reed memoryless nonlinearity, linear sum, one shared first-order
// highpass. This makes chord behaviour exact (superposition at the node) and
// keeps the measured register shaping: H2/H1 still falls above the corner
// because H(2f)/H(f) tends to 1 there. The alternative -- a time-varying RC
// per voice, openwurli's form -- implicitly gives each reed the full 240 pF,
// right for a lone reed on its own plate, not for a comb.
// ---------------------------------------------------------------------------
class WurliPickupBus
{
public:
    // (1M || 402k) x 240 pF = 287 kOhm x 240 pF -> 2312 Hz, read from the
    // 200A schematic #203720-S-3 (the 1 MOhm HV feed, component 56) against
    // the 200A input network (22k + 380k). The often-quoted 2834 Hz pairs the
    // Series 200's 560k R-56 with the 200A's input network -- a chimera of
    // two electrically different instruments. The honest residual is C_total
    // itself: 240 pF is single-sourced; the geometric estimate (64 x 2-4 pF
    // + wiring) is consistent, and one LCR reading of a real reed bar closes
    // it (open question 1). R and C stay internal constants, not knobs.
    static constexpr double kCornerHz = 2312.0;
    static constexpr double kCTotalPf = 240.0;

    // Runs at the oversampled rate: pass fs * kOver.
    void prepare (double rate)
    {
        hp.setCutoff (kCornerHz, rate);
        reset();
    }

    void reset() { hp.reset(); }

    // The polarizing rail. +150 V nominal on the 200A ("BLACK +150V TO PREAM
    // & REED BAR" on the assembly drawing); Pfeifle measured a sagging unit
    // at 130 V; 160-170 V is the Series 200's hotter rail, inside the range
    // deliberately. Scales sensitivity linearly -- a physical drive control.
    // No back-action: softening is -0.077 cents at the 0.5 mm gap, and the
    // gap knob floors at 0.3 mm to keep the pure-sensor claim true.
    void setBias (double volts) { u0 = std::clamp (volts, 100.0, 200.0); }

    // Summed capacitance perturbation in pF -> volts at the preamp input.
    double process (double sumDcPf)
    {
        return hp.highpass (sumDcPf) * (u0 / kCTotalPf);
    }

private:
    OnePoleD hp;
    double u0 = 150.0;
};

// ---------------------------------------------------------------------------
// The 200A preamp. Solid state, definitively: the 200A is transistors end to
// end (two-transistor reed-bar preamp, +15 V rail); tube Wurlitzers are the
// earlier 100-series, and no tube stage is modelled or missed.
//
// Small dedicated stage, not the Suitcase circuit: input coupling highpass
// ~18 Hz (0.022 uF into 402 kOhm), no-vibrato gain ~14 dB (openwurli's
// corrected divider analysis, agreeing with Avenson's ~15 dB measurement),
// one asymmetric soft-clip with headroom ~2 V toward saturation against
// ~11 V toward cutoff, and the ~15.5 kHz bandwidth of the 100 pF Miller
// caps. The real 200A has no tone controls -- only volume and vibrato -- so
// bass/treble stay a labelled studio convenience for the engine's shared
// parameters and are not part of this class.
// ---------------------------------------------------------------------------
class WurliPreamp
{
public:
    void prepare (double rate)
    {
        input.setCutoff (18.0, rate);
        miller.setCutoff (15500.0, rate);
        reset();
    }

    void reset() { input.reset(); miller.reset(); }

    // drive 0..1 scales the signal into the clip knee exponentially, default
    // nearly clean. At kStockDrive the stage sees exactly the circuit's own
    // level: 14 dB of gain into the real 2 V / 11 V headroom split.
    static constexpr double kStockDrive = 0.31;
    void setDrive (double d)
    {
        driveGain = 0.25 * std::pow (40.0, std::clamp (d, 0.0, 1.0));
    }

    double process (double x)
    {
        double y = input.highpass (x) * kGain * driveGain;

        // The asymmetric stage: the collector runs out ~2 V above the
        // operating point and ~11 V below it, so positive excursions
        // saturate five times earlier than negative ones cut off. This is
        // where the even harmonics of a hard-driven bass note come from
        // downstream of the pickup's own asymmetry.
        y = y >= 0.0 ? kSatPos * std::tanh (y / kSatPos)
                     : kSatNeg * std::tanh (y / kSatNeg);

        y = miller.lowpass (y);
        return y / driveGain;
    }

private:
    static constexpr double kGain   = 5.01;   // +14 dB
    static constexpr double kSatPos = 2.0;    // V toward saturation
    static constexpr double kSatNeg = 11.0;   // V toward cutoff

    OnePoleD input, miller;
    double driveGain = 0.25 * std::pow (40.0, kStockDrive);
};

// ---------------------------------------------------------------------------
// The tremolo, which on a Wurlitzer really is one: the LED/LDR optocoupler
// shunts a feedback divider, modulating GAIN, not pan. Both channels ride the
// same gain -- stereo Wurli is a labelled studio trick the engine may add on
// top, not something this circuit can do.
//
// Two figures with a home: the twin-T oscillator is near-sinusoidal at
// ~5.6 Hz (the famous 5.75 Hz belongs to the Series 200's phase-shift
// oscillator -- a different instrument -- and is not cross-applied), and full
// depth is ~7.3 dB peak-to-peak of gain, openwurli's Rust 7.33 dB against
// ngspice's 7.31 dB on the real divider network.
//
// The photocell's asymmetric lag (VTL5C-class: ~2.5 ms attack, ~35 ms decay,
// the LG-1's figures) is kept, so the depth genuinely falls at fast rates.
// The swing constant below is calibrated so the MEASURED gain trace at the
// stock 5.6 Hz spans 7.3 dB after that lag, not before it.
//
// Flagged, not shipped: the LDR sits inside the preamp feedback, so gain,
// bandwidth and distortion breathe together on the real unit; this launches
// as pure gain AM, to be revisited on A/B evidence.
// ---------------------------------------------------------------------------
class WurliTremolo
{
public:
    void prepare (double rate)
    {
        fs = rate;
        aAtk = 1.0 - std::exp (-1.0 / (0.0025 * fs));
        aDec = 1.0 - std::exp (-1.0 / (0.0350 * fs));
        reset();
    }

    void reset() { phase = 0.0; env = 1.0; }

    void setRate (double hz)  { rate = std::clamp (hz, 0.1, 30.0); }
    void setDepth (double d)  { depth = std::clamp (d, 0.0, 1.0); }

    // One gain per sample; the caller applies it to both channels alike.
    double gain()
    {
        phase += rate / fs;
        if (phase >= 1.0) phase -= 1.0;

        const double tgt = 0.5 + 0.5 * std::sin (2.0 * kPiD * phase);
        env += (tgt > env ? aAtk : aDec) * (tgt - env);

        const double db = -kSwingDb * depth * (1.0 - env);
        return std::pow (10.0, db / 20.0);
    }

private:
    // Pre-lag swing sized so the post-lag trace at 5.6 Hz measures 7.3 dB
    // peak-to-peak at full depth (row M2 is the measurement; 9.6 measured
    // 7.70 dB through the lag, this value lands 7.3).
    static constexpr double kSwingDb = 9.1;

    double fs = 48000.0, rate = 5.6, depth = 0.0;
    double phase = 0.0, env = 1.0;
    double aAtk = 0.1, aDec = 0.01;
};

// ---------------------------------------------------------------------------
// Speaker voicing for the shared Cabinet class: two 4x8 in oval
// ceramic-magnet speakers in the open-back lid. No new cabinet is built --
// these are the recommended setVoicing() constants, chosen against the
// plan's corners: highpass ~95 Hz with Q ~0.75 (open-baffle cancellation
// plus the driver resonance bump), lowpass ~5.5 kHz (openwurli's A/B against
// recordings puts the treble centroid there, not at the nominal 7.5 k).
//
// The Cabinet ties its box corner and Q to one knob (a small box is both
// higher and peakier), so the two targets cannot be hit exactly at once:
// box = 0.62 lands fc ~83 Hz with Q ~0.90, splitting the difference toward
// the resonance bump the open-back lid really has. cone = 0.90 puts the
// breakup at 5.5 kHz exactly. The small oval frames have short suspension
// travel (susp 0.35, an early excursion limit is the 200A's onboard grind),
// heard slightly off-axis in the room rather than close-miked.
//
// cabMix defaults 0.7 in Wurlitzer presets: the onboard speakers are the
// canonical 200A sound; cabMix = 0 is the (also real) aux/DI path.
// ---------------------------------------------------------------------------
namespace wurliSpeaker
{
    inline constexpr double kBox   = 0.62;
    inline constexpr double kCone  = 0.90;
    inline constexpr double kDist  = 0.60;
    inline constexpr double kAngle = 0.35;
    inline constexpr double kSusp  = 0.35;
    inline constexpr double kMix   = 0.70;
}

} // namespace epi
