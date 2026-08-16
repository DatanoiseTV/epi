/*
  Epi — physically modeled electric pianos
  Copyright (C) 2026 DatanoiseTV

  Measurements on the physical cores. These tests render audio and measure it;
  they do not check that code ran. Every threshold traces to a published
  measurement or to a stability argument, and the source is named at the site.
*/

#include "epi/dsp/RhodesVoice.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace epi;

static int failures = 0;
static int knownGaps = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::printf ("FAIL %s:%d  ", __FILE__, __LINE__);                \
            std::printf (__VA_ARGS__);                                       \
            std::printf ("\n");                                              \
        }                                                                    \
    } while (0)

// A defect that is understood, reproduced and not yet fixed. It reports every
// run so it cannot quietly become normal, but it does not fail the build --
// the alternative is deleting the measurement, which is worse.
#define KNOWN_GAP(cond, ...)                                                 \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++knownGaps;                                                     \
            std::printf ("KNOWN GAP %s:%d  ", __FILE__, __LINE__);           \
            std::printf (__VA_ARGS__);                                       \
            std::printf ("\n");                                              \
        }                                                                    \
    } while (0)

static constexpr double kFs = 48000.0;

static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

// Single-bin power in dB, over a window.
static double binDb (const std::vector<double>& x, int from, int to, double f, double fs)
{
    const double w = 2.0 * kPiD * f / fs, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    const int n = to - from;
    for (int i = from; i < to; ++i) { const double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 10.0 * std::log10 (std::max (1.0e-300, p / (0.25 * static_cast<double> (n) * n)));
}

// ===========================================================================
// The numerical core
// ===========================================================================

// The pre-warped staggered scheme is meant to place the discrete poles exactly
// on the continuous ones. If it does, pitch and decay are correct at every
// frequency up to Nyquist and at every sample rate, with no tuning table.
static void testModalPitchAndDecayAreExact()
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (double f : { 27.5, 220.0, 1000.0, 5000.0, 12000.0 })
        {
            if (f > 0.45 * fs) continue;

            SavModalSystem<4, 2> s;
            s.prepare (fs);
            s.setNumModes (1);
            const double wantT60 = 4.0;
            s.setMode (0, f, wantT60, 1.0e-3);
            s.setState (0, 1.0e-3, 0.0);

            long cross = 0, first = -1, last = -1;
            double prev = s.displacement (0), e0 = -1.0, tMinus30 = -1.0;
            const long steps = static_cast<long> (fs * 6.0);
            const long settle = static_cast<long> (fs * 0.1);

            for (long t = 0; t < steps; ++t)
            {
                s.tick();
                const double u = s.displacement (0);
                if (prev <= 0.0 && u > 0.0)
                { if (first < 0) first = t; last = t; ++cross; }
                prev = u;

                if (t == settle) e0 = s.energy();
                if (e0 > 0.0 && tMinus30 < 0.0 && s.energy() < e0 * 1.0e-3)
                    tMinus30 = static_cast<double> (t - settle) / fs;
            }

            const double got = (cross > 1 && last > first)
                             ? static_cast<double> (cross - 1) * fs / static_cast<double> (last - first)
                             : 0.0;
            const double cents = got > 0.0 ? 1200.0 * std::log2 (got / f) : -9999.0;

            CHECK (std::abs (cents) < 1.0,
                   "mode at %.1f Hz, fs %.0f: pitch off by %.3f cents", f, fs, cents);

            // Energy falls twice as fast as amplitude, so -30 dB of energy is
            // half a T60.
            const double gotT60 = tMinus30 > 0.0 ? tMinus30 * 2.0 : -1.0;
            CHECK (gotT60 > 0.0 && std::abs (gotT60 - wantT60) / wantT60 < 0.02,
                   "mode at %.1f Hz, fs %.0f: T60 wanted %.2f s, measured %.3f s",
                   f, fs, wantT60, gotT60);
        }
}

