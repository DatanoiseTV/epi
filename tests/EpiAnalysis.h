/*
  Epi — measurement primitives for the acoustic reference suite.

  These exist so the checks in test_epi_reference.cpp can assert on numbers
  that mean the same thing as the numbers measured off the real instrument.
  Nothing here is shipped; it is the ruler, not the thing being measured.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace epianalysis
{

using cplx = std::complex<double>;
inline constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Radix-2 FFT, in place. Only used for the brightness measurements, where a
// bin-accurate result is not needed.
// ---------------------------------------------------------------------------
inline void fft (std::vector<cplx>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / static_cast<double> (len);
        const cplx wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            cplx w (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const cplx u = a[i + k];
                const cplx v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// One partial's complex amplitude over time.
//
// The signal is shifted so the partial sits at DC, then run through three
// cascaded boxcars one period of the FUNDAMENTAL long. That length matters and
// it is not the analysis frequency: a boxcar nulls multiples of its own
// reciprocal length, so a comb cut to f0 puts a null on every harmonic of f0.
// Shifting harmonic k to DC moves harmonic m to (m-k)*f0, which is a multiple
// of f0 for every m, so every neighbour lands exactly on a null and is removed
// outright rather than merely attenuated.
//
// Cutting the comb to the analysis frequency instead -- the obvious mistake --
// leaves the fundamental sitting halfway between DC and the first null while
// measuring H2, where three cascaded boxcars attenuate it by only 11.8 dB. On
// a soft note, where the fundamental is 12 dB up on the second harmonic, what
// comes out is the fundamental again, and every partial then appears to decay
// at exactly the fundamental's rate.
//
// The price is a group delay of 1.5 periods of f0, which is removed here but
// cannot be undone: the first few periods after the strike are smeared and
// must not be read as attack detail. Use fitHarmonics() inside the attack.
// ---------------------------------------------------------------------------
struct Envelope
{
    double rate = 0.0;   // envelope samples per second
    double t0   = 0.0;   // time of z[0] in the source signal, delay removed
    std::vector<cplx> z;

    double time (std::size_t i) const { return t0 + static_cast<double> (i) / rate; }
    double amp  (std::size_t i) const { return std::abs (z[i]); }
    double db   (std::size_t i) const { return 20.0 * std::log10 (std::max (1.0e-300, amp (i))); }

    // Nearest envelope sample to a time, or size() if outside.
    std::size_t indexAt (double t) const
    {
        const double x = (t - t0) * rate;
        if (x < 0.0) return z.size();
        const std::size_t i = static_cast<std::size_t> (std::lround (x));
        return i < z.size() ? i : z.size();
    }

    double dbAt (double t) const
    {
        const std::size_t i = indexAt (t);
        return i < z.size() ? db (i) : -300.0;
    }
};

// combF is the spacing of the partials to be rejected -- the fundamental, for
// a harmonic series. It defaults to the analysis frequency, which is only
// correct when analysing the fundamental itself.
inline Envelope heterodyne (const std::vector<double>& x, double fs, double f, double combF = 0.0)
{
    Envelope e;
    if (f <= 0.0 || x.empty()) return e;
    if (combF <= 0.0) combF = f;

    const int L = std::max (2, static_cast<int> (std::lround (fs / combF)));
    if (static_cast<std::size_t> (3 * L) >= x.size()) return e;

    std::vector<cplx> y (x.size());
    const double w = -2.0 * kPi * f / fs;
    for (std::size_t n = 0; n < x.size(); ++n)
        y[n] = x[n] * cplx (std::cos (w * static_cast<double> (n)),
                            std::sin (w * static_cast<double> (n)));

    std::vector<cplx> buf (x.size());
    for (int pass = 0; pass < 3; ++pass)
    {
        cplx run (0.0, 0.0);
        for (std::size_t n = 0; n < y.size(); ++n)
        {
            run += y[n];
            if (n >= static_cast<std::size_t> (L)) run -= y[n - static_cast<std::size_t> (L)];
            buf[n] = run / static_cast<double> (L);
        }
        y.swap (buf);
    }

    const int D = std::max (1, L / 8);
    const double delaySamples = 1.5 * static_cast<double> (L - 1);

    e.rate = fs / static_cast<double> (D);
    e.t0   = -delaySamples / fs;
    e.z.reserve (y.size() / static_cast<std::size_t> (D) + 1);
    // The heterodyne halves a real sinusoid's amplitude; undo that so the
    // envelope reads in the units of the source signal.
    for (std::size_t n = 0; n < y.size(); n += static_cast<std::size_t> (D))
        e.z.push_back (2.0 * y[n]);
    return e;
}

// ---------------------------------------------------------------------------
// Straight-line fit of the envelope in dB against time. The slope is the decay
// rate in dB/s (negative while decaying); the residual says how far the
// envelope is from a single exponential, which is itself a measured property
// of the instrument.
// ---------------------------------------------------------------------------
struct LineFit
{
    bool   valid       = false;
    double slopeDbPerS = 0.0;
    double interceptDb = 0.0;
    double residRmsDb  = 0.0;
    int    n           = 0;
};

inline LineFit fitDecay (const Envelope& e, double ta, double tb, double floorDb = -160.0)
{
    LineFit r;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (std::size_t i = 0; i < e.z.size(); ++i)
    {
        const double t = e.time (i);
        if (t < ta || t > tb) continue;
        const double d = e.db (i);
        if (d < floorDb) continue;
        sx += t; sy += d; sxx += t * t; sxy += t * d; ++n;
    }
    if (n < 8) return r;

    const double den = static_cast<double> (n) * sxx - sx * sx;
    if (std::abs (den) < 1.0e-18) return r;

    r.slopeDbPerS = (static_cast<double> (n) * sxy - sx * sy) / den;
    r.interceptDb = (sy - r.slopeDbPerS * sx) / static_cast<double> (n);

    double acc = 0.0;
    for (std::size_t i = 0; i < e.z.size(); ++i)
    {
        const double t = e.time (i);
        if (t < ta || t > tb) continue;
        const double d = e.db (i);
        if (d < floorDb) continue;
        const double res = d - (r.interceptDb + r.slopeDbPerS * t);
        acc += res * res;
    }
    r.residRmsDb = std::sqrt (acc / static_cast<double> (n));
    r.n = n;
    r.valid = true;
    return r;
}

// Peak-to-peak of the envelope in dB once the exponential trend is removed.
// This is the amplitude modulation: on the real instrument it is a fraction of
// a decibel, and anything approaching a decibel is audible as chorus.
inline double detrendedSwingDb (const Envelope& e, double ta, double tb)
{
    const LineFit f = fitDecay (e, ta, tb);
    if (! f.valid) return -1.0;
    double lo = 1.0e300, hi = -1.0e300;
    for (std::size_t i = 0; i < e.z.size(); ++i)
    {
        const double t = e.time (i);
        if (t < ta || t > tb) continue;
        const double res = e.db (i) - (f.interceptDb + f.slopeDbPerS * t);
        lo = std::min (lo, res);
        hi = std::max (hi, res);
    }
    return hi > lo ? hi - lo : -1.0;
}

// The partial's true frequency, from the rate at which its phase rotates away
// from the analysis frequency. Far more precise than reading an FFT peak,
// because it uses the whole segment rather than one bin.
inline double partialFrequency (const Envelope& e, double analysisF, double ta, double tb)
{
    std::vector<double> ts, ph;
    double prev = 0.0, unwrapped = 0.0;
    bool first = true;
    for (std::size_t i = 0; i < e.z.size(); ++i)
    {
        const double t = e.time (i);
        if (t < ta || t > tb) continue;
        if (std::abs (e.z[i]) < 1.0e-14) continue;
        double p = std::arg (e.z[i]);
        if (first) { unwrapped = p; first = false; }
        else
        {
            double d = p - prev;
            while (d >  kPi) d -= 2.0 * kPi;
            while (d < -kPi) d += 2.0 * kPi;
            unwrapped += d;
        }
        prev = p;
        ts.push_back (t);
        ph.push_back (unwrapped);
    }
    if (ts.size() < 8) return 0.0;

    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double n = static_cast<double> (ts.size());
    for (std::size_t i = 0; i < ts.size(); ++i)
    { sx += ts[i]; sy += ph[i]; sxx += ts[i] * ts[i]; sxy += ts[i] * ph[i]; }
    const double den = n * sxx - sx * sx;
    if (std::abs (den) < 1.0e-18) return 0.0;
    const double slope = (n * sxy - sx * sy) / den;     // rad/s of drift
    return analysisF + slope / (2.0 * kPi);
}

// Refine a nominal fundamental until the analysis frequency and the measured
// one agree. Three passes is plenty; each one removes most of the error.
inline double refineF0 (const std::vector<double>& x, double fs, double nominal,
                        double ta = 0.4, double tb = 1.4)
{
    double f = nominal;
    for (int pass = 0; pass < 3; ++pass)
    {
        const Envelope e = heterodyne (x, fs, f);
        if (e.z.empty()) return f;
        const double got = partialFrequency (e, f, ta, tb);
        if (got <= 0.0 || std::abs (got - f) > 0.5 * f) return f;
        f = got;
    }
    return f;
}

// ---------------------------------------------------------------------------
// Least-squares fit of K harmonics of f0 across one window, and what is left
// over. Unlike the heterodyne this has no group delay, so it is the right tool
// inside the attack -- at the cost of needing a window several periods long to
// tell neighbouring harmonics apart at all.
//
// Anything the harmonic series cannot explain lands in the residual: the
// tonebar mode, the hammer, and any genuine inharmonicity.
// ---------------------------------------------------------------------------
struct HarmonicFit
{
    bool   valid    = false;
    std::vector<double> amp;      // per harmonic, 1-based index k at amp[k-1]
    double residRms = 0.0;
    double signalRms = 0.0;
    double maxAmp   = 0.0;
    int    windowLen = 0;

    // The inharmonic content, in dB relative to the strongest harmonic. This
    // is the quantity the reference tables report.
    double residRelMaxDb() const
    {
        if (maxAmp <= 0.0) return 0.0;
        return 20.0 * std::log10 (std::max (1.0e-300, residRms * std::sqrt (2.0) / maxAmp));
    }
};

inline HarmonicFit fitHarmonics (const std::vector<double>& x, double fs, double f0,
                                 std::size_t start, std::size_t len, int maxK = 16)
{
    HarmonicFit r;
    if (f0 <= 0.0 || start + len > x.size() || len < 32) return r;

    int K = std::min (maxK, static_cast<int> (0.45 * fs / f0));
    if (K < 1) return r;
    const int M = 2 * K;

    // Normal equations. The window is several periods long, so the columns are
    // near-orthogonal and this stays well conditioned.
    std::vector<double> A (static_cast<std::size_t> (M) * static_cast<std::size_t> (M), 0.0);
    std::vector<double> b (static_cast<std::size_t> (M), 0.0);
    std::vector<double> basis (static_cast<std::size_t> (M));

    for (std::size_t n = 0; n < len; ++n)
    {
        const double t = static_cast<double> (start + n) / fs;
        for (int k = 1; k <= K; ++k)
        {
            const double w = 2.0 * kPi * f0 * static_cast<double> (k) * t;
            basis[static_cast<std::size_t> (2 * (k - 1))]     = std::cos (w);
            basis[static_cast<std::size_t> (2 * (k - 1) + 1)] = std::sin (w);
        }
        const double v = x[start + n];
        for (int i = 0; i < M; ++i)
        {
            b[static_cast<std::size_t> (i)] += basis[static_cast<std::size_t> (i)] * v;
            for (int j = i; j < M; ++j)
                A[static_cast<std::size_t> (i) * static_cast<std::size_t> (M) + static_cast<std::size_t> (j)]
                    += basis[static_cast<std::size_t> (i)] * basis[static_cast<std::size_t> (j)];
        }
    }
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < i; ++j)
            A[static_cast<std::size_t> (i) * static_cast<std::size_t> (M) + static_cast<std::size_t> (j)]
                = A[static_cast<std::size_t> (j) * static_cast<std::size_t> (M) + static_cast<std::size_t> (i)];

    // Gaussian elimination with partial pivoting.
    std::vector<double> c = b;
    for (int col = 0; col < M; ++col)
    {
        int piv = col;
        for (int row = col + 1; row < M; ++row)
            if (std::abs (A[static_cast<std::size_t> (row) * static_cast<std::size_t> (M) + static_cast<std::size_t> (col)])
                > std::abs (A[static_cast<std::size_t> (piv) * static_cast<std::size_t> (M) + static_cast<std::size_t> (col)]))
                piv = row;
        const double p = A[static_cast<std::size_t> (piv) * static_cast<std::size_t> (M) + static_cast<std::size_t> (col)];
        if (std::abs (p) < 1.0e-12) return r;
        if (piv != col)
        {
            for (int j = 0; j < M; ++j)
                std::swap (A[static_cast<std::size_t> (piv) * static_cast<std::size_t> (M) + static_cast<std::size_t> (j)],
                           A[static_cast<std::size_t> (col) * static_cast<std::size_t> (M) + static_cast<std::size_t> (j)]);
            std::swap (c[static_cast<std::size_t> (piv)], c[static_cast<std::size_t> (col)]);
        }
        for (int row = col + 1; row < M; ++row)
        {
            const double f = A[static_cast<std::size_t> (row) * static_cast<std::size_t> (M) + static_cast<std::size_t> (col)]
                           / A[static_cast<std::size_t> (col) * static_cast<std::size_t> (M) + static_cast<std::size_t> (col)];
            if (f == 0.0) continue;
            for (int j = col; j < M; ++j)
                A[static_cast<std::size_t> (row) * static_cast<std::size_t> (M) + static_cast<std::size_t> (j)]
                    -= f * A[static_cast<std::size_t> (col) * static_cast<std::size_t> (M) + static_cast<std::size_t> (j)];
            c[static_cast<std::size_t> (row)] -= f * c[static_cast<std::size_t> (col)];
        }
    }
    for (int row = M - 1; row >= 0; --row)
    {
        double s = c[static_cast<std::size_t> (row)];
        for (int j = row + 1; j < M; ++j)
            s -= A[static_cast<std::size_t> (row) * static_cast<std::size_t> (M) + static_cast<std::size_t> (j)]
                 * c[static_cast<std::size_t> (j)];
        c[static_cast<std::size_t> (row)] = s / A[static_cast<std::size_t> (row) * static_cast<std::size_t> (M) + static_cast<std::size_t> (row)];
    }

    r.amp.resize (static_cast<std::size_t> (K));
    for (int k = 1; k <= K; ++k)
    {
        const double a = c[static_cast<std::size_t> (2 * (k - 1))];
        const double bb = c[static_cast<std::size_t> (2 * (k - 1) + 1)];
        r.amp[static_cast<std::size_t> (k - 1)] = std::sqrt (a * a + bb * bb);
        r.maxAmp = std::max (r.maxAmp, r.amp[static_cast<std::size_t> (k - 1)]);
    }

    double resid = 0.0, sig = 0.0;
    for (std::size_t n = 0; n < len; ++n)
    {
        const double t = static_cast<double> (start + n) / fs;
        double model = 0.0;
        for (int k = 1; k <= K; ++k)
        {
            const double w = 2.0 * kPi * f0 * static_cast<double> (k) * t;
            model += c[static_cast<std::size_t> (2 * (k - 1))] * std::cos (w)
                   + c[static_cast<std::size_t> (2 * (k - 1) + 1)] * std::sin (w);
        }
        const double d = x[start + n] - model;
        resid += d * d;
        sig   += x[start + n] * x[start + n];
    }
    r.residRms  = std::sqrt (resid / static_cast<double> (len));
    r.signalRms = std::sqrt (sig / static_cast<double> (len));
    r.windowLen = static_cast<int> (len);
    r.valid = true;
    return r;
}

// ---------------------------------------------------------------------------
// How much of the signal's power at one instant no harmonic accounts for, in
// dB relative to the loudest harmonic.
//
// Every harmonic's true, time-varying amplitude comes from the comb
// heterodyne, so a partial that is decaying fast is still accounted for
// exactly -- which a fit of static sinusoids cannot do, and which matters
// enormously here because the thirtieth harmonic decays thirty times faster
// than the first. Subtracting the harmonic power from the total leaves the
// tonebar, the hammer, and anything genuinely inharmonic.
//
// The series is summed to Nyquist rather than to some convenient cutoff. A bass
// note carries real harmonic energy past its hundredth partial, and stopping at
// the sixteenth reports 30 dB of ordinary harmonics as inharmonic noise.
// ---------------------------------------------------------------------------
// Each harmonic is rebuilt from its tracked envelope and subtracted from the
// signal, and what survives is measured. Differencing the two powers instead
// looks equivalent and is not: the harmonics account for all but a thousandth
// of the total, so that form subtracts two nearly equal large numbers and the
// answer is whatever the rounding was -- in practice it goes negative and
// clamps to silence, which reads as a perfect result.
inline double inharmonicDb (const std::vector<double>& x, double fs, double f0, double t)
{
    const int K = static_cast<int> (0.45 * fs / f0);
    const int L = std::max (2, static_cast<int> (std::lround (fs / f0)));
    if (K < 1 || x.empty()) return 0.0;

    const int W = 3 * L;
    const std::size_t s0 = static_cast<std::size_t> (std::max (0.0, t * fs - 0.5 * W));
    if (s0 + static_cast<std::size_t> (W) >= x.size()) return 0.0;

    std::vector<double> resid (x.begin() + static_cast<std::ptrdiff_t> (s0),
                               x.begin() + static_cast<std::ptrdiff_t> (s0 + static_cast<std::size_t> (W)));
    double maxAmp = 0.0;

    for (int k = 1; k <= K; ++k)
    {
        const double fk = static_cast<double> (k) * f0;
        const Envelope e = heterodyne (x, fs, fk, f0);
        if (e.z.empty()) continue;

        for (int n = 0; n < W; ++n)
        {
            const double tn = static_cast<double> (s0 + static_cast<std::size_t> (n)) / fs;
            const double pos = (tn - e.t0) * e.rate;
            if (pos < 0.0) continue;
            const std::size_t i = static_cast<std::size_t> (pos);
            if (i + 1 >= e.z.size()) continue;
            const double frac = pos - static_cast<double> (i);
            const cplx env = e.z[i] * (1.0 - frac) + e.z[i + 1] * frac;
            if (n == W / 2) maxAmp = std::max (maxAmp, std::abs (env));
            const double w = 2.0 * kPi * fk * tn;
            resid[static_cast<std::size_t> (n)] -= env.real() * std::cos (w) - env.imag() * std::sin (w);
        }
    }
    if (maxAmp <= 0.0) return 0.0;

    double acc = 0.0;
    for (double v : resid) acc += v * v;
    const double rms = std::sqrt (acc / static_cast<double> (W));
    return 20.0 * std::log10 (std::max (1.0e-300, rms * std::sqrt (2.0) / maxAmp));
}

// The window the residual measurement uses: several fundamental periods, which
// is the shortest span in which harmonics a fundamental apart can be told
// apart at all. Below that no method can separate them, and a measurement that
// claims otherwise is reading its own window shape.
inline std::size_t residualWindow (double fs, double f0, int periods = 6)
{
    return static_cast<std::size_t> (std::max (256.0, std::ceil (periods * fs / f0)));
}

// ---------------------------------------------------------------------------
// Broadband amplitude envelope, for attack timing.
// ---------------------------------------------------------------------------
inline std::vector<double> rectifiedEnvelope (const std::vector<double>& x, double fs, double tauMs)
{
    std::vector<double> e (x.size(), 0.0);
    const double a = std::exp (-1.0 / (0.001 * tauMs * fs));
    double y = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n)
    {
        const double v = std::abs (x[n]);
        y = v > y ? v : a * y + (1.0 - a) * v;   // fast attack, slow release
        e[n] = y;
    }
    return e;
}

// Time from 10% to 90% of the envelope's first peak.
inline double attackTimeMs (const std::vector<double>& x, double fs, double searchS = 0.25)
{
    const std::vector<double> e = rectifiedEnvelope (x, fs, 1.0);
    const std::size_t n = std::min (e.size(), static_cast<std::size_t> (searchS * fs));
    if (n < 8) return -1.0;

    std::size_t peakIdx = 0;
    double peak = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        if (e[i] > peak) { peak = e[i]; peakIdx = i; }
    if (peak <= 0.0) return -1.0;

    std::size_t i10 = 0, i90 = peakIdx;
    for (std::size_t i = 0; i <= peakIdx; ++i) if (e[i] >= 0.1 * peak) { i10 = i; break; }
    for (std::size_t i = i10; i <= peakIdx; ++i) if (e[i] >= 0.9 * peak) { i90 = i; break; }
    return 1000.0 * static_cast<double> (i90 - i10) / fs;
}

// ---------------------------------------------------------------------------
// Spectral centroid over one window, Hann weighted.
// ---------------------------------------------------------------------------
inline double spectralCentroid (const std::vector<double>& x, double fs,
                                std::size_t start, std::size_t len, double fMax = 8000.0)
{
    std::size_t n = 1;
    while (n < len) n <<= 1;
    if (start + len > x.size() || len < 64) return -1.0;

    std::vector<cplx> a (n, cplx (0.0, 0.0));
    for (std::size_t i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i)
                                               / static_cast<double> (len - 1));
        a[i] = x[start + i] * w;
    }
    fft (a);

    double num = 0.0, den = 0.0;
    const std::size_t top = std::min (n / 2, static_cast<std::size_t> (fMax * static_cast<double> (n) / fs));
    for (std::size_t k = 1; k < top; ++k)
    {
        const double m = std::abs (a[k]);
        const double f = static_cast<double> (k) * fs / static_cast<double> (n);
        num += f * m;
        den += m;
    }
    return den > 0.0 ? num / den : -1.0;
}

} // namespace epianalysis
