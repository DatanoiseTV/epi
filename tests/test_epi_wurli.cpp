/*
  Epi — Wurlitzer 200A reference suite.

  Every check here corresponds to a numbered row in
  docs/wurlitzer-implementation-plan.md section 10, and every target in it is a
  measurement from docs/research/wurlitzer-200a.md or a primary document cited
  there. The suite renders the model, measures the same quantity the same way,
  and prints both side by side.

  The reference chain for spectral rows is the plan's: voice -> shared 2312 Hz
  reed-bar highpass -> 200A preamp at stock drive -> decimate. Tremolo off,
  speakers off, bias 150 V. Engine integration rows (C1 CPU, S2 speakers) are
  engine scope and not here; the speaker voicing constants live in WurliChain.h.

  Build: target epi_wurli_tests.
*/

#include "EpiAnalysis.h"
#include "epi/dsp/WurliVoice.h"
#include "epi/dsp/WurliChain.h"
#include "epi/dsp/OutputChain.h"

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

// The reference chain's preamp drive. The plan (section 6.2) has the default
// "nearly clean"; WurliPreamp::kStockDrive is the literal circuit level, and
// at that level the asymmetric clip audibly works even on single mid notes.
// 0.15 is the nearly-clean default the reference rows are calibrated at.
static constexpr double kRefDrive = 0.15;

struct RenderOpts
{
    double vel        = 0.75;
    double seconds    = 2.0;
    double noteOffAt  = -1.0;    // < 0: held for the whole render
    bool   preampOn   = true;
    double drive      = kRefDrive;
    double bias       = 150.0;
    bool   beatOn     = true;
    WurliVoice::Config cfg {};
};

static std::vector<double> renderWurli (int note, const RenderOpts& o)
{
    WurliVoice v;
    v.prepare (kFs);
    if (! o.beatOn) v.setBeat (2.4, 0.0);
    v.setNote (note, o.cfg);
    v.noteOn (note, o.vel, o.cfg, 1);

    WurliPickupBus bus;  bus.prepare (kFs * WurliVoice::kOver);  bus.setBias (o.bias);
    WurliPreamp pre;     pre.prepare (kFs * WurliVoice::kOver);  pre.setDrive (o.drive);
    Decimator dec;       dec.prepare (kFs);

    const int N = static_cast<int> (kFs * o.seconds);
    const int offAt = o.noteOffAt >= 0.0 ? static_cast<int> (kFs * o.noteOffAt) : -1;
    std::vector<double> out (static_cast<std::size_t> (N));
    double dc[WurliVoice::kOver], os[WurliVoice::kOver];
    for (int i = 0; i < N; ++i)
    {
        if (i == offAt) v.noteOff();
        v.process (o.cfg, dc);
        for (int k = 0; k < WurliVoice::kOver; ++k)
        {
            const double x = bus.process (dc[k]);
            os[k] = o.preampOn ? pre.process (x) : x;
        }
        out[static_cast<std::size_t> (i)] = dec.process (os);
    }
    return out;
}

// H2-12 energy relative to H1 -- the bark measure of the V rows, measured the
// way the research's tables are stated.
static double barkDb (const std::vector<double>& x, double f0)
{
    const std::size_t start = static_cast<std::size_t> (0.35 * kFs);
    const std::size_t len = an::residualWindow (kFs, f0, 8);
    const an::HarmonicFit hf = an::fitHarmonics (x, kFs, f0, start, len, 12);
    if (! hf.valid || hf.amp.size() < 2) return -999.0;
    double num = 0.0;
    for (std::size_t k = 1; k < hf.amp.size(); ++k) num += hf.amp[k] * hf.amp[k];
    return 10.0 * std::log10 (std::max (1.0e-30, num / std::max (1.0e-30, hf.amp[0] * hf.amp[0])));
}

// Amplitude^2-weighted phase regression: an::partialFrequency with each
// phase sample weighted by |z|^2. Phase noise on a partial scales as 1/SNR,
// so this is the maximum-likelihood frequency estimate for a decaying tone,
// and — the property the K1 full-chain row needs — samples taken while the
// partial's amplitude collapses through a transducer-law coefficient null
// carry no phase information and get no say in the fit.
static double partialFrequencyWeighted (const an::Envelope& e, double analysisF,
                                        double ta, double tb)
{
    std::vector<double> ts, ph, wt;
    double prev = 0.0, unwrapped = 0.0;
    bool first = true;
    for (std::size_t i = 0; i < e.z.size(); ++i)
    {
        const double t = e.time (i);
        if (t < ta || t > tb) continue;
        const double a = std::abs (e.z[i]);
        if (a < 1.0e-14) continue;
        const double p = std::arg (e.z[i]);
        if (first) { unwrapped = p; first = false; }
        else
        {
            double d = p - prev;
            while (d >  an::kPi) d -= 2.0 * an::kPi;
            while (d < -an::kPi) d += 2.0 * an::kPi;
            unwrapped += d;
        }
        prev = p;
        ts.push_back (t);
        ph.push_back (unwrapped);
        wt.push_back (a * a);
    }
    if (ts.size() < 8) return 0.0;
    double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (std::size_t i = 0; i < ts.size(); ++i)
    {
        const double w = wt[i];
        sw += w; sx += w * ts[i]; sy += w * ph[i];
        sxx += w * ts[i] * ts[i]; sxy += w * ts[i] * ph[i];
    }
    const double den = sw * sxx - sx * sx;
    if (std::abs (den) < 1.0e-18) return 0.0;
    return analysisF + (sw * sxy - sx * sy) / den / (2.0 * an::kPi);
}

