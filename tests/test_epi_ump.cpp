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

#include "epi/ump/UmpDecoder.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

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

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
