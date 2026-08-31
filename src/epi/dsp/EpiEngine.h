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

#include "ActionNoise.h"
#include "CP70Voice.h"
#include "WurliVoice.h"
#include "GrandVoice.h"
#include "GrandBoard.h"
#include "GrandRadiator.h"
#include "GrandMicStage.h"
#include "ClavinetVoice.h"
#include "ClavinetChain.h"
#include "WurliChain.h"

#include <vector>
#include "Effects.h"
#include "Harp.h"
#include "OutputChain.h"
#include "PickupMagnetic.h"
#include "Room.h"
#include "RhodesVoice.h"

#include <array>
#include <atomic>

namespace epi
{

// There is deliberately no Instrument enum. There was one, unused, and it
// had drifted: it listed four instruments in an order that stopped matching
// the parameter's own names, and never gained the grand. A second statement
// of an ordering that nothing reads is a trap for whoever trusts it -- the
// authority is epi::ids::instrumentNames, and the engine dispatches on the
// integer that parameter carries.

// Clavinet pickup switching. Order must match epi::ids::pickupSelNames.
enum class PickupSelect { neck = 0, bridge, bothIn, bothOut };

// Control-rate parameters in real units, resolved once per block by whatever
// front end drives the engine.
struct EngineParams
{
    int    instrument = 0;
    float  tuneCents  = 0.0f;

    // Action
    float velCurve    = 0.5f;
    float hammerHard  = 0.5f;
    float hammerMass  = 0.5f;
    float escapement  = 0.4f;
    float strikeNoise = 0.3f;
    float damperGrip  = 0.6f;

    // Resonator
    float tipMass   = 0.5f;
    float resDamp   = 0.35f;
    float barCouple = 0.6f;
    float barTune   = 0.0f;
    float bodyMix   = 0.25f;
    float nonlinAmt = 0.5f;

    // Transducer
    // Not centred. On the pole centreline the field is symmetric, so the tine
    // crosses its peak twice a cycle and the output comes out an octave up with
    // almost no fundamental -- a real effect, and a real voicing, but not the
    // one anybody means by "a Rhodes". Real instruments are voiced off-centre.
    float pickupPos  = -0.35f;  // -1..1, the voicing screw
    float pickupDist = 0.35f;
    int   pickupSel  = 1;
    float coilFreq   = 0.5f;
    float coilQ      = 0.5f;
    float coilSat    = 0.25f;

    // Amplifier
    float preampDrive = 0.3f;
    float bassDb      = 0.0f;
    float trebleDb    = 0.0f;
    float clarityDb   = 0.0f;
    // Transducer swap: 0 Magnetic, 1 Native (the instrument's own),
    // 2 Electro, 3 Contact. The founding measurement of this project is that
    // the transducer makes the timbre; this is that fact handed to the
    // player.
    int   transducer  = 1;
    // What the resonator is made of: index into kMaterials, 0 = stock.
    int   material    = 0;
    // The Clavinet's own switches: the 4-way pickup matrix and the rockers.
    int   clavSwitch  = 0;
    int   bodyMat     = 0;
    float bodySize    = 0.5f;
    int   damperFelt  = 0;
    int   keyBed      = 0;
    int   hammerMat   = 0;
    int   roomProfile = 0;
    int   softMode    = 0;   // CC67 mechanism: 0 shift (una corda), 1 rail (half-blow)
    float wearAmount  = 0.0f; // tangent-rubber notching (Clav), 0 = new
    bool  clavBrill   = false;
    bool  clavTreb    = false;
    bool  clavMed     = true;
    bool  clavSoft    = false;
    float tremRate    = 5.5f;
    float tremDepth   = 0.0f;
    float tremStereo  = 1.0f;   // 1 = the Rhodes panner, 0 = amplitude tremolo
    float cabMix      = 0.5f;

    // Phaser. Not part of the instrument; see Effects.h.
    float phaserMix   = 0.0f;
    float phaserRate  = 0.4f;
    float phaserDepth = 0.7f;
    float phaserFb    = 0.5f;

