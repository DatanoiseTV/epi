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
// The platform's vector header, at global scope where a system header has to
// be. Inside a namespace its include guard is consumed there too, so every
// NEON type lands in epi:: and is unavailable to anything that includes this
// header and then needs vectors itself -- which the plugin does, because JUCE
// pulls <arm_neon.h> in through juce_dsp. That combination does not compile,
// and the only reason the tree built at all was that no translation unit had
// yet needed both.
#if defined (__aarch64__) && ! defined (EPI_SCALAR_DOTS)
 #include <arm_neon.h>
#endif

namespace epi
{

inline constexpr double kPiD = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Two doubles at a time, with the association unchanged.
//
// One hot loop earns this: the board's bridge dot in GrandBoard.h, which
// runs once per sounding voice per sample and is a reduction. This project's
// rule is that the order of a floating-point sum is fixed in the source, so
// it is written with eight independent accumulators and a hand-written
// combining tree -- the split is what keeps the reduction off the FMA latency
// chain, and the tree says exactly which partial sums are added to which.
//
// What that costs is codegen. Clang sees N scalar chains striding by N and
// vectorises them by DE-INTERLEAVING: the 72-mode bridge dot came out of
// Apple clang 21 on aarch64 as a fully unrolled run of ld2.2d pairs with a
// 336-byte frame and a dozen register spills, because eight strided chains do
// not fit the register file. It ran at about 1.3 fused multiply-adds per
// cycle where the core will retire four.
//
// The fix is to notice that the split is already the right shape. Accumulator
// u_j takes the indices congruent to j, so the PAIR (u_0, u_1) wants elements
// (m, m+1) -- adjacent -- and (u_2, u_3) wants (m+2, m+3). Each accumulator
// pair is one 128-bit lane pair fed by one ordinary contiguous load. Every
// lane keeps its own chain in its own order, so the arithmetic is the same
// arithmetic, and the combining tree at the end is written with the same
// parenthesisation as before.
//
// Only aarch64 is vectorised here. It is the target this was measured on, and
// the equivalence argument depends on the compiler contracting the scalar
// `acc += a * b` into a fused multiply-add, which aarch64 does (verified in
// the emitted assembly: 36 fmla.2d for the 72-mode dot, one per element) and
// a baseline x86-64 without FMA does not. Everywhere else takes the scalar
// loop, which is the same numbers it always produced. Defining
// EPI_SCALAR_DOTS forces the scalar form back on aarch64 too, which is how
// the equality is checked: the rendered grand digests must not move.
// ---------------------------------------------------------------------------
#if defined (__aarch64__) && ! defined (EPI_SCALAR_DOTS)
 #define EPI_HAVE_F64X2 1

namespace simd
{
    using f64x2 = float64x2_t;

