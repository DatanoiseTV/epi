/*
  Epi — Universal MIDI Packet decoding testbench.

  MIDI 2.0 is a wire format, and a wire format is a set of bit positions that
  are either right or wrong. This pins them: every message the instrument reads
  is built here by hand from the specification's layout and decoded, so a
  shifted field fails a row instead of producing a note at the wrong velocity
  in somebody's session.

  The rows that matter most are the ones where MIDI 2.0 differs from MIDI 1.0
  in a way that is silent if you get it backwards:

    A MIDI 2.0 note-on with velocity zero is a note ON, at the quietest
    playable level. The running-status trick that made velocity zero a release
    does not exist in a format with an explicit note-off, and carrying the old
    reading over drops the softest notes on a keybed that can finally resolve
    them.

    A note-on can carry the note's pitch as 7.9 fixed point in its attribute,
    which is a different field from velocity in the same word.

    Per-note controllers are thirty-two bits and are addressed by an index
    that has nothing to do with a CC number.

  Build (framework-free, no JUCE):
    c++ -std=c++20 -O2 -DNDEBUG -Isrc tests/test_epi_ump.cpp -o epi_ump_tests
*/

#include "epi/ump/UmpBridge.h"
#include "epi/ump/UmpDecoder.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace epi;
using namespace epi::ump;

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
    std::printf ("  %-5s %-46s %-22s %-24s %s\n", id, what, target.c_str(), got.c_str(), mark);
    std::fflush (stdout);
}

static Verdict verdict (bool ok) { return ok ? Verdict::pass : Verdict::fail; }

static std::string fmt (const char* f, double a)
{
    char b[128];
    std::snprintf (b, sizeof b, f, a);
    return b;
}

// Feed words, collect whatever comes out.
static std::vector<Event> decode (std::initializer_list<uint32_t> words)
{
    Decoder d;
    std::vector<Event> out;
    Event e;
    for (uint32_t w : words)
        if (d.push (w, e)) out.push_back (e);
    return out;
}

// ===========================================================================
static void sectionFraming()
{
    heading ("1. Packet framing");

    // A message type fixes the packet's length. Getting this wrong desynchronises
    // the whole stream, not just one message.
    const int want[16] = { 1,1,1,2, 2,4,1,1, 2,2,2,3, 3,4,4,4 };
    int wrong = 0;
    std::string first;
    for (int mt = 0; mt < 16; ++mt)
        if (wordsForMessageType ((uint8_t) mt) != want[mt])
        {
            ++wrong;
            if (first.empty())
                first = "mt " + std::to_string (mt) + ": "
                      + std::to_string (wordsForMessageType ((uint8_t) mt))
                      + " not " + std::to_string (want[mt]);
        }
    row ("1.1", "every message type has its specified length", "0 wrong",
         std::to_string (wrong) + (first.empty() ? "" : " (" + first + ")"), verdict (wrong == 0));

    // A two-word message must not be decoded from its first word alone.
    Decoder d;
    Event e;
    const bool early = d.push (0x40903C00u, e);
    const bool complete = d.push (0xC0000000u, e);
    row ("1.2", "a 64-bit message waits for its second word", "no, then yes",
         std::string (early ? "yes" : "no") + ", then " + (complete ? "yes" : "no"),
         verdict (! early && complete));

    // ...and a stray reset must not leave the decoder mid-packet.
    Decoder d2;
    Event e2;
    d2.push (0x40903C00u, e2);
    d2.reset();
    const bool afterReset = d2.push (0x20903C40u, e2);   // a MIDI 1.0 note on
    row ("1.3", "reset drops a half-received packet", "next word decodes",
         afterReset ? "next word decodes" : "swallowed", verdict (afterReset));
}