    // Output
    float spaceMix   = 0.15f;
    float spaceSize  = 0.40f;
    float outGainLin = 1.0f;
};

// Sample-accurate note events, already flattened from MIDI by the caller.
struct NoteEvent
{
    // sustain carries a continuous CC64 value in `velocity`: the damper is
    // a contact term, so half-pedal is a physical statement, not a switch.
    // sostenuto latches the keys held at press (grand only -- the electrics
    // never had the middle pedal); soft is the una corda shift, also the
    // grand's. Both carry on/off in `velocity` (>0.5 = down).
    enum Type { noteOn, noteOff, allNotesOff, sustainOn, sustainOff, sustain,
                sostenuto, soft };
    int   offset   = 0;
    Type  type     = noteOn;
    int   note     = 60;
    float velocity = 0.8f;
    // Per-note tuning, in cents, read on a note-on and on nothing else. This
    // is the tuner's offset for THIS string, not a bend: it is latched when
    // the hammer lands and never revisited while the note rings. Zero is the
    // instrument as the TUNE knob leaves it, and a stream that never sets it
    // renders exactly as it did before the field existed.
    float tuneCents = 0.0f;
};

// ---------------------------------------------------------------------------
// A host's tuning system, taken as a tuner's instruction.
//
// These instruments have no pitch bend. There is no wheel on a piano, an
// electric piano or a clavinet, and nobody turns a tuning pin while a string
// rings. Per-note TUNING is a different action and an entirely physical one --
// a tuner sets every string on its own, which is exactly what the workshop's
// length lane already does -- so a host that retunes per note is answered at
// the moment the hammer lands, and not after.
//
// The transport is MPE: each sounding note owns a channel, and that channel's
// pitch bend carries the note's own offset (MMA RP-053). This class is the
// register of what each channel is currently asking for. It emits nothing: the
// value is read once, when a note-on on that channel is flattened into a
// NoteEvent, and latched there. A later change on the same channel is the
// tuner reaching for the next string, and it reaches the next note struck on
// that channel.
//
// Framework-free so the semantics are measurable by the offline suites; the
// plugin only feeds it the bytes it has already parsed.
// ---------------------------------------------------------------------------
class MpeTuning
{
public:
    // RP-053, "Pitch Bend": a Member Channel's default sensitivity is +/-48
    // semitones and a Master Channel's the usual +/-2. A host that sends
    // RPN 0 on a channel overrides that channel's value.
    static constexpr float kMemberRangeSemis = 48.0f;
    static constexpr float kMasterRangeSemis = 2.0f;
    static constexpr int   kNumChannels      = 16;

    // Off: the register is inert and every note is tuned by the TUNE knob
    // alone -- the instrument exactly as it was before this existed.
    // Detect: a zone opens only when the host sends an MPE Configuration
    // Message, so a host that speaks plain MIDI never enters this path.
    // On: assume the standard full-keyboard lower zone (master channel 1,
    // members 2-16) whether or not the configuration message arrived.
    enum class Mode { off = 0, detect, on };

    void setMode (Mode m)
    {
        mode = m;
        if (m == Mode::off) closeZones();
        // Forcing it on with no configuration message means the lower zone
        // in its standard full-keyboard form.
        if (m == Mode::on && lowerMembers == 0 && upperMembers == 0)
            openLowerZone (15);
    }
    Mode getMode() const { return mode; }

    void reset()
    {
        closeZones();
        if (mode == Mode::on) openLowerZone (15);
    }

    bool isActive() const { return mode != Mode::off && (lowerMembers > 0 || upperMembers > 0); }

    // Channels are 1-based, as the MIDI messages number them.
    bool isMember (int channel) const
    {
        if (! isActive()) return false;
        if (lowerMembers > 0 && channel >= 2 && channel <= 1 + lowerMembers) return true;
        if (upperMembers > 0 && channel <= 15 && channel >= 16 - upperMembers) return true;
        return false;
    }

    // A pitch wheel on a member channel is that channel's tuning and nothing
    // else; returns true when it was taken that way, so the caller leaves the
    // global bend alone. Everything else -- master channel, inactive zone,
    // plain MIDI -- returns false and keeps the old behaviour verbatim.
    bool pitchWheel (int channel, int value14)
    {
        if (! isMember (channel)) return false;
        bend14[idx (channel)] = value14;
        return true;
    }

    // One control change, already unpacked. Returns true when it was consumed
    // as registered-parameter traffic. Only the four RPN controllers are
    // touched, so no controller the instrument already reads can be swallowed.
    bool controller (int channel, int cc, int value)
    {
        const int c = idx (channel);
        switch (cc)
        {
            // RPN and NRPN share one data-entry pair, and a data byte
            // belongs to whichever of them was selected LAST. Tracking only
            // the RPN half means a host's ordinary NRPN traffic -- which has
            // nothing to do with tuning -- walks straight into the pitch
            // bend range and the zone layout. Measured before this guard: a
            // plain NRPN exchange cut the member range from 48 semitones to
            // 5 and the zone from 15 members to 3.
            case 99:  nrpnActive[c] = true;  return false;   // NRPN MSB
            case 98:  nrpnActive[c] = true;  return false;   // NRPN LSB
            case 101: nrpnActive[c] = false; rpnMsb[c] = value; return true;
            case 100: nrpnActive[c] = false; rpnLsb[c] = value; return true;
            // 127/127 is RPN NULL: the selector deliberately points at
            // nothing so that stray data entry reaches nothing, which is
            // the entire purpose of sending it.
            case 6:   if (nrpnActive[c] || isNull (c)) return false;
                      dataMsb[c] = value; applyData (channel); return true;
            case 38:  if (nrpnActive[c] || isNull (c)) return false;
                      dataLsb[c] = value; applyData (channel); return true;
            default:  return false;
        }
    }

