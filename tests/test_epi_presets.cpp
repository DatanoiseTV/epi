/*
  Epi — factory preset verification suite.

  Every preset in src/epi/presets/PresetData.h is rendered here through the
  real engine, built through the same raw-to-engine mapping the plugin uses
  (epi/EngineParamMap.h), and held to bounds: finite output, legal peak, a
  stage-worthy RMS window, no stuck DC -- and, where a preset's name promises
  something measurable, a character row that measures it (a dark preset has a
  lower spectral centroid than its reference, a bark preset more high-band
  energy than its clean sibling, a tremolo preset amplitude modulation at its
  set rate, a phaser preset spectral movement). The bench presets get the
  same treatment against their own twins -- the identical preset with the
  bench overridden back to stock, which the deterministic render makes an
  exact baseline: the hardened-felt preset must show more post-release high
  tail, the worn-rail preset a louder release thud (isolated against a
  noise-free twin, at the thud's own level, since a full chord masks it),
  a swapped frame an audible change in the render, a scaled grand board a
  shifted low signature.

  The structural rows run first: every id a preset names must exist in the
  replica of the APVTS layout below, with its value inside the parameter's
  range, so a typo fails this suite instead of shipping as a silently dropped
  value. Material/transducer pairings are checked against the physics flags
  in EpiModel.h: a non-ferromagnetic resonator is invisible to a magnetic
  pickup, an insulator cannot be an electrostatic plate -- and the render
  rows would catch the resulting silence anyway.

  Build: part of ctest, target epi_preset_tests.
*/

#include "EpiAnalysis.h"
#include "epi/EngineParamMap.h"
#include "epi/dsp/EpiEngine.h"
#include "epi/dsp/EpiModel.h"
#include "epi/presets/PresetData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace epi;
namespace pd = epi::presetdata;
namespace an = epianalysis;

// ===========================================================================
// Reporting, in the house style of the acoustic suite.
// ===========================================================================

static int failures = 0;

static void heading (const char* s)
{
    std::printf ("\n%s\n", s);
    std::printf ("  ------------------------------------------------------------------------------------------\n");
}

static void row (const char* name, const char* what, const std::string& target,
                 const std::string& got, bool pass)
{
    if (! pass) ++failures;
    std::printf ("  %-15s %-32s %-24s %-20s %s\n",
                 name, what, target.c_str(), got.c_str(), pass ? "PASS" : "FAIL");
}

static std::string fmt (const char* f, double a)
{
    char b[96];
    std::snprintf (b, sizeof b, f, a);
    return b;
}

// ===========================================================================
// The APVTS layout, replicated from src/epi/ParameterIDs.h. Deliberately a
// second copy: this suite exists to catch drift between the preset data, the
// engine mapping and the layout, and a shared table cannot catch drift with
// itself.
// ===========================================================================

struct ParamSpec { const char* id; float def, lo, hi; };

static const ParamSpec kLayout[] = {
    { "tune",        0.0f, -100.0f, 100.0f },
    { "velCurve",    0.5f,    0.0f,   1.0f },
    { "hammerHard",  0.5f,    0.0f,   1.0f },
    { "hammerMass",  0.5f,    0.0f,   1.0f },
    { "escapement",  0.4f,    0.0f,   1.0f },
    { "strikeNoise", 0.22f,   0.0f,   1.0f },
    { "damperGrip",  0.6f,    0.0f,   1.0f },
    { "tipMass",     0.5f,    0.0f,   1.0f },
    { "resDamp",     0.35f,   0.0f,   1.0f },
    { "barCouple",   0.6f,    0.0f,   1.0f },
    { "barTune",     0.0f,  -24.0f,  24.0f },
    { "bodyMix",     0.25f,   0.0f,   1.0f },
    { "nonlinAmt",   0.5f,    0.0f,   1.0f },
    { "pickupPos",  -0.35f,  -1.0f,   1.0f },
    { "pickupDist",  0.35f,   0.0f,   1.0f },
    { "pickupSel",   1.0f,    0.0f,   3.0f },
    { "coilFreq",    0.5f,    0.0f,   1.0f },
    { "coilQ",       0.5f,    0.0f,   1.0f },
    { "coilSat",     0.25f,   0.0f,   1.0f },
    { "preampDrive", 0.3f,    0.0f,   1.0f },
    { "bass",        0.0f,  -12.0f,  12.0f },
    { "treble",      0.0f,  -12.0f,  12.0f },
    { "tremRate",    5.5f,    0.1f,  12.0f },
    { "tremDepth",   0.0f,    0.0f,   1.0f },
    { "tremStereo",  1.0f,    0.0f,   1.0f },
    { "cabMix",      0.5f,    0.0f,   1.0f },
    { "phaserMix",   0.0f,    0.0f,   1.0f },
    { "phaserRate",  0.40f,   0.02f,  8.0f },
    { "phaserDepth", 0.70f,   0.0f,   1.0f },
    { "phaserFb",    0.50f,   0.0f,   1.0f },
    { "spaceMix",    0.15f,   0.0f,   1.0f },
    { "spaceSize",   0.40f,   0.0f,   1.0f },
    { "outGain",     0.0f,  -24.0f,  12.0f },
    { "instrument",  0.0f,    0.0f,   4.0f },
    { "clarity",     0.0f,  -12.0f,  12.0f },
    { "material",    0.0f,    0.0f,   7.0f },
    { "clavSwitch",  0.0f,    0.0f,   3.0f },
    { "clavBrill",   0.0f,    0.0f,   1.0f },
    { "clavTreb",    0.0f,    0.0f,   1.0f },
    { "clavMed",     1.0f,    0.0f,   1.0f },
    { "clavSoft",    0.0f,    0.0f,   1.0f },
    { "bodyMat",     0.0f,    0.0f,   7.0f },
    { "bodySize",    0.5f,    0.0f,   1.0f },
    { "damperFelt",  0.0f,    0.0f,   3.0f },
    { "keyBed",      0.0f,    0.0f,   3.0f },
    { "hammerMat",   0.0f,    0.0f,   5.0f },
    { "roomProfile", 0.0f,    0.0f,   5.0f },
    { "softMode",    0.0f,    0.0f,   1.0f },
    { "wearAmount",  0.0f,    0.0f,   1.0f },
};

