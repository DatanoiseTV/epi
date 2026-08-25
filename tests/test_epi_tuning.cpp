/*
  Epi — per-note tuning testbench.

  A host's tuning system retunes each note individually. That is a tuner's
  action and it is entirely physical: every string on a real instrument is set
  on its own, which is what the workshop's length lane already does. What is
  NOT physical is retuning a string while it rings -- there is no bend wheel on
  a piano, an electric piano or a clavinet, and nobody turns a tuning pin
  mid-note. So the offset that arrives with a note-on is latched at the strike,
  and a later change on that channel reaches the next note struck, not the one
  sounding.

  This suite measures that, end to end: it renders the engine and reads the
  pitch back out of the audio rather than trusting the parameter. It also
  fences the thing the whole design turns on -- an instrument nobody has
  retuned must render BIT-IDENTICALLY to one built before per-note tuning
  existed.

  The MIDI-side rows drive epi::MpeTuning through the same three calls
  EpiAudioProcessor::collectEvents makes. That replica is deliberate: this
  suite exists to catch drift, and a shared helper cannot catch drift with
  itself.

  Build (framework-free, no JUCE):
    c++ -std=c++20 -O3 -DNDEBUG -Isrc -include chrono \
        tests/test_epi_tuning.cpp src/epi/dsp/EpiEngine.cpp -o epi_tuning_tests
*/

#include "EpiAnalysis.h"
#include "epi/dsp/EpiEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace epi;
namespace an = epianalysis;

// ===========================================================================
// Reporting (the table discipline the sibling suites use)
// ===========================================================================

static int failures = 0;

static void heading (const char* s)
{
    std::printf ("\n%s\n", s);
    std::printf ("  ---------------------------------------------------------------------------------\n");
}

enum class Verdict { pass, fail, info };

static void row (const char* id, const char* what, const std::string& target,
                 const std::string& got, Verdict v)
{
    const char* mark = "PASS";
    if (v == Verdict::fail) { mark = "FAIL"; ++failures; }
    if (v == Verdict::info)   mark = "  --";
    std::printf ("  %-5s %-44s %-24s %-28s %s\n", id, what, target.c_str(), got.c_str(), mark);
    std::fflush (stdout);
}

static Verdict verdict (bool ok) { return ok ? Verdict::pass : Verdict::fail; }

static std::string fmt (const char* f, double a)
{
    char b[128];
    std::snprintf (b, sizeof b, f, a);
    return b;
}

static std::string fmt2 (const char* f, double a, double b2)
{
    char b[160];
    std::snprintf (b, sizeof b, f, a, b2);
    return b;
}

static const char* const kInstName[5] = { "Tine", "E-Grand", "Reed", "Grand", "Clav" };

// ===========================================================================
// Rendering
// ===========================================================================

static constexpr double kFs    = 48000.0;
static constexpr int    kBlock = 128;

static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

// Cents between a measured frequency and a reference one.
static double centsBetween (double f, double ref)
{
    return 1200.0 * std::log2 (std::max (1.0e-12, f) / std::max (1.0e-12, ref));
}

// The measurement configuration the sibling suites render with: everything
// that is not the instrument is off, so what is measured is the string.
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

struct TimedEvent { int sample = 0; NoteEvent ev; };

struct Stereo
{
    std::vector<float> L, R;
    std::vector<double> mono() const
    {
        std::vector<double> m (L.size());
        for (std::size_t i = 0; i < L.size(); ++i)
            m[i] = 0.5 * (static_cast<double> (L[i]) + static_cast<double> (R[i]));
        return m;
    }
    bool sameSamplesAs (const Stereo& o) const
    {
        return L.size() == o.L.size() && R.size() == o.R.size()
            && std::memcmp (L.data(), o.L.data(), L.size() * sizeof (float)) == 0
            && std::memcmp (R.data(), o.R.data(), R.size() * sizeof (float)) == 0;
    }
};

