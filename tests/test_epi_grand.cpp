/*
  Epi — grand piano reference rows, step 2 of docs/grand-implementation-plan.md.

  Covers the string + bridge + board stage (no radiator, no pedals beyond the
  basic damper): rows E1, K1-K3, U1-U2, W1-W5, P1 from the plan's section 10,
  plus finiteness. Every target is the plan's own Salamander C5 measurement.

  The rendered signal is the summed full-band termination force -- the feed
  the step-3 radiator will consume -- because the coupled band's effect on
  the STRINGS (decay knees, filled nulls, broken superposition) is the object
  under test, and the string state carries all of it.

  Build: part of ctest, target epi_grand_tests.
*/

#include "EpiAnalysis.h"
#include "epi/dsp/GrandBoard.h"
#include "epi/dsp/GrandVoice.h"

#include <algorithm>
#include <complex>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace epi;
namespace an = epianalysis;
using cplx = std::complex<double>;

// ===========================================================================
// Reporting (the reference suite's row machinery, local to this binary)
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
    std::printf ("  %-3s %-36s %-24s %-24s %s\n", id, what, target.c_str(), got.c_str(), mark);
}

static Verdict within (double v, double lo, double hi)
{
    return (v >= lo && v <= hi) ? Verdict::pass : Verdict::fail;
}

static Verdict gapV (double v, double lo, double hi, double boundLo, double boundHi)
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
// Rendering: board + voices, the step-2 rig
// ===========================================================================

static constexpr double kFs = 48000.0;

static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

struct Strike { int note; double vel; };

static std::vector<double> renderGrand (const std::vector<Strike>& strikes, double seconds,
                                        bool release = false, double releaseAt = 0.0,
                                        const GrandVoice::Config& cfg = {})
{
    const int N = static_cast<int> (kFs * seconds);
    GrandBoard board;
    board.prepare (kFs);
    std::vector<std::unique_ptr<GrandVoice>> vs;
    for (const Strike& s : strikes)
    {
        vs.push_back (std::make_unique<GrandVoice>());
        vs.back()->prepare (kFs);
        vs.back()->noteOn (s.note, s.vel, cfg, board, 0);
    }
    const int relSample = static_cast<int> (releaseAt * kFs);
    std::vector<double> x (static_cast<std::size_t> (N), 0.0);
    for (int i = 0; i < N; ++i)
    {
        if (release && i == relSample)
            for (auto& v : vs) v->noteOff();
        double f = 0.0;
        for (auto& v : vs)
        {
            f += v->process (cfg, board);
            v->applyDamperIfDue();
        }
        board.tick();
        x[static_cast<std::size_t> (i)] = f;
    }
    return x;
}

static std::vector<double> renderNote (int note, double vel, double seconds)
{
    return renderGrand ({ { note, vel } }, seconds);
}

// ===========================================================================
// Complex-exponential (Prony) fit on the heterodyned baseband: resolves the
// coupled unison's normal modes -- components fractions of a hertz apart with
// decay rates an order of magnitude apart -- which no envelope-segment fit
// can separate. Order stays small (<= 6), so plain normal equations and
// Durand-Kerner roots are enough.
// ===========================================================================

struct CexpComp
{
    double levelDb0 = 0.0;    // component level extrapolated to t = 0
    double levelDbT0 = 0.0;   // component level at the fit segment's start
    double rateDbPerS = 0.0;  // negative while decaying
    double freqHz = 0.0;      // offset from the analysis frequency
};

static void solveComplex (int n, cplx A[][8], cplx* b)
{
    for (int c = 0; c < n; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs (A[r][c]) > std::abs (A[piv][c])) piv = r;
        if (piv != c)
        {
            for (int cc = 0; cc < n; ++cc) std::swap (A[c][cc], A[piv][cc]);
            std::swap (b[c], b[piv]);
        }
        if (std::abs (A[c][c]) < 1.0e-280) continue;
        for (int r = c + 1; r < n; ++r)
        {
            const cplx f = A[r][c] / A[c][c];
            for (int cc = c; cc < n; ++cc) A[r][cc] -= f * A[c][cc];
            b[r] -= f * b[c];
        }
    }
    for (int r = n - 1; r >= 0; --r)
    {
        cplx s = b[r];
        for (int c = r + 1; c < n; ++c) s -= A[r][c] * b[c];
        b[r] = std::abs (A[r][r]) > 1.0e-280 ? s / A[r][r] : cplx (0.0);
    }
}

