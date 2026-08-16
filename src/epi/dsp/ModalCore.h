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

#include <algorithm>
#include <cmath>
#include <cstdint>

// The numerical core: a bank of modes that can be coupled to each other, and
// to the outside world, through nonlinearities that provably cannot generate
// energy.
//
// This file is where the instrument's stability lives, so the reasoning is
// written out rather than assumed.
namespace epi
{

inline constexpr double kPiD = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Why this is not a bank of biquads, and not a bank of complex rotators.
//
// The modes here have Q in the hundreds to low thousands and are driven by
// nonlinearities that feed back into their own stiffness. Two things then
// matter far more than they would in an ordinary filter:
//
// 1. THE LINEAR PART MUST NOT DRIFT.
//
//    A complex rotator (z *= exp(j*w/fs)) is the obvious choice and it is
//    wrong in single precision: the rounded rotator has magnitude very
//    slightly off one, and that bias compounds every sample. Measured on an
//    undamped mode, it GROWS at about +0.75% per four seconds -- a spurious
//    negative damping of Q ~ -49000. On a resonator that is supposed to ring
//    for seconds, an error of that sign is not a tuning problem, it is a
//    generator.
//
//    The staggered (leapfrog) form below is symplectic, so its amplitude
//    behaviour is exact by construction rather than by luck. Its usual
//    drawback is frequency warping and a stability ceiling at fs/pi -- which
//    at 44.1 kHz is 14 kHz, below Nyquist, and would put a bright tine's
//    upper modes into divergence. Pre-warping removes both:
//
//        a     = tanh(sigma/fs)
//        wEff  = fs * sqrt( 2 - 2*cos(w/fs)/cosh(sigma/fs) )
//
//    With those coefficients the scheme's poles are exactly
//    exp((-sigma +/- j*w)/fs): the frequency and the decay are correct to
//    machine precision, the stability ceiling moves to exactly Nyquist, and
//    the symplectic structure -- which the passivity proof below needs -- is
//    kept. It is the exact integrator and a symplectic integrator at once.
//
// 2. THE NONLINEAR PART MUST BE PROVABLY PASSIVE.
//
//    Adding a nonlinear force evaluated at the current state to any linear
//    integrator voids its stability guarantee. Tested directly on the tine's
//    own geometric nonlinearity, the naive version diverges in nine
//    milliseconds at a realistic playing amplitude.
//
//    Instead every nonlinearity is carried through a scalar auxiliary
//    variable (SAV / invariant energy quadratisation). A nonlinear potential
//    V(q) >= 0 is represented by psi = sqrt(2V), so that its contribution to
//    the energy is exactly psi^2/2 -- a square, therefore non-negative,
//    therefore incapable of being a source. The scheme updates psi alongside
//    q, and the resulting system is a rank-P perturbation of the identity,
//    solved directly by Woodbury. No iteration, no Newton solve, and the
//    stability condition comes out of the LINEAR part alone: the
//    nonlinearities contribute nothing to it, at any amplitude.
//
//    Two payoffs specific to this instrument:
//
//    - The tine's stretching nonlinearity has potential (EA/8)*K^2, which is
//      a constant times a square, so it is admissible with no conditions at
//      all. Its psi works out to a constant times K and its gradient to a
//      constant times G*q: no division, no singularity at rest, no fudge
//      constant.
//    - The tine-to-tonebar joint is extremely stiff. Left in the explicit
//      stiffness matrix it imposes a time-step limit -- measured at about
//      6.4e4 N/m for a plausible tine and bar, which is far below the real
//      joint. Carried as a quadratised term instead (psi = sqrt(Ks)*eta, with
//      a constant gradient) the stiffness stops constraining the time step
//      entirely, and the joint can be as rigid as the real aluminium block.
//
// State is double precision. The modal bank is a small fraction of the cost
// of the solver that sits on top of it, so the wider state is close to free,
// and it removes the damping-resolution cliff: at Q of a few thousand and a
// low fundamental, the per-sample decay factor sits only a handful of float
// ulps below one.
// ---------------------------------------------------------------------------

// One quadratised nonlinearity: its auxiliary variable and its gradient.
struct SavTerm
{
    double psi = 0.0;      // sqrt(2V), carries the sign of the root
    bool   active = false;
};

// A stacked modal system with up to `MaxN` modes and `MaxP` quadratised
// nonlinearities. The caller owns the physics: it says what the modes are,
// and each sample it supplies the gradient of each active nonlinearity.
template <int MaxN, int MaxP>
class SavModalSystem
{
public:
    static constexpr int kMaxModes = MaxN;
    static constexpr int kMaxTerms = MaxP;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        k  = 1.0 / sampleRate;
        n  = 0;
        clear();
    }