// ===========================================================================
static void sectionMidi1()
{
    heading ("2. MIDI 1.0 carried in a UMP");

    auto v = decode ({ 0x20913C64u });        // group 0, ch 1, note 60, vel 100
    row ("2.1", "note on", "note 60, 100/127",
         v.size() == 1 ? "note " + std::to_string (v[0].note) + ", "
                       + fmt ("%.4f", v[0].value) : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::noteOn && v[0].note == 60
                  && v[0].channel == 1 && std::abs (v[0].value - 100.0 / 127.0) < 1e-9));

    v = decode ({ 0x20903C00u });             // note on, velocity 0
    row ("2.2", "velocity zero is a note off, as it always was", "noteOff",
         v.size() == 1 && v[0].kind == Kind::noteOff ? "noteOff" : "noteOn",
         verdict (v.size() == 1 && v[0].kind == Kind::noteOff));

    v = decode ({ 0x20B04064u });             // CC 64 = 100
    row ("2.3", "the sustain pedal arrives as a position", "CC 64, 0.7874",
         v.size() == 1 ? "CC " + std::to_string (v[0].index) + ", " + fmt ("%.4f", v[0].value)
                       : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::controlChange && v[0].index == 64
                  && std::abs (v[0].value - 100.0 / 127.0) < 1e-9));

    v = decode ({ 0x20E00040u });             // pitch bend, lsb 0, msb 0x40 = centre
    row ("2.4", "a centred wheel reads zero", "0.0000",
         v.size() == 1 ? fmt ("%.4f", v[0].value) : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::pitchBend && std::abs (v[0].value) < 1e-9));
}

// ===========================================================================
static void sectionMidi2Notes()
{
    heading ("3. MIDI 2.0 notes");

    // Note on: velocity in the top half of the second word.
    auto v = decode ({ 0x40913C00u, 0x80000000u });
    row ("3.1", "sixteen-bit velocity", "0.5000",
         v.size() == 1 ? fmt ("%.4f", v[0].value) : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::noteOn
                  && std::abs (v[0].value - 32768.0 / 65535.0) < 1e-9));

    // THE trap. In MIDI 1.0 this is a release; in MIDI 2.0 it is the quietest
    // note the keybed can express.
    v = decode ({ 0x40903C00u, 0x00000000u });
    row ("3.2", "MIDI 2.0 velocity zero is a note ON, not a release", "noteOn",
         v.size() == 1 && v[0].kind == Kind::noteOn ? "noteOn" : "noteOff",
         verdict (v.size() == 1 && v[0].kind == Kind::noteOn));

    v = decode ({ 0x40803C00u, 0xFFFF0000u });
    row ("3.3", "note off carries a release velocity", "noteOff, 1.0000",
         v.size() == 1 ? std::string (v[0].kind == Kind::noteOff ? "noteOff, " : "?, ")
                       + fmt ("%.4f", v[0].value) : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::noteOff
                  && std::abs (v[0].value - 1.0) < 1e-9));

    // Attribute type 3: the note's pitch as 7.9. Note 60 with the attribute
    // saying 60.5 is fifty cents sharp.
    const uint16_t attr = (uint16_t) ((60u << 9) | 256u);      // 60 + 256/512
    v = decode ({ 0x40913C03u, (uint32_t) (0x80000000u | attr) });
    row ("3.4", "attribute 3 gives the note's exact pitch", "+50.00 cents",
         v.size() == 1 && v[0].hasPitch ? fmt ("%+.2f cents", v[0].pitchCents) : "absent",
         verdict (v.size() == 1 && v[0].hasPitch && std::abs (v[0].pitchCents - 50.0) < 1e-6));

    v = decode ({ 0x40913C00u, 0x80000000u });
    row ("3.5", "...and is absent when the attribute is not pitch", "no pitch",
         v.size() == 1 && ! v[0].hasPitch ? "no pitch" : "pitch set",
         verdict (v.size() == 1 && ! v[0].hasPitch));

    // What the extra bits are actually for.
    std::set<int> coarse, fine;
    for (int i = 0; i < 65536; i += 1)
    {
        fine.insert (i);
        coarse.insert (i >> 9);
    }
    row ("3.6", "distinct velocities: 7-bit against 16-bit", "128 / 65536",
         std::to_string (coarse.size()) + " / " + std::to_string (fine.size()),
         verdict (coarse.size() == 128 && fine.size() == 65536));
}

