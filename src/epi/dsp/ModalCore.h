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

    // Highest mode frequency as a fraction of the sample rate. 1/pi is the
    // omega*k < 2 bound that an explicit contact imposes; see setMode.
    static constexpr double kModeBudget = 1.0 / kPiD;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        k  = 1.0 / sampleRate;
        n  = 0;
        clear();
    }

    // Clears the STATE. It does not touch the mode definitions, and it used to:
    // it set every stiffness to zero and every inverse mass to one, which
    // destroys the modes rather than clearing them.
    //
    // That mattered because Harp::prepare sets its six modes and then calls
    // reset(), so the frame's modes were wiped immediately after being
    // configured and it has never resonated -- it behaved as a damped free
    // mass. Nothing caught it because the step read stiffness and mass live,
    // so a zero stiffness is a perfectly well behaved integrator, and the
    // sympathetic path still passed its test by moving at all.
    void clear()
    {
        for (int i = 0; i < MaxN; ++i)
        {
            q[i] = qPrev[i] = 0.0;
            drive[i] = 0.0;
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

        // Mode budget: fs/pi, not Nyquist.
        //
        // The pre-warped scheme itself is stable to Nyquist, but a mode is only
        // as safe as everything coupled to it, and an EXPLICIT contact force
        // has no energy balance of its own. The binding condition is then
        // omega*k < 2, i.e. f < fs/pi ~ 0.318*fs (Ducceschi, Bilbao & Webb,
        // DAFx-23 eq. 37, stated three times across that group's papers).
        // Retaining modes between fs/pi and Nyquist is what lets a hammer
        // deposit energy into modes the scheme cannot carry.
        if (! (freqHz > 0.0) || freqHz >= kModeBudget * fs || ! (modalMass > 0.0))
        {
            stiff[i] = 0.0; invM[i] = 0.0; damp[i] = 0.0;
            q[i] = qPrev[i] = 0.0;
            live[i] = false;
            cacheStep (i);
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
        cacheStep (i);
    }

    // Retune without touching stored energy. Used by the geometric
    // stiffening, at control rate.
    void setFrequency (int i, double freqHz)
    {
        if (i < 0 || i >= MaxN || ! live[i]) return;
        if (! (freqHz > 0.0) || freqHz >= kModeBudget * fs) return;
        setModeKeepingState (i, freqHz, t60[i]);
    }

    // Retune AND redamp without touching stored energy: the body benches
    // re-pitch a ringing frame, and a live sweep must move the ring, not cut
    // it.
    void retuneKeepingState (int i, double freqHz, double t60Sec)
    {
        if (i < 0 || i >= MaxN || ! live[i]) return;
        if (! (freqHz > 0.0) || freqHz >= kModeBudget * fs) return;
        setModeKeepingState (i, freqHz, t60Sec);
        t60[i] = t60Sec;
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
        //
        // Written as fused single passes: the earlier form materialised
        // alpha[][] and beta[][] scratch arrays each tick and re-read them
        // four times, and at ninety-six kilohertz that memory traffic was the
        // single largest line in the profile. Every use of alpha and beta is
        // a dot product with g, so the scalars are folded in and the arrays
        // never exist. The Woodbury matrix is symmetric -- beta_r . alpha_c
        // is the cAl-weighted product of two gradients, indifferent to their
        // order -- so only its upper triangle is computed.
        // A dormant voice is one mode and no terms, and there are often
        // eighty of them a sample: worth its own three lines ahead of the
        // scratch array and the term scan.
        bool anyTerm = false;
        for (int p = 0; p < MaxP; ++p) anyTerm |= termActive[p];
        if (n == 1 && ! anyTerm)
        {
            const double b0 = cAK[0] * q[0] - cB[0] * qPrev[0] + cD[0] * drive[0];
            qPrev[0] = q[0];
            q[0] = b0;
            drive[0] = 0.0;
            return;
        }

        double b[MaxN];

        // Branch-free: a dead mode has zero coefficients, so it contributes
        // nothing and does not need testing for. cAK is (cA - cK), folded at
        // coefficient time.
        for (int i = 0; i < n; ++i)
            b[i] = cAK[i] * q[i] - cB[i] * qPrev[i] + cD[i] * drive[i];

        // Collect the active terms.
        int act[MaxP], na = 0;
        for (int p = 0; p < MaxP; ++p) if (termActive[p]) act[na++] = p;

        if (na == 0)
        {
            // One pass: swap the history, place the step, clear the drive.
            for (int i = 0; i < n; ++i)
            {
                qPrev[i] = q[i];
                q[i] = b[i];
                drive[i] = 0.0;
            }
            return;
        }

        const double hk = 0.5 * k;

        // alpha_p = cAl * g_p, materialised once per term because it is read
        // three times and a contiguous row is what the vector unit wants.
        // beta_p = (k/2) g_p never needs to exist: every use is a dot with
        // g_p and the scalar folds into the sum.
        double alpha[MaxP][MaxN];
        for (int j = 0; j < na; ++j)
        {
            const double* gj = g[act[j]];
            for (int i = 0; i < n; ++i) alpha[j][i] = cAl[i] * gj[i];
        }

        // b gets the explicit part of the nonlinear force:
        //   alpha_j (beta_j^T qPrev - 2k psi_j)
        for (int j = 0; j < na; ++j)
        {
            const double* gj = g[act[j]];
            double bt = 0.0;
            for (int i = 0; i < n; ++i) bt += gj[i] * qPrev[i];
            const double wj = hk * bt - 2.0 * k * psi[act[j]];
            for (int i = 0; i < n; ++i) b[i] += alpha[j][i] * wj;
        }

        // Solve (I + sum_j alpha_j beta_j^T) x = b by Woodbury on the na x na
        // system. S is symmetric -- beta_r . alpha_c is the cAl-weighted
        // product of two gradients, indifferent to order -- so only the upper
        // triangle is summed.
        double S[MaxP][MaxP], rhs[MaxP];
        for (int r = 0; r < na; ++r)
        {
            const double* gr = g[act[r]];
            double br = 0.0;
            for (int i = 0; i < n; ++i) br += gr[i] * b[i];
            rhs[r] = hk * br;
            for (int c = r; c < na; ++c)
            {
                double v = 0.0;
                for (int i = 0; i < n; ++i) v += gr[i] * alpha[c][i];
                S[r][c] = hk * v + (r == c ? 1.0 : 0.0);
                S[c][r] = S[r][c];
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

        // x lands in b in place, one contiguous pass per term.
        for (int j = 0; j < na; ++j)
        {
            const double yj = y[j];
            for (int i = 0; i < n; ++i) b[i] -= alpha[j][i] * yj;
        }

        // psi[n+1/2] = psi[n-1/2] + (1/2) g^T (q[n+1] - q[n-1])
        for (int j = 0; j < na; ++j)
        {
            const double* gj = g[act[j]];
            double d = 0.0;
            for (int i = 0; i < n; ++i) d += gj[i] * (b[i] - qPrev[i]);
            psi[act[j]] += 0.5 * d;
        }

        for (int i = 0; i < n; ++i) { qPrev[i] = q[i]; q[i] = b[i]; drive[i] = 0.0; }
    }

    // ---- readout ---------------------------------------------------------
    double displacement (int i) const { return (i >= 0 && i < MaxN) ? q[i] : 0.0; }
    double psiValue (int p) const { return (p >= 0 && p < MaxP) ? psi[p] : 0.0; }
    bool   termIsActive (int p) const { return (p >= 0 && p < MaxP) && termActive[p]; }

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
    // The step's per-mode coefficients, which depend only on the mode and so
    // have no business being recomputed every sample.
    //
    // They were, and it was the single largest cost in the instrument: two
    // divisions per mode per sample is 186 million divisions a second across
    // eighty-eight tines of twenty-two modes, for numbers that change only when
    // a mode is retuned. Hoisting them also removes the live[] branch from the
    // inner loop, which is what was stopping it vectorising.
    void cacheStep (int i)
    {
        if (! live[i]) { cA[i] = cB[i] = cK[i] = cD[i] = cAl[i] = cAK[i] = 0.0; return; }
        const double a  = damp[i];
        const double r  = 1.0 / (1.0 + a);
        cA[i]  = 2.0 * r;
        cB[i]  = (1.0 - a) * r;
        cK[i]  = k * k * invM[i] * stiff[i] * r;
        cD[i]  = k * k * invM[i] * r;
        cAl[i] = 0.5 * k * invM[i] * r;
        cAK[i] = cA[i] - cK[i];
    }

    void setModeKeepingState (int i, double freqHz, double t60Sec)
    {
        const double w     = 2.0 * kPiD * freqHz;
        const double sigma = (t60Sec > 1.0e-5) ? (3.0 * std::log (10.0) / t60Sec) : 1.0e6;
        const double sT = sigma * k;
        const double inner = 2.0 - 2.0 * std::cos (w * k) / std::cosh (sT);
        damp[i]  = std::tanh (sT);
        stiff[i] = mass[i] * std::max (0.0, inner) / (k * k);
        cacheStep (i);
        freq[i]  = freqHz;
    }

    double fs = 48000.0, k = 1.0 / 48000.0;
    int    n  = 0;

    double q[MaxN] {}, qPrev[MaxN] {}, drive[MaxN] {};
    double cA[MaxN] {}, cB[MaxN] {}, cK[MaxN] {}, cD[MaxN] {}, cAl[MaxN] {}, cAK[MaxN] {};
    double invM[MaxN] {}, mass[MaxN] {}, stiff[MaxN] {}, damp[MaxN] {};
    double freq[MaxN] {}, baseFreq[MaxN] {}, t60[MaxN] {};
    bool   live[MaxN] {};

    double g[MaxP][MaxN] {};
    double psi[MaxP] {};
    bool   termActive[MaxP] {};
};

} // namespace epi