    void clear()
    {
        for (int i = 0; i < MaxN; ++i)
        {
            q[i] = qPrev[i] = 0.0;
            invM[i] = 1.0;
            stiff[i] = 0.0;
        }
        for (int p = 0; p < MaxP; ++p) { psi[p] = 0.0; termActive[p] = false; }
    }

    void setNumModes (int count) { n = std::clamp (count, 0, MaxN); }
    int  numModes() const { return n; }

    // Mode i: natural frequency, decay to -60 dB, and modal mass.
    //
    // The pre-warping is the whole point of these two lines. sigma is the
    // decay rate; w is the angular frequency; the effective stiffness that
    // comes out reproduces the exact continuous pole rather than the
    // frequency the naive scheme would actually produce, which at 5 kHz and
    // 44.1 kHz sample rate is nearly forty cents sharp.
    void setMode (int i, double freqHz, double t60Sec, double modalMass)
    {
        if (i < 0 || i >= MaxN) return;

        if (! (freqHz > 0.0) || freqHz >= 0.5 * fs || ! (modalMass > 0.0))
        {
            stiff[i] = 0.0; invM[i] = 0.0; damp[i] = 0.0;
            q[i] = qPrev[i] = 0.0;
            live[i] = false;
            return;
        }

        live[i] = true;
        invM[i] = 1.0 / modalMass;
        mass[i] = modalMass;

        const double w     = 2.0 * kPiD * freqHz;
        const double sigma = (t60Sec > 1.0e-5) ? (3.0 * std::log (10.0) / t60Sec) : 1.0e6;

        const double sT = sigma * k;
        const double a  = std::tanh (sT);
        const double ch = std::cosh (sT);

        // wEff^2 such that the discrete poles land exactly on
        // exp((-sigma +/- j w)/fs).
        const double inner = 2.0 - 2.0 * std::cos (w * k) / ch;
        const double wEff2 = std::max (0.0, inner) / (k * k);

        damp[i]  = a;
        stiff[i] = modalMass * wEff2;
        freq[i]  = freqHz;
        baseFreq[i] = freqHz;
        t60[i]   = t60Sec;
    }

    // Retune without touching stored energy. Used by the geometric
    // stiffening, at control rate.
    void setFrequency (int i, double freqHz)
    {
        if (i < 0 || i >= MaxN || ! live[i]) return;
        if (! (freqHz > 0.0) || freqHz >= 0.5 * fs) return;
        setModeKeepingState (i, freqHz, t60[i]);
    }

    double baseFrequency (int i) const { return (i >= 0 && i < MaxN) ? baseFreq[i] : 0.0; }
    double frequency (int i) const { return (i >= 0 && i < MaxN) ? freq[i] : 0.0; }
    bool   isLive (int i) const { return i >= 0 && i < MaxN && live[i]; }

    // ---- forcing --------------------------------------------------------
    // An external force on mode i for one sample. Enters as an impulse on the
    // momentum, which in this two-step q-only form is an offset on q[n+1].
    void addForce (int i, double force)
    {
        if (i < 0 || i >= MaxN || ! live[i]) return;
        drive[i] += force;
    }

    // ---- quadratised nonlinearities -------------------------------------
    // Declare term p with gradient `grad` (length n). The caller computes the
    // gradient from the CURRENT state; psi is carried internally.
    //
    // `psiInit` seeds the auxiliary variable the first time a term becomes
    // active, and must equal sqrt(2V) for the current state. For a potential
    // that is a perfect square -- which both of this instrument's structural
    // nonlinearities are -- that is just the signed root and is exact.
    void setTerm (int p, const double* grad, double psiInit, bool enable)
    {
        if (p < 0 || p >= MaxP) return;
        if (! enable) { termActive[p] = false; psi[p] = 0.0; return; }

        if (! termActive[p]) psi[p] = psiInit;
        termActive[p] = true;
        for (int i = 0; i < n; ++i) g[p][i] = grad[i];
    }

