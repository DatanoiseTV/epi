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

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace epi
{

// ---------------------------------------------------------------------------
// Continuous controllers and registered parameters, both directions.
//
// Reading is a small state machine because NRPN is four messages that have to
// be remembered between calls, and because those four messages share their
// data-entry pair with RPN -- which this instrument already reads, for MPE
// tuning. The rule the specification gives is that data entry belongs to
// whichever selector arrived LAST, so that is exactly what is tracked, per
// channel, and the surface reports a data byte as not-its-own whenever an RPN
// was selected more recently. Getting that wrong does not fail quietly: an
// ordinary NRPN sweep would walk into the pitch bend range.
//
// Writing exists so the panel can be a physical one. An encoder with a display
// has to be told when a preset load moved the parameter under it, or it shows
// the wrong number until the player touches it. So every parameter is polled
// and its change sent back out. Two things make that safe rather than a
// feedback loop:
//
//   * A value that ARRIVED over MIDI is recorded as already-sent, so it is
//     never echoed to the device that just sent it. The instrument will not
//     hold it exactly -- a choice parameter snaps to an index, a continuous
//     one to its own step -- and reporting every one of those corrections
//     would put a message on the wire for every message taken off it, halving
//     what an encoder sweep can carry. Measured on the real parameters, the
//     corrections are 9 to 64 parts in 16384, all inside a single CC step, so
//     one is absorbed silently after each incoming edit. A correction LARGER
//     than a CC step means the instrument went somewhere the sender did not
//     ask for, and that is still reported.
//   * The poll has a per-tick budget and resumes where it stopped, so a preset
//     load -- which moves all forty-nine at once -- is spread over a few ticks
//     instead of emitting nearly two hundred messages into a DIN port that can
//     carry about a thousand a second.
// ---------------------------------------------------------------------------
class MidiControlSurface
{
public:
    static constexpr int kNumChannels = 16;
    static constexpr int kMax14 = 16383;
    // One seven-bit step. The far end cannot represent, display or resend a
    // difference finer than this, so a correction inside it carries nothing.
    static constexpr int kAbsorb = 128;

    // Normalised 0..1 to the wire, and back. Both directions round to nearest
    // so that a value which leaves as one integer comes back as itself: a
    // choice parameter has to survive the round trip exactly or a controller
    // echo would walk it one step at a time.
    static int    to14   (float norm)  { return std::clamp ((int) std::lround (norm * (float) kMax14), 0, kMax14); }
    static float  from14 (int v)       { return std::clamp ((float) v / (float) kMax14, 0.0f, 1.0f); }
    static int    to7    (float norm)  { return std::clamp ((int) std::lround (norm * 127.0f), 0, 127); }
    static float  from7  (int v)       { return std::clamp ((float) v / 127.0f, 0.0f, 1.0f); }

    // ---- reading ---------------------------------------------------------
    // Returns true when the message was a parameter edit and must not be
    // offered to anything else. `apply` is called as (controlIndex, 0..1).
    //
    // NOTE on the two-message case: a controller that sends only CC 6 and no
    // CC 38 is common, and it must still work. So the value is applied on the
    // data MSB with a zero low byte, and applied AGAIN when the low byte
    // arrives. The intermediate is at most one part in 128 away from the
    // final, which no parameter here can turn into an audible step.
    bool controller (int channel, int cc, int value,
                     const std::function<void (int, float)>& apply)
    {
        const int c = std::clamp (channel, 1, kNumChannels) - 1;

        switch (cc)
        {
            case 99: nrpnMsb[c] = value; nrpnActive[c] = true;  return true;
            case 98: nrpnLsb[c] = value; nrpnActive[c] = true;  return true;

            // Not ours, and deliberately NOT consumed: the MPE tuner reads
            // these. Observing them is only how this side learns that the
            // data-entry pair has changed owner.
            case 100:
            case 101: nrpnActive[c] = false; return false;

            case 6:
                if (! nrpnActive[c]) return false;
                dataMsb[c] = value;
                return emit (c, (dataMsb[c] << 7), apply);

            case 38:
                if (! nrpnActive[c]) return false;
                return emit (c, (dataMsb[c] << 7) | value, apply);

            // Data increment / decrement. The specification leaves the step
            // for a non-registered parameter to the instrument; one CC step
            // is the reading that makes a front-panel button useful, so that
            // is what a press moves.
            case 96:
            case 97:
            {
                if (! nrpnActive[c]) return false;
                const int idx = index (c);
                if (idx < 0) return true;
                const int step = (cc == 96 ? 128 : -128);
                lastSeen[idx] = std::clamp (lastSeen[idx] + step, 0, kMax14);
                return emit (c, lastSeen[idx], apply);
            }

            default: break;
        }

        const int idx = controlIndexForCc (cc);
        if (idx < 0) return false;
        markSent (idx, to14 (from7 (value)));
        apply (idx, from7 (value));
        return true;
    }

    // ---- writing ---------------------------------------------------------
    // Poll the parameters and hand back the messages that have to go out.
    // `readNormalised(i)` returns the current value of control i; `send` is
    // called with (statusChannel-independent) controller number and value.
    //
    // Returns the number of parameters reported this call.
    template <typename ReadFn, typename SendCcFn>
    int collectFeedback (ReadFn&& readNormalised, SendCcFn&& sendCc,
                         bool sendNrpn, bool sendCcToo, int budget)
    {
        int reported = 0;
        for (int n = 0; n < kNumControls && reported < budget; ++n)
        {
            const int i = cursor;
            cursor = (cursor + 1) % kNumControls;

            const int v = to14 (readNormalised (i));
            if (primed[i] && v == lastSeen[i]) continue;

            // The settling of a value this surface just took in, rather than
            // news. Record it and say nothing.
            if (absorbOnce[i] && std::abs (v - lastSeen[i]) <= kAbsorb)
            {
                absorbOnce[i] = false;
                lastSeen[i] = v;
                continue;
            }

            absorbOnce[i] = false;
            primed[i] = true;
            lastSeen[i] = v;
            ++reported;

            if (sendNrpn)
            {
                sendCc (99, kNrpnBank);
                sendCc (98, kControlMap[i].nrpn);
                sendCc (6,  (v >> 7) & 0x7f);
                sendCc (38, v & 0x7f);
            }
            if (sendCcToo && kControlMap[i].cc >= 0)
                sendCc (kControlMap[i].cc, to7 (from14 (v)));
        }
        return reported;
    }

    // Forget what the far end is believed to know, so the next poll restates
    // every parameter. Sent once when a device connects, because a controller
    // that was plugged in after the fact has no idea what anything is set to.
    void resendAll() { primed.fill (false); absorbOnce.fill (false); cursor = 0; }

private:
    int index (int c) const { return controlIndexForNrpn (nrpnMsb[c], nrpnLsb[c]); }

    bool emit (int c, int v14, const std::function<void (int, float)>& apply)
    {
        const int idx = index (c);
        if (idx < 0) return true;           // a selector pointing at nothing
        markSent (idx, v14);
        apply (idx, from14 (v14));
        return true;
    }

    // A value that came IN is a value the far end already has. Recording it
    // as sent is what stops the poll bouncing it straight back at the encoder
    // the player is still turning.
    void markSent (int idx, int v14)
    { lastSeen[idx] = v14; primed[idx] = true; absorbOnce[idx] = true; }

    std::array<int, kNumChannels>  nrpnMsb {}, nrpnLsb {}, dataMsb {};
    std::array<bool, kNumChannels> nrpnActive {};

    std::array<int, kNumControls>  lastSeen {};
    std::array<bool, kNumControls> primed {};
    std::array<bool, kNumControls> absorbOnce {};
    int cursor = 0;
};

} // namespace epi