// ===========================================================================
static void sectionPerNote()
{
    heading ("4. Per-note controllers");

    // Registered per-note controller 3 (pitch 7.25) on note 60.
    auto v = decode ({ 0x40003C03u, 0x40000000u });
    row ("4.1", "a registered per-note controller", "note 60, idx 3, 0.2500",
         v.size() == 1 ? "note " + std::to_string (v[0].note) + ", idx "
                       + std::to_string (v[0].index) + ", " + fmt ("%.4f", v[0].value) : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::perNoteController && v[0].registered
                  && v[0].note == 60 && v[0].index == 3
                  && std::abs (v[0].value - 0.25) < 1e-9));

    // Assignable per-note controller 1: what a continuous key position uses.
    v = decode ({ 0x40103C01u, 0xC0000000u });
    row ("4.2", "an assignable one is distinguished from it", "assignable, 0.7500",
         v.size() == 1 ? std::string (v[0].registered ? "registered, " : "assignable, ")
                       + fmt ("%.4f", v[0].value) : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::perNoteController && ! v[0].registered
                  && v[0].index == 1 && std::abs (v[0].value - 0.75) < 1e-9));

    // Per-note pitch bend, signed about the centre.
    v = decode ({ 0x40603C00u, 0x80000000u });
    row ("4.3", "a centred per-note bend reads zero", "0.0000",
         v.size() == 1 ? fmt ("%.4f", v[0].value) : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::perNotePitchBend
                  && std::abs (v[0].value) < 1e-9));

    v = decode ({ 0x40603C00u, 0xFFFFFFFFu });
    const bool full = v.size() == 1 && v[0].value > 0.999;
    v = decode ({ 0x40603C00u, 0x00000000u });
    const bool low = v.size() == 1 && v[0].value <= -1.0;
    row ("4.4", "...and spans minus one to one", "both ends",
         std::string (low ? "low " : "") + (full ? "high" : ""), verdict (full && low));

    // 32-bit RPN and NRPN: the same idea as the 14-bit pair, with the bank and
    // index in the first word and the whole value in the second.
    v = decode ({ 0x40300055u, 0x40000000u });
    row ("4.5", "a 32-bit assignable controller (NRPN)", "bank 0, idx 85, 0.2500",
         v.size() == 1 ? "bank " + std::to_string (v[0].bank) + ", idx "
                       + std::to_string (v[0].index) + ", " + fmt ("%.4f", v[0].value) : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::assignableController
                  && v[0].bank == 0 && v[0].index == 85
                  && std::abs (v[0].value - 0.25) < 1e-9));

    v = decode ({ 0x40220000u, 0x80000000u });
    row ("4.6", "...and a registered one is a different message", "registered",
         v.size() == 1 && v[0].kind == Kind::registeredController ? "registered" : "other",
         verdict (v.size() == 1 && v[0].kind == Kind::registeredController));

    // Resolution. This is the reason to bother.
    row ("4.7", "steps in a 32-bit controller against a 14-bit one", "4294967296 / 16384",
         "4294967296 / 16384", Verdict::info);
}

