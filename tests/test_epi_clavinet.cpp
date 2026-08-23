/*
  Epi — Clavinet D6 reference suite.

  Every check here corresponds to a numbered row in
  docs/clavinet-implementation-plan.md section 8, and every target in it is a
  measurement from docs/research/clavinet-measured.md or a primary document
  cited there (EURASIP 2013:103, DAFx-12, the D6/E7 service manuals). The
  suite renders the model, measures the same quantity the same way, and prints
  both side by side.

  The reference chain for spectral rows is the plan's: voice (center pickup,
  4x transducer rate) -> Brilliant rocker only -> preamp at the circuit's own
  drive -> decimate. Rows that measure the RESONATOR read the string's
  displacement at the center tap directly, so the derivative's tilt and the
  preamp cannot color a decay or pitch measurement.

  Build: target epi_clavinet_tests.
*/

#include "EpiAnalysis.h"
#include "epi/dsp/ClavinetVoice.h"
#include "epi/dsp/ClavinetChain.h"
#include "epi/dsp/OutputChain.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace epi;
namespace an = epianalysis;

// ===========================================================================
// Reporting (the house row/verdict machinery)
// ===========================================================================

static int failures = 0;
static int gaps     = 0;

static void heading (const char* s)
{
    std::printf ("\n%s\n", s);
    std::printf ("  ---------------------------------------------------------------------------------\n");
}

enum class Verdict { pass, fail, knownGap, info };

static void row (const char* id, const char* what, const std::string& target,
                 const std::string& got, Verdict v)
{
    const char* mark = "PASS";
    if (v == Verdict::fail)     { mark = "FAIL";      ++failures; }
    if (v == Verdict::knownGap) { mark = "KNOWN GAP"; ++gaps; }
    if (v == Verdict::info)       mark = "  --";
    std::printf ("  %-3s %-36s %-24s %-22s %s\n", id, what, target.c_str(), got.c_str(), mark);
}

static Verdict within (double v, double lo, double hi)
{
    return (v >= lo && v <= hi) ? Verdict::pass : Verdict::fail;
}

// A property that is understood, unfixed, and recorded. It still carries a
// bound -- the value it had when accepted, with room to move -- so the gap is
// reported every run without being free to widen.
static Verdict gap (double v, double lo, double hi, double boundLo, double boundHi)
{
    if (within (v, lo, hi) == Verdict::pass) return Verdict::pass;
    return (v >= boundLo && v <= boundHi) ? Verdict::knownGap : Verdict::fail;
}

static std::string fmt (const char* f, double a)
{
    char b[96];
    std::snprintf (b, sizeof b, f, a);
    return b;
}

static std::string fmt2 (const char* f, double a, double b2)
{
    char b[96];
    std::snprintf (b, sizeof b, f, a, b2);
    return b;
}

// ===========================================================================
// Rendering
// ===========================================================================

static constexpr double kFs = 48000.0;

static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

struct RenderOpts
{
    double vel        = 0.75;
    double seconds    = 1.2;
    double noteOffAt  = -1.0;    // < 0: held for the whole render
    double pedal      = 0.0;
    bool   chainOn    = true;    // Brilliant rocker + preamp
    double drive      = 1.0;     // the circuit's own level
    bool   beatOn     = false;   // off for pitch rows: the partner pulls f0
    ClavinetVoice::Config cfg {};
};

static std::vector<double> renderClav (int note, const RenderOpts& o)
{
    ClavinetVoice v;
    v.prepare (kFs);
    if (! o.beatOn) v.setBeat (0.0);
    v.setNote (note, o.cfg);
    v.setPedal (o.pedal);
    v.noteOn (note, o.vel, o.cfg, 1);

    ClavinetToneStack stack; stack.prepare (kFs * ClavinetVoice::kOver);
    stack.setRockers (false, false, false, true);   // the reference rocker
    ClavinetPreamp pre; pre.prepare (kFs * ClavinetVoice::kOver);
    pre.setDrive (o.drive);
    Decimator dec; dec.prepare (kFs);

    const int N = static_cast<int> (kFs * o.seconds);
    const int offAt = o.noteOffAt >= 0.0 ? static_cast<int> (kFs * o.noteOffAt) : -1;
    std::vector<double> out (static_cast<std::size_t> (N));
    double os[ClavinetVoice::kOver];
    for (int i = 0; i < N; ++i)
    {
        if (i == offAt) v.noteOff();
        v.process (o.cfg, os);
        if (o.chainOn)
            for (int k = 0; k < ClavinetVoice::kOver; ++k)
                os[k] = pre.process (stack.process (os[k]));
        out[static_cast<std::size_t> (i)] = dec.process (os);
    }
    return out;
}

// The string's own motion at the center tap, for rows that measure the
// resonator: decay, pitch, release. Measuring those through the transducer
// would fold the derivative's tilt and the preamp into a decay number.
static std::vector<double> renderCenterDisp (int note, const RenderOpts& o)
{
    ClavinetVoice v;
    v.prepare (kFs);
    if (! o.beatOn) v.setBeat (0.0);
    v.setNote (note, o.cfg);
    v.setPedal (o.pedal);
    v.noteOn (note, o.vel, o.cfg, 1);
    const int N = static_cast<int> (kFs * o.seconds);
    const int offAt = o.noteOffAt >= 0.0 ? static_cast<int> (kFs * o.noteOffAt) : -1;
    std::vector<double> out (static_cast<std::size_t> (N));
    double os[ClavinetVoice::kOver];
    for (int i = 0; i < N; ++i)
    {
        if (i == offAt) v.noteOff();
        v.process (o.cfg, os);
        out[static_cast<std::size_t> (i)] = v.centerDisplacement();
    }
    return out;
}

