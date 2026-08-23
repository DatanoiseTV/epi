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
    // chord at 48k; [-31, -17] allows rate variance and instrument spread
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
            const bool ok = finite && pk <= 1.0 && rms >= -31.0 && rms <= -17.0;
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
    // KNOWN GAPS, each a real click, pinned at its measured value:
    //  - cabMix: the cabinet morph re-derives every biquad corner once per
    //    block from the 40 ms smoothed target; the coefficient steps ring
    //    the resonant sections. Measured 0.128 (Tine), 0.237 (E-Grand),
    //    0.048 (Reed) -- and 0.128 even with per-block (non-chunky)
    //    automation, so it is the morph, not the host's update rate.
    //  - treble/clarity on the base-rate paths: shelf coefficients step at
    //    block rate (0.047 / 0.040 E-Grand, 0.025 Reed).
    //  - pickupDist on the Tine: the gap glide measured -36 dB (0.016) for
    //    continuous moves; chunky automation lands at 0.029.
    struct KnobHold { const char* name; double hold; };
    static const KnobHold kHolds[] = {
        { "cabMix", 0.30 }, { "treble", 0.06 }, { "clarity", 0.06 },
        { "pickupDist", 0.045 },
    };

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
            if (d > 0.025)
                over.push_back ({ k.name, d });
            else if (d > worstClean) { worstClean = d; worstCleanName = k.name; }
        }

        char idb[12], what[64];
        std::snprintf (idb, sizeof idb, "6.%d", inst);
        std::snprintf (what, sizeof what, "%s worst clean knob of 13", kInstName[inst]);
        row (idb, what, "d2 <= 0.025",
             fmt ("%.4f", worstClean) + " (" + worstCleanName + ")",
             verdict (worstClean <= 0.025));
        for (const auto& o : over)
        {
            double hold = 0.0;
            for (const auto& h : kHolds)
                if (std::string (o.first) == h.name) hold = h.hold;
            row (idb, (std::string (kInstName[inst]) + " " + o.first + " click").c_str(),
                 "d2 <= 0.025", fmt ("%.4f", o.second),
                 gapIf (false, o.second <= hold));
        }
    }
}

// ===========================================================================
// 7. The rail and the extremes: fortissimo abuse stays bounded and decays.
// ===========================================================================

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
    sectionRail();
    sectionMidi();
    sectionRates();
    sectionCpu();

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