// Roots of the monic polynomial z^p + c[p-1] z^(p-1) + ... + c[0].
static void durandKerner (int p, const cplx* c, cplx* roots)
{
    const cplx seed (0.4, 0.9);
    cplx r[8];
    r[0] = seed;
    for (int i = 1; i < p; ++i) r[i] = r[i - 1] * seed;
    for (int it = 0; it < 200; ++it)
    {
        double moved = 0.0;
        for (int i = 0; i < p; ++i)
        {
            cplx num = 1.0;                       // P(r_i), monic
            for (int j = p - 1; j >= 0; --j) num = num * r[i] + c[j];
            cplx den = 1.0;
            for (int j = 0; j < p; ++j) if (j != i) den *= (r[i] - r[j]);
            if (std::abs (den) < 1.0e-280) den = 1.0e-280;
            const cplx d = num / den;
            r[i] -= d;
            moved = std::max (moved, std::abs (d));
        }
        if (moved < 1.0e-13) break;
    }
    for (int i = 0; i < p; ++i) roots[i] = r[i];
}

static std::vector<CexpComp> cexpFit (const an::Envelope& e, double ta, double tb, int order,
                                      double fMaxOff = 3.5)
{
    std::vector<CexpComp> out;
    if (e.z.empty() || order < 1 || order > 6) return out;

    // Decimate the baseband to ~50 Hz with a boxcar (its zeros land on the
    // decimated bin edges, so what little out-of-band residue the comb left
    // does not fold onto the components).
    const int M = std::max (1, static_cast<int> (e.rate / 50.0));
    std::vector<cplx> y;
    double t0 = 0.0;
    bool first = true;
    for (std::size_t i = 0; i + static_cast<std::size_t> (M) <= e.z.size(); i += static_cast<std::size_t> (M))
    {
        const double t = e.time (i);
        if (t < ta || t > tb) continue;
        cplx acc (0.0);
        for (int j = 0; j < M; ++j) acc += e.z[i + static_cast<std::size_t> (j)];
        y.push_back (acc / static_cast<double> (M));
        if (first) { t0 = t; first = false; }
    }
    const double rate = e.rate / M;
    const int N = static_cast<int> (y.size());
    const int p = order;
    if (N < 4 * p) return out;

    // Linear prediction, least squares.
    cplx R[8][8] {}, rhs[8] {};
    for (int n = p; n < N; ++n)
        for (int j = 1; j <= p; ++j)
        {
            const cplx cj = std::conj (y[static_cast<std::size_t> (n - j)]);
            rhs[j - 1] += y[static_cast<std::size_t> (n)] * cj;
            for (int k = 1; k <= p; ++k)
                R[j - 1][k - 1] += y[static_cast<std::size_t> (n - k)] * cj;
        }
    // R is built transposed relative to solve order; symmetrise access.
    cplx A[8][8];
    for (int j = 0; j < p; ++j)
        for (int k = 0; k < p; ++k)
            A[j][k] = R[j][k];
    cplx a[8];
    for (int j = 0; j < p; ++j) a[j] = rhs[j];
    solveComplex (p, A, a);

    // Characteristic polynomial z^p - a1 z^(p-1) - ... - ap.
    cplx c[8];
    for (int j = 0; j < p; ++j) c[p - 1 - j] = -a[j];
    cplx z[8];
    durandKerner (p, c, z);

    // Keep plausible poles, then fit amplitudes over the segment.
    cplx keep[8];
    int nk = 0;
    for (int i = 0; i < p; ++i)
    {
        const double m = std::abs (z[i]);
        if (m < 0.5 || m > 1.02) continue;
        keep[nk++] = z[i];
    }
    if (nk == 0) return out;

    cplx G[8][8] {}, gb[8] {};
    for (int n = 0; n < N; ++n)
    {
        cplx pw[8];
        for (int i = 0; i < nk; ++i) pw[i] = std::pow (keep[i], n);
        for (int i = 0; i < nk; ++i)
        {
            const cplx ci = std::conj (pw[i]);
            gb[i] += y[static_cast<std::size_t> (n)] * ci;
            for (int j = 0; j < nk; ++j) G[i][j] += pw[j] * ci;
        }
    }
    solveComplex (nk, G, gb);

    for (int i = 0; i < nk; ++i)
    {
        const double amp = std::abs (gb[i]);
        if (amp <= 0.0) continue;
        CexpComp comp;
        comp.rateDbPerS = 20.0 / std::log (10.0) * std::log (std::abs (keep[i])) * rate;
        comp.freqHz     = std::arg (keep[i]) * rate / (2.0 * an::kPi);
        comp.levelDbT0  = 20.0 * std::log10 (amp);
        // Extrapolate the segment-start level back to the strike.
        comp.levelDb0   = comp.levelDbT0 - comp.rateDbPerS * t0;
        // Physical unison/polarisation components sit within a few Hz of the
        // analysis frequency and decay between 0 and ~90 dB/s; anything else
        // is the fit mopping up residual (junk poles whose back-extrapolated
        // levels would otherwise dominate the ranking).
        if (std::abs (comp.freqHz) > fMaxOff) continue;
        if (comp.rateDbPerS > -0.02 || comp.rateDbPerS < -90.0) continue;
        out.push_back (comp);
    }
    std::sort (out.begin(), out.end(),
               [] (const CexpComp& a2, const CexpComp& b2) { return a2.levelDbT0 > b2.levelDbT0; });
    return out;
}

