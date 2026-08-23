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

    bool   unaCorda    = false;   // CC67, grand only
    bool   pedalDown   = false;   // engaged at all -- gates the sympathetic path
    double pedalAmount = 0.0;     // continuous CC64, 0 = up, 1 = fully down

    void setPedalAmount (double a)
    {
        pedalAmount = std::clamp (a, 0.0, 1.0);
        pedalDown = pedalAmount > 0.01;
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
    GrandRadiator grandRad;
    GrandMicPair  grandMics;
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
    std::atomic<float> cabBox { 0.74f }, cabCone { 0.59f }, cabDist { 0.5f },
                       cabAngle { 0.25f }, cabSusp { 0.5f };
    std::atomic<bool> cabDirty { false };
    std::atomic<float> micSpread { 1.0f }, micBias { 0.0f }, micDist { 0.0f },
                       micLvlL { 1.0f }, micLvlR { 1.0f };
    std::atomic<bool> micDirty { false };
    double micGL = 1.0, micGR = 1.0, micLidAmt = 0.0;
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
