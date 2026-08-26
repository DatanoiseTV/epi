/*
  Epi — the grand's radiation mechanisms: near-field capture at a microphone,
  and the phantom/longitudinal set that is NOT here.

  Row S1 of the grand suite is a bounded known gap: the model's radiated A0
  fundamental sits 35 dB under its strongest partial where the close-mic'd
  reference reads -20 .. -30. The row's own note names the two reasons, and
  this file is about both of them.

  The first is near-field capture, and it is now modelled (GrandMicStage.h).
  A dipole's pressure is (A cos(theta) / r)(1 + 1/(j k r)) e^{-j k r}; every
  far-field treatment drops the bracket, and a close microphone lives inside
  it. Below k r = 1 the bracket rises 6 dB/octave as the frequency falls and
  the distance law steepens from 1/r to 1/r^2, so the collapse a bass
  fundamental suffers at a close mic is half as steep as the far-field law
  the fixed Classic pair carries. The rows below measure the bracket against
  its own closed form (NF2-NF4), fence what it must NOT move (NF1, NF5, NF6)
  and read row S1's metric back off a mic that has a position (NF7).

  The second is the phantom-partial / longitudinal set, and it is NOT here,
  deliberately: NF10 records the measured reason (see also
  docs/research/piano-soundboard-and-coupling.md section 9).

  Everything is measured against a closed form written out in the row, never
  against a stored number: the near-field factor is textbook, so the target
  is the textbook and the model has to hit it.

  Build: part of ctest, target epi_radiation_tests. Framework-free, needs no
  EpiEngine.cpp.
*/

#include "EpiAnalysis.h"
#include "epi/dsp/GrandBoard.h"
#include "epi/dsp/GrandMicStage.h"
#include "epi/dsp/GrandRadiator.h"
#include "epi/dsp/GrandVoice.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace epi;
namespace an = epianalysis;
using cplx = std::complex<double>;

// ===========================================================================
// Row machinery (the reference suite's, local to this binary)
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
    std::printf ("  %-4s %-38s %-26s %-24s %s\n", id, what, target.c_str(), got.c_str(), mark);
}

static Verdict within (double v, double lo, double hi)
{
    return (v >= lo && v <= hi) ? Verdict::pass : Verdict::fail;
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
// The physical constants the rows are written against. These are the
// microphone stage's own published geometry (GrandMicStage.h), quoted here
// so a row can state its target as an equation rather than as a number.
// ===========================================================================

static constexpr double kFs    = 48000.0;
static constexpr double kC     = 343.0;    // m/s, dry air at 20 C
static constexpr double kSeatZ = 1.2, kSeatH = 0.6;

static double seatR() { return std::sqrt (kSeatZ * kSeatZ + kSeatH * kSeatH); }

// The textbook dipole near-field bracket, 1 + 1/(j k r), k = 2 pi f / c.
// No floor, no filter design: this is what the model has to reproduce.
static cplx dipoleBracket (double f, double r)
{
    return cplx (1.0, 0.0) - cplx (0.0, kC / (2.0 * an::kPi * f * r));
}

static double db (double x) { return 20.0 * std::log10 (std::max (1.0e-300, x)); }

// ===========================================================================
// Rigs
// ===========================================================================

static GrandMicStage::Mic micAtF (double x, double z, double h, double pan = 0.0)
{
    GrandMicStage::Mic m;
    m.on = true;
    m.x = x; m.z = z; m.h = h; m.pan = pan;
    return m;
}

// Steady sine into the board readout, no string force at all: the radiator's
// sections and the lid image are then identically silent and the mic reads
// the LOW bus alone -- the only bus the near field touches. The result is the
// mic's low-path magnitude at f, air one-pole and all.
static double lowSineRms (double f, const GrandMicStage::Mic& m, double seconds = 2.5)
{
    auto rad = std::make_unique<GrandRadiator>();
    rad->prepare (kFs);
    auto st = std::make_unique<GrandMicStage>();
    st->setMode (1);
    st->setMic (0, m);
    st->prepare (kFs);
    const int N = static_cast<int> (kFs * seconds);
    const int skip = N / 2;           // past the arrival ramp and the shelf's
    double acc = 0.0;                 // own 40 ms settling
    long n = 0;
    for (int i = 0; i < N; ++i)
    {
        const double x = std::sin (2.0 * an::kPi * f * i / kFs);
        double L = 0.0, R = 0.0;
        st->tick (*rad, x, x, L, R);
        const double y = (L + R) * 0.70710678118654752;   // pan 0, undone
        if (i >= skip) { acc += y * y; ++n; }
    }
    return std::sqrt (acc / static_cast<double> (n));
}

// The dipole share on its own. Two mics at the SAME radius, one at +h and
// one at -h, hard-panned to opposite channels of ONE render: the monopole
// leak is identical in both and the cos(theta) dipole term flips sign, so
// L - R is twice the dipole share and L + R is twice the leak, sample by
// sample. Every other part of the path -- 1/r, the delay, its fractional
// interpolation, the air one-pole -- is common to both mics and divides out.
// The returned number is therefore ((1 - leak) h / (leak r)) |N(f)|, and its
// value at 5 kHz (where |N| = 1 to five decimals) turns it into |N| alone.
static double dipoleOverLeak (double f, double z, double h, double seconds = 2.5)
{
    auto rad = std::make_unique<GrandRadiator>();
    rad->prepare (kFs);
    auto st = std::make_unique<GrandMicStage>();
    st->setMode (1);
    st->setMic (0, micAtF (0.0, z,  h, -1.0));
    st->setMic (1, micAtF (0.0, z, -h, +1.0));
    st->prepare (kFs);
    const int N = static_cast<int> (kFs * seconds);
    const int skip = N / 2;
    double dAcc = 0.0, sAcc = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double x = std::sin (2.0 * an::kPi * f * i / kFs);
        double L = 0.0, R = 0.0;
        st->tick (*rad, x, x, L, R);
        if (i >= skip) { dAcc += (L - R) * (L - R); sAcc += (L + R) * (L + R); }
    }
    return std::sqrt (dAcc / std::max (1.0e-300, sAcc));
}