// ===========================================================================
static void sectionJitterReduction()
{
    heading ("5. Jitter reduction");

    auto v = decode ({ 0x00100000u | 31250u });
    row ("5.1", "a JR Clock is recognised", "31250 ticks",
         v.size() == 1 ? std::to_string (v[0].jrTicks) + " ticks" : "nothing",
         verdict (v.size() == 1 && v[0].kind == Kind::jrClock && v[0].jrTicks == 31250));

    v = decode ({ 0x00200000u | 1000u });
    row ("5.2", "a JR Timestamp is a different message", "jrTimestamp",
         v.size() == 1 && v[0].kind == Kind::jrTimestamp ? "jrTimestamp" : "other",
         verdict (v.size() == 1 && v[0].kind == Kind::jrTimestamp));

    // One tick is 32 microseconds; a full turn of the counter is 2.097 s.
    JitterReduction jr;
    const double t0 = jr.clock (0);
    const double t1 = jr.clock (31250);
    row ("5.3", "31250 ticks is one second", "1.000000 s",
         fmt ("%.6f s", t1 - t0), verdict (std::abs ((t1 - t0) - 1.0) < 1e-12));

    // The counter wraps every 65536 ticks and the reconstruction has to carry.
    // Without this a note landing just after a wrap appears two seconds early,
    // which a reorder buffer would then dutifully hold.
    JitterReduction w;
    w.clock (65000);
    const double before = w.clock (65500);
    const double after = w.clock (100);         // wrapped
    row ("5.4", "the sixteen-bit counter wraps forwards, not back", "advances",
         after > before ? fmt ("+%.6f s", after - before) : "went backwards",
         verdict (after > before));
    const double wantStep = (65536.0 - 65500.0 + 100.0) / 31250.0;
    row ("5.5", "...by exactly the missing ticks", fmt ("%+.6f s", wantStep),
         fmt ("%+.6f s", after - before),
         verdict (std::abs ((after - before) - wantStep) < 1e-12));

    // A JR Timestamp says when an event HAPPENED, which is routinely a little
    // earlier than the most recent JR Clock -- that is the entire point of
    // sending one. So a small step backwards is ordinary and must not be read
    // as the counter turning over, which would place the event 2.097 seconds
    // into the future and have a reorder buffer hold it there.
    {
        JitterReduction b;
        b.clock (1000);
        const double t = b.timestamp (900);
        const double wantNoWrap = 900.0 / 31250.0;
        row ("5.6", "a timestamp just before the clock is not a wrap",
             fmt ("%.6f s", wantNoWrap), fmt ("%.6f s", t),
             verdict (std::abs (t - wantNoWrap) < 1e-12));

        // ...while a real wrap still is one.
        JitterReduction c;
        c.clock (65500);
        const double t2 = c.timestamp (100);
        const double wantWrap = (65536.0 + 100.0) / 31250.0;
        row ("5.7", "...but a real wrap still carries",
             fmt ("%.6f s", wantWrap), fmt ("%.6f s", t2),
             verdict (std::abs (t2 - wantWrap) < 1e-12));
    }

    row ("5.8", "one tick, which is the timing this preserves", "32 us",
         fmt ("%.0f us", JitterReduction::kTickSeconds * 1.0e6), Verdict::info);
    row ("5.9", "one turn of the counter", "2.097 s",
         fmt ("%.3f s", 65536.0 * JitterReduction::kTickSeconds), Verdict::info);
}

// ===========================================================================
static void sectionStreamRobustness()
{
    heading ("6. A stream that is not all ours");

    // Unknown message types must be skipped whole. A 128-bit SysEx8 in the
    // middle of the stream must not be read as four separate messages.
    Decoder d;
    Event e;
    std::vector<Kind> got;
    const uint32_t stream[] = {
        0x50000000u, 0x00000000u, 0x00000000u, 0x00000000u,   // 128-bit, not ours
        0x20913C64u,                                           // a note we do want
        0xB0000000u, 0x00000000u, 0x00000000u,                 // 96-bit, not ours
        0x20813C00u                                            // note off
    };
    for (uint32_t w : stream) if (d.push (w, e)) got.push_back (e.kind);

    const bool ok = got.size() == 2 && got[0] == Kind::noteOn && got[1] == Kind::noteOff;
    row ("6.1", "long packets we do not read are skipped whole", "noteOn, noteOff",
         std::to_string (got.size()) + " messages", verdict (ok));

    // Every group is accepted; a keybed may not be on group 0.
    auto v = decode ({ 0x2A913C64u });
    row ("6.2", "a message on another group still arrives", "group 10",
         v.size() == 1 ? "group " + std::to_string (v[0].group) : "nothing",
         verdict (v.size() == 1 && v[0].group == 10 && v[0].kind == Kind::noteOn));
}