    void disableTerm (int p) { if (p >= 0 && p < MaxP) { termActive[p] = false; psi[p] = 0.0; } }

    // ---- the step -------------------------------------------------------
    void tick()
    {
        // b = (2I - k^2 Minv K) q - (I - alpha beta^T) qPrev - 2k alpha psi
        //     + k^2 Minv * drive
        //
        // with alpha_p = (k/2) Minv g_p and beta_p = (k/2) g_p. Damping enters
        // as the leapfrog's (1-a)/(1+a) on the qPrev term, which is what makes
        // the poles exact.
        double b[MaxN];

        for (int i = 0; i < n; ++i)
        {
            if (! live[i]) { b[i] = 0.0; continue;  }
            const double a  = damp[i];
            const double c1 = 2.0 / (1.0 + a);
            const double c2 = (1.0 - a) / (1.0 + a);
            b[i] = c1 * q[i] - c2 * qPrev[i]
                 - (k * k * invM[i] * stiff[i] / (1.0 + a)) * q[i]
                 + (k * k * invM[i] / (1.0 + a)) * drive[i];
        }

        // Collect the active terms.
        int act[MaxP], na = 0;
        for (int p = 0; p < MaxP; ++p) if (termActive[p]) act[na++] = p;

        if (na == 0)
        {
            for (int i = 0; i < n; ++i) { qPrev[i] = q[i]; q[i] = b[i]; }
            for (int i = 0; i < n; ++i) drive[i] = 0.0;
            return;
        }

        // alpha_p = (k/2) Minv g_p / (1+a),  beta_p = (k/2) g_p
        double alpha[MaxP][MaxN], beta[MaxP][MaxN];
        for (int j = 0; j < na; ++j)
        {
            const int p = act[j];
            for (int i = 0; i < n; ++i)
            {
                const double a = damp[i];
                alpha[j][i] = live[i] ? 0.5 * k * invM[i] * g[p][i] / (1.0 + a) : 0.0;
                beta[j][i]  = 0.5 * k * g[p][i];
            }
        }

        // b gets the explicit part of the nonlinear force:
        //   -2k alpha psi[n-1/2]  +  alpha (beta^T qPrev)
        for (int j = 0; j < na; ++j)
        {
            const int p = act[j];
            double bt = 0.0;
            for (int i = 0; i < n; ++i) bt += beta[j][i] * qPrev[i];
            const double coef = 2.0 * k * psi[p];
            for (int i = 0; i < n; ++i)
                b[i] += alpha[j][i] * (bt - coef);
        }

        // Solve (I + sum_j alpha_j beta_j^T) x = b by Woodbury on the na x na
        // system. na is at most a handful, so this is a tiny dense solve.
        double S[MaxP][MaxP], rhs[MaxP];
        for (int r = 0; r < na; ++r)
        {
            double br = 0.0;
            for (int i = 0; i < n; ++i) br += beta[r][i] * b[i];
            rhs[r] = br;
            for (int c = 0; c < na; ++c)
            {
                double v = 0.0;
                for (int i = 0; i < n; ++i) v += beta[r][i] * alpha[c][i];
                S[r][c] = v + (r == c ? 1.0 : 0.0);
            }
        }

        // Gaussian elimination with partial pivoting.
        double y[MaxP];
        for (int i = 0; i < na; ++i) y[i] = rhs[i];
        for (int c = 0; c < na; ++c)
        {
            int piv = c;
            for (int r = c + 1; r < na; ++r)
                if (std::abs (S[r][c]) > std::abs (S[piv][c])) piv = r;
            if (piv != c)
            {
                for (int cc = 0; cc < na; ++cc) std::swap (S[c][cc], S[piv][cc]);
                std::swap (y[c], y[piv]);
            }
            const double d = S[c][c];
            if (std::abs (d) < 1.0e-300) continue;
            for (int r = c + 1; r < na; ++r)
            {
                const double f = S[r][c] / d;
                if (f == 0.0) continue;
                for (int cc = c; cc < na; ++cc) S[r][cc] -= f * S[c][cc];
                y[r] -= f * y[c];
            }
        }
        for (int r = na - 1; r >= 0; --r)
        {
            double s = y[r];
            for (int c = r + 1; c < na; ++c) s -= S[r][c] * y[c];
            y[r] = (std::abs (S[r][r]) > 1.0e-300) ? s / S[r][r] : 0.0;
        }

        double qNext[MaxN];
        for (int i = 0; i < n; ++i)
        {
            double s = b[i];
            for (int j = 0; j < na; ++j) s -= alpha[j][i] * y[j];
            qNext[i] = s;
        }

        // psi[n+1/2] = psi[n-1/2] + (1/2) g^T (q[n+1] - q[n-1])
        for (int j = 0; j < na; ++j)
        {
            const int p = act[j];
            double d = 0.0;
            for (int i = 0; i < n; ++i) d += g[p][i] * (qNext[i] - qPrev[i]);
            psi[p] += 0.5 * d;
        }

        for (int i = 0; i < n; ++i) { qPrev[i] = q[i]; q[i] = qNext[i]; drive[i] = 0.0; }
    }

