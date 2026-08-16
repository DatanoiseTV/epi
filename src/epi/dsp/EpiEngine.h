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

#include "OutputChain.h"
#include "PickupMagnetic.h"
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
    float cabMix      = 0.5f;

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
//   32 voices -> summed flux -> one coil -> preamp -> vibrato -> cabinet
//
// The summing point is where the real instrument sums too: every note has its
// own pickup, but they are all wired onto one bus feeding one preamp, so the
// nonlinearities downstream of that bus are SHARED. Two notes played together
// intermodulate in the preamp exactly as they do on the real thing, which is a
// large part of why a Rhodes chord does not sound like two Rhodes notes.
// ---------------------------------------------------------------------------
class EpiEngine
{
public:
    static constexpr int kMaxVoices = 32;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void process (float* outL, float* outR, int numSamples,
                  const EngineParams& params,
                  const NoteEvent* events, int numEvents);

    void setPitchBend (float semitones) { bendSemis = semitones; }
    void setExpression (float scale)    { expression = scale; }

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
    static constexpr int kTraceLen = RhodesVoice::kTraceLen;
    float vizTrace (int i) const { return vTrace[i].load (std::memory_order_relaxed); }
    float vizNoteHz() const      { return vNoteHz.load (std::memory_order_relaxed); }
    int   vizStrikes() const     { return vStrikes.load (std::memory_order_relaxed); }

private:
    void handleEvent (const NoteEvent& e, const EngineParams& p);
    RhodesVoice::Config rhodesConfig (const EngineParams& p) const;
    int  allocateVoice (int note);
    void publishField();

    double fs = 48000.0;

    MagneticPickup field;
    std::array<RhodesVoice, kMaxVoices> voices;
    std::array<int, kMaxVoices> voiceAge {};
    int ageCounter = 0;

    Decimator decimator;
    PickupCoil coil;
    SuitcasePreamp preamp;
    SuitcaseVibrato vibrato;
    Cabinet cabinet;

    bool pedalDown = false;
    float bendSemis = 0.0f;
    float expression = 1.0f;
    std::uint32_t seed = 0x2545f491u;

    // Cached so a parameter that costs a full voice reconfigure only triggers
    // one when it actually moves.
    float lastPickupPos = -99.0f, lastPickupDist = -99.0f;
    float lastTipMass = -99.0f, lastResDamp = -99.0f, lastBarCouple = -99.0f;

    std::atomic<int>   numActive { 0 };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> vTip { 0.0f }, vFlux { 0.0f }, vOffset { 0.0f };
    std::atomic<float> vVibL { 1.0f }, vVibR { 1.0f };
    std::atomic<float> vField[kFieldPoints];
    std::atomic<float> vTrace[RhodesVoice::kTraceLen];
    std::atomic<float> vNoteHz { 440.0f };
    std::atomic<int>   vStrikes { 0 };
    int strikeCount = 0;
};

} // namespace epi