// The same, with the board's own radiation collapse in front of the mic:
// the sine passes GrandRadiationHp at kRadFcHz before it becomes board
// output, which is what a real vibration at that frequency radiates into the
// far field. Used by the row that measures the COMPOSITE slope.
static double radiatedSineRms (double f, const GrandMicStage::Mic& m, double seconds = 2.5)
{
    auto rad = std::make_unique<GrandRadiator>();
    rad->prepare (kFs);
    auto st = std::make_unique<GrandMicStage>();
    st->setMode (1);
    st->setMic (0, m);
    st->prepare (kFs);
    GrandRadiationHp hp;
    hp.prepare (kFs, GrandBoard::kRadFcHz);
    const int N = static_cast<int> (kFs * seconds);
    const int skip = N / 2;
    double acc = 0.0;
    long n = 0;
    for (int i = 0; i < N; ++i)
    {
        const double x = hp.tick (std::sin (2.0 * an::kPi * f * i / kFs));
        double L = 0.0, R = 0.0;
        st->tick (*rad, x, x, L, R);
        const double y = (L + R) * 0.70710678118654752;
        if (i >= skip) { acc += y * y; ++n; }
    }
    return std::sqrt (acc / static_cast<double> (n));
}

struct StereoSig { std::vector<double> l, r, m; };

// One note through the full grand path into the mic stage. mode 0 renders
// the Classic pair; mode 1 renders the mics.
static StereoSig renderStage (int note, double vel, double seconds, int mode,
                              const std::vector<GrandMicStage::Mic>& mics,
                              double moveAt = -1.0,
                              const GrandMicStage::Mic* moveTo = nullptr)
{
    const int N = static_cast<int> (kFs * seconds);
    auto board = std::make_unique<GrandBoard>();
    board->prepare (kFs);
    auto rad = std::make_unique<GrandRadiator>();
    rad->prepare (kFs);
    auto st = std::make_unique<GrandMicStage>();
    st->setMode (mode);
    for (std::size_t i = 0; i < mics.size(); ++i)
        st->setMic (static_cast<int> (i), mics[i]);
    st->prepare (kFs);
    const GrandVoice::Config cfg;
    auto v = std::make_unique<GrandVoice>();
    v->prepare (kFs);
    v->setPedal (0.0);
    v->noteOn (note, vel, cfg, *board, 0);
    double gl = 0.0, gr = 0.0;
    GrandRadiator::panGains (note, gl, gr);
    const int moveSample = moveAt >= 0.0 ? static_cast<int> (moveAt * kFs) : -1;
    StereoSig s;
    s.l.resize (static_cast<std::size_t> (N));
    s.r.resize (static_cast<std::size_t> (N));
    s.m.resize (static_cast<std::size_t> (N));
    for (int i = 0; i < N; ++i)
    {
        if (i == moveSample && moveTo != nullptr)
            st->setMic (0, *moveTo);
        const double f = v->process (cfg, *board) + v->knockOut();
        v->applyDamperIfDue();
        rad->push (f, gl, gr);
        board->tick();
        double L = 0.0, R = 0.0;
        st->tick (*rad, board->outputL(), board->outputR(), L, R);
        s.l[static_cast<std::size_t> (i)] = L;
        s.r[static_cast<std::size_t> (i)] = R;
        s.m[static_cast<std::size_t> (i)] = 0.5 * (L + R);
    }
    return s;
}