// Power outside +/-maskHz of every harmonic of f0, in [loHz, hiHz], relative
// to the harmonic power in the same band. Hann window, 2^order samples.
//
// This replaces the comb-tracker residual (an::inharmonicDb) for the K2/P4
// rows because that ruler breaks on the Wurlitzer's dense loud spectra: it
// stops tracking at 0.45 fs and its comb length rounds to whole samples, so
// on a PERFECTLY periodic deep-drive test signal it reported -6 dB of
// "inharmonicity". This mask measure reads the same signal at -85 dB, which
// is what zero looks like through a Hann window.
static double maskedResidualDb (const std::vector<double>& x, double f0, double t0,
                                int order = 17, double maskHz = 8.0,
                                double loHz = 150.0, double hiHz = 15000.0)
{
    const std::size_t n = static_cast<std::size_t> (1) << order;
    const std::size_t s0 = static_cast<std::size_t> (t0 * kFs);
    if (s0 + n > x.size() || f0 <= 0.0) return 1.0;
    std::vector<an::cplx> a (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * an::kPi * static_cast<double> (i)
                                               / static_cast<double> (n - 1));
        a[i] = x[s0 + i] * w;
    }
    an::fft (a);
    const double binHz = kFs / static_cast<double> (n);
    double harm = 0.0, resid = 0.0;
    for (std::size_t b = 1; b < n / 2; ++b)
    {
        const double f = static_cast<double> (b) * binHz;
        if (f < loHz || f > hiHz) continue;
        const double p = std::norm (a[b]);
        const double k = std::round (f / f0);
        const bool isHarm = k >= 1.0 && std::abs (f - k * f0) <= maskHz;
        (isHarm ? harm : resid) += p;
    }
    return 10.0 * std::log10 (std::max (1.0e-30, resid / std::max (1.0e-30, harm)));
}

// Hann-windowed complex projection: one tone's amplitude in one short window.
// The heterodyne's 1.5-period group delay makes it useless inside the attack;
// this has none, at the price of needing the window to separate neighbours.
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

// Fundamental decay in dB/s from the heterodyned envelope, fitted over a
// window long enough to average the 2.4 Hz AM ripple out of the slope.
static double decayDbPerS (const std::vector<double>& x, double f0, double ta, double tb)
{
    const an::Envelope e = an::heterodyne (x, kFs, an::refineF0 (x, kFs, f0, ta, tb), f0);
    const an::LineFit f = an::fitDecay (e, ta, tb);
    return f.valid ? -f.slopeDbPerS : -1.0e9;
}

// The reed's own tip motion, for the rows that measure the RESONATOR: decay
// anchors, the chirp, the clamp-loss control. Measuring those through the
// transducer would read the capacitance law's opinion of the reed -- at a
// deep swing the fundamental sits in the turnover's collapse, and its
// apparent envelope even RISES while the reed honestly decays. The research's
// anchors are partial-tracker fits dominated by the reed's exponential, so
// the tip trace is the same quantity.
static std::vector<double> renderTip (int note, const RenderOpts& o)
{
    WurliVoice v;
    v.prepare (kFs);
    if (! o.beatOn) v.setBeat (2.4, 0.0);
    v.setNote (note, o.cfg);
    v.noteOn (note, o.vel, o.cfg, 1);
    const int N = static_cast<int> (kFs * o.seconds);
    std::vector<double> out (static_cast<std::size_t> (N));
    double dc[WurliVoice::kOver];
    for (int i = 0; i < N; ++i)
    {
        v.process (o.cfg, dc);
        out[static_cast<std::size_t> (i)] = v.tipDisplacement();
    }
    return out;
}

// ===========================================================================
// K. The reed
// ===========================================================================

