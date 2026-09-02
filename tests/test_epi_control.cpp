/*
  Epi — external control surface testbench.

  src/epi/ControlMap.h is a published interface. Somebody's encoder box,
  somebody's controller template and somebody's sequencer lane are keyed to
  the numbers in it, and a number that moves silently breaks all three without
  breaking a build. So this suite is mostly a PIN: it carries its own literal
  copy of every assignment, written out independently of the header, and
  compares the two. Changing an assignment then costs a deliberate edit in two
  places, which is the point -- it makes the change a decision instead of a
  side effect of adding a parameter.

  The rest measures the reading and writing machinery: that fourteen bits
  survive a round trip, that a choice parameter lands back on the index it
  left from, that data entry goes to whichever of RPN and NRPN was selected
  last -- the instrument reads RPN for MPE tuning, so getting that wrong walks
  an ordinary NRPN sweep into the pitch bend range -- and that the feedback
  poll never echoes a value back at the encoder that just sent it.

  Build (framework-free, no JUCE):
    c++ -std=c++20 -O3 -DNDEBUG -Isrc tests/test_epi_control.cpp -o epi_control_tests
*/

#include "epi/ControlMap.h"
#include "epi/MidiControlSurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace epi;

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

// ===========================================================================
// 1. The pin
//
// Written out by hand, not read from the header. Two independent statements
// of the same fact is the only arrangement in which one of them drifting can
// be detected.
// ===========================================================================

struct Pin { const char* id; int cc; int nrpn; };

static constexpr Pin kPinned[] = {
    { "instrument",    90,  0 },
    { "tune",          28,  1 },
    { "clarity",        3,  2 },
    { "velCurve",      20,  8 },
    { "hammerHard",    73,  9 },
    { "hammerMass",    21, 10 },
    { "escapement",    22, 11 },
    { "strikeNoise",   23, 12 },
    { "damperGrip",    75, 13 },
    { "keyBed",        24, 14 },
    { "hammerMat",     25, 15 },
    { "damperFelt",    26, 16 },
    { "softMode",      27, 17 },
    { "tipMass",       70, 24 },
    { "resDamp",       72, 25 },
    { "barCouple",     29, 26 },
    { "barTune",       30, 27 },
    { "nonlinAmt",     85, 28 },
    { "material",      86, 29 },
    { "wearAmount",    89, 30 },
    { "bodyMix",       31, 36 },
    { "bodyMat",       87, 37 },
    { "bodySize",      88, 38 },
    { "pickupPos",    102, 44 },
    { "pickupDist",   103, 45 },
    { "pickupSel",    106, 46 },
    { "clavSwitch",   107, 47 },
    { "coilFreq",     104, 48 },
    { "coilQ",        105, 49 },
    { "coilSat",       71, 50 },
    { "preampDrive",  108, 56 },
    { "bass",         109, 57 },
    { "treble",        74, 58 },
    { "cabMix",       111, 59 },
    { "clavBrill",    116, 60 },
    { "clavTreb",     117, 61 },
    { "clavMed",      118, 62 },
    { "clavSoft",     119, 63 },
    { "tremRate",      76, 70 },
    { "tremDepth",     92, 71 },
    { "tremStereo",   110, 72 },
    { "phaserMix",     95, 73 },
    { "phaserRate",   112, 74 },
    { "phaserDepth",  113, 75 },
    { "phaserFb",     114, 76 },
    { "spaceMix",      91, 82 },
    { "spaceSize",    115, 83 },
    { "roomProfile",    9, 84 },
    { "outGain",        7, 85 },
};

static constexpr int kNumPinned = (int) (sizeof (kPinned) / sizeof (kPinned[0]));

