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

#include "epi/ui/UiBridge.h"
#include "epi/PluginProcessor.h"

#include <array>
#include <cmath>

namespace epi::ui
{
namespace
{
    inline float f (const juce::var& payload, const char* name, double fallback)
    {
        return static_cast<float> (static_cast<double> (payload.getProperty (name, fallback)));
    }

    template <typename Rows>
    juce::var flatten (const Rows& rows)
    {
        juce::Array<juce::var> flat;
        for (const auto& row : rows)
            for (float v : row)
                flat.add (juce::var (static_cast<double> (v)));
        return juce::var (flat);
    }

    template <typename Row>
    juce::var flattenOne (const Row& row)
    {
        juce::Array<juce::var> flat;
        for (float v : row)
            flat.add (juce::var (static_cast<double> (v)));
        return juce::var (flat);
    }
}

const std::function<void (const juce::var&)>* Bridge::findListener (const juce::String& name) const
{
    for (const auto& e : listeners)
        if (e.first.toString() == name)
            return &e.second;
    return nullptr;
}

const std::function<juce::var (const juce::var&)>* Bridge::findNative (const juce::String& name) const
{
    for (const auto& e : natives)
        if (e.first.toString() == name)
            return &e.second;
    return nullptr;
}

Bridge makeBridge (EpiAudioProcessor& proc)
{
    Bridge b;
    auto& presets = proc.getPresetManager();

    auto native = [&b] (const char* name, std::function<juce::var (const juce::var&)> fn)
    { b.natives.emplace_back (juce::Identifier { name }, std::move (fn)); };
    auto listen = [&b] (const char* name, std::function<void (const juce::var&)> fn)
    { b.listeners.emplace_back (juce::Identifier { name }, std::move (fn)); };

    // ---- presets ----------------------------------------------------------
    native ("listFactoryPresets", [&presets] (const juce::var&)
    {
        juce::Array<juce::var> arr;
        for (const auto& n : presets.getFactoryNames()) arr.add (juce::var (n));
        return juce::var (arr);
    });
    native ("listUserPresets", [&presets] (const juce::var&)
    {
        juce::Array<juce::var> arr;
        for (const auto& n : presets.getUserNames()) arr.add (juce::var (n));
        return juce::var (arr);
    });
    listen ("preset_prev", [&presets] (const juce::var&) { presets.previous(); });
    listen ("preset_next", [&presets] (const juce::var&) { presets.next(); });
    listen ("preset_load", [&presets] (const juce::var& p)
    {
        const auto name = p.getProperty ("name", juce::String()).toString();
        if (name.isNotEmpty()) presets.loadByName (name);
    });
    listen ("preset_save", [&presets] (const juce::var& p)
    {
        const auto name = p.getProperty ("name", juce::String()).toString().trim();
        if (name.isNotEmpty()) presets.saveUser (name);
    });
    listen ("preset_delete", [&presets] (const juce::var& p)
    {
        const auto name = p.getProperty ("name", juce::String()).toString();
        if (name.isNotEmpty()) presets.deleteUser (name);
    });

    // ---- the per-part benches --------------------------------------------
    native ("getTineMods", [&proc] (const juce::var&) { return flatten (proc.getTineMods()); });
    listen ("tine_mod", [&proc] (const juce::var& p)
    {
        proc.setTineMod (static_cast<int> (p.getProperty ("index", -1)),
                         f (p, "len", 1.0), f (p, "dia", 1.0));
    });
    listen ("tine_mod_reset", [&proc] (const juce::var&) { proc.resetTineMods(); });

    native ("getStringMods", [&proc] (const juce::var&) { return flatten (proc.getStringMods()); });
    listen ("string_mod", [&proc] (const juce::var& p)
    {
        proc.setStringMod (static_cast<int> (p.getProperty ("index", -1)),
                           f (p, "len", 1.0), f (p, "dia", 1.0));
    });
    listen ("string_mod_reset", [&proc] (const juce::var&) { proc.resetStringMods(); });

    native ("getGrandMods", [&proc] (const juce::var&) { return flatten (proc.getGrandMods()); });
    listen ("grand_mod", [&proc] (const juce::var& p)
    {
        proc.setGrandMod (static_cast<int> (p.getProperty ("index", -1)),
                          f (p, "len", 1.0), f (p, "dia", 1.0));
    });
    listen ("grand_mod_reset", [&proc] (const juce::var&) { proc.resetGrandMods(); });

    native ("getPickupMods", [&proc] (const juce::var&) { return flatten (proc.getPickupMods()); });
    listen ("pickup_mod", [&proc] (const juce::var& p)
    {
        proc.setPickupMod (static_cast<int> (p.getProperty ("index", -1)),
                           f (p, "h", 0.0), f (p, "g", 0.0), f (p, "s", 1.0));
    });
    listen ("pickup_mod_reset", [&proc] (const juce::var&) { proc.resetPickupMods(); });

    native ("getCabMods", [&proc] (const juce::var&) { return flattenOne (proc.getCabMods()); });
    listen ("cab_mod", [&proc] (const juce::var& p)
    {
        proc.setCabMod ({ f (p, "box", 0.74), f (p, "cone", 0.59), f (p, "dist", 0.5),
                          f (p, "angle", 0.25), f (p, "susp", 0.5) });
    });
    listen ("cab_mod_reset", [&proc] (const juce::var&) { proc.resetCabMods(); });

    native ("getMicMods", [&proc] (const juce::var&) { return flattenOne (proc.getMicMods()); });
    listen ("mic_mod", [&proc] (const juce::var& p)
    {
        proc.setMicMod ({ f (p, "spread", 1.0), f (p, "bias", 0.0), f (p, "dist", 0.0),
                          f (p, "lvlL", 1.0), f (p, "lvlR", 1.0) });
    });
    listen ("mic_mod_reset", [&proc] (const juce::var&) { proc.resetMicMods(); });

    native ("getMicStage", [&proc] (const juce::var&) { return flattenOne (proc.getMicStage()); });
    listen ("mic_stage", [&proc] (const juce::var& p)
    {
        if (auto* arr = p.getProperty ("v", juce::var()).getArray();
            arr != nullptr && arr->size() == 31)
        {
            std::array<float, 31> v {};
            for (int i = 0; i < 31; ++i)
                v[static_cast<size_t> (i)] = static_cast<float> (static_cast<double> ((*arr)[i]));
            proc.setMicStage (v);
        }
    });

    native ("getVelMap", [&proc] (const juce::var&) { return flattenOne (proc.getVelMap()); });
    listen ("vel_map", [&proc] (const juce::var& p)
    {
        proc.setVelMap ({ f (p, "y0", 0.0), f (p, "y1", 0.25), f (p, "y2", 0.5),
                          f (p, "y3", 0.75), f (p, "y4", 1.0) });
    });
    listen ("vel_map_reset", [&proc] (const juce::var&) { proc.resetVelMap(); });

    // ---- the on-screen keyboard ------------------------------------------
    listen ("ui_note", [&proc] (const juce::var& p)
    {
        proc.pushUiNote (static_cast<int> (p.getProperty ("note", -1)),
                         f (p, "velocity", 0.75),
                         static_cast<bool> (p.getProperty ("on", false)));
    });

    return b;
}

juce::var buildLevels (EpiAudioProcessor& proc)
{
    auto toDb = [] (float lin) -> float
    {
        if (lin <= 1.0e-5f) return -90.0f;
        return 20.0f * std::log10 (lin);
    };

    auto& engine = proc.getEngine();

    juce::Array<juce::var> outArr;
    for (int i = 0; i < 2; ++i)
        outArr.add (juce::var (toDb (engine.consumeOutPeak (i))));

    // The field the tine is actually moving through, straight from the model.
    // The drawing is the computed magnetic profile, not an illustration of one,
    // so moving the pickup height on screen moves the tine along the real curve.
    juce::Array<juce::var> profile;
    for (int i = 0; i < epi::EpiEngine::kFieldPoints; ++i)
        profile.add (juce::var (engine.vizField (i)));

    // The tine's own motion, decimated to about four cycles. Without this the
    // interface can only invent a wobble: at sixty telemetry ticks a second it
    // cannot otherwise represent an eighty-hertz waveform, let alone a treble
    // note.
    juce::Array<juce::var> trace;
    for (int i = 0; i < epi::EpiEngine::kTraceLen; ++i)
        trace.add (juce::var (engine.vizTrace (i)));

    // The whole harp: every tine's peak motion this frame, in microns, so the
    // interface can draw the instrument instead of one part of it.
    juce::Array<juce::var> harp;
    for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
        harp.add (juce::var (engine.vizTineTip (i) * 1.0e6f));

    juce::Array<juce::var> keys;
    for (int w = 0; w < epi::EpiEngine::kKeyWords; ++w)
        keys.add (juce::var (static_cast<double> (engine.vizKeys (w))));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("out",      juce::var (outArr));
    root->setProperty ("trace",    juce::var (trace));
    root->setProperty ("noteHz",   engine.vizNoteHz());
    root->setProperty ("strikes",  engine.vizStrikes());
    root->setProperty ("field",    juce::var (profile));
    root->setProperty ("tip",      engine.vizTipDisplacement());
    root->setProperty ("flux",     engine.vizFlux());
    root->setProperty ("offset",   engine.vizPickupOffset());
    root->setProperty ("vibL",     engine.vizVibratoL());
    root->setProperty ("vibR",     engine.vizVibratoR());
    root->setProperty ("voices",   engine.activeVoices());
    root->setProperty ("harp",     juce::var (harp));
    root->setProperty ("keys",     juce::var (keys));
    root->setProperty ("pedal",    engine.vizPedal());
    root->setProperty ("lastNote", engine.vizLastNote());
    root->setProperty ("loNote",   epi::EpiEngine::kLoNote);
    return juce::var (root.get());
}

juce::var buildPresetInfo (EpiAudioProcessor& proc)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("name",  proc.getPresetManager().getCurrentName());
    obj->setProperty ("dirty", proc.isCurrentPresetDirty());
    return juce::var (obj.get());
}

} // namespace epi::ui