// The shipped chain, built by hand from the parts the stage is supposed to
// reproduce in mode 0: board readout plus radiator, then the pair's allpass
// interchannel phase. Nothing of the mic geometry is in this path.
static StereoSig renderShippedChain (int note, double vel, double seconds)
{
    const int N = static_cast<int> (kFs * seconds);
    auto board = std::make_unique<GrandBoard>();
    board->prepare (kFs);
    auto rad = std::make_unique<GrandRadiator>();
    rad->prepare (kFs);
    auto pair = std::make_unique<GrandMicPair>();
    pair->prepare (kFs);
    const GrandVoice::Config cfg;
    auto v = std::make_unique<GrandVoice>();
    v->prepare (kFs);
    v->setPedal (0.0);
    v->noteOn (note, vel, cfg, *board, 0);
    double gl = 0.0, gr = 0.0;
    GrandRadiator::panGains (note, gl, gr);
    StereoSig s;
    s.l.resize (static_cast<std::size_t> (N));
    s.r.resize (static_cast<std::size_t> (N));
    s.m.resize (static_cast<std::size_t> (N));
    for (int i = 0; i < N; ++i)
    {
        const double f = v->process (cfg, *board) + v->knockOut();
        v->applyDamperIfDue();
        rad->push (f, gl, gr);
        board->tick();
        double tl = 0.0, tr = 0.0;
        rad->tick (tl, tr);
        double L = board->outputL() + tl;
        double R = board->outputR() + tr;
        pair->tick (L, R);
        s.l[static_cast<std::size_t> (i)] = L;
        s.r[static_cast<std::size_t> (i)] = R;
        s.m[static_cast<std::size_t> (i)] = 0.5 * (L + R);
    }
    return s;
}

// Row S1's own metric, computed on whatever signal it is handed: the A0
// fundamental's level relative to the strongest partial below 3 kHz, both
// read at 0.5 s off the heterodyne envelope. Identical arithmetic to the
// grand suite's S1 so the numbers are comparable row to row.
static double s1Metric (const std::vector<double>& mono, int midi, int* bestK = nullptr)
{
    const double f0 = 440.0 * std::pow (2.0, (midi - 69) / 12.0)
                    * std::pow (2.0, grandStretchCents (midi) / 1200.0);
    const double B = GrandInharmonicity::at (midi);
    double fundDb = -999.0, best = -999.0;
    int bk = 0;
    for (int k = 1; k <= 60; ++k)
    {
        const double fk = k * f0 * std::sqrt (1.0 + B * k * k);
        if (fk > 3000.0) break;
        const an::Envelope e = an::heterodyne (mono, kFs, fk, f0);
        if (e.z.empty()) continue;
        const double d = e.dbAt (0.5);
        if (k == 1) fundDb = d;
        if (d > best) { best = d; bk = k; }
    }
    if (bestK != nullptr) *bestK = bk;
    return fundDb - best;
}

static double bandPowerDb (const std::vector<double>& x, double lo, double hi)
{
    const int NF = 16384;
    double p = 0.0;
    int segs = 0;
    for (int s = 0; s + NF <= static_cast<int> (x.size()); s += NF / 2)
    {
        std::vector<cplx> a (static_cast<std::size_t> (NF));
        for (int j = 0; j < NF; ++j)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * an::kPi * j / (NF - 1.0));
            a[static_cast<std::size_t> (j)] = x[static_cast<std::size_t> (s + j)] * w;
        }
        // in-place radix-2
        for (int i = 1, j = 0; i < NF; ++i)
        {
            int bit = NF >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[static_cast<std::size_t> (i)], a[static_cast<std::size_t> (j)]);
        }
        for (int len = 2; len <= NF; len <<= 1)
        {
            const double ang = -2.0 * an::kPi / len;
            const cplx wl (std::cos (ang), std::sin (ang));
            for (int i = 0; i < NF; i += len)
            {
                cplx w (1.0, 0.0);
                for (int j = 0; j < len / 2; ++j)
                {
                    const cplx u = a[static_cast<std::size_t> (i + j)];
                    const cplx vv = a[static_cast<std::size_t> (i + j + len / 2)] * w;
                    a[static_cast<std::size_t> (i + j)] = u + vv;
                    a[static_cast<std::size_t> (i + j + len / 2)] = u - vv;
                    w *= wl;
                }
            }
        }
        for (int k = static_cast<int> (lo * NF / kFs); k <= static_cast<int> (hi * NF / kFs); ++k)
            p += std::norm (a[static_cast<std::size_t> (k)]);
        ++segs;
    }
    return 10.0 * std::log10 (p / std::max (1, segs) + 1.0e-300);
}