static void sectionPin()
{
    heading ("1. Published numbers are what they were");

    row ("1.1", "map covers the pinned set", std::to_string (kNumPinned),
         std::to_string (kNumControls), verdict (kNumControls == kNumPinned));

    if (kNumControls != kNumPinned)
        return;

    int badId = 0, badCc = 0, badNrpn = 0;
    std::string firstBad;
    for (int i = 0; i < kNumControls; ++i)
    {
        const auto& a = kControlMap[i];
        const auto& p = kPinned[i];
        if (std::string (a.paramId) != p.id)
        {
            ++badId;
            if (firstBad.empty()) firstBad = std::string (p.id) + " -> " + a.paramId;
        }
        else
        {
            if (a.cc   != p.cc)   { ++badCc;   if (firstBad.empty()) firstBad = std::string (p.id) + " cc " + std::to_string (p.cc) + " -> " + std::to_string (a.cc); }
            if (a.nrpn != p.nrpn) { ++badNrpn; if (firstBad.empty()) firstBad = std::string (p.id) + " nrpn " + std::to_string (p.nrpn) + " -> " + std::to_string (a.nrpn); }
        }
    }

    row ("1.2", "every parameter id in its pinned slot", "0 moved",
         std::to_string (badId) + " moved", verdict (badId == 0));
    row ("1.3", "every CC number unchanged", "0 changed",
         std::to_string (badCc) + " changed", verdict (badCc == 0));
    row ("1.4", "every NRPN number unchanged", "0 changed",
         std::to_string (badNrpn) + " changed", verdict (badNrpn == 0));
    if (! firstBad.empty())
        row ("1.5", "first difference", "-", firstBad, Verdict::info);
}

// ===========================================================================
// 2. The map is self-consistent
// ===========================================================================

static void sectionConsistency()
{
    heading ("2. No collisions, nothing reserved");

    std::set<std::string> ids;
    std::set<int> ccs, nrpns;
    int dupId = 0, dupCc = 0, dupNrpn = 0;
    for (int i = 0; i < kNumControls; ++i)
    {
        if (! ids.insert (kControlMap[i].paramId).second) ++dupId;
        if (kControlMap[i].cc >= 0 && ! ccs.insert (kControlMap[i].cc).second) ++dupCc;
        if (! nrpns.insert (kControlMap[i].nrpn).second) ++dupNrpn;
    }
    row ("2.1", "no parameter appears twice", "0", std::to_string (dupId), verdict (dupId == 0));
    row ("2.2", "no CC number used twice",    "0", std::to_string (dupCc), verdict (dupCc == 0));
    row ("2.3", "no NRPN number used twice",  "0", std::to_string (dupNrpn), verdict (dupNrpn == 0));

    // Controllers the protocol or the instrument already owns. Assigning a
    // parameter to one of these does not produce a warning, it produces an
    // instrument whose sustain pedal also moves the output level.
    const std::set<int> reserved = {
        0, 32,          // bank select
        1,              // modulation, left for the host to map
        6, 38,          // data entry
        11,             // expression, read by the engine
        64, 66, 67,     // sustain (continuously), sostenuto, soft
        96, 97,         // data increment / decrement
        98, 99,         // NRPN select
        100, 101        // RPN select, read by the MPE tuner
    };
    int clash = 0;
    std::string which;
    for (int i = 0; i < kNumControls; ++i)
    {
        const int cc = kControlMap[i].cc;
        if (cc < 0) continue;
        if (reserved.count (cc) || cc >= 120)
        {
            ++clash;
            if (which.empty()) which = std::string (kControlMap[i].paramId) + " on CC " + std::to_string (cc);
        }
    }
    row ("2.4", "no CC collides with a reserved one", "0",
         std::to_string (clash) + (which.empty() ? "" : " (" + which + ")"), verdict (clash == 0));

    int outOfRange = 0;
    for (int i = 0; i < kNumControls; ++i)
    {
        if (kControlMap[i].cc >= 0 && (kControlMap[i].cc < 0 || kControlMap[i].cc > 119)) ++outOfRange;
        if (kControlMap[i].nrpn < 0 || kControlMap[i].nrpn > 127) ++outOfRange;
    }
    row ("2.5", "every number is inside its field", "0", std::to_string (outOfRange),
         verdict (outOfRange == 0));

    // The lookups are what the decoder runs on; a map that is consistent but
    // unsearchable is no better than a wrong one.
    int lost = 0;
    for (int i = 0; i < kNumControls; ++i)
    {
        if (controlIndexForNrpn (kNrpnBank, kControlMap[i].nrpn) != i) ++lost;
        if (kControlMap[i].cc >= 0 && controlIndexForCc (kControlMap[i].cc) != i) ++lost;
    }
    row ("2.6", "every entry is found by its own numbers", "0", std::to_string (lost),
         verdict (lost == 0));
    row ("2.7", "an unassigned NRPN finds nothing", "-1",
         std::to_string (controlIndexForNrpn (kNrpnBank, 127)),
         verdict (controlIndexForNrpn (kNrpnBank, 127) < 0));
    row ("2.8", "another bank finds nothing", "-1",
         std::to_string (controlIndexForNrpn (1, kControlMap[0].nrpn)),
         verdict (controlIndexForNrpn (1, kControlMap[0].nrpn) < 0));
}

