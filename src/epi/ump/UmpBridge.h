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

#include "epi/ControlMap.h"
#include "epi/dsp/EpiEngine.h"
#include "epi/ump/UmpDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// From Universal MIDI Packets to the instrument.
//
// This turns decoded UMP into the two things Epi actually consumes: note
// events with a sample offset inside the block, and parameter changes indexed
// by the control map. It is framework-free and knows nothing about audio
// devices; the host hands it words and a wall-clock time and takes events
// back.
//
// The part worth reading is the timing. A sender that measures a key's
// trajectory to tens of microseconds hands the result to a bus that delivers
// in millisecond lumps, and the arrival time of a message says more about USB
// frame scheduling than about when the key was pressed. JR Timestamps carry
// the real time; using them means mapping the sender's clock onto ours, which
// is what JrAligner does, and then placing the event at the right SAMPLE
// rather than at the top of whatever block it happened to land in.
// ---------------------------------------------------------------------------
namespace epi::ump
{

// ---- what Epi reads on a per-note controller -----------------------------
//
// MIDI 2.0's registered per-note controllers have assigned meanings and none
// of them is "where the key is". So key position arrives as an ASSIGNABLE
// per-note controller, and the index is published here for the same reason the
// CC numbers are published in ControlMap.h: somebody's keybed firmware is
// going to be keyed to it.
inline constexpr int kKeyPositionController = 1;   // 0 at rest, 1 fully depressed

// ---------------------------------------------------------------------------
// Mapping the sender's clock onto ours.
//
// The two clocks run at slightly different rates and start at unrelated
// points, so what is wanted is the offset between them. Every message arrives
// LATE by some amount -- transport scheduling, driver buffering, thread
// wake-up -- and never early, so the smallest difference seen between arrival
// and timestamp is the closest thing to the true offset, and everything above
// it is delay. Tracking the minimum is therefore the estimator, not an
// approximation of one.
//
// The minimum is allowed to creep upward slowly, because the two clocks drift
// and a single unusually direct message must not pin the estimate for the rest
// of the session. The rate below is far slower than any plausible crystal
// error and far faster than a session.
// ---------------------------------------------------------------------------
class JrAligner
{
public:
    // Roughly one part in ten thousand: a hundred microseconds a second, which
    // no crystal pair beats and no burst of traffic can outrun.
    static constexpr double kLeakPerSecond = 1.0e-4;

    void reset() { started = false; offset = 0.0; lastHost = 0.0; }

    // arrivalHost: when we saw it. jrSeconds: when the sender says it happened.
    // Returns the sender's time expressed on our clock.
    double toHostTime (double jrSeconds, double arrivalHost)
    {
        const double d = arrivalHost - jrSeconds;

        if (! started)
        {
            started = true;
            offset = d;
        }
        else
        {
            const double dt = std::max (0.0, arrivalHost - lastHost);
            offset += kLeakPerSecond * dt;      // let the estimate drift up
            if (d < offset) offset = d;         // ...but a more direct message wins
        }

        lastHost = arrivalHost;
        return jrSeconds + offset;
    }

    bool isLocked() const { return started; }
    double currentOffset() const { return offset; }

private:
    bool started = false;
    double offset = 0.0;
    double lastHost = 0.0;
};

// ---------------------------------------------------------------------------
// The bridge.
// ---------------------------------------------------------------------------
class Bridge
{
public:
    struct ParamChange
    {
        int    control = -1;      // index into epi::kControlMap
        double value = 0.0;       // normalised
    };

    // Everything a block produced. Cleared by beginBlock.
    struct Block
    {
        std::vector<NoteEvent>   notes;
        std::vector<ParamChange> params;
    };

    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = maxBlockSize;
        decoder.reset();
        jr.reset();
        aligner.reset();
        haveTimestamp = false;
    }

    // Called on the audio thread at the top of each block. `blockStartHost` is
    // the time the first sample of this block will be heard, on the same clock
    // the MIDI arrival times use.
    void beginBlock (double blockStartHost, int numSamples)
    {
        block.notes.clear();
        block.params.clear();
        blockStart = blockStartHost;
        blockSamples = numSamples;
    }

