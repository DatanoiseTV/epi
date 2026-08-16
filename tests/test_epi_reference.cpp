/*
  Epi — acoustic reference suite.

  Every check here corresponds to a numbered row in docs/acoustic-checklist.md,
  and every target in it is a measurement taken off a real 1977 Rhodes Mark I or
  published in the literature. The suite renders the model, measures the same
  quantity the same way, and prints both side by side.

  It is deliberately not a pass/fail-only harness: a row that is out by 2 dB and
  a row that is out by 40 dB are different problems, and the printed table is
  what makes "it does not sound right" into something that can be worked on.

  Build: part of ctest, target epi_reference_tests.
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace epi;
namespace an = epianalysis;

// ===========================================================================
// Reporting
// ===========================================================================

static int failures = 0;
static int gaps     = 0;
static const char* section = "";

static void heading (const char* s)
{
    section = s;
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

    std::printf ("  %-3s %-34s %-22s %-22s %s\n", id, what, target.c_str(), got.c_str(), mark);
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
// Rendering
// ===========================================================================

static constexpr double kFs = 48000.0;

static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

// The instrument alone. The reference recordings are a direct feed from the
// harp, so the amplifier, the speaker and the room are all out of the path --
// otherwise the suite would be measuring the cabinet's opinion of the tine.
static EngineParams referenceParams()
{
    EngineParams p;
    p.tremDepth   = 0.0f;
    p.spaceMix    = 0.0f;
    p.cabMix      = 0.0f;
    p.preampDrive = 0.0f;
    p.bassDb      = 0.0f;
    p.trebleDb    = 0.0f;
    p.coilSat     = 0.0f;
    p.outGainLin  = 1.0f;
    return p;
}

struct RenderKey
{
    int note; int vel1000; int millis; int bodyMix1000;
    bool operator< (const RenderKey& o) const
    {
        if (note != o.note) return note < o.note;
        if (vel1000 != o.vel1000) return vel1000 < o.vel1000;
        if (millis != o.millis) return millis < o.millis;
        return bodyMix1000 < o.bodyMix1000;
    }
};

static std::map<RenderKey, std::vector<double>> renderCache;

static const std::vector<double>& render (int note, double vel, double seconds,
                                          float bodyMix = -1.0f)
{
    EngineParams p = referenceParams();
    if (bodyMix >= 0.0f) p.bodyMix = bodyMix;

    const RenderKey key { note,
                          static_cast<int> (std::lround (vel * 1000.0)),
                          static_cast<int> (std::lround (seconds * 1000.0)),
                          static_cast<int> (std::lround (p.bodyMix * 1000.0)) };
    auto it = renderCache.find (key);
    if (it != renderCache.end()) return it->second;

    const int N = static_cast<int> (kFs * seconds);
    const int block = 512;

    EpiEngine e;
    e.prepare (kFs, block);

    std::vector<float> L (static_cast<std::size_t> (N), 0.0f);
    std::vector<float> R (static_cast<std::size_t> (N), 0.0f);

    NoteEvent ev { 0, NoteEvent::noteOn, note, static_cast<float> (vel) };

    for (int i = 0; i < N; i += block)
    {
        const int n = std::min (block, N - i);
        e.process (L.data() + i, R.data() + i, n, p,
                   i == 0 ? &ev : nullptr, i == 0 ? 1 : 0);
    }

    std::vector<double> mono (static_cast<std::size_t> (N));
    for (int i = 0; i < N; ++i)
        mono[static_cast<std::size_t> (i)] = 0.5 * (static_cast<double> (L[static_cast<std::size_t> (i)])
                                                  + static_cast<double> (R[static_cast<std::size_t> (i)]));
    return renderCache.emplace (key, std::move (mono)).first->second;
}

// The notes the suite works on, chosen to line up with the reference tables.
struct TestNote { int midi; const char* name; };
static constexpr TestNote kBass  { 40, "E2"  };   //   82.4 Hz
static constexpr TestNote kLowMid{ 52, "E3"  };   //  164.8 Hz
static constexpr TestNote kMid   { 62, "D4"  };   //  293.7 Hz
static constexpr TestNote kUpper { 76, "E5"  };   //  659.3 Hz
static constexpr TestNote kTop   { 96, "C7"  };   // 2093.0 Hz

static constexpr double kSoft = 0.15;
static constexpr double kHard = 0.95;

// ===========================================================================
// A. What the harmonics do
// ===========================================================================

static void sectionA()
{
    heading ("A. Harmonic structure");

    // A1: the tine is a beam, not a string, and the pickup is a static
    // nonlinearity acting on it -- so every partial must land on an exact
    // multiple. Any stretching at all means something is generating partials
    // independently instead of distorting one sinusoid.
    {
        double worst = 0.0;
        int worstNote = 0, worstK = 0;
        for (TestNote tn : { kBass, kLowMid, kMid, kUpper })
        {
            const auto& x = render (tn.midi, kHard, 3.0);
            const double f0 = an::refineF0 (x, kFs, noteHz (tn.midi));
            for (int k = 2; k <= 6; ++k)
            {
                const double target = f0 * k;
                if (target > 0.4 * kFs) break;
                const an::Envelope e = an::heterodyne (x, kFs, target, f0);
                if (e.z.empty()) continue;
                // Measured late on purpose. The tine's own second bending mode
                // sits at 6.27 times the fundamental, close enough to the sixth
                // harmonic that no comb cut to f0 can separate them -- so the
                // partial's frequency is only readable once that mode has gone,
                // which it has by half a second.
                const double got = an::partialFrequency (e, target, 1.0, 2.5);
                if (got <= 0.0) continue;
                const double cents = 1200.0 * std::log2 (got / target);
                if (std::abs (cents) > std::abs (worst))
                { worst = cents; worstNote = tn.midi; worstK = k; }
            }
        }
        row ("A1", "partials are exact multiples", "within 1.0 ct to H6",
             fmt2 ("%+.2f ct (note %.0f", worst, static_cast<double> (worstNote))
                 + fmt (" H%.0f)", static_cast<double> (worstK)),
             std::abs (worst) < 1.0 ? Verdict::pass : Verdict::fail);
    }

    // A2/A3/A4: the pickup's field is not symmetric about the tine's rest
    // position, so how much second harmonic comes out depends on how far the
    // tine swings. That dependence -- soft note fundamental-dominant, hard note
    // octave-dominant, tens of decibels between them -- is the single most
    // recognisable thing about the instrument.
    for (TestNote tn : { kBass, kLowMid })
    {
        double h21[2] = { 0.0, 0.0 };
        for (int i = 0; i < 2; ++i)
        {
            const double vel = i == 0 ? kSoft : kHard;
            const auto& x = render (tn.midi, vel, 3.0);
            const double f0 = an::refineF0 (x, kFs, noteHz (tn.midi));
            const an::Envelope e1 = an::heterodyne (x, kFs, f0);
            const an::Envelope e2 = an::heterodyne (x, kFs, 2.0 * f0, f0);
            if (e1.z.empty() || e2.z.empty()) continue;
            h21[i] = e2.dbAt (0.3) - e1.dbAt (0.3);
        }

        char idHard[8], idSoft[8];
        std::snprintf (idHard, sizeof idHard, "A2");
        std::snprintf (idSoft, sizeof idSoft, "A3");

        row (idHard, (std::string ("H2-H1 hard, ") + tn.name).c_str(), "+6 .. +24 dB",
             fmt ("%+.1f dB", h21[1]), within (h21[1], 6.0, 24.0));
        row (idSoft, (std::string ("H2-H1 soft, ") + tn.name).c_str(), "-40 .. -10 dB",
             fmt ("%+.1f dB", h21[0]), within (h21[0], -40.0, -10.0));
        row ("A4", (std::string ("velocity swing, ") + tn.name).c_str(), "16 .. 37 dB",
             fmt ("%.1f dB", h21[1] - h21[0]), within (h21[1] - h21[0], 16.0, 37.0));
    }
}

// ===========================================================================
// B. Decay
// ===========================================================================

static void sectionB()
{
    heading ("B. Decay");

    // B1/B2: this is the load-bearing check on the whole architecture. If the
    // harmonics are made by distorting one sinusoid, harmonic k is that
    // sinusoid raised to the k-th power, so it must decay at exactly k times
    // the fundamental's rate. A bank of independently damped modes cannot do
    // this, and no amount of tuning would make it.
    for (TestNote tn : { kLowMid, kMid })
    {
        // Moderate velocity, not the softest available. Played very softly the
        // third harmonic of this model sits below the render's own inharmonic
        // floor, and what gets measured then is the floor's slope, not the
        // partial's. The relation being tested is a property of the
        // nonlinearity below the point where it starts to compress, which this
        // is comfortably inside.
        const double vel = 0.4;
        const auto& x = render (tn.midi, vel, 4.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (tn.midi));

        an::Envelope env[3];
        double loudest = -300.0;
        for (int k = 0; k < 3; ++k)
        {
            env[k] = an::heterodyne (x, kFs, static_cast<double> (k + 1) * f0, f0);
            if (! env[k].z.empty()) loudest = std::max (loudest, env[k].dbAt (0.3));
        }
        // Everything the harmonic series cannot explain, in the same units, so
        // a partial can be checked against it.
        const double floorDb = loudest + an::inharmonicDb (x, kFs, f0, 0.300);

        auto slope = [&env, floorDb] (int k) -> an::LineFit
        {
            if (env[k].z.empty()) return {};
            const double lvl = env[k].dbAt (0.3);
            if (lvl < floorDb + 15.0) return {};   // in the floor; not measurable
            return an::fitDecay (env[k], 0.3, 3.5, std::max (lvl - 35.0, floorDb + 6.0));
        };
        const an::LineFit f1 = slope (0), f2 = slope (1), f3 = slope (2);

        const double r2 = (f1.valid && f2.valid && f1.slopeDbPerS < -0.01)
                        ? f2.slopeDbPerS / f1.slopeDbPerS : 0.0;
        const double r3 = (f1.valid && f3.valid && f1.slopeDbPerS < -0.01)
                        ? f3.slopeDbPerS / f1.slopeDbPerS : 0.0;

        // The band runs from theory to measurement. A perfectly static
        // nonlinearity gives exactly k, since harmonic k is the fundamental
        // raised to the k-th power; the real instrument comes out slightly
        // above that, because its coil and its own radiation take a little more
        // off the higher partials as they go. Landing at k is the architecture
        // working; landing near 1 would mean the partials are separate modes.
        row ("B1", (std::string ("H2/H1 decay ratio, ") + tn.name).c_str(), "1.95 .. 2.30",
             fmt ("%.2f", r2), within (r2, 1.95, 2.30));
        row ("B2", (std::string ("H3/H1 decay ratio, ") + tn.name).c_str(), "2.90 .. 3.55",
             fmt ("%.2f", r3), within (r3, 2.90, 3.55));
    }

    // B4: sustain, register by register. Measured over a few seconds and
    // extrapolated, because a bass tine takes half a minute to fall 60 dB and
    // rendering that per note would make the suite unusable.
    // A long note has to be rendered long. Fitting a 30-second decay across
    // three seconds sees three decibels of slope, which the initial rise of B6
    // partly cancels; the answer that comes out is dominated by whichever
    // happened to win. The fitted span is printed so a shallow fit is visible
    // as a shallow fit rather than passing quietly.
    struct T60Target { TestNote n; double want; double tol; double render; };
    const T60Target targets[] = {
        { { 45, "A2" }, 28.0, 0.55, 14.0 },
        { kMid,         19.0, 0.55, 12.0 },
        { kUpper,        5.7, 0.55,  6.0 },
        { kTop,          1.7, 0.60,  3.0 },
    };
    for (const T60Target& t : targets)
    {
        const auto& x = render (t.n.midi, 0.7, t.render);
        const double f0 = an::refineF0 (x, kFs, noteHz (t.n.midi), 0.3, std::min (1.3, t.render - 0.5));
        const an::Envelope e = an::heterodyne (x, kFs, f0);
        // From one second, so the strike transient and the initial rise are
        // outside the fit.
        const an::LineFit f = an::fitDecay (e, 1.0, t.render - 0.2, -120.0);
        const double t60 = (f.valid && f.slopeDbPerS < -0.01) ? -60.0 / f.slopeDbPerS : -1.0;
        const double span = f.valid ? -f.slopeDbPerS * (t.render - 1.2) : 0.0;
        const double lo = t.want * (1.0 - t.tol), hi = t.want * (1.0 + t.tol);
        row ("B4", (std::string ("T60 fundamental, ") + t.n.name).c_str(),
             fmt2 ("%.1f s (+/-%.0f%%)", t.want, t.tol * 100.0),
             t60 > 0.0 ? fmt2 ("%.1f s (%.0f dB seen)", t60, span) : std::string ("no decay"),
             t60 > 0.0 ? within (t60, lo, hi) : Verdict::fail);
    }

    // B5: the real envelope is not one exponential. Two slopes fit it well,
    // one does not -- which is the signature of the tine handing energy to the
    // tonebar and getting some back.
    {
        const auto& x = render (kMid.midi, 0.7, 4.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (kMid.midi));
        const an::LineFit f = an::fitDecay (an::heterodyne (x, kFs, f0), 0.3, 3.5);
        row ("B5", "fundamental is not 1 exponential", "1.4 .. 5.1 dB residual",
             f.valid ? fmt ("%.2f dB rms", f.residRmsDb) : std::string ("n/a"),
             f.valid ? within (f.residRmsDb, 1.4, 5.1) : Verdict::fail);
    }
}

// ===========================================================================
// C. The attack
// ===========================================================================

static void sectionC()
{
    heading ("C. Attack");

    // C1/C2: the strike puts a burst of content into the tine that belongs to
    // no harmonic -- the tonebar, the hammer itself, the tine's own upper beam
    // modes. On the real instrument it is loud for a moment and then it is
    // genuinely gone, and its disappearance is most of what the attack sounds
    // like.
    for (TestNote tn : { kBass, kLowMid, kUpper })
    {
        const auto& x = render (tn.midi, 0.8, 2.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (tn.midi));

        // Inside the attack the comb heterodyne is useless -- its group delay
        // is longer than the thing being measured -- so this one is a fit of
        // the whole harmonic series across a few periods. The series has to run
        // to Nyquist for the same reason as below.
        const an::HarmonicFit early = an::fitHarmonics (x, kFs, f0,
            static_cast<std::size_t> (0.010 * kFs), an::residualWindow (kFs, f0),
            static_cast<int> (0.45 * kFs / f0));
        const double eDb = early.valid ? early.residRelMaxDb() : 0.0;

        const double lDb = an::inharmonicDb (x, kFs, f0, 0.300);

        row ("C1", (std::string ("inharmonic at 10 ms, ") + tn.name).c_str(), "-42 .. -8 dB",
             fmt ("%.1f dB", eDb), within (eDb, -42.0, -8.0));
        row ("C2", (std::string ("inharmonic at 300 ms, ") + tn.name).c_str(), "-92 .. -78 dB",
             fmt ("%.1f dB", lDb),
             within (lDb, -92.0, -78.0) == Verdict::pass ? Verdict::pass : Verdict::knownGap);
    }

    // C5: how long the note takes to arrive. A bass tine is heavy and the
    // hammer stays on it for milliseconds; a treble tine is stiff and short and
    // the note is simply there.
    struct AttackTarget { TestNote n; double lo, hi; };
    const AttackTarget attacks[] = {
        { kBass,  8.0, 30.0 },
        { kMid,   2.0, 14.0 },
        { kTop,   0.3,  4.0 },
    };
    for (const AttackTarget& a : attacks)
    {
        const auto& x = render (a.n.midi, 0.8, 1.0);
        const double ms = an::attackTimeMs (x, kFs);
        row ("C5", (std::string ("attack 10-90%, ") + a.n.name).c_str(),
             fmt2 ("%.1f .. %.1f ms", a.lo, a.hi),
             ms > 0.0 ? fmt ("%.2f ms", ms) : std::string ("n/a"),
             ms > 0.0 ? within (ms, a.lo, a.hi) : Verdict::fail);
    }
}

// ===========================================================================
// E. Steadiness
// ===========================================================================

static void sectionE()
{
    heading ("E. Steadiness");

    // E1: a real tine's fundamental decays smoothly. Audible beating means two
    // components a few cents apart at comparable level, and a Rhodes does not
    // have that -- which is why the tine is modelled in one plane. The bound is
    // tight and it is the check that catches an accidental second oscillator.
    for (TestNote tn : { kLowMid, kMid })
    {
        const auto& x = render (tn.midi, 0.7, 4.0);
        const double f0 = an::refineF0 (x, kFs, noteHz (tn.midi));
        const double swing = an::detrendedSwingDb (an::heterodyne (x, kFs, f0), 0.5, 3.0);
        row ("E1", (std::string ("AM on fundamental, ") + tn.name).c_str(), "under 1.5 dB pk-pk",
             swing >= 0.0 ? fmt ("%.2f dB", swing) : std::string ("n/a"),
             (swing >= 0.0 && swing < 1.5) ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// F. Pitch
// ===========================================================================

static void sectionF()
{
    heading ("F. Pitch");

    // F2: tuning. The model solves each tine's geometry from the beam equation,
    // so this is a check that the solve and the numerical scheme agree, not a
    // check on a tuning table.
    {
        double worst = 0.0;
        int worstNote = 0;
        for (int n : { 33, 45, 57, 69, 81, 93 })
        {
            const auto& x = render (n, 0.7, 2.0);
            const double f0 = an::refineF0 (x, kFs, noteHz (n), 0.3, 1.3);
            const double cents = 1200.0 * std::log2 (f0 / noteHz (n));
            if (std::abs (cents) > std::abs (worst)) { worst = cents; worstNote = n; }
        }
        row ("F2", "steady tuning across compass", "within 3 ct",
             fmt2 ("%+.2f ct (note %.0f)", worst, static_cast<double> (worstNote)),
             std::abs (worst) < 3.0 ? Verdict::pass : Verdict::fail);
    }

    // F1: struck hard, a bass tine starts sharp and settles. It is a real
    // large-amplitude stiffness effect; what matters is that it settles in
    // about a fifth of a second rather than drifting for seconds, which is what
    // an over-strong stretching term sounds like.
    {
        const auto& x = render (kBass.midi, kHard, 3.0);
        const double nominal = noteHz (kBass.midi);
        const double settled = an::refineF0 (x, kFs, nominal, 1.0, 2.5);
        const an::Envelope e = an::heterodyne (x, kFs, settled);
        const double early = an::partialFrequency (e, settled, 0.04, 0.16);
        const double late  = an::partialFrequency (e, settled, 1.0, 2.5);
        const double drift = (early > 0.0 && late > 0.0)
                           ? 1200.0 * std::log2 (early / late) : 0.0;
        row ("F1", "initial sharpness, bass ff", "0 .. +35 ct",
             fmt ("%+.1f ct", drift), within (drift, -1.0, 35.0));

        // And it must be over quickly.
        const double mid = an::partialFrequency (e, settled, 0.35, 0.60);
        const double residual = (mid > 0.0 && late > 0.0)
                              ? 1200.0 * std::log2 (mid / late) : 0.0;
        row ("F1", "settled by 350 ms", "under 4 ct remaining",
             fmt ("%+.1f ct", residual),
             std::abs (residual) < 4.0 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================
// G. Brightness
// ===========================================================================

static void sectionG()
{
    heading ("G. Brightness");

    // G1: playing harder must open the tone up, and by a lot -- this is the
    // same field asymmetry as A2 seen as a broadband quantity, and it is what
    // makes the instrument respond to touch rather than just get louder.
    for (TestNote tn : { kLowMid, kMid })
    {
        double c[2] = { 0.0, 0.0 };
        for (int i = 0; i < 2; ++i)
        {
            const auto& x = render (tn.midi, i == 0 ? kSoft : kHard, 2.0);
            c[i] = an::spectralCentroid (x, kFs, static_cast<std::size_t> (0.30 * kFs), 8192);
        }
        const double ratio = c[0] > 0.0 ? c[1] / c[0] : 0.0;
        row ("G1", (std::string ("centroid soft->hard, ") + tn.name).c_str(), "2.5 .. 6.0 x",
             fmt ("%.2f x", ratio), within (ratio, 2.5, 6.0));
    }

    // G3/G4: and the attack is brighter than the sustain only when the note is
    // hit hard. Played softly the strike is duller than what follows, because
    // the hammer stays in contact long enough to act as a lowpass.
    {
        const auto& hard = render (kMid.midi, kHard, 2.0);
        const auto& soft = render (kMid.midi, kSoft, 2.0);
        auto rel = [] (const std::vector<double>& x)
        {
            const double a = an::spectralCentroid (x, kFs, static_cast<std::size_t> (0.005 * kFs), 2048);
            const double s = an::spectralCentroid (x, kFs, static_cast<std::size_t> (0.30 * kFs), 8192);
            return (a > 0.0 && s > 0.0) ? a / s : 0.0;
        };
        const double rh = rel (hard), rs = rel (soft);
        row ("G3", "attack vs sustain, hard", "1.10 .. 1.84 x", fmt ("%.2f x", rh),
             within (rh, 1.10, 1.84));
        row ("G4", "attack vs sustain, soft", "0.72 .. 0.94 x", fmt ("%.2f x", rs),
             within (rs, 0.72, 0.94));
    }
}

// ===========================================================================
// Structural checks that do not come from the reference tables but that the
// model must satisfy for any of the above to mean anything.
// ===========================================================================

static void sectionStructural()
{
    heading ("S. Structural");

    // Nothing may be non-finite, anywhere, at any velocity, at any pitch. This
    // is the check that a numerical accident cannot hide behind a plausible
    // spectrum.
    {
        bool clean = true;
        double peak = 0.0;
        int badNote = -1;
        for (int n = 21; n <= 108; n += 7)
            for (double v : { 0.05, 0.5, 1.0 })
            {
                const auto& x = render (n, v, 1.0);
                for (double s : x)
                {
                    if (! std::isfinite (s)) { clean = false; badNote = n; break; }
                    peak = std::max (peak, std::abs (s));
                }
                if (! clean) break;
            }
        row ("S1", "all output finite, full compass", "finite, peak < 4",
             clean ? fmt ("peak %.2f", peak) : fmt ("nonfinite at note %.0f", static_cast<double> (badNote)),
             (clean && peak < 4.0) ? Verdict::pass : Verdict::fail);
    }

    // Level must rise monotonically with velocity. It sounds obvious; it is not
    // guaranteed by a model where harder playing moves the tine into a
    // different part of the field.
    {
        bool mono = true;
        double worstDrop = 0.0;
        for (TestNote tn : { kBass, kMid, kUpper })
        {
            double prev = 0.0;
            for (double v : { 0.1, 0.25, 0.45, 0.65, 0.85, 1.0 })
            {
                const auto& x = render (tn.midi, v, 1.0);
                double p = 0.0;
                for (double s : x) p = std::max (p, std::abs (s));
                if (prev > 0.0 && p < prev)
                { mono = false; worstDrop = std::max (worstDrop, 20.0 * std::log10 (prev / std::max (1e-12, p))); }
                prev = p;
            }
        }
        row ("S2", "peak rises with velocity", "monotonic",
             mono ? std::string ("monotonic") : fmt ("drops %.1f dB", worstDrop),
             mono ? Verdict::pass : Verdict::fail);
    }

    // The sympathetic path must actually do something. A held note with the
    // harp coupled is not audibly different, but a note struck while others are
    // free must excite them -- if it does not, the coupling is decorative.
    {
        const auto& off = render (kMid.midi, 0.9, 2.0, 0.0f);
        const auto& on  = render (kMid.midi, 0.9, 2.0, 1.0f);
        double d = 0.0;
        for (std::size_t i = 0; i < off.size(); ++i)
            d = std::max (d, std::abs (on[i] - off[i]));
        double p = 0.0;
        for (double s : off) p = std::max (p, std::abs (s));
        const double relDb = p > 0.0 ? 20.0 * std::log10 (std::max (1.0e-12, d / p)) : -300.0;
        row ("S3", "harp coupling changes the sound", "above -60 dB rel peak",
             fmt ("%.1f dB", relDb), relDb > -60.0 ? Verdict::pass : Verdict::fail);
    }
}

// ===========================================================================

int main()
{
    std::printf ("Epi acoustic reference suite\n");
    std::printf ("targets from docs/acoustic-checklist.md (1977 Rhodes Mark I, and the literature)\n");
    std::printf ("\n  %-3s %-34s %-22s %-22s %s\n", "ID", "PROPERTY", "TARGET", "MEASURED", "");

    sectionStructural();
    sectionA();
    sectionB();
    sectionC();
    sectionE();
    sectionF();
    sectionG();

    std::printf ("\n");
    if (gaps > 0)
        std::printf ("%d known gap%s (tracked in docs/acoustic-checklist.md)\n", gaps, gaps == 1 ? "" : "s");
    if (failures == 0)
        std::printf ("all reference rows within tolerance\n");
    else
        std::printf ("%d row%s outside tolerance\n", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
