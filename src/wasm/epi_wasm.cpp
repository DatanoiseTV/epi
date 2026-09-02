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

// ---------------------------------------------------------------------------
// Epi in a browser tab.
//
// The whole instrument compiled to WebAssembly and driven from an
// AudioWorklet: the DSP runs on the listener's own machine, out of their own
// speakers, with nothing installed. This file is the only new C++ it needs,
// and it is a shim -- roughly two hundred lines of glue around exactly the
// same engine the plugin runs.
//
// What is NOT here, deliberately: the parameter layout and the presets. Both
// are JUCE, and transcribing them into a second copy is how a copy starts to
// rot. They are dumped from the running instrument as JSON instead
// (epi-headless --dump-parameters / --dump-presets) and read on the JS side,
// which then hands raw values across by index. The index is the control map's
// order, which tests/test_epi_control.cpp already pins, so the two sides share
// one ordering that cannot move quietly.
//
// The interface is not new either. It is ui/epi, the plugin's own bundle,
// unchanged: this is the third host to sit behind it after the plugin's
// WebView and the headless server, and like the second it substitutes one
// file -- the one that talks to the host -- and leaves the rest alone.
// ---------------------------------------------------------------------------

#include "epi/ControlMap.h"
#include "epi/EngineParamMap.h"
#include "epi/dsp/EpiEngine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
 #include <emscripten/emscripten.h>
 #define EPI_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
 #define EPI_EXPORT extern "C"
#endif

namespace
{

using namespace epi;

// The telemetry frame, packed once per visual tick into one float array so the
// worklet posts a single buffer rather than twenty calls across the boundary.
// The layout is fixed here and read by ui/wasm/wasm-frontend.js; the sizes are
// published through epi_telemetry_size() so the two cannot disagree about the
// length.
enum : int
{
    kTeleOut     = 0,                              // 2  peak dB, L and R
    kTeleScalars = kTeleOut + 2,                   // 10 see the packer below
    kTeleTrace   = kTeleScalars + 10,
    kTeleField   = kTeleTrace + EpiEngine::kTraceLen,
    kTeleHarp    = kTeleField + EpiEngine::kFieldPoints,
    kTeleTotal   = kTeleHarp + EpiEngine::kNumTines
};

// The keys are a BITMASK, and a bitmask does not survive a float. Above 2^24 a
// float32 cannot represent consecutive integers, so the top of an eighty-eight
// key mask would come back with keys that are not down and keys that are not
// up. It travels as its own integer array.

struct Instance
{
    explicit Instance (double sampleRate, int maxBlock)
        : block (maxBlock), l ((size_t) maxBlock), r ((size_t) maxBlock)
    {
        engine.prepare (sampleRate, maxBlock);

        // The engine's parameters arrive from JS as raw values by index; the
        // mapping into EngineParams is engineParamsFrom, the SAME function the
        // plugin uses, so the web build cannot drift from it.
        raw.assign ((size_t) kNumControls, 0.0f);
        for (int i = 0; i < kNumControls; ++i)
            index.emplace (kControlMap[i].paramId, i);
    }

    EngineParams params() const
    {
        return engineParamsFrom ([this] (const char* id) -> float
        {
            const auto it = index.find (id);
            return it == index.end() ? 0.0f : raw[(size_t) it->second];
        });
    }

    EpiEngine engine;
    int block;
    std::vector<float> l, r;
    std::vector<float> raw;
    std::unordered_map<std::string, int> index;
    std::vector<NoteEvent> pending;
    std::vector<float> telemetry = std::vector<float> ((size_t) kTeleTotal, 0.0f);
    std::vector<std::uint32_t> keys = std::vector<std::uint32_t> ((size_t) EpiEngine::kKeyWords, 0u);