    // What a note struck on this channel is cut to, in cents.
    float noteCents (int channel) const
    {
        if (! isMember (channel)) return 0.0f;
        const int c = idx (channel);
        return static_cast<float> (bend14[c] - 8192) / 8192.0f * rangeSemis[c] * 100.0f;
    }

    // Test and interface access: what the register currently holds.
    int  memberCount (bool lower) const { return lower ? lowerMembers : upperMembers; }
    float channelRangeSemis (int channel) const { return rangeSemis[idx (channel)]; }

private:
    static int idx (int channel) { return std::clamp (channel, 1, kNumChannels) - 1; }

    // RPN NULL (127, 127): the selector points at nothing on purpose.
    bool isNull (int c) const { return rpnMsb[c] == 127 && rpnLsb[c] == 127; }

    void closeZones()
    {
        lowerMembers = upperMembers = 0;
        for (int c = 0; c < kNumChannels; ++c)
        {
            bend14[c] = 8192;
            rangeSemis[c] = kMasterRangeSemis;
        }
    }

    // RP-053: a configuration message resets the zone's member channels to
    // the default sensitivity and to centre. Not doing that leaves a stale
    // offset on a channel the host believes it has just cleared.
    void openLowerZone (int members)
    {
        lowerMembers = std::clamp (members, 0, 15);
        rangeSemis[0] = kMasterRangeSemis;
        for (int c = 1; c <= lowerMembers; ++c)
        {
            bend14[c] = 8192;
            rangeSemis[c] = kMemberRangeSemis;
        }
    }

    void openUpperZone (int members)
    {
        upperMembers = std::clamp (members, 0, 15);
        rangeSemis[kNumChannels - 1] = kMasterRangeSemis;
        for (int c = kNumChannels - 1 - upperMembers; c < kNumChannels - 1; ++c)
        {
            bend14[c] = 8192;
            rangeSemis[c] = kMemberRangeSemis;
        }
    }

    void applyData (int channel)
    {
        const int c = idx (channel);
        if (rpnMsb[c] != 0) return;                       // only RPN 0x00xx here
        if (rpnLsb[c] == 0)
        {
            // RPN 0: pitch bend sensitivity, semitones in the MSB and cents
            // in the LSB. A host that sets it on a member channel is telling
            // us how to read that channel's wheel.
            rangeSemis[c] = static_cast<float> (dataMsb[c])
                          + static_cast<float> (dataLsb[c]) / 100.0f;
        }
        else if (rpnLsb[c] == 6 && mode != Mode::off)
        {
            // RPN 6: the MPE Configuration Message. It is sent on the zone's
            // master channel and its value is the member count; zero closes
            // the zone. Off stays inert. On listens as well -- forcing it on
            // means "do not wait to be told", not "ignore what the host says",
            // so a host that configures eight member channels gets eight --
            // but a zero count would close the zone, and an instrument the
            // player has forced on is not allowed to end up with none.
            const int members = dataMsb[c];
            if (mode == Mode::on && members == 0) return;
            if (channel == 1)       openLowerZone (members);
            else if (channel == 16) openUpperZone (members);
        }
    }

    Mode mode = Mode::detect;
    int  lowerMembers = 0, upperMembers = 0;
    int  bend14[kNumChannels] {};
    float rangeSemis[kNumChannels] {};
    int  rpnMsb[kNumChannels] {}, rpnLsb[kNumChannels] {};
    // Whether an NRPN was selected more recently than an RPN on this
    // channel; data entry belongs to whichever came last.
    bool nrpnActive[kNumChannels] {};
    int  dataMsb[kNumChannels] {}, dataLsb[kNumChannels] {};

public:
    MpeTuning() { closeZones(); }
};

// ---------------------------------------------------------------------------
// The instrument.
//
//   every tine, always present  ->  harp  ->  every other tine
//        |
//        +-> summed flux -> one coil -> preamp -> vibrato -> cabinet
//
// There is no voice allocation. A Rhodes has one tine per note, permanently,
// and so does this: eighty-eight of them, each cut for its own note and struck
// when its key goes down. That removes an entire class of problem rather than
// managing it -- nothing is ever stolen, so nothing is ever cut off mid-ring,
// and a repeated note strikes a tine that is still moving, which is what makes
// repeated notes on the real instrument reinforce or fight the one before
// depending on where in the cycle the hammer lands.
//
// It also buys the thing that cannot be faked with a voice pool: every tine is
// bolted to the same harp, so a struck one shakes the frame and the frame
// shakes the rest. With the pedal down the whole keyboard answers. That is
// most of what a pedalled Rhodes chord is, and with allocated voices there is
// simply nothing there to answer.
//
// The cost is real and is paid where it should be: a tine with no energy and no
// hammer on it is skipped, so an unpedalled passage costs what the notes cost,
// and a pedalled one costs the whole instrument -- which is exactly the
// bargain the real thing makes.
//
// The pickups all sit on one bus feeding one preamp, so the nonlinearities
// downstream of that bus are SHARED and a chord intermodulates there rather
// than arriving as the sum of its notes.
// ---------------------------------------------------------------------------
class EpiEngine
{
public:
    // The full piano compass. A 73-key Rhodes runs E1 to E6; the extra octaves
    // cost nothing when they are not ringing and mean a part written outside
    // the real range still plays.
    static constexpr int kLoNote   = 21;    // A0
    static constexpr int kHiNote   = 108;   // C8
    static constexpr int kNumTines = kHiNote - kLoNote + 1;
    static constexpr int kMaxVoices = kNumTines;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void process (float* outL, float* outR, int numSamples,
                  const EngineParams& params,
                  const NoteEvent* events, int numEvents);