static const ParamSpec* findSpec (const char* id)
{
    for (const auto& s : kLayout)
        if (std::strcmp (s.id, id) == 0) return &s;
    return nullptr;
}

// A preset's complete resolved value set: defaults, then its departures.
static std::map<std::string, float> resolve (const pd::Preset& p)
{
    std::map<std::string, float> m;
    for (const auto& s : kLayout) m[s.id] = s.def;
    m["instrument"] = static_cast<float> (p.instrument);
    for (std::size_t i = 0; i < p.numValues; ++i)
        m[p.values[i].id] = p.values[i].value;
    return m;
}

// ===========================================================================
// Rendering and measurement.
// ===========================================================================

static constexpr double kFs    = 48000.0;
static constexpr int    kBlock = 256;
static constexpr int    kLen   = static_cast<int> (4.0 * kFs);

struct Measured
{
    bool   finite     = true;
    double peak       = 0.0;
    double rmsDb      = -300.0;
    double dcAbs      = 0.0;    // mean of the whole render, worst channel
    double tailDcAbs  = 0.0;    // mean of the last half second, worst channel
    double centroid   = 0.0;    // Hz, sustained-chord window
    double highRatio  = 0.0;    // energy above 1.5 kHz / total, same window
    double topRatio   = 0.0;    // energy above 4 kHz / total, same window
    double lowRatio   = 0.0;    // energy below 250 Hz / total, same window
    double postHighDb = -300.0;   // absolute >1.5 kHz energy dB after the pedal lifts
    double postLowMidDb = -300.0; // absolute 100..250 Hz energy dB, same window
    double centStdHz  = 0.0;    // centroid movement across 100 ms hops
    double sideRatio  = 0.0;    // rms(l-r) / rms(l+r)
    double amFreq     = 0.0;    // strongest 3..8 Hz envelope line, Hz
    double amDepthDb  = 0.0;    // its peak-to-peak depth in dB
};

// One FFT window's spectral centroid and band ratios, 30 Hz .. 12 kHz.
// `highAbsDb` / `lowMidAbsDb` are un-normalised band energies in dB --
// comparable between renders of the same window length, which is all the
// character rows ask of them.
static void spectrum (const std::vector<double>& mono, std::size_t start, std::size_t n,
                      double& centroid, double& highRatio, double* topRatio = nullptr,
                      double* lowRatio = nullptr, double* highAbsDb = nullptr,
                      double* lowMidAbsDb = nullptr)
{
    std::vector<an::cplx> buf (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * an::kPi * static_cast<double> (i)
                                               / static_cast<double> (n - 1));
        buf[i] = mono[start + i] * w;
    }
    an::fft (buf);

    double num = 0.0, den = 0.0, high = 0.0, top = 0.0, low = 0.0, lowMid = 0.0;
    for (std::size_t k = 1; k < n / 2; ++k)
    {
        const double f = static_cast<double> (k) * kFs / static_cast<double> (n);
        if (f < 30.0 || f > 12000.0) continue;
        const double e = std::norm (buf[k]);
        num += f * e;
        den += e;
        if (f > 1500.0) high += e;
        if (f > 4000.0) top += e;
        if (f < 250.0)  low += e;
        if (f >= 100.0 && f < 250.0) lowMid += e;
    }
    centroid  = den > 0.0 ? num / den : 0.0;
    highRatio = den > 0.0 ? high / den : 0.0;
    if (topRatio != nullptr)  *topRatio  = den > 0.0 ? top / den : 0.0;
    if (lowRatio != nullptr)  *lowRatio  = den > 0.0 ? low / den : 0.0;
    if (highAbsDb != nullptr) *highAbsDb = 10.0 * std::log10 (std::max (1.0e-30, high));
    if (lowMidAbsDb != nullptr) *lowMidAbsDb = 10.0 * std::log10 (std::max (1.0e-30, lowMid));
}

// `overrides` force parameters after resolution -- used to render a preset's
// own twin (phaser bypassed, felt back to stock, frame back to the casting)
// as the baseline for the character rows. `monoOut`, when given, receives
// the mono mix so a row can measure the difference between two renders.
// renderResolved is the core, taking the completed value set plus the preset
// name (which keys the workshop tables); renderPreset is the everyday form.
struct Override { const char* id; float value; };