// Optional per-block hook, so a row can move something mid-render without
// producing a note event -- which is exactly what a mid-note wheel move is.
using BlockHook = std::function<void (int blockStart)>;

static Stereo render (int instrument, double seconds, std::vector<TimedEvent> evs,
                      const BlockHook& hook = {})
{
    const int N = static_cast<int> (kFs * seconds);
    Stereo out;
    out.L.assign (static_cast<std::size_t> (N), 0.0f);
    out.R.assign (static_cast<std::size_t> (N), 0.0f);

    std::stable_sort (evs.begin(), evs.end(),
                      [] (const TimedEvent& a, const TimedEvent& b) { return a.sample < b.sample; });

    EpiEngine e;
    e.prepare (kFs, kBlock);
    const EngineParams p = measParams (instrument);

    std::vector<NoteEvent> be;
    std::size_t k = 0;
    for (int i = 0; i < N; i += kBlock)
    {
        const int n = std::min (kBlock, N - i);
        if (hook) hook (i);
        be.clear();
        while (k < evs.size() && evs[k].sample < i + n)
        {
            NoteEvent ev = evs[k].ev;
            ev.offset = std::max (0, evs[k].sample - i);
            be.push_back (ev);
            ++k;
        }
        e.process (out.L.data() + i, out.R.data() + i, n, p,
                   be.empty() ? nullptr : be.data(), static_cast<int> (be.size()));
    }
    return out;
}