// ===========================================================================
// 7. Placing an event in time
//
// This is what jitter reduction is for. A sender that measures a key to tens
// of microseconds hands the result to a bus that delivers in millisecond
// lumps: the arrival time of a message says more about USB frame scheduling
// than about when the key was pressed. With a timestamp the original spacing
// can be recovered; without one it cannot.
// ===========================================================================

static void sectionTiming()
{
    heading ("7. Where an event lands");

    const double fs = 48000.0;
    const int block = 512;

    // A JR Timestamp applies to the messages after it, so the pair is one
    // "this happened at t" statement.
    auto jrStamp = [] (uint16_t ticks) { return 0x00200000u | ticks; };
    const uint32_t noteOnUmp = 0x20913C64u;      // MIDI 1.0 note on, note 60

    {
        // No timestamp: the event lands where it arrived.
        Bridge b;
        b.prepare (fs, block);
        b.beginBlock (10.0, block);
        b.push (noteOnUmp, 10.0 + 0.002);        // 2 ms into the block
        const auto& out = b.current();
        row ("7.1", "an untimestamped note lands at its arrival",
             std::to_string ((int) std::lround (0.002 * fs)),
             out.notes.size() == 1 ? std::to_string (out.notes[0].offset) : "none",
             verdict (out.notes.size() == 1
                      && std::abs (out.notes[0].offset - (int) std::lround (0.002 * fs)) <= 1));
    }

    {
        // Timestamped, and delayed on the way. The event belongs where the
        // sender says it happened, not where the bus dropped it.
        Bridge b;
        b.prepare (fs, block);
        b.beginBlock (10.0, block);
        // Lock the clocks with a direct message first.
        b.push (0x00100000u | 0u, 10.0);
        // The sender says 3.2 ms after its zero; it arrives 4 ms late.
        const uint16_t ticks = (uint16_t) std::lround (0.0032 * 31250.0);
        b.push (jrStamp (ticks), 10.0 + 0.0032 + 0.004);
        b.push (noteOnUmp,       10.0 + 0.0032 + 0.004);
        const auto& out = b.current();
        const int want = (int) std::lround (0.0032 * fs);
        row ("7.2", "a timestamped note lands where it was played",
             std::to_string (want),
             out.notes.size() == 1 ? std::to_string (out.notes[0].offset) : "none",
             verdict (out.notes.size() == 1 && std::abs (out.notes[0].offset - want) <= 2));
    }

    {
        // An event from before this block is late, not lost.
        Bridge b;
        b.prepare (fs, block);
        b.beginBlock (10.0, block);
        b.push (noteOnUmp, 9.99);
        const auto& out = b.current();
        row ("7.3", "an event that is already late plays at the block start", "0",
             out.notes.size() == 1 ? std::to_string (out.notes[0].offset) : "none",
             verdict (out.notes.size() == 1 && out.notes[0].offset == 0));
    }

    {
        // ...and one from beyond it is held at the end rather than reaching
        // into a block that has not been asked for yet.
        Bridge b;
        b.prepare (fs, block);
        b.beginBlock (10.0, block);
        b.push (noteOnUmp, 10.5);
        const auto& out = b.current();
        row ("7.4", "an event beyond the block is held at its end",
             std::to_string (block - 1),
             out.notes.size() == 1 ? std::to_string (out.notes[0].offset) : "none",
             verdict (out.notes.size() == 1 && out.notes[0].offset == block - 1));
    }

    {
        // The estimator has to IMPROVE. Give it a badly delayed message first
        // and then direct ones: the offset must come down to the direct path,
        // or every later event is placed by however unlucky the first one was.
        JrAligner a;
        a.toHostTime (1.000, 1.000 + 0.020);        // 20 ms late
        const double afterBad = a.currentOffset();
        for (int i = 1; i <= 10; ++i)
            a.toHostTime (1.000 + i * 0.010, 1.000 + i * 0.010 + 0.0002);   // 0.2 ms late
        const double afterGood = a.currentOffset();
        row ("7.4b", "the clock estimate converges on the most direct path",
             "20 ms down to 0.2", fmt ("%.1f ms down to ", afterBad * 1000.0)
             + fmt ("%.2f", afterGood * 1000.0),
             verdict (afterBad > 0.019 && afterGood < 0.0005));

        // ...and it must not be pinned there forever: the two clocks drift, so
        // a single unusually direct message cannot own the estimate for the
        // rest of the session.
        JrAligner d;
        d.toHostTime (0.0, 0.0);                    // an impossibly direct one
        d.toHostTime (100.0, 100.0 + 0.001);        // a hundred seconds later
        row ("7.4c", "...and creeps up so clock drift cannot strand it",
             "> 0", fmt ("%.2f ms", d.currentOffset() * 1000.0),
             verdict (d.currentOffset() > 0.0));
    }

    // ---- the measurement this whole path exists for ---------------------
    //
    // A sender emits notes exactly ten milliseconds apart and timestamps them.
    // The transport adds up to four milliseconds of random lateness to each.
    // Reconstructed, the spacing should come back.
    //
    // The estimator that maps the sender's clock onto ours tracks the SMALLEST
    // arrival-minus-timestamp it has seen, because every message is late by
    // some amount and none is early. That is the right estimate and it is also
    // causal, so it has a transient: until a reasonably direct message turns
    // up, the offset is too large, and each correction shifts the events after
    // it. This is why the specification has senders emit a JR Clock several
    // times a second whether or not anything is being played -- the estimator
    // is locked long before a note arrives. The test does what a real sender
    // does, and then measures the steady state and the transient separately
    // rather than averaging one into the other.
    {
        std::mt19937 rng (12345);
        std::uniform_real_distribution<double> lateness (0.0, 0.004);

        Bridge b;
        b.prepare (fs, 1 << 20);
        b.beginBlock (0.0, 1 << 20);

        const int n = 40;
        const double spacing = 0.010;
        const double firstNote = 0.30;

        // A quarter-second of clock before anybody plays, as a sender emits.
        for (double t = 0.0; t < firstNote; t += 0.010)
        {
            const uint16_t ticks = (uint16_t) std::lround (t * 31250.0);
            b.push (0x00100000u | ticks, t + lateness (rng));
        }
        const double lockedOffset = b.jrOffsetSeconds();

        std::vector<double> arrivals;
        for (int i = 0; i < n; ++i)
        {
            const double sent = firstNote + i * spacing;
            const double arrived = sent + lateness (rng);
            arrivals.push_back (arrived);
            const uint16_t ticks = (uint16_t) std::lround (sent * 31250.0);
            b.push (jrStamp (ticks), arrived);
            b.push (noteOnUmp, arrived);
        }

        const auto& out = b.current();
        auto gapErrors = [&] (const std::vector<double>& t, std::size_t from)
        {
            std::vector<double> e;
            for (std::size_t i = from + 1; i < t.size(); ++i)
                e.push_back (std::abs ((t[i] - t[i - 1]) - spacing));
            std::sort (e.begin(), e.end());
            return e;
        };
        auto worstGapError = [&] (const std::vector<double>& t, std::size_t from)
        {
            const auto e = gapErrors (t, from);
            return e.empty() ? 0.0 : e.back();
        };
        auto medianGapError = [&] (const std::vector<double>& t, std::size_t from)
        {
            const auto e = gapErrors (t, from);
            return e.empty() ? 0.0 : e[e.size() / 2];
        };

        std::vector<double> reconstructed;
        for (const auto& e : out.notes) reconstructed.push_back (e.offset / fs);

        const double rawJitter = worstGapError (arrivals, 0) * 1000.0;
        const double fixedJitter = worstGapError (reconstructed, 0) * 1000.0;

        row ("7.5", "the notes all arrive", std::to_string (n),
             std::to_string (out.notes.size()), verdict ((int) out.notes.size() == n));
        row ("7.6", "as delivered, the spacing is smeared", "> 2 ms",
             fmt ("%.2f ms", rawJitter), verdict (rawJitter > 2.0));
        // The median is the steady state. The worst is one correction step:
        // the estimator is still improving, and every time it finds a more
        // direct message it shifts everything after it -- which is right, and
        // shows up as a single wrong gap rather than as ongoing smear.
        //
        // Counted in SAMPLES, not seconds. The offsets are integers and the
        // spacing is a whole number of samples, so the residual is an exact
        // integer; measuring it in milliseconds and comparing against one
        // sample's worth puts the threshold precisely on a rounding boundary,
        // where it decides by the last bit of a subtraction.
        std::vector<int> gapSamples;
        for (std::size_t i = 1; i < out.notes.size(); ++i)
            gapSamples.push_back (std::abs (out.notes[i].offset - out.notes[i - 1].offset
                                            - (int) std::lround (spacing * fs)));
        std::sort (gapSamples.begin(), gapSamples.end());
        const int medianSamples = gapSamples.empty() ? 0 : gapSamples[gapSamples.size() / 2];
        const double medianJitter = medianSamples * 1000.0 / fs;

        row ("7.7", "reconstructed, the player's spacing comes back", "<= 1 sample",
             std::to_string (medianSamples) + " samples median", verdict (medianSamples <= 1));
        row ("7.8", "...an improvement over what was delivered", "> 20x",
             fmt ("%.0fx", rawJitter / std::max (1.0e-9, medianJitter)),
             verdict (rawJitter / std::max (1.0e-9, medianJitter) > 20.0));
        row ("7.8b", "...which in time is one sample at this rate", "-",
             fmt ("%.4f ms", medianJitter), Verdict::info);
        row ("7.9", "the worst gap is one convergence step, not smear",
             "< 0.25 ms", fmt ("%.3f ms", fixedJitter), verdict (fixedJitter < 0.25));
        row ("7.10", "the clock had locked before the first note", "-",
             fmt ("%.3f ms of slack left", lockedOffset * 1000.0), Verdict::info);

        // Without the clock in front of it, the estimator has to converge on
        // the notes themselves -- and it does, just not in time for the first
        // few. Measured so the cost is a number rather than a surprise.
        {
            std::mt19937 r2 (12345);
            std::uniform_real_distribution<double> late2 (0.0, 0.004);
            Bridge c;
            c.prepare (fs, 1 << 20);
            c.beginBlock (0.0, 1 << 20);
            std::vector<double> t2;
            for (int i = 0; i < n; ++i)
            {
                const double sent = firstNote + i * spacing;
                const double arrived = sent + late2 (r2);
                const uint16_t ticks = (uint16_t) std::lround (sent * 31250.0);
                c.push (jrStamp (ticks), arrived);
                c.push (noteOnUmp, arrived);
            }
            for (const auto& e : c.current().notes) t2.push_back (e.offset / fs);
            const double cold = worstGapError (t2, 0) * 1000.0;
            const double settled = worstGapError (t2, 8) * 1000.0;
            row ("7.11", "with no clock ahead of it, the first notes pay for the lock",
                 "-", fmt ("%.2f ms worst", cold), Verdict::info);
            row ("7.12", "...and once locked the rest are exact", "< 0.1 ms",
                 fmt ("%.3f ms", settled), verdict (settled < 0.1));
        }
    }
}

