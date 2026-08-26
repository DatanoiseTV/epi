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

#include "EpiAnalysis.h"
#include "epi/PluginProcessor.h"
#include "epi/ui/BoundParameterIds.h"

#include <EpiUIData.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

// Every processor in this suite is heap-allocated, which is how a host
// creates one and, more to the point, the only way this suite survives a
// small stack: an EpiAudioProcessor carries the whole instrument, and
// Windows gives a main thread about a megabyte where macOS and Linux give
// eight. Built on the stack it fitted on two platforms and blew the third,
// with a segfault that named nothing. Reproduced on macOS under
// `ulimit -s 1024`, which is the honest way to test that claim.
namespace
{
int failures = 0;

// Flushed on every line, deliberately. stdout is block-buffered when a CI
// runner redirects it, so a crash discards everything the suite has already
// printed -- which is how this suite first failed on Windows with 0.22
// seconds of runtime and not one line of output to say where. A test that
// loses its own diagnostics at the moment they matter is not much of a test.
void say (const char* text)
{
    std::fputs (text, stdout);
    std::fflush (stdout);
}

void row (const char* id, const char* what, bool pass, const char* detail = "")
{
    char line[256];
    std::snprintf (line, sizeof line, "%-4s %-58s %s %s\n",
                   id, what, pass ? "PASS" : "FAIL", detail);
    say (line);
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

    say ("Epi state round-trip suite: starting\n");
    {
        // Construction is announced on its own, because that is where a
        // platform-specific failure lands and the rows after it say nothing
        // if nobody knows whether it got that far.
        auto probeOwned = std::make_unique<EpiAudioProcessor>();
        auto& probe = *probeOwned;
        say ("processor constructed\n");
        (void) probe.getValueTreeState().getParameter ("tune");
        say ("parameters reachable\n\n");
    }

    // S1 -- a fresh processor sits on the calibrated defaults.
    {
        auto aOwned = std::make_unique<EpiAudioProcessor>();
        auto& a = *aOwned;
        row ("S1", "fresh processor: stage at defaults, classic pair mode",
             near (a.getMicStage(), EpiAudioProcessor::kStageDefaults)
                 && a.getMicStage()[0] < 0.5f);
    }

    // S2 -- host project state: getState/setState across two processors.
    {
        auto aOwned = std::make_unique<EpiAudioProcessor>();
        auto& a = *aOwned;
        a.setMicStage (kProbeStage);
        a.setMicMod (kProbePair);
        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        auto bOwned = std::make_unique<EpiAudioProcessor>();

        auto& b = *bOwned;
        b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        row ("S2", "project state: mic stage and pair trims survive the trip",
             near (b.getMicStage(), kProbeStage) && near (b.getMicMods(), kProbePair));
    }

    // S3 -- a saved user preset is a complete snapshot: save, scribble over
    // the live stage, load the preset back, and the saved placement wins.
    {
        auto aOwned = std::make_unique<EpiAudioProcessor>();
        auto& a = *aOwned;
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
        auto aOwned = std::make_unique<EpiAudioProcessor>();
        auto& a = *aOwned;
        a.setMicStage (kProbeStage);
        a.getPresetManager().loadFactory (0);
        row ("S4", "factory preset load: the player's stage placement stays",
             near (a.getMicStage(), kProbeStage));
    }

    // S5 -- legacy state: a project saved before the stage existed carries
    // no 'mstage' entry, and loading it must mean 'stock stage', never
    // 'whatever happened to be set'.
    {
        auto pristineOwned = std::make_unique<EpiAudioProcessor>();
        auto& pristine = *pristineOwned;
        juce::MemoryBlock legacy;
        pristine.getStateInformation (legacy);

        auto bOwned = std::make_unique<EpiAudioProcessor>();

        auto& b = *bOwned;
        b.setMicStage (kProbeStage);
        b.setStateInformation (legacy.getData(), static_cast<int> (legacy.getSize()));
        row ("S5", "legacy state without a stage entry resets to defaults",
             near (b.getMicStage(), EpiAudioProcessor::kStageDefaults));
    }

    // S6 -- a factory preset that ships a stage placement applies it, and
    // the next plain factory load still leaves it alone (the bench rule).
    {
        auto aOwned = std::make_unique<EpiAudioProcessor>();
        auto& a = *aOwned;
        a.getPresetManager().loadByName ("Jazz Club");
        const auto st = a.getMicStage();   // copy: the kept-check below must not compare the live array with itself
        const bool applied = st[0] >= 0.5f            // Stage mode
                          && st[1] >= 0.5f            // mic 1 on
                          && std::abs (st[3] - 0.6f) < 1.0e-4f;   // z = 0.6
        a.getPresetManager().loadFactory (0);
        const bool kept = near (a.getMicStage(), st);
        row ("S6", "stage-carrying factory preset applies; plain load keeps it",
             applied && kept);
    }

    // S7 -- every parameter the editor binds must be declared in the UI
    // bundle, and the two kinds are declared differently. A continuous
    // parameter needs an entry in the PARAMS table, because the knob reads
    // its formatter: without one the panel throws "undefined is not an
    // object (evaluating 'spec.format')" the moment it renders -- a runtime
    // error in the plugin window, invisible to every suite that never loads
    // the page, and exactly what shipped when the wear knob reached the
    // panel but not the table. A choice parameter takes its value from the
    // combo relay instead, so what it needs is an entry in the browser
    // mock's list, or the page is dead outside the plugin (which is where
    // the UI is verified headlessly). The bundle is compiled in, so this
    // reads the resource the plugin actually serves.
    {
        auto aOwned = std::make_unique<EpiAudioProcessor>();
        auto& a = *aOwned;
        const juce::String js (juce::CharPointer_UTF8 (EpiUIData::jucebridge_jsx),
                               (size_t) EpiUIData::jucebridge_jsxSize);
        juce::StringArray missing;
        for (const auto& id : epi::ui::boundParameterIds())
        {
            const auto* p = a.getValueTreeState().getParameter (id);
            const bool isChoice = dynamic_cast<const juce::AudioParameterChoice*> (p) != nullptr;
            const bool declared = isChoice ? js.contains ("'" + id + "', ")
                                           : js.contains ("    " + id + ":");
            if (! declared) missing.add (id + (isChoice ? " (mock list)" : " (PARAMS)"));
        }
        row ("S7", "every bound parameter id is declared in the UI bundle",
             EpiUIData::jucebridge_jsxSize > 0 && missing.isEmpty(),
             missing.joinIntoString (", ").toRawUTF8());
    }

    // S8 -- every parameter, not just the benches. Each one is moved off
    // its default to a value inside its own range (a quarter of the way up
    // for continuous, the next index for choices, so no two are the same
    // and none sits at a boundary), the whole state is written and read
    // back into a second processor, and every value has to match. This is
    // the check that catches a parameter added to the layout but forgotten
    // in whatever the host actually persists.
    {
        auto aOwned = std::make_unique<EpiAudioProcessor>();
        auto& a = *aOwned;
        auto& sa = a.getValueTreeState();
        std::vector<std::pair<juce::String, float>> set;
        for (auto* p : a.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr) continue;
            const auto& r = rp->getNormalisableRange();
            float v;
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
                v = static_cast<float> ((c->getIndex() + 1) % c->choices.size());
            else
                v = r.convertFrom0to1 (0.27f);
            if (auto* fp = sa.getParameter (rp->getParameterID()))
                fp->setValueNotifyingHost (r.convertTo0to1 (v));
            // Record what the parameter ACTUALLY took, not what was asked
            // for: a stepped one (the tone rockers, the dB knobs) snaps to
            // its own grid, and a round-trip test that ignores that is
            // testing the grid rather than the persistence.
            set.emplace_back (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));
        }