// The measured tables quote a fast (prompt) and a slow (aftersound)
// component: fast is the strongest component at the segment start, slow the
// strongest of the clearly-slower remainder, with levels compared at t = 0.
struct FastSlow
{
    bool valid = false;
    double fastDbPerS = 0.0, slowDbPerS = 0.0, slowRelDb = 0.0;
    double fastLevelDb0 = 0.0;
};

static FastSlow pickFastSlow (const std::vector<CexpComp>& comps)
{
    FastSlow r;
    if (comps.empty()) return r;
    const CexpComp& fast = comps.front();
    r.fastDbPerS = fast.rateDbPerS;
    r.fastLevelDb0 = fast.levelDb0;
    // The aftersound is the LONGEST-LIVED audible component -- the measured
    // tables' slow member -- so among everything clearly slower than the
    // prompt component and still audible, take the slowest physical one.
    const CexpComp* slow = nullptr;
    for (std::size_t i = 1; i < comps.size(); ++i)
    {
        const CexpComp& c = comps[i];
        if (c.levelDbT0 < fast.levelDbT0 - 24.0) continue;
        if (c.rateDbPerS < 0.5 * fast.rateDbPerS) continue;   // not clearly slower
        if (c.rateDbPerS > -0.8) continue;   // slower than any measured aftersound
        if (! slow || c.rateDbPerS > slow->rateDbPerS) slow = &c;
    }
    if (slow)
    {
        r.slowDbPerS = slow->rateDbPerS;
        r.slowRelDb = slow->levelDb0 - fast.levelDb0;
        r.valid = true;
    }
    return r;
}

// The prompt/aftersound pair the measured tables quote, with the aftersound
// fit on a LATE window. A single-window fit cannot see it: least squares
// weights the loud early samples, so the poles chase the partially-coupled
// antisymmetric cluster and the true slowest component -- which owns the
// signal from mid-decay on -- never gets one.
static FastSlow fastSlowTwoWindow (const an::Envelope& e,
                                   double taE, double tbE,
                                   double taL, double tbL)
{
    FastSlow r = pickFastSlow (cexpFit (e, taE, tbE, 5));
    r.valid = false;
    // Order 5: a trichord's aftersound is up to three H members within a
    // fraction of a hertz, and an under-ordered fit aliases them into one
    // spuriously slow pole.
    const auto late = cexpFit (e, taL, tbL, 5);
    if (! late.empty())
    {
        r.slowDbPerS = late.front().rateDbPerS;
        // The measured tables' slow AMPLITUDE is the whole aftersound
        // envelope -- on a trichord that is up to three H members beating
        // slowly, not the one strongest pole -- so the level sums every
        // late component of comparable rate.
        double amp = 0.0;
        for (const auto& c : late)
            if (c.rateDbPerS >= 2.5 * late.front().rateDbPerS)
                amp += std::pow (10.0, c.levelDb0 / 20.0);
        r.slowRelDb = 20.0 * std::log10 (std::max (1.0e-300, amp)) - r.fastLevelDb0;
        r.valid = true;
    }
    return r;
}

// A partial's frequency as the strongest RESOLVED component's frequency --
// beat-proof where a phase-slope fit on the composite is not: near an
// amplitude crossover the composite phase rotates through the whole pair
// and a linear fit reads garbage.
static double strongCompFreq (const std::vector<double>& x, double combF, double guess,
                              double ta, double tb)
{
    const an::Envelope e = an::heterodyne (x, kFs, guess, combF);
    if (e.z.empty()) return -1.0;
    // The admission window scales with frequency: one cent of a 9 kHz
    // partial is over 5 Hz.
    const auto comps = cexpFit (e, ta, tb, 3, std::max (3.5, 1.2e-3 * guess));
    if (comps.empty()) return -1.0;
    return guess + comps.front().freqHz;
}

// Two-segment piecewise-linear fit of a broadband peak-hold envelope in dB;
// the knee is scanned for least RMS, exactly the measurement the Salamander
// knee table was made with.
struct KneeFit
{
    bool valid = false;
    double earlyDbPerS = 0.0, lateDbPerS = 0.0;
    double kneeS = 0.0, kneeDb = 0.0;
};