    void setPitchBend (float semitones) { bendSemis = semitones; }
    void setExpression (float scale)    { expression = scale; }

    // The workshop: change one tine's steel. Message thread; the audio
    // thread rebuilds that tine at its own pace through the priority loop.
    void setTineMod (int i, float lenScale, float diaScale)
    {
        if (i < 0 || i >= kNumTines) return;
        tineMod[static_cast<std::size_t> (i)].len.store (lenScale, std::memory_order_relaxed);
        tineMod[static_cast<std::size_t> (i)].dia.store (diaScale, std::memory_order_relaxed);
        tineMod[static_cast<std::size_t> (i)].dirty.store (true, std::memory_order_release);
    }

    // The cabinet workshop: five physical dimensions, applied to both
    // channels at the next block. Atomics for the same reason as the tines.
    void setCabMod (float box, float cone, float dist, float angle, float susp)
    {
        cabBox.store (box, std::memory_order_relaxed);
        cabCone.store (cone, std::memory_order_relaxed);
        cabDist.store (dist, std::memory_order_relaxed);
        cabAngle.store (angle, std::memory_order_relaxed);
        cabSusp.store (susp, std::memory_order_relaxed);
        cabDirty.store (true, std::memory_order_release);
    }

    // The mic studio: the grand's bench. Spread scales the measured ILD
    // line (a wider pair sees a steeper bass-left image) and lowers the
    // onset of the interchannel phase; balance walks the whole image;
    // distance is the lid's high-frequency shadow as the pair backs off the
    // rim; the two level trims are the mixer's own. Defaults are exactly
    // the calibrated pair the grand suite measured.
    void setMicMod (float spread, float bias, float dist, float lvlL, float lvlR)
    {
        micSpread.store (spread, std::memory_order_relaxed);
        micBias.store (bias, std::memory_order_relaxed);
        micDist.store (dist, std::memory_order_relaxed);
        micLvlL.store (lvlL, std::memory_order_relaxed);
        micLvlR.store (lvlR, std::memory_order_relaxed);
        micDirty.store (true, std::memory_order_release);
    }

    // The grand's multi-mic stage (GrandMicStage.h): mode and per-mic
    // geometry are driven directly on the stage object, which does its own
    // click-safe pickup on the audio thread. Mode 0 is the calibrated pair
    // above; mode 1 is the freely positioned Stage.
    GrandMicStage& grandMicStage() noexcept { return grandMics; }

    // The string workshop: the CP-70's own bench. Same discipline as the
    // tines: atomics, a dirty flag, the bounded priority rebuild.
    void setStringMod (int i, float lenScale, float diaScale)
    {
        if (i < 0 || i >= kNumTines) return;
        auto& m = stringMod[static_cast<std::size_t> (i)];
        m.len.store (lenScale, std::memory_order_relaxed);
        m.dia.store (diaScale, std::memory_order_relaxed);
        m.dirty.store (true, std::memory_order_release);
    }

    // The velocity map: five editable ordinates over fixed abscissae
    // {0, 1/4, 1/2, 3/4, 1}, identity by default (bit-exact: the evaluator
    // short-circuits). Monotone by construction -- the setter sorts, and
    // the evaluator is a Fritsch-Carlson monotone cubic, so a curve can
    // never invert the player's dynamics.
    void setVelMap (const float* y5)
    {
        float y[5];
        for (int i = 0; i < 5; ++i) y[i] = std::clamp (y5[i], 0.0f, 1.0f);
        for (int i = 1; i < 5; ++i) y[i] = std::max (y[i], y[i - 1]);
        bool ident = true;
        for (int i = 0; i < 5; ++i)
        {
            velMapY[i].store (y[i], std::memory_order_relaxed);
            if (std::abs (y[i] - 0.25f * i) > 1.0e-6f) ident = false;
        }
        velMapIdentity.store (ident, std::memory_order_release);
    }

    // The grand's string bench: same discipline again.
    void setGrandMod (int i, float lenScale, float diaScale)
    {
        if (i < 0 || i >= kNumTines) return;
        auto& m = grandMod[static_cast<std::size_t> (i)];
        m.len.store (lenScale, std::memory_order_relaxed);
        m.dia.store (diaScale, std::memory_order_relaxed);
        m.dirty.store (true, std::memory_order_release);
    }