        juce::MemoryBlock blob;
        a.getStateInformation (blob);
        auto bOwned = std::make_unique<EpiAudioProcessor>();
        auto& b = *bOwned;
        b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

        juce::StringArray bad;
        for (const auto& [id, want] : set)
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (b.getValueTreeState().getParameter (id));
            if (rp == nullptr) { bad.add (id + " (missing)"); continue; }
            const float got = rp->convertFrom0to1 (rp->getValue());
            const float tol = std::max (1.0e-4f, 1.0e-3f * std::abs (want));
            if (std::abs (got - want) > tol)
                bad.add (id + " (" + juce::String (want, 4) + " -> " + juce::String (got, 4) + ")");
        }
        row ("S8", ("all " + juce::String (set.size()) + " parameters round-trip").toRawUTF8(),
             ! set.empty() && bad.isEmpty(), bad.joinIntoString (", ").toRawUTF8());
    }

    // S9 -- the MPE switch. It lives on the bench tree rather than the
    // parameter tree (it is a setup switch, not something a player rides),
    // so S8 cannot see it and it needs the bench treatment: default on a
    // fresh processor, a round trip through project state, and a state
    // written before it existed meaning Detect rather than whatever the
    // receiving processor happened to be set to.
    {
        auto freshOwned = std::make_unique<EpiAudioProcessor>();
        auto& fresh = *freshOwned;
        row ("S9a", "fresh processor: MPE on Detect",
             fresh.getMpeMode() == EpiAudioProcessor::kMpeModeDefault);

        auto aOwned = std::make_unique<EpiAudioProcessor>();

        auto& a = *aOwned;
        a.setMpeMode (2);                       // forced on
        juce::MemoryBlock blob;
        a.getStateInformation (blob);
        auto bOwned = std::make_unique<EpiAudioProcessor>();
        auto& b = *bOwned;
        b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        row ("S9b", "project state: MPE mode survives the trip",
             b.getMpeMode() == 2);

        auto pristineOwned = std::make_unique<EpiAudioProcessor>();

        auto& pristine = *pristineOwned;
        juce::MemoryBlock legacy;
        pristine.getStateInformation (legacy);
        auto cOwned = std::make_unique<EpiAudioProcessor>();
        auto& c = *cOwned;
        c.setMpeMode (0);                       // forced off, then handed an old project
        c.setStateInformation (legacy.getData(), static_cast<int> (legacy.getSize()));
        row ("S9c", "legacy state without an MPE entry means Detect",
             c.getMpeMode() == EpiAudioProcessor::kMpeModeDefault);
    }

    // S10 -- what the whole campaign turns on, at the plugin's own boundary:
    // a plain-MIDI block must build exactly the note events it always did.
    // A note-on carries a per-note tuning offset now, and on anything that is
    // not an open MPE zone that offset has to be zero -- not small, zero.
    {
        auto aOwned = std::make_unique<EpiAudioProcessor>();
        auto& a = *aOwned;
        a.prepareToPlay (48000.0, 64);
        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        // The traffic a plain-MIDI host sends, including a wheel: on no zone
        // that is the global bend, exactly as before.
        midi.addEvent (juce::MidiMessage::pitchWheel (1, 12000), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.7f), 1);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 2);
        a.processBlock (buf, midi);
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 64; ++i)
                finite = finite && std::isfinite (buf.getSample (ch, i));
        row ("S10", "plain-MIDI block renders, no zone opened", finite);
    }

    // S11 -- the whole per-note tuning path through the artifact, with real
    // MIDI. The offline suite drives the tuning register directly; this row
    // is the only place the JUCE parsing in between is exercised, so it sends
    // the bytes a host sends -- an MPE Configuration Message, a member-channel
    // wheel, a note on that member channel -- and reads the pitch back out of
    // the rendered audio rather than trusting the parameter.
    {
        constexpr double fs = 48000.0;
        constexpr int block = 512;
        constexpr int seconds = 2;
        constexpr int total = static_cast<int> (fs) * seconds;
        constexpr int note = 60;
        // A whole quarter tone, well clear of any measurement slack, at the
        // MPE member default of +/-48 semitones.
        constexpr double wantCents = 50.0;
        const int wheel = 8192 + juce::roundToInt (wantCents / (48.0 * 100.0) * 8192.0);
        const double asked = (wheel - 8192) / 8192.0 * 48.0 * 100.0;

        auto renderMidi = [&] (bool mpe)
        {
            auto aOwned = std::make_unique<EpiAudioProcessor>();
            auto& a = *aOwned;
            a.prepareToPlay (fs, block);
            juce::AudioBuffer<float> buf (2, block);
            std::vector<double> mono;
            mono.reserve (static_cast<std::size_t> (total));
            for (int i = 0; i < total; i += block)
            {
                juce::MidiBuffer midi;
                if (i == 0)
                {
                    if (mpe)
                    {
                        // RPN 6 on channel 1: the lower zone, 15 members.
                        midi.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), 0);
                        midi.addEvent (juce::MidiMessage::controllerEvent (1, 100, 6), 0);
                        midi.addEvent (juce::MidiMessage::controllerEvent (1, 6, 15), 0);
                        midi.addEvent (juce::MidiMessage::pitchWheel (2, wheel), 1);
                    }
                    midi.addEvent (juce::MidiMessage::noteOn (2, note, 0.7f), 2);
                }
                buf.clear();
                a.processBlock (buf, midi);
                for (int n = 0; n < block && i + n < total; ++n)
                    mono.push_back (0.5 * (static_cast<double> (buf.getSample (0, n))
                                         + static_cast<double> (buf.getSample (1, n))));
            }
            return mono;
        };

        const double nominal = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const double plain = epianalysis::refineF0 (renderMidi (false), fs, nominal, 0.3, 1.6);
        const double tuned = epianalysis::refineF0 (renderMidi (true),  fs, nominal, 0.3, 1.6);
        const double got = 1200.0 * std::log2 (tuned / plain);
        char detail[96];
        std::snprintf (detail, sizeof detail, "(asked %+.1f ct, measured %+.2f ct)", asked, got);
        row ("S11", "MPE stream through the artifact retunes the note",
             std::abs (got - asked) < 1.0, detail);
    }

    char tail[64];
    std::snprintf (tail, sizeof tail, "\nSUMMARY fail=%d\n", failures);
    say (tail);
    return failures;
}