static KneeFit kneeFit (const std::vector<double>& x, double hopS, double tMax, double floorDb)
{
    const int hop = static_cast<int> (kFs * hopS);
    std::vector<double> tv, hv;
    for (std::size_t i = 0; i + static_cast<std::size_t> (hop) < x.size();
         i += static_cast<std::size_t> (hop))
    {
        const double t = static_cast<double> (i) / kFs;
        if (t > tMax) break;
        double w = 0.0;
        for (int j = 0; j < hop; ++j) w = std::max (w, std::abs (x[i + static_cast<std::size_t> (j)]));
        tv.push_back (t);
        hv.push_back (w);
    }
    // Reference: the envelope once the attack's phase-aligned crest has
    // dephased (>= 0.1 s) -- the same convention as the measurement script,
    // which starts 0.1 s after the peak. The raw termination-force feed has
    // a crest the radiated recordings do not, and referencing to it would
    // shift every knee depth by that crest.
    double pk = 0.0;
    for (std::size_t i = 0; i < tv.size(); ++i)
        if (tv[i] >= 0.1) pk = std::max (pk, hv[i]);
    if (pk <= 0.0) return {};
    std::vector<double> dv;
    std::vector<double> tv2;
    for (std::size_t i = 0; i < tv.size(); ++i)
    {
        if (tv[i] < 0.1) continue;
        const double d = 20.0 * std::log10 (std::max (1.0e-300, hv[i] / pk));
        if (d < floorDb) break;
        tv2.push_back (tv[i]);
        dv.push_back (d);
    }
    tv = tv2;
    const int n = static_cast<int> (tv.size());
    if (n < 12) return {};

    auto lineFit = [&] (int i0, int i1, double& s, double& b)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const int m = i1 - i0;
        for (int i = i0; i < i1; ++i)
        { sx += tv[static_cast<std::size_t> (i)]; sy += dv[static_cast<std::size_t> (i)];
          sxx += tv[static_cast<std::size_t> (i)] * tv[static_cast<std::size_t> (i)];
          sxy += tv[static_cast<std::size_t> (i)] * dv[static_cast<std::size_t> (i)]; }
        const double den = m * sxx - sx * sx;
        s = den != 0.0 ? (m * sxy - sx * sy) / den : 0.0;
        b = (sy - s * sx) / m;
        double rss = 0.0;
        for (int i = i0; i < i1; ++i)
        { const double r = dv[static_cast<std::size_t> (i)] - (b + s * tv[static_cast<std::size_t> (i)]); rss += r * r; }
        return rss;
    };

    KneeFit best;
    double bestRss = 1.0e300;
    for (int k = 4; k < n - 4; ++k)
    {
        double s1, b1, s2, b2;
        const double rss = lineFit (0, k, s1, b1) + lineFit (k, n, s2, b2);
        if (rss < bestRss)
        {
            bestRss = rss;
            best.valid = true;
            best.earlyDbPerS = s1;
            best.lateDbPerS = s2;
            best.kneeS = tv[static_cast<std::size_t> (k)];
            best.kneeDb = b2 + s2 * best.kneeS;
        }
    }
    return best;
}

// Deepest dip of the fundamental's envelope below its own two-segment trend.
static double deepestDipDb (const an::Envelope& e, double ta, double tb)
{
    std::vector<double> tv, dv;
    for (std::size_t i = 0; i < e.z.size(); ++i)
    {
        const double t = e.time (i);
        if (t < ta || t > tb) continue;
        tv.push_back (t);
        dv.push_back (e.db (i));
    }
    const int n = static_cast<int> (tv.size());
    if (n < 24) return 0.0;

    auto rssOf = [&] (int i0, int i1, double& s, double& b)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const int m = i1 - i0;
        for (int i = i0; i < i1; ++i)
        { sx += tv[static_cast<std::size_t> (i)]; sy += dv[static_cast<std::size_t> (i)];
          sxx += tv[static_cast<std::size_t> (i)] * tv[static_cast<std::size_t> (i)];
          sxy += tv[static_cast<std::size_t> (i)] * dv[static_cast<std::size_t> (i)]; }
        const double den = m * sxx - sx * sx;
        s = den != 0.0 ? (m * sxy - sx * sy) / den : 0.0;
        b = (sy - s * sx) / m;
    };
    // Two-segment trend so the knee's curvature does not read as a null.
    double bestDip = 0.0, bestRss = 1.0e300;
    for (int k = 8; k < n - 8; ++k)
    {
        double s1, b1, s2, b2;
        rssOf (0, k, s1, b1);
        rssOf (k, n, s2, b2);
        double rss = 0.0, dip = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double trend = i < k ? b1 + s1 * tv[static_cast<std::size_t> (i)]
                                       : b2 + s2 * tv[static_cast<std::size_t> (i)];
            const double r = dv[static_cast<std::size_t> (i)] - trend;
            rss += r * r;
            dip = std::min (dip, r);
        }
        if (rss < bestRss) { bestRss = rss; bestDip = dip; }
    }
    return bestDip;
}

// ===========================================================================
// The rows
// ===========================================================================