    inline f64x2 zero()                 { return vdupq_n_f64 (0.0); }
    inline f64x2 load (const double* p) { return vld1q_f64 (p); }
    // acc + a*b, fused: the same single rounding the scalar form gets from
    // the compiler's own contraction.
    inline f64x2 fma (f64x2 acc, f64x2 a, f64x2 b) { return vfmaq_f64 (acc, a, b); }
    // lane0 + lane1, in that order: the leaf of the combining tree.
    inline double pairSum (f64x2 v) { return vgetq_lane_f64 (v, 0) + vgetq_lane_f64 (v, 1); }
}
#endif

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
            stiff[i] = 0.0;
            q[i] = qPrev[i] = 0.0;
            live[i] = false;
            cacheStep (i, 0.0, 0.0);
            return;
        }

        live[i] = true;
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

        stiff[i] = modalMass * wEff2;
        freq[i]  = freqHz;
        baseFreq[i] = freqHz;
        t60[i]   = t60Sec;
        cacheStep (i, a, 1.0 / modalMass);
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
    // The sympathetic fast path, for a bank of linear modes coupled to a
    // bridge through one weight vector: accumulate the bridge-sense force
    // from the pre-step displacements, add the bridge drive, step the
    // leapfrog, and read the output shape from the post-step state -- all in
    // one pass over the arrays. Identical arithmetic to addForce + tick +
    // displacementAt in that order; the fusion exists because the grand runs
    // this for close to eighty open strings every sample and the separate
    // passes re-read every coefficient stream three times. Preconditions:
    // no active terms (the grand's strings are linear) and the coupled
    // prefix is the whole active bank (n == nc), both true by construction
    // for a sympathetic grand voice.
    double tickCoupled (const double* w, double uB,
                        const double* readShape, double& senseOut)
    {
        // Two-way split accumulators for the same reason as the board's
        // bridge dot: keep the reduction off the FMA latency chain.
        // Deliberately NOT hand-vectorised, and the reason is not the usual
        // one. The two split accumulators are what this file's own rule calls
        // a fixed association -- and in the shipped binary they are not.
        // Clang's loop vectoriser rewrites the pair loop below into ONE
        // accumulator fed lane by lane, with the w*q and readShape*b products
        // rounded separately rather than fused; the same source built with
        // -fno-vectorize renders a different stream, byte for byte. So the
        // arithmetic that ships here is the vectoriser's choice, not this
        // text's, and any hand-written form that is actually fast -- two
        // independent lane chains, fused products -- is a DIFFERENT sum from
        // what the instrument sounds like today. Measured, that form bought
        // 1.8% of the ten-note pedal figure and moved the render, which is
        // the wrong side of the trade. The board's bridge dot in GrandBoard.h
        // is the opposite case: there the vectoriser keeps the association
        // and only picks a poor instruction sequence, so writing the sequence
        // out by hand costs nothing.
        double F0 = 0.0, F1 = 0.0, o0 = 0.0, o1 = 0.0;
        int i = 0;
        for (; i + 1 < n; i += 2)
        {
            const double qa = q[i], qb = q[i + 1];
            F0 += w[i] * qa;
            F1 += w[i + 1] * qb;
            const double ba = cAK[i] * qa - cB[i] * qPrev[i]
                            + cD[i] * (drive[i] + w[i] * uB);
            const double bb = cAK[i + 1] * qb - cB[i + 1] * qPrev[i + 1]
                            + cD[i + 1] * (drive[i + 1] + w[i + 1] * uB);
            qPrev[i] = qa;         qPrev[i + 1] = qb;
            q[i] = ba;             q[i + 1] = bb;
            drive[i] = 0.0;        drive[i + 1] = 0.0;
            o0 += readShape[i] * ba;
            o1 += readShape[i + 1] * bb;
        }
        for (; i < n; ++i)
        {
            const double qo = q[i];
            F0 += w[i] * qo;
            const double bi = cAK[i] * qo - cB[i] * qPrev[i]
                            + cD[i] * (drive[i] + w[i] * uB);
            qPrev[i] = qo;
            q[i] = bi;
            drive[i] = 0.0;
            o0 += readShape[i] * bi;
        }
        senseOut = (F0 + F1);
        return (o0 + o1);
    }

    void tick()
    {
        // b = (2I - k^2 Minv K) q - (I - alpha beta^T) qPrev - 2k alpha psi
        //     + k^2 Minv * drive
        //
        // with alpha_p = (k/2) Minv g_p and beta_p = (k/2) g_p. Damping enters
        // as the leapfrog's (1-a)/(1+a) on the qPrev term, which is what makes
        // the poles exact.
        //
        // The active terms are collected FIRST, so that a bank with none of
        // them -- every grand string between contacts, every dormant voice,
        // and the great majority of ticks in any instrument -- never touches
        // the solver's scratch at all. That path is written as one fused
        // pass below; the quadratised one lives in tickWithTerms.
        int act[MaxP], na = 0;
        for (int p = 0; p < MaxP; ++p) if (termActive[p]) act[na++] = p;

        if (na == 0)
        {
            // One pass: read the state, place the step, clear the drive. The
            // earlier form staged the step in a b[MaxN] scratch and copied it
            // back, which is a second write and a second read of the whole
            // bank -- and at MaxN 220 that scratch put the frame over a page,
            // so every call paid a stack probe (___chkstk_darwin, visible in
            // the profile) whether it needed the scratch or not. Nothing
            // about the arithmetic changes: same three terms, same order,
            // same association, and the step for mode i reads nothing but
            // mode i.
            //
            // Branch-free: a dead mode has zero coefficients, so it
            // contributes nothing and does not need testing for. cAK is
            // (cA - cK), folded at coefficient time.
            for (int i = 0; i < n; ++i)
            {
                const double bi = cAK[i] * q[i] - cB[i] * qPrev[i] + cD[i] * drive[i];
                qPrev[i] = q[i];
                q[i] = bi;
                drive[i] = 0.0;
            }
            return;
        }

        tickWithTerms (act, na);
    }

private:
    // The quadratised path, kept out of line so that the linear step above
    // keeps a frame small enough to skip the stack probe: this one owns the
    // b[] and alpha[][] scratch.
    //
    // Written as fused single passes: an earlier form materialised alpha[][]
    // and beta[][] scratch arrays each tick and re-read them four times, and
    // at ninety-six kilohertz that memory traffic was the single largest line
    // in the profile. Every use of beta is a dot product with g, so its
    // scalar is folded in and that array never exists. The Woodbury matrix is
    // symmetric -- beta_r . alpha_c is the cAl-weighted product of two
    // gradients, indifferent to their order -- so only its upper triangle is
    // computed.