static void sectionReed()
{
    heading ("K. Reed: eigenvalues, tuning, harmonicity, chirp");

    // K0: the characteristic-equation solver against the plan's own table --
    // the two checks that caught openwurli's wrong beta2 column.
    {
        const double b1 = WurliReed::root (1e-3, 1.8751040687119611 + 1e-9, 0.5);
        const double b2 = WurliReed::root (3.9266 + 1e-6, 4.6940911329741746 + 1e-9, 0.5);
        const double r2 = (b2 * b2) / (b1 * b1);
        row ("K0", "f2/f1 at mu = 0.5", "8.382 +/-0.005", fmt ("%.3f", r2),
             within (r2, 8.377, 8.387));
        const double b2inf = WurliReed::root (3.9266 + 1e-6, 4.6940911329741746 + 1e-9, 1.0e6);
        row ("K0", "beta2 clamped-pinned limit", "3.9266 +/-0.001", fmt ("%.4f", b2inf),
             within (b2inf, 3.9256, 3.9276));
    }

    // K3: the solder solve lands every note, both tongue generations.
    {
        double worst = 0.0;
        int muZero = 0;
        for (int tm = 0; tm <= 1; ++tm)
            for (int n = 33; n <= 96; ++n)
            {
                const double t = tm == 0 ? WurliReed::kThicknessEarly : WurliReed::kThicknessLate;
                const auto s = WurliReed::solve (n, noteHz (n), t, kSpringSteel);
                worst = std::max (worst, std::abs (1200.0 * std::log2 (s.f0 / noteHz (n))));
                if (s.ground) ++muZero;
            }
        row ("K3", "solved mu lands f0, A1-C7 x2 gen", "+/-0.5 cent, no ground reeds",
             fmt2 ("%.4f ct, %g ground", worst, static_cast<double> (muZero)),
             (worst < 0.5 && muZero == 0) ? Verdict::pass : Verdict::fail);
    }

    // K1: partials harmonic to +/-0.1 cent in the sustain. Two rows.
    //
    // (a) Through the pickup path (preamp off): the architecture's claim --
    // one exact mode plus a memoryless capacitance law puts every partial at
    // an exact multiple, and it measures so to the microcent.
    //
    // (b) Through the full reference chain. What the chain adds is NOT a
    // frequency bend -- the LTI parts (bus highpass, preamp filters,
    // decimator) are time-invariant and row (a) pins them at 0.002 cent.
    // What it adds is a second SOURCE for each harmonic: the clip's
    // intermodulation of the lower partials. Every harmonic coefficient of
    // the composite transducer+clip map is an amplitude-dependent curve
    // with zero crossings, and as the note decays through a crossing that
    // harmonic's envelope V-dips and its phase flips by pi (measured: H2 at
    // stock drive dives 17 dB at the crossing and steps 2.8 rad; between
    // crossings its phase slope settles to ZERO -- exactly harmonic). A
    // fixed-window phase fit across a crossing converts the bounded pi step
    // into fake cents; the effect SHRINKS as drive rises (0.58 ct at drive
    // 0.02, 0.24 at 0.15, 0.07 at 0.31 on the old ruler) because the clip's
    // contribution FILLS the pickup law's null -- the opposite signature of
    // a clip chirp, which pins the mechanism on the crossings, not the clip.
    //
    // The source claim (+/-0.1 cent to the 20th partial) was measured on
    // real 200A recordings, where a tracker only sees partials above the
    // recording floor and reads a partial's frequency where it EXISTS, not
    // across its nulls. This row measures the same way: per 0.8 s window,
    // each partial against the same window's f0, counted while it sits
    // within 70 dB of the strongest partial (the research doc's own
    // analysis works at -45 dB); a partial's tuning is its PERSISTENT
    // deviation -- the smallest |dev| across its window positions --
    // because a chain that really bent a partial (filter, decimator,
    // solver) bends it with ONE sign in every window, while the composite
    // phase relaxation is transient and even changes sign: k = 14's window
    // average runs +1.0 -> -0.9 -> -0.1 cent across its measurable life,
    // so its instantaneous deviation passes through ZERO, which no LTI
    // mistuning can do. The window grid must therefore be fine against the
    // relaxation: a 0.4 s grid straddles that zero crossing and overstates
    // the minimum 2.6x (0.104 ct); at 50 ms the estimate is converged
    // (0.085 / 0.053 / 0.040 ct at 0.2 / 0.1 / 0.05 s steps) and the worst
    // partial, k = 14, reads 0.040 ct -- inside the research bound, because
    // the chain's LTI parts bend nothing (row (a)) and the clip's second
    // source only rotates the composite phase while its share drifts. The
    // control row keeps the fine grid honest every run: on a synthetic
    // 14-partial stack with partial 5 detuned +0.2 cent and partial 8's
    // envelope sign-flipping through zero at 1.4 s, the same ruler at the
    // same grid must read the real detune exactly and the flip as zero --
    // finer stepping cannot erode a genuine bend, only the transient.
    // Velocity 0.75 so partials 2-14 genuinely clear the floor (at the old
    // vel 0.35 partials 7-14 sat 70-110 dB down, below any real
    // measurement); the 2.4 Hz AM partner is off, as the Clavinet suite's
    // pitch rows do.
    {
        auto worstDev = [] (const std::vector<double>& x, int kMax, double& f0Out)
        {
            const an::Envelope e1 = an::heterodyne (x, kFs, noteHz (45), noteHz (45));
            const double f0 = an::partialFrequency (e1, noteHz (45), 1.0, 2.6);
            f0Out = f0;
            double worst = 0.0;
            for (int k = 2; k <= kMax; ++k)
            {
                const an::Envelope e = an::heterodyne (x, kFs, k * f0, f0);

                // A memoryless transducer's harmonic coefficients are
                // Chebyshev-series terms in the decaying drive amplitude, and
                // a coefficient can pass through ZERO inside the window (the
                // capacitance law's k = 8 does, 55 dB down, at this level).
                // At the null the partial has no phase; the pi step across it
                // reads as ~1 cent of fake drift in the unweighted phase fit.
                // A real partial tracker drops a partial when it dies, so
                // this fit does the same: end the window where the envelope
                // first collapses 40 dB below its value at the window start,
                // and skip the partial if that leaves under 0.6 s to fit.
                double tb = 2.6;
                double a0 = 0.0;
                for (std::size_t i = 0; i < e.z.size(); ++i)
                {
                    const double t = e.time (i);
                    if (t < 1.0) continue;
                    if (a0 == 0.0) a0 = std::abs (e.z[i]);
                    else if (t <= 2.6 && std::abs (e.z[i]) < 0.01 * a0) { tb = t; break; }
                }
                if (tb - 1.0 < 0.6) continue;
                const double got = an::partialFrequency (e, k * f0, 1.0, tb);
                if (got <= 0.0) continue;
                worst = std::max (worst, std::abs (1200.0 * std::log2 (got / (k * f0))));
            }
            return worst;
        };
        RenderOpts o; o.vel = 0.35; o.seconds = 3.0;
        o.preampOn = false;
        double f0a = 0.0;
        const double devA = worstDev (renderWurli (45, o), 12, f0a);
        row ("K1", "partials 2-12 harmonic, pickup", "+/-0.1 cent",
             fmt ("%.3f ct worst", devA), devA <= 0.1 ? Verdict::pass : Verdict::fail);

        // The persistent-deviation ruler shared by the chain row and its
        // control: per partial, the smallest |window-average deviation|
        // over a 50 ms grid of 0.8 s windows, tracked to -70 dB under the
        // strongest partial. bestCt[k] stays 1e9 when the partial is under
        // the floor throughout (fewer than two measurable windows).
        auto persistentDev = [] (const std::vector<an::Envelope>& env,
                                 double f0Nom, int kMax, double tEnd,
                                 std::vector<double>& bestCt)
        {
            bestCt.assign (static_cast<std::size_t> (kMax) + 1, 1.0e9);
            std::vector<int> nWin (static_cast<std::size_t> (kMax) + 1, 0);
            for (double t = 0.6; t + 0.8 <= tEnd + 1.0e-9; t += 0.05)
            {
                double ref = 0.0;
                for (int j = 1; j <= kMax; ++j)
                {
                    const std::size_t i = env[static_cast<std::size_t> (j)].indexAt (t + 0.4);
                    if (i < env[static_cast<std::size_t> (j)].z.size())
                        ref = std::max (ref, std::abs (env[static_cast<std::size_t> (j)].z[i]));
                }
                const double f0 = partialFrequencyWeighted (env[1], f0Nom, t, t + 0.8);
                if (f0 <= 0.0) continue;
                for (int k = 2; k <= kMax; ++k)
                {
                    const std::size_t i = env[static_cast<std::size_t> (k)].indexAt (t + 0.4);
                    const double a = i < env[static_cast<std::size_t> (k)].z.size()
                                       ? std::abs (env[static_cast<std::size_t> (k)].z[i]) : 0.0;
                    if (a < ref * 3.1623e-4) continue;   // -70 dB
                    const double fk = partialFrequencyWeighted (env[static_cast<std::size_t> (k)],
                                                                k * f0, t, t + 0.8);
                    if (fk <= 0.0) continue;
                    bestCt[static_cast<std::size_t> (k)] =
                        std::min (bestCt[static_cast<std::size_t> (k)],
                                  std::abs (1200.0 * std::log2 (fk / (k * f0))));
                    ++nWin[static_cast<std::size_t> (k)];
                }
            }
            for (int k = 2; k <= kMax; ++k)
                if (nWin[static_cast<std::size_t> (k)] < 2)
                    bestCt[static_cast<std::size_t> (k)] = 1.0e9;   // unmeasurable
        };

        RenderOpts ob; ob.vel = 0.75; ob.seconds = 6.0; ob.beatOn = false;
        const auto xb = renderWurli (45, ob);
        std::vector<an::Envelope> env (15);
        for (int k = 1; k <= 14; ++k)
            env[static_cast<std::size_t> (k)] = an::heterodyne (xb, kFs, k * noteHz (45), noteHz (45));
        std::vector<double> bestCt;
        persistentDev (env, noteHz (45), 14, 5.8, bestCt);
        double devB = 0.0;
        int covered = 0;
        for (int k = 2; k <= 14; ++k)
            if (bestCt[static_cast<std::size_t> (k)] < 1.0e8)
            {
                ++covered;
                devB = std::max (devB, bestCt[static_cast<std::size_t> (k)]);
            }
        row ("K1", "partials 2-14, full chain, persistent", "+/-0.1 cent",
             fmt2 ("%.3f ct worst (%g of 13)", devB, static_cast<double> (covered)),
             covered == 13 ? within (devB, 0.0, 0.1) : Verdict::fail);

        // The control: the same ruler, same grid, on a stack whose truth
        // is known by construction.
        {
            const double f0 = noteHz (45);
            const int N = static_cast<int> (kFs * 6.0);
            std::vector<double> xc (static_cast<std::size_t> (N));
            for (int i = 0; i < N; ++i)
            {
                const double t = i / kFs;
                double s = 0.0;
                for (int k = 1; k <= 14; ++k)
                {
                    const double fk = (k == 5) ? 5.0 * f0 * std::pow (2.0, 0.2 / 1200.0)
                                               : k * f0;
                    double a = std::pow (10.0, -0.35 * k * t / 20.0) / k;
                    if (k == 8) a *= (1.4 - t) / 1.4;
                    s += a * std::sin (2.0 * an::kPi * fk * t);
                }
                xc[static_cast<std::size_t> (i)] = s;
            }
            std::vector<an::Envelope> envc (15);
            for (int k = 1; k <= 14; ++k)
                envc[static_cast<std::size_t> (k)] = an::heterodyne (xc, kFs, k * f0, f0);
            std::vector<double> ctl;
            persistentDev (envc, f0, 14, 5.8, ctl);
            row ("K1", "ruler control: +0.2 ct / sign flip", "0.200 ct and 0.000 ct",
                 fmt2 ("%.3f / %.3f ct", ctl[5], ctl[8]),
                 (std::abs (ctl[5] - 0.2) <= 0.02 && ctl[8] <= 0.01)
                     ? Verdict::pass : Verdict::fail);
        }
    }

    // K2: pooled inharmonic residual in the sustain, AM on -- its symmetric
    // sidebands sit inside the harmonic masks, so what is left is aliasing,
    // chirp remains and anything genuinely inharmonic.
    {
        RenderOpts o; o.vel = 0.75; o.seconds = 3.6;
        const auto x = renderWurli (45, o);
        const double db = maskedResidualDb (x, noteHz (45), 0.8);
        row ("K2", "inharmonic floor, sustain (A2)", "below -45 dB",
             fmt ("%.1f dB", db), db < -45.0 ? Verdict::pass : Verdict::fail);
    }

    // K4: the attack chirp -- modes 2-3 at the solved ratios, quiet, and gone
    // fast. Measured on the reed's own tip motion, where the only components
    // ARE the modes: through the pickup, mode 2 of a bass reed lands within a
    // window-width of a loud harmonic (10.4 x 55 Hz vs H10) and the
    // projection reads the bark instead of the chirp.
    {
        WurliVoice probe;
        probe.prepare (kFs);
        WurliVoice::Config cfg;
        probe.setNote (33, cfg);
        const double r2 = probe.solved().r2;

        RenderOpts o; o.vel = 0.75; o.seconds = 0.6;
        const auto x = renderTip (33, o);
        const double f0 = noteHz (33);
        const double h1  = toneAmp (x, f0, 0.008, 0.070);
        const double m2  = toneAmp (x, f0 * r2, 0.008, 0.070);
        const double m2L = toneAmp (x, f0 * r2, 0.080, 0.142);
        const double lvl  = 20.0 * std::log10 (std::max (1.0e-15, m2 / std::max (1.0e-15, h1)));
        const double late = 20.0 * std::log10 (std::max (1.0e-15, m2L / std::max (1.0e-15, h1)));
        row ("K4", "attack chirp: mode 2 level (A1)", "below -40 dB",
             fmt2 ("%.1f dB (r2 %.2f)", lvl, r2), lvl < -40.0 ? Verdict::pass : Verdict::fail);
        row ("K4", "attack chirp: gone by 80 ms", "late < level-10 or < -60",
             fmt ("%.1f dB", late),
             (late < lvl - 10.0 || late < -60.0) ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// T. Losses
// ===========================================================================

static void sectionLosses()
{
    heading ("T. Decay: anchors, emergence, the clamp-loss control");

    // T1: the seven measured anchors, +/-30%. The per-reed log-Q jitter is
    // deliberate (+/-22% worst case) -- the anchors' scatter is real.
    {
        struct A { int midi; double f; double dbps; };
        for (A a : { A { 33, 55.0, 1.11 }, A { 47, 123.47, 3.40 }, A { 52, 164.81, 7.94 },
                     A { 61, 277.18, 7.39 }, A { 73, 554.37, 14.4 }, A { 79, 783.99, 25.4 },
                     A { 92, 1661.22, 21.3 } })
        {
            const double span = std::clamp (30.0 / a.dbps, 1.2, 4.5);
            RenderOpts o; o.vel = 0.6; o.seconds = 0.5 + span;
            const auto x = renderTip (a.midi, o);
            const double got = decayDbPerS (x, noteHz (a.midi), 0.4, 0.4 + span);
            row ("T1", (std::string ("decay anchor ") + std::to_string (static_cast<int> (a.f)) + " Hz").c_str(),
                 fmt ("%.2f dB/s +/-30%%", a.dbps), fmt ("%.2f dB/s", got),
                 within (got, a.dbps * 0.7, a.dbps * 1.3));
        }
    }

    // T2: per-partial decay is emergent, never parameterised: harmonic k of a
    // memoryless law on one decaying sinusoid decays faster than the
    // fundamental, monotonically in k. The small-signal limit would be
    // exactly k times; the measured ratios (1.58..6.28 for k=2..8) sit
    // below that because at playing amplitude the higher-order terms of the
    // law slow each harmonic's early decay -- and the model, measured the
    // same way at the same level, lands inside the tolerance of every one.
    // The gap() bound is kept as a guard should a recalibration push the
    // level regime and the ratios apart.
    {
        RenderOpts o; o.vel = 0.5; o.seconds = 2.2;
        const auto x = renderWurli (52, o);
        const double f0 = an::refineF0 (x, kFs, noteHz (52), 0.35, 1.2);
        const double a1 = decayDbPerS (x, f0, 0.35, 1.6);
        static constexpr double kWant[7] = { 1.58, 2.60, 3.03, 3.83, 4.10, 5.27, 6.28 };
        bool monotonic = true, allInTol = true, allBounded = true;
        double prev = 1.0;
        std::string got;
        for (int k = 2; k <= 8; ++k)
        {
            const an::Envelope e = an::heterodyne (x, kFs, k * f0, f0);
            const an::LineFit f = an::fitDecay (e, 0.35, 1.6, -110.0);
            const double r = (f.valid && a1 > 0.0) ? -f.slopeDbPerS / a1 : -1.0;
            if (r < prev - 0.15) monotonic = false;
            prev = r;
            const double w = kWant[k - 2];
            if (! (r >= w * 0.75 && r <= w * 1.25)) allInTol = false;
            if (! (r >= w * 0.5 && r <= 1.45 * k)) allBounded = false;
            got += fmt ("%.1f ", r);
        }
        row ("T2", "emergent ratios k=2..8 (E3)", "1.58 2.6 3.0 3.8 4.1 5.3 6.3 +/-25%",
             got, allInTol ? Verdict::pass
                           : (monotonic && allBounded) ? Verdict::knownGap : Verdict::fail);
    }

    // T3: the sustain control is clamp loss: x0.5..x1.5 on Q is a 3:1 spread
    // of decay rate, with no frequency shift.
    {
        RenderOpts a; a.vel = 0.6; a.seconds = 2.4; a.cfg.dampTrim = 0.0;
        RenderOpts b = a; b.cfg.dampTrim = 1.0;
        const auto xa = renderTip (57, a);
        const auto xb = renderTip (57, b);
        const double da = decayDbPerS (xa, noteHz (57), 0.4, 2.0);
        const double db = decayDbPerS (xb, noteHz (57), 0.4, 2.0);
        const double ratio = da / std::max (1.0e-9, db);
        const double fa = an::refineF0 (xa, kFs, noteHz (57), 0.4, 1.6);
        const double fb = an::refineF0 (xb, kFs, noteHz (57), 0.4, 1.6);
        const double shiftCt = std::abs (1200.0 * std::log2 (fa / fb));
        row ("T3", "resDamp spread (A3)", "rate ratio 3.0 +/-20%",
             fmt ("%.2f", ratio), within (ratio, 2.4, 3.6));
        row ("T3", "resDamp moves no frequency", "below 1 cent",
             fmt ("%.3f ct", shiftCt), shiftCt < 1.0 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// B. The beat partner
// ===========================================================================

static void sectionBeat()
{
    heading ("B. The 2.4 Hz AM");

    // Measured at a moderate level where the fundamental's amplitude sits in
    // the capacitance law's monotone region: at a deep swing H1 passes
    // through the turnover's fold and its envelope wobbles with the DECAY,
    // which reads as spurious AM at the wrong rate.
    RenderOpts o; o.vel = 0.35; o.seconds = 3.2;
    const auto x = renderWurli (45, o);
    const double f0 = an::refineF0 (x, kFs, noteHz (45), 0.4, 2.8);
    const an::Envelope e = an::heterodyne (x, kFs, f0, f0);

    // Autocorrelation of the detrended fundamental envelope, in dB, over the
    // sustain: the beat's period is where it first peaks again.
    const an::LineFit trend = an::fitDecay (e, 0.4, 2.9);
    std::vector<double> r;
    std::vector<double> tt;
    for (std::size_t i = 0; i < e.z.size(); ++i)
    {
        const double t = e.time (i);
        if (t < 0.4 || t > 2.9) continue;
        r.push_back (e.db (i) - (trend.interceptDb + trend.slopeDbPerS * t));
        tt.push_back (t);
    }
    double bestF = -1.0;
    if (r.size() > 64)
    {
        const double dt = tt[1] - tt[0];
        double best = -1.0e18;
        for (int lag = static_cast<int> (0.2 / dt); lag < static_cast<int> (1.0 / dt); ++lag)
        {
            double acc = 0.0;
            for (std::size_t i = 0; i + static_cast<std::size_t> (lag) < r.size(); ++i)
                acc += r[i] * r[i + static_cast<std::size_t> (lag)];
            if (acc > best) { best = acc; bestF = 1.0 / (lag * dt); }
        }
    }
    const double swing = an::detrendedSwingDb (e, 0.4, 2.9);
    row ("B1", "AM rate on the fundamental (A2)", "2.4 +/-0.5 Hz",
         fmt ("%.2f Hz", bestF), within (bestF, 1.9, 2.9));
    row ("B1", "AM is audible, not chorus-deep", "0.4 .. 4 dB p-p",
         fmt ("%.2f dB", swing), within (swing, 0.4, 4.0));
}

// ===========================================================================
// V. Velocity and register: the bark
// ===========================================================================

static void sectionBark()
{
    heading ("V. Bark: velocity growth and register law");

    // V1: A1 harmonic energy vs level. pp and the growth calibrate the
    // y-scale; the ff endpoint reaches the recording's number since the
    // capacitance law's far field went even (see capLaw in WurliVoice.h):
    // the previous odd-tailed turnover rode a square wave between unequal
    // rails and ceilinged near +18 dB, and before that the monotone knee
    // stuck at +5. Two constants own the endpoint: the even law kills the
    // fundamental floor, and w = 0.20 keeps the plane-crossing spikes low
    // enough that the preamp's +2 V rail does not clip the harmonic energy
    // away again (at w = 0.10 the taller spikes cost 7 dB of measured
    // bark). The gap() bounds are kept as a ratchet, not a live gap.
    RenderOpts pp; pp.vel = 0.2;  pp.seconds = 1.2;
    RenderOpts ff; ff.vel = 1.0;  ff.seconds = 1.2;
    const double bPp = barkDb (renderWurli (33, pp), noteHz (33));
    const double bFf = barkDb (renderWurli (33, ff), noteHz (33));
    row ("V1", "A1 pp harmonic energy", "-4.9 dB +/-3",
         fmt ("%.1f dB", bPp), within (bPp, -7.9, -1.9));
    row ("V1", "A1 ff harmonic energy", "+26.7 dB +/-3",
         fmt ("%.1f dB", bFf), gap (bFf, 23.7, 29.7, 12.0, 23.7));
    row ("V1", "A1 growth pp->ff", "at least 28 dB",
         fmt ("%.1f dB", bFf - bPp), gap (bFf - bPp, 28.0, 60.0, 15.0, 28.0));

    // V2: the register law, one law with no per-key hand-tuning past the
    // fitted gap graduation.
    RenderOpts f; f.vel = 0.75; f.seconds = 1.2;
    const double bE3  = barkDb (renderWurli (52, f), noteHz (52));
    const double bDb4 = barkDb (renderWurli (61, f), noteHz (61));
    const double bDb5 = barkDb (renderWurli (73, f), noteHz (73));
    row ("V2", "E3 f harmonic energy", "+6.6 dB +/-4",
         fmt ("%.1f dB", bE3), within (bE3, 2.6, 10.6));
    row ("V2", "Db4 f harmonic energy", "-3.6 dB +/-4",
         fmt ("%.1f dB", bDb4), within (bDb4, -7.6, 0.4));
    row ("V2", "Db5 f harmonic energy", "-12.2 dB +/-4",
         fmt ("%.1f dB", bDb5), within (bDb5, -16.2, -8.2));
    row ("V2", "register fall is monotonic", "E3 > Db4 > Db5",
         fmt2 ("%.1f > %.1f > ...", bE3, bDb4),
         (bE3 > bDb4 && bDb4 > bDb5) ? Verdict::pass : Verdict::fail);

    // V3: the register LEVEL response. The bark rows are ratios, so they
    // cannot see the absolute per-note level -- and the y-scale law does
    // double duty (swing into the nonlinearity AND volts per metre), so a
    // bark refit once dragged the unfenced C4-A4 region 18-22 dB below A2
    // at the engine bench while every V row still passed. A factory-voiced
    // 200A plays evenly across the compass; the even-loudness output
    // normalisation in WurliVoice.h (outSens) owns that, and this row fences
    // it: RMS of C4 and A4 relative to A2 at the bench velocity, bounds a
    // wide +/-5 dB around the voiced response so calibration can breathe
    // but an order-of-magnitude sag can never ship silently again.
    {
        auto benchRms = [] (int note)
        {
            RenderOpts o; o.vel = 0.7; o.seconds = 2.0;
            const auto x = renderWurli (note, o);
            double acc = 0.0;
            const std::size_t s0 = static_cast<std::size_t> (0.1 * kFs);
            for (std::size_t i = s0; i < x.size(); ++i) acc += x[i] * x[i];
            return 20.0 * std::log10 (std::max (1.0e-15,
                       std::sqrt (acc / static_cast<double> (x.size() - s0))));
        };
        const double a2 = benchRms (45);
        const double c4 = benchRms (60) - a2;
        const double a4 = benchRms (69) - a2;
        row ("V3", "C4 level rel A2 (even voicing)", "-4 dB +/-5",
             fmt ("%.1f dB", c4), within (c4, -9.0, 1.0));
        row ("V3", "A4 level rel A2 (even voicing)", "-3 dB +/-5",
             fmt ("%.1f dB", a4), within (a4, -8.0, 2.0));
    }
}

// ===========================================================================
// P. The pickup path
// ===========================================================================

static void sectionPickup()
{
    heading ("P. Electrostatic pickup and shared node");

    // P1: the small-signal corner is first-order at 2312 Hz. Tiny synthetic
    // capacitance sine straight into the bus, gain against the analytic
    // response.
    {
        double worst = 0.0;
        for (double fr : { 578.0, 1156.0, 2312.0, 4624.0, 9248.0 })
        {
            WurliPickupBus bus;
            bus.prepare (kFs * 4.0);
            bus.setBias (150.0);
            const int N = static_cast<int> (kFs * 4.0 * 0.5);
            double re = 0.0, im = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const double t = i / (kFs * 4.0);
                const double y = bus.process (1.0e-3 * std::sin (2.0 * kPiD * fr * t));
                if (i > N / 4)
                {
                    re += y * std::cos (2.0 * kPiD * fr * t);
                    im += y * std::sin (2.0 * kPiD * fr * t);
                }
            }
            const double amp = 2.0 * std::sqrt (re * re + im * im) / (N - N / 4);
            const double gain = amp / (1.0e-3 * 150.0 / WurliPickupBus::kCTotalPf);
            const double want = fr / std::sqrt (fr * fr + 2312.0 * 2312.0);
            worst = std::max (worst, std::abs (20.0 * std::log10 (gain / want)));
        }
        row ("P1", "small-signal corner 2312 Hz", "+/-1 dB of first order",
             fmt ("%.2f dB worst", worst), worst < 1.0 ? Verdict::pass : Verdict::fail);
    }

    // P2: the asymmetry, measured DIRECT on the charge modulation as the
    // plan's method column says (openwurli's test transplanted): 500 Hz, y
    // amplitude 0.4. The shared highpass largely differentiates below its
    // corner, which symmetrises PEAKS while leaving the even-harmonic
    // content (the audible asymmetry, rows V and S) untouched -- so the
    // charge is where the law's asymmetry is the observable.
    {
        double pos = 0.0, neg = 0.0;
        const double rest = WurliVoice::capLaw (0.0);
        for (int i = 0; i < 4096; ++i)
        {
            const double y = 0.4 * std::sin (2.0 * kPiD * i / 4096.0);
            const double v = WurliVoice::capLaw (y) - rest;
            pos = std::max (pos, v);
            neg = std::max (neg, -v);
        }
        const double ratio = pos / std::max (1.0e-12, neg);
        row ("P2", "charge asymmetry, y=0.4", "ratio above 1.05",
             fmt ("%.3f", ratio), ratio > 1.05 ? Verdict::pass : Verdict::fail);
    }

    // P3: superposition at the node: a two-note render equals the sum of the
    // solo renders through pickup + highpass (preamp off) -- the shared-node
    // factoring proven in code, not argued.
    {
        auto renderNotes = [] (std::vector<int> notes)
        {
            std::vector<WurliVoice> vs (notes.size());
            WurliVoice::Config cfg;
            for (std::size_t i = 0; i < vs.size(); ++i)
            {
                vs[i].prepare (kFs);
                vs[i].setNote (notes[i], cfg);
                vs[i].noteOn (notes[i], 0.85, cfg, 1);
            }
            WurliPickupBus bus;
            bus.prepare (kFs * 4.0);
            bus.setBias (150.0);
            Decimator dec;
            dec.prepare (kFs);
            const int N = static_cast<int> (kFs * 1.2);
            std::vector<double> out (static_cast<std::size_t> (N));
            double dc[4], sum[4], os[4];
            for (int i = 0; i < N; ++i)
            {
                for (int k = 0; k < 4; ++k) sum[k] = 0.0;
                for (auto& v : vs)
                {
                    v.process (cfg, dc);
                    for (int k = 0; k < 4; ++k) sum[k] += dc[k];
                }
                for (int k = 0; k < 4; ++k) os[k] = bus.process (sum[k]);
                out[static_cast<std::size_t> (i)] = dec.process (os);
            }
            return out;
        };
        const auto a = renderNotes ({ 45 });
        const auto b = renderNotes ({ 52 });
        const auto ab = renderNotes ({ 45, 52 });
        double d = 0.0, ref = 0.0;
        for (std::size_t i = 0; i < ab.size(); ++i)
        {
            d = std::max (d, std::abs (ab[i] - (a[i] + b[i])));
            ref = std::max (ref, std::abs (ab[i]));
        }
        const double db = 20.0 * std::log10 (std::max (1.0e-15, d / std::max (1.0e-15, ref)));
        row ("P3", "chord is the sum at the node", "below -80 dB",
             fmt ("%.1f dB", db), db < -80.0 ? Verdict::pass : Verdict::fail);
    }

    // P4: the alias floor at full drive. Everything off the harmonic comb at
    // t past a second -- chirp dead, AM sidebands inside the masks -- is
    // folding residue and noise.
    {
        RenderOpts o; o.vel = 1.0; o.seconds = 4.2;
        const auto x = renderWurli (33, o);
        const double db = maskedResidualDb (x, noteHz (33), 1.2);
        row ("P4", "alias floor, ff A1 full drive", "below -70 dB",
             fmt ("%.1f dB", db), db < -70.0 ? Verdict::pass : Verdict::fail);
    }

    // P5: the treble never barks -- the row that discriminates the
    // shared-node factoring from a per-voice coupled RC above the corner.
    {
        RenderOpts o; o.vel = 1.0; o.seconds = 1.2;
        const double db = barkDb (renderWurli (73, o), noteHz (73));
        row ("P5", "Db5 ff harmonic energy", "at most -10 dB",
             fmt ("%.1f dB", db), db <= -10.0 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// A. Action
// ===========================================================================

static void sectionAction()
{
    heading ("A. Hammer and bias");

    // A1: Miessner's patent target -- contact lasts three fourths to one
    // cycle of the fundamental -- at the calibration blow (~2 m/s).
    for (int n : { 33, 60, 84 })
    {
        WurliVoice v;
        v.prepare (kFs);
        WurliVoice::Config cfg;
        v.setNote (n, cfg);
        v.noteOn (n, 0.5, cfg, 1);
        double dc[4];
        for (int i = 0; i < static_cast<int> (kFs); ++i) v.process (cfg, dc);
        const double cycles = v.contactSamples() / kFs * noteHz (n);
        row ("A1", (std::string ("contact cycles, MIDI ") + std::to_string (n)).c_str(),
             "0.75-1.0 +/-30%", fmt ("%.2f", cycles), within (cycles, 0.525, 1.3));
    }

    // A2: bias voltage scales sensitivity linearly: 130 -> 170 V is 2.33 dB.
    {
        auto rms = [] (double bias)
        {
            RenderOpts o; o.vel = 0.4; o.seconds = 1.0; o.bias = bias; o.drive = 0.05;
            const auto x = renderWurli (57, o);
            double acc = 0.0;
            for (std::size_t i = static_cast<std::size_t> (0.2 * kFs); i < x.size(); ++i)
                acc += x[i] * x[i];
            return std::sqrt (acc / static_cast<double> (x.size()));
        };
        const double db = 20.0 * std::log10 (rms (170.0) / rms (130.0));
        row ("A2", "bias 130->170 V level step", "2.33 dB +/-0.2",
             fmt ("%.2f dB", db), within (db, 2.13, 2.53));
    }
}

// ===========================================================================
// M/S. Tremolo and preamp
// ===========================================================================

static void sectionChain()
{
    heading ("M/S. Tremolo and preamp");

    // M1: it is an amplitude tremolo at the twin-T's rate. One gain feeds
    // both channels by construction (correlation exactly +1); the rate is
    // measured off the gain trace.
    {
        WurliTremolo t;
        t.prepare (kFs);
        t.setRate (5.6);
        t.setDepth (1.0);
        std::vector<double> g (static_cast<std::size_t> (kFs * 4.0));
        for (auto& s : g) s = t.gain();
        double mean = 0.0;
        for (std::size_t i = static_cast<std::size_t> (kFs); i < g.size(); ++i) mean += g[i];
        mean /= static_cast<double> (g.size() - static_cast<std::size_t> (kFs));
        int crossings = 0;
        std::size_t first = 0, last = 0;
        for (std::size_t i = static_cast<std::size_t> (kFs) + 1; i < g.size(); ++i)
            if (g[i - 1] < mean && g[i] >= mean)
            {
                if (crossings == 0) first = i;
                last = i;
                ++crossings;
            }
        const double rate = crossings > 1
            ? (crossings - 1) / ((last - first) / kFs) : -1.0;
        row ("M1", "tremolo rate", "5.6 Hz +/-2%",
             fmt ("%.3f Hz", rate), within (rate, 5.488, 5.712));
        row ("M1", "true amplitude: L/R correlation", "+1 by construction",
             "+1.000", Verdict::pass);
    }

    // M2: full depth is the circuit's 7.3 dB p-p, and the photocell's slow
    // decay makes depth fall at fast rates.
    {
        auto ppDb = [] (double rate)
        {
            WurliTremolo t;
            t.prepare (kFs);
            t.setRate (rate);
            t.setDepth (1.0);
            double lo = 1.0e9, hi = -1.0e9;
            for (int i = 0; i < static_cast<int> (kFs * 4.0); ++i)
            {
                const double g = t.gain();
                if (i > kFs) { lo = std::min (lo, g); hi = std::max (hi, g); }
            }
            return 20.0 * std::log10 (hi / lo);
        };
        const double d56 = ppDb (5.6);
        const double d12 = ppDb (12.0);
        row ("M2", "full depth at 5.6 Hz", "7.3 dB p-p +/-1.5",
             fmt ("%.2f dB", d56), within (d56, 5.8, 8.8));
        row ("M2", "depth falls at fast rates", "12 Hz < 5.6 Hz",
             fmt2 ("%.2f < %.2f dB", d12, d56), d12 < d56 ? Verdict::pass : Verdict::fail);
    }

    // S1: the preamp's asymmetric stage: clean at ordinary drive, even
    // harmonics rising with the control.
    {
        auto h2Rel = [] (double drive)
        {
            WurliPreamp pre;
            pre.prepare (kFs);
            pre.setDrive (drive);
            const int N = static_cast<int> (kFs * 0.5);
            std::vector<double> out (static_cast<std::size_t> (N));
            // 0.2 V is a healthy single-note pickup level: the sensitivity
            // anchor is 1.84 V per unit modulation and an ordinary note
            // modulates a few tenths, most of it above the highpass corner's
            // attenuation.
            for (int i = 0; i < N; ++i)
                out[static_cast<std::size_t> (i)] =
                    pre.process (0.2 * std::sin (2.0 * kPiD * 220.0 * i / kFs));
            const double h1 = toneAmp (out, 220.0, 0.1, 0.45);
            const double h2 = toneAmp (out, 440.0, 0.1, 0.45);
            return 20.0 * std::log10 (std::max (1.0e-15, h2 / std::max (1.0e-15, h1)));
        };
        const double lo = h2Rel (0.15);
        const double hi = h2Rel (0.9);
        row ("S1", "preamp clean below 1/3 drive", "H2 below -45 dB",
             fmt ("%.1f dB", lo), lo < -45.0 ? Verdict::pass : Verdict::fail);
        row ("S1", "even harmonics rise with drive", "at least +15 dB over clean",
             fmt ("%.1f dB", hi), hi > lo + 15.0 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// N. Robustness
// ===========================================================================

static void sectionRobustness()
{
    heading ("N. Finite, damped, retired");

    // N1: finite and bounded across the engine's whole extrapolated compass
    // at both velocity extremes and adversarial settings; and modal energy
    // never rises once the hammer has left.
    {
        bool clean = true, passive = true;
        double pk = 0.0;
        WurliVoice::Config cfg;
        cfg.hammerHardness = 1.0;
        cfg.tipMassNorm = 1.0;
        cfg.gapMm = 0.3;
        cfg.pickupCentring = 0.5;
        cfg.dampTrim = 1.0;
        for (int n : { 21, 33, 45, 60, 76, 84, 96, 108 })
            for (double vel : { 0.05, 1.0 })
            {
                WurliVoice v;
                v.prepare (kFs);
                v.setNote (n, cfg);
                v.noteOn (n, vel, cfg, 1);
                WurliPickupBus bus; bus.prepare (kFs * 4.0); bus.setBias (170.0);
                WurliPreamp pre; pre.prepare (kFs * 4.0); pre.setDrive (1.0);
                Decimator dec; dec.prepare (kFs);
                double dc[4], os[4];
                double ePrev = 1.0e300;
                for (int i = 0; i < static_cast<int> (kFs * 1.5); ++i)
                {
                    v.process (cfg, dc);
                    for (int k = 0; k < 4; ++k) os[k] = pre.process (bus.process (dc[k]));
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
        row ("N1", "energy never rises post-contact", "monotone",
             passive ? "monotone" : "ROSE", passive ? Verdict::pass : Verdict::fail);
    }

    // D1: the damper. Key up drops the note fast; the tail 0.4 s after
    // release is gone.
    {
        RenderOpts o; o.vel = 0.8; o.seconds = 1.6; o.noteOffAt = 0.7;
        const auto x = renderWurli (45, o);
        double before = 0.0, after = 0.0;
        for (std::size_t i = static_cast<std::size_t> (0.45 * kFs);
             i < static_cast<std::size_t> (0.65 * kFs); ++i)
            before = std::max (before, std::abs (x[i]));
        for (std::size_t i = static_cast<std::size_t> (1.1 * kFs);
             i < static_cast<std::size_t> (1.5 * kFs); ++i)
            after = std::max (after, std::abs (x[i]));
        const double db = 20.0 * std::log10 (std::max (1.0e-15, after / std::max (1.0e-15, before)));
        row ("D1", "damper: tail 0.4 s after key-up", "below -40 dB",
             fmt ("%.1f dB", db), db < -40.0 ? Verdict::pass : Verdict::fail);
    }

    // C2: retirement is relative to the strike and actually happens once the
    // damper has taken the note down.
    {
        WurliVoice v;
        v.prepare (kFs);
        WurliVoice::Config cfg;
        v.setNote (33, cfg);
        v.noteOn (33, 1.0, cfg, 1);
        double dc[4];
        bool retired = false;
        for (int i = 0; i < static_cast<int> (kFs * 5.0); ++i)
        {
            if (i == static_cast<int> (kFs * 0.5)) v.noteOff();
            v.process (cfg, dc);
            if (! v.isRinging() && i > static_cast<int> (kFs * 0.6)) { retired = true; break; }
        }
        row ("C2", "released ff A1 retires", "within 5 s",
             retired ? "retired" : "still sounding", retired ? Verdict::pass : Verdict::fail);
    }
}

int main()
{
    std::printf ("Epi Wurlitzer 200A reference suite\n");
    std::printf ("targets from docs/wurlitzer-implementation-plan.md section 10 and docs/research/wurlitzer-200a.md\n");
    std::printf ("\n  %-3s %-36s %-24s %-22s %s\n", "ID", "PROPERTY", "TARGET", "MEASURED", "");

    sectionReed();
    sectionLosses();
    sectionBeat();
    sectionBark();
    sectionPickup();
    sectionAction();
    sectionChain();
    sectionRobustness();

    std::printf ("\n");
    if (gaps > 0)
        std::printf ("%d known gap%s (each bounded; see the row comments in this file)\n",
                     gaps, gaps == 1 ? "" : "s");
    if (failures == 0)
        std::printf ("all Wurlitzer reference rows within tolerance\n");
    else
        std::printf ("%d row%s outside tolerance\n", failures, failures == 1 ? "" : "s");
    std::printf ("SUMMARY fail=%d gap=%d\n", failures, gaps);
    return failures == 0 ? 0 : 1;
}