    // The pickup workshop: height and gap offsets need a rebuild (they move
    // the operating point the voice glides to); the winding scale is read
    // live at the flux sum and needs none.
    void setPickupMod (int i, float heightOff, float gapOff, float sens)
    {
        if (i < 0 || i >= kNumTines) return;
        auto& m = tineMod[static_cast<std::size_t> (i)];
        m.pkH.store (heightOff, std::memory_order_relaxed);
        m.pkG.store (gapOff, std::memory_order_relaxed);
        m.pkS.store (sens, std::memory_order_relaxed);
        m.dirty.store (true, std::memory_order_release);
    }

    // Test access: one tine's modal energy.
    double tineEnergy (int i) const { return tines[static_cast<std::size_t> (i)].energy(); }

    double cp70Energy (int i) const { return cp70[static_cast<std::size_t> (i)].modalEnergy(); }
    double wurliEnergy (int i) const { return wurli[static_cast<std::size_t> (i)].modalEnergy(); }
    int activeVoices() const { return numActive.load (std::memory_order_relaxed); }

    // Metering for the UI: destructive read, so the peak resets per tick.
    float consumeOutPeak (int ch)
    {
        auto& a = ch == 0 ? peakL : peakR;
        return a.exchange (0.0f, std::memory_order_relaxed);
    }

    // Visualisation, written relaxed from the audio thread.
    float vizTipDisplacement() const { return vTip.load (std::memory_order_relaxed); }
    float vizFlux() const            { return vFlux.load (std::memory_order_relaxed); }
    float vizPickupOffset() const    { return vOffset.load (std::memory_order_relaxed); }
    float vizVibratoL() const        { return vVibL.load (std::memory_order_relaxed); }
    float vizVibratoR() const        { return vVibR.load (std::memory_order_relaxed); }

    // The pickup's field profile, so the UI can draw the actual curve the tine
    // is moving through rather than an artist's impression of one.
    static constexpr int kFieldPoints = 96;
    float vizField (int i) const { return vField[i].load (std::memory_order_relaxed); }

    // The loudest sounding voice's actual tine motion, and the note it is
    // playing. The interface animates from this, so what moves on screen is
    // what the model is doing.
    // Every tine's own motion, so the interface can draw the instrument rather
    // than one part of it. Published once per block as the peak within that
    // block: a snapshot of something oscillating samples a random phase and
    // flickers, and at sixty frames a second the peak is what reads as motion.
    float vizTineTip (int i) const { return vTineTip[i].load (std::memory_order_relaxed); }
    int   vizLastNote() const      { return vLastNote.load (std::memory_order_relaxed); }

    // For tests: which tine holds what, so a blow-up can be located rather
    // than guessed at.
    const RhodesVoice& tine (int i) const { return tines[static_cast<std::size_t> (i)]; }
    double harpEnergy() const { return harp.energy(); }
    int    recoveryCount() const { return recoveries.load (std::memory_order_relaxed); }

    // Which keys are down, packed a bit per note. A key can be held long after
    // its tine has gone quiet, so the motion alone cannot show it.
    std::uint32_t vizKeys (int word) const { return vKeys[word].load (std::memory_order_relaxed); }
    static constexpr int kKeyWords = (kNumTines + 31) / 32;
    bool  vizPedal() const { return vPedal.load (std::memory_order_relaxed); }

    static constexpr int kTraceLen = RhodesVoice::kTraceLen;
    float vizTrace (int i) const { return vTrace[i].load (std::memory_order_relaxed); }
    float vizNoteHz() const      { return vNoteHz.load (std::memory_order_relaxed); }
    int   vizStrikes() const     { return vStrikes.load (std::memory_order_relaxed); }

private:
    void handleEvent (const NoteEvent& e, const EngineParams& p);
    RhodesVoice::Config rhodesConfig (const EngineParams& p) const;
    CP70Voice::Config cp70Config (const EngineParams& p) const;
    WurliVoice::Config wurliConfig (const EngineParams& p) const;
    GrandVoice::Config grandConfig (const EngineParams& p) const;
    ClavinetVoice::Config clavConfig (const EngineParams& p) const;
    void processActive (float* outL, float* outR, int numSamples,
                        const EngineParams& p,
                        const NoteEvent* events, int numEvents);
    void processWurli (float* outL, float* outR, int numSamples,
                       const EngineParams& p, const NoteEvent* events, int numEvents);
    void processClav (float* outL, float* outR, int numSamples,
                      const EngineParams& p, const NoteEvent* events, int numEvents);
    void processGrand (float* outL, float* outR, int numSamples,
                       const EngineParams& p, const NoteEvent* events, int numEvents);
    void processCP70 (float* outL, float* outR, int numSamples,
                      const EngineParams& p,
                      const NoteEvent* events, int numEvents);
    void publishField();