    // Feed one UMP word. `arrivalHost` is when the packet reached us.
    void push (uint32_t word, double arrivalHost)
    {
        Event e;
        if (! decoder.push (word, e)) return;
        handle (e, arrivalHost);
    }

    const Block& current() const { return block; }

    // For the host to report, and for tests: how far the sender's clock sits
    // from ours, and whether jitter reduction is actually running.
    bool jrRunning() const { return jr.isRunning(); }
    double jrOffsetSeconds() const { return aligner.currentOffset(); }

private:
    void handle (const Event& e, double arrivalHost)
    {
        // ---- jitter reduction ------------------------------------------
        if (e.kind == Kind::jrClock)
        {
            // A clock is a time reference, not an event; feeding it to the
            // aligner is how the offset stays current when nothing is being
            // played.
            aligner.toHostTime (jr.clock (e.jrTicks), arrivalHost);
            return;
        }
        if (e.kind == Kind::jrTimestamp)
        {
            // Applies to the messages that FOLLOW it, until the next one.
            stampedHost = aligner.toHostTime (jr.timestamp (e.jrTicks), arrivalHost);
            haveTimestamp = true;
            return;
        }

        // A timestamped message is placed where the sender says it happened;
        // an untimestamped one where it arrived. That is the whole difference
        // between preserving a player's timing and quantising it to whenever
        // the transport got round to us.
        const double when = haveTimestamp ? stampedHost : arrivalHost;
        const int offset = sampleOffset (when);

        switch (e.kind)
        {
            case Kind::noteOn:
                emit ({ offset, NoteEvent::noteOn, (int) e.note,
                        std::max (0.0009f, (float) e.value),
                        e.hasPitch ? (float) e.pitchCents : 0.0f });
                break;

            case Kind::noteOff:
                emit ({ offset, NoteEvent::noteOff, (int) e.note, 0.0f });
                break;

            case Kind::allNotesOff:
            case Kind::allSoundOff:
                emit ({ offset, NoteEvent::allNotesOff, 0, 0.0f });
                break;

            case Kind::perNoteController:
                // The one Epi reads: where the key is.
                if (! e.registered && e.index == kKeyPositionController)
                    emit ({ offset, NoteEvent::keyPosition, (int) e.note, (float) e.value });
                break;

            case Kind::controlChange:
                controller (e.index, e.value, offset);
                break;

            case Kind::assignableController:
                // MIDI 2.0's NRPN: bank zero, index as published, and the full
                // thirty-two bits instead of fourteen.
                if (e.bank == kNrpnBank)
                    if (const int c = controlIndexForNrpn (e.bank, e.index); c >= 0)
                        block.params.push_back ({ c, e.value });
                break;

            default: break;
        }
    }

    void controller (uint8_t cc, double v, int offset)
    {
        switch (cc)
        {
            // The pedals, as positions. The damper is a contact term, so the
            // sustain pedal's depth is a physical statement.
            case 64: emit ({ offset, NoteEvent::sustain,   0, (float) v }); return;
            case 66: emit ({ offset, NoteEvent::sostenuto, 0, v >= 0.5 ? 1.0f : 0.0f }); return;
            case 67: emit ({ offset, NoteEvent::soft,      0, v >= 0.5 ? 1.0f : 0.0f }); return;
            default: break;
        }

        if (const int c = controlIndexForCc (cc); c >= 0)
            block.params.push_back ({ c, v });
    }

    void emit (NoteEvent e) { block.notes.push_back (e); }

    // Where in this block the event belongs. An event whose time has already
    // passed goes at the start rather than being dropped -- it is late, and a
    // late note is better than a missing one -- and one from the future is
    // held at the end of the block rather than reaching into the next.
    int sampleOffset (double when) const
    {
        const double d = (when - blockStart) * fs;
        if (! (d > 0.0)) return 0;
        return std::min (blockSamples > 0 ? blockSamples - 1 : 0,
                         (int) std::lround (d));
    }

    Decoder decoder;
    JitterReduction jr;
    JrAligner aligner;

    Block block;
    double fs = 48000.0;
    int maxBlock = 512;
    double blockStart = 0.0;
    int blockSamples = 0;

    bool haveTimestamp = false;
    double stampedHost = 0.0;
};

} // namespace epi::ump