// The pitch a note is actually sounding at.
//
// Two things rule out the sibling suites' refineF0 here, and both were
// measured on the way to this tool rather than assumed.
//
// It iterates a heterodyne outward from the nominal and its comb is one
// fundamental wide, so with a second note sounding it can refine its way onto
// the wrong one: a note-60 window in a two-note chord locked onto note 66
// outright and reported a 70-cent error that was purely the analysis. Several
// of these rows have to read two pitches out of one chord.
//
// And a struck grand note has no single fundamental to report. Three strings
// per unison, coupled through the bridge, put a cluster about ten cents wide
// where a single-string instrument puts a line -- measured here on note 66,
// two near-equal maxima four cents apart -- so the TALLEST member of that
// cluster jumps when the unison is retuned, and reading the peak alone
// reported a three-cent error on a shift the model had made exactly. The
// pitch of a unison is the group, not its loudest string.
//
// So: locate the partial anywhere within a band far wider than any offset
// under test, then report the power-weighted centroid of the cluster around
// it. On the four single-string instruments the two agree to a hundredth of a
// cent; on the grand the centroid is what tracks the unison.
static double goertzelMag (const std::vector<double>& w, double f)
{
    const double c = 2.0 * std::cos (2.0 * an::kPi * f / kFs);
    double s1 = 0.0, s2 = 0.0;
    for (double v : w)
    {
        const double s0 = v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
}

// Hann over the analysis window: at a hundred bins out its sidelobes are far
// below anything that could move a peak, which is what keeps a neighbouring
// note from leaking into the read.
static std::vector<double> analysisWindow (const std::vector<double>& x, double ta, double tb)
{
    const std::size_t a = static_cast<std::size_t> (std::max (0.0, ta * kFs));
    const std::size_t b = std::min (x.size(), static_cast<std::size_t> (tb * kFs));
    if (b <= a + 1024) return {};
    std::vector<double> w (b - a);
    const double n = static_cast<double> (w.size());
    for (std::size_t i = 0; i < w.size(); ++i)
        w[i] = x[a + i] * (0.5 - 0.5 * std::cos (2.0 * an::kPi * static_cast<double> (i) / n));
    return w;
}

static double narrowF0 (const std::vector<double>& x, double nominal, double ta, double tb)
{
    const auto w = analysisWindow (x, ta, tb);
    if (w.empty()) return 0.0;

    // Where the partial is. The search band is +/-160 cents -- wider than any
    // offset these rows ask for, so a note that failed to move is reported
    // where it actually is rather than railed against a band edge, and far
    // narrower than the interval to the neighbouring note in the chord rows.
    constexpr double kSearchCents = 160.0;
    constexpr int    kSearchSteps = 161;
    double peakCents = 0.0, peakMag = -1.0;
    for (int i = 0; i < kSearchSteps; ++i)
    {
        const double c = -kSearchCents + 2.0 * kSearchCents * i / (kSearchSteps - 1);
        const double m = goertzelMag (w, nominal * std::pow (2.0, c / 1200.0));
        if (m > peakMag) { peakMag = m; peakCents = c; }
    }

    // The cluster around it. Twenty-five cents each way covers the grand's
    // unison from either of its maxima; the floor discards the skirts, which
    // otherwise drag the centroid toward the middle of the window.
    constexpr double kClusterCents = 25.0;
    constexpr int    kClusterSteps = 101;
    constexpr double kFloorDb      = 25.0;
    std::vector<double> mag (kClusterSteps);
    double top = 0.0;
    for (int i = 0; i < kClusterSteps; ++i)
    {
        const double c = peakCents - kClusterCents + 2.0 * kClusterCents * i / (kClusterSteps - 1);
        mag[static_cast<std::size_t> (i)] = goertzelMag (w, nominal * std::pow (2.0, c / 1200.0));
        top = std::max (top, mag[static_cast<std::size_t> (i)]);
    }
    const double floor = top * std::pow (10.0, -kFloorDb / 20.0);
    double sumP = 0.0, sumPC = 0.0;
    for (int i = 0; i < kClusterSteps; ++i)
    {
        const double c = peakCents - kClusterCents + 2.0 * kClusterCents * i / (kClusterSteps - 1);
        const double a = mag[static_cast<std::size_t> (i)] < floor
                       ? 0.0 : mag[static_cast<std::size_t> (i)];
        sumP  += a * a;
        sumPC += a * a * c;
    }
    const double cents = sumP > 0.0 ? sumPC / sumP : peakCents;
    return nominal * std::pow (2.0, cents / 1200.0);
}

static double measuredF0 (const Stereo& s, int note, double ta, double tb)
{
    return narrowF0 (s.mono(), noteHz (note), ta, tb);
}

// ===========================================================================
// The plugin's MIDI side, replicated. These are the same three calls
// EpiAudioProcessor::collectEvents makes -- see the note at the top.
// ===========================================================================

// A note-on, tuned by whatever the channel is currently asking for.
static NoteEvent midiNoteOn (MpeTuning& mpe, int channel, int note, float vel)
{
    return { 0, NoteEvent::noteOn, note, vel, mpe.noteCents (channel) };
}

// A 14-bit wheel word for a given offset in cents at a given bend range.
// Rounded to the nearest step, because that is all fourteen bits can carry.
static int wheelFor (double cents, double rangeSemis)
{
    const double frac = cents / (rangeSemis * 100.0);
    return std::clamp (8192 + static_cast<int> (std::lround (frac * 8192.0)), 0, 16383);
}

// What that word actually asks for, which is what the row must measure against.
static double centsOfWheel (int wheel, double rangeSemis)
{
    return (wheel - 8192) / 8192.0 * rangeSemis * 100.0;
}

// The MPE Configuration Message: RPN 6 on the zone's master channel, member
// count in the data-entry MSB.
static void sendMcm (MpeTuning& mpe, int masterChannel, int members)
{
    mpe.controller (masterChannel, 101, 0);
    mpe.controller (masterChannel, 100, 6);
    mpe.controller (masterChannel, 6, members);
}

// RPN 0: pitch bend sensitivity, semitones in the MSB and cents in the LSB.
static void sendBendRange (MpeTuning& mpe, int channel, int semis, int cents)
{
    mpe.controller (channel, 101, 0);
    mpe.controller (channel, 100, 0);
    mpe.controller (channel, 6, semis);
    mpe.controller (channel, 38, cents);
}

// ===========================================================================
// 1. The register: what the MIDI side means, before any audio is rendered
// ===========================================================================
static void sectionRegister()
{
    heading ("1. The tuning register (MMA RP-053)");

    {
        // Detect is the shipped default and it must stay shut until a host
        // asks. This is the row that keeps a plain-MIDI session untouched.
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::detect);
        bool ok = ! mpe.isActive() && ! mpe.isMember (2);
        // A wheel and a note on any channel while shut: no tuning, and the
        // caller is told to treat the wheel as the global bend.
        ok = ok && ! mpe.pitchWheel (2, 16383) && mpe.noteCents (2) == 0.0f;
        row ("1.1", "Detect: shut until the host configures", "inactive, 0 ct",
             fmt ("%.1f ct", mpe.noteCents (2)), verdict (ok));
    }

    {
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::detect);
        sendMcm (mpe, 1, 15);
        // RP-053: master channel 1, members 2-16, member default +/-48 semis.
        const bool ok = mpe.isActive()
                     && ! mpe.isMember (1) && mpe.isMember (2) && mpe.isMember (16)
                     && mpe.channelRangeSemis (2) == MpeTuning::kMemberRangeSemis
                     && mpe.memberCount (true) == 15;
        row ("1.2", "MCM opens the lower zone, master ch1", "15 members, +/-48 st",
             fmt2 ("%.0f members, +/-%.0f st", (double) mpe.memberCount (true),
                   (double) mpe.channelRangeSemis (2)), verdict (ok));
    }

    {
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::detect);
        sendMcm (mpe, 1, 15);
        // Full deflection at the member default is a whole four octaves.
        mpe.pitchWheel (2, 16383);
        const double top = mpe.noteCents (2);
        mpe.pitchWheel (2, 0);
        const double bot = mpe.noteCents (2);
        // 8191/8192 of 4800 cents up, exactly 4800 down: the wheel is not
        // symmetric about centre and pretending it is would be a lie.
        const bool ok = std::abs (top - 4800.0 * 8191.0 / 8192.0) < 0.01
                     && std::abs (bot + 4800.0) < 0.01;
        row ("1.3", "wheel maps to cents at the member range", "+4799.4 / -4800.0 ct",
             fmt2 ("%+.1f / %+.1f ct", top, bot), verdict (ok));
    }

    {
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::detect);
        sendMcm (mpe, 1, 15);
        sendBendRange (mpe, 2, 2, 0);   // a host that wants the classic +/-2
        mpe.pitchWheel (2, 8192 + 4096);
        const bool ok = std::abs (mpe.channelRangeSemis (2) - 2.0f) < 1.0e-6f
                     && std::abs (mpe.noteCents (2) - 100.0) < 0.01;
        row ("1.4", "RPN 0 overrides a channel's range", "+/-2 st, +100.0 ct",
             fmt2 ("+/-%.0f st, %+.1f ct", (double) mpe.channelRangeSemis (2),
                   mpe.noteCents (2)), verdict (ok));
    }

    {
        // The zone reopening must clear the channels, or a stale offset
        // survives a reconfiguration the host believes cleared it.
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::detect);
        sendMcm (mpe, 1, 15);
        mpe.pitchWheel (2, 12000);
        const double before = mpe.noteCents (2);
        sendMcm (mpe, 1, 15);
        const double after = mpe.noteCents (2);
        row ("1.5", "reconfiguring clears the member channels",
             "nonzero -> 0.0 ct", fmt2 ("%+.1f -> %+.1f ct", before, after),
             verdict (before != 0.0 && after == 0.0));
    }

    {
        // Off is off: a host that sends the whole configuration still gets
        // the instrument exactly as it was before this existed.
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::off);
        sendMcm (mpe, 1, 15);
        const bool consumed = mpe.pitchWheel (2, 16383);
        row ("1.6", "Off ignores the configuration entirely", "inactive, wheel is bend",
             fmt ("%.1f ct", mpe.noteCents (2)),
             verdict (! mpe.isActive() && ! consumed && mpe.noteCents (2) == 0.0f));
    }

    {
        // On assumes the standard zone without waiting to be told.
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::on);
        mpe.pitchWheel (5, wheelFor (25.0, MpeTuning::kMemberRangeSemis));
        row ("1.7", "On assumes the standard lower zone", "member, ~+25 ct",
             fmt ("%+.2f ct", mpe.noteCents (5)),
             verdict (mpe.isMember (5) && std::abs (mpe.noteCents (5) - 25.0) < 0.6));
    }

    {
        // On still listens: a player who forces it on and then loads a host
        // that configures a narrower zone gets that zone, not fifteen
        // channels of guesswork. A zero count is the one thing it refuses,
        // because a forced-on instrument with no member channels is not a
        // state anyone asked for.
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::on);
        sendMcm (mpe, 1, 8);
        const bool narrowed = mpe.memberCount (true) == 8
                           && mpe.isMember (9) && ! mpe.isMember (10);
        sendMcm (mpe, 1, 0);
        row ("1.8", "On honours a narrower zone, refuses none",
             "8 members, then 8", fmt2 ("%.0f members, then %.0f",
                                        8.0, (double) mpe.memberCount (true)),
             verdict (narrowed && mpe.memberCount (true) == 8));
    }
}