    double fs = 48000.0;
    // Instrument crossfade-through-silence state. Notes that arrive while
    // the old instrument fades belong to the NEW one -- they are parked
    // here and replayed the moment the seam passes, so a switch never
    // swallows the first phrase.
    int    activeInst = -1;   // -1: snap to the first block's instrument
    double instGain   = 1.0;
    static constexpr int kMaxPending = 32;
    NoteEvent pendingNotes[kMaxPending];
    int       numPending = 0;

    MagneticPickup field;
    // Heap, not std::array: eighty-eight voices weigh megabytes, and tests
    // build engines as locals. macOS grants eight megabytes of stack and
    // forgave it; Windows grants one and delivered an instant segfault --
    // the CP-70 vector learned this first, the Rhodes bank follows.
    std::vector<RhodesVoice> tines;
    // The CP-70's strings, permanently present exactly as the tines are. Two
    // arrays, one active: the process loop dispatches once per block on the
    // instrument, and each path keeps its own shape -- the Rhodes needs the
    // oversampled flux sum, the CP-70 sums forces at base rate and skips it.
    // A vector, not an array, and the difference is a segfault: 88 CP-70
    // voices carry over a hundred modes each -- about four megabytes -- and
    // an engine built on a test's stack died the moment they lived inside
    // the object. The Rhodes tines stay inline; they are a tenth the size.
    std::vector<CP70Voice> cp70;
    std::array<std::uint32_t, kNumTines> cp70CfgVersion {};
    CP70Preamp cp70Preamp;
    std::array<bool, kNumTines> keyDown {};
    Harp harp;
    ActionNoise action;
    Rng noiseRng { 0x51ed270bu };

    PickupCoil coil;
    SuitcasePreamp preamp;
    SuitcaseVibrato vibrato;
    Cabinet cabinetL, cabinetR;
    Phaser phaserL, phaserR;
    Decimator decimL, decimR;
    Room room;
    float lastSpaceSize = -1.0f;

