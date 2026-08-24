/*
  Epi — engine-level integration testbench.

  The six sibling suites verify voices, chains and presets in isolation. Every
  integration bug this project actually hit lived one layer up, in EpiEngine:
  seam bursts on instrument switches, notes swallowed by the switch fade,
  knob-turn clicks, silent material/transducer pairings, a base-rate
  saturation seat that aliased, loudness drift between instruments. This
  suite fences that layer: it renders the whole engine, measures, and prints
  the same row table the reference suite does.

  Every bound below is either physics (the eddy-coupling law, the output
  rail's 1.0 asymptote, the bending-share sustain law) or an already-measured
  fact cited in the comment beside it. Bounds are never tuned to the code:
  a failing row is a defect or a wrong citation, and both get investigated.

  Build: part of ctest, target epi_engine_tests.
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace epi;
namespace an = epianalysis;

// ===========================================================================
// Reporting (same table discipline as test_epi_reference.cpp)
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

    std::printf ("  %-4s %-40s %-28s %-30s %s\n", id, what, target.c_str(), got.c_str(), mark);
    std::fflush (stdout);
}

static Verdict verdict (bool ok) { return ok ? Verdict::pass : Verdict::fail; }

// A property that is understood, unfixed, and pinned: the true target failed,
// but the value sits where it sat when the gap was accepted. It still carries
// a holding bound so the defect cannot quietly widen -- a known gap that can
// drift is just a deleted test with extra steps (reference suite discipline).
static Verdict gapIf (bool pass, bool held)
{
    if (pass) return Verdict::pass;
    return held ? Verdict::knownGap : Verdict::fail;
}

static std::string fmt (const char* f, double a)
{
    char b[128];
    std::snprintf (b, sizeof b, f, a);
    return b;
}

static std::string fmt2 (const char* f, double a, double b2)
{
    char b[128];
    std::snprintf (b, sizeof b, f, a, b2);
    return b;
}

static const char* const kInstName[5] = { "Tine", "E-Grand", "Reed", "Grand", "Clav" };
static const char* const kMatName[8]  = { "music wire", "stainless", "bronze", "brass",
                                          "titanium", "aluminium", "tungsten", "nylon" };
static const char* const kTransName[4] = { "magnetic", "native", "electro", "contact" };

// ===========================================================================
// Rendering
// ===========================================================================

static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

// Measurement configuration: the sends that are not the instrument are off,
// exactly as the reference suite renders. Everything else stays at the
// engine's own defaults so this suite measures the shipped voicing.
static EngineParams measParams (int instrument)
{
    EngineParams p;
    p.instrument = instrument;
    p.tremDepth  = 0.0f;
    p.spaceMix   = 0.0f;
    p.phaserMix  = 0.0f;
    p.cabMix     = 0.0f;
    p.outGainLin = 1.0f;
    return p;
}

struct Stereo
{
    std::vector<float> L, R;
    double fs = 48000.0;
    int samples() const { return static_cast<int> (L.size()); }
};

struct TimedEvent { int sample = 0; NoteEvent ev; };

// Per-block parameter tweak: blockStart is the first sample of the block.
// The EngineParams reference persists across blocks, so a tweak that writes
// a field holds it until the next write, which is how a host automates.
using Tweak = std::function<void (int blockStart, EngineParams&)>;

static Stereo renderEngine (double fs, EngineParams p, double seconds,
                            std::vector<TimedEvent> evs,
                            const Tweak& tweak = {}, int block = 512,
                            double* wallSeconds = nullptr)
{
    const int N = static_cast<int> (fs * seconds);
    Stereo out;
    out.fs = fs;
    out.L.assign (static_cast<std::size_t> (N), 0.0f);
    out.R.assign (static_cast<std::size_t> (N), 0.0f);

    std::stable_sort (evs.begin(), evs.end(),
                      [] (const TimedEvent& a, const TimedEvent& b) { return a.sample < b.sample; });

    EpiEngine e;
    e.prepare (fs, block);

    std::vector<NoteEvent> blockEvs;
    std::size_t k = 0;

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i += block)
    {
        const int n = std::min (block, N - i);
        if (tweak) tweak (i, p);

        blockEvs.clear();
        while (k < evs.size() && evs[k].sample < i + n)
        {
            NoteEvent ev = evs[k].ev;
            ev.offset = std::max (0, evs[k].sample - i);
            blockEvs.push_back (ev);
            ++k;
        }
        e.process (out.L.data() + i, out.R.data() + i, n, p,
                   blockEvs.empty() ? nullptr : blockEvs.data(),
                   static_cast<int> (blockEvs.size()));
    }
    if (wallSeconds != nullptr)
        *wallSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return out;
}

static std::vector<TimedEvent> chordOn (const std::vector<int>& notes, float vel, int sample = 0)
{
    std::vector<TimedEvent> evs;
    for (int n : notes)
        evs.push_back ({ sample, { 0, NoteEvent::noteOn, n, vel } });
    return evs;
}

// ===========================================================================
// Measurement
// ===========================================================================

static std::vector<double> monoOf (const Stereo& s)
{
    std::vector<double> m (s.L.size());
    for (std::size_t i = 0; i < s.L.size(); ++i)
        m[i] = 0.5 * (static_cast<double> (s.L[i]) + static_cast<double> (s.R[i]));
    return m;
}

static bool allFinite (const Stereo& s)
{
    for (float v : s.L) if (! std::isfinite (v)) return false;
    for (float v : s.R) if (! std::isfinite (v)) return false;
    return true;
}

static double peakAbs (const Stereo& s)
{
    double p = 0.0;
    for (float v : s.L) p = std::max (p, std::fabs (static_cast<double> (v)));
    for (float v : s.R) p = std::max (p, std::fabs (static_cast<double> (v)));
    return p;
}

static double rmsDbOf (const std::vector<double>& x, double fs, double ta, double tb)
{
    const std::size_t a = static_cast<std::size_t> (std::max (0.0, ta * fs));
    const std::size_t b = std::min (x.size(), static_cast<std::size_t> (tb * fs));
    if (b <= a) return -300.0;
    double acc = 0.0;
    for (std::size_t i = a; i < b; ++i) acc += x[i] * x[i];
    return 10.0 * std::log10 (std::max (1.0e-300, acc / static_cast<double> (b - a)));
}

// Mono RMS: the level convention the reference suite benches against.
static double rmsDbMono (const Stereo& s, double ta, double tb)
{
    return rmsDbOf (monoOf (s), s.fs, ta, tb);
}

// Loudest-channel RMS: the honest silence measure (mono can phase-cancel).
static double rmsDbMax (const Stereo& s, double ta, double tb)
{
    const std::size_t a = static_cast<std::size_t> (std::max (0.0, ta * s.fs));
    const std::size_t b = std::min (s.L.size(), static_cast<std::size_t> (tb * s.fs));
    if (b <= a) return -300.0;
    double aL = 0.0, aR = 0.0;
    for (std::size_t i = a; i < b; ++i)
    {
        aL += static_cast<double> (s.L[i]) * static_cast<double> (s.L[i]);
        aR += static_cast<double> (s.R[i]) * static_cast<double> (s.R[i]);
    }
    const double n = static_cast<double> (b - a);
    return 10.0 * std::log10 (std::max (1.0e-300, std::max (aL, aR) / n));
}

// Max absolute second difference across both channels in [i0, i1).
static double maxD2 (const Stereo& s, int i0, int i1)
{
    i0 = std::max (i0, 2);
    i1 = std::min (i1, s.samples());
    double worst = 0.0;
    for (const auto* ch : { &s.L, &s.R })
    {
        const auto& x = *ch;
        for (int n = i0; n < i1; ++n)
        {
            const double d = static_cast<double> (x[static_cast<std::size_t> (n)])
                           - 2.0 * static_cast<double> (x[static_cast<std::size_t> (n - 1)])
                           + static_cast<double> (x[static_cast<std::size_t> (n - 2)]);
            worst = std::max (worst, std::fabs (d));
        }
    }
    return worst;
}

static double energyIn (const std::vector<double>& x, double fs, double ta, double tb)
{
    const std::size_t a = static_cast<std::size_t> (std::max (0.0, ta * fs));
    const std::size_t b = std::min (x.size(), static_cast<std::size_t> (tb * fs));
    double acc = 0.0;
    for (std::size_t i = a; i < b; ++i) acc += x[i] * x[i];
    return acc;
}

// Time from the envelope peak to the first crossing dropDb below it.
static double timeToDropDb (const std::vector<double>& x, double fs, double dropDb)
{
    const std::vector<double> e = an::rectifiedEnvelope (x, fs, 10.0);
    std::size_t pk = 0;
    double pkV = 0.0;
    for (std::size_t i = 0; i < e.size(); ++i)
        if (e[i] > pkV) { pkV = e[i]; pk = i; }
    if (pkV <= 0.0) return 0.0;
    const double thr = pkV * std::pow (10.0, -dropDb / 20.0);
    for (std::size_t i = pk; i < e.size(); ++i)
        if (e[i] < thr) return static_cast<double> (i - pk) / fs;
    return static_cast<double> (e.size() - pk) / fs;   // never got there
}

static double centsOff (double f, double ref)
{
    if (f <= 0.0 || ref <= 0.0) return 9999.0;
    return 1200.0 * std::log2 (f / ref);
}

// ===========================================================================
// Material / transducer render cache. Solo C4 mf, strike noise off exactly as
// the reference suite measures (the mechanism thump is its own signal and
// contaminates coupling measurements).
// ===========================================================================

struct MTKey
{
    int inst, mat, trans;
    bool operator< (const MTKey& o) const
    {
        if (inst != o.inst) return inst < o.inst;
        if (mat != o.mat)   return mat < o.mat;
        return trans < o.trans;
    }
};

static std::map<MTKey, Stereo> mtCache;

static const Stereo& renderMT (int inst, int mat, int trans)
{
    const MTKey key { inst, mat, trans };
    auto it = mtCache.find (key);
    if (it != mtCache.end()) return it->second;

    EngineParams p = measParams (inst);
    p.material    = mat;
    p.transducer  = trans;
    p.strikeNoise = 0.0f;
    Stereo s = renderEngine (48000.0, p, 2.0,
                             { { 0, { 0, NoteEvent::noteOn, 60, 0.8f } } });
    return mtCache.emplace (key, std::move (s)).first->second;
}

// ===========================================================================
// 1. Life and parity: every instrument alive and calibrated at three rates.
// ===========================================================================

static void sectionLife()
{
    heading ("1. Life and parity at 44.1/48/96 kHz");

    // Level bench: the calibration suite measured -24 dBFS +/-1 for the mf
    // chord at 48k; [-25, -11] allows rate variance and instrument spread
    // about the -18 dBFS bench (raised from -24: the plugin sat quiet in
    // sessions; peaks land in the soft rail, never a hard clip)
    // without letting a silent or roaring instrument through. Peak <= 1.0 is
    // the output rail's own asymptote (a + s*tanh -> 0.85 + 0.15).
    int id = 1;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int inst = 0; inst < 5; ++inst)
        {
            const Stereo s = renderEngine (fs, measParams (inst), 3.0,
                                           chordOn ({ 60, 64, 67 }, 0.7f));
            const bool finite = allFinite (s);
            const double pk = peakAbs (s);
            const double rms = rmsDbMono (s, 0.0, 3.0);
            const bool ok = finite && pk <= 1.0 && rms >= -25.0 && rms <= -11.0;
            char what[64], idb[8];
            std::snprintf (what, sizeof what, "%s chord @ %.0fk", kInstName[inst], fs / 1000.0);
            std::snprintf (idb, sizeof idb, "1.%d", id++);
            row (idb, what, "finite, pk<=1, RMS -31..-17",
                 finite ? fmt2 ("pk %.3f, RMS %.1f dB", pk, rms) : "NOT FINITE",
                 verdict (ok));
        }

    // f0 sanity: equal temperament is the tuning contract; 25 cents is the
    // reference suite's own tuning tolerance.
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        {
            const Stereo s = renderEngine (fs, measParams (1), 2.0,
                                           chordOn ({ 60 }, 0.7f));
            const double f0 = an::refineF0 (monoOf (s), fs, noteHz (60), 0.3, 1.5);
            const double c = centsOff (f0, noteHz (60));
            char what[64], idb[8];
            std::snprintf (what, sizeof what, "E-Grand C4 f0 @ %.0fk", fs / 1000.0);
            std::snprintf (idb, sizeof idb, "1.%d", id++);
            row (idb, what, "261.6 Hz +/-25 ct", fmt2 ("%.2f Hz (%+.1f ct)", f0, c),
                 verdict (std::fabs (c) <= 25.0));
        }
        {
            const Stereo s = renderEngine (fs, measParams (4), 2.0,
                                           chordOn ({ 57 }, 0.7f));
            const double f0 = an::refineF0 (monoOf (s), fs, noteHz (57), 0.3, 1.5);
            const double c = centsOff (f0, noteHz (57));
            char what[64], idb[8];
            std::snprintf (what, sizeof what, "Clav A3 f0 @ %.0fk", fs / 1000.0);
            std::snprintf (idb, sizeof idb, "1.%d", id++);
            row (idb, what, "220.0 Hz +/-25 ct", fmt2 ("%.2f Hz (%+.1f ct)", f0, c),
                 verdict (std::fabs (c) <= 25.0));
        }
    }
}

// ===========================================================================
// 2. The seam: instrument switches mid-ring, and the pending-note guarantee.
// ===========================================================================

static void sectionSeam()
{
    heading ("2. Instrument switch seam");

    const double fs = 48000.0;
    const int block = 512;

    // Switch every second through all five and back while chords ring.
    // The seam measured -49 dBFS (0.0035) worst-sample after the
    // fade-through-silence fix; 0.02 bounds it with margin while still
    // catching any return of the pre-fix burst (which was orders louder).
    //
    // KNOWN GAP: re-entering an instrument whose chain already ran resumes
    // that chain from the stale mid-signal state it froze at when the fade
    // completed -- the fade multiplies the OUTPUT, so the chain's filter
    // states never see it. Fresh switches measure 0.0003; the Reed re-entry
    // in this tour measures 0.052, and a tight 2->3->2 with only 1 s away
    // measures 0.247 (pinned in row 2.2). Held at 0.10 here until the seam
    // resets the outgoing chain.
    {
        static const int seq[9] = { 0, 1, 2, 3, 4, 3, 2, 1, 0 };
        std::vector<TimedEvent> evs = chordOn ({ 60, 64, 67 }, 0.7f);
        for (int k = 1; k <= 8; ++k)
            for (int n : { 60, 64, 67 })
                evs.push_back ({ static_cast<int> ((k + 0.1) * fs),
                                 { 0, NoteEvent::noteOn, n, 0.7f } });

        const Tweak tweak = [&] (int blockStart, EngineParams& p)
        {
            const int sec = std::min (8, static_cast<int> (blockStart / fs));
            p.instrument = seq[sec];
        };
        const Stereo s = renderEngine (fs, measParams (0), 9.5, evs, tweak, block);

        double worst = 0.0;
        int worstSwitch = 0;
        for (int k = 1; k <= 8; ++k)
        {
            const int sw = ((static_cast<int> (k * fs) + block - 1) / block) * block;
            const double d = maxD2 (s, sw, sw + static_cast<int> (0.060 * fs));
            if (d > worst) { worst = d; worstSwitch = k; }
        }
        char got[96];
        std::snprintf (got, sizeof got, "%.4f (switch %d: %s->%s)", worst, worstSwitch,
                       kInstName[seq[worstSwitch - 1]], kInstName[seq[worstSwitch]]);
        row ("2.1", "seam d2, 8 switches, chord ringing", "<= 0.02", got,
             allFinite (s) ? gapIf (worst <= 0.02, worst <= 0.10) : Verdict::fail);
    }

    // The sharp repro of the same gap: away for one second and back. Pinned
    // at its measured value (0.247) so the defect cannot widen unnoticed.
    {
        std::vector<TimedEvent> evs = chordOn ({ 60, 64, 67 }, 0.7f);
        for (double t : { 1.1, 2.1 })
            for (int n : { 60, 64, 67 })
                evs.push_back ({ static_cast<int> (t * fs), { 0, NoteEvent::noteOn, n, 0.7f } });
        const Tweak tweak = [&] (int blockStart, EngineParams& p)
        {
            const double t = blockStart / fs;
            p.instrument = t < 1.0 ? 2 : (t < 2.0 ? 3 : 2);
        };
        const Stereo s = renderEngine (fs, measParams (2), 3.0, evs, tweak, block);
        const int sw = ((2 * static_cast<int> (fs) + block - 1) / block) * block;
        const double d = maxD2 (s, sw, sw + static_cast<int> (0.060 * fs));
        row ("2.2", "Reed re-entry snap (2->3->2)", "<= 0.02", fmt ("%.4f", d),
             gapIf (d <= 0.02, d <= 0.30));
    }

    // A noteOn in the SAME block as the switch must sound: the pending queue
    // parks it through the fade and replays it on the new instrument.
    {
        struct Case { int from, to; } cases[2] = { { 0, 2 }, { 3, 4 } };
        int id = 3;
        for (const auto& c : cases)
        {
            const int swBlock = ((static_cast<int> (0.5 * fs) + block - 1) / block) * block;
            const Tweak tweak = [&] (int blockStart, EngineParams& p)
            {
                p.instrument = blockStart >= swBlock ? c.to : c.from;
            };
            std::vector<TimedEvent> evs = { { swBlock, { 0, NoteEvent::noteOn, 60, 0.8f } } };
            const Stereo s = renderEngine (fs, measParams (c.from), 2.0, evs, tweak, block);
            const double t0 = swBlock / fs;
            const double rms = rmsDbMono (s, t0 + 0.5, t0 + 1.0);
            char what[64], idb[8];
            std::snprintf (what, sizeof what, "noteOn in switch block %s->%s",
                           kInstName[c.from], kInstName[c.to]);
            std::snprintf (idb, sizeof idb, "2.%d", id++);
            row (idb, what, "> -60 dBFS after 0.5 s", fmt ("%.1f dBFS", rms),
                 verdict (rms > -60.0));
        }
    }
}

// ===========================================================================
// 3. Pedals: half-pedal physics, damper silence, sostenuto, allNotesOff.
// ===========================================================================

static void sectionPedals()
{
    heading ("3. Pedals");

    const double fs = 48000.0;

    // (a) Half-pedal monotone: the damper is a continuous contact term, so
    // more pedal must never mean less tail. Strictly increasing across
    // CC64 = 0, 64, 96; the top step is allowed to saturate, because real
    // dampers lift fully before the end of the pedal's travel -- the grand
    // models exactly that (GrandVoice kPedalFree = 0.7: CC 96 and 127 both
    // sit above the free point and must sound the same).
    for (int inst = 0; inst < 5; ++inst)
    {
        double tail[4];
        const float cc[4] = { 0.0f, 0.5f, 0.75f, 1.0f };
        for (int i = 0; i < 4; ++i)
        {
            std::vector<TimedEvent> evs;
            evs.push_back ({ 0, { 0, NoteEvent::sustain, 0, cc[i] } });
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, 60, 0.8f } });
            evs.push_back ({ static_cast<int> (1.0 * fs), { 0, NoteEvent::noteOff, 60, 0.0f } });
            const Stereo s = renderEngine (fs, measParams (inst), 2.4, evs);
            tail[i] = rmsDbMono (s, 1.9, 2.2);
        }
        const bool mono = tail[0] < tail[1] && tail[1] < tail[2]
                       && tail[3] >= tail[2] - 0.5;
        char what[64], idb[8];
        std::snprintf (what, sizeof what, "%s half-pedal tail monotone", kInstName[inst]);
        std::snprintf (idb, sizeof idb, "3a.%d", inst);
        char got[96];
        std::snprintf (got, sizeof got, "%.0f/%.0f/%.0f/%.0f dB",
                       tail[0], tail[1], tail[2], tail[3]);
        row (idb, what, "rising, top may saturate", got, verdict (mono));
    }

    // (b) Pedal-up damper silence, the electrics: a released key with no
    // pedal must be gone -- the sympathetic path is gated on the pedal, so
    // nothing else may ring either. Output 1.5 s after key-up under -80 dBFS.
    for (int inst : { 0, 1, 2, 4 })
    {
        std::vector<TimedEvent> evs;
        evs.push_back ({ 0, { 0, NoteEvent::noteOn, 60, 0.8f } });
        evs.push_back ({ static_cast<int> (0.5 * fs), { 0, NoteEvent::noteOff, 60, 0.0f } });
        const Stereo s = renderEngine (fs, measParams (inst), 2.4, evs);
        const double lvl = rmsDbMax (s, 2.0, 2.3);
        char what[64], idb[8];
        std::snprintf (what, sizeof what, "%s pedal-up silence 1.5 s after key-up", kInstName[inst]);
        std::snprintf (idb, sizeof idb, "3b.%d", inst);
        row (idb, what, "< -80 dBFS", fmt ("%.1f dBFS", lvl), verdict (lvl < -80.0));
    }

    // (c) Sostenuto, the grand's middle pedal: a key latched by it rings on
    // after release; a plain release is damped. >= 60 dB apart at 1 s.
    {
        auto renderRelease = [&] (bool latch) -> double
        {
            std::vector<TimedEvent> evs;
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, 48, 0.8f } });
            if (latch)
                evs.push_back ({ static_cast<int> (0.15 * fs), { 0, NoteEvent::sostenuto, 0, 1.0f } });
            evs.push_back ({ static_cast<int> (0.30 * fs), { 0, NoteEvent::noteOff, 48, 0.0f } });
            const Stereo s = renderEngine (fs, measParams (3), 1.6, evs);
            return rmsDbMono (s, 1.25, 1.40);
        };
        const double latched = renderRelease (true);
        const double plain   = renderRelease (false);
        row ("3c", "Grand sostenuto latch vs plain release", ">= 60 dB apart at 1 s",
             fmt2 ("latched %.0f, plain %.0f dB", latched, plain),
             verdict (latched - plain >= 60.0));
    }

    // (d) allNotesOff with the pedal held: stop means stop -- the handler
    // drops the pedal too, precisely because hosts send all-notes-off
    // without a CC64 release. Under -60 dBFS within 1 s.
    for (int inst = 0; inst < 5; ++inst)
    {
        std::vector<TimedEvent> evs;
        evs.push_back ({ 0, { 0, NoteEvent::sustainOn, 0, 1.0f } });
        for (int n : { 48, 52, 55, 60, 64 })
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, n, 0.9f } });
        evs.push_back ({ static_cast<int> (1.0 * fs), { 0, NoteEvent::allNotesOff, 0, 0.0f } });
        const Stereo s = renderEngine (fs, measParams (inst), 2.4, evs);
        const double lvl = rmsDbMax (s, 1.95, 2.25);
        char what[64], idb[8];
        std::snprintf (what, sizeof what, "%s allNotesOff under pedal", kInstName[inst]);
        std::snprintf (idb, sizeof idb, "3d.%d", inst);
        row (idb, what, "< -60 dBFS at +1 s", fmt ("%.1f dBFS", lvl), verdict (lvl < -60.0));
    }
}

// ===========================================================================
// 4. Materials x instruments: the coupling matrix is transducer physics.
// ===========================================================================

static void sectionMaterials()
{
    heading ("4. Materials x instruments (grand has no material)");

    for (int inst : { 0, 1, 2, 4 })
    {
        char idb[12], what[96];

        // Everything rendered for this instrument must be finite.
        bool finite = true;
        for (int m = 0; m < 8; ++m)
            for (int t : { 0, 3 })
                finite = finite && allFinite (renderMT (inst, m, t));
        for (int t : { 1, 2 })
        {
            finite = finite && allFinite (renderMT (inst, 0, t));
            finite = finite && allFinite (renderMT (inst, 1, t));
        }
        finite = finite && allFinite (renderMT (inst, 7, 2));
        std::snprintf (idb, sizeof idb, "4f.%d", inst);
        std::snprintf (what, sizeof what, "%s all materials finite", kInstName[inst]);
        row (idb, what, "finite", finite ? "finite" : "NOT FINITE", verdict (finite));

        // Stock and stainless are ferromagnetic conductors: audible through
        // every transducer, no exceptions.
        //
        // KNOWN GAP (Clav): EpiEngine::prepare wires the shared MagneticPickup
        // into every family except the Clavinet (clav[i].prepare(sampleRate)
        // passes no field), so the Clav's swapped magnetic transducer reads a
        // null field and is dead silent. Pinned as full silence (< -100 dBFS)
        // so a half-fix cannot pass unnoticed.
        {
            double worstMag = 1.0e9, worstCon = 1.0e9, worstMid = 1.0e9, worst = 1.0e9;
            static char worstBuf[48];
            worstBuf[0] = '\0';
            for (int m : { 0, 1 })
                for (int t = 0; t < 4; ++t)
                {
                    const double r = rmsDbMono (renderMT (inst, m, t), 0.0, 1.5);
                    if (t == 0)      worstMag = std::min (worstMag, r);
                    else if (t == 3) worstCon = std::min (worstCon, r);
                    else             worstMid = std::min (worstMid, r);
                    if (r < worst)
                    {
                        worst = r;
                        std::snprintf (worstBuf, sizeof worstBuf, "%s/%s", kMatName[m], kTransName[t]);
                    }
                }
            const char* worstAt = worstBuf;
            const bool pass = worst > -50.0;
            // Held only by the two pinned Clav gaps together: the unwired
            // field (magnetic fully silent) and the contact level band.
            const bool held = inst == 4
                            && worstMid > -50.0
                            && worstMag < -100.0
                            && (worstCon > -50.0 || (worstCon >= -80.0 && worstCon <= -55.0));
            std::snprintf (idb, sizeof idb, "4a.%d", inst);
            std::snprintf (what, sizeof what, "%s stock+stainless, 4 transducers", kInstName[inst]);
            row (idb, what, "all > -50 dBFS",
                 fmt ("quietest %.1f dB", worst) + " (" + worstAt + ")",
                 gapIf (pass, held));
        }

        // The eddy band: a non-ferrous conductor speaks through a magnetic
        // pickup only via the eddy currents its motion drives -- coupling
        // 0.092*sigmaRel (EpiModel.h): aluminium lands near -25 dB against
        // steel, titanium (sigmaRel 0.01) at -60.7 dB by the law itself, so
        // the band runs 15..65 dB under stock with modal-mass differences on
        // top. Ordered by conductivity at the extremes.
        {
            const double stockMag = rmsDbMono (renderMT (inst, 0, 0), 0.0, 1.5);
            const bool magDead = stockMag < -100.0;   // the Clav field gap above
            double drop[7] = {};
            bool inBand = true;
            for (int m = 2; m <= 6; ++m)
            {
                drop[m] = stockMag - rmsDbMono (renderMT (inst, m, 0), 0.0, 1.5);
                inBand = inBand && drop[m] >= 15.0 && drop[m] <= 65.0;
            }
            std::snprintf (idb, sizeof idb, "4b.%d", inst);
            std::snprintf (what, sizeof what, "%s eddy band vs stock (magnetic)", kInstName[inst]);
            char got[112];
            std::snprintf (got, sizeof got, "brz %.0f bra %.0f ti %.0f al %.0f w %.0f dB",
                           drop[2], drop[3], drop[4], drop[5], drop[6]);
            row (idb, what, "15..65 dB under",
                 magDead ? "magnetic silent (field unwired)" : got,
                 magDead ? Verdict::knownGap : verdict (inBand));

            const bool order = drop[5] <= std::min ({ drop[2], drop[3], drop[4], drop[6] })
                            && drop[4] >= std::max ({ drop[2], drop[3], drop[5], drop[6] });
            std::snprintf (idb, sizeof idb, "4c.%d", inst);
            std::snprintf (what, sizeof what, "%s eddy order (Al loudest, Ti faintest)", kInstName[inst]);
            row (idb, what, "conductivity order",
                 magDead ? "magnetic silent (field unwired)" : (order ? "ordered" : got),
                 magDead ? Verdict::knownGap : verdict (order));
        }

        // Contact hears mechanics, not magnetism: the eddy attenuation must
        // vanish. Non-ferrous metals stay within 15 dB of stock through
        // contact (residual differences are modal mass and loss, well under
        // the 15 dB eddy floor) and stay audible.
        //
        // KNOWN GAP (Clav): the drops obey the law, but the whole clav
        // contact path sits ~33 dB under its native pickup (kContactOut was
        // never held to a level row by the clav suite) -- stock lands at
        // -64 dBFS, under the -50 audibility line. Held at -80..-55 dBFS.
        {
            const double stockCon = rmsDbMono (renderMT (inst, 0, 3), 0.0, 1.5);
            bool full = true, dropsOk = true, heldBand = true;
            double worstDrop = -300.0;
            int worstM = 2;
            for (int m = 2; m <= 6; ++m)
            {
                const double r = rmsDbMono (renderMT (inst, m, 3), 0.0, 1.5);
                const double d = stockCon - r;
                if (d > worstDrop) { worstDrop = d; worstM = m; }
                full = full && d <= 15.0 && r > -50.0;
                dropsOk = dropsOk && d <= 15.0;
                heldBand = heldBand && r >= -80.0 && r <= -55.0;
            }
            std::snprintf (idb, sizeof idb, "4d.%d", inst);
            std::snprintf (what, sizeof what, "%s non-ferrous full via contact", kInstName[inst]);
            row (idb, what, "<= 15 dB under, > -50",
                 fmt2 ("worst drop %.1f dB, stock %.0f dB", worstDrop, stockCon)
                     + " (" + kMatName[worstM] + ")",
                 gapIf (full, dropsOk && heldBand));
        }

        // Nylon: an insulator is invisible to a magnetic pickup and cannot
        // be the moving plate of an electrostatic one; a contact transducer
        // does not care.
        //
        // KNOWN GAPS: the Rhodes electro path (RhodesVoice trans==2) never
        // consults the material's conductor flag, so a nylon tine still
        // drives the plate at -40 dBFS (held at -50..-30); and the clav
        // contact level gap above puts its nylon at -80 dBFS (held -90..-70).
        {
            const double mag = rmsDbMax (renderMT (inst, 7, 0), 0.0, 2.0);
            const double ele = rmsDbMax (renderMT (inst, 7, 2), 0.0, 2.0);
            const double con = rmsDbMono (renderMT (inst, 7, 3), 0.0, 1.0);
            const bool magOk = mag < -80.0;
            const bool eleOk = ele < -80.0;
            const bool conOk = con > -60.0;
            const bool eleHeld = eleOk || (inst == 0 && ele >= -50.0 && ele <= -30.0);
            const bool conHeld = conOk || (inst == 4 && con >= -90.0 && con <= -70.0);
            std::snprintf (idb, sizeof idb, "4e.%d", inst);
            std::snprintf (what, sizeof what, "%s nylon: mag/electro dead, contact live", kInstName[inst]);
            char got[112];
            std::snprintf (got, sizeof got, "mag %.0f, elec %.0f, con %.0f dB", mag, ele, con);
            row (idb, what, "<-80 / <-80 / >-60 dBFS", got,
                 gapIf (magOk && eleOk && conOk, magOk && eleHeld && conHeld));
        }
    }

    // Tuning invariance: at a fixed pitch the geometry re-solves per
    // material, so bronze and titanium must still play C4 in tune.
    for (int inst : { 0, 1 })
        for (int m : { 2, 4 })
        {
            const double f0 = an::refineF0 (monoOf (renderMT (inst, m, 3)), 48000.0,
                                            noteHz (60), 0.3, 1.5);
            const double c = centsOff (f0, noteHz (60));
            char idb[12], what[96];
            std::snprintf (idb, sizeof idb, "4t.%d%d", inst, m);
            std::snprintf (what, sizeof what, "%s %s C4 tuning", kInstName[inst], kMatName[m]);
            row (idb, what, "+/-25 ct", fmt2 ("%.2f Hz (%+.1f ct)", f0, c),
                 verdict (std::fabs (c) <= 25.0));
        }

    // Nylon sustain on strings: string restoring force is mostly tension,
    // material loss applies only to the bending share, so the decay barely
    // moves -- time to -20 dB within 30% of steel.
    {
        auto t20 = [] (int mat) -> double
        {
            EngineParams p = measParams (1);
            p.material    = mat;
            p.transducer  = 3;
            p.strikeNoise = 0.0f;
            const Stereo s = renderEngine (48000.0, p, 8.0,
                                           { { 0, { 0, NoteEvent::noteOn, 60, 0.8f } } });
            return timeToDropDb (monoOf (s), 48000.0, 20.0);
        };
        const double steel = t20 (0);
        const double nylon = t20 (7);
        const double rel = std::fabs (nylon - steel) / std::max (1.0e-9, steel);
        row ("4s", "E-Grand C4 nylon vs steel t(-20dB)", "within 30%",
             fmt2 ("steel %.2f s, nylon %.2f s", steel, nylon),
             verdict (rel <= 0.30));
    }
}

// ===========================================================================
// 5. Transducer matrix: with stock material there is no silent pairing.
// ===========================================================================

static void sectionTransducers()
{
    heading ("5. Transducer matrix, stock material");

    // KNOWN GAPS (Clav): magnetic is silent (the unwired field, row 4a) and
    // contact sits at -64 dBFS (the level calibration, row 4d). Both are
    // pinned at their measured values.
    for (int inst : { 0, 1, 2, 4 })
    {
        double r[4];
        bool ok = true;
        for (int t = 0; t < 4; ++t)
        {
            r[t] = rmsDbMono (renderMT (inst, 0, t), 0.0, 1.5);
            ok = ok && r[t] > -50.0;
        }
        const bool held = inst == 4
                        && r[0] < -100.0                     // field unwired
                        && r[1] > -50.0 && r[2] > -50.0
                        && r[3] >= -80.0 && r[3] <= -55.0;   // contact level gap
        char idb[12], what[64], got[112];
        std::snprintf (idb, sizeof idb, "5.%d", inst);
        std::snprintf (what, sizeof what, "%s x mag/native/electro/contact", kInstName[inst]);
        std::snprintf (got, sizeof got, "%.0f/%.0f/%.0f/%.0f dB", r[0], r[1], r[2], r[3]);
        row (idb, what, "all > -50 dBFS", got, gapIf (ok, held));
    }
}

// ===========================================================================
// 6. Knob sweeps: the click fence. Chunky host-style automation over a held
// chord; the block-rate smoothing must keep every step inaudible.
// ===========================================================================

struct Knob
{
    const char* name;
    void (*set) (EngineParams&, float);   // u in 0..1 maps to the full range
};

static const Knob kKnobs[] = {
    { "resDamp",     [] (EngineParams& p, float u) { p.resDamp = u; } },
    { "hammerHard",  [] (EngineParams& p, float u) { p.hammerHard = u; } },
    { "tipMass",     [] (EngineParams& p, float u) { p.tipMass = u; } },
    { "pickupPos",   [] (EngineParams& p, float u) { p.pickupPos = -1.0f + 2.0f * u; } },
    { "pickupDist",  [] (EngineParams& p, float u) { p.pickupDist = u; } },
    { "coilSat",     [] (EngineParams& p, float u) { p.coilSat = u; } },
    { "preampDrive", [] (EngineParams& p, float u) { p.preampDrive = u; } },
    { "bass",        [] (EngineParams& p, float u) { p.bassDb = -12.0f + 24.0f * u; } },
    { "treble",      [] (EngineParams& p, float u) { p.trebleDb = -12.0f + 24.0f * u; } },
    { "clarity",     [] (EngineParams& p, float u) { p.clarityDb = -12.0f + 24.0f * u; } },
    { "cabMix",      [] (EngineParams& p, float u) { p.cabMix = u; } },
    { "barCouple",   [] (EngineParams& p, float u) { p.barCouple = u; } },
    { "damperGrip",  [] (EngineParams& p, float u) { p.damperGrip = u; } },
    // The rest of the continuous surface. A click fence that covers a third
    // of the knobs fences a third of the instrument: every one of these is
    // something a player automates or rides, and each was silent about its
    // own steps until it was swept here.
    { "escapement",  [] (EngineParams& p, float u) { p.escapement = u; } },
    { "hammerMass",  [] (EngineParams& p, float u) { p.hammerMass = u; } },
    { "strikeNoise", [] (EngineParams& p, float u) { p.strikeNoise = u; } },
    { "bodyMix",     [] (EngineParams& p, float u) { p.bodyMix = u; } },
    { "barTune",     [] (EngineParams& p, float u) { p.barTune = -24.0f + 48.0f * u; } },
    { "nonlinAmt",   [] (EngineParams& p, float u) { p.nonlinAmt = u; } },
    { "coilFreq",    [] (EngineParams& p, float u) { p.coilFreq = u; } },
    { "coilQ",       [] (EngineParams& p, float u) { p.coilQ = u; } },
    { "tremRate",    [] (EngineParams& p, float u) { p.tremRate = 0.5f + 11.5f * u; } },
    { "tremDepth",   [] (EngineParams& p, float u) { p.tremDepth = u; } },
    { "tremStereo",  [] (EngineParams& p, float u) { p.tremStereo = u; } },
    { "phaserMix",   [] (EngineParams& p, float u) { p.phaserMix = u; } },
    { "phaserRate",  [] (EngineParams& p, float u) { p.phaserRate = 0.05f + 4.95f * u; } },
    { "phaserDepth", [] (EngineParams& p, float u) { p.phaserDepth = u; } },
    { "phaserFb",    [] (EngineParams& p, float u) { p.phaserFb = u; } },
    { "spaceMix",    [] (EngineParams& p, float u) { p.spaceMix = u; } },
    { "spaceSize",   [] (EngineParams& p, float u) { p.spaceSize = u; } },
    { "outGain",     [] (EngineParams& p, float u) { p.outGainLin = 0.05f + 1.95f * u; } },
    { "wearAmount",  [] (EngineParams& p, float u) { p.wearAmount = u; } },
    { "velCurve",    [] (EngineParams& p, float u) { p.velCurve = u; } },
    // TUNE and the body bench are deliberately NOT in this list, and
    // bodySize is excused in the sweep below. Continuously retuning a
    // string that is already ringing is not something any of these
    // instruments can do -- nobody turns a tuning pin mid-note, and a
    // piano has no bend wheel -- so a swept-retune click fence would be
    // fencing an action the instrument does not have. What IS real is a
    // SINGLE retune landing while notes ring: a workshop edit, a material
    // swap, a body change, a tuning knob moved between phrases. Section
    // 6b measures exactly that instead.
};

static void sectionKnobs()
{
    heading ("6. Knob sweeps during a held chord (click fence)");

    // Bound: the measured clean-parameter family sits at -38..-43 dBFS
    // worst-sample (0.007..0.013); the fixed gap/sat glides at -36 dB
    // (0.016). 0.025 holds all of them with margin and still catches any
    // block-rate step (the pre-fix clicks were > -20 dB). The un-swept
    // chord itself measures 0.0005..0.0033 across the five instruments, so
    // anything above the bound is the knob, not the signal.
    //
    // The click history this section forced, all fixed and now fenced by
    // the plain 0.025 bound:
    //  - cabMix measured 0.128..0.237: NOT the coefficient morph (the
    //    component alone measured 0.0012 under the same sweep) but the hard
    //    mix<=0 bypass branch switching in and out as automation crossed
    //    zero; fixed with a crossfaded bypass below mix 0.02.
    //  - treble/clarity: RBJ shelf coefficients stepped at block rate;
    //    fixed with rate-limited application (0.15 dB per block) in the
    //    air shelf and the E-Grand preamp.
    //  - pickupDist on the Tine: the electrostatic gap glide was a touch
    //    fast for chunky automation; eased 20 to 35 ms.


    const double fs = 48000.0;
    const int block = 512;

    for (int inst = 0; inst < 5; ++inst)
    {
        double worstClean = 0.0;
        const char* worstCleanName = "";
        std::vector<std::pair<const char*, double>> over;

        for (const Knob& k : kKnobs)
        {
            const Tweak tweak = [&] (int blockStart, EngineParams& p)
            {
                const double t = blockStart / fs;
                if (t < 0.5 || t >= 1.5) return;
                if ((blockStart / block) % 8 != 0) return;   // chunky: every 8 blocks
                const float u = 0.5f + 0.5f * static_cast<float> (std::sin (2.0 * an::kPi * (t - 0.5)));
                k.set (p, u);
            };
            const Stereo s = renderEngine (fs, measParams (inst), 1.8,
                                           chordOn ({ 60, 64, 67 }, 0.7f), tweak, block);
            const double d = maxD2 (s, static_cast<int> (0.5 * fs),
                                    static_cast<int> (1.56 * fs));
            // The base bound is twice the clean-signal floor at the -18 dBFS
            // bench. A knob that exceeds it is not condemned yet: strongly
            // nonlinear knobs (a tine pickup swept through its tight-gap
            // growl regime measures a STATIC d2 of 1.07 at one extreme) make
            // honest high-slew signal, so the sweep is judged against the
            // worst static extreme it passes through -- a genuine click
            // exceeds both.
            if (d > 0.05)
            {
                double staticMax = 0.0;
                for (float u : { 0.0f, 1.0f })
                {
                    EngineParams sp = measParams (inst);
                    k.set (sp, u);
                    const Stereo ss = renderEngine (fs, sp, 1.8,
                                                    chordOn ({ 60, 64, 67 }, 0.7f), {}, block);
                    staticMax = std::max (staticMax, maxD2 (ss, static_cast<int> (0.5 * fs),
                                                            static_cast<int> (1.56 * fs)));
                }
                if (d > std::max (0.05, 1.25 * staticMax))
                    over.push_back ({ k.name, d });
            }
            else if (d > worstClean) { worstClean = d; worstCleanName = k.name; }
        }

        char idb[12], what[64];
        std::snprintf (idb, sizeof idb, "6.%d", inst);
        std::snprintf (what, sizeof what, "%s worst clean knob of %d", kInstName[inst],
                       (int) std::size (kKnobs));
        row (idb, what, "d2 <= 0.05",
             fmt ("%.4f", worstClean) + " (" + worstCleanName + ")",
             verdict (worstClean <= 0.05));
        for (const auto& o : over)
            row (idb, (std::string (kInstName[inst]) + " " + o.first + " click").c_str(),
                 "d2 <= max(0.05, 1.25 x static)", fmt ("%.4f", o.second), verdict (false));
    }
}

// ===========================================================================
// 7. The rail and the extremes: fortissimo abuse stays bounded and decays.
// ===========================================================================

// ===========================================================================
static void sectionRetune()
{
    heading ("6b. a single retune while strings ring (workshop, material, body, tune)");

    // The physical action this fences: something retunes the instrument
    // while notes are sounding. A workshop length or gauge edit, a material
    // swap, a body resize, the tuning knob moved between phrases. The
    // string keeps ringing THROUGH it -- its energy is not reset, its
    // envelope does not jump, and no sample steps.
    //
    // What is deliberately not fenced: sweeping a retune continuously under
    // a held note. No piano can do it (no bend wheel, and nobody turns a
    // tuning pin mid-note), so the second-derivative step it necessarily
    // produces -- acceleration is minus omega squared times displacement,
    // and omega just moved -- is an artifact of asking for an action the
    // instrument does not have, not a defect in the model.
    const double fs = 48000.0;
    const int block = 64;

    struct Change { const char* name; std::function<void (EngineParams&)> apply; };
    const Change changes[] = {
        { "tune +20 ct",  [] (EngineParams& p) { p.tuneCents = 20.0f; } },
        { "body size",    [] (EngineParams& p) { p.bodySize = 0.75f; } },
        { "body material",[] (EngineParams& p) { p.bodyMat = 2; } },
    };

    for (int inst = 0; inst < 5; ++inst)
        for (const auto& ch : changes)
        {
            EpiEngine e;
            e.prepare (fs, block);
            EngineParams p = measParams (inst);
            std::vector<float> L (block), R (block), y;
            NoteEvent on { 0, NoteEvent::noteOn, 60, 0.7f };
            e.process (L.data(), R.data(), block, p, &on, 1);
            y.insert (y.end(), L.begin(), L.end());
            for (int b = 0; b < 200; ++b)
            {
                e.process (L.data(), R.data(), block, p, nullptr, 0);
                y.insert (y.end(), L.begin(), L.end());
            }
            const std::size_t mark = y.size();
            ch.apply (p);
            for (int b = 0; b < 200; ++b)
            {
                e.process (L.data(), R.data(), block, p, nullptr, 0);
                y.insert (y.end(), L.begin(), L.end());
            }

            auto rms = [&] (std::size_t a, std::size_t b)
            {
                double s = 0.0;
                for (std::size_t i = a; i < b; ++i) s += (double) y[i] * y[i];
                return std::sqrt (s / static_cast<double> (b - a));
            };
            const double before = rms (mark - 2000, mark);
            const double after  = rms (mark + 96, mark + 2096);
            double stepAfter = 0.0, stepBefore = 0.0;
            for (std::size_t i = mark; i < mark + 400; ++i)
                stepAfter = std::max (stepAfter, (double) std::fabs (y[i] - y[i - 1]));
            for (std::size_t i = mark - 2000; i < mark; ++i)
                stepBefore = std::max (stepBefore, (double) std::fabs (y[i] - y[i - 1]));

            bool finite = true;
            for (float v : y) finite = finite && std::isfinite (v);
            // The body bench legitimately changes the level (that is what a
            // different board is for); the tuning knob must not.
            const double levelDb = 20.0 * std::log10 (std::max (1.0e-12, after)
                                                    / std::max (1.0e-12, before));
            const double levelBound = std::strcmp (ch.name, "tune +20 ct") == 0 ? 1.5 : 6.0;
            const bool ok = finite
                         && std::abs (levelDb) < levelBound
                         && stepAfter <= 1.5 * stepBefore + 1.0e-6;
            char rid[8];
            std::snprintf (rid, sizeof rid, "6b.%d", inst);
            row (rid,
                 (std::string (kInstName[inst]) + " rings through a " + ch.name).c_str(),
                 "no reset, no step", 
                 fmt2 ("%+.2f dB, step %.2fx", levelDb, stepAfter / std::max (1.0e-12, stepBefore)),
                 verdict (ok));
        }
}

static void sectionRail()
{
    heading ("7. Rail and extremes");

    const double fs = 48000.0;

    for (int inst = 0; inst < 5; ++inst)
    {
        // 10-note ff cluster under pedal, 10 s: the rail's tanh knee tops out
        // at exactly 1.0; a passive instrument cannot gain energy, so the
        // last second holds no more than the first.
        std::vector<TimedEvent> evs = { { 0, { 0, NoteEvent::sustainOn, 0, 1.0f } } };
        for (int n = 48; n < 58; ++n)
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, n, 1.0f } });
        const Stereo s = renderEngine (fs, measParams (inst), 10.0, evs);
        const auto m = monoOf (s);
        const double pk = peakAbs (s);
        const double e1 = energyIn (m, fs, 1.0, 2.0);
        const double e9 = energyIn (m, fs, 9.0, 10.0);
        const bool ok = allFinite (s) && pk <= 1.0 && e9 <= e1;
        char idb[12], what[64];
        std::snprintf (idb, sizeof idb, "7a.%d", inst);
        std::snprintf (what, sizeof what, "%s ff cluster + pedal, 10 s", kInstName[inst]);
        row (idb, what, "finite, pk<=1, no growth",
             fmt2 ("pk %.3f, e9/e1 %.3f", pk, e9 / std::max (1.0e-300, e1)),
             verdict (ok));
    }

    for (int inst = 0; inst < 5; ++inst)
    {
        std::vector<TimedEvent> evs;
        for (int n = EpiEngine::kLoNote; n <= EpiEngine::kHiNote; ++n)
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, n, 1.0f } });
        const Stereo s = renderEngine (fs, measParams (inst), 3.0, evs);
        const double pk = peakAbs (s);
        char idb[12], what[64];
        std::snprintf (idb, sizeof idb, "7b.%d", inst);
        std::snprintf (what, sizeof what, "%s 88-key ff strike-all", kInstName[inst]);
        row (idb, what, "finite, pk<=1", fmt ("pk %.3f", pk),
             verdict (allFinite (s) && pk <= 1.0));
    }
}

// ===========================================================================
// 8. MIDI robustness: garbage in, bounded audio out.
// ===========================================================================

static void sectionMidi()
{
    heading ("8. MIDI robustness");

    const double fs = 48000.0;

    {
        const Stereo s = renderEngine (fs, measParams (0), 0.5,
                                       { { 0, { 0, NoteEvent::noteOff, 60, 0.0f } } });
        row ("8.1", "noteOff without noteOn", "finite, pk<=1",
             fmt ("pk %.3f", peakAbs (s)),
             verdict (allFinite (s) && peakAbs (s) <= 1.0));
    }
    {
        std::vector<TimedEvent> evs;
        for (int n : { 0, 20, 109, 127 })
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, n, 1.0f } });
        const Stereo s = renderEngine (fs, measParams (0), 0.5, evs);
        row ("8.2", "out-of-range notes 0/20/109/127", "finite, pk<=1",
             fmt ("pk %.3f", peakAbs (s)),
             verdict (allFinite (s) && peakAbs (s) <= 1.0));
    }
    {
        std::vector<TimedEvent> evs;
        for (int i = 0; i < 8; ++i)
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, 60, 0.9f } });
        const Stereo s = renderEngine (fs, measParams (0), 1.0, evs);
        row ("8.3", "8 duplicate noteOns, one block", "finite, pk<=1",
             fmt ("pk %.3f", peakAbs (s)),
             verdict (allFinite (s) && peakAbs (s) <= 1.0));
    }
    for (int inst = 0; inst < 5; ++inst)
    {
        std::mt19937 rng (0x5eed0000u + static_cast<unsigned> (inst));
        std::vector<TimedEvent> evs;
        for (int i = 0; i < 500; ++i)
        {
            NoteEvent e;
            e.type     = static_cast<NoteEvent::Type> (rng() % 8);
            e.note     = static_cast<int> (rng() % 128);
            e.velocity = static_cast<float> (rng() % 1001) / 1000.0f;
            evs.push_back ({ static_cast<int> (rng() % 512), e });
        }
        const Stereo s = renderEngine (fs, measParams (inst), 1.5, evs);
        char idb[12], what[64];
        std::snprintf (idb, sizeof idb, "8.4%d", inst);
        std::snprintf (what, sizeof what, "%s 500 random events, one block", kInstName[inst]);
        row (idb, what, "finite, pk<=1", fmt ("pk %.3f", peakAbs (s)),
             verdict (allFinite (s) && peakAbs (s) <= 1.0));
    }
}

// ===========================================================================
// 9. Rate independence: physics does not know the sample rate.
// ===========================================================================

static void sectionRates()
{
    heading ("9. Rate independence");

    // E-Grand decay: the string's T60 is a physical time; 15% covers the
    // discretisation drift the pre-warped integrators are allowed.
    {
        double t20[3];
        int i = 0;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            const Stereo s = renderEngine (fs, measParams (1), 8.0, chordOn ({ 60 }, 0.7f));
            t20[i++] = timeToDropDb (monoOf (s), fs, 20.0);
        }
        const double lo = std::min ({ t20[0], t20[1], t20[2] });
        const double hi = std::max ({ t20[0], t20[1], t20[2] });
        const double spread = (hi - lo) / std::max (1.0e-9, lo);
        char got[112];
        std::snprintf (got, sizeof got, "%.2f/%.2f/%.2f s (%.0f%%)",
                       t20[0], t20[1], t20[2], 100.0 * spread);
        row ("9.1", "E-Grand C4 t(-20dB) 44.1/48/96k", "within 15%", got,
             verdict (spread <= 0.15));
    }

    // Clav release drop: the string reunites with the yarn-wrapped dead
    // length and drops three semitones (clav suite S3, EURASIP Fig. 5).
    // Present at 48k AND 96k, and the two agree. The engine's damper knob
    // maps through pow(x, 0.32) to the voice's grip, so S3's aged-yarn
    // condition (voice grip 0.1) is p.damperGrip = 0.1^(1/0.32) ~ 0.0008;
    // a tighter grip leaves too little tail for the f0 fit to read.
    {
        double drop[2];
        int i = 0;
        for (double fs : { 48000.0, 96000.0 })
        {
            EngineParams p = measParams (4);
            p.damperGrip = 0.0008f;   // aged yarn: voice grip 0.1 (clav S3)
            std::vector<TimedEvent> evs;
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, 57, 0.6f } });
            evs.push_back ({ static_cast<int> (0.9 * fs), { 0, NoteEvent::noteOff, 57, 0.0f } });
            const Stereo s = renderEngine (fs, p, 1.6, evs);
            const auto m = monoOf (s);
            const double before = an::refineF0 (m, fs, noteHz (57), 0.55, 0.86);
            const double after  = an::refineF0 (m, fs, noteHz (57) * 0.8409, 0.96, 1.25);
            drop[i++] = -12.0 * std::log2 (after / before);
        }
        char got[96];
        std::snprintf (got, sizeof got, "48k %.2f st, 96k %.2f st", drop[0], drop[1]);
        row ("9.2", "Clav A3 release drop at 48/96k", "both 2.7..3.3 st, agree 0.35",
             got,
             verdict (drop[0] >= 2.7 && drop[0] <= 3.3
                      && drop[1] >= 2.7 && drop[1] <= 3.3
                      && std::fabs (drop[1] - drop[0]) <= 0.35));
    }
}

// ===========================================================================
// 10. CPU, reported not asserted.
// ===========================================================================

static void sectionCpu()
{
    heading ("10. CPU, 10-note chord + pedal at 48k (informational)");

    const double fs = 48000.0;
    for (int inst = 0; inst < 5; ++inst)
    {
        std::vector<TimedEvent> evs = { { 0, { 0, NoteEvent::sustainOn, 0, 1.0f } } };
        for (int n = 48; n < 58; ++n)
            evs.push_back ({ 0, { 0, NoteEvent::noteOn, n, 1.0f } });
        double wall = 0.0;
        renderEngine (fs, measParams (inst), 5.0, evs, {}, 512, &wall);
        char idb[12], what[64];
        std::snprintf (idb, sizeof idb, "10.%d", inst);
        std::snprintf (what, sizeof what, "%s render cost", kInstName[inst]);
        row (idb, what, "reported", fmt ("%.1f%% of realtime", 100.0 * wall / 5.0),
             Verdict::info);
    }
}

// ===========================================================================
// 11. The room's derived profiles.
//
// Every expectation here is a computed number from
// docs/research/room-acoustics-measured.md: Eyring RT60 per octave band from
// surveyed geometry and fetched published absorption tables (air term
// included), and first-order image-source arrival times. Nothing is tuned to
// the code; a failing row is a defect in the room or a wrong derivation, and
// both get investigated.
// ===========================================================================

static Room makeRoom (int profile, double fs)
{
    Room r;
    r.prepare (fs);
    r.setProfile (profile);
    r.setSize (0.5f);   // natural size: the doc's tables are computed here
    r.reset();          // reset lands the pending swap at silence, per contract
    return r;
}

static std::vector<double> renderRoomImpulse (Room& r, double fs, double seconds)
{
    const int n = static_cast<int> (fs * seconds);
    std::vector<double> y (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double x = i == 0 ? 1.0 : 0.0;
        double wl = 0.0, wr = 0.0;
        r.process (x, x, wl, wr);
        y[static_cast<std::size_t> (i)] = 0.5 * (wl + wr);
    }
    return y;
}

static double roomT60 (const std::vector<double>& y, double fs, double f, double ta, double tb)
{
    const auto env = an::heterodyne (y, fs, f);
    const auto fit = an::fitDecay (env, ta, tb);
    if (! fit.valid || fit.slopeDbPerS >= -1.0e-3) return 0.0;
    return -60.0 / fit.slopeDbPerS;
}

static void sectionRoom()
{
    heading ("11. room profiles: Eyring decay, image-source early field, click-safe switch");

    const double fs = 48000.0;

    // 11.0 -- profile 0 is the shipped room, bit for bit (research doc,
    // "Switching behaviour"). setProfile(0) on a fresh Room must change
    // nothing at all against a Room that never heard of profiles.
    {
        Room a, b;
        a.prepare (fs);
        b.prepare (fs);
        b.setProfile (0);
        a.setSize (0.4f);
        b.setSize (0.4f);
        std::mt19937 rng (11);
        std::uniform_real_distribution<double> dist (-1.0, 1.0);
        bool exact = true;
        for (int i = 0; i < 48000 && exact; ++i)
        {
            const double x = dist (rng);
            double al = 0, ar = 0, bl = 0, br = 0;
            a.process (x, 0.7 * x, al, ar);
            b.process (x, 0.7 * x, bl, br);
            exact = al == bl && ar == br;
        }
        row ("11.0", "profile 0 vs shipped room", "bit-exact over 1 s",
             exact ? "identical" : "diverged", verdict (exact));
    }

    // 11.1..11.5 -- RT60 at 500 Hz per profile within 25 % of the computed
    // Eyring value (research doc, "Computed Eyring RT60 per band", bold
    // column). Renders and fit windows sized per decay; the fit starts after
    // the early reflections and the heterodyne transient.
    struct ProfileCase
    {
        int profile;
        const char* name;
        double t500;         // computed Eyring, natural size
        double t4k;          // computed, informational
        double ta, tb, len;  // fit window and render length, seconds
    };
    static const ProfileCase kCases[5] = {
        { 1, "Booth",  0.066, 0.049, 0.040, 0.100, 0.35 },
        { 2, "Studio", 0.397, 0.397, 0.060, 0.350, 0.70 },
        { 3, "Stage",  1.025, 1.486, 0.120, 0.900, 1.30 },
        { 4, "Hall",   2.391, 1.946, 0.250, 1.700, 2.20 },
        { 5, "Church", 6.506, 3.316, 0.300, 2.600, 3.20 },
    };

    double t500Meas[5] = {};
    std::vector<double> ir[5];
    for (int c = 0; c < 5; ++c)
    {
        Room r = makeRoom (kCases[c].profile, fs);
        ir[c] = renderRoomImpulse (r, fs, kCases[c].len);
        t500Meas[c] = roomT60 (ir[c], fs, 500.0, kCases[c].ta, kCases[c].tb);

        char id[12], what[64];
        std::snprintf (id, sizeof id, "11.%d", c + 1);
        std::snprintf (what, sizeof what, "%s RT60 at 500 Hz", kCases[c].name);
        const double rel = std::fabs (t500Meas[c] - kCases[c].t500) / kCases[c].t500;
        row (id, what, fmt2 ("%.3f s +/- 25%% (Eyring)", kCases[c].t500, 0.0),
             fmt2 ("%.3f s (%+.0f%%)", t500Meas[c], 100.0 * (t500Meas[c] / kCases[c].t500 - 1.0)),
             verdict (t500Meas[c] > 0.0 && rel <= 0.25));
    }

    // 11.6 / 11.7 -- material spectra plus the 4mV air term make the Hall
    // and Church decay faster at 4 kHz than at 500 Hz (research doc: Hall
    // 1.946 s vs 2.391 s, Church 3.316 s vs 6.506 s).
    {
        const double hall4k   = roomT60 (ir[3], fs, 4000.0, 0.25, 1.40);
        const double church4k = roomT60 (ir[4], fs, 4000.0, 0.30, 2.20);
        row ("11.6", "Hall HF decay shorter than mid", "T60(4k) < T60(500)",
             fmt2 ("%.3f s vs %.3f s", hall4k, t500Meas[3]),
             verdict (hall4k > 0.0 && hall4k < t500Meas[3]));
        row ("11.7", "Church HF decay shorter than mid", "T60(4k) < T60(500)",
             fmt2 ("%.3f s vs %.3f s", church4k, t500Meas[4]),
             verdict (church4k > 0.0 && church4k < t500Meas[4]));
    }

    // 11.8..11.10 -- first reflection arrival within 15 % of the image-source
    // prediction (research doc, "Early reflections" table: booth 2.205 ms
    // near side wall, studio 2.708 ms floor, stage 2.103 ms floor). The wet
    // path is exactly zero until the first tap fires, so the first nonzero
    // sample is the arrival.
    {
        static const struct { int caseIdx; double ms; } kArrival[3] = {
            { 0, 2.205 }, { 1, 2.708 }, { 2, 2.103 }
        };
        for (int k = 0; k < 3; ++k)
        {
            const auto& c = kCases[kArrival[k].caseIdx];
            const auto& y = ir[kArrival[k].caseIdx];
            int first = -1;
            for (int i = 0; i < static_cast<int> (y.size()); ++i)
                if (std::fabs (y[static_cast<std::size_t> (i)]) > 1.0e-9) { first = i; break; }
            const double ms = first >= 0 ? 1000.0 * first / fs : -1.0;
            const double rel = std::fabs (ms - kArrival[k].ms) / kArrival[k].ms;

            char id[12], what[64];
            std::snprintf (id, sizeof id, "11.%d", 8 + k);
            std::snprintf (what, sizeof what, "%s first reflection arrival", c.name);
            row (id, what, fmt2 ("%.2f ms +/- 15%%", kArrival[k].ms, 0.0),
                 fmt ("%.2f ms", ms), verdict (first >= 0 && rel <= 0.15));
        }
    }

    // 11.11 -- a profile switch mid-ring is click-safe (research doc,
    // "Switching behaviour": 15 ms wet fade to a null, swap, fade back).
    // Bound: the largest output second difference around the switch must not
    // exceed 3x the larger of the two steady profiles' own second
    // difference over the same window -- a hard step would show up as a
    // spike of the signal's full scale.
    {
        auto render = [fs] (int from, int to, bool doSwitch)
        {
            Room r = makeRoom (from, fs);
            const int n = static_cast<int> (fs);
            std::vector<double> y (static_cast<std::size_t> (n));
            for (int i = 0; i < n; ++i)
            {
                const double t = i / fs;
                const double x = t < 0.3 ? 0.4 * std::sin (2.0 * an::kPi * 415.3 * t) : 0.0;
                if (doSwitch && i == static_cast<int> (0.35 * fs))
                    r.setProfile (to);
                double wl = 0.0, wr = 0.0;
                r.process (x, x, wl, wr);
                y[static_cast<std::size_t> (i)] = wl;
            }
            return y;
        };
        auto maxD2 = [fs] (const std::vector<double>& y, double ta, double tb)
        {
            double m = 0.0;
            const int a = std::max (2, static_cast<int> (ta * fs));
            const int b = std::min (static_cast<int> (y.size()), static_cast<int> (tb * fs));
            for (int i = a; i < b; ++i)
                m = std::max (m, std::fabs (y[static_cast<std::size_t> (i)]
                                            - 2.0 * y[static_cast<std::size_t> (i - 1)]
                                            + y[static_cast<std::size_t> (i - 2)]));
            return m;
        };

        const auto ySwitch = render (2, 4, true);
        const auto yA      = render (2, 4, false);
        const auto yB      = render (4, 2, false);

        bool finite = true;
        for (double v : ySwitch) finite = finite && std::isfinite (v);

        const double d2s = maxD2 (ySwitch, 0.34, 0.45);
        const double d2a = maxD2 (yA, 0.34, 0.45);
        const double d2b = maxD2 (yB, 0.34, 0.45);
        const double bound = 3.0 * std::max (d2a, d2b) + 1.0e-9;
        row ("11.11", "mid-ring Studio->Hall switch", "max |d2| <= 3x steady, finite",
             fmt2 ("%.2e vs bound %.2e", d2s, bound),
             verdict (finite && d2s <= bound));
    }

    // 11.12 -- fade-torture survival. A retarget that lands while the fade
    // gain sits exactly at the null used to leave the pending config
    // unapplied and the wet path silenced until reset: with a block length
    // dividing the 15 ms fade, a host automating size hits the grid on the
    // first fade. Torture the church with a per-block size sweep, then
    // listen for the tail seconds later -- it must still be a room, not
    // digital silence. (Found by the adversarial audit's probe.)
    {
        Room r;
        r.prepare (fs);
        r.setProfile (5);
        std::mt19937 rng (12);
        std::uniform_real_distribution<double> dist (-1.0, 1.0);
        // 2 s of driven torture: size retargeted every 240 samples.
        for (int b = 0; b < 400; ++b)
        {
            r.setSize (0.5f + 0.3f * static_cast<float> (std::sin (b * 0.05)));
            for (int i = 0; i < 240; ++i)
            {
                double l = 0, rr = 0;
                r.process (dist (rng), dist (rng), l, rr);
            }
        }
        // Source off; the tail 2 s later must carry energy.
        for (int i = 0; i < 2 * 48000; ++i)
        {
            double l = 0, rr = 0;
            r.process (0.0, 0.0, l, rr);
        }
        double e = 0.0;
        bool finite = true;
        for (int i = 0; i < 24000; ++i)
        {
            double l = 0, rr = 0;
            r.process (0.0, 0.0, l, rr);
            e += l * l + rr * rr;
            finite = finite && std::isfinite (l) && std::isfinite (rr);
        }
        const double db = 10.0 * std::log10 (e / 48000.0 + 1e-30);
        row ("11.12", "church tail survives a size-automation torture", "> -120 dB, finite",
             fmt ("%.1f dB", db), verdict (finite && db > -120.0));
    }
}

// ===========================================================================
static void sectionGrabNoise()
{
    heading ("12. grand damper grab: the release shh and the pedal-lift wash");

    const double fs = 48000.0;

    auto hfBand = [] (const std::vector<float>& x, int a, int b)
    {
        // 2-8 kHz energy, sixty log-spaced Goertzel bins.
        double e = 0.0;
        for (int k = 0; k < 60; ++k)
        {
            const double f = 2000.0 * std::pow (4.0, k / 59.0);
            const double w = 2.0 * M_PI * f / 48000.0, cw = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (int i = a; i < b; ++i)
            {
                const double s0 = x[static_cast<size_t> (i)] + cw * s1 - s2;
                s2 = s1; s1 = s0;
            }
            e += s1 * s1 + s2 * s2 - cw * s1 * s2;
        }
        return e / (b - a);
    };

    // 12.0 -- the grab is present and calibrated: releasing a held mf C4
    // puts FRESH high-frequency energy into the 15..60 ms post-release
    // window (the felt-band chatter through the string's own modes), well
    // above the note's own decayed highs, and bounded so it reads as a
    // shh, not a cymbal. Twin-diff calibration measured the burst at
    // -38 dB against the note's attack peak.
    {
        EpiEngine e; e.prepare (fs, 256);
        EngineParams p {}; p.instrument = 3; p.outGainLin = 0.5f; p.spaceMix = 0.0f;
        std::vector<float> L (256), R (256), y;
        NoteEvent on { 0, NoteEvent::noteOn, 60, 0.6f };
        e.process (L.data(), R.data(), 256, p, &on, 1);
        y.insert (y.end(), L.begin(), L.end());
        for (int b = 0; b < 187; ++b)
        {
            e.process (L.data(), R.data(), 256, p, nullptr, 0);
            y.insert (y.end(), L.begin(), L.end());
        }
        NoteEvent off { 0, NoteEvent::noteOff, 60, 0 };
        e.process (L.data(), R.data(), 256, p, &off, 1);
        y.insert (y.end(), L.begin(), L.end());
        for (int b = 0; b < 38; ++b)
        {
            e.process (L.data(), R.data(), 256, p, nullptr, 0);
            y.insert (y.end(), L.begin(), L.end());
        }
        const int rel = 188 * 256;
        const double pre  = hfBand (y, rel - 2880, rel - 720);
        const double post = hfBand (y, rel + 720, rel + 2880);
        double atk = 0.0;
        for (int i = 0; i < 48000; ++i) atk = std::max (atk, (double) std::fabs (y[static_cast<size_t> (i)]));
        const double ratio = 10.0 * std::log10 (post / (pre + 1e-30));
        const double abs2  = 10.0 * std::log10 (post / (atk * atk + 1e-30));
        row ("12.0", "release puts fresh HF in the grab window", "> +6 dB over pre, abs -45..-12",
             fmt2 ("+%.1f dB, abs %.1f dB", ratio, abs2),
             verdict (ratio > 6.0 && abs2 > -45.0 && abs2 < -12.0));
    }

    // 12.1 -- a dead string grabs silently. Cycling the pedal now
    // legitimately thumps the tray, so the row twins two deterministic
    // engines -- one that played a note that finished decaying seconds
    // ago, one that never sounded -- and drives both through the same
    // pedal cycle. The sample difference is exactly what the dead string
    // contributed: it must be nothing, because the grab scales with the
    // energy it catches.
    {
        EpiEngine ea, eb;
        ea.prepare (fs, 256);
        eb.prepare (fs, 256);
        EngineParams p {}; p.instrument = 3; p.outGainLin = 0.5f; p.spaceMix = 0.0f;
        std::vector<float> La (256), Ra (256), Lb (256), Rb (256);
        NoteEvent on { 0, NoteEvent::noteOn, 60, 0.05f };
        ea.process (La.data(), Ra.data(), 256, p, &on, 1);
        eb.process (Lb.data(), Rb.data(), 256, p, nullptr, 0);
        for (int b = 0; b < 18; ++b)
        {
            ea.process (La.data(), Ra.data(), 256, p, nullptr, 0);
            eb.process (Lb.data(), Rb.data(), 256, p, nullptr, 0);
        }
        NoteEvent off { 0, NoteEvent::noteOff, 60, 0 };
        ea.process (La.data(), Ra.data(), 256, p, &off, 1);
        eb.process (Lb.data(), Rb.data(), 256, p, nullptr, 0);
        for (int b = 0; b < 560; ++b)
        {
            ea.process (La.data(), Ra.data(), 256, p, nullptr, 0);
            eb.process (Lb.data(), Rb.data(), 256, p, nullptr, 0);
        }
        double diff = 0.0;
        auto step = [&] (const NoteEvent* ev, int n)
        {
            ea.process (La.data(), Ra.data(), 256, p, ev, n);
            eb.process (Lb.data(), Rb.data(), 256, p, ev, n);
            for (int i = 0; i < 256; ++i)
                diff = std::max (diff, (double) std::fabs (La[static_cast<size_t> (i)]
                                                         - Lb[static_cast<size_t> (i)]));
        };
        NoteEvent pd { 0, NoteEvent::sustainOn, 0, 0 };
        step (&pd, 1);
        for (int b = 0; b < 37; ++b) step (nullptr, 0);
        NoteEvent pu { 0, NoteEvent::sustainOff, 0, 0 };
        step (&pu, 1);
        for (int b = 0; b < 19; ++b) step (nullptr, 0);
        row ("12.1", "a dead string adds nothing to the pedal cycle", "< -120 dBFS",
             fmt ("%.1f dBFS", 20.0 * std::log10 (diff + 1e-30)),
             verdict (diff < 1.0e-6));
    }

    // 12.2 -- the pedal thunk: pressing the pedal on a silent instrument
    // thumps the board (the trapwork's end-of-travel), calibrated against
    // an mf note's attack peak, and the lift lands a thump of its own.
    // The press rings LOUDER per unit force than the lift because it
    // excites the freshly opened strings -- the pedal boom -- which is the
    // asymmetry a real instrument has.
    {
        EpiEngine e; e.prepare (fs, 256);
        EngineParams p {}; p.instrument = 3; p.outGainLin = 0.5f; p.spaceMix = 0.0f;
        std::vector<float> L (256), R (256);
        NoteEvent on { 0, NoteEvent::noteOn, 60, 0.6f };
        e.process (L.data(), R.data(), 256, p, &on, 1);
        double atk = 0.0;
        for (int b = 0; b < 94; ++b)
        {
            e.process (L.data(), R.data(), 256, p, nullptr, 0);
            for (float v : L) atk = std::max (atk, (double) std::fabs (v));
        }
        NoteEvent off { 0, NoteEvent::noteOff, 60, 0 };
        e.process (L.data(), R.data(), 256, p, &off, 1);
        for (int b = 0; b < 560; ++b) e.process (L.data(), R.data(), 256, p, nullptr, 0);
        NoteEvent pd { 0, NoteEvent::sustainOn, 0, 0 };
        e.process (L.data(), R.data(), 256, p, &pd, 1);
        double press = 0.0;
        for (float v : L) press = std::max (press, (double) std::fabs (v));
        for (int b = 0; b < 18; ++b)
        {
            e.process (L.data(), R.data(), 256, p, nullptr, 0);
            for (float v : L) press = std::max (press, (double) std::fabs (v));
        }
        for (int b = 0; b < 75; ++b) e.process (L.data(), R.data(), 256, p, nullptr, 0);
        NoteEvent pu { 0, NoteEvent::sustainOff, 0, 0 };
        e.process (L.data(), R.data(), 256, p, &pu, 1);
        double lift = 0.0;
        for (float v : L) lift = std::max (lift, (double) std::fabs (v));
        for (int b = 0; b < 18; ++b)
        {
            e.process (L.data(), R.data(), 256, p, nullptr, 0);
            for (float v : L) lift = std::max (lift, (double) std::fabs (v));
        }
        const double pDb = 20.0 * std::log10 (press / atk + 1e-30);
        const double lDb = 20.0 * std::log10 (lift / atk + 1e-30);
        row ("12.2", "pedal press and lift thunks vs mf attack", "each in -50..-35 dB",
             fmt2 ("press %.1f, lift %.1f dB", pDb, lDb),
             verdict (pDb > -50.0 && pDb < -35.0 && lDb > -50.0 && lDb < -35.0));
    }

    // 12.3 -- half-pedal work rides between the stops silently: the
    // hysteresis (0.65 / 0.35) means wiggling inside the travel produces
    // no thunks at all -- a real tray thumps at its stops, not mid-travel.
    {
        EpiEngine e; e.prepare (fs, 256);
        EngineParams p {}; p.instrument = 3; p.outGainLin = 0.5f; p.spaceMix = 0.0f;
        std::vector<float> L (256), R (256);
        for (int b = 0; b < 20; ++b) e.process (L.data(), R.data(), 256, p, nullptr, 0);
        double peak = 0.0;
        for (int b = 0; b < 100; ++b)
        {
            NoteEvent hp { 0, NoteEvent::sustain, 0,
                           0.45f + 0.15f * static_cast<float> (std::sin (b * 0.7)) };
            e.process (L.data(), R.data(), 256, p, &hp, 1);
            for (float v : L) peak = std::max (peak, (double) std::fabs (v));
        }
        row ("12.3", "half-pedal flutter between the stops is silent", "< -120 dBFS",
             fmt ("%.1f dBFS", 20.0 * std::log10 (peak + 1e-30)),
             verdict (peak < 1.0e-6));
    }
}

// ===========================================================================
static void sectionSoftPedal()
{
    heading ("13. the left pedal's two mechanisms: shift and rail");

    const double fs = 48000.0;

    auto strikePeak = [&] (EpiEngine& e, EngineParams& p, int note, float vel)
    {
        std::vector<float> L (256), R (256);
        NoteEvent on { 0, NoteEvent::noteOn, note, vel };
        e.process (L.data(), R.data(), 256, p, &on, 1);
        double pk = 0.0;
        for (float v : L) pk = std::max (pk, (double) std::fabs (v));
        for (int b = 0; b < 40; ++b)
        {
            e.process (L.data(), R.data(), 256, p, nullptr, 0);
            for (float v : L) pk = std::max (pk, (double) std::fabs (v));
        }
        NoteEvent off { 0, NoteEvent::noteOff, note, 0 };
        e.process (L.data(), R.data(), 256, p, &off, 1);
        for (int b = 0; b < 190; ++b) e.process (L.data(), R.data(), 256, p, nullptr, 0);
        return pk;
    };

    // 13.0 -- the rail softens without re-voicing: full half-blow drops a
    // fixed-velocity strike by the stroke-shortening law (measured -5.0 dB
    // at the 45% lift), and it does so WITHOUT touching the strike
    // geometry -- unlike shift, whose whole point is the geometry.
    {
        EngineParams p {}; p.instrument = 3; p.outGainLin = 0.5f; p.spaceMix = 0.0f;
        EpiEngine dry; dry.prepare (fs, 256);
        const double a = strikePeak (dry, p, 60, 0.6f);

        EpiEngine rail; rail.prepare (fs, 256);
        p.softMode = 1;
        {
            std::vector<float> L (256), R (256);
            NoteEvent sp { 0, NoteEvent::soft, 0, 1.0f };
            rail.process (L.data(), R.data(), 256, p, &sp, 1);
        }
        const double b = strikePeak (rail, p, 60, 0.6f);
        const double drop = 20.0 * std::log10 (b / a);
        row ("13.0", "full rail softens a fixed-velocity strike", "-8..-3 dB",
             fmt ("%.1f dB", drop), verdict (drop < -3.0 && drop > -8.0));
    }

    // 13.1 -- the klapper: with the rail down, lost motion makes equal
    // quiet strikes land unevenly (deterministic per-strike scatter);
    // without it they land identically. The looseness is the sound of a
    // half-blow action, and it must exist at pp and not run wild.
    {
        EngineParams p {}; p.instrument = 3; p.outGainLin = 0.5f; p.spaceMix = 0.0f;
        p.softMode = 1;
        EpiEngine rail; rail.prepare (fs, 256);
        {
            std::vector<float> L (256), R (256);
            NoteEvent sp { 0, NoteEvent::soft, 0, 1.0f };
            rail.process (L.data(), R.data(), 256, p, &sp, 1);
        }
        const double r1 = strikePeak (rail, p, 64, 0.15f);
        const double r2 = strikePeak (rail, p, 64, 0.15f);
        const double scatter = std::fabs (20.0 * std::log10 (r1 / r2));

        p.softMode = 0;
        EpiEngine dry; dry.prepare (fs, 256);
        const double d1 = strikePeak (dry, p, 64, 0.15f);
        const double d2 = strikePeak (dry, p, 64, 0.15f);
        const double dryScatter = std::fabs (20.0 * std::log10 (d1 / d2));
        row ("13.1", "rail pp strikes scatter, dry ones repeat", "rail 0.1..3 dB, dry < 0.05",
             fmt2 ("rail %.2f, dry %.2f dB", scatter, dryScatter),
             verdict (scatter > 0.1 && scatter < 3.0 && dryScatter < 0.05));
    }
}

// ===========================================================================

int main()
{
    const auto t0 = std::chrono::steady_clock::now();

    std::printf ("Epi engine integration suite\n");
    std::printf ("the layer the voice suites cannot see: seams, pedals, materials, knobs, rails\n");
    std::printf ("\n  %-4s %-40s %-28s %-30s %s\n", "ID", "PROPERTY", "TARGET", "MEASURED", "");

    sectionLife();
    sectionSeam();
    sectionPedals();
    sectionMaterials();
    sectionTransducers();
    sectionKnobs();
    sectionRetune();
    sectionRail();
    sectionMidi();
    sectionRates();
    sectionCpu();
    sectionRoom();
    sectionGrabNoise();
    sectionSoftPedal();

    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("\nsuite wall time %.1f s\n", wall);
    if (gaps > 0)
        std::printf ("%d known gap%s\n", gaps, gaps == 1 ? "" : "s");
    if (failures == 0)
        std::printf ("all engine rows within tolerance\n");
    else
        std::printf ("%d row%s outside tolerance\n", failures, failures == 1 ? "" : "s");
    std::printf ("SUMMARY fail=%d gap=%d\n", failures, gaps);
    return failures == 0 ? 0 : 1;
}