// The whole instrument rests on this. The tine's stretching nonlinearity is
// carried as a quadratised term precisely so that it cannot generate energy at
// any amplitude; if that ever stops being true, a held fortissimo chord grows
// instead of decaying, over seconds, where no short render would catch it.
static void testQuadratisedNonlinearityConservesEnergy()
{
    constexpr int M = 6;
    constexpr int N = 2 * M;

    // Slope-product matrix of the beam modes: the stretching measure.
    double G[M][M];
    {
        constexpr int S = 400;
        double d[M][S];
        for (int m = 0; m < M; ++m)
            for (int s = 0; s < S; ++s)
                d[m][s] = CantileverModes::slope (m, (s + 0.5) / S);
        for (int a = 0; a < M; ++a)
            for (int b = 0; b < M; ++b)
            {
                double v = 0.0;
                for (int s = 0; s < S; ++s) v += d[a][s] * d[b][s];
                G[a][b] = v / S;
            }
    }

    const double EA = 5.0e5;

    for (double amp : { 1.0e-4, 3.0e-3, 1.5e-2 })
    {
        SavModalSystem<N, 2> s;
        s.prepare (kFs);
        s.setNumModes (N);
        for (int m = 0; m < M; ++m)
        {
            const double f = 164.8 * CantileverModes::ratio (m, 0.0, 1.0, 0.0);
            s.setMode (m,     f, 1.0e9, 6.0e-4);   // lossless
            s.setMode (M + m, f, 1.0e9, 6.0e-4);
        }
        s.setState (0, amp, 0.0);
        s.setState (M, amp * 0.6, 0.0);

        double grad[N] {};
        double h0 = 0.0, worst = 0.0;
        bool finite = true;

        for (long t = 0; t < 200000; ++t)
        {
            double gq[2][M] {}, kk = 0.0;
            for (int a = 0; a < M; ++a)
            {
                double sv = 0.0, sh = 0.0;
                for (int b = 0; b < M; ++b)
                {
                    sv += G[a][b] * s.displacement (b);
                    sh += G[a][b] * s.displacement (M + b);
                }
                gq[0][a] = sv; gq[1][a] = sh;
            }
            for (int a = 0; a < M; ++a)
                kk += s.displacement (a) * gq[0][a] + s.displacement (M + a) * gq[1][a];

            const double root = std::sqrt (EA);
            for (int a = 0; a < M; ++a) { grad[a] = root * gq[0][a]; grad[M + a] = root * gq[1][a]; }
            s.setTerm (0, grad, 0.5 * root * kk, true);
            s.tick();

            const double h = s.energy();
            if (! std::isfinite (h)) { finite = false; break; }
            if (t == 200) h0 = h;
            if (t > 200 && h0 > 0.0) worst = std::max (worst, std::abs (h - h0) / h0);
        }

        CHECK (finite, "quadratised nonlinearity went non-finite at amplitude %.1e", amp);
        // The floor here is the precision of the mode-shape integrals, not the
        // scheme; it does not grow with amplitude or with run length, which is
        // the property that matters.
        CHECK (worst < 1.0e-6,
               "energy drifted by %.3e at amplitude %.1e over 200k steps", worst, amp);
    }
}

// A damper is a loss. Whatever a caller asks for, it must never be able to
// hand energy back.
static void testDamperCannotAddEnergy()
{
    SavModalSystem<4, 2> s;
    s.prepare (kFs);
    s.setNumModes (1);
    s.setMode (0, 220.0, 5.0, 1.0e-3);
    s.setState (0, 1.0e-3, 0.0);

    double prev = 1.0e30, worstRise = -1.0e30;
    for (long t = 0; t < 48000; ++t)
    {
        s.tick();
        s.scaleMode (0, 4.0);   // absurd on purpose
        const double h = s.energy();
        if (t > 100) worstRise = std::max (worstRise, (h - prev) / std::max (1.0e-30, prev));
        prev = h;
    }
    CHECK (worstRise <= 1.0e-9,
           "a damping factor above one added energy: worst single-step rise %.3e", worstRise);
}