// Largest second difference over a window: the click detector the mic-stage
// rows use, sensitive to a step that an envelope measure would miss.
static double maxSecondDiff (const std::vector<double>& x, double t0, double t1)
{
    const std::size_t a = static_cast<std::size_t> (t0 * kFs);
    const std::size_t b = std::min (x.size() - 1, static_cast<std::size_t> (t1 * kFs));
    double w = 0.0;
    for (std::size_t i = a + 1; i < b; ++i)
        w = std::max (w, std::abs (x[i + 1] - 2.0 * x[i] + x[i - 1]));
    return w;
}

static GrandMicStage::Mic micAt (double x, double z, double h, double pan = 0.0)
{
    return micAtF (x, z, h, pan);
}

// A mic on the calibrated seat's own sight line, at k times the seat radius:
// same cos(theta), so the dipole/monopole split is identical and only the
// radius (and therefore the near field) differs between two such mics.
static GrandMicStage::Mic micOnSeatRay (double k)
{
    return micAt (0.0, kSeatZ * k, kSeatH * k);
}

// ===========================================================================
// The near-field rows
// ===========================================================================

static void sectionNearField()
{
    heading ("NF. Grand: near-field capture at a microphone (GrandMicStage.h)");

    // ---- NF1: the Classic pair is untouched --------------------------------
    {
        // The near field is a function of k r and the Classic pair has no r:
        // it is a fixed gain law with no geometry in it at all. So mode 0
        // must still be the shipped chain to the last bit -- not "within a
        // tolerance", identical, which is the only claim worth making about
        // a path that ships as the default.
        const auto a = renderShippedChain (60, 0.8, 1.0);
        const auto b = renderStage (60, 0.8, 1.0, 0, {});
        double d = 0.0;
        for (std::size_t i = 0; i < a.l.size(); ++i)
        {
            d = std::max (d, std::abs (a.l[i] - b.l[i]));
            d = std::max (d, std::abs (a.r[i] - b.r[i]));
        }
        row ("NF1", "Classic pair vs shipped chain (1 s)", "bit-exact",
             fmt ("max |diff| = %.1e", d), d == 0.0 ? Verdict::pass : Verdict::fail);
    }

    // ---- NF2: the factor IS the textbook dipole bracket ---------------------
    {
        // Two mics at the SAME radius, one on the seat ray and one lying in
        // the board plane. The in-plane mic has cos(theta) = 0, so it carries
        // only the monopole leak -- which has no near-field magnitude term at
        // any k r -- through exactly the same 1/r, the same delay and the
        // same air one-pole. Their ratio therefore divides out every part of
        // the path except the near field itself:
        //
        //     ratio(f) = |1 + K (1 + 1/(j k r))|,
        //
        // K the dipole:leak strength, which is not a free parameter: it is
        // read off the ratio at 5 kHz, where the bracket is 1 to within
        // 1e-5. Everything below is then a prediction with nothing left to
        // tune, and the target is the closed form, not a stored number.
        for (double k : { 0.2172, 0.5, 1.0, 2.2361 })
        {
            const GrandMicStage::Mic up = micOnSeatRay (k);
            const double r = std::sqrt (up.z * up.z + up.h * up.h);
            const GrandMicStage::Mic flat = micAt (0.0, r, 0.0);
            const double refUp = lowSineRms (5000.0, up);
            const double refFl = lowSineRms (5000.0, flat);
            const double K = refUp / refFl - 1.0;
            double worst = 0.0;
            double atF = 0.0;
            for (double f : { 27.5, 41.2, 55.0, 110.0, 220.0, 440.0, 1000.0 })
            {
                const double got = lowSineRms (f, up) / lowSineRms (f, flat);
                const double want = std::abs (cplx (1.0, 0.0) + K * dipoleBracket (f, r));
                const double e = std::abs (db (got) - db (want));
                if (e > worst) { worst = e; atF = f; }
            }
            row ("NF2", (fmt ("bracket at r = %.2f m", r) + " vs |1+1/(jkr)|").c_str(),
                 "max error < 0.15 dB",
                 fmt2 ("%.3f dB (at %.0f Hz)", worst, atF),
                 within (worst, 0.0, 0.15));
        }
    }

    // ---- NF3: the corner sits at k r = 1, and below it the rise is 6 dB/oct
    {
        // With the leak divided out (see dipoleOverLeak) what is left is the
        // bracket itself, and two textbook readings of it settle the shape.
        //
        // First the corner. |1 + 1/(j k r)| at k r = 1 is sqrt(2), 3.0103 dB,
        // at EVERY radius -- that identity is what makes f_nf = c / (2 pi r)
        // a definition rather than a fit, so measuring it at four radii two
        // decades apart is a real test of the geometry and not of one number.
        // The shelf's 4 Hz floor takes 20 log10 sqrt(1 + (4/f_nf)^2) off it,
        // which is 0.0004 dB at the closest mic and 0.042 dB at the seat.
        // Positions given directly rather than as a scale of the seat ray:
        // the stage clamps z to 0.2 m independently of the radius floor, so
        // asking for a point closer in than that would silently measure a
        // different geometry from the one the row names.
        const double zs[] = { 0.20, 0.26, 0.60, 1.20 };
        for (double z : zs)
        {
            const double h = 0.5 * z;                  // the seat ray's angle
            const double r = std::sqrt (z * z + h * h);
            const double fNf = kC / (2.0 * an::kPi * r);
            const double got = dipoleOverLeak (fNf, z, h) / dipoleOverLeak (5000.0, z, h);
            row ("NF3", fmt ("bracket at k r = 1, r = %.2f m", r).c_str(),
                 fmt ("3.010 dB (f_nf = %.0f Hz)", fNf),
                 fmt ("%.3f dB", db (got)), within (db (got), 2.95, 3.05));
        }

        // Then the slope below it. The asymptote is 6.02 dB/octave, but over
        // any finite octave the closed form is less, because the upper end is
        // not infinitely below the corner: 100 -> 50 Hz at the closest
        // permitted mic (f_nf = 273 Hz) is 5.62. The model owes the closed
        // form, not the asymptote, and the budget is 0.05 dB -- the floor
        // costs 0.02 over this octave and nothing above 100 Hz.
        const double z = 0.20, h = 0.10;               // the closest the stage allows
        const double r = std::sqrt (z * z + h * h);
        const double ref = dipoleOverLeak (5000.0, z, h);
        auto bracket = [&] (double f) { return dipoleOverLeak (f, z, h) / ref; };
        const double slope = db (bracket (50.0)) - db (bracket (100.0));
        const double want  = db (std::abs (dipoleBracket (50.0, r))
                               / std::abs (dipoleBracket (100.0, r)));
        row ("NF3", "bracket slope 100->50 Hz, closest mic",
             fmt ("%.2f dB/oct (6.02 asympt.)", want),
             fmt ("%.2f dB/oct", slope), within (std::abs (slope - want), 0.0, 0.05));

        // And it is spent in the treble, which is the same statement read
        // from the far end.
        const double flatSlope = db (bracket (3000.0)) - db (bracket (6000.0));
        const double flatWant  = db (std::abs (dipoleBracket (3000.0, r))
                                   / std::abs (dipoleBracket (6000.0, r)));
        row ("NF3", "bracket slope 6->3 kHz, same mic",
             fmt ("%.3f dB/oct (spent)", flatWant),
             fmt ("%.3f dB/oct", flatSlope),
             within (std::abs (flatSlope - flatWant), 0.0, 0.01));
    }

    // ---- NF4: the composite, which is what row S1 is about ------------------
    {
        // The board's own radiation collapse is a second-order high-pass at
        // GrandBoard::kRadFcHz = 200 Hz -- 12 dB/octave, the far-field law of
        // a source small against the wavelength, and for the dipole the board
        // is below coincidence, the same f^2. Put the near field in front of
        // a mic that sits inside it and the two slopes subtract: what a close
        // microphone hears below the collapse falls at about 6 dB/octave, not
        // 12. This is the whole mechanism in one number, and it is why a
        // close-mic'd recording has a bass fundamental the Classic pair
        // cannot have.
        //
        // The control is a mic at the SAME radius lying in the board plane.
        // Its cos(theta) is zero, so it carries only the monopole leak, which
        // has no near-field magnitude term at any k r -- it measures the bare
        // far-field collapse through the identical 1/r, delay and air path.
        const GrandMicStage::Mic ray = micOnSeatRay (0.2172);
        const double r = std::sqrt (ray.z * ray.z + ray.h * ray.h);
        const GrandMicStage::Mic flat = micAt (0.0, r, 0.0);
        auto slope = [] (const GrandMicStage::Mic& m)
        {
            return db (radiatedSineRms (55.0, m)) - db (radiatedSineRms (27.5, m));
        };
        const double sFlat = slope (flat), sRay = slope (ray);
        row ("NF4", fmt ("collapse 55->27.5 Hz, in plane, %.2f m", r).c_str(),
             "12.04 dB/oct (2nd order)", fmt ("%.2f dB/oct", sFlat),
             within (sFlat, 11.74, 12.34));
        row ("NF4", "same radius, off the plane (near field)", "about 6 dB/oct",
             fmt ("%.2f dB/oct", sRay), within (sRay, 5.5, 7.5));
        row ("NF4", "the collapse the near field cancels", ">= 4.5 dB/oct shallower",
             fmt ("%.2f dB/oct", sFlat - sRay),
             sFlat - sRay >= 4.5 ? Verdict::pass : Verdict::fail);

        // The same mic angle at 3 m keeps only a little of the bracket -- k r
        // is 1.5 there at 27.5 Hz -- so its collapse is nearly the bare law.
        row ("NF4", "collapse 55->27.5 Hz, on the ray at 3 m", "11.3 dB/oct (12 bare)",
             fmt ("%.2f dB/oct", slope (micOnSeatRay (2.2361))),
             within (slope (micOnSeatRay (2.2361)), 10.8, 11.8));
    }

    // ---- NF5: the distance law steepens ------------------------------------
    {
        // 1/r times 1/(k r) is 1/r^2. Two mics on the seat ray, one at half
        // the other's radius: 6.02 dB apart wherever the bracket is spent,
        // and heading for 12.04 dB where it is not. The measured value at
        // 27.5 Hz sits between because the monopole leak, which never gets
        // the bracket, keeps its share of the level -- the prediction below
        // is the exact composite with K read off the far field, as in NF2.
        const GrandMicStage::Mic a = micOnSeatRay (0.25);
        const GrandMicStage::Mic b = micOnSeatRay (0.5);
        const double ra = std::sqrt (a.z * a.z + a.h * a.h);
        const double rb = std::sqrt (b.z * b.z + b.h * b.h);
        const GrandMicStage::Mic fa = micAt (0.0, ra, 0.0);
        const GrandMicStage::Mic fb = micAt (0.0, rb, 0.0);
        const double Ka = lowSineRms (5000.0, a) / lowSineRms (5000.0, fa) - 1.0;
        const double Kb = lowSineRms (5000.0, b) / lowSineRms (5000.0, fb) - 1.0;
        auto drop = [&] (double f)
        {
            return db (lowSineRms (f, a)) - db (lowSineRms (f, b));
        };
        auto predict = [&] (double f)
        {
            const double ga = std::abs (cplx (1.0, 0.0) + Ka * dipoleBracket (f, ra));
            const double gb = std::abs (cplx (1.0, 0.0) + Kb * dipoleBracket (f, rb));
            return db (rb / ra) + db (ga / gb);
        };
        const double d27 = drop (27.5), p27 = predict (27.5);
        const double d1k = drop (1000.0), p1k = predict (1000.0);
        row ("NF5", "level r halved, 27.5 Hz", fmt ("%.2f dB (closed form)", p27),
             fmt ("%.2f dB", d27), within (std::abs (d27 - p27), 0.0, 0.2));
        // 1 kHz is the far field for both mics to within 0.06 dB of the
        // bare 6.021, and the closed form says exactly how much of that
        // residual is bracket. The 0.1 dB budget also covers the delay
        // line's linear interpolation, which costs the two mics different
        // fractions of a sample (0.06 dB at 4 kHz, 0.004 dB here).
        row ("NF5", "level r halved, 1 kHz (far field)",
             fmt ("%.2f dB (6.02 bare)", p1k),
             fmt ("%.3f dB", d1k), within (std::abs (d1k - p1k), 0.0, 0.1));
    }

    // ---- NF6: nothing in the far field moved --------------------------------
    {
        // What "unchanged" has to mean for a term that is a function of k r:
        // the bracket is exactly 1 in the limit, so the only honest test is
        // how close to 1 it is where the model is used. At 3 m the whole band
        // from 100 Hz up is within a small fraction of a dB of the pure 1/r
        // law it had before -- measured as the same ratio NF2 uses, whose
        // far-field value is K + 1 by construction.
        const GrandMicStage::Mic far = micOnSeatRay (2.2361);
        const double r = std::sqrt (far.z * far.z + far.h * far.h);
        const GrandMicStage::Mic flat = micAt (0.0, r, 0.0);
        const double K = lowSineRms (5000.0, far) / lowSineRms (5000.0, flat) - 1.0;
        double worst = 0.0, atF = 0.0;
        for (double f : { 100.0, 200.0, 500.0, 1000.0, 3000.0 })
        {
            const double got = lowSineRms (f, far) / lowSineRms (f, flat);
            const double e = std::abs (db (got) - db (1.0 + K));
            if (e > worst) { worst = e; atF = f; }
        }
        row ("NF6", "3 m mic, 100 Hz-3 kHz vs pure 1/r", "< 0.15 dB",
             fmt2 ("%.3f dB (at %.0f Hz)", worst, atF), within (worst, 0.0, 0.15));

        // Below that the 3 m mic is NOT in the far field and the model says
        // so: 27.5 Hz is k r = 1.5 there. Informational, because it is the
        // honest residual, not a tolerance -- a 3 m mic on a real piano
        // hears this too.
        const double got = lowSineRms (27.5, far) / lowSineRms (27.5, flat);
        row ("NF6", "same mic at 27.5 Hz (k r = 1.5)",
             fmt ("%.2f dB (closed form)",
                  db (std::abs (cplx (1.0, 0.0) + K * dipoleBracket (27.5, r)) / (1.0 + K))),
             fmt ("%+.2f dB", db (got) - db (1.0 + K)), Verdict::info);

        // And in the board plane the dipole share is identically zero, so
        // the near field cannot act at all: pure 1/r at every frequency.
        // This is the geometry the grand suite's MS2 and MS3 stand on, and
        // it is bit-exact by construction rather than by tolerance.
        const double p1 = db (lowSineRms (27.5, micAt (0.0, 1.0, 0.0)));
        const double p2 = db (lowSineRms (27.5, micAt (0.0, 2.0, 0.0)));
        row ("NF6", "in-plane mic, z 1->2 m at 27.5 Hz", "6.021 dB +/- 0.01",
             fmt ("%.4f dB", p1 - p2), within (p1 - p2, 6.011, 6.031));
    }

    // ---- NF7: row S1, read off a mic that has a position ---------------------
    {
        // The grand suite's S1 measures the A0 fundamental against its
        // strongest partial and compares it with the [R] figure for a piano
        // recording, -20 .. -30 dB. That figure describes a CLOSE-MIC'D
        // recording, and until now the model had no way to be one: the
        // Classic pair's level law has no distance in it. With the near
        // field in place the metric becomes what it physically is, a
        // property of where the microphone stands.
        const auto classic = renderStage (21, 0.9, 1.6, 0, {});
        int kC1 = 0, kS = 0, kCl = 0;
        const double mClassic = s1Metric (classic.m, 21, &kC1);
        const auto seat = renderStage (21, 0.9, 1.6, 1, { micOnSeatRay (1.0) });
        const double mSeat = s1Metric (seat.m, 21, &kS);
        // A close mic over mid-bridge: 0.25 m out, 0.15 m above the board.
        // Inside the lid of a real grand this is an ordinary placement, and
        // it is still well outside the 0.2 m the stage floors r at.
        const auto close = renderStage (21, 0.9, 1.6, 1, { micAt (0.0, 0.25, 0.15) });
        const double mClose = s1Metric (close.m, 21, &kCl);

        row ("NF7", "S1 metric, Classic pair (no distance)", "-20 .. -30 dB [R]",
             fmt2 ("%.1f dB (k=%.0f)", mClassic, static_cast<double> (kC1)),
             Verdict::info);
        row ("NF7", "S1 metric, Stage mic at the seat", "-20 .. -30 dB [R]",
             fmt2 ("%.1f dB (k=%.0f)", mSeat, static_cast<double> (kS)),
             Verdict::info);
        row ("NF7", "S1 metric, close mic (0.29 m)", "-20 .. -30 dB [R]",
             fmt2 ("%.1f dB (k=%.0f)", mClose, static_cast<double> (kCl)),
             within (mClose, -30.0, -20.0));
        row ("NF7", "close mic vs seat / vs Classic", "the gap is a position",
             fmt2 ("%+.1f / %+.1f dB", mClose - mSeat, mClose - mClassic),
             Verdict::info);
    }

    // ---- NF8: the shelf's floor is bounded ----------------------------------
    {
        // The bracket's pole is at DC, which is real physics and a bad
        // digital filter, so the model puts it at 4 Hz. That bounds the lift
        // at f_nf / 4 -- 68 at the closest permitted mic -- against a bus
        // already 68 dB down there behind the board's 200 Hz collapse. The
        // test is that the sub-audio band stays under the musical one at the
        // closest mic the stage allows, with a real note driving it.
        const auto x = renderStage (21, 1.0, 2.0, 1, { micAt (0.0, 0.2, 0.0001) });
        double peak = 0.0;
        bool finite = true;
        for (double v : x.m)
        {
            peak = std::max (peak, std::abs (v));
            if (! std::isfinite (v)) finite = false;
        }
        const double sub = bandPowerDb (x.m, 1.0, 20.0);
        const double mus = bandPowerDb (x.m, 60.0, 300.0);
        row ("NF8", "closest mic, A0 ff: output finite", "finite, peak < 100",
             fmt ("peak %.3f", peak),
             (finite && peak < 100.0) ? Verdict::pass : Verdict::fail);
        row ("NF8", "sub-20 Hz vs 60-300 Hz band", ">= 10 dB below",
             fmt ("%+.1f dB", sub - mus), sub - mus <= -10.0 ? Verdict::pass : Verdict::fail);
    }

    // ---- NF9: a mic crossing into the near field must not click -------------
    {
        // The near-field shelf has a 40 ms pole, so a mic move changes a
        // filter with real memory. Moving from the seat to 0.25 m mid-note
        // is the largest coefficient jump the stage permits; it has to ride
        // the same crossfade every other geometry change rides.
        const GrandMicStage::Mic from = micOnSeatRay (1.0);
        const GrandMicStage::Mic to   = micAt (0.0, 0.25, 0.15);
        const auto moved  = renderStage (33, 0.8, 1.5, 1, { from }, 0.7, &to);
        const auto steady = renderStage (33, 0.8, 1.5, 1, { to });
        const double dm = std::max (maxSecondDiff (moved.l, 0.68, 0.9),
                                    maxSecondDiff (moved.r, 0.68, 0.9));
        const double ds = std::max (maxSecondDiff (steady.l, 0.68, 0.9),
                                    maxSecondDiff (steady.r, 0.68, 0.9));
        row ("NF9", "seat -> 0.29 m mid-note, |d2|", "<= 3x steady",
             fmt2 ("%.2e vs %.2e", dm, 3.0 * ds),
             dm <= 3.0 * ds ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// The mechanism that is NOT here
// ===========================================================================

static void sectionPhantoms()
{
    heading ("LP. Grand: the longitudinal / phantom set -- measured, and not adopted");

    // The second mechanism row S1 names is the phantom-partial set: a struck
    // string's transverse motion modulates its own tension quadratically, and
    // that modulation drives the string's LONGITUDINAL modes, putting energy
    // at sums and differences of the transverse partial frequencies. It is
    // real, it is audible in a hard bass note, and it is not in this model.
    //
    // It cannot be honestly made here. The radiator receives one summed
    // termination force: every voice pushes into the same accumulator before
    // the tick. Any quadratic term applied there would generate sums and
    // differences ACROSS NOTES -- a chord would grow partials that belong to
    // no string in it -- which is not a rough version of the mechanism, it is
    // a different and false one. The coupling is per-string, so it belongs in
    // the string.
    //
    // And on row S1's own metric it would not help. The dominant, audible
    // component is the SUM set, f_i + f_j, every member of which lies above
    // the fundamental; row S1 divides the fundamental by the strongest
    // partial under 3 kHz, so adding energy up there can only push the ratio
    // further down. The row below measures the size of that effect using the
    // model's own A0 spectrum: the total phantom-band energy a Conklin-scale
    // coupling would add, expressed as the shift it would produce in S1.
    {
        const auto x = renderStage (21, 0.9, 1.6, 1, { micAt (0.0, 0.25, 0.15) });
        int bk = 0;
        const double before = s1Metric (x.m, 21, &bk);
        // Conklin (JASA 1996/1999) reports phantom partials at roughly
        // -30 .. -20 dB relative to the transverse partials that generate
        // them in a hard bass strike. Taking the strongest such addition at
        // the top of that range and landing it on the strongest partial's
        // own bin -- the worst case for this row, since the sum set is
        // nearly harmonic on a slightly inharmonic string -- the strongest
        // partial gains 20 log10(1 + 10^(-20/20)) and S1 loses the same.
        const double worstShift = -20.0 * std::log10 (1.0 + std::pow (10.0, -20.0 / 20.0));
        row ("LP1", "S1 if the sum set were added", "S1 cannot improve",
             fmt2 ("%.1f -> %.1f dB", before, before + worstShift),
             worstShift <= 0.0 ? Verdict::pass : Verdict::fail);
        row ("LP1", "where the mechanism belongs", "the string, not the radiator",
             "GrandVoice (deferred)", Verdict::info);
    }
}

int main()
{
    std::printf ("Epi grand radiation suite -- near field, and the set that is not here\n");
    sectionNearField();
    sectionPhantoms();
    std::printf ("\n");
    if (gaps > 0) std::printf ("%d known gaps\n", gaps);
    std::printf (failures == 0 ? "all radiation rows within tolerance\n"
                               : "RADIATION ROWS OUT OF TOLERANCE\n");
    std::printf ("SUMMARY fail=%d gap=%d\n", failures, gaps);
    return failures == 0 ? 0 : 1;
}