// ===========================================================================
// 3. Values survive the wire
// ===========================================================================

static void sectionResolution()
{
    heading ("3. What goes out comes back");

    using S = MidiControlSurface;

    double worst14 = 0.0;
    for (int i = 0; i <= 2000; ++i)
    {
        const float n = (float) i / 2000.0f;
        worst14 = std::max (worst14, std::abs ((double) S::from14 (S::to14 (n)) - n));
    }
    // Half a step of 1/16383 is the best a rounding encoder can do.
    row ("3.1", "14-bit round trip error", "<= 3.1e-5", fmt ("%.2e", worst14),
         verdict (worst14 <= 3.1e-5));

    double worst7 = 0.0;
    for (int i = 0; i <= 2000; ++i)
    {
        const float n = (float) i / 2000.0f;
        worst7 = std::max (worst7, std::abs ((double) S::from7 (S::to7 (n)) - n));
    }
    row ("3.2", "7-bit round trip error", "<= 4.0e-3", fmt ("%.2e", worst7),
         verdict (worst7 <= 4.0e-3));

    // A choice parameter is normalised as index/(n-1). If that does not come
    // back as the same index, a controller echo walks the selector one step
    // per message -- the instrument would change by itself.
    //
    // Sweeping to the BREAKING POINT rather than to the largest selector the
    // instrument currently has: at five choices the quantisation error is a
    // fiftieth of a step and the row cannot fail, which makes it worth
    // nothing. The number of choices that still round-trips exactly is a real
    // property of the wire scaling -- it is 128 for seven bits and 16384 for
    // fourteen only if encode and decode agree -- so that is what is pinned.
    // It also says plainly how large a selector may grow before CC stops
    // being able to address it.
    auto largestExact = [] (auto to, auto from, int limit)
    {
        for (int n = 2; n <= limit; ++n)
            for (int idx = 0; idx < n; ++idx)
            {
                const float norm = (float) idx / (float) (n - 1);
                if ((int) std::lround (from (to (norm)) * (n - 1)) != idx)
                    return n - 1;
            }
        return limit;
    };

    const int exact7  = largestExact (S::to7,  S::from7,  200);
    const int exact14 = largestExact (S::to14, S::from14, 300);
    row ("3.3", "choices that round trip exactly, 7-bit", "128",
         std::to_string (exact7), verdict (exact7 == 128));
    row ("3.4", "choices that round trip exactly, 14-bit", ">= 300",
         std::to_string (exact14), verdict (exact14 >= 300));
    // Measured, not asserted, in the state suite's S14 -- this suite cannot
    // see the instrument. Recorded here because it is the number that says
    // how much of the margin above is actually being used.
    row ("3.5", "widest selector the instrument has", "-", "8 (material), of 128 addressable",
         Verdict::info);
}

// ===========================================================================
// 4. Reading
// ===========================================================================

namespace
{
    struct Capture
    {
        int calls = 0, lastIndex = -1;
        float lastValue = -1.0f;
        void operator() (int i, float v) { ++calls; lastIndex = i; lastValue = v; }
    };
}