// ===========================================================================
// 2. Bit-identity: an instrument nobody has retuned
// ===========================================================================
static void sectionIdentity()
{
    heading ("2. No per-note tuning present: the instrument is untouched");

    // The phrase: a chord under the pedal, a repeated note, a half-pedal, a
    // stop. It walks the strike path, the sympathetic path, the priority
    // rebuild and the pedal, so a per-note field leaking anywhere shows here.
    auto phrase = [] (float ct0, float ct1, float ct2)
    {
        return std::vector<TimedEvent> {
            { 0,     { 0, NoteEvent::sustainOn, 0, 1.0f } },
            { 0,     { 0, NoteEvent::noteOn, 52, 0.62f, ct0 } },
            { 2400,  { 0, NoteEvent::noteOn, 59, 0.78f, ct1 } },
            { 4800,  { 0, NoteEvent::noteOn, 64, 0.41f, ct2 } },
            { 12000, { 0, NoteEvent::noteOn, 64, 0.90f, ct2 } },
            { 24000, { 0, NoteEvent::noteOff, 52, 0.0f } },
            { 36000, { 0, NoteEvent::sustain, 0, 0.4f } },
        };
    };

    for (int inst = 0; inst < 5; ++inst)
    {
        // (a) the events a caller that has never heard of per-note tuning
        // builds -- the field left at its default.
        std::vector<TimedEvent> plain = {
            { 0,     { 0, NoteEvent::sustainOn, 0, 1.0f } },
            { 0,     { 0, NoteEvent::noteOn, 52, 0.62f } },
            { 2400,  { 0, NoteEvent::noteOn, 59, 0.78f } },
            { 4800,  { 0, NoteEvent::noteOn, 64, 0.41f } },
            { 12000, { 0, NoteEvent::noteOn, 64, 0.90f } },
            { 24000, { 0, NoteEvent::noteOff, 52, 0.0f } },
            { 36000, { 0, NoteEvent::sustain, 0, 0.4f } },
        };

        // (b) the same phrase through a live MPE session that never opens a
        // zone: the host sends wheel and controller traffic, the register
        // stays shut, and every note-on is cut to nominal.
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::detect);
        mpe.pitchWheel (2, 3000);
        sendBendRange (mpe, 2, 12, 0);
        std::vector<TimedEvent> viaMpe = plain;
        for (auto& te : viaMpe)
            if (te.ev.type == NoteEvent::noteOn)
                te.ev = midiNoteOn (mpe, 2, te.ev.note, te.ev.velocity);
        for (std::size_t i = 0; i < plain.size(); ++i) viaMpe[i].sample = plain[i].sample;

        const Stereo a = render (inst, 3.0, plain);
        const Stereo b = render (inst, 3.0, viaMpe);
        const Stereo c = render (inst, 3.0, phrase (0.0f, 0.0f, 0.0f));

        char id[8];
        std::snprintf (id, sizeof id, "2.%d", inst);
        row (id, (std::string (kInstName[inst]) + ": default == MPE-shut == explicit 0").c_str(),
             "bit-identical", a.sameSamplesAs (b) && a.sameSamplesAs (c) ? "identical" : "DIFFERS",
             verdict (a.sameSamplesAs (b) && a.sameSamplesAs (c)));
    }
}

