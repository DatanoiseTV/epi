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

// Which instrument. Order must match epi::ids::instrumentNames.
enum class Instrument { rhodes = 0, wurlitzer, clavinet, cp70 };

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
    enum Type { noteOn, noteOff, allNotesOff, sustainOn, sustainOff };
    int   offset   = 0;
    Type  type     = noteOn;
    int   note     = 60;
    float velocity = 0.8f;
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
    const RhodesVoice& tine (int i) const { return tines[i]; }
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
    void processCP70 (float* outL, float* outR, int numSamples,
                      const EngineParams& p,
                      const NoteEvent* events, int numEvents);
    void publishField();

    double fs = 48000.0;

    MagneticPickup field;
    std::array<RhodesVoice, kNumTines> tines;
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

    bool pedalDown = false;
    float bendSemis = 0.0f;
    float expression = 1.0f;
    std::uint32_t seed = 0x2545f491u;

    // The configuration the tines are currently built to, compared whole so a
    // newly added field cannot be forgotten. Deliberately initialised to
    // something no real configuration equals, so the first block always builds.
    RhodesVoice::Config lastCfg { -1.0e30 };
    CP70Voice::Config lastCP70Cfg { -1.0e30 };
    // Which configuration each tine has been built to. A knob being turned
    // changes the configuration every block, and rebuilding all eighty-eight
    // every time is what pushed blocks past their deadline.
    std::uint32_t cfgVersion = 0;
    std::array<std::uint32_t, kNumTines> tineCfgVersion {};

    // The workshop's per-tine steel, written from the message thread and read
    // by the audio thread's rebuild. Atomics rather than a lock: a torn pair
    // would only mistune one tine for one rebuild, but a formal race is a
    // formal race, and lock-free floats cost nothing here.
    struct TineMod { std::atomic<float> len { 1.0f }, dia { 1.0f };
                     std::atomic<float> pkH { 0.0f }, pkG { 0.0f }, pkS { 1.0f };
                     std::atomic<bool> dirty { false } ; };
    std::array<TineMod, kNumTines> tineMod {};
    void rebuildTine (int i, const RhodesVoice::Config& cfg);
    std::atomic<float> cabBox { 0.74f }, cabCone { 0.59f }, cabDist { 0.5f },
                       cabAngle { 0.25f }, cabSusp { 0.5f };
    std::atomic<bool> cabDirty { false };
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