    // ---- readout ---------------------------------------------------------
    double displacement (int i) const { return (i >= 0 && i < MaxN) ? q[i] : 0.0; }

    double displacementAt (const double* shape) const
    {
        double u = 0.0;
        for (int i = 0; i < n; ++i) if (live[i]) u += shape[i] * q[i];
        return u;
    }

    double velocityAt (const double* shape) const
    {
        double v = 0.0;
        for (int i = 0; i < n; ++i) if (live[i]) v += shape[i] * (q[i] - qPrev[i]) * fs;
        return v;
    }

    // Total energy: kinetic + linear potential + every quadratised term.
    // This is the quantity the tests watch; it must never rise.
    double energy() const
    {
        double h = 0.0;
        for (int i = 0; i < n; ++i)
        {
            if (! live[i]) continue;
            const double v = (q[i] - qPrev[i]) / k;
            h += 0.5 * mass[i] * v * v;
            h += 0.5 * stiff[i] * q[i] * qPrev[i];
        }
        for (int p = 0; p < MaxP; ++p)
            if (termActive[p]) h += 0.5 * psi[p] * psi[p];
        return h;
    }

    void setState (int i, double displacement, double velocity)
    {
        if (i < 0 || i >= MaxN) return;
        q[i]     = displacement;
        qPrev[i] = displacement - velocity * k;
    }

    // Extra loss on one mode for one sample, as a multiplicative factor.
    // Scaling the state and its history together scales displacement and
    // velocity alike, so this removes energy without touching frequency.
    // Factors above one are refused: a damper must never be able to become a
    // source, whatever a caller asks for.
    void scaleMode (int i, double factor)
    {
        if (i < 0 || i >= MaxN || ! live[i]) return;
        const double g0 = std::clamp (factor, 0.0, 1.0);
        q[i]     *= g0;
        qPrev[i] *= g0;
    }

private:
    void setModeKeepingState (int i, double freqHz, double t60Sec)
    {
        const double w     = 2.0 * kPiD * freqHz;
        const double sigma = (t60Sec > 1.0e-5) ? (3.0 * std::log (10.0) / t60Sec) : 1.0e6;
        const double sT = sigma * k;
        const double inner = 2.0 - 2.0 * std::cos (w * k) / std::cosh (sT);
        damp[i]  = std::tanh (sT);
        stiff[i] = mass[i] * std::max (0.0, inner) / (k * k);
        freq[i]  = freqHz;
    }

    double fs = 48000.0, k = 1.0 / 48000.0;
    int    n  = 0;

    double q[MaxN] {}, qPrev[MaxN] {}, drive[MaxN] {};
    double invM[MaxN] {}, mass[MaxN] {}, stiff[MaxN] {}, damp[MaxN] {};
    double freq[MaxN] {}, baseFreq[MaxN] {}, t60[MaxN] {};
    bool   live[MaxN] {};

    double g[MaxP][MaxN] {};
    double psi[MaxP] {};
    bool   termActive[MaxP] {};
};

} // namespace epi