// ===========================================================================
// 3. A per-note offset is heard, at the size it asked for
// ===========================================================================
static void sectionShift()
{
    heading ("3. The struck note sounds at the offset it was cut to");

    // Not a round number and not a multiple of anything: a shift that came
    // from rounding or from a quantised table cannot fake it.
    const float offset = 37.5f;

    for (int inst = 0; inst < 5; ++inst)
    {
        const std::vector<TimedEvent> nominal = { { 0, { 0, NoteEvent::noteOn, 60, 0.7f } } };
        const std::vector<TimedEvent> tuned   = { { 0, { 0, NoteEvent::noteOn, 60, 0.7f, offset } } };

        const double f0 = measuredF0 (render (inst, 2.5, nominal), 60, 0.3, 1.6);
        const double f1 = measuredF0 (render (inst, 2.5, tuned),   60, 0.3, 1.6);
        const double got = centsBetween (f1, f0);

        // The bound is the analysis, not the model: detuneCents enters the
        // geometry solve as an exact factor 2^(c/1200) on the note frequency
        // in all five voices, so the target is the offset itself and the only
        // slack is the centroid read. Measured across every row in this
        // suite, the worst residual is the grand's 0.30 ct -- its unison
        // cluster reshaping as it moves -- and the other four sit under
        // 0.05 ct. One cent fences that with a threefold margin and is still
        // inside what a listener can hear on a sustained tone.
        const bool ok = std::abs (got - offset) < 1.0;
        char id[8];
        std::snprintf (id, sizeof id, "3.%d", inst);
        row (id, (std::string (kInstName[inst]) + ": measured shift at +37.5 ct").c_str(),
             "+37.5 ct +/-1.0", fmt2 ("%+.2f ct (%.2f Hz)", got, f1), verdict (ok));
    }
}