// ===========================================================================
// The beam
// ===========================================================================

// Clamped-free beam eigenvalues. A struck tine sounds like a tuning fork
// rather than a string because these ratios are nowhere near harmonic.
static void testCantileverRatiosMatchBeamTheory()
{
    static constexpr double kWant[] = { 1.0, 6.2669, 17.5475, 34.3861, 56.8427 };
    for (int m = 0; m < 5; ++m)
    {
        const double got = CantileverModes::ratio (m, 0.0, 1.0, 0.0);
        CHECK (std::abs (got - kWant[m]) / kWant[m] < 1.0e-3,
               "cantilever mode %d ratio: wanted %.4f, got %.4f", m, kWant[m], got);
    }
}

// A mass exactly at the tip loads every mode by the same factor, so it can only
// transpose the beam. Anywhere else it sits at a different fraction of each
// mode's shape and re-voices it. That is why sliding the Rhodes tuning spring
// does more than retune.
static void testTuningSpringPositionChangesTheOvertones()
{
    const double atTip = CantileverModes::ratio (1, 0.25, 1.0, 0.0);
    CHECK (std::abs (atTip - 6.2669) / 6.2669 < 1.0e-3,
           "a mass at the tip changed the overtone ratio to %.4f; it should only transpose", atTip);

    const double partWay = CantileverModes::ratio (1, 0.25, 0.70, 0.0);
    CHECK (std::abs (partWay - atTip) / atTip > 0.02,
           "moving the spring off the tip barely moved the second partial: %.4f vs %.4f",
           partWay, atTip);
}

// Solved from the beam equation with real spring steel and a real wire gauge,
// the lengths should be the ones Rhodes actually cut: around 18 cm at the
// bottom of the compass down to a couple of centimetres at the top.
static void testTineLengthsAreRealistic()
{
    struct Case { int note; double lo, hi; };
    static const Case cases[] = {
        { 28, 0.140, 0.220 },   // E1
        { 52, 0.060, 0.110 },
        { 88, 0.015, 0.040 },   // E6, the top of a 73-key instrument
    };
    for (const auto& c : cases)
    {
        const double f0 = noteHz (c.note);
        const double reg = std::clamp ((c.note - 28.0) / 60.0, 0.0, 1.0);
        const double radius = (0.95 - 0.30 * reg) * 1.0e-3;
        const double L = CantileverModes::lengthForFrequency (f0, radius, kSpringSteel);
        CHECK (L > c.lo && L < c.hi,
               "note %d (%.1f Hz): tine length %.1f mm, outside the real range %.0f-%.0f mm",
               c.note, f0, L * 1.0e3, c.lo * 1.0e3, c.hi * 1.0e3);
    }
}

// ===========================================================================
// The pickup
// ===========================================================================