#if defined (__GNUC__)
    __attribute__ ((noinline))
#endif
    void tickWithTerms (const int* act, int na)
    {
        double b[MaxN];
        for (int i = 0; i < n; ++i)
            b[i] = cAK[i] * q[i] - cB[i] * qPrev[i] + cD[i] * drive[i];

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

public:
    // ---- readout ---------------------------------------------------------
    double displacement (int i) const { return (i >= 0 && i < MaxN) ? q[i] : 0.0; }

    // Raw views for hot exchange loops (the grand's bridge two-port runs a
    // dot with every voice's shape vector every sample; per-element checked
    // accessors defeat vectorisation there). Read-only displacements and a
    // writable drive accumulator; bounds are the caller's mode count.
    const double* displacementData() const { return q; }
    double*       driveData()              { return drive; }
    double psiValue (int p) const { return (p >= 0 && p < MaxP) ? psi[p] : 0.0; }
    bool   termIsActive (int p) const { return (p >= 0 && p < MaxP) && termActive[p]; }

    double displacementAt (const double* shape) const
    {
        // No live[] gate: a dead mode's q is zeroed at deactivation and its
        // coefficients keep it there, so the term is exactly zero and the
        // branch only cost the vectoriser the loop. Split accumulators keep
        // the reduction off the FMA latency chain.
        double u0 = 0.0, u1 = 0.0, u2 = 0.0, u3 = 0.0;
        int i = 0;
        for (; i + 3 < n; i += 4)
        {
            u0 += shape[i]     * q[i];
            u1 += shape[i + 1] * q[i + 1];
            u2 += shape[i + 2] * q[i + 2];
            u3 += shape[i + 3] * q[i + 3];
        }
        for (; i < n; ++i) u0 += shape[i] * q[i];
        return (u0 + u1) + (u2 + u3);
    }

    double velocityAt (const double* shape) const
    {
        double v = 0.0;
        for (int i = 0; i < n; ++i) v += shape[i] * (q[i] - qPrev[i]) * fs;
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
    // `a` is the mode's tanh(sigma/fs) and `invMass` its 1/M. Both used to be
    // kept as arrays, and neither was read anywhere else: every call site has
    // them in hand immediately before calling, so storing them only widened
    // the object the hot loops reach across. cA and cK went the same way --
    // only their difference cAK is ever stepped with. Four arrays of MaxN
    // doubles each, which for the grand's two hundred and sixty-four string
    // banks is 1.9 MB that no sample ever touched.
    void cacheStep (int i, double a, double invMass)
    {
        if (! live[i]) { cB[i] = cD[i] = cAl[i] = cAK[i] = 0.0; return; }
        const double r  = 1.0 / (1.0 + a);
        const double cA = 2.0 * r;
        const double cK = k * k * invMass * stiff[i] * r;
        cB[i]  = (1.0 - a) * r;
        cD[i]  = k * k * invMass * r;
        cAl[i] = 0.5 * k * invMass * r;
        cAK[i] = cA - cK;
    }

    void setModeKeepingState (int i, double freqHz, double t60Sec)
    {
        const double w     = 2.0 * kPiD * freqHz;
        const double sigma = (t60Sec > 1.0e-5) ? (3.0 * std::log (10.0) / t60Sec) : 1.0e6;
        const double sT = sigma * k;
        const double inner = 2.0 - 2.0 * std::cos (w * k) / std::cosh (sT);
        const double a = std::tanh (sT);
        stiff[i] = mass[i] * std::max (0.0, inner) / (k * k);
        cacheStep (i, a, 1.0 / mass[i]);
        freq[i]  = freqHz;
    }

    double fs = 48000.0, k = 1.0 / 48000.0;
    int    n  = 0;

    double q[MaxN] {}, qPrev[MaxN] {}, drive[MaxN] {};
    double cB[MaxN] {}, cD[MaxN] {}, cAl[MaxN] {}, cAK[MaxN] {};
    double mass[MaxN] {}, stiff[MaxN] {};
    double freq[MaxN] {}, baseFreq[MaxN] {}, t60[MaxN] {};
    bool   live[MaxN] {};

    double g[MaxP][MaxN] {};
    double psi[MaxP] {};
    bool   termActive[MaxP] {};
};

} // namespace epi