// ===========================================================================
// 4. A mid-note change reaches the next note, not the ringing one
// ===========================================================================
static void sectionMidNote()
{
    heading ("4. A change while the string rings does not retune it");

    // The action the instrument does not have: a tuning pin turned mid-note.
    // The design forbids it structurally -- nothing but a note-on reads the
    // offset -- and these rows measure that rather than asserting it.
    const int wheel = wheelFor (60.0, MpeTuning::kMemberRangeSemis);
    const double asked = centsOfWheel (wheel, MpeTuning::kMemberRangeSemis);

    for (int inst = 0; inst < 5; ++inst)
    {
        MpeTuning quiet, moved;
        for (auto* m : { &quiet, &moved })
        {
            m->setMode (MpeTuning::Mode::detect);
            sendMcm (*m, 1, 15);
        }

        // Control: note 60 on channel 2, wheel never touched.
        std::vector<TimedEvent> ctl = {
            { 0, { 0, NoteEvent::noteOn, 60, 0.7f, quiet.noteCents (2) } },
            { static_cast<int> (2.0 * kFs), midiNoteOn (quiet, 2, 66, 0.7f) } };

        // The same, except the host moves channel 2's wheel one second in.
        // A wheel move produces no event at all -- that is the whole point --
        // so the second note simply reads a different register.
        std::vector<TimedEvent> mv;
        mv.push_back ({ 0, midiNoteOn (moved, 2, 60, 0.7f) });
        moved.pitchWheel (2, wheel);
        mv.push_back ({ static_cast<int> (2.0 * kFs), midiNoteOn (moved, 2, 66, 0.7f) });

        const Stereo a = render (inst, 4.0, ctl);
        const Stereo b = render (inst, 4.0, mv);

        // The ringing note: measured over the window that starts after the
        // wheel moved and ends before the second note lands.
        const double fa = measuredF0 (a, 60, 1.1, 1.9);
        const double fb = measuredF0 (b, 60, 1.1, 1.9);
        const double drift = centsBetween (fb, fa);

        // The first two seconds of both renders carry nothing but the first
        // note and a wheel move; if the move reached the string at all, the
        // samples differ. This is the strong form of the row.
        bool sameUntilSecondNote = true;
        for (std::size_t i = 0; i < static_cast<std::size_t> (2.0 * kFs); ++i)
            if (a.L[i] != b.L[i] || a.R[i] != b.R[i]) { sameUntilSecondNote = false; break; }

        char id[8];
        std::snprintf (id, sizeof id, "4.%d", inst);
        row (id, (std::string (kInstName[inst]) + ": ringing note ignores the move").c_str(),
             "bit-identical, 0 ct",
             std::string (sameUntilSecondNote ? "identical" : "DIFFERS")
                 + fmt (", %+.2f ct", drift),
             verdict (sameUntilSecondNote && std::abs (drift) < 0.5));

        // And the note struck after the move IS tuned to it: the tuner
        // reached the next string.
        const double f66a = measuredF0 (a, 66, 2.3, 3.6);
        const double f66b = measuredF0 (b, 66, 2.3, 3.6);
        const double got = centsBetween (f66b, f66a);
        std::snprintf (id, sizeof id, "4.%d+", inst);
        row (id, (std::string (kInstName[inst]) + ": the NEXT note takes it").c_str(),
             fmt ("%+.1f ct +/-1.0", asked), fmt ("%+.2f ct", got),
             verdict (std::abs (got - asked) < 1.0));
    }
}