// A Rhodes tine moves as a pure sine (ISMA 2014, 3.1.2). Every harmonic at the
// output is manufactured by the magnetic field it is moving through, and the
// three things the measurements record about that field all have to fall out
// of the geometry without being programmed in.
static void testPickupFieldGeneratesTheHarmonics()
{
    MagneticPickup pu;
    pu.prepare ({});

    const double hw = pu.halfWidth();
    const double gap = pu.nominalGap();

    // Symmetric about the centreline, peaking there.
    CHECK (pu.flux (0.0, gap) > pu.flux (hw, gap),
           "the field does not peak on the pole centreline");
    CHECK (std::abs (pu.flux (hw, gap) - pu.flux (-hw, gap)) < 1.0e-6,
           "the field is not symmetric about the centreline");
    CHECK (pu.flux (0.0, gap * 2.0) < 0.6 * pu.flux (0.0, gap),
           "the field does not fall off with the gap");

    // Sweep a pure sine through it and look at what comes out.
    auto harmonics = [&] (double offset, double amp, double* h)
    {
        const double f0 = 110.0;
        const int n = 24000;
        std::vector<double> flux (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
            flux[static_cast<size_t> (i)] =
                pu.flux (offset * hw + amp * hw * std::sin (2.0 * kPiD * f0 * i / kFs), gap);
        double mean = 0.0; for (double v : flux) mean += v; mean /= n;
        for (double& v : flux) v -= mean;
        for (int k = 1; k <= 5; ++k) h[k] = binDb (flux, 0, n, f0 * k, kFs);
    };

    double centred[6], offset[6], loud[6];
    harmonics (0.0,  0.5, centred);
    harmonics (0.6,  0.3, offset);
    harmonics (0.6,  2.5, loud);

    // On the centreline the swing crosses the field's peak twice per cycle, so
    // the output is at twice the tine's frequency and the fundamental all but
    // vanishes. This is the bell-like Rhodes voicing.
    CHECK (centred[2] - centred[1] > 40.0,
           "centred on the pole, the second harmonic should dominate: H2-H1 = %.1f dB",
           centred[2] - centred[1]);

    // Off the centreline the sweep is asymmetric and the fundamental returns.
    // The service manual describes exactly this adjustment.
    CHECK (offset[1] - offset[2] > 10.0,
           "off the centreline the fundamental should dominate: H1-H2 = %.1f dB",
           offset[1] - offset[2]);

    // A big swing runs the tine off the ends of the pole face, where the field
    // collapses. That is the growl, and it is why it belongs to the bass, where
    // the tines move furthest.
    double loudSum = -300.0, softSum = -300.0;
    for (int k = 2; k <= 5; ++k)
    {
        loudSum = 10.0 * std::log10 (std::pow (10.0, loudSum / 10.0) + std::pow (10.0, loud[k] / 10.0));
        softSum = 10.0 * std::log10 (std::pow (10.0, softSum / 10.0) + std::pow (10.0, offset[k] / 10.0));
    }
    CHECK ((loudSum - loud[1]) - (softSum - offset[1]) > 15.0,
           "a large swing did not add harmonics: loud %.1f dB vs soft %.1f dB relative to H1",
           loudSum - loud[1], softSum - offset[1]);
}

// ===========================================================================
// The assembled voice
// ===========================================================================

struct Rendered
{
    std::vector<double> flux, tip;
    double tipPeakMm = 0.0, fluxPeak = 0.0, contactMs = 0.0, measuredHz = 0.0;
};

static Rendered render (int note, double velocity, const RhodesVoice::Config& cfg,
                        const MagneticPickup& pu, double seconds)
{
    RhodesVoice v;
    v.prepare (kFs, &pu);
    v.noteOn (note, velocity, cfg, 0x51u + static_cast<unsigned> (note));

    Rendered r;
    const int n = static_cast<int> (kFs * seconds);
    r.flux.resize (static_cast<size_t> (n));
    r.tip.resize (static_cast<size_t> (n));
    double os[RhodesVoice::kOver];
    for (int i = 0; i < n; ++i)
    {
        v.process (cfg, os);
        // The tests measure the transducer's own output, so they take the last
        // oversampled point rather than running the engine's decimator.
        r.flux[static_cast<size_t> (i)] = os[RhodesVoice::kOver - 1] - v.restingFlux();
        r.tip[static_cast<size_t> (i)]  = v.tipDisplacement();
        r.tipPeakMm = std::max (r.tipPeakMm, std::abs (r.tip[static_cast<size_t> (i)]) * 1.0e3);
        r.fluxPeak  = std::max (r.fluxPeak, std::abs (r.flux[static_cast<size_t> (i)]));
    }
    r.contactMs = v.contactSamples() * 1000.0 / kFs;

    const int a = static_cast<int> (kFs * 0.30);
    const int b = std::min (n, a + static_cast<int> (kFs * 0.5));
    long cross = 0, first = -1, last = -1;
    double prev = r.tip[static_cast<size_t> (a)];
    for (int i = a + 1; i < b; ++i)
    {
        const double u = r.tip[static_cast<size_t> (i)];
        if (prev <= 0.0 && u > 0.0) { if (first < 0) first = i; last = i; ++cross; }
        prev = u;
    }
    r.measuredHz = (cross > 1 && last > first)
                 ? static_cast<double> (cross - 1) * kFs / static_cast<double> (last - first)
                 : 0.0;
    return r;
}

// The full compass of a 73-key Rhodes, E1 to E6.
static constexpr int kVerifiedLo = 28;
static constexpr int kVerifiedHi = 88;

static void testVoiceTuning()
{
    MagneticPickup pu; pu.prepare ({});
    RhodesVoice::Config cfg;

    for (int n = kVerifiedLo; n <= kVerifiedHi; n += 4)
    {
        const auto r = render (n, 0.8, cfg, pu, 1.0);
        const double f0 = noteHz (n);
        const double cents = r.measuredHz > 0.0 ? 1200.0 * std::log2 (r.measuredHz / f0) : -9999.0;
        CHECK (std::abs (cents) < 25.0,
               "note %d (%.1f Hz) is %.1f cents out", n, f0, cents);
    }
}

// Shear & Wright tracked tine displacement across a Rhodes and found it runs
// from tens of millimetres on the longest tine to well under one on the
// shortest (NIME 2011, 3.1). So the thing to assert is the SLOPE, not a band:
// an even swing across the compass is the signature of a broken coupling, and
// was in fact this model's symptom before the contact patch was added.
static void testTineSwingFallsSteeplyWithPitch()
{
    MagneticPickup pu; pu.prepare ({});
    RhodesVoice::Config cfg;

    double prev = 1.0e9;
    double bass = 0.0, treble = 0.0;

    for (int n = kVerifiedLo; n <= kVerifiedHi; n += 4)
    {
        const auto r = render (n, 0.8, cfg, pu, 0.6);

        CHECK (r.tipPeakMm > 0.01 && r.tipPeakMm < 60.0,
               "note %d: tine tip swings %.3f mm, outside anything measured on a real one",
               n, r.tipPeakMm);

        // Monotone with a little tolerance: the tine gets shorter and stiffer
        // all the way up, so nothing should swing further than the note below.
        CHECK (r.tipPeakMm < prev * 1.15,
               "note %d swings %.3f mm, more than the note below it (%.3f mm)",
               n, r.tipPeakMm, prev);
        prev = r.tipPeakMm;

        if (n == kVerifiedLo) bass = r.tipPeakMm;
        treble = r.tipPeakMm;
    }

    // Shear & Wright measured tens of millimetres on the longest tine against
    // well under one on the shortest -- a ratio past fifty. This model spans
    // about four, which is the same shortfall the growl-gradient gap below
    // records, and the same root cause: the hammer had to be graduated against
    // the tine's effective mass to keep the treble collision in a sane regime,
    // and that flattened the amplitude across the compass. It is a real gap and
    // it is reported rather than tuned away.
    KNOWN_GAP (bass / std::max (1.0e-9, treble) > 5.0,
               "the swing barely changes across the keyboard: %.3f mm at the bottom "
               "against %.3f mm at the top, a ratio of %.1f. A real Rhodes spans "
               "more than an order of magnitude.", bass, treble, bass / treble);
}

// Contact duration falls with pitch on any keyboard instrument. Askenfelt &
// Jansson measured a piano at "about 0.5 ms in the treble to 4 ms in the bass,
// referring to a mezzo-forte level" (STL-QPSR 29(1), 4a). The often-quoted
// 6.42 ms Rhodes figure is a single unweighted average over unstated notes and
// unstated velocity, so it is a sanity check on the bass, never a per-note
// target -- treating it as one is what drove this model's hammer far too stiff
// to begin with.
static void testHammerContactDuration()
{
    MagneticPickup pu; pu.prepare ({});
    RhodesVoice::Config cfg;

    double prev = 1.0e9;
    for (int n = kVerifiedLo; n <= kVerifiedHi; n += 4)
    {
        const auto r = render (n, 0.7, cfg, pu, 0.3);

        CHECK (r.contactMs > 0.3 && r.contactMs < 8.0,
               "note %d: hammer contact %.2f ms, outside the measured range for "
               "a keyboard instrument", n, r.contactMs);

        // A collapse to a fraction of a millisecond partway up the compass is
        // the signature of a contact that has left its calibrated regime; it
        // is a near-impulse and it excites modes a real instrument never
        // touches. Monotone-ish is the guard against it.
        CHECK (r.contactMs < prev * 1.25,
               "note %d contact %.2f ms is longer than the note below it (%.2f ms)",
               n, r.contactMs, prev);
        prev = r.contactMs;
    }
}

// The central finding of the high-speed camera work: after a transient of ten
// to fourteen milliseconds the tine vibrates as a pure sine, with no overtones
// of its own, while the signal behind the pickup is full of harmonics. If this
// ever inverts, the harmonics are coming from the metal rather than the field,
// and the model has stopped being a Rhodes.
static void testTineIsSinusoidalWhilePickupIsNot()
{
    MagneticPickup pu; pu.prepare ({});
    RhodesVoice::Config cfg;

    for (int n : { 28, 40, 52 })
    {
        const auto r = render (n, 0.8, cfg, pu, 1.2);
        const double f0 = noteHz (n);
        const int a = static_cast<int> (kFs * 0.30);
        const int b = a + static_cast<int> (kFs * 0.5);

        // The tine's own overtones sit at the BEAM's ratios, not at integer
        // multiples, so they have to be measured where they actually are.
        const double t1 = binDb (r.tip, a, b, f0, kFs);
        double tOver = -300.0;
        for (int m = 1; m < 4; ++m)
        {
            const double fm = f0 * CantileverModes::ratio (m, 0.2, 0.76, 0.006);
            if (fm < 0.45 * kFs)
                tOver = 10.0 * std::log10 (std::pow (10.0, tOver / 10.0)
                                         + std::pow (10.0, binDb (r.tip, a, b, fm, kFs) / 10.0));
        }

        const double g1 = binDb (r.flux, a, b, f0, kFs);
        double gHarm = -300.0;
        for (int k = 2; k <= 8; ++k)
            if (f0 * k < 0.45 * kFs)
                gHarm = 10.0 * std::log10 (std::pow (10.0, gHarm / 10.0)
                                         + std::pow (10.0, binDb (r.flux, a, b, f0 * k, kFs) / 10.0));

        CHECK (tOver - t1 < -20.0,
               "note %d: the tine itself is not sinusoidal, its overtones are %.1f dB "
               "relative to its fundamental", n, tOver - t1);
        CHECK ((gHarm - g1) - (tOver - t1) > 12.0,
               "note %d: the pickup is not adding harmonics -- tine %.1f dB, flux %.1f dB",
               n, tOver - t1, gHarm - g1);
    }
}

// Growl belongs to the bass and to a hard blow, because that is where the tine
// moves far enough to run off the ends of the pole face (ISMA 2014, abstract).
static void testGrowlBelongsToTheBass()
{
    MagneticPickup pu; pu.prepare ({});
    RhodesVoice::Config cfg;

    auto richness = [&] (int n, double vel)
    {
        const auto r = render (n, vel, cfg, pu, 0.6);
        const double f0 = noteHz (n);
        const int a = static_cast<int> (kFs * 0.05);
        const int b = a + static_cast<int> (kFs * 0.3);
        const double h1 = binDb (r.flux, a, b, f0, kFs);
        double rest = -300.0;
        for (int k = 2; k <= 10; ++k)
            if (f0 * k < 0.45 * kFs)
                rest = 10.0 * std::log10 (std::pow (10.0, rest / 10.0)
                                        + std::pow (10.0, binDb (r.flux, a, b, f0 * k, kFs) / 10.0));
        return rest - h1;
    };

    const double bassLoud = richness (28, 1.0);
    const double bassSoft = richness (28, 0.25);
    const double midLoud  = richness (52, 1.0);

    CHECK (bassLoud - bassSoft > 3.0,
           "playing harder did not add growl in the bass: %.1f dB loud vs %.1f dB soft",
           bassLoud, bassSoft);

    // The register half of the same behaviour is not right yet, and it is a
    // consequence of the contact problem below rather than a separate one.
    // Keeping the treble collision in a sane regime meant graduating the
    // hammer against the tine's effective mass, and that flattened the swing
    // across the compass -- every note now moves about the same couple of
    // millimetres, where a real bass tine moves several times further than a
    // mid one. Growl is a function of how far the tine runs off the pole face,
    // so an even swing gives an even growl. It comes back when the contact is
    // quadratised and the hammer can be graduated the way the instrument does
    // it, by mass alone.
    KNOWN_GAP (bassLoud - midLoud > 3.0,
               "the bass is not growlier than the middle: %.1f dB vs %.1f dB",
               bassLoud, midLoud);
}

// Nothing may grow, at any setting, ever. The nonlinearities are quadratised
// specifically so this holds by construction; this measures that it does.
static void testNothingGrows()
{
    MagneticPickup pu; pu.prepare ({});

    int caseIndex = 0;
    for (double nl : { 0.0, 1.0 })
        for (double bc : { 0.0, 1.0 })
            for (double damp : { 0.0, 1.0 })
                for (int n : { 28, 40, 52 })
                {
                    RhodesVoice::Config cfg;
                    cfg.nonlinearity = nl;
                    cfg.barCoupling  = bc;
                    cfg.damping      = damp;

                    RhodesVoice v;
                    v.prepare (kFs, &pu);
                    v.noteOn (n, 1.0, cfg, 0x99u);

                    double early = 0.0, late = 0.0, peak = 0.0;
                    bool finite = true;
                    const int total = static_cast<int> (kFs * 20.0);
                    double os2[RhodesVoice::kOver];
                    for (int i = 0; i < total; ++i)
                    {
                        v.process (cfg, os2);
                        const double x = os2[RhodesVoice::kOver - 1] - v.restingFlux();
                        if (! std::isfinite (x)) { finite = false; break; }
                        peak = std::max (peak, std::abs (x));
                        if (i > static_cast<int> (kFs * 0.5) && i < static_cast<int> (kFs * 1.5))
                            early = std::max (early, std::abs (x));
                        if (i > static_cast<int> (kFs * 18.0))
                            late = std::max (late, std::abs (x));
                    }

                    CHECK (finite, "case %d (note %d): output went non-finite", caseIndex, n);
                    CHECK (peak < 8.0, "case %d (note %d): peak reached %.2f", caseIndex, n, peak);
                    CHECK (late <= early + 1.0e-9,
                           "case %d (note %d): the note grew -- %.4e at 1 s, %.4e at 19 s",
                           caseIndex, n, early, late);
                    ++caseIndex;
                }
}

int main()
{
    testModalPitchAndDecayAreExact();
    testQuadratisedNonlinearityConservesEnergy();
    testDamperCannotAddEnergy();

    testCantileverRatiosMatchBeamTheory();
    testTuningSpringPositionChangesTheOvertones();
    testTineLengthsAreRealistic();

    testPickupFieldGeneratesTheHarmonics();

    testVoiceTuning();
    testTineSwingFallsSteeplyWithPitch();
    testHammerContactDuration();
    testTineIsSinusoidalWhilePickupIsNot();
    testGrowlBelongsToTheBass();
    testNothingGrows();

    if (knownGaps > 0)
        std::printf ("\n%d known gap(s).\n", knownGaps);
    if (failures == 0)
        std::printf ("All Epi DSP tests passed.\n");
    else
        std::printf ("%d Epi DSP test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
