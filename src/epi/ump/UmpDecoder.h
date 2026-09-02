/*
  Epi — physically modeled electric pianos
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#pragma once

#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// Universal MIDI Packets, decoded.
//
// MIDI 2.0 matters to a physical model for reasons that have nothing to do with
// the version number. Four things arrive that MIDI 1.0 throws away, and this
// instrument has somewhere to put all four:
//
//   Sixteen-bit velocity. 128 steps is coarse for a hammer: across the useful
//   playing range that is about a third of a decibel per step, and a keybed
//   that estimates velocity from a fitted trajectory can resolve far finer
//   than it can express. The engine's velocity input has always been a float.
//
//   A note's exact pitch, in the note-on itself. Attribute type 3 carries
//   pitch as 7.9 fixed point -- a note number and nine bits of fraction. Epi
//   already latches a per-note tuning offset at the strike and never revisits
//   it, because that is what a tuner does and a piano has no bend wheel; the
//   attribute is that number arriving properly instead of through an RPN
//   negotiation and a pitch wheel.
//
//   Per-note controllers, thirty-two bits each. This is the one that changes
//   what the instrument can do. A key on a real grand lifts its own damper as
//   it descends, well before the hammer arrives, and lets it down again
//   progressively on release -- which is why half-releasing a key half-damps
//   the note. Two thresholds cannot express that. A continuous key position
//   can, and the damper here is already a continuous contact term rather than
//   an envelope, so the number has somewhere real to go.
//
//   Jitter Reduction timestamps. Almost nothing implements them. They carry
//   the time an event actually happened, so a receiver can undo the
//   quantisation the transport imposed -- which is the only way the timing a
//   sender went to the trouble of measuring survives the trip.
//
// This decoder is framework-free and allocation-free: it takes 32-bit words
// and produces events. It does not know what an audio buffer is.
// ---------------------------------------------------------------------------
namespace epi::ump
{

// ---- the wire ------------------------------------------------------------
// A UMP is one to four 32-bit words; the top nibble of the first word is the
// message type and fixes the length.
inline constexpr int wordsForMessageType (uint8_t mt)
{
    switch (mt)
    {
        case 0x0: case 0x1: case 0x2: case 0x6: case 0x7: return 1;
        case 0x3: case 0x4: case 0x8: case 0x9: case 0xa: return 2;
        case 0xb: case 0xc:                               return 3;
        default:                                          return 4;
    }
}

enum class Kind
{
    none,
    noteOn,
    noteOff,
    polyPressure,
    controlChange,
    channelPressure,
    pitchBend,
    perNotePitchBend,
    perNoteController,      // registered or assignable, see `registered`
    perNoteManagement,
    registeredController,   // MIDI 2.0's RPN, 32-bit
    assignableController,   // MIDI 2.0's NRPN, 32-bit
    programChange,
    allNotesOff,
    allSoundOff,
    jrClock,
    jrTimestamp
};

struct Event
{
    Kind    kind    = Kind::none;
    uint8_t group   = 0;        // 0..15
    uint8_t channel = 0;        // 0..15, zero based
    uint8_t note    = 0;
    uint8_t index   = 0;        // controller number, or per-note controller index
    uint8_t bank    = 0;        // RPN/NRPN bank
    bool    registered = false; // per-note controller: registered vs assignable

    // Normalised to 0..1 for everything continuous, so a MIDI 1.0 stream and a
    // MIDI 2.0 stream reach the instrument through one path and differ only in
    // how many distinct values they can express.
    double  value   = 0.0;
    uint32_t raw    = 0;        // the value before normalising, for tests

    // Set when a MIDI 2.0 note-on carries attribute type 3: the note's pitch
    // as 7.9 fixed point, expressed here as cents away from the note number.
    bool    hasPitch  = false;
    double  pitchCents = 0.0;

    // JR clock and timestamp, in 1/31250 s ticks (32 microseconds).
    uint16_t jrTicks = 0;
};

// ---- normalising ---------------------------------------------------------
inline constexpr double norm7  (uint32_t v) { return (double) (v & 0x7f) / 127.0; }
inline constexpr double norm16 (uint32_t v) { return (double) (v & 0xffff) / 65535.0; }
inline constexpr double norm32 (uint32_t v) { return (double) v / 4294967295.0; }

// A MIDI 2.0 pitch bend is 32-bit with centre at 0x80000000.
inline constexpr double bendSigned32 (uint32_t v)
{
    return ((double) v - 2147483648.0) / 2147483648.0;
}

// ---- the decoder ---------------------------------------------------------
//
// Feed it words. It returns true when a complete message has been decoded into
// `out`; words belonging to a longer packet are absorbed and return false.
class Decoder
{
public:
    bool push (uint32_t word, Event& out)
    {
        if (pending > 0)
        {
            buffer[filled++] = word;
            if (--pending > 0) return false;
            return finish (out);
        }

        const uint8_t mt = (uint8_t) ((word >> 28) & 0xf);
        const int words = wordsForMessageType (mt);
        buffer[0] = word;
        filled = 1;

        if (words == 1) return finish (out);

        pending = words - 1;
        return false;
    }

    void reset() { pending = 0; filled = 0; }

private:
    bool finish (Event& out)
    {
        const uint32_t w0 = buffer[0];
        const uint32_t w1 = buffer[1];
        const uint8_t mt = (uint8_t) ((w0 >> 28) & 0xf);

        out = Event{};
        out.group = (uint8_t) ((w0 >> 24) & 0xf);

        const bool decoded = (mt == 0x0) ? utility (w0, out)
                           : (mt == 0x2) ? midi1ChannelVoice (w0, out)
                           : (mt == 0x4) ? midi2ChannelVoice (w0, w1, out)
                           : false;

        pending = 0;
        filled = 0;
        return decoded;
    }

    // Message type 0: utility, which is where jitter reduction lives.
    static bool utility (uint32_t w0, Event& out)
    {
        const uint8_t status = (uint8_t) ((w0 >> 20) & 0xf);
        const uint16_t ticks = (uint16_t) (w0 & 0xffff);
        if (status == 0x1) { out.kind = Kind::jrClock;     out.jrTicks = ticks; return true; }
        if (status == 0x2) { out.kind = Kind::jrTimestamp; out.jrTicks = ticks; return true; }
        return false;                                   // NOOP and the rest
    }

    // Message type 2: a MIDI 1.0 message carried in a UMP. Seven-bit values,
    // widened here so that everything downstream sees one representation.
    static bool midi1ChannelVoice (uint32_t w0, Event& out)
    {
        const uint8_t status = (uint8_t) ((w0 >> 20) & 0xf);
        const uint8_t d1 = (uint8_t) ((w0 >> 8) & 0x7f);
        const uint8_t d2 = (uint8_t) (w0 & 0x7f);
        out.channel = (uint8_t) ((w0 >> 16) & 0xf);
        out.note = d1;
        out.index = d1;
        out.raw = d2;

        switch (status)
        {
            case 0x8: out.kind = Kind::noteOff;  out.value = norm7 (d2); return true;
            case 0x9:
                // Velocity zero is a note off, as it has always been.
                out.kind = (d2 == 0) ? Kind::noteOff : Kind::noteOn;
                out.value = norm7 (d2);
                return true;
            case 0xa: out.kind = Kind::polyPressure;    out.value = norm7 (d2); return true;
            case 0xb: return midi1Controller (d1, d2, out);
            case 0xc: out.kind = Kind::programChange;   out.raw = d1; return true;
            case 0xd: out.kind = Kind::channelPressure; out.value = norm7 (d1); out.raw = d1; return true;
            case 0xe:
                out.kind = Kind::pitchBend;
                out.raw = (uint32_t) (((uint32_t) d2 << 7) | d1);
                out.value = ((double) out.raw - 8192.0) / 8192.0;
                return true;
            default: return false;
        }
    }

    static bool midi1Controller (uint8_t cc, uint8_t v, Event& out)
    {
        if (cc == 123) { out.kind = Kind::allNotesOff; return true; }
        if (cc == 120) { out.kind = Kind::allSoundOff; return true; }
        out.kind = Kind::controlChange;
        out.index = cc;
        out.value = norm7 (v);
        out.raw = v;
        return true;
    }

    // Message type 4: MIDI 2.0 channel voice. Two words; the value lives in
    // the second and is the full width of the field.
    static bool midi2ChannelVoice (uint32_t w0, uint32_t w1, Event& out)
    {
        const uint8_t status = (uint8_t) ((w0 >> 20) & 0xf);
        out.channel = (uint8_t) ((w0 >> 16) & 0xf);
        const uint8_t b2 = (uint8_t) ((w0 >> 8) & 0xff);
        const uint8_t b3 = (uint8_t) (w0 & 0xff);
        out.note = b2 & 0x7f;
        out.index = b3;
        out.raw = w1;

        switch (status)
        {
            case 0x8:
                out.kind = Kind::noteOff;
                out.value = norm16 (w1 >> 16);
                out.raw = w1 >> 16;
                return true;

            case 0x9:
            {
                const uint16_t vel = (uint16_t) (w1 >> 16);
                // A MIDI 2.0 note-on with velocity zero is a note ON at the
                // quietest playable level, NOT a note off. The running-status
                // trick is gone, and treating it as a release would silently
                // drop the softest notes on a keybed that can actually
                // resolve them.
                out.kind = Kind::noteOn;
                out.value = norm16 (vel);
                out.raw = vel;

                const uint8_t attrType = b3;
                const uint16_t attr = (uint16_t) (w1 & 0xffff);
                if (attrType == 0x03)
                {
                    // Pitch 7.9: the top seven bits are a note number, the low
                    // nine a fraction of a semitone.
                    const double pitch = (double) (attr >> 9) + (double) (attr & 0x1ff) / 512.0;
                    out.hasPitch = true;
                    out.pitchCents = (pitch - (double) out.note) * 100.0;
                }
                return true;
            }

            case 0xa: out.kind = Kind::polyPressure;    out.value = norm32 (w1); return true;
            case 0xd: out.kind = Kind::channelPressure; out.value = norm32 (w1); return true;

            case 0xb:
                if (b2 == 123) { out.kind = Kind::allNotesOff; return true; }
                if (b2 == 120) { out.kind = Kind::allSoundOff; return true; }
                out.kind = Kind::controlChange;
                out.index = b2;
                out.value = norm32 (w1);
                return true;

            case 0xe: out.kind = Kind::pitchBend; out.value = bendSigned32 (w1); return true;
            case 0x6: out.kind = Kind::perNotePitchBend; out.value = bendSigned32 (w1); return true;

            case 0x0:
            case 0x1:
                out.kind = Kind::perNoteController;
                out.registered = (status == 0x0);
                out.index = b3;
                out.value = norm32 (w1);
                return true;

            case 0x2:
            case 0x3:
                out.kind = (status == 0x2) ? Kind::registeredController
                                           : Kind::assignableController;
                out.bank = b2 & 0x7f;
                out.index = b3 & 0x7f;
                out.value = norm32 (w1);
                return true;

            case 0xf:
                out.kind = Kind::perNoteManagement;
                out.index = b3;
                return true;

            default: return false;
        }
    }

    uint32_t buffer[4] {};
    int pending = 0;
    int filled = 0;
};

// ---- jitter reduction ----------------------------------------------------
//
// JR Clock is the sender's running time, sixteen bits at 1/31250 s -- 32
// microsecond resolution, wrapping every 2.097 seconds. A JR Timestamp
// immediately precedes the messages it applies to and says when they actually
// happened. Undoing the transport's quantisation is the only reason the
// sender's timing survives at all, and it is the whole point of a keybed that
// measures a key's trajectory to a few tens of microseconds and then hands the
// result to a bus that delivers in millisecond lumps.
//
// This tracks the wrap and returns seconds since the first clock seen.
class JitterReduction
{
public:
    static constexpr double kTickSeconds = 1.0 / 31250.0;
    static constexpr uint32_t kWrap = 1u << 16;

    void reset() { started = false; high = 0; last = 0; }

    // A JR Clock: the sender's own time base. Returns its unwrapped seconds.
    double clock (uint16_t ticks) { return unwrap (ticks); }

    // A JR Timestamp: when the following messages happened, in the same
    // unwrapped seconds.
    double timestamp (uint16_t ticks) { return unwrap (ticks); }

    bool isRunning() const { return started; }

private:
    double unwrap (uint16_t ticks)
    {
        if (! started) { started = true; last = ticks; high = 0; }
        // A step backwards of more than half the range is a wrap, not a
        // reordering: JR messages are emitted in order by definition.
        else if (ticks < last && (uint32_t) (last - ticks) > kWrap / 2) ++high;
        last = ticks;
        return ((double) high * (double) kWrap + (double) ticks) * kTickSeconds;
    }

    bool started = false;
    uint32_t high = 0;
    uint16_t last = 0;
};

} // namespace epi::ump