// ===========================================================================
// 5. Two channels, two offsets, one chord
// ===========================================================================
static void sectionSimultaneous()
{
    heading ("5. Two notes on two channels ring at two pitches at once");

    // A tritone, so neither fundamental sits on a low harmonic of the other
    // and the two heterodynes cannot borrow from each other.
    const int wheelLo = wheelFor (-42.0, MpeTuning::kMemberRangeSemis);
    const int wheelHi = wheelFor (+31.0, MpeTuning::kMemberRangeSemis);
    const double askLo = centsOfWheel (wheelLo, MpeTuning::kMemberRangeSemis);
    const double askHi = centsOfWheel (wheelHi, MpeTuning::kMemberRangeSemis);

    for (int inst = 0; inst < 5; ++inst)
    {
        MpeTuning mpe;
        mpe.setMode (MpeTuning::Mode::detect);
        sendMcm (mpe, 1, 15);
        mpe.pitchWheel (2, wheelLo);
        mpe.pitchWheel (3, wheelHi);

        const std::vector<TimedEvent> both = {
            { 0, midiNoteOn (mpe, 2, 60, 0.7f) },
            { 0, midiNoteOn (mpe, 3, 66, 0.7f) } };
        const std::vector<TimedEvent> ref = {
            { 0, { 0, NoteEvent::noteOn, 60, 0.7f } },
            { 0, { 0, NoteEvent::noteOn, 66, 0.7f } } };

        const Stereo s = render (inst, 2.5, both);
        const Stereo r = render (inst, 2.5, ref);

        const double lo = centsBetween (measuredF0 (s, 60, 0.3, 1.6),
                                        measuredF0 (r, 60, 0.3, 1.6));
        const double hi = centsBetween (measuredF0 (s, 66, 0.3, 1.6),
                                        measuredF0 (r, 66, 0.3, 1.6));
        const bool ok = std::abs (lo - askLo) < 1.0 && std::abs (hi - askHi) < 1.0;

        char id[8];
        std::snprintf (id, sizeof id, "5.%d", inst);
        row (id, (std::string (kInstName[inst]) + ": ch2 flat, ch3 sharp, together").c_str(),
             fmt2 ("%+.1f / %+.1f ct", askLo, askHi),
             fmt2 ("%+.2f / %+.2f ct", lo, hi), verdict (ok));
    }
}