    // The per-part benches. The engine owns the effect; these are the cache
    // the interface reads back, exactly as the plugin's processor keeps one.
    // Without them the workshop panels would open onto controls that do
    // nothing, and a control that does nothing is worse than one that is
    // missing.
    std::vector<float> tineMods   = std::vector<float> (EpiEngine::kNumTines * 2, 1.0f);
    std::vector<float> stringMods = std::vector<float> (EpiEngine::kNumTines * 2, 1.0f);
    std::vector<float> grandMods  = std::vector<float> (EpiEngine::kNumTines * 2, 1.0f);
    std::vector<float> pickupMods = std::vector<float> (EpiEngine::kNumTines * 3, 0.0f);
    std::vector<float> cabMods    = std::vector<float> (5, 0.0f);
    std::vector<float> micMods    = std::vector<float> (5, 0.0f);
    std::vector<float> micStage   = std::vector<float> (31, 0.0f);
    std::vector<float> velMap     = std::vector<float> { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
};

std::unique_ptr<Instance> g;

float toDb (float lin) { return lin <= 1.0e-5f ? -90.0f : 20.0f * std::log10 (lin); }

} // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------
EPI_EXPORT void epi_create (double sampleRate, int maxBlock)
{
    // Heap, not stack. The engine is about 180 kB and a wasm module's default
    // stack is 64 kB, which traps on construction rather than failing to
    // compile -- an afternoon of confusion the first time.
    g = std::make_unique<Instance> (sampleRate, maxBlock);
}

EPI_EXPORT void epi_destroy() { g.reset(); }

EPI_EXPORT int epi_num_params()      { return kNumControls; }
EPI_EXPORT int epi_telemetry_size()  { return kTeleTotal; }
EPI_EXPORT int epi_trace_len()       { return EpiEngine::kTraceLen; }
EPI_EXPORT int epi_field_points()    { return EpiEngine::kFieldPoints; }
EPI_EXPORT int epi_num_tines()       { return EpiEngine::kNumTines; }
EPI_EXPORT int epi_key_words()       { return EpiEngine::kKeyWords; }
EPI_EXPORT int epi_lo_note()         { return EpiEngine::kLoNote; }

// Pointers into the wasm heap, so JS reads and writes in place with a typed
// array view and nothing is copied across the boundary per block.
EPI_EXPORT float* epi_out_l()        { return g ? g->l.data() : nullptr; }
EPI_EXPORT float* epi_out_r()        { return g ? g->r.data() : nullptr; }
EPI_EXPORT float* epi_telemetry()    { return g ? g->telemetry.data() : nullptr; }
EPI_EXPORT std::uint32_t* epi_keys() { return g ? g->keys.data() : nullptr; }

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
EPI_EXPORT void epi_set_param (int i, float rawValue)
{
    if (g && i >= 0 && i < kNumControls) g->raw[(size_t) i] = rawValue;
}

EPI_EXPORT void epi_note (int note, float velocity, int on)
{
    if (! g || note < 0 || note > 127) return;
    g->pending.push_back ({ 0, on ? NoteEvent::noteOn : NoteEvent::noteOff,
                            note, on ? std::max (0.05f, velocity) : 0.0f });
}

// ---------------------------------------------------------------------------
// An event placed at a SAMPLE inside the coming block.
//
// Everything above puts its event at offset zero, which is right for a control
// somebody is turning: it happens when it happens, and a block is under three
// milliseconds. It is not right for a score. A file played by firing events
// from a timer quantises every note to a block boundary -- at 128 frames and
// 48 kHz that is 2.7 ms of grid, which is audible as stiffness on rolled
// chords and as a flam on anything doubled, and it is exactly the smearing the
// jitter reduction path elsewhere in this instrument exists to remove. A
// player that knows the score in advance has no excuse for it.
//
// `type` follows NoteEvent::Type. `value` is the velocity, the pedal position,
// or the key position, depending on the type.
// ---------------------------------------------------------------------------
EPI_EXPORT void epi_event (int type, int note, float value, int offset)
{
    if (! g) return;
    if (type < 0 || type > (int) NoteEvent::keyPosition) return;

    NoteEvent e;
    e.offset = std::clamp (offset, 0, std::max (0, g->block - 1));
    e.type = (NoteEvent::Type) type;
    e.note = std::clamp (note, 0, 127);
    e.velocity = value;
    if (e.type == NoteEvent::noteOn) e.velocity = std::max (0.0009f, value);
    g->pending.push_back (e);
}

// The type numbers, so the JavaScript side does not carry a second copy of an
// enum that could quietly disagree with this one.
EPI_EXPORT int epi_type_note_on()     { return (int) NoteEvent::noteOn; }
EPI_EXPORT int epi_type_note_off()    { return (int) NoteEvent::noteOff; }
EPI_EXPORT int epi_type_all_off()     { return (int) NoteEvent::allNotesOff; }
EPI_EXPORT int epi_type_sustain()     { return (int) NoteEvent::sustain; }
EPI_EXPORT int epi_type_sostenuto()   { return (int) NoteEvent::sostenuto; }
EPI_EXPORT int epi_type_soft()        { return (int) NoteEvent::soft; }
EPI_EXPORT int epi_type_key_position(){ return (int) NoteEvent::keyPosition; }

// The damper rail reads pedal DEPTH, not an on/off switch, which is what makes
// half-pedalling work -- so the full 0..1 is carried through.
EPI_EXPORT void epi_sustain (float depth)
{
    if (g) g->pending.push_back ({ 0, NoteEvent::sustain, 0, depth });
}

EPI_EXPORT void epi_sostenuto (int down)
{
    if (g) g->pending.push_back ({ 0, NoteEvent::sostenuto, 0, down ? 1.0f : 0.0f });
}

EPI_EXPORT void epi_soft (int down)
{
    if (g) g->pending.push_back ({ 0, NoteEvent::soft, 0, down ? 1.0f : 0.0f });
}

EPI_EXPORT void epi_all_notes_off()
{
    if (g) g->pending.push_back ({ 0, NoteEvent::allNotesOff, 0, 0.0f });
}

EPI_EXPORT void epi_expression (float v) { if (g) g->engine.setExpression (v); }
EPI_EXPORT void epi_pitch_bend (float semis) { if (g) g->engine.setPitchBend (semis); }

EPI_EXPORT void epi_set_vel_map (float y0, float y1, float y2, float y3, float y4)
{
    if (! g) return;
    g->velMap = { y0, y1, y2, y3, y4 };
    g->engine.setVelMap (g->velMap.data());
}

// ---------------------------------------------------------------------------
// The workshops.
//
// Every one of these forwards to the same engine call the plugin's processor
// makes, and keeps the same read-back cache, so the web build's benches are
// the benches rather than a subset.
// ---------------------------------------------------------------------------
EPI_EXPORT float* epi_tine_mods()   { return g ? g->tineMods.data()   : nullptr; }
EPI_EXPORT float* epi_string_mods() { return g ? g->stringMods.data() : nullptr; }
EPI_EXPORT float* epi_grand_mods()  { return g ? g->grandMods.data()  : nullptr; }
EPI_EXPORT float* epi_pickup_mods() { return g ? g->pickupMods.data() : nullptr; }
EPI_EXPORT float* epi_cab_mods()    { return g ? g->cabMods.data()    : nullptr; }
EPI_EXPORT float* epi_mic_mods()    { return g ? g->micMods.data()    : nullptr; }
EPI_EXPORT float* epi_mic_stage()   { return g ? g->micStage.data()   : nullptr; }
EPI_EXPORT float* epi_vel_map()     { return g ? g->velMap.data()     : nullptr; }

EPI_EXPORT void epi_set_tine_mod (int i, float len, float dia)
{
    if (! g || i < 0 || i >= EpiEngine::kNumTines) return;
    g->tineMods[(size_t) (i * 2)] = len; g->tineMods[(size_t) (i * 2 + 1)] = dia;
    g->engine.setTineMod (i, len, dia);
}

EPI_EXPORT void epi_set_string_mod (int i, float len, float dia)
{
    if (! g || i < 0 || i >= EpiEngine::kNumTines) return;
    g->stringMods[(size_t) (i * 2)] = len; g->stringMods[(size_t) (i * 2 + 1)] = dia;
    g->engine.setStringMod (i, len, dia);
}

EPI_EXPORT void epi_set_grand_mod (int i, float len, float dia)
{
    if (! g || i < 0 || i >= EpiEngine::kNumTines) return;
    g->grandMods[(size_t) (i * 2)] = len; g->grandMods[(size_t) (i * 2 + 1)] = dia;
    g->engine.setGrandMod (i, len, dia);
}

EPI_EXPORT void epi_set_pickup_mod (int i, float h, float gap, float sens)
{
    if (! g || i < 0 || i >= EpiEngine::kNumTines) return;
    g->pickupMods[(size_t) (i * 3)]     = h;
    g->pickupMods[(size_t) (i * 3 + 1)] = gap;
    g->pickupMods[(size_t) (i * 3 + 2)] = sens;
    g->engine.setPickupMod (i, h, gap, sens);
}

EPI_EXPORT void epi_set_cab_mod (float box, float cone, float dist, float angle, float susp)
{
    if (! g) return;
    g->cabMods = { box, cone, dist, angle, susp };
    g->engine.setCabMod (box, cone, dist, angle, susp);
}

EPI_EXPORT void epi_set_mic_mod (float spread, float bias, float dist, float lvlL, float lvlR)
{
    if (! g) return;
    g->micMods = { spread, bias, dist, lvlL, lvlR };
    g->engine.setMicMod (spread, bias, dist, lvlL, lvlR);
}

// Thirty-one numbers: the mode, then five microphones of six. Unpacked here
// the same way the processor unpacks it, because the interface sends the flat
// array and the stage wants structures.
EPI_EXPORT void epi_set_mic_stage_from (const float* v)
{
    if (! g || v == nullptr) return;
    for (int i = 0; i < 31; ++i) g->micStage[(size_t) i] = v[i];

    auto& st = g->engine.grandMicStage();
    st.setMode (static_cast<int> (v[0] + 0.5f));
    for (int i = 0; i < 5; ++i)
    {
        GrandMicStage::Mic m;
        m.on     = v[1 + i * 6] >= 0.5f;
        m.x      = v[2 + i * 6];
        m.z      = v[3 + i * 6];
        m.h      = v[4 + i * 6];
        m.gainDb = v[5 + i * 6];
        m.pan    = v[6 + i * 6];
        st.setMic (i, m);
    }
}

// JS writes the 31 values straight into epi_mic_stage() and then calls this,
// which avoids marshalling an array across the boundary.
EPI_EXPORT void epi_commit_mic_stage()
{
    if (g) epi_set_mic_stage_from (g->micStage.data());
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
EPI_EXPORT void epi_process (int numFrames)
{
    if (! g) return;
    const int n = std::min (numFrames, g->block);
    const auto p = g->params();

    g->engine.process (g->l.data(), g->r.data(), n, p,
                       g->pending.empty() ? nullptr : g->pending.data(),
                       (int) g->pending.size());
    g->pending.clear();
}

// ---------------------------------------------------------------------------
// Telemetry, packed into the shared array in the layout above.
// ---------------------------------------------------------------------------
EPI_EXPORT void epi_pack_telemetry()
{
    if (! g) return;
    auto& e = g->engine;
    float* t = g->telemetry.data();

    t[kTeleOut + 0] = toDb (e.consumeOutPeak (0));
    t[kTeleOut + 1] = toDb (e.consumeOutPeak (1));

    t[kTeleScalars + 0] = e.vizNoteHz();
    t[kTeleScalars + 1] = (float) e.vizStrikes();
    t[kTeleScalars + 2] = e.vizTipDisplacement();
    t[kTeleScalars + 3] = e.vizFlux();
    t[kTeleScalars + 4] = e.vizPickupOffset();
    t[kTeleScalars + 5] = e.vizVibratoL();
    t[kTeleScalars + 6] = e.vizVibratoR();
    t[kTeleScalars + 7] = (float) e.activeVoices();
    t[kTeleScalars + 8] = (float) e.vizLastNote();
    t[kTeleScalars + 9] = e.vizPedal() ? 1.0f : 0.0f;

    for (int i = 0; i < EpiEngine::kTraceLen; ++i)    t[kTeleTrace + i] = e.vizTrace (i);
    for (int i = 0; i < EpiEngine::kFieldPoints; ++i) t[kTeleField + i] = e.vizField (i);
    // Microns, which is the unit the interface's harp is drawn in.
    for (int i = 0; i < EpiEngine::kNumTines; ++i)    t[kTeleHarp + i] = e.vizTineTip (i) * 1.0e6f;
    for (int i = 0; i < EpiEngine::kKeyWords; ++i)    g->keys[(size_t) i] = e.vizKeys (i);
}