static void sectionReading()
{
    heading ("4. Reading controllers");

    const int idx = controlIndexForCc (kControlMap[0].cc);

    {
        MidiControlSurface s;
        Capture cap;
        const bool used = s.controller (1, kControlMap[0].cc, 127,
                                        [&cap] (int i, float v) { cap (i, v); });
        row ("4.1", "a mapped CC is consumed and applied", "yes, 1.000",
             std::string (used ? "yes, " : "no, ") + fmt ("%.3f", cap.lastValue),
             verdict (used && cap.calls == 1 && cap.lastIndex == idx
                      && std::abs (cap.lastValue - 1.0f) < 1.0e-6f));
    }

    {
        MidiControlSurface s;
        Capture cap;
        // CC 3 is assigned; CC 14 is a documented spare and must pass through
        // untouched, or a controller sending it would silently move something.
        const bool used = s.controller (1, 14, 64, [&cap] (int i, float v) { cap (i, v); });
        row ("4.2", "an unmapped CC is left alone", "not consumed",
             used ? "consumed" : "not consumed", verdict (! used && cap.calls == 0));
    }

    {
        // The full four-message NRPN write, on the last parameter in the map
        // so an off-by-one in the lookup cannot pass.
        const auto& target = kControlMap[kNumControls - 1];
        MidiControlSurface s;
        Capture cap;
        auto f = [&cap] (int i, float v) { cap (i, v); };
        s.controller (1, 99, kNrpnBank, f);
        s.controller (1, 98, target.nrpn, f);
        s.controller (1, 6,  100, f);
        s.controller (1, 38, 42, f);
        const int want = (100 << 7) | 42;
        row ("4.3", "NRPN writes 14 bits to the right parameter",
             std::to_string (want) + "/16383",
             std::to_string ((int) std::lround (cap.lastValue * 16383.0f)) + "/16383",
             verdict (cap.lastIndex == kNumControls - 1
                      && std::abs (cap.lastValue - MidiControlSurface::from14 (want)) < 1.0e-6f));
    }

    {
        // A controller that sends no low byte at all still has to work.
        const auto& target = kControlMap[3];
        MidiControlSurface s;
        Capture cap;
        auto f = [&cap] (int i, float v) { cap (i, v); };
        s.controller (1, 99, kNrpnBank, f);
        s.controller (1, 98, target.nrpn, f);
        s.controller (1, 6,  64, f);
        row ("4.4", "MSB alone applies, within one part in 128", "0.5000 +/-0.008",
             fmt ("%.4f", cap.lastValue),
             verdict (cap.calls == 1 && std::abs (cap.lastValue - 0.5f) < 0.008f));
    }

    {
        // The arbitration that matters. RPN 0 is pitch bend sensitivity and
        // the MPE tuner owns it; once an RPN is selected, data entry is not
        // ours and must reach it untouched.
        MidiControlSurface s;
        Capture cap;
        auto f = [&cap] (int i, float v) { cap (i, v); };
        s.controller (1, 99, kNrpnBank, f);
        s.controller (1, 98, kControlMap[0].nrpn, f);
        s.controller (1, 101, 0, f);            // RPN MSB -- selector changes owner
        s.controller (1, 100, 0, f);            // RPN LSB
        const bool used = s.controller (1, 6, 48, f);
        row ("4.5", "after an RPN select, data entry is not ours", "not consumed, 0 applied",
             std::string (used ? "consumed, " : "not consumed, ") + std::to_string (cap.calls),
             verdict (! used && cap.calls == 0));
    }

    {
        // ...and it comes back when an NRPN is selected again.
        MidiControlSurface s;
        Capture cap;
        auto f = [&cap] (int i, float v) { cap (i, v); };
        s.controller (1, 101, 0, f);
        s.controller (1, 100, 0, f);
        s.controller (1, 99, kNrpnBank, f);
        s.controller (1, 98, kControlMap[1].nrpn, f);
        const bool used = s.controller (1, 6, 48, f);
        row ("4.6", "selecting an NRPN takes data entry back", "consumed, 1 applied",
             std::string (used ? "consumed, " : "not consumed, ") + std::to_string (cap.calls),
             verdict (used && cap.calls == 1 && cap.lastIndex == 1));
    }

    {
        // An NRPN nobody has assigned must be swallowed, not applied. It is
        // still ours -- the selector says so -- but it points at nothing.
        MidiControlSurface s;
        Capture cap;
        auto f = [&cap] (int i, float v) { cap (i, v); };
        s.controller (1, 99, kNrpnBank, f);
        s.controller (1, 98, 127, f);
        const bool used = s.controller (1, 6, 64, f);
        row ("4.7", "an unassigned NRPN applies nothing", "consumed, 0 applied",
             std::string (used ? "consumed, " : "not consumed, ") + std::to_string (cap.calls),
             verdict (used && cap.calls == 0));
    }

    {
        // Channels are independent: two controllers on two channels each
        // holding their own selector must not read each other's.
        MidiControlSurface s;
        Capture cap;
        auto f = [&cap] (int i, float v) { cap (i, v); };
        s.controller (1,  99, kNrpnBank, f);
        s.controller (1,  98, kControlMap[0].nrpn, f);
        s.controller (10, 99, kNrpnBank, f);
        s.controller (10, 98, kControlMap[5].nrpn, f);
        s.controller (1,  6, 127, f);
        const int onCh1 = cap.lastIndex;
        s.controller (10, 6, 127, f);
        const int onCh10 = cap.lastIndex;
        row ("4.8", "NRPN selectors are per channel", "0 then 5",
             std::to_string (onCh1) + " then " + std::to_string (onCh10),
             verdict (onCh1 == 0 && onCh10 == 5));
    }
}