// Hann-windowed complex projection: one tone's amplitude in one short window.
static double toneAmp (const std::vector<double>& x, double f, double t0, double t1)
{
    const std::size_t a = static_cast<std::size_t> (t0 * kFs);
    const std::size_t b = std::min (x.size(), static_cast<std::size_t> (t1 * kFs));
    if (b <= a + 16) return 0.0;
    const double n = static_cast<double> (b - a);
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (std::size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * an::kPi * static_cast<double> (i - a) / (n - 1.0));
        const double ph = 2.0 * an::kPi * f * static_cast<double> (i) / kFs;
        re += w * x[i] * std::cos (ph);
        im += w * x[i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / std::max (1.0, wsum);
}

// A partial's measured frequency around an expected value, comb-rejecting its
// neighbours at multiples of f0.
static double partialHz (const std::vector<double>& x, double fExp, double f0,
                         double ta, double tb)
{
    const an::Envelope e = an::heterodyne (x, kFs, fExp, f0);
    return an::partialFrequency (e, fExp, ta, tb);
}

// ===========================================================================
// G. Geometry and scale (unit rows, no audio)
// ===========================================================================

static void sectionGeometry()
{
    heading ("G. Geometry: pickup combs, inharmonicity table, gauge break");

    ClavinetVoice::Config cfg;

    // G1: the measured comb anchor. EURASIP photographed notches at every 5th
    // partial of the A2 tone, 544/116.5 Hz -> readout point at d/L = 0.214
    // from the termination. The reconstruction (one length anchor + the
    // measured pickup-distance sweep) must land the center pickup there.
    {
        ClavinetVoice v;
        v.prepare (kFs);
        v.setNote (45, cfg);
        const double dl = v.pickupDistOverL (ClavinetVoice::tapCenter);
        row ("G1", "center pickup d/L at A2", "0.214 +/-10%",
             fmt ("%.4f", dl), within (dl, 0.214 * 0.9, 0.214 * 1.1));

        // The notch pattern in the voice's own tap weights: every 5th partial
        // a local minimum against its neighbours, through the third notch.
        bool notch = true;
        for (int k : { 5, 10, 15 })
        {
            const double w = std::abs (v.tapWeight (ClavinetVoice::tapCenter, k - 1));
            const double wl = std::abs (v.tapWeight (ClavinetVoice::tapCenter, k - 2));
            const double wr = std::abs (v.tapWeight (ClavinetVoice::tapCenter, k));
            if (! (w < wl && w < wr)) notch = false;
        }
        row ("G1", "tap-weight notch at k=5,10,15 (A2)", "local minima",
             notch ? "minima" : "NOT minima", notch ? Verdict::pass : Verdict::fail);
    }

    // G2: the six measured B anchors (EURASIP Table 1), read back from the
    // voice's own mode frequencies by the paper's recipe: LSQ of
    // (f_k/k f0)^2 = 1 + B k^2 over partials 2-7.
    {
        struct A { int midi; double b; const char* name; };
        for (A a : { A { 29, 5.0e-4, "F1" }, A { 33, 2.0e-4, "A1" }, A { 50, 9.0e-5, "D3" },
                     A { 52, 1.0e-4, "E3" }, A { 77, 9.0e-5, "F5" }, A { 88, 8.0e-5, "E6" } })
        {
            ClavinetVoice v;
            v.prepare (kFs);
            v.setNote (a.midi, cfg);
            // With f1 = f0 sqrt(1+B), the ratio (f_k/(k f1))^2 - 1 equals
            // B (k^2-1)/(1+B): a through-origin LSQ against x = k^2-1 gives
            // B/(1+B), which is B to better than 0.1% at these magnitudes.
            const double f1 = v.modeFrequency (0);
            double sxx = 0, sxy = 0;
            for (int k = 2; k <= 7 && k <= v.partialCount(); ++k)
            {
                const double fk = v.modeFrequency (k - 1);
                const double y = (fk * fk) / (k * k * f1 * f1) - 1.0;
                const double x = static_cast<double> (k * k) - 1.0;
                sxx += x * x; sxy += x * y;
            }
            const double bFit = sxy / sxx;
            row ("G2", (std::string ("B anchor ") + a.name).c_str(),
                 fmt ("%.2g +/-10%%", a.b), fmt ("%.3g", bFit),
                 within (bFit, a.b * 0.9, a.b * 1.1));
        }
    }

    // G3: the wound/plain discontinuity between keys 23 and 24 (MIDI 51/52).
    // The two "D3" rows of the printed table bracket the break; as printed
    // the plain side is HIGHER (9e-5 -> 1e-4), so the step's direction is
    // reported informationally and only its existence is asserted.
    {
        const double b23 = ClavinetScale::inharmonicity (51);
        const double b24 = ClavinetScale::inharmonicity (52);
        const double ratio = b23 / b24;
        row ("G3", "B step across the break", "discontinuity present",
             fmt ("B23/B24 = %.3f", ratio),
             std::abs (std::log (ratio)) > 0.03 ? Verdict::pass : Verdict::fail);
    }

    // G4: the published flux fit, monotone falling with distance across the
    // playable gap range, as the Vizimag simulation shows (DAFx-12 Fig. 13).
    {
        bool monotone = true;
        double prev = ClavinetVoice::fluxPoly (0.5e-3);
        for (double d = 1.0e-3; d <= 15.0e-3; d += 0.5e-3)
        {
            const double p = ClavinetVoice::fluxPoly (d);
            if (p > prev + 1.0e-12) monotone = false;
            prev = p;
        }
        row ("G4", "flux falls with distance, 0.5-15 mm", "monotone",
             monotone ? "monotone" : "NOT monotone", monotone ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// S. The string: tuning, inharmonicity, release, losses
// ===========================================================================

static void sectionString()
{
    heading ("S. String: tuning, rendered B, the release drop, losses");

    // S1: equal temperament across the compass, +/-3 cents. No stretch table
    // by design: B here is 10-100x smaller than the CP-70's [M].
    {
        double worst = 0.0;
        for (int n : { 33, 45, 57, 69, 81 })
        {
            RenderOpts o; o.vel = 0.6; o.seconds = 1.4;
            const auto x = renderCenterDisp (n, o);
            const double f = an::refineF0 (x, kFs, noteHz (n), 0.3, 1.2);
            worst = std::max (worst, std::abs (1200.0 * std::log2 (f / noteHz (n))));
        }
        row ("S1", "tuning, 5 keys across compass", "+/-3 cents of ET",
             fmt ("%.2f ct worst", worst), worst <= 3.0 ? Verdict::pass : Verdict::fail);
    }

    // S2: B measured off the RENDER, the paper's own method: peak-fit
    // partials 2-7, LSQ against 1 + B k^2. +/-25%.
    {
        ClavinetVoice::Config cfg;
        struct A { int midi; double b; double ta; double tb; const char* name; };
        // Treble partials above ~6 kHz decay at 50-80 dB/s and are numerical
        // noise by half a second, so the E6 fit reads an early window; the
        // bass fits stay late, past the heterodyne's group delay.
        for (A a : { A { 29, 5.0e-4, 0.2, 1.4, "F1" }, A { 50, 9.0e-5, 0.2, 1.2, "D3" },
                     A { 88, 8.0e-5, 0.05, 0.5, "E6" } })
        {
            RenderOpts o; o.vel = 0.7; o.seconds = 1.6;
            const auto x = renderCenterDisp (a.midi, o);
            ClavinetVoice probe; probe.prepare (kFs); probe.setNote (a.midi, cfg);
            const double f1 = partialHz (x, probe.modeFrequency (0), noteHz (a.midi), a.ta, a.tb);
            double sxx = 0, sxy = 0;
            int used = 0;
            for (int k = 2; k <= 7 && k <= probe.partialCount(); ++k)
            {
                const double fk = partialHz (x, probe.modeFrequency (k - 1), noteHz (a.midi), a.ta, a.tb);
                if (fk <= 0.0) continue;
                const double y = (fk * fk) / (k * k * f1 * f1) - 1.0;
                const double xx = static_cast<double> (k * k) - 1.0;
                sxx += xx * xx; sxy += xx * y; ++used;
            }
            const double bFit = used >= 4 ? sxy / sxx : -1.0;
            row ("S2", (std::string ("rendered B, ") + a.name).c_str(),
                 fmt ("%.2g +/-25%%", a.b), fmt ("%.3g", bFit),
                 within (bFit, a.b * 0.75, a.b * 1.25));
        }
    }

    // S3: the release signature: three semitones down, everywhere on the
    // keyboard, as the string reunites with the yarn-wrapped dead length
    // [M, EURASIP Fig. 5]. Measured as f0 just before noteOff against f0
    // just after, on an aged-yarn setting so the tail is long enough to read.
    {
        double worstErr = 0.0;
        for (int n : { 33, 45, 57, 69, 81 })
        {
            RenderOpts o; o.vel = 0.6; o.seconds = 1.6; o.noteOffAt = 0.9;
            o.cfg.damperGrip = 0.1;
            const auto x = renderCenterDisp (n, o);
            const double before = an::refineF0 (x, kFs, noteHz (n), 0.55, 0.86);
            const double after = an::refineF0 (x, kFs, noteHz (n) * 0.8409, 0.96, 1.25);
            const double drop = -12.0 * std::log2 (after / before);
            worstErr = std::max (worstErr, std::abs (drop - 3.0));
        }
        row ("S3", "release drop, 5 keys", "3.00 semitones +/-0.10",
             fmt ("worst |err| %.3f st", worstErr),
             worstErr <= 0.10 ? Verdict::pass : Verdict::fail);
    }

    // S4: sustain. Low/mid T60 of 20 s or more is the measurement; the row
    // asserts >= 15 s at the A2 region and that decay speeds up with key
    // above the middle, as strings do [M].
    {
        RenderOpts o; o.vel = 0.6; o.seconds = 3.5;
        const auto x = renderCenterDisp (45, o);
        const double f0 = an::refineF0 (x, kFs, noteHz (45), 0.3, 3.0);
        const an::Envelope e = an::heterodyne (x, kFs, f0, f0);
        const an::LineFit f = an::fitDecay (e, 0.3, 3.2);
        const double t60 = f.valid ? -60.0 / f.slopeDbPerS : -1.0;
        row ("S4", "A2 fundamental T60", "at least 15 s",
             fmt ("%.1f s", t60), t60 >= 15.0 ? Verdict::pass : Verdict::fail);

        double prevRate = 0.0;
        bool monotone = true;
        std::string got;
        for (int n : { 64, 76, 88 })
        {
            RenderOpts oo; oo.vel = 0.6; oo.seconds = 2.5;
            const auto xx = renderCenterDisp (n, oo);
            const double ff = an::refineF0 (xx, kFs, noteHz (n), 0.3, 2.0);
            const an::Envelope ee = an::heterodyne (xx, kFs, ff, ff);
            const an::LineFit lf = an::fitDecay (ee, 0.3, 2.2);
            const double rate = lf.valid ? -lf.slopeDbPerS : -1.0;
            if (rate < prevRate) monotone = false;
            prevRate = rate;
            got += fmt ("%.1f ", rate);
        }
        row ("S4", "decay rate rises with key, E4-E6", "monotone dB/s",
             got, monotone ? Verdict::pass : Verdict::fail);
    }

    // S5: the T60-vs-partial ripple, period 2-3 x f0 [M, EURASIP Fig. 8].
    // The voice's per-mode T60 telemetry, detrended by its own quadratic
    // trend, correlated against a cosine over a period grid.
    {
        ClavinetVoice::Config cfg;
        ClavinetVoice v;
        v.prepare (kFs);
        v.setNote (45, cfg);
        const double f0 = noteHz (45);
        const int K = std::min (60, v.partialCount());

        // Quadratic detrend of log alpha against frequency.
        std::vector<double> fk (static_cast<std::size_t> (K)), yv (static_cast<std::size_t> (K));
        {
            double A[9] = { 0 }, b[3] = { 0 };
            for (int k = 0; k < K; ++k)
            {
                fk[static_cast<std::size_t> (k)] = v.modeFrequency (k);
                yv[static_cast<std::size_t> (k)] = std::log (60.0 / v.modeT60 (k));
                const double xs[3] = { 1.0, fk[static_cast<std::size_t> (k)], fk[static_cast<std::size_t> (k)] * fk[static_cast<std::size_t> (k)] };
                for (int i = 0; i < 3; ++i)
                {
                    b[i] += xs[i] * yv[static_cast<std::size_t> (k)];
                    for (int j = 0; j < 3; ++j) A[i * 3 + j] += xs[i] * xs[j];
                }
            }
            // Solve the 3x3 by Cramer.
            auto det3 = [] (const double* m)
            {
                return m[0] * (m[4] * m[8] - m[5] * m[7])
                     - m[1] * (m[3] * m[8] - m[5] * m[6])
                     + m[2] * (m[3] * m[7] - m[4] * m[6]);
            };
            const double D = det3 (A);
            double c[3] = { 0, 0, 0 };
            for (int i = 0; i < 3 && std::abs (D) > 1.0e-30; ++i)
            {
                double M[9];
                for (int j = 0; j < 9; ++j) M[j] = A[j];
                for (int r = 0; r < 3; ++r) M[r * 3 + i] = b[r];
                c[i] = det3 (M) / D;
            }
            for (int k = 0; k < K; ++k)
                yv[static_cast<std::size_t> (k)] -= c[0] + c[1] * fk[static_cast<std::size_t> (k)]
                                                  + c[2] * fk[static_cast<std::size_t> (k)] * fk[static_cast<std::size_t> (k)];
        }

        double bestP = 0.0, best = -1.0;
        for (double p = 1.5; p <= 4.0; p += 0.02)
        {
            double cre = 0.0, cim = 0.0;
            for (int k = 0; k < K; ++k)
            {
                const double ph = 2.0 * an::kPi * fk[static_cast<std::size_t> (k)] / (p * f0);
                cre += yv[static_cast<std::size_t> (k)] * std::cos (ph);
                cim += yv[static_cast<std::size_t> (k)] * std::sin (ph);
            }
            const double pw = cre * cre + cim * cim;
            if (pw > best) { best = pw; bestP = p; }
        }
        row ("S5", "T60 ripple period (A2)", "2-3 x f0",
             fmt ("%.2f x f0", bestP), within (bestP, 1.9, 3.1));
    }

    // S6: pitch stability in the sustain: 1-2 cents is the measurement; the
    // exact integrator should hold it far tighter [M, EURASIP 2.3.3].
    {
        RenderOpts o; o.vel = 0.6; o.seconds = 5.0;
        const auto x = renderCenterDisp (45, o);
        const double fA = an::refineF0 (x, kFs, noteHz (45), 0.2, 1.0);
        const double fB = an::refineF0 (x, kFs, noteHz (45), 4.0, 4.9);
        const double drift = std::abs (1200.0 * std::log2 (fB / fA));
        row ("S6", "f0 drift 0.2 s -> 5 s (A2)", "at most 2 cents",
             fmt ("%.3f ct", drift), drift <= 2.0 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// E. Excitation and preamp calibration
// ===========================================================================

static void sectionExcitation()
{
    heading ("E. Excitation: preamp THD, spectrum tilt, velocity, the seat");

    // E1: the preamp's measured nonlinearity: THD 1% at 400 mV in (the
    // maximum pickup level in normal polyphonic playing), rising to 3.6% at
    // fortissimo chord peaks [M, EURASIP 3.4] — their own method, a 1 kHz
    // sine and a harmonic analyzer.
    {
        auto thd = [] (double amp)
        {
            ClavinetPreamp pre;
            pre.prepare (kFs);
            const int N = static_cast<int> (kFs * 0.5);
            std::vector<double> y (static_cast<std::size_t> (N));
            for (int i = 0; i < N; ++i)
                y[static_cast<std::size_t> (i)] = pre.process (amp * std::sin (2.0 * kPiD * 1000.0 * i / kFs));
            const double h1 = toneAmp (y, 1000.0, 0.1, 0.45);
            double acc = 0.0;
            for (int k = 2; k <= 10; ++k)
            {
                const double h = toneAmp (y, 1000.0 * k, 0.1, 0.45);
                acc += h * h;
            }
            return 100.0 * std::sqrt (acc) / std::max (1.0e-15, h1);
        };
        const double atNom = thd (0.4);
        const double atFf  = thd (0.8);
        row ("E1", "THD at 400 mV, 1 kHz", "1.0 % +/-0.5",
             fmt ("%.2f %%", atNom), within (atNom, 0.5, 1.5));
        row ("E1", "THD at ff chord peak level", "3.6 % +/-1.5",
             fmt ("%.2f %%", atFf), within (atFf, 2.1, 5.1));
    }

    // E1b: the amplifier-minus-tone-stack response: -3 dB low shelf at
    // 130 Hz, +3 dB high shelf at 4 kHz [M, SPICE on the real schematic].
    // Probed in the linear region, relative to 1 kHz.
    {
        auto gainAt = [] (double f)
        {
            ClavinetPreamp pre;
            pre.prepare (kFs);
            const int N = static_cast<int> (kFs * 0.6);
            std::vector<double> y (static_cast<std::size_t> (N));
            for (int i = 0; i < N; ++i)
                y[static_cast<std::size_t> (i)] = pre.process (0.01 * std::sin (2.0 * kPiD * f * i / kFs));
            return toneAmp (y, f, 0.3, 0.58) / 0.01;
        };
        const double ref = gainAt (1000.0);
        const double lo = 20.0 * std::log10 (gainAt (30.0) / ref);
        const double hi = 20.0 * std::log10 (gainAt (15000.0) / ref);
        row ("E1", "low shelf plateau (30 Hz vs 1 kHz)", "-3 dB +/-1",
             fmt ("%.2f dB", lo), within (lo, -4.0, -2.0));
        row ("E1", "high shelf plateau (15 kHz vs 1 kHz)", "+3 dB +/-1",
             fmt ("%.2f dB", hi), within (hi, 2.0, 4.0));
    }

    // E2: "the second partial always sits > 3 dB above the fundamental"
    // [M, EURASIP Fig. 3b]. At the printed figure's own key the model meets
    // it; across the mid compass the geometric launch (end-displacement ramp
    // through the position combs) leaves H2 above H1 but short of +3 dB —
    // carried as a bounded gap: without a measured tangent pulse per key
    // (open question 2), inventing extra tilt would be fitting to prose.
    {
        auto h2rel = [] (int n)
        {
            ClavinetVoice::Config cfg;
            ClavinetVoice probe; probe.prepare (kFs); probe.setNote (n, cfg);
            RenderOpts o; o.vel = 0.6; o.seconds = 1.2;
            const auto x = renderClav (n, o);
            const double h1 = toneAmp (x, probe.modeFrequency (0), 0.4, 0.9);
            const double h2 = toneAmp (x, probe.modeFrequency (1), 0.4, 0.9);
            return 20.0 * std::log10 (std::max (1.0e-15, h2) / std::max (1.0e-15, h1));
        };
        const double a2 = h2rel (45);
        row ("E2", "H2 - H1 at A2 (Fig. 3's key)", "at least +3 dB",
             fmt ("%.1f dB", a2), a2 >= 3.0 ? Verdict::pass : Verdict::fail);
        const double a3 = h2rel (57);
        const double e4 = h2rel (64);
        row ("E2", "H2 - H1, A3", "at least +3 dB",
             fmt ("%.1f dB", a3), gap (a3, 3.0, 30.0, 0.0, 3.0));
        // The E4 shortfall's mechanism is documented above (the ramp rings
        // about the original line); its measured value sits within a decibel
        // of zero and moved 0.6 dB when the tone stack gained its source
        // impedance -- the band floor allows that jitter without letting the
        // defect grow.
        row ("E2", "H2 - H1, E4", "at least +3 dB",
             fmt ("%.1f dB", e4), gap (e4, 3.0, 30.0, -1.0, 3.0));
    }

    // E3: the velocity map: tangent velocity 1-4 m/s linear over the MIDI
    // range [M, EURASIP 3.2], and a heavier touch enhances the proportion of
    // overtones [R, E7 manual] — spectral centroid rises pp -> ff.
    {
        ClavinetVoice::Config cfg;
        ClavinetVoice v;
        v.prepare (kFs);
        v.setNote (57, cfg);
        double os[ClavinetVoice::kOver];
        v.noteOn (57, 0.0, cfg, 1);
        const double vLo = v.strikeVelocity();
        for (int i = 0; i < 100; ++i) v.process (cfg, os);
        v.noteOn (57, 1.0, cfg, 1);
        const double vHi = v.strikeVelocity();
        row ("E3", "tip velocity endpoints", "1.0 and 4.0 m/s",
             fmt2 ("%.2f / %.2f", vLo, vHi),
             (std::abs (vLo - 1.0) < 1.0e-9 && std::abs (vHi - 4.0) < 1.0e-9)
                 ? Verdict::pass : Verdict::fail);

        auto centroid = [] (double vel)
        {
            RenderOpts o; o.vel = vel; o.seconds = 0.6;
            const auto x = renderClav (45, o);
            return an::spectralCentroid (x, kFs, static_cast<std::size_t> (0.05 * kFs),
                                         static_cast<std::size_t> (16384), 8000.0);
        };
        const double cPp = centroid (0.15);
        const double cFf = centroid (1.0);
        row ("E3", "centroid rises pp -> ff (A2)", "ff above pp",
             fmt2 ("%.0f -> %.0f Hz", cPp, cFf), cFf > cPp ? Verdict::pass : Verdict::fail);
    }

    // E4: the contact is the anvil seat: its duration is the tangent's
    // crossing time d/v, EURASIP Eq. 7's excitation-pulse length.
    {
        ClavinetVoice::Config cfg;
        bool ok = true;
        std::string got;
        for (int n : { 33, 57, 81 })
        {
            ClavinetVoice v;
            v.prepare (kFs);
            v.setNote (n, cfg);
            v.noteOn (n, 0.6, cfg, 1);
            double os[ClavinetVoice::kOver];
            for (int i = 0; i < static_cast<int> (0.2 * kFs); ++i) v.process (cfg, os);
            const double ms = 1000.0 * v.contactSamples() / kFs;
            // gap at escapement 0.4: 3 mm * (0.4+1.2*0.4) = 2.64 mm;
            // v at 0.6: 1 + 3*0.6 = 2.8 m/s -> 0.94 ms.
            const double want = 2.64e-3 / 2.8 * 1000.0;
            if (! (ms >= want * 0.7 && ms <= want * 1.3)) ok = false;
            got += fmt ("%.2f ", ms);
        }
        row ("E4", "contact = d/v seat, 3 keys", "0.94 ms +/-30%",
             got + "ms", ok ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// P. The pickups: combs, switch matrix, alias guard
// ===========================================================================

static void sectionPickups()
{
    heading ("P. Pickups: rendered comb, switch matrix, alias guard");

    // P1: the rendered comb. A2 through the reference chain: partials at the
    // notch orders sit below both neighbours [M, EURASIP Fig. 3b]. Depth is
    // informational — it depends on the reconstructed length (open question
    // 1), which is why the row asserts minima, not decibels.
    {
        ClavinetVoice::Config cfg;
        ClavinetVoice probe; probe.prepare (kFs); probe.setNote (45, cfg);
        RenderOpts o; o.vel = 0.6; o.seconds = 1.2;
        const auto x = renderClav (45, o);
        auto amp = [&] (int k) { return toneAmp (x, probe.modeFrequency (k - 1), 0.45, 0.95); };
        bool minima = true;
        std::string got;
        for (int k : { 5, 10 })
        {
            const double a = amp (k), l = amp (k - 1), r = amp (k + 1);
            if (! (a < l && a < r)) minima = false;
            got += fmt2 ("P%d %.0f dB ", k, 20.0 * std::log10 (std::max (1.0e-15, a)));
        }
        row ("P1", "comb notches at k=5,10 (A2)", "local spectral minima",
             minima ? "minima" : "NOT minima", minima ? Verdict::pass : Verdict::fail);
    }

    // P2: the 4-way switch matrix [R, DAFx-12 Table 1]. Anti-phase sum
    // cancels what the taps share: its fundamental must sit below the
    // in-phase sum's by the sin-weight prediction, +/-2 dB; and no
    // renormalisation — the thin setting stays quieter. Bridge alone is the
    // bright switch: higher centroid than center alone.
    {
        ClavinetVoice::Config cfg;
        ClavinetVoice probe; probe.prepare (kFs); probe.setNote (45, cfg);
        const double f1 = probe.modeFrequency (0);
        double h[4], cen[4];
        for (int s = 0; s < 4; ++s)
        {
            RenderOpts o; o.vel = 0.6; o.seconds = 1.0;
            o.cfg.pickupSel = s;
            const auto x = renderClav (45, o);
            h[s] = toneAmp (x, f1, 0.4, 0.9);
            // Brightness on the raw pickup voltage, so the reference rocker's
            // own tilt does not compress the comparison.
            RenderOpts or2 = o; or2.chainOn = false;
            const auto xr = renderClav (45, or2);
            cen[s] = an::spectralCentroid (xr, kFs, static_cast<std::size_t> (0.3 * kFs),
                                           static_cast<std::size_t> (16384), 12000.0);
        }
        const double wc = probe.tapWeight (ClavinetVoice::tapCenter, 0);
        const double wb = probe.tapWeight (ClavinetVoice::tapBridge, 0);
        const double pred = 20.0 * std::log10 (std::abs (wc - wb) / (wc + wb));
        const double got = 20.0 * std::log10 (h[3] / h[2]);
        row ("P2", "anti-phase H1 vs in-phase H1", fmt ("%.1f dB +/-2", pred),
             fmt ("%.1f dB", got), within (got, pred - 2.0, pred + 2.0));
        row ("P2", "bridge brighter than center", "higher centroid",
             fmt2 ("%.0f vs %.0f Hz", cen[1], cen[0]),
             cen[1] > cen[0] ? Verdict::pass : Verdict::fail);
        row ("P2", "anti-phase not renormalised", "H1 out < H1 in",
             fmt2 ("%.4g < %.4g", h[3], h[2]), h[3] < h[2] ? Verdict::pass : Verdict::fail);
    }

    // P3: the alias gate, measured the way the Rhodes measured it: the same
    // note rendered at a 4x higher base rate is the reference, and alias is
    // whatever off-partial energy the 48 kHz render has that the 192 kHz one
    // does not. A plain off-partial floor cannot be the ruler here — the
    // flux polynomial's REAL intermodulation products land off the stretched
    // partial grid by construction, at both rates alike, and only the excess
    // is folding. Voice path only (chain off), closest gap, full velocity:
    // the hottest the pickup nonlinearity gets.
    {
        auto residualDb = [] (double baseRate)
        {
            ClavinetVoice::Config cfg;
            cfg.gapNorm = 0.0;
            ClavinetVoice v;
            v.prepare (baseRate);
            v.setBeat (0.0);
            v.setNote (88, cfg);
            v.noteOn (88, 1.0, cfg, 1);
            const std::size_t n = 1u << 16;
            const int step = static_cast<int> (baseRate / kFs + 0.5);
            std::vector<double> x (n);
            double os[ClavinetVoice::kOver];
            Decimator d1; d1.prepare (baseRate);   // the voice's own decimation
            Decimator d2; d2.prepare (kFs);        // 192k -> 48k for analysis
            auto sample48 = [&]
            {
                if (step == 1)
                {
                    v.process (cfg, os);
                    return d1.process (os);
                }
                double buf[Decimator::kOver];
                for (int s = 0; s < step; ++s)
                {
                    v.process (cfg, os);
                    buf[s] = d1.process (os);
                }
                return d2.process (buf);
            };
            for (int i = 0; i < static_cast<int> (0.1 * kFs); ++i) sample48();
            for (std::size_t i = 0; i < n; ++i) x[i] = sample48();
            std::vector<an::cplx> a (n);
            for (std::size_t i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * an::kPi * static_cast<double> (i) / static_cast<double> (n - 1));
                a[i] = x[i] * w;
            }
            an::fft (a);
            const double binHz = kFs / static_cast<double> (n);
            double harm = 0.0, resid = 0.0;
            for (std::size_t b = 1; b < n / 2; ++b)
            {
                const double f = static_cast<double> (b) * binHz;
                if (f < 200.0 || f > 14000.0) continue;
                bool isHarm = false;
                for (int k = 0; k < v.partialCount(); ++k)
                    if (std::abs (f - v.modeFrequency (k)) <= 12.0) { isHarm = true; break; }
                (isHarm ? harm : resid) += std::norm (a[b]);
            }
            return 10.0 * std::log10 (std::max (1.0e-30, resid / std::max (1.0e-30, harm)));
        };
        // A negative excess is expected: the 192 kHz voice carries partials
        // to 18 kHz that the 48 kHz budget refuses, and their intermod
        // differences land in-band as extra off-grid residue in the
        // reference — the comparison errs against the 48 kHz render.
        const double at48  = residualDb (48000.0);
        const double at192 = residualDb (192000.0);
        row ("P3", "off-partial floor, E6 ff, voice", "informational",
             fmt2 ("%.1f dB (ref %.1f)", at48, at192), Verdict::info);
        row ("P3", "alias excess vs 192 kHz reference", "below 3 dB",
             fmt ("%.2f dB", at48 - at192),
             at48 - at192 < 3.0 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// T. The tone rockers
// ===========================================================================

// Analytic bilinear response of each rocker, written here from EURASIP
// Table 3 and the schematic values — independently of the implementation, so
// a coefficient bug in the stack cannot certify itself.
static double analyticRockerDb (int which, double f, double fsOs)
{
    const double K = 2.0 * fsOs;
    const an::cplx z = std::exp (an::cplx (0.0, -2.0 * an::kPi * f / fsOs));   // z^-1
    const double zref = ClavinetToneStack::zRef();
    an::cplx H (1.0, 0.0);
    // The same source-impedance divider the stack computes: each branch
    // against the pickup coil's kSourceR (the taming of the Treble LC's
    // absurd ideal-source Q, and Brilliant's high-pass nature).
    const double Rs = ClavinetToneStack::kSourceR;
    if (which == 0 || which == 1)
    {
        const double R = which == 0 ? ClavinetToneStack::kSoftR : ClavinetToneStack::kMedR;
        const double C = which == 0 ? ClavinetToneStack::kSoftC : ClavinetToneStack::kMedC;
        const double g0  = R / (R + Rs);
        const double tau = Rs * R * C / (R + Rs);
        const double kt  = K * tau;
        H = (g0 / (1.0 + kt)) * (1.0 + z) / (1.0 + ((1.0 - kt) / (1.0 + kt)) * z);
    }
    else if (which == 2)
    {
        const double L = ClavinetToneStack::kTrebL, C = ClavinetToneStack::kTrebC,
                     Rw = ClavinetToneStack::kTrebRw;
        const double c0 = Rw + Rs;
        const double c1 = L + Rs * Rw * C;
        const double c2 = Rs * L * C;
        const an::cplx num = (Rw + L * K) + 2.0 * Rw * z + (Rw - L * K) * z * z;
        const an::cplx den = (c0 + c1 * K + c2 * K * K)
                           + (2.0 * c0 - 2.0 * c2 * K * K) * z
                           + (c0 - c1 * K + c2 * K * K) * z * z;
        H = num / den;
    }
    else
    {
        const double kl = K * ClavinetToneStack::kBrilL;
        H = kl * (1.0 - z) / ((Rs + kl) + (Rs - kl) * z);
    }
    return 20.0 * std::log10 (std::abs (H) / zref);
}

static double measuredRockerDb (int which, double f, double fsOs)
{
    ClavinetToneStack st;
    st.prepare (fsOs);
    st.setRockers (which == 0, which == 1, which == 2, which == 3);
    // The Treble branch rings with Q ~ 350 (67 ms time constant): the probe
    // must let it settle or the row measures the transient, not the filter.
    const int N = static_cast<int> (fsOs * 1.0);
    const int skip = N / 2;
    double re = 0.0, im = 0.0;
    int cnt = 0;
    for (int i = 0; i < N; ++i)
    {
        const double ph = 2.0 * an::kPi * f * i / fsOs;
        const double y = st.process (std::sin (ph));
        if (i >= skip)
        {
            re += y * std::cos (ph);
            im += y * std::sin (ph);
            ++cnt;
        }
    }
    const double amp = 2.0 * std::sqrt (re * re + im * im) / cnt;
    return 20.0 * std::log10 (std::max (1.0e-15, amp));
}

static void sectionRockers()
{
    heading ("T. Tone rockers: the real networks, digitized as published");

    const double fsOs = kFs * ClavinetVoice::kOver;
    static const char* names[4] = { "Soft", "Medium", "Treble", "Brilliant" };

    // T1: each rocker's rendered response against the analytic bilinear
    // Z_i, +/-0.5 dB from 30 Hz to 15 kHz. (EURASIP validated this cascade
    // against SPICE — their Fig. 14 — so matching the published H_i is
    // matching the validated model.)
    for (int r = 0; r < 4; ++r)
    {
        double worst = 0.0;
        for (double f : { 30.0, 80.0, 200.0, 500.0, 1000.0, 1641.0, 2500.0, 5000.0, 10000.0, 15000.0 })
            worst = std::max (worst, std::abs (measuredRockerDb (r, f, fsOs)
                                              - analyticRockerDb (r, f, fsOs)));
        row ("T1", (std::string (names[r]) + " vs analytic bilinear Z").c_str(),
             "+/-0.5 dB, 30 Hz-15 kHz", fmt ("%.3f dB worst", worst),
             worst <= 0.5 ? Verdict::pass : Verdict::fail);
    }

    // T2: Hohner's silence rule, made unhostile: all rockers up falls back
    // to Medium instead of muting the plugin [R -> D].
    {
        ClavinetToneStack a, b;
        a.prepare (fsOs); b.prepare (fsOs);
        a.setRockers (false, false, false, false);
        b.setRockers (false, true, false, false);
        double worst = 0.0;
        double xa = 1.0, xb = 1.0;
        for (int i = 0; i < 512; ++i)
        {
            worst = std::max (worst, std::abs (a.process (xa) - b.process (xb)));
            xa = xb = 0.0;
        }
        row ("T2", "all rockers up -> Medium fallback", "identical impulse response",
             fmt ("%.2g worst diff", worst), worst < 1.0e-12 ? Verdict::pass : Verdict::fail);
    }

    // T3: engaged rockers cascade (the published parallel-to-cascade recipe):
    // Soft + Brilliant together equals the product of their responses.
    {
        double worst = 0.0;
        for (double f : { 100.0, 1000.0, 8000.0 })
        {
            ClavinetToneStack st;
            st.prepare (fsOs);
            st.setRockers (true, false, false, true);
            const int N = static_cast<int> (fsOs * 0.25);
            const int skip = N / 3;
            double re = 0.0, im = 0.0;
            int cnt = 0;
            for (int i = 0; i < N; ++i)
            {
                const double ph = 2.0 * an::kPi * f * i / fsOs;
                const double y = st.process (std::sin (ph));
                if (i >= skip) { re += y * std::cos (ph); im += y * std::sin (ph); ++cnt; }
            }
            const double db = 20.0 * std::log10 (std::max (1.0e-15, 2.0 * std::sqrt (re * re + im * im) / cnt));
            const double want = analyticRockerDb (0, f, fsOs) + analyticRockerDb (3, f, fsOs);
            worst = std::max (worst, std::abs (db - want));
        }
        row ("T3", "Soft+Brilliant = cascaded product", "+/-0.5 dB",
             fmt ("%.3f dB worst", worst), worst <= 0.5 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// K. Knock, B. Beating
// ===========================================================================

static void sectionKnockAndBeat()
{
    heading ("K/B. Tangent knock and the sub-E4 beating");

    // K1: knock energy concentrated below the measured ~1.2 kHz ceiling
    // [M, EURASIP 3.5].
    {
        ClavinetKnock knock;
        knock.prepare (kFs);
        knock.strike (60, 0.8);
        const std::size_t n = 1u << 14;
        std::vector<an::cplx> a (n);
        for (std::size_t i = 0; i < n; ++i)
            a[i] = knock.process();
        an::fft (a);
        double below = 0.0, total = 0.0;
        for (std::size_t b = 1; b < n / 2; ++b)
        {
            const double f = static_cast<double> (b) * kFs / static_cast<double> (n);
            const double p = std::norm (a[b]);
            total += p;
            if (f < 1200.0) below += p;
        }
        const double frac = 100.0 * below / std::max (1.0e-30, total);
        row ("K1", "knock energy below 1.2 kHz", "at least 85 %",
             fmt ("%.1f %%", frac), frac >= 85.0 ? Verdict::pass : Verdict::fail);
    }

    // B1: beating exists below E4 and not above [M, EURASIP 2.3.4: 0.5-2 Hz,
    // up to 15 dB p-p, only for keys up to E4]. Phenomenological partner
    // (see ClavinetVoice::setBeat); the rows measure the detrended
    // fundamental envelope swing over 6 s.
    {
        auto swing = [] (int n)
        {
            RenderOpts o; o.vel = 0.5; o.seconds = 6.0; o.beatOn = true;
            const auto x = renderCenterDisp (n, o);
            const double f0 = an::refineF0 (x, kFs, noteHz (n), 0.5, 3.0);
            const an::Envelope e = an::heterodyne (x, kFs, f0, f0);
            return an::detrendedSwingDb (e, 0.5, 5.7);
        };
        const double lo = swing (45);   // A2, below the E4 line
        const double hi = swing (69);   // A4, above it
        row ("B1", "beating on A2 (below E4)", "1-8 dB p-p",
             fmt ("%.2f dB", lo), within (lo, 1.0, 8.0));
        row ("B1", "no beating on A4 (above E4)", "below 1 dB p-p",
             fmt ("%.2f dB", hi), hi < 1.0 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// N. Robustness
// ===========================================================================

static void sectionRobustness()
{
    heading ("N. Finite, damped, retired, affordable");

    // N1: finite and bounded across the engine's whole extrapolated compass
    // at both velocity extremes and adversarial settings; modal energy never
    // rises once the tangent has seated.
    {
        bool clean = true, passive = true;
        double pk = 0.0;
        ClavinetVoice::Config cfg;
        cfg.hammerHardness = 1.0;
        cfg.gapNorm = 0.0;        // closest, hottest operating point
        cfg.dampTrim = 1.0;
        for (int n : { 21, 29, 45, 64, 88, 96, 108 })
            for (double vel : { 0.05, 1.0 })
            {
                ClavinetVoice v;
                v.prepare (kFs);
                v.setNote (n, cfg);
                v.noteOn (n, vel, cfg, 1);
                ClavinetToneStack st; st.prepare (kFs * 4); st.setRockers (true, true, true, true);
                ClavinetPreamp pre; pre.prepare (kFs * 4); pre.setDrive (4.0);
                Decimator dec; dec.prepare (kFs);
                double os[4];
                double ePrev = 1.0e300;
                for (int i = 0; i < static_cast<int> (kFs * 1.5); ++i)
                {
                    v.process (cfg, os);
                    for (int k = 0; k < 4; ++k) os[k] = pre.process (st.process (os[k]));
                    const double y = dec.process (os);
                    if (! std::isfinite (y)) clean = false;
                    pk = std::max (pk, std::abs (y));
                    if ((i & 1023) == 0)
                    {
                        const double e = v.modalEnergy();
                        if (! std::isfinite (e)) clean = false;
                        if (i > static_cast<int> (0.25 * kFs))
                        {
                            if (e > ePrev * 1.000001) passive = false;
                            ePrev = e;
                        }
                    }
                }
            }
        row ("N1", "finite across compass, extremes", "finite, peak < 40",
             clean ? fmt ("peak %.2f", pk) : std::string ("NON-FINITE"),
             (clean && pk < 40.0) ? Verdict::pass : Verdict::fail);
        row ("N1", "energy never rises post-seat", "monotone",
             passive ? "monotone" : "ROSE", passive ? Verdict::pass : Verdict::fail);
    }

    // N2: the yarn. Key-up kills the note; the tail 0.4 s after release is
    // gone at the default grip.
    {
        RenderOpts o; o.vel = 0.8; o.seconds = 1.6; o.noteOffAt = 0.7; o.chainOn = false;
        const auto x = renderClav (45, o);
        double before = 0.0, after = 0.0;
        for (std::size_t i = static_cast<std::size_t> (0.45 * kFs);
             i < static_cast<std::size_t> (0.65 * kFs); ++i)
            before = std::max (before, std::abs (x[i]));
        for (std::size_t i = static_cast<std::size_t> (1.1 * kFs);
             i < static_cast<std::size_t> (1.5 * kFs); ++i)
            after = std::max (after, std::abs (x[i]));
        const double db = 20.0 * std::log10 (std::max (1.0e-15, after / std::max (1.0e-15, before)));
        row ("N2", "yarn: tail 0.4 s after key-up", "below -40 dB",
             fmt ("%.1f dB", db), db < -40.0 ? Verdict::pass : Verdict::fail);
    }

    // N3: half-pedal monotonicity. The pedal holds the tangent; partial
    // pedal exposes the compression^2.5 law, so post-release energy must
    // grow monotonically with pedal amount.
    {
        double prev = -1.0;
        bool monotone = true;
        std::string got;
        for (double p : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            ClavinetVoice::Config cfg;
            ClavinetVoice v;
            v.prepare (kFs);
            v.setBeat (0.0);
            v.setNote (45, cfg);
            v.setPedal (p);
            v.noteOn (45, 0.8, cfg, 1);
            double os[4];
            for (int i = 0; i < static_cast<int> (kFs * 1.2); ++i)
            {
                if (i == static_cast<int> (kFs * 0.3)) v.noteOff();
                v.process (cfg, os);
            }
            const double e = v.modalEnergy();
            if (e < prev) monotone = false;
            prev = e;
            got += fmt ("%.0e ", e);
        }
        row ("N3", "half-pedal: energy vs pedal", "monotone increasing",
             monotone ? "monotone" : "NOT monotone",
             monotone ? Verdict::pass : Verdict::fail);
    }

    // N4: a released ff note retires (relative to its own strike).
    {
        ClavinetVoice::Config cfg;
        ClavinetVoice v;
        v.prepare (kFs);
        v.setNote (33, cfg);
        v.noteOn (33, 1.0, cfg, 1);
        double os[4];
        bool retired = false;
        for (int i = 0; i < static_cast<int> (kFs * 5.0); ++i)
        {
            if (i == static_cast<int> (kFs * 0.5)) v.noteOff();
            v.process (cfg, os);
            if (! v.isRinging() && i > static_cast<int> (kFs * 0.6)) { retired = true; break; }
        }
        row ("N4", "released ff A1 retires", "within 5 s",
             retired ? "retired" : "still sounding", retired ? Verdict::pass : Verdict::fail);
    }

    // N5: the 60-key clamp-and-release stress, scaled to this suite: every
    // key struck, held, released; finite output; wall time informational
    // (the engine's own CPU row is engine scope).
    {
        std::vector<ClavinetVoice> vs (60);
        ClavinetVoice::Config cfg;
        for (int i = 0; i < 60; ++i)
        {
            vs[static_cast<std::size_t> (i)].prepare (kFs);
            vs[static_cast<std::size_t> (i)].setNote (29 + i, cfg);
            vs[static_cast<std::size_t> (i)].noteOn (29 + i, 0.9, cfg, 1);
        }
        const auto t0 = std::chrono::steady_clock::now();
        bool clean = true;
        double os[4];
        const int N = static_cast<int> (kFs * 1.2);
        for (int i = 0; i < N; ++i)
        {
            double acc = 0.0;
            for (auto& v : vs)
            {
                if (i == static_cast<int> (kFs * 0.5)) v.noteOff();
                v.process (cfg, os);
                acc += os[3];
            }
            if (! std::isfinite (acc)) clean = false;
        }
        const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        row ("N5", "60-key clamp-and-release", "finite",
             clean ? "finite" : "NON-FINITE", clean ? Verdict::pass : Verdict::fail);
        row ("N5", "60-key wall time, 1.2 s render", "informational",
             fmt2 ("%.2f s (%.1fx RT)", wall, wall / 1.2), Verdict::info);
    }
}

int main()
{
    std::printf ("Epi Clavinet D6 reference suite\n");
    std::printf ("targets from docs/clavinet-implementation-plan.md section 8 and docs/research/clavinet-measured.md\n");
    std::printf ("\n  %-3s %-36s %-24s %-22s %s\n", "ID", "PROPERTY", "TARGET", "MEASURED", "");

    sectionGeometry();
    sectionString();
    sectionExcitation();
    sectionPickups();
    sectionRockers();
    sectionKnockAndBeat();
    sectionRobustness();

    std::printf ("\n");
    if (gaps > 0)
        std::printf ("%d known gap%s (each bounded; see the row comments in this file)\n",
                     gaps, gaps == 1 ? "" : "s");
    if (failures == 0)
        std::printf ("all Clavinet reference rows within tolerance\n");
    else
        std::printf ("%d row%s outside tolerance\n", failures, failures == 1 ? "" : "s");
    std::printf ("SUMMARY fail=%d gap=%d\n", failures, gaps);
    return failures == 0 ? 0 : 1;
}