    double softAmount  = 0.0;     // CC67 continuous, for the rail (half-blow) mode
    std::atomic<float> velMapY[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    std::atomic<bool> velMapIdentity { true };

    float velMapEval (float v) const
    {
        if (velMapIdentity.load (std::memory_order_acquire)) return v;
        float y[5];
        for (int i = 0; i < 5; ++i) y[i] = velMapY[i].load (std::memory_order_relaxed);
        v = std::clamp (v, 0.0f, 1.0f);
        const float x = v * 4.0f;
        const int seg = std::min (3, static_cast<int> (x));
        const float t = x - static_cast<float> (seg);
        // Fritsch-Carlson slopes on the uniform grid: secants, endpoint
        // one-sided, interior harmonic mean of neighbouring secants (zero
        // when either secant is zero) -- monotone by construction.
        auto slope = [&] (int i) -> float
        {
            auto sec = [&] (int a) { return (y[a + 1] - y[a]) * 4.0f; };
            if (i == 0) return sec (0);
            if (i == 4) return sec (3);
            const float s0 = sec (i - 1), s1 = sec (i);
            if (s0 <= 0.0f || s1 <= 0.0f) return 0.0f;
            return 2.0f * s0 * s1 / (s0 + s1);
        };
        const float y0 = y[seg], y1 = y[seg + 1];
        const float m0 = slope (seg) * 0.25f, m1 = slope (seg + 1) * 0.25f;
        const float t2 = t * t, t3 = t2 * t;
        return std::clamp ((2*t3 - 3*t2 + 1) * y0 + (t3 - 2*t2 + t) * m0
                         + (-2*t3 + 3*t2) * y1 + (t3 - t2) * m1, 0.0f, 1.0f);
    }
    bool   pedalDown   = false;   // engaged at all -- gates the sympathetic path
    bool   sostenutoDown = false; // the middle pedal's own state, for its edge
    bool   pedalHigh   = false;   // thunk hysteresis state (0.65 / 0.35)
    static constexpr double kThunkPress = 0.60;  // calibrated by engine row 12.2
    static constexpr double kThunkLift  = 2.2;
    double pedalAmount = 0.0;     // continuous CC64, 0 = up, 1 = fully down

    void setPedalAmount (double a)
    {
        const double prev = pedalAmount;
        pedalAmount = std::clamp (a, 0.0, 1.0);
        pedalDown = pedalAmount > 0.01;
        // The trapwork's end-of-travel thumps, with hysteresis (0.65 up,
        // 0.35 down) so half-pedal work rides between the stops silently --
        // a real tray thumps at the stops, not in the middle of the travel.
        // The lift lands slightly harder: the tray falls under its spring
        // plus gravity, the press is the foot's controlled push.
        if (! pedalHigh && pedalAmount > 0.65 && prev <= 0.65)
        {
            pedalHigh = true;
            grandBoard.pedalThunk (kThunkPress);
        }
        else if (pedalHigh && pedalAmount < 0.35 && prev >= 0.35)
        {
            pedalHigh = false;
            grandBoard.pedalThunk (kThunkLift);
        }
        for (auto& v : tines) v.setPedal (pedalAmount);
        for (auto& v : cp70)  v.setPedal (pedalAmount);
        for (auto& v : wurli) v.setPedal (pedalAmount);
        for (auto& v : grand) v.setPedal (pedalAmount);
        for (auto& v : clav)  v.setPedal (pedalAmount);
    }
    float bendSemis = 0.0f;
    float expression = 1.0f;
    std::uint32_t seed = 0x2545f491u;

    // The configuration the tines are currently built to, compared whole so a
    // newly added field cannot be forgotten. Deliberately initialised to
    // something no real configuration equals, so the first block always builds.
    RhodesVoice::Config lastCfg { -1.0e30 };
    CP70Voice::Config lastCP70Cfg { -1.0e30 };
    WurliVoice::Config lastWurliCfg { -1.0e30 };
    GrandVoice::Config lastGrandCfg { -1.0e30 };
    ClavinetVoice::Config lastClavCfg { -1.0e30 };
    // Which configuration each tine has been built to. A knob being turned
    // changes the configuration every block, and rebuilding all eighty-eight
    // every time is what pushed blocks past their deadline.
    std::uint32_t cfgVersion = 0;
    std::array<std::uint32_t, kNumTines> tineCfgVersion {};
    std::array<std::uint32_t, kNumTines> wurliCfgVersion {};
    std::vector<WurliVoice> wurli;
    // The grand: eighty-eight voices on one soundboard, radiated through a
    // mic pair. Render order is the calibrated one from the grand suite:
    // voices push termination force and knock into the radiator, the board
    // ticks, the radiator ticks, board readout and radiator sum, mic pair
    // last. Pan gains per note are the radiator's own bass-left law.
    std::array<std::uint32_t, kNumTines> grandCfgVersion {};
    std::vector<GrandVoice> grand;
    GrandBoard    grandBoard;
    // What the board is currently built to, so a body-bench change can wait
    // for silence instead of re-solving the plate under a ringing note.
    GrandBoard::Config lastBoardCfg { -1.0e30 };
    GrandRadiator grandRad;
    GrandMicStage grandMics;
    std::array<double, kNumTines> grandPanL {}, grandPanR {};
    // The Clavinet: sixty real keys (F1-E6, MIDI 29-88); voices outside that
    // compass simply do not exist, as on the instrument.
    std::array<std::uint32_t, kNumTines> clavCfgVersion {};
    std::vector<ClavinetVoice> clav;
    ClavinetToneStack clavTone;
    ClavinetPreamp    clavPre;
    ClavinetKnock     clavKnock;
    WurliPickupBus wurliBus;
    WurliPreamp   wurliPre;
    WurliTremolo  wurliTrem;
    // The base-rate instruments get their own cabinet pair: the shared pair
    // is prepared at the oversampled rate for the Rhodes loop, and running
    // those filters at base rate lands every corner four times low -- which
    // is exactly what the CP-70 was doing before this pair existed.
    Cabinet cabinetBL, cabinetBR;
    // One frame per instrument family: the sympathetic path. The Rhodes'
    // harp came first; the string bridge-frame and the reed bar follow the
    // same passive spring discipline.
    Harp cp70Frame, wurliFrame;

    // Block-rate parameter smoothing for the controls whose consumers take
    // discrete jumps -- tone shelves, drive gains, the cabinet morph, the
    // Wurlitzer's rail. A knob dragged across blocks otherwise steps its
    // coefficients at the block rate, and the steps are heard as crackle.
    // The output gain gets a per-sample ramp instead, in each path.
    struct SmoothedParams
    {
        float drive = -1.0f, bass = 0.0f, treb = 0.0f, cabMix = -1.0f, sat = -1.0f;
        float air = 0.0f;
        float gain = -1.0f;
        // How much of the coil, and of the reed's electrostatic bus, is in
        // the path. The transducer choice is a bench swap -- you cannot
        // change the pickup under a ringing note -- and these are what stop
        // it being applied to one retroactively. Two values rather than one
        // because the two instruments take the swap differently: the tine
        // reads a coil on Magnetic and Native, the reed its bus on Native
        // and Electro.
        float coilMix = -1.0f;
        float electroMix = -1.0f;
        void step (float& v, float target, float k)
        {
            if (v < -0.5f) { v = target; return; }   // first block: snap
            v += (target - v) * k;
            if (std::abs (v - target) < 1.0e-5f) v = target;
        }
    };
    SmoothedParams sm;

    // The clarity shelf: a textbook RBJ high shelf at 8.5 kHz. A one-pole
    // blend was tried first and measured: at this corner the pole leaks so
    // much passband that the blend phase-cancels its own boost -- three
    // decibels of swing from a twenty-four decibel control. The biquad
    // measures the full swing.
    struct AirShelf
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double curDb = 0.0;
        bool shelfInit = false;
        void set (double dB, double fs)
        {
            // Rate-limited like the cabinet morph: an RBJ shelf's coefficient
            // step rings its own resonance under fast automation.
            curDb += shelfInit ? std::clamp (dB - curDb, -0.15, 0.15) : dB - curDb;
            shelfInit = true;
            const double A = std::pow (10.0, curDb / 40.0);
            const double w = 2.0 * kPiD * 8500.0 / fs;
            const double c = std::cos (w);
            const double al = std::sin (w) / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / 0.9 - 1.0) + 2.0);
            const double sq = 2.0 * std::sqrt (A) * al;
            const double a0 = (A + 1.0) - (A - 1.0) * c + sq;
            b0 = A * ((A + 1.0) + (A - 1.0) * c + sq) / a0;
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0;
            b2 = A * ((A + 1.0) + (A - 1.0) * c - sq) / a0;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c) / a0;
            a2 = ((A + 1.0) - (A - 1.0) * c - sq) / a0;
        }
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0.0; }
    };
    AirShelf airL, airR;
    float smoothK (int numSamples) const
    {
        return 1.0f - std::exp (-static_cast<float> (numSamples) / (0.040f * static_cast<float> (fs)));   // ~40 ms
    }

    // The workshop's per-tine steel, written from the message thread and read
    // by the audio thread's rebuild. Atomics rather than a lock: a torn pair
    // would only mistune one tine for one rebuild, but a formal race is a
    // formal race, and lock-free floats cost nothing here.
    struct TineMod { std::atomic<float> len { 1.0f }, dia { 1.0f };
                     std::atomic<float> pkH { 0.0f }, pkG { 0.0f }, pkS { 1.0f };
                     std::atomic<bool> dirty { false } ; };
    std::array<TineMod, kNumTines> tineMod {};
    void rebuildTine (int i, const RhodesVoice::Config& cfg);
    struct StringMod { std::atomic<float> len { 1.0f }, dia { 1.0f };
                       std::atomic<bool> dirty { false }; };
    std::array<StringMod, kNumTines> stringMod {};
    void rebuildString (int i, const CP70Voice::Config& cfg);
    std::array<StringMod, kNumTines> grandMod {};
    void rebuildGrandString (int i, const GrandVoice::Config& cfg);
    void rebuildWurli (int i, const WurliVoice::Config& cfg);
    void rebuildClav (int i, const ClavinetVoice::Config& cfg);

    // Per-note tuning, latched. This is the tuner's offset for each string,
    // and it lives beside the workshop's steel because it is the same kind of
    // thing: a per-note property of the instrument that every rebuild path has
    // to carry, not a control the player rides. Written only from the audio
    // thread, at a note-on, from that event's own tuneCents -- so a change
    // reaches the NEXT note struck on that string and never the one ringing.
    // All zero is the instrument as it has always been, and the rebuild
    // helpers fold nothing in at zero, so the untouched path is untouched.
    std::array<float, kNumTines> noteCents {};
    void invalidateNoteBuild (int i);
    std::atomic<float> cabBox { 0.74f }, cabCone { 0.59f }, cabDist { 0.5f },
                       cabAngle { 0.25f }, cabSusp { 0.5f };
    std::atomic<bool> cabDirty { false };
    std::atomic<float> micSpread { 1.0f }, micBias { 0.0f }, micDist { 0.0f },
                       micLvlL { 1.0f }, micLvlR { 1.0f };
    std::atomic<bool> micDirty { false };
    double micGL = 1.0, micGR = 1.0, micLidAmt = 0.0;
    double grandBassGain = 0.0, grandTrebGain = 0.0;
    double deskLoL = 0.0, deskLoR = 0.0, deskHiL = 0.0, deskHiR = 0.0;
    double micLidL = 0.0, micLidR = 0.0;   // one-pole states for the lid shadow
    float lastCoilSat = -1.0f;
    // How many times the output chain had to be rebuilt. Reported by tests.
    std::atomic<int> recoveries { 0 };

    std::atomic<int>   numActive { 0 };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> vTip { 0.0f }, vFlux { 0.0f }, vOffset { 0.0f };
    std::atomic<float> vVibL { 1.0f }, vVibR { 1.0f };
    std::atomic<float> vField[kFieldPoints];
    std::atomic<float> vTrace[RhodesVoice::kTraceLen];
    std::atomic<float> vNoteHz { 440.0f };
    std::atomic<int>   vStrikes { 0 };
    std::atomic<int>   vLastNote { 60 };
    std::atomic<float> vTineTip[kNumTines];
    std::atomic<std::uint32_t> vKeys[kKeyWords];
    std::atomic<bool> vPedal { false };
    float tineBlockPeak[kNumTines] {};
    int strikeCount = 0;
};

} // namespace epi
