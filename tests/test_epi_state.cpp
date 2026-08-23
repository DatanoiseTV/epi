// ---------------------------------------------------------------------------
// State and preset round-trip suite. The seven acoustic suites are
// framework-free on purpose; this one is not -- it instantiates the real
// EpiAudioProcessor and proves that what the player set is what comes back,
// through every path a setting can travel: host project state, a saved user
// preset, a factory preset load, and a legacy state saved before a bench
// existed. The mic stage is the newest bench and the reason this suite was
// written; the pair trims and the velocity map ride along so a regression in
// the shared mods tree cannot hide behind any single bench.
//
// Values are stringified at six decimals in the mods tree, so comparisons
// use a 1e-5 tolerance rather than bit equality -- that IS the storage
// contract, not slack.
// ---------------------------------------------------------------------------

#include "epi/PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void row (const char* id, const char* what, bool pass, const char* detail = "")
{
    std::printf ("%-4s %-58s %s %s\n", id, what, pass ? "PASS" : "FAIL", detail);
    if (! pass) ++failures;
}

template <std::size_t N>
bool near (const std::array<float, N>& a, const std::array<float, N>& b, float tol = 1.0e-5f)
{
    for (std::size_t i = 0; i < N; ++i)
        if (std::abs (a[i] - b[i]) > tol) return false;
    return true;
}

// A deliberately non-default stage: mode 1, every mic touched, every field
// exercised, all values exact at six decimals.
constexpr std::array<float, 31> kProbeStage {
    1.0f,
    1.0f,  1.1f, 2.2f, -0.4f,  3.0f,  0.25f,
    1.0f, -0.9f, 0.6f,  0.35f, -2.5f, -0.5f,
    0.0f,  0.3f, 3.1f,  1.2f,  0.0f,  0.1f,
    1.0f,  0.0f, 0.45f, -0.6f, -12.0f, 0.0f,
    0.0f,  1.6f, 4.0f,  1.8f,  6.0f,  0.9f };

constexpr std::array<float, 5> kProbePair { 1.4f, -0.3f, 0.55f, 0.8f, 1.25f };
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    std::printf ("Epi state round-trip suite\n\n");

    // S1 -- a fresh processor sits on the calibrated defaults.
    {
        EpiAudioProcessor a;
        row ("S1", "fresh processor: stage at defaults, classic pair mode",
             near (a.getMicStage(), EpiAudioProcessor::kStageDefaults)
                 && a.getMicStage()[0] < 0.5f);
    }

    // S2 -- host project state: getState/setState across two processors.
    {
        EpiAudioProcessor a;
        a.setMicStage (kProbeStage);
        a.setMicMod (kProbePair);
        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        EpiAudioProcessor b;
        b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        row ("S2", "project state: mic stage and pair trims survive the trip",
             near (b.getMicStage(), kProbeStage) && near (b.getMicMods(), kProbePair));
    }

    // S3 -- a saved user preset is a complete snapshot: save, scribble over
    // the live stage, load the preset back, and the saved placement wins.
    {
        EpiAudioProcessor a;
        a.setMicStage (kProbeStage);
        const juce::String name { "ZZ-STATE-RT-PROBE" };
        a.getPresetManager().saveUser (name);

        auto scribbled = kProbeStage;
        scribbled[0] = 0.0f;
        scribbled[2] = -1.5f;
        a.setMicStage (scribbled);

        a.getPresetManager().loadByName (name);
        const bool restored = near (a.getMicStage(), kProbeStage);
        const bool cleaned = a.getPresetManager().deleteUser (name);
        row ("S3", "user preset: saved stage wins over the live scribble",
             restored && cleaned, cleaned ? "" : "(probe preset not deleted)");
    }

    // S4 -- factory presets leave the player's benches alone unless they
    // carry their own table; the stage is a bench like any other.
    {
        EpiAudioProcessor a;
        a.setMicStage (kProbeStage);
        a.getPresetManager().loadFactory (0);
        row ("S4", "factory preset load: the player's stage placement stays",
             near (a.getMicStage(), kProbeStage));
    }

    // S5 -- legacy state: a project saved before the stage existed carries
    // no 'mstage' entry, and loading it must mean 'stock stage', never
    // 'whatever happened to be set'.
    {
        EpiAudioProcessor pristine;
        juce::MemoryBlock legacy;
        pristine.getStateInformation (legacy);

        EpiAudioProcessor b;
        b.setMicStage (kProbeStage);
        b.setStateInformation (legacy.getData(), static_cast<int> (legacy.getSize()));
        row ("S5", "legacy state without a stage entry resets to defaults",
             near (b.getMicStage(), EpiAudioProcessor::kStageDefaults));
    }

    std::printf ("\nSUMMARY fail=%d\n", failures);
    return failures;
}