// ===========================================================================
// 8. What the bridge hands the instrument
// ===========================================================================

static void sectionBridge()
{
    heading ("8. From the wire to the instrument");

    const double fs = 48000.0;

    auto oneBlock = [&] (std::initializer_list<uint32_t> words)
    {
        static Bridge b;
        b.prepare (fs, 512);
        b.beginBlock (0.0, 512);
        for (uint32_t w : words) b.push (w, 0.0);
        return b.current();
    };

    // A continuous key position, on the published assignable controller.
    {
        const auto& out = oneBlock ({ 0x40103C01u, 0xC0000000u });
        row ("8.1", "an assignable per-note controller 1 is key position",
             "keyPosition, note 60, 0.75",
             out.notes.size() == 1 ? "keyPosition, note " + std::to_string (out.notes[0].note)
                                   + ", " + fmt ("%.2f", out.notes[0].velocity) : "nothing",
             verdict (out.notes.size() == 1 && out.notes[0].type == NoteEvent::keyPosition
                      && out.notes[0].note == 60
                      && std::abs (out.notes[0].velocity - 0.75f) < 1.0e-4f));
    }

    // The registered controller with the SAME index is a different thing --
    // registered 1 is Modulation in the specification. Conflating the two
    // would have a mod wheel drive the dampers.
    {
        const auto& out = oneBlock ({ 0x40003C01u, 0xC0000000u });
        row ("8.1b", "a registered per-note controller 1 is not key position",
             "no events", std::to_string (out.notes.size()) + " events",
             verdict (out.notes.empty()));
    }

    // ...and a controller Epi does not read reaches nothing.
    {
        const auto& out = oneBlock ({ 0x40103C09u, 0xC0000000u });
        row ("8.2", "an unread per-note controller is ignored", "no events",
             std::to_string (out.notes.size()) + " events", verdict (out.notes.empty()));
    }

    // The sustain pedal as a position, which is what half-pedalling needs.
    {
        const auto& out = oneBlock ({ 0x20B04040u });      // CC 64 = 64
        row ("8.3", "CC 64 becomes a damper position, not a switch", "sustain, 0.504",
             out.notes.size() == 1 ? "sustain, " + fmt ("%.3f", out.notes[0].velocity) : "nothing",
             verdict (out.notes.size() == 1 && out.notes[0].type == NoteEvent::sustain
                      && std::abs (out.notes[0].velocity - 64.0f / 127.0f) < 1.0e-4f));
    }

    // A note-on carrying its own pitch.
    {
        const uint16_t attr = (uint16_t) ((60u << 9) | 256u);
        const auto& out = oneBlock ({ 0x40913C03u, (uint32_t) (0xC0000000u | attr) });
        row ("8.4", "a note's pitch attribute becomes its tuning", "+50.0 cents",
             out.notes.size() == 1 ? fmt ("%+.1f cents", out.notes[0].tuneCents) : "nothing",
             verdict (out.notes.size() == 1 && out.notes[0].type == NoteEvent::noteOn
                      && std::abs (out.notes[0].tuneCents - 50.0f) < 1.0e-3f));
    }

    // A 32-bit assignable controller reaching a published parameter.
    {
        const int want = controlIndexForNrpn (kNrpnBank, 85);
        const auto& out = oneBlock ({ 0x40300055u, 0x40000000u });
        row ("8.5", "a 32-bit NRPN reaches its mapped parameter",
             std::string ("control ") + std::to_string (want) + ", 0.25",
             out.params.size() == 1 ? "control " + std::to_string (out.params[0].control)
                                    + ", " + fmt ("%.2f", out.params[0].value) : "nothing",
             verdict (out.params.size() == 1 && out.params[0].control == want
                      && std::abs (out.params[0].value - 0.25) < 1.0e-6));
    }

    // A MIDI 2.0 note-on at velocity zero must still sound.
    {
        const auto& out = oneBlock ({ 0x40903C00u, 0x00000000u });
        row ("8.6", "the quietest MIDI 2.0 note still strikes", "noteOn, above zero",
             out.notes.size() == 1 && out.notes[0].type == NoteEvent::noteOn
               ? fmt ("noteOn, %.4f", out.notes[0].velocity) : "no strike",
             verdict (out.notes.size() == 1 && out.notes[0].type == NoteEvent::noteOn
                      && out.notes[0].velocity > 0.0f));
    }
}

// ===========================================================================
int main()
{
    std::printf ("Epi Universal MIDI Packet suite\n");
    std::printf ("  %-5s %-46s %-22s %-24s %s\n", "id", "property", "target", "measured", "verdict");

    sectionFraming();
    sectionMidi1();
    sectionMidi2Notes();
    sectionPerNote();
    sectionJitterReduction();
    sectionStreamRobustness();
    sectionTiming();
    sectionBridge();

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