static Measured renderResolved (std::map<std::string, float> vals, const char* name,
                                std::vector<double>* monoOut = nullptr)
{
    const EngineParams ep = engineParamsFrom ([&vals] (const char* id)
    {
        const auto it = vals.find (id);
        return it != vals.end() ? it->second : 0.0f;
    });

    // Heap engine: 88 voices weigh megabytes.
    auto engine = std::make_unique<EpiEngine>();
    engine->prepare (kFs, kBlock);

    // A preset that paints a bench sounds through that bench.
    if (const auto* t = pd::tineModsFor (name))
        for (int i = 0; i < EpiEngine::kNumTines; ++i)
            engine->setTineMod (i, (*t)[static_cast<std::size_t> (i)][0],
                                   (*t)[static_cast<std::size_t> (i)][1]);
    if (const auto* c = pd::cabModsFor (name))
        engine->setCabMod ((*c)[0], (*c)[1], (*c)[2], (*c)[3], (*c)[4]);
    if (const auto* m = pd::micModsFor (name))
        engine->setMicMod ((*m)[0], (*m)[1], (*m)[2], (*m)[3], (*m)[4]);

    // The phrase: a bass note, then a mf C major triad, then a treble note;
    // pedal down at half a second, keys up at 1.5 s, pedal up at 3 s. The
    // same phrase for every preset, so levels compare across the bank.
    struct Ev { int at; NoteEvent e; };
    const std::vector<Ev> script = {
        { 0,      { 0, NoteEvent::noteOn, 40, 0.75f } },   // E2
        { 2400,   { 0, NoteEvent::noteOn, 60, 0.65f } },   // C4
        { 2400,   { 0, NoteEvent::noteOn, 64, 0.65f } },   // E4
        { 2400,   { 0, NoteEvent::noteOn, 67, 0.65f } },   // G4
        { 4800,   { 0, NoteEvent::noteOn, 84, 0.70f } },   // C6
        { 24000,  { 0, NoteEvent::sustain, 0, 1.0f } },
        { 72000,  { 0, NoteEvent::noteOff, 40, 0.0f } },
        { 72000,  { 0, NoteEvent::noteOff, 60, 0.0f } },
        { 72000,  { 0, NoteEvent::noteOff, 64, 0.0f } },
        { 72000,  { 0, NoteEvent::noteOff, 67, 0.0f } },
        { 72000,  { 0, NoteEvent::noteOff, 84, 0.0f } },
        { 144000, { 0, NoteEvent::sustain, 0, 0.0f } },
    };

    std::vector<float> l (kLen), r (kLen);
    std::vector<NoteEvent> evs;
    for (int at = 0; at < kLen; at += kBlock)
    {
        evs.clear();
        for (const auto& s : script)
            if (s.at >= at && s.at < at + kBlock)
            {
                NoteEvent e = s.e;
                e.offset = s.at - at;
                evs.push_back (e);
            }
        engine->process (l.data() + at, r.data() + at, kBlock, ep,
                         evs.data(), static_cast<int> (evs.size()));
    }

    Measured m;
    double sumSq = 0.0, meanL = 0.0, meanR = 0.0, tailL = 0.0, tailR = 0.0;
    double sumMid = 0.0, sumSide = 0.0;
    const int tail0 = kLen - static_cast<int> (0.5 * kFs);
    for (int i = 0; i < kLen; ++i)
    {
        if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) { m.finite = false; break; }
        m.peak = std::max (m.peak, std::max (std::abs ((double) l[i]), std::abs ((double) r[i])));
        sumSq += (double) l[i] * l[i] + (double) r[i] * r[i];
        meanL += l[i]; meanR += r[i];
        if (i >= tail0) { tailL += l[i]; tailR += r[i]; }
        const double mid = 0.5 * ((double) l[i] + r[i]), side = 0.5 * ((double) l[i] - r[i]);
        sumMid += mid * mid; sumSide += side * side;
    }
    if (! m.finite) return m;

    m.rmsDb      = 10.0 * std::log10 (std::max (1.0e-30, sumSq / (2.0 * kLen)));
    m.dcAbs      = std::max (std::abs (meanL), std::abs (meanR)) / kLen;
    m.tailDcAbs  = std::max (std::abs (tailL), std::abs (tailR)) / (0.5 * kFs);
    m.sideRatio  = std::sqrt (std::max (1.0e-30, sumSide) / std::max (1.0e-30, sumMid));

    std::vector<double> mono (kLen), left (kLen);
    for (int i = 0; i < kLen; ++i)
    {
        mono[static_cast<std::size_t> (i)] = 0.5 * ((double) l[i] + r[i]);
        left[static_cast<std::size_t> (i)] = l[i];
    }

    // Sustained-chord spectrum: 32768 samples (683 ms) from 350 ms in.
    spectrum (mono, static_cast<std::size_t> (0.35 * kFs), 32768,
              m.centroid, m.highRatio, &m.topRatio, &m.lowRatio);

    // The post-release tail: the pedal lifts at 3.0 s and the dampers take
    // the strings. What survives above 1.5 kHz in the next third of a second
    // is what the damper felt failed to grip -- the zing, when the felt is
    // hardened -- and what survives at 100..250 Hz is the body's own low
    // ring, which is where a scaled grand board shows itself. The window was
    // swept before it was trusted: 100 ms after the lift, 341 ms long, is
    // where the felt margin measured widest.
    {
        double c, h;
        spectrum (mono, static_cast<std::size_t> (3.10 * kFs), 16384,
                  c, h, nullptr, nullptr, &m.postHighDb, &m.postLowMidDb);
    }

    // Spectral movement: centroid per 100 ms hop, 8192-sample windows,
    // 0.4 .. 3.3 s, on the LEFT channel -- the right phaser runs a quarter
    // cycle offset, so a mono sum smears the very sweep being measured.
    // The std across hops is the phaser's signature.
    {
        std::vector<double> cs;
        for (double t = 0.4; t + 8192.0 / kFs < 3.3; t += 0.1)
        {
            double c, h;
            spectrum (left, static_cast<std::size_t> (t * kFs), 8192, c, h);
            cs.push_back (c);
        }
        double mean = 0.0;
        for (double c : cs) mean += c;
        mean /= static_cast<double> (cs.size());
        double var = 0.0;
        for (double c : cs) var += (c - mean) * (c - mean);
        m.centStdHz = std::sqrt (var / static_cast<double> (cs.size()));
    }

    // Amplitude modulation: rectify, one-pole at 30 Hz, decimate to 750 Hz,
    // detrend the log envelope with a straight line, then scan 3..8 Hz.
    {
        const double a = 1.0 - std::exp (-2.0 * an::kPi * 30.0 / kFs);
        double env = 0.0;
        std::vector<double> e;
        const int e0 = static_cast<int> (0.5 * kFs), e1 = static_cast<int> (3.4 * kFs);
        for (int i = 0; i < e1; ++i)
        {
            env += a * (std::abs (mono[static_cast<std::size_t> (i)]) - env);
            if (i >= e0 && (i % 64) == 0)
                e.push_back (20.0 * std::log10 (std::max (1.0e-9, env)));
        }
        const double n = static_cast<double> (e.size());
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (std::size_t i = 0; i < e.size(); ++i)
        {
            sx += (double) i; sy += e[i];
            sxx += (double) i * (double) i; sxy += (double) i * e[i];
        }
        const double slope = (n * sxy - sx * sy) / std::max (1.0, n * sxx - sx * sx);
        const double icpt  = (sy - slope * sx) / n;
        std::vector<double> d (e.size());
        for (std::size_t i = 0; i < e.size(); ++i) d[i] = e[i] - (icpt + slope * (double) i);

        const double envRate = kFs / 64.0;
        double best = 0.0, bestF = 0.0;
        for (double f = 3.0; f <= 8.0; f += 0.05)
        {
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < d.size(); ++i)
            {
                const double ph = 2.0 * an::kPi * f * (double) i / envRate;
                re += d[i] * std::cos (ph);
                im += d[i] * std::sin (ph);
            }
            const double amp = 2.0 * std::sqrt (re * re + im * im) / n;
            if (amp > best) { best = amp; bestF = f; }
        }
        m.amFreq    = bestF;
        m.amDepthDb = 2.0 * best;   // sinusoid amplitude -> peak-to-peak dB
    }

    if (monoOut != nullptr) *monoOut = std::move (mono);

    return m;
}