// ===========================================================================
// 6. Per-note tuning composes with the workshop
// ===========================================================================
static void sectionCompose()
{
    heading ("6. The tuner and the modder work on the same string");

    // The workshop's length trim retunes a tine by the beam equation's own
    // 1/L^2, and per-note tuning is a factor 2^(c/1200) on the note that trim
    // leaves. Both enter the same geometry solve, so what they do must add in
    // cents -- if per-note tuning replaced the trim rather than composing with
    // it, this row reads the tuning alone.
    const double lenScale = 0.99;                       // a shorter tine
    const double trimCents = 1200.0 * std::log2 (1.0 / (lenScale * lenScale));
    const float  tuneCents = -18.0f;

    struct Case { const char* name; bool trim; float cents; };
    const Case cases[] = {
        { "length trim alone",  true,  0.0f },
        { "per-note tuning alone", false, tuneCents },
        { "both",               true,  tuneCents } };

    double got[3] = {};
    double base = 0.0;
    for (int c = -1; c < 3; ++c)
    {
        EpiEngine e;
        e.prepare (kFs, kBlock);
        const bool trim  = c >= 0 && cases[c].trim;
        const float cts  = c >= 0 ? cases[c].cents : 0.0f;
        if (trim) e.setTineMod (60 - EpiEngine::kLoNote, static_cast<float> (lenScale), 1.0f);

        const int N = static_cast<int> (kFs * 2.5);
        Stereo s;
        s.L.assign (static_cast<std::size_t> (N), 0.0f);
        s.R.assign (static_cast<std::size_t> (N), 0.0f);
        const EngineParams p = measParams (0);
        NoteEvent on { 0, NoteEvent::noteOn, 60, 0.7f, cts };
        for (int i = 0; i < N; i += kBlock)
        {
            const int n = std::min (kBlock, N - i);
            e.process (s.L.data() + i, s.R.data() + i, n, p, i == 0 ? &on : nullptr, i == 0 ? 1 : 0);
        }
        const double f = measuredF0 (s, 60, 0.3, 1.6);
        if (c < 0) base = f; else got[c] = centsBetween (f, base);
    }

    row ("6.1", "Tine: length trim 0.99 alone", fmt ("%+.1f ct +/-1.0", trimCents),
         fmt ("%+.2f ct", got[0]), verdict (std::abs (got[0] - trimCents) < 1.0));
    row ("6.2", "Tine: per-note tuning alone", fmt ("%+.1f ct +/-1.0", (double) tuneCents),
         fmt ("%+.2f ct", got[1]), verdict (std::abs (got[1] - tuneCents) < 1.0));
    row ("6.3", "Tine: both, and they add", fmt ("%+.1f ct +/-1.0", trimCents + tuneCents),
         fmt ("%+.2f ct", got[2]), verdict (std::abs (got[2] - (trimCents + tuneCents)) < 1.0));
}

// ===========================================================================
int main()
{
    std::printf ("Epi per-note tuning suite\n");
    std::printf ("  %-5s %-44s %-24s %-28s %s\n", "id", "property", "target", "measured", "verdict");

    sectionRegister();
    sectionIdentity();
    sectionShift();
    sectionMidNote();
    sectionSimultaneous();
    sectionCompose();

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