static void sectionGrand()
{
    heading ("G. Grand: strings + bridge two-port + board (plan section 10)");

    // ---- K1/K2: inharmonicity and partial placement -------------------------
    {
        struct R { int midi; };
        for (R r : { R { 21 }, R { 42 }, R { 48 }, R { 57 }, R { 60 }, R { 72 }, R { 84 }, R { 96 } })
        {
            const double bWant = GrandInharmonicity::at (r.midi);
            const auto x = renderNote (r.midi, 0.7, 3.2);
            // Treble notes decay tens of dB per second: the analysis window
            // must sit early, and the frequencies come from the strongest
            // RESOLVED component, which a beat crossover cannot corrupt.
            const double ta = r.midi >= 84 ? 0.06 : r.midi >= 72 ? 0.08 : 0.12;
            const double tb = r.midi >= 96 ? 0.55 : r.midi >= 84 ? 0.7 : r.midi >= 72 ? 0.9 : 1.6;
            const double f0n = noteHz (r.midi) * std::pow (2.0, grandStretchCents (r.midi) / 1200.0);
            // The SOUNDING fundamental sits sqrt(1+B) above the series-law
            // f0 -- at C7 that is +7 cents, far outside the component
            // admission window if the search starts at the law value.
            double f0 = f0n * std::sqrt (1.0 + bWant);
            for (int it = 0; it < 2; ++it)
            {
                const double got = strongCompFreq (x, f0n, f0, ta, tb);
                if (got > 0.0) f0 = got;
            }
            // The fitted "f0" of the series law is the fundamental with its
            // own inharmonic shift removed.
            f0 /= std::sqrt (1.0 + bWant);

            // Count usable partials under the 12 kHz cap, then fit high ones,
            // where B k^2 dwarfs the unison detune.
            int kTop = 1;
            while (kTop < 15 && (kTop + 1) * f0 * std::sqrt (1.0 + bWant * (kTop + 1.0) * (kTop + 1.0)) < 11500.0)
                ++kTop;
            const int kHi = std::min (14, kTop);
            const int kLo = std::max (2, kHi - 9);

            // Two-parameter fit y = C + B k^2: the offset C absorbs any
            // residual error in the fundamental anchor (bridge pull, unison
            // composite), so it cannot masquerade as inharmonicity.
            double s0 = 0.0, s1 = 0.0, s2 = 0.0, sy = 0.0, syk = 0.0;
            int used = 0;
            std::vector<std::pair<int, double>> got2;
            for (int k = kLo; k <= kHi; ++k)
            {
                const double guess = k * f0 * std::sqrt (1.0 + bWant * k * k);
                const double got = strongCompFreq (x, f0n, guess, ta, tb);
                if (got <= 0.0) continue;
                const double y = (got / (k * f0)) * (got / (k * f0)) - 1.0;
                const double k2 = static_cast<double> (k) * k;
                s0 += 1.0; s1 += k2; s2 += k2 * k2;
                sy += y;  syk += y * k2;
                got2.push_back ({ k, got });
                ++used;
            }
            const double det = s0 * s2 - s1 * s1;
            const double bGot = (used >= 3 && std::abs (det) > 1e-12)
                              ? (s0 * syk - s1 * sy) / det : -1.0;
            const double cOff = used >= 3 ? (sy - bGot * s1) / s0 : 0.0;
            double maxCents = 0.0;
            for (auto [k, gk] : got2)
                if (k <= 8 && bGot > 0.0)
                {
                    const double law = k * f0 * std::sqrt (1.0 + cOff + bGot * k * k);
                    maxCents = std::max (maxCents, std::abs (1200.0 * std::log2 (gk / law)));
                }
            row ("K1", (std::string ("inharmonicity B, MIDI ") + std::to_string (r.midi)).c_str(),
                 fmt ("%.2e +/-15%%", bWant),
                 bGot > 0.0 ? fmt ("%.2e", bGot) : std::string ("unfit"),
                 bGot > 0.0 ? within (bGot, bWant * 0.85, bWant * 1.15) : Verdict::fail);
            if (r.midi == 48 || r.midi == 72)
                row ("K2", (std::string ("partials to k=8, MIDI ") + std::to_string (r.midi)).c_str(),
                     "within 3 c", fmt ("worst %.2f c", maxCents),
                     within (maxCents, 0.0, 3.0));
        }
    }

    // ---- K3: the measured Railsback curve -----------------------------------
    {
        struct R { int midi; double want; };
        for (R r : { R { 21, -20.3 }, R { 60, -0.9 }, R { 72, 2.3 }, R { 84, 13.5 } })
        {
            const auto x = renderNote (r.midi, 0.6, 3.2);
            const double ta = r.midi >= 84 ? 0.06 : r.midi >= 72 ? 0.08 : 0.12;
            const double tb = r.midi >= 84 ? 0.7 : r.midi >= 72 ? 0.9 : 1.6;
            // The measured Railsback column is the FITTED series f0, so the
            // sounding fundamental is measured and its sqrt(1+B) inharmonic
            // shift removed before comparing.
            const double bH = GrandInharmonicity::at (r.midi);
            const double f0n = noteHz (r.midi) * std::pow (2.0, r.want / 1200.0);
            double f0 = f0n * std::sqrt (1.0 + bH);
            for (int it = 0; it < 2; ++it)
            {
                const double got = strongCompFreq (x, f0n, f0, ta, tb);
                if (got > 0.0) f0 = got;
            }
            const double cents = 1200.0 * std::log2 (f0 / std::sqrt (1.0 + bH) / noteHz (r.midi));
            row ("K3", (std::string ("Railsback, MIDI ") + std::to_string (r.midi)).c_str(),
                 fmt ("%+.1f c +/-3", r.want), fmt ("%+.1f c", cents),
                 within (cents, r.want - 3.0, r.want + 3.0));
        }
    }

    // ---- W1: C4 fundamental's two coupled components ------------------------
    {
        const auto x = renderNote (60, 0.9, 14.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (60), 0.3, 1.2);
        const an::Envelope e = an::heterodyne (x, kFs, f0, f0);
        const FastSlow fs2 = fastSlowTwoWindow (e, 0.25, 3.0, 7.0, 13.5);
        row ("W1", "C4 fast component", "-23.3 dB/s +/-40%",
             fmt ("%.1f dB/s", fs2.fastDbPerS), within (-fs2.fastDbPerS, 14.0, 32.6));
        row ("W1", "C4 slow component", "-1.6 dB/s +/-40%",
             fs2.valid ? fmt ("%.1f dB/s", fs2.slowDbPerS) : std::string ("none"),
             fs2.valid ? within (-fs2.slowDbPerS, 0.96, 2.24) : Verdict::fail);
        row ("W1", "C4 slow starts", "-18 +/-6 dB",
             fs2.valid ? fmt ("%.1f dB", fs2.slowRelDb) : std::string ("none"),
             fs2.valid ? within (fs2.slowRelDb, -24.0, -12.0) : Verdict::fail);
    }

    // ---- W2: A3 trichord resolves three normal modes ------------------------
    {
        const auto x = renderNote (57, 0.9, 12.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (57), 0.3, 1.2);
        const an::Envelope e = an::heterodyne (x, kFs, f0, f0);
        const auto comps = cexpFit (e, 0.25, 10.0, 6);
        std::vector<double> rates;
        for (const auto& c : comps)
            if (c.levelDbT0 > comps.front().levelDbT0 - 35.0) rates.push_back (-c.rateDbPerS);
        std::sort (rates.begin(), rates.end());
        const double spread = rates.size() >= 3 ? rates.back() / rates.front() : -1.0;
        char buf[64];
        std::snprintf (buf, sizeof buf, "%d comps%s", static_cast<int> (rates.size()),
                       rates.size() >= 3 ? fmt (", %.1fx", spread).c_str() : "");
        row ("W2", "A3 trichord components", ">= 3, spread >= 4x", buf,
             (rates.size() >= 3 && spread >= 4.0) ? Verdict::pass : Verdict::fail);
    }

    // ---- W3: C5 pair ---------------------------------------------------------
    {
        const auto x = renderNote (72, 0.9, 9.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (72) * std::pow (2.0, 2.3 / 1200.0), 0.2, 0.9);
        const an::Envelope e = an::heterodyne (x, kFs, f0, f0);
        const FastSlow fs2 = fastSlowTwoWindow (e, 0.2, 2.0, 4.0, 8.9);
        row ("W3", "C5 fast component", "-33.6 dB/s +/-40%",
             fmt ("%.1f dB/s", fs2.fastDbPerS), within (-fs2.fastDbPerS, 20.2, 47.0));
        row ("W3", "C5 slow component", "-4.8 dB/s +/-40%",
             fs2.valid ? fmt ("%.1f dB/s", fs2.slowDbPerS) : std::string ("none"),
             fs2.valid ? within (-fs2.slowDbPerS, 2.9, 6.7) : Verdict::fail);
        row ("W3", "C5 slow starts", "-14.5 +/-6 dB",
             fs2.valid ? fmt ("%.1f dB", fs2.slowRelDb) : std::string ("none"),
             fs2.valid ? within (fs2.slowRelDb, -20.5, -8.5) : Verdict::fail);
    }

    // ---- W4: broadband decay knees ------------------------------------------
    {
        struct R { int midi; double kneeS, kneeDb; bool gapRow; const char* name; };
        // C3's rows are a KNOWN GAP until the radiator exists: its measured
        // knee (-41 dB at 1.1 s) is a property of the RADIATED signal, in
        // which the 131 Hz fundamental is nearly absent (radiation
        // efficiency collapses below ~200 Hz [R F&R]). The raw termination
        // force keeps the slowly-decaying fundamental ~20 dB up, so the
        // envelope flattens long before -41 dB. Step 3 owns this; the gap is
        // bounded so it cannot widen silently.
        for (R r : { R { 60, 1.6, -25.0, false, "C4" }, R { 48, 1.1, -41.0, true, "C3" },
                     R { 72, 1.3, -24.0, false, "C5" } })
        {
            const auto x = renderNote (r.midi, 0.9, 10.0);
            // Fit over the knee region: past ~4 s the envelope has a third
            // regime (the aftersound's own structure) that a two-segment
            // model splits arbitrarily.
            const KneeFit k = kneeFit (x, 0.05, 4.0, -60.0);
            row ("W4", (std::string (r.name) + " knee time").c_str(),
                 fmt ("%.1f s +/-0.6", r.kneeS),
                 k.valid ? fmt ("%.2f s", k.kneeS) : std::string ("unfit"),
                 ! k.valid ? Verdict::fail
                 : r.gapRow ? gapV (k.kneeS, r.kneeS - 0.6, r.kneeS + 0.6, 0.7, 4.0)
                            : within (k.kneeS, r.kneeS - 0.6, r.kneeS + 0.6));
            row ("W4", (std::string (r.name) + " knee depth").c_str(),
                 fmt ("%.0f dB +/-8", r.kneeDb),
                 k.valid ? fmt ("%.1f dB", k.kneeDb) : std::string ("unfit"),
                 ! k.valid ? Verdict::fail
                 : r.gapRow ? gapV (k.kneeDb, r.kneeDb - 8.0, r.kneeDb + 8.0, -49.0, -14.0)
                            : within (k.kneeDb, r.kneeDb - 8.0, r.kneeDb + 8.0));
        }
    }

    // ---- W5: gentle notes stay gentle ---------------------------------------
    {
        const auto x = renderNote (39, 0.9, 12.0);
        const KneeFit k = kneeFit (x, 0.05, 11.5, -75.0);
        row ("W5", "D#2 early rate", "about -5 dB/s",
             k.valid ? fmt ("%.1f dB/s", k.earlyDbPerS) : std::string ("unfit"),
             k.valid ? gapV (-k.earlyDbPerS, 3.0, 7.0, 1.5, 12.0) : Verdict::fail);
        // The knee lands where the prompt track meets the aftersound; with
        // the early rate dead on the measured -5 dB/s, a knee 15-20% early
        // means the aftersound band sits a decibel or two high -- a residual
        // of the same H-level calibration the null rows constrain from the
        // other side. Bounded gap until the step-6 calibration pass.
        row ("W5", "D#2 knee", ">= 4 s (or none)",
             k.valid ? fmt ("%.1f s", k.kneeS) : std::string ("unfit"),
             k.valid ? gapV (k.kneeS, 4.0, 99.0, 2.4, 99.0) : Verdict::fail);

        const auto xa = renderNote (57, 0.9, 8.0);
        const double f0 = an::refineF0 (xa, kFs, noteHz (57), 0.3, 1.2);
        const an::Envelope e = an::heterodyne (xa, kFs, f0, f0);
        const double dip = deepestDipDb (e, 0.3, 6.0);
        row ("W5", "A3 fundamental, no null", "dip > -3 dB",
             fmt ("%.1f dB", dip), dip > -3.0 ? Verdict::pass : Verdict::fail);
    }

    // ---- U1: superposition must FAIL ----------------------------------------
    {
        auto render = [] (std::vector<int> ns)
        {
            std::vector<Strike> s;
            for (int n : ns) s.push_back ({ n, 0.9 });
            return renderGrand (s, 2.5);
        };
        const auto a = render ({ 48 });
        const auto b = render ({ 60 });
        const auto ab = render ({ 48, 60 });
        double d = 0.0, ref = 0.0;
        for (std::size_t i = 0; i < ab.size(); ++i)
        {
            d = std::max (d, std::abs (ab[i] - (a[i] + b[i])));
            ref = std::max (ref, std::abs (ab[i]));
        }
        const double db = 20.0 * std::log10 (std::max (1.0e-12, d / std::max (1.0e-12, ref)));
        // The exact inversion of the CP-70's strongest row: through the shared
        // board a chord is NOT the sum of its notes, measurably.
        row ("U1", "chord is NOT the sum of its notes", "residual >= -45 dB",
             fmt ("%.1f dB", db), db >= -45.0 ? Verdict::pass : Verdict::fail);
    }

    // ---- U2: beat minima filled in ------------------------------------------
    {
        const auto x = renderNote (60, 0.9, 6.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (60), 0.3, 1.2);
        const an::Envelope e = an::heterodyne (x, kFs, f0, f0);
        const double dip = deepestDipDb (e, 0.25, 4.0);
        row ("U2", "C4 deepest null in 4 s", "-10 .. -28 dB",
             fmt ("%.1f dB", dip), within (dip, -28.0, -10.0));
    }

    // ---- P1: single-string vertical/horizontal split ------------------------
    {
        const auto x = renderNote (36, 0.9, 12.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (36) * std::pow (2.0, grandStretchCents (36) / 1200.0), 0.4, 2.0);
        const an::Envelope e = an::heterodyne (x, kFs, f0, f0);
        const auto comps = cexpFit (e, 0.3, 10.0, 4);
        std::vector<double> rates;
        for (const auto& c : comps)
            if (! comps.empty() && c.levelDbT0 > comps.front().levelDbT0 - 35.0)
                rates.push_back (-c.rateDbPerS);
        std::sort (rates.begin(), rates.end());
        const double ratio = rates.size() >= 2 ? rates.back() / rates.front() : -1.0;
        row ("P1", "C2 single string: V/H split", "2 comps, ratio 4-17x",
             rates.size() >= 2 ? fmt ("ratio %.1fx", ratio) : std::string ("1 comp"),
             (rates.size() >= 2 && ratio >= 4.0 && ratio <= 17.0) ? Verdict::pass : Verdict::fail);
    }

    // ---- E1: coupled passivity at heavy polyphony ---------------------------
    // The row the whole bridge design answers to. Ten-note ff bass-heavy
    // chord; once every hammer has separated, the total energy of strings +
    // board must never rise.
    {
        GrandBoard board;
        board.prepare (kFs);
        GrandVoice::Config cfg;
        std::vector<std::unique_ptr<GrandVoice>> vs;
        for (int n : { 21, 26, 31, 36, 43, 48, 55, 60, 67, 72 })
        {
            vs.push_back (std::make_unique<GrandVoice>());
            vs.back()->prepare (kFs);
            vs.back()->noteOn (n, 1.0, cfg, board, 0);
            vs.back()->setPedal (true);
        }
        const int N = static_cast<int> (kFs * 3.0);
        // Hammers strike within the first milliseconds and never return; the
        // segment from 0.5 s on is hammer-free by construction. The tracked
        // quantity is the FULL discrete energy: subsystem sums plus the
        // coupling cross-potential in the integrator's own staggered
        // convention -- without the cross term, its bounded oscillation
        // masquerades as generation at the 1e-4 level.
        const int from = static_cast<int> (kFs * 0.5);
        const std::size_t nv = vs.size();
        std::vector<double> uPrev (nv, 0.0), fPrev (nv, 0.0);
        double ePrev = -1.0, maxRise = 0.0;
        for (int i = 0; i < N; ++i)
        {
            for (auto& v : vs) v->process (cfg, board);
            board.tick();
            double e = board.energy();
            for (auto& v : vs) e += v->modalEnergy();
            for (std::size_t j = 0; j < nv; ++j)
            {
                const double u = board.bridgeDisplacement (vs[j]->bridgeShape());
                const double F = vs[j]->coupledForceRead();
                e += -0.5 * (F * uPrev[j] + fPrev[j] * u)
                   + 0.5 * vs[j]->couplingLoad() * u * uPrev[j];
                uPrev[j] = u;
                fPrev[j] = F;
            }
            if (i < from) { ePrev = -1.0; continue; }
            if (ePrev > 0.0 && e > ePrev)
                maxRise = std::max (maxRise, (e - ePrev) / ePrev);
            ePrev = e;
        }
        row ("E1", "coupled energy never rises", "<= 1e-9 /sample",
             fmt ("%.2e", maxRise), maxRise <= 1.0e-9 ? Verdict::pass : Verdict::fail);
    }

    // ---- N1: finite and bounded across the compass --------------------------
    {
        bool clean = true;
        double pk = 0.0;
        for (int n : { 21, 36, 52, 69, 88, 101, 108 })
            for (double v : { 0.05, 1.0 })
            {
                const auto x = renderNote (n, v, 1.0);
                for (double s2 : x)
                {
                    if (! std::isfinite (s2)) clean = false;
                    pk = std::max (pk, std::abs (s2));
                }
            }
        row ("N1", "finite across compass", "finite, peak < 4",
             clean ? fmt ("peak %.2f", pk) : std::string ("NON-FINITE"),
             (clean && pk < 4.0) ? Verdict::pass : Verdict::fail);
    }
}

int main()
{
    std::printf ("Epi grand reference rows (step 2: strings + bridge + board)\n");
    std::printf ("targets from docs/grand-implementation-plan.md (Salamander C5 measurements)\n");
    std::printf ("\n  %-3s %-36s %-24s %-24s %s\n", "ID", "PROPERTY", "TARGET", "MEASURED", "");

    sectionGrand();

    std::printf ("\n");
    if (gaps > 0)
        std::printf ("%d known gap%s\n", gaps, gaps == 1 ? "" : "s");
    if (failures == 0)
        std::printf ("all grand rows within tolerance\n");
    else
        std::printf ("%d row%s outside tolerance\n", failures, failures == 1 ? "" : "s");
    std::printf ("SUMMARY fail=%d gap=%d\n", failures, gaps);
    return failures == 0 ? 0 : 1;
}