// ===========================================================================
// 5. Writing back
// ===========================================================================

static void sectionFeedback()
{
    heading ("5. Feedback to a physical panel");

    std::array<float, kNumControls> value {};
    value.fill (0.25f);
    auto read = [&value] (int i) { return value[(size_t) i]; };

    {
        MidiControlSurface s;
        int sent = 0;
        auto send = [&sent] (int, int) { ++sent; };

        // First poll: nothing is known about the far end, so everything is
        // stated. Budget applies, so it takes several ticks.
        int reported = 0, ticks = 0;
        while (reported < kNumControls && ticks < 100)
        {
            reported += s.collectFeedback (read, send, true, false, 8);
            ++ticks;
        }
        row ("5.1", "a cold panel is told every parameter", std::to_string (kNumControls),
             std::to_string (reported), verdict (reported == kNumControls));
        row ("5.2", "the budget is respected", ">= 7 ticks", std::to_string (ticks) + " ticks",
             verdict (ticks >= kNumControls / 8));
        row ("5.3", "NRPN costs four messages each", std::to_string (4 * kNumControls),
             std::to_string (sent), verdict (sent == 4 * kNumControls));

        // Second pass over a still instrument: silence.
        const int quiet = s.collectFeedback (read, send, true, false, kNumControls);
        row ("5.4", "an unchanged parameter is not restated", "0", std::to_string (quiet),
             verdict (quiet == 0));

        // One parameter moves, one parameter is reported.
        value[3] = 0.75f;
        int moved = 0, guard = 0;
        while (moved == 0 && guard++ < 100) moved = s.collectFeedback (read, send, true, false, 8);
        row ("5.5", "a moved parameter is reported once", "1", std::to_string (moved),
             verdict (moved == 1));
    }

    {
        // The loop that must not close. A value arriving over MIDI is a value
        // the far end already has, so the poll must not send it back.
        MidiControlSurface s;
        auto send = [] (int, int) {};
        int guard = 0;
        while (s.collectFeedback (read, send, true, false, kNumControls) > 0 && guard++ < 10) {}

        const int idx = controlIndexForCc (kControlMap[0].cc);
        value[(size_t) idx] = 1.0f;
        s.controller (1, kControlMap[0].cc, 127, [&value] (int i, float v) { value[(size_t) i] = v; });

        const int echoed = s.collectFeedback (read, send, true, false, kNumControls);
        row ("5.6", "a value that arrived is not echoed back", "0", std::to_string (echoed),
             verdict (echoed == 0));
    }

    {
        // The instrument does not hold an incoming value exactly: a choice
        // parameter snaps to an index, a continuous one to its own step.
        // Reporting each of those corrections would put a message on the wire
        // for every message taken off it. Measured on the real parameters the
        // corrections run from 9 to 64 parts in 16384, so one inside a CC step
        // is absorbed.
        MidiControlSurface s;
        auto send = [] (int, int) {};
        int guard = 0;
        while (s.collectFeedback (read, send, true, false, kNumControls) > 0 && guard++ < 10) {}

        const int idx = controlIndexForCc (kControlMap[0].cc);
        s.controller (1, kControlMap[0].cc, 100, [] (int, float) {});
        // ...and the instrument settles 60 units away, as a five-way selector
        // driven from seven bits does.
        value[(size_t) idx] = MidiControlSurface::from14 (MidiControlSurface::to14 (MidiControlSurface::from7 (100)) - 60);
        const int corrected = s.collectFeedback (read, send, true, false, kNumControls);
        row ("5.7", "a sub-CC-step correction is absorbed", "0 sent",
             std::to_string (corrected) + " sent", verdict (corrected == 0));

        // But a correction the sender could not have meant is still news.
        s.controller (1, kControlMap[0].cc, 100, [] (int, float) {});
        value[(size_t) idx] = 0.0f;
        const int big = s.collectFeedback (read, send, true, false, kNumControls);
        row ("5.8", "a correction larger than a CC step is reported", "1 sent",
             std::to_string (big) + " sent", verdict (big == 1));

        // And the absorption is one-shot: a later move is not swallowed too.
        s.controller (1, kControlMap[0].cc, 100, [] (int, float) {});
        value[(size_t) idx] = MidiControlSurface::from14 (MidiControlSurface::to14 (MidiControlSurface::from7 (100)) - 10);
        s.collectFeedback (read, send, true, false, kNumControls);      // absorbs
        value[(size_t) idx] = MidiControlSurface::from14 (MidiControlSurface::to14 (MidiControlSurface::from7 (100)) - 20);
        const int later = s.collectFeedback (read, send, true, false, kNumControls);
        row ("5.9", "absorption is one-shot, the next move is reported", "1 sent",
             std::to_string (later) + " sent", verdict (later == 1));
    }

    {
        // A controller plugged in mid-session knows nothing; resendAll is how
        // it is caught up.
        MidiControlSurface s;
        auto send = [] (int, int) {};
        int guard = 0;
        while (s.collectFeedback (read, send, true, false, kNumControls) > 0 && guard++ < 10) {}
        s.resendAll();
        const int again = s.collectFeedback (read, send, true, false, kNumControls);
        row ("5.10", "resendAll restates everything", std::to_string (kNumControls),
             std::to_string (again), verdict (again == kNumControls));
    }

    {
        // CC feedback covers only the parameters that have one, which is the
        // whole map here -- but the code path must not assume that.
        MidiControlSurface s;
        int sent = 0;
        auto send = [&sent] (int, int) { ++sent; };
        s.collectFeedback (read, send, false, true, kNumControls);
        int withCc = 0;
        for (int i = 0; i < kNumControls; ++i) if (kControlMap[i].cc >= 0) ++withCc;
        row ("5.11", "CC feedback sends one message per assigned CC", std::to_string (withCc),
             std::to_string (sent), verdict (sent == withCc));
    }
}

// ===========================================================================
int main()
{
    std::printf ("Epi external control surface suite\n");
    std::printf ("  %-5s %-44s %-24s %-28s %s\n", "id", "property", "target", "measured", "verdict");

    sectionPin();
    sectionConsistency();
    sectionResolution();
    sectionReading();
    sectionFeedback();

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