static Measured renderPreset (const pd::Preset& p,
                              std::initializer_list<Override> overrides = {},
                              std::vector<double>* monoOut = nullptr)
{
    auto vals = resolve (p);
    for (const auto& o : overrides) vals[o.id] = o.value;
    return renderResolved (std::move (vals), p.name, monoOut);
}

// ===========================================================================
int main()
{
    // ---- structural rows --------------------------------------------------
    heading ("Structure: preset data against the layout replica");

    {
        // The defaults table in PresetData.h must be the layout, exactly.
        bool ok = std::size (pd::kDefaults) == std::size (kLayout);
        for (const auto& d : pd::kDefaults)
        {
            const auto* s = findSpec (d.id);
            if (s == nullptr || std::abs (s->def - d.value) > 1.0e-6f) { ok = false; break; }
        }
        row ("bank", "PresetData defaults == layout",
             fmt ("%2.0f params", (double) std::size (kLayout)),
             fmt ("%2.0f params", (double) std::size (pd::kDefaults)), ok);
    }

    {
        // The shared mapping must consume exactly the layout's ids.
        std::set<std::string> asked;
        (void) engineParamsFrom ([&asked] (const char* id)
        {
            asked.insert (id);
            const auto* s = findSpec (id);
            return s != nullptr ? s->def : -1.0e9f;
        });
        bool ok = true;
        for (const auto& id : asked) if (findSpec (id.c_str()) == nullptr) ok = false;
        for (const auto& s : kLayout) if (asked.count (s.id) == 0) ok = false;
        row ("bank", "EngineParamMap covers the layout",
             fmt ("%2.0f ids", (double) std::size (kLayout)),
             fmt ("%2.0f ids", (double) asked.size()), ok);
    }

    {
        // dB to linear, spot value: -6 dB is 0.5012.
        std::map<std::string, float> v;
        for (const auto& s : kLayout) v[s.id] = s.def;
        v["outGain"] = -6.0f;
        const auto p = engineParamsFrom ([&v] (const char* id) { return v[id]; });
        const bool ok = std::abs (p.outGainLin - 0.501187f) < 1.0e-4f;
        row ("bank", "outGain -6 dB -> linear", "0.5012",
             fmt ("%.4f", p.outGainLin), ok);
    }

    {
        std::set<std::string> names;
        for (const auto& p : pd::kPresets) names.insert (p.name);
        row ("bank", "preset names are unique",
             fmt ("%2.0f", (double) pd::kNumPresets),
             fmt ("%2.0f", (double) names.size()), names.size() == pd::kNumPresets);
    }

    {
        // The reference stays frozen: Suitcase, exactly as the acoustic
        // suite's baseline was voiced.
        static const std::pair<const char*, float> frozen[] = {
            { "tremDepth", 0.35f }, { "tremRate", 4.2f }, { "bass", 2.0f },
            { "treble", 1.0f }, { "cabMix", 0.55f }, { "spaceMix", 0.12f },
        };
        const auto vals = resolve (pd::kPresets[0]);
        bool ok = std::strcmp (pd::kPresets[0].name, "Suitcase") == 0
               && pd::kPresets[0].numValues == std::size (frozen);
        for (const auto& [id, v] : frozen)
            if (std::abs (vals.at (id) - v) > 1.0e-6f) ok = false;
        // And everything it does not name sits at the layout default.
        for (const auto& s : kLayout)
        {
            bool named = false;
            for (const auto& [id, v] : frozen) if (std::strcmp (id, s.id) == 0) named = true;
            if (! named && std::abs (vals.at (s.id) - s.def) > 1.0e-6f) ok = false;
        }
        row ("Suitcase", "reference voicing frozen", "unchanged",
             ok ? "unchanged" : "CHANGED", ok);
    }

    for (const auto& p : pd::kPresets)
    {
        bool idsOk = true, rangeOk = true;
        for (std::size_t i = 0; i < p.numValues; ++i)
        {
            const auto* s = findSpec (p.values[i].id);
            if (s == nullptr) { idsOk = false; continue; }
            if (p.values[i].value < s->lo || p.values[i].value > s->hi) rangeOk = false;
        }
        if (! (idsOk && rangeOk))
            row (p.name, "param ids exist and in range", "all", "violation", false);

        const auto vals = resolve (p);
        const int mat = static_cast<int> (vals.at ("material"));
        const int sel = static_cast<int> (vals.at ("pickupSel"));
        const auto& mspec = kMaterials[std::clamp (mat, 0, kNumMaterials - 1)];

        // Magnetic needs iron; electrostatic needs a conductor. "Native" is
        // magnetic on the tine, electrostatic on the reed, a force bridge on
        // the e-grand; the grand is mic'd and reads anything.
        bool legal = true;
        if (p.instrument != 3)
        {
            const bool magnetic = sel == 0 || (sel == 1 && p.instrument == 0);
            const bool electro  = sel == 2 || (sel == 1 && p.instrument == 2);
            if (magnetic && ! mspec.ferro)      legal = false;
            if (electro  && ! mspec.conductive) legal = false;
        }
        if (! legal)
            row (p.name, "material visible to transducer", "audible", "SILENT PAIRING", false);

        if (p.instrument == 0 && vals.at ("pickupDist") < 0.20f)
            row (p.name, "tine pickup gap legal", ">= 0.20",
                 fmt ("%.2f", vals.at ("pickupDist")), false);
    }
    std::printf ("  (per-preset structural violations only print on failure)\n");

    // ---- render rows ------------------------------------------------------
    heading ("Render: 4 s phrase per preset (E2 + C4-E4-G4 + C6, pedal worked)");
    std::printf ("  %-15s %-4s %10s %10s %10s %10s %10s %10s\n",
                 "preset", "inst", "peak", "RMS dBFS", "DC", "centroid", "hi-ratio", "verdict");

    std::map<std::string, Measured> mm;
    for (const auto& p : pd::kPresets)
    {
        const Measured m = renderPreset (p);
        mm[p.name] = m;

        // The whole-render mean tolerates the reed preamp's asymmetric
        // saturation (a real, physical offset while the signal is loud);
        // stuck DC is what the tail measures, after the phrase has decayed.
        // Both loudness fences moved with the engine's -18 dBFS bench: the
        // RMS window (was [-34,-14] against the old -24 dBFS bench) and the
        // whole-render DC allowance (was 5e-3; the reed offset is the same
        // physical asymmetry, six decibels hotter). The tail fence does not
        // move -- stuck DC is stuck at any bench. Peaks pinned at exactly
        // 0.891 are the soft output rail's -1 dBFS ceiling doing its job,
        // not clipping.
        const bool ok = m.finite
                     && m.peak <= 1.0
                     && m.rmsDb >= -28.0 && m.rmsDb <= -8.0
                     && m.dcAbs < 8.0e-3 && m.tailDcAbs < 1.0e-3;
        if (! ok) ++failures;
        std::printf ("  %-15s %-4d %10.3f %10.1f %10.6f %10.0f %10.3f %10s\n",
                     p.name, p.instrument, m.peak, m.rmsDb,
                     std::max (m.dcAbs, m.tailDcAbs), m.centroid, m.highRatio,
                     m.finite ? (ok ? "PASS" : "FAIL") : "NOT FINITE");
    }

    // ---- character rows ---------------------------------------------------
    heading ("Character: what a name promises, measured");

    auto darker = [&] (const char* a, const char* b)
    {
        row (a, (std::string ("darker than ") + b).c_str(),
             fmt ("< %.0f Hz", mm[b].centroid), fmt ("%.0f Hz", mm[a].centroid),
             mm[a].centroid < mm[b].centroid);
    };
    auto brighter = [&] (const char* a, const char* b)
    {
        row (a, (std::string ("brighter than ") + b).c_str(),
             fmt ("> %.0f Hz", mm[b].centroid), fmt ("%.0f Hz", mm[a].centroid),
             mm[a].centroid > mm[b].centroid);
    };
    auto dirtier = [&] (const char* a, const char* b)
    {
        row (a, (std::string ("more high-band vs ") + b).c_str(),
             fmt ("> %.3f", mm[b].highRatio), fmt ("%.3f", mm[a].highRatio),
             mm[a].highRatio > mm[b].highRatio);
    };
    auto tremAt = [&] (const char* a, double hz)
    {
        const bool ok = std::abs (mm[a].amFreq - hz) <= 0.06 * hz + 0.051
                     && mm[a].amDepthDb > 0.5;
        row (a, "amplitude modulation at rate",
             fmt ("%.1f Hz, > 0.5 dB", hz),
             fmt ("%.2f Hz, ", mm[a].amFreq) + fmt ("%.1f dB", mm[a].amDepthDb), ok);
    };
    // The phrase's own decay already walks the centroid, so the fair
    // baseline for a movement row is the SAME preset with the phaser
    // bypassed: the render is deterministic, so the difference is the
    // effect's contribution and nothing else.
    auto presetNamed = [] (const char* a) -> const pd::Preset&
    {
        for (const auto& q : pd::kPresets)
            if (std::strcmp (q.name, a) == 0) return q;
        std::printf ("no preset named %s\n", a);
        std::abort();
    };
    auto moves = [&] (const char* a, double factor)
    {
        const pd::Preset* p = &presetNamed (a);
        const Measured dry = renderPreset (*p, { { "phaserMix", 0.0f } });
        row (a, "spectrum moves vs its dry twin",
             fmt ("> %.0f Hz std", factor * dry.centStdHz),
             fmt ("%.0f Hz std", mm[a].centStdHz),
             mm[a].centStdHz > factor * dry.centStdHz);
    };

    darker  ("Mellow",       "Suitcase");
    brighter("Funk",         "Mellow");
    dirtier ("Bark",         "Suitcase");
    tremAt  ("Amp Tremolo",  5.8);
    moves   ("Phase 90",     1.25);

    darker  ("Dark Ballad",  "CP-70");
    brighter("Bright Pop",   "CP-70");
    moves   ("Chorus Era",   1.25);

    dirtier ("Reed Grind",   "Two Hundred");
    dirtier ("Reed Bark",    "Two Hundred");
    darker  ("Lo-Fi Reed",   "Two Hundred");
    tremAt  ("Reed Tremolo", 5.5);

    darker  ("Felt Grand",   "Concert Grand");
    // Voicing is an attack operation: the needled felt yields sooner, so the
    // blow's high band drops while the body the strings produce stays. The
    // row measures where the effect actually lives -- the >4 kHz share --
    // rather than the centroid, which averages the attack away.
    row ("Voiced Grand", "attack top drops vs the unvoiced instrument",
         fmt ("< %.5f top-ratio", mm["Concert Grand"].topRatio),
         fmt ("%.5f top-ratio", mm["Voiced Grand"].topRatio),
         mm["Voiced Grand"].topRatio < mm["Concert Grand"].topRatio);
    // The lid's shadow takes the top of the range, not the balance of the
    // fundamentals -- the centroid barely sees it, the high band does.
    row ("Past The Rim", "lid shades 4 kHz up vs Concert",
         fmt ("< %.5f top-ratio", 0.8 * mm["Concert Grand"].topRatio),
         fmt ("%.5f top-ratio", mm["Past The Rim"].topRatio),
         mm["Past The Rim"].topRatio < 0.8 * mm["Concert Grand"].topRatio);
    row ("Wide Cinema", "wider than Concert Grand",
         fmt ("> %.3f side/mid", mm["Concert Grand"].sideRatio),
         fmt ("%.3f side/mid", mm["Wide Cinema"].sideRatio),
         mm["Wide Cinema"].sideRatio > mm["Concert Grand"].sideRatio);

    // ---- the bench presets, against their own stock twins ------------------
    // The twin is the identical preset with the bench overridden back to
    // stock; the render is deterministic, so the difference is the bench's
    // contribution and nothing else.

    // Hardened damper felt: the high partials the felt can no longer grip
    // survive the pedal lift. The bench measured +26 dB at the voice; what
    // remains of that through the whole chain, the room and three seconds of
    // natural decay was swept before it was trusted -- 7-8 dB in the chosen
    // window -- so the fence sits at +4.
    auto zing = [&] (const char* a)
    {
        const Measured fresh = renderPreset (presetNamed (a),
                                             { { "damperFelt", 0.0f } });
        row (a, "post-release zing vs stock felt",
             fmt ("> %.1f dB + 4", fresh.postHighDb),
             fmt ("%.1f dB", mm[a].postHighDb),
             mm[a].postHighDb > fresh.postHighDb + 4.0);
    };
    zing ("Roadworn");
    zing ("Tack Grand");
    // Worn rail cloth: the release thud is louder. A full mf chord masks a
    // key-up thump -- measured, the bed's whole contribution sits over 60 dB
    // under the phrase -- so the thud is isolated at its own level: the
    // action noise is the exact difference against a strikeNoise-zero twin
    // (renders are deterministic), and the worn bed's release-window action
    // must beat the stock bed's by the bench's own margin.
    {
        const pd::Preset& p = presetNamed ("Tired Stage");
        auto actionDb = [&] (float bed) -> double
        {
            std::vector<double> with, without;
            (void) renderPreset (p, { { "keyBed", bed } }, &with);
            (void) renderPreset (p, { { "keyBed", bed }, { "strikeNoise", 0.0f } }, &without);
            const std::size_t r0 = static_cast<std::size_t> (1.50 * kFs);
            const std::size_t r1 = static_cast<std::size_t> (1.62 * kFs);
            double s = 0.0;
            for (std::size_t i = r0; i < r1; ++i)
            {
                const double d = with[i] - without[i];
                s += d * d;
            }
            return 10.0 * std::log10 (std::max (1.0e-30, s / static_cast<double> (r1 - r0)));
        };
        const double worn  = actionDb (3.0f);   // the preset's own bed
        const double stock = actionDb (0.0f);
        row ("Tired Stage", "release thud louder vs stock rail",
             fmt ("> %.1f dB + 3", stock),
             fmt ("%.1f dB", worn),
             worn > stock + 3.0);
    }
    // A swapped frame must be audible in the render, not a label: the RMS of
    // the difference against the stock-frame twin, relative to the preset's
    // own level. The voices themselves never read the body parameters on the
    // electrics, so the difference IS the frame.
    auto bodyAudible = [&] (const char* a)
    {
        std::vector<double> now, stock;
        (void) renderPreset (presetNamed (a), {}, &now);
        (void) renderPreset (presetNamed (a),
                             { { "bodyMat", 0.0f }, { "bodySize", 0.5f } }, &stock);
        double dSum = 0.0, sSum = 0.0;
        for (std::size_t i = 0; i < now.size(); ++i)
        {
            const double d = now[i] - stock[i];
            dSum += d * d;
            sSum += now[i] * now[i];
        }
        const double rel = 10.0 * std::log10 (std::max (1.0e-30, dSum)
                                            / std::max (1.0e-30, sSum));
        row (a, "frame swap audible vs stock", "> -30 dB rel",
             fmt ("%.1f dB rel", rel), rel > -30.0);
    };
    // The Tine deliberately has no row (and no preset) here: measured, its
    // frame swap sits over 70 dB under the notes through every transducer,
    // so no honest tine body preset exists yet.
    bodyAudible ("Studio Frame");
    bodyAudible ("Pocket Reed");
    // The grand board: a small board loses its bottom by geometry. Measured
    // as the low band's share of the sustained chord against the stock-board
    // twin.
    {
        const Measured stock = renderPreset (presetNamed ("Parlor"),
                                             { { "bodyMat", 0.0f }, { "bodySize", 0.5f } });
        row ("Parlor", "less low band than stock board",
             fmt ("< %.3f low-ratio", stock.lowRatio),
             fmt ("%.3f low-ratio", mm["Parlor"].lowRatio),
             mm["Parlor"].lowRatio < stock.lowRatio);
    }
    // The full-size board's honest signature is the released tail: the big
    // board's low modes keep carrying after the pedal lifts where the stock
    // board has let go (measured +5.7 dB in the 100..250 Hz band, dry; the
    // sustained low RATIO actually falls, because the ladder shifts off the
    // phrase's fundamentals -- asserting "more low ratio" here would fail
    // against the real physics).
    {
        const Measured stock = renderPreset (presetNamed ("Nine Foot"),
                                             { { "bodySize", 0.5f } });
        row ("Nine Foot", "released tail carries more body",
             fmt ("> %.1f dB + 3", stock.postLowMidDb),
             fmt ("%.1f dB", mm["Nine Foot"].postLowMidDb),
             mm["Nine Foot"].postLowMidDb > stock.postLowMidDb + 3.0);
    }
    // Materials, promised and measured. Brass at the reed: what survives to
    // the OUTPUT is the bite -- the softer, springier tongue swings further
    // for the same blow and rides the slot harder, so the sustained spectrum
    // sits clearly above its own steel twin's. (The extra internal loss is
    // real at the reed -- materialT60 takes it in full on a cantilever --
    // but two decay rows were tried and failed honestly: the 200A chain's
    // preamp knee and speaker compression flatten the hotter brass signal
    // enough to hide the faster decay at the output.)
    {
        const Measured steel = renderPreset (presetNamed ("Brass Reeds"),
                                             { { "material", 0.0f } });
        row ("Brass Reeds", "bites harder than steel twin",
             fmt ("> %.0f Hz", steel.centroid),
             fmt ("%.0f Hz", mm["Brass Reeds"].centroid),
             mm["Brass Reeds"].centroid > steel.centroid);
    }
    // Nylon takes its material loss only on the bending share -- nearly zero
    // for nylon -- so the light courses ring the top clear where wire
    // crowds it: measurably brighter than its own wire twin.
    {
        const Measured steel = renderPreset (presetNamed ("Harp Grand"),
                                             { { "material", 0.0f } });
        row ("Harp Grand", "clearer top than its wire twin",
             fmt ("> %.0f Hz", steel.centroid),
             fmt ("%.0f Hz", mm["Harp Grand"].centroid),
             mm["Harp Grand"].centroid > steel.centroid);
    }

    // ---- the Clav, and the benches the practitioner report added -----------

    // The funk patch is the bridge tap through the Treble rocker against the
    // classic's summed taps through Medium: the percussive end of the
    // instrument, and it has to measure that way.
    brighter ("Clav Funk",      "Clav Classic");
    // Out of phase keeps only what the two taps do NOT share, which takes
    // the fundamentals out and leaves the hollow middle.
    row ("Clav Phase Cut", "less low band than the summed taps",
         fmt ("< %.3f low-ratio", mm["Clav Classic"].lowRatio),
         fmt ("%.3f low-ratio", mm["Clav Phase Cut"].lowRatio),
         mm["Clav Phase Cut"].lowRatio < mm["Clav Classic"].lowRatio);

    // Notched tangent rubbers catch the string on release. The signature is
    // in the release window, not the sustain, so it is measured there
    // against the same preset with the rubbers new -- deterministic renders,
    // so the difference is the wear and nothing else.
    {
        std::vector<double> worn, mint;
        (void) renderPreset (presetNamed ("Gigged Clav"), {}, &worn);
        (void) renderPreset (presetNamed ("Gigged Clav"), { { "wearAmount", 0.0f } }, &mint);
        double dSum = 0.0, sSum = 0.0;
        for (std::size_t i = 0; i < worn.size(); ++i)
        {
            const double d = worn[i] - mint[i];
            dSum += d * d;
            sSum += worn[i] * worn[i];
        }
        const double rel = 10.0 * std::log10 (std::max (1.0e-30, dSum)
                                            / std::max (1.0e-30, sSum));
        // Measured -33.5 dB of the render: audible as the catch it is, and
        // nowhere near a second instrument. Fenced both ways -- silent wear
        // would be a dead bench, loud wear would be a percussion track.
        row ("Gigged Clav", "worn rubbers catch, audibly and no more",
             "-45 .. -22 dB rel", fmt ("%.1f dB rel", rel),
             rel > -45.0 && rel < -22.0);
    }
    // The case reaches the pickup by structure-borne sound: turning it off
    // must change the render, and audibly. (The paper measured the Clav's
    // ACOUSTIC output as feeble, which is a different claim -- a pickup
    // senses relative motion, so the box is in the signal without being in
    // the room.)
    {
        std::vector<double> now, quiet;
        (void) renderPreset (presetNamed ("Gigged Clav"), {}, &now);
        (void) renderPreset (presetNamed ("Gigged Clav"), { { "bodyMix", 0.0f } }, &quiet);
        double dSum = 0.0, sSum = 0.0;
        for (std::size_t i = 0; i < now.size(); ++i)
        {
            const double d = now[i] - quiet[i];
            dSum += d * d;
            sSum += now[i] * now[i];
        }
        const double rel = 10.0 * std::log10 (std::max (1.0e-30, dSum)
                                            / std::max (1.0e-30, sSum));
        // Measured -55.7 dB of the whole render. That is a subordinate but
        // real contribution, which is what both sources say it should be:
        // the practitioner hears the box plainly (it is the knock under
        // every key press), the paper measured the ACOUSTIC output as
        // feeble -- and a pickup senses relative motion, so the case is in
        // the signal without being in the room. The absolute level waits on
        // the isolation recordings the case constants were derived without.
        row ("Gigged Clav", "the case is in the pickup, subordinate",
             "-68 .. -40 dB rel", fmt ("%.1f dB rel", rel),
             rel > -68.0 && rel < -40.0);
    }

    // ---- the two presets that ship a mic-stage placement --------------------

    // The club seat sits close over the open lid, where the lid image
    // arrives with the direct sound: brighter up top than the calibrated
    // pair at its polite distance.
    row ("Jazz Club", "brighter top than Concert Grand",
         fmt ("> %.5f top-ratio", mm["Concert Grand"].topRatio),
         fmt ("%.5f top-ratio", mm["Jazz Club"].topRatio),
         mm["Jazz Club"].topRatio > mm["Concert Grand"].topRatio);
    // The church carries after the notes stop, which is the whole point of
    // the profile: measured in the released window, not the sustain.
    row ("Cathedral", "rings on after the pedal lifts",
         fmt ("> %.1f dB + 3", mm["Concert Grand"].postLowMidDb),
         fmt ("%.1f dB", mm["Cathedral"].postLowMidDb),
         mm["Cathedral"].postLowMidDb > mm["Concert Grand"].postLowMidDb + 3.0);

    // ---- summary ----------------------------------------------------------
    std::printf ("\n%s: %d failure%s\n",
                 failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
