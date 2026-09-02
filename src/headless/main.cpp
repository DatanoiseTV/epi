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
// Epi as an appliance.
//
// The same instrument the plugin runs, hosted by a small program that opens an
// audio device and a MIDI port itself, with no plugin host and no window. What
// it is for: a small computer bolted inside a keyboard, a rack box, a Pi on a
// stage. Two ways in --
//
//   the interface   the plugin's own ui/epi bundle, served over HTTP and
//                   reached from a phone or a laptop on the same network.
//                   Byte-identical to the plugin's; only the file that talks
//                   to the host is swapped, so there is no second interface
//                   to keep in step.
//
//   MIDI            every parameter as CC and as NRPN, both directions. That
//                   is what makes a physical panel possible: encoders and
//                   displays that both send and are told, so a preset load
//                   moves the hardware rather than leaving it lying. See
//                   docs/ControlMap.md, which this program can regenerate.
//
// It hosts EpiAudioProcessor unchanged. Presets, benches, state and all
// forty-nine parameters are the plugin's, not a reimplementation, so anything
// saved here loads there.
// ---------------------------------------------------------------------------

#include "epi/PluginProcessor.h"
#include "epi/ParameterIDs.h"
#include "epi/ControlMap.h"
#include "epi/MidiControlSurface.h"
#include "epi/ui/BoundParameterIds.h"
#include "epi/ui/UiBridge.h"
#include "headless/WebServer.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <EpiHeadlessUIData.h>
#include <EpiUIData.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{

// Ctrl-C, and the TERM a service manager sends on stop. Both have to bring the
// audio device down in order rather than leaving the process to be killed with
// a stream open.
std::atomic<bool> shouldQuit { false };
extern "C" void onSignal (int) { shouldQuit.store (true); }

constexpr int kTelemetryHz = 30;

// How many parameters may be restated to the hardware in one telemetry tick.
// A preset load moves all forty-nine at once, which as NRPN is nearly two
// hundred messages; a DIN port carries about a thousand a second, so sending
// them in one go would block the port for a fifth of a second and delay the
// notes behind them. Eight per tick at thirty ticks refreshes the whole panel
// in under a fifth of a second without ever occupying the port.
constexpr int kFeedbackBudget = 8;

// ---------------------------------------------------------------------------
// Serving the interface out of the two binary bundles.
// ---------------------------------------------------------------------------
struct Bundle
{
    int listSize;
    const char* const* list;
    const char* (*get) (const char*, int&);
    const char* (*name) (const char*);
};

class Assets
{
public:
    Assets()
    {
        add ({ EpiUIData::namedResourceListSize, EpiUIData::namedResourceList,
               EpiUIData::getNamedResource, EpiUIData::getNamedResourceOriginalFilename });
        add ({ EpiHeadlessUIData::namedResourceListSize, EpiHeadlessUIData::namedResourceList,
               EpiHeadlessUIData::getNamedResource, EpiHeadlessUIData::getNamedResourceOriginalFilename });
    }

    // The URL's last path segment, which is how the plugin's resource provider
    // resolves too -- the bundle is flat.
    static juce::String basename (const juce::String& url)
    {
        juce::String n = url.startsWithChar ('/') ? url.substring (1) : url;
        if (n.isEmpty()) n = "index.html";
        const auto slash = n.lastIndexOfChar ('/');
        if (slash >= 0) n = n.substring (slash + 1);
        const auto q = n.indexOfChar ('?');
        if (q >= 0) n = n.substring (0, q);
        return n;
    }

    static juce::String mimeFor (const juce::String& name)
    {
        if (name.endsWith (".html"))  return "text/html";
        if (name.endsWith (".css"))   return "text/css";
        if (name.endsWith (".js") || name.endsWith (".jsx") || name.endsWith (".mjs"))
                                      return "application/javascript";
        if (name.endsWith (".svg"))   return "image/svg+xml";
        if (name.endsWith (".png"))   return "image/png";
        if (name.endsWith (".json"))  return "application/json";
        if (name.endsWith (".woff2")) return "font/woff2";
        return "application/octet-stream";
    }

    bool find (const juce::String& name, const char*& data, int& size) const
    {
        for (const auto& e : entries)
            if (e.filename == name) { data = e.data; size = e.size; return true; }
        return false;
    }

private:
    void add (Bundle b)
    {
        if (b.list == nullptr || b.get == nullptr || b.name == nullptr) return;
        for (int i = 0; i < b.listSize; ++i)
        {
            int size = 0;
            const char* data = b.get (b.list[i], size);
            if (data != nullptr)
                entries.push_back ({ juce::String (b.name (b.list[i])), data, size });
        }
    }

    struct Entry { juce::String filename; const char* data; int size; };
    std::vector<Entry> entries;
};

// ---------------------------------------------------------------------------
// The host.
// ---------------------------------------------------------------------------
class HeadlessHost final : public juce::AudioIODeviceCallback,
                           public juce::MidiInputCallback,
                           private juce::Timer
{
public:
    HeadlessHost() { buildParameterTable(); }

    ~HeadlessHost() override { stopTimer(); }

    EpiAudioProcessor& processor() { return proc; }

    // ---- audio ----------------------------------------------------------
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const double fs = device->getCurrentSampleRate();
        const int block = device->getCurrentBufferSizeSamples();
        collector.reset (fs);
        proc.setPlayConfigDetails (0, 2, fs, block);
        proc.prepareToPlay (fs, block);
        scratch.setSize (2, block);
    }

    void audioDeviceStopped() override { proc.releaseResources(); }

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        if (scratch.getNumSamples() < numSamples)
            scratch.setSize (2, numSamples, false, false, true);

        juce::AudioBuffer<float> buffer (scratch.getArrayOfWritePointers(), 2, numSamples);
        buffer.clear();

        midi.clear();
        collector.removeNextBlockOfMessages (midi, numSamples);
        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
                juce::FloatVectorOperations::copy (out[ch],
                                                   buffer.getReadPointer (juce::jmin (ch, 1)),
                                                   numSamples);
    }

    // ---- MIDI in --------------------------------------------------------
    // Controllers are filtered HERE rather than in the audio callback, so a
    // parameter edit never happens on the audio thread. A controller that the
    // map claims is consumed and goes no further; everything else -- notes,
    // the pedals, the wheel, the registered-parameter traffic the tuner reads
    // -- passes through untouched.
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isController())
        {
            const bool consumed = surface.controller (
                m.getChannel(), m.getControllerNumber(), m.getControllerValue(),
                [this] (int control, float norm)
                {
                    if (auto* p = controlParams[(size_t) control])
                        p->setValueNotifyingHost (norm);
                });
            if (consumed) return;
        }
        collector.addMessageToQueue (m);
    }

    // ---- wiring ---------------------------------------------------------
    void attach (epi::WebServer* s, juce::MidiOutput* out, bool ccFeedback)
    {
        server = s;
        midiOut = out;
        sendCc = ccFeedback;
        surface.resendAll();
        startTimerHz (kTelemetryHz);
    }

    // What the browser is primed with before its first render.
    juce::var initialState()
    {
        juce::DynamicObject::Ptr sliders = new juce::DynamicObject();
        juce::DynamicObject::Ptr combos  = new juce::DynamicObject();
        for (const auto& b : bound)
        {
            if (b.choice) combos->setProperty (b.id, choiceIndex (b));
            else          sliders->setProperty (b.id, (double) b.param->getValue());
        }
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty ("sliders", juce::var (sliders.get()));
        root->setProperty ("combos",  juce::var (combos.get()));
        root->setProperty ("toggles", juce::var (new juce::DynamicObject()));
        return juce::var (root.get());
    }

    // A page that has just connected -- or reconnected after a dropped link,
    // which the interface does by itself -- must be told everything. Its
    // cached values are either absent or, after a reconnect, out of date.
    void resendToBrowser()
    {
        for (auto& b : bound) b.lastSentToBrowser = -1.0;
    }

    void setParamFromBrowser (const juce::String& kind, const juce::String& id, double value)
    {
        for (const auto& b : bound)
        {
            if (b.id != id) continue;
            if (kind == "combo" || b.choice)
                b.param->setValueNotifyingHost (b.param->convertTo0to1 ((float) value));
            else
                b.param->setValueNotifyingHost ((float) value);
            return;
        }
    }

private:
    struct Bound
    {
        juce::String id;
        juce::RangedAudioParameter* param = nullptr;
        bool choice = false;
        double lastSentToBrowser = -1.0;
    };

    static int choiceIndex (const Bound& b)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (b.param)) return c->getIndex();
        return (int) std::lround (b.param->convertFrom0to1 (b.param->getValue()));
    }

    void buildParameterTable()
    {
        auto& apvts = proc.getValueTreeState();
        auto push = [&] (const char* id, bool isChoice)
        {
            if (auto* p = apvts.getParameter (id))
                bound.push_back ({ juce::String (id), p, isChoice, -1.0 });
            else
                std::fprintf (stderr, "Epi: parameter \"%s\" is bound by the interface but "
                                      "does not exist in the layout\n", id);
        };
        for (auto* id : epi::ui::kFloatIds)  push (id, false);
        for (auto* id : epi::ui::kChoiceIds) push (id, true);

        // The control map indexes into the same parameters, by id.
        for (int i = 0; i < epi::kNumControls; ++i)
        {
            controlParams[(size_t) i] = apvts.getParameter (epi::kControlMap[i].paramId);
            if (controlParams[(size_t) i] == nullptr)
                std::fprintf (stderr, "Epi: control map names \"%s\", which is not a parameter\n",
                              epi::kControlMap[i].paramId);
        }
    }

    void timerCallback() override
    {
        pushTelemetry();
        pushParameterEchoes();
        pushMidiFeedback();
    }

    void pushTelemetry()
    {
        if (server == nullptr) return;
        auto wrap = [] (const char* name, const juce::var& payload)
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("t", "e");
            o->setProperty ("name", name);
            o->setProperty ("payload", payload);
            return juce::JSON::toString (juce::var (o.get()), true);
        };
        server->broadcast (wrap ("levels", epi::ui::buildLevels (proc)));
        server->broadcast (wrap ("presetInfo", epi::ui::buildPresetInfo (proc)));
    }

    // A parameter the browser did NOT move -- a preset load, a controller, the
    // host -- has to reach it, or its knob shows the old value until touched.
    void pushParameterEchoes()
    {
        if (server == nullptr) return;
        for (auto& b : bound)
        {
            const double v = b.choice ? (double) choiceIndex (b) : (double) b.param->getValue();
            if (b.lastSentToBrowser == v) continue;
            b.lastSentToBrowser = v;

            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("t", "p");
            o->setProperty ("kind", b.choice ? "combo" : "slider");
            o->setProperty ("id", b.id);
            o->setProperty ("value", v);
            server->broadcast (juce::JSON::toString (juce::var (o.get()), true));
        }
    }

    void pushMidiFeedback()
    {
        if (midiOut == nullptr) return;
        surface.collectFeedback (
            [this] (int i)
            {
                auto* p = controlParams[(size_t) i];
                return p != nullptr ? p->getValue() : 0.0f;
            },
            [this] (int cc, int value)
            {
                midiOut->sendMessageNow (juce::MidiMessage::controllerEvent (feedbackChannel, cc, value));
            },
            true, sendCc, kFeedbackBudget);
    }

    EpiAudioProcessor proc;
    juce::MidiMessageCollector collector;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> scratch;

    epi::MidiControlSurface surface;
    std::array<juce::RangedAudioParameter*, epi::kNumControls> controlParams {};
    std::vector<Bound> bound;

    epi::WebServer* server = nullptr;
    juce::MidiOutput* midiOut = nullptr;
    bool sendCc = true;
    int feedbackChannel = 1;
};

// ---------------------------------------------------------------------------
// docs/ControlMap.md, generated from the map and the live parameter layout so
// the document cannot drift from what the instrument actually answers.
// ---------------------------------------------------------------------------
void dumpControlMap (EpiAudioProcessor& proc)
{
    auto& apvts = proc.getValueTreeState();

    // One table per panel rather than one of forty-nine rows: the map is read
    // while building a panel, and a panel is a group of controls.
    juce::String lastPanel;
    for (int i = 0; i < epi::kNumControls; ++i)
    {
        const auto& a = epi::kControlMap[i];

        if (juce::String (a.panel) != lastPanel)
        {
            lastPanel = a.panel;
            std::printf ("\n### %s\n\n", a.panel);
            std::printf ("| CC | NRPN | Parameter | Range | Default | Note |\n");
            std::printf ("| --: | --: | --- | --- | --- | --- |\n");
        }

        auto* p = apvts.getParameter (a.paramId);
        juce::String name = a.paramId, range, dflt;
        if (p != nullptr)
        {
            name = p->getName (64);
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
            {
                range = c->choices.joinIntoString (" / ");
                dflt  = c->choices[(int) std::lround (c->convertFrom0to1 (p->getDefaultValue()))];
            }
            else
            {
                const auto r = p->getNormalisableRange();
                range = juce::String (r.start, 2) + " .. " + juce::String (r.end, 2);
                dflt  = juce::String (p->convertFrom0to1 (p->getDefaultValue()), 2);
            }
        }

        std::printf ("| %s | %d | **%s** `%s` | %s | %s | %s |\n",
                     a.cc >= 0 ? juce::String (a.cc).toRawUTF8() : "—",
                     a.nrpn, name.toRawUTF8(), a.paramId,
                     range.toRawUTF8(), dflt.toRawUTF8(), a.note);
    }
    std::printf ("\n");
}

int usage()
{
    std::printf (
        "epi-headless — Epi with no plugin host and no window\n"
        "\n"
        "  --port <n>          HTTP port for the interface (default 8080)\n"
        "  --bind <addr>       address to listen on (default 0.0.0.0; 127.0.0.1\n"
        "                      to refuse everything but this machine)\n"
        "  --device <name>     audio output device (default: the system's)\n"
        "  --rate <hz>         sample rate (default: the device's)\n"
        "  --buffer <n>        buffer size in samples (default: the device's)\n"
        "  --midi-in <name>    MIDI input; repeatable. Default: every input.\n"
        "  --midi-out <name>   MIDI output for parameter feedback (default: none)\n"
        "  --no-cc-feedback    send only NRPN back, not CC\n"
        "  --no-audio          serve the interface without opening an audio\n"
        "                      device; silent, for checking a build or a port\n"
        "  --preset <name>     load a preset at startup\n"
        "  --list-devices      print audio and MIDI devices, then exit\n"
        "  --dump-control-map  print docs/ControlMap.md's table, then exit\n"
        "  --help              this\n");
    return 0;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));

    auto flag  = [&args] (const char* f) { return args.contains (f); };
    auto value = [&args] (const char* f, juce::String fallback) -> juce::String
    {
        const int i = args.indexOf (f);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : fallback;
    };
    auto values = [&args] (const char* f)
    {
        juce::StringArray out;
        for (int i = 0; i < args.size() - 1; ++i)
            if (args[i] == f) out.add (args[i + 1]);
        return out;
    };

    if (flag ("--help") || flag ("-h")) return usage();

    auto host = std::make_unique<HeadlessHost>();

    if (flag ("--dump-control-map")) { dumpControlMap (host->processor()); return 0; }

    juce::AudioDeviceManager devices;

    if (flag ("--list-devices"))
    {
        devices.initialiseWithDefaultDevices (0, 2);
        std::printf ("Audio output devices:\n");
        for (auto* type : devices.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            for (const auto& n : type->getDeviceNames (false))
                std::printf ("  [%s] %s\n", type->getTypeName().toRawUTF8(), n.toRawUTF8());
        }
        std::printf ("MIDI inputs:\n");
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            std::printf ("  %s\n", d.name.toRawUTF8());
        std::printf ("MIDI outputs:\n");
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            std::printf ("  %s\n", d.name.toRawUTF8());
        return 0;
    }

    // ---- audio ------------------------------------------------------------
    // Without a device the instrument still has to be prepared, or the
    // telemetry the interface draws from would be read out of an engine that
    // was never given a sample rate.
    const bool noAudio = flag ("--no-audio");
    if (noAudio)
    {
        host->processor().setPlayConfigDetails (0, 2, 48000.0, 512);
        host->processor().prepareToPlay (48000.0, 512);
        std::printf ("Epi: no audio device (--no-audio); the interface is served but silent\n");
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = value ("--device", {});
    setup.sampleRate       = value ("--rate", "0").getDoubleValue();
    setup.bufferSize       = value ("--buffer", "0").getIntValue();
    setup.inputChannels.clear();
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = true;

    if (! noAudio)
        if (const auto err = devices.initialise (0, 2, nullptr, true, setup.outputDeviceName, &setup);
            err.isNotEmpty())
        {
            std::fprintf (stderr, "Epi: no audio device — %s\n", err.toRawUTF8());
            return 1;
        }

    // ---- MIDI -------------------------------------------------------------
    const auto wantedIn = values ("--midi-in");
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
    {
        if (wantedIn.size() > 0 && ! wantedIn.contains (d.name)) continue;
        if (auto in = juce::MidiInput::openDevice (d.identifier, host.get()))
        {
            in->start();
            std::printf ("Epi: MIDI in  %s\n", d.name.toRawUTF8());
            inputs.push_back (std::move (in));
        }
    }

    std::unique_ptr<juce::MidiOutput> midiOut;
    if (const auto wantedOut = value ("--midi-out", {}); wantedOut.isNotEmpty())
    {
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name == wantedOut)
                midiOut = juce::MidiOutput::openDevice (d.identifier);
        if (midiOut == nullptr)
            std::fprintf (stderr, "Epi: no MIDI output named \"%s\"\n", wantedOut.toRawUTF8());
        else
            std::printf ("Epi: MIDI out %s (parameter feedback)\n", wantedOut.toRawUTF8());
    }

    if (const auto preset = value ("--preset", {}); preset.isNotEmpty())
        host->processor().getPresetManager().loadByName (preset);

    // ---- the interface ----------------------------------------------------
    Assets assets;
    const auto bridge = epi::ui::makeBridge (host->processor());

    epi::WebServer::Handlers handlers;

    handlers.asset = [&assets, &host] (const juce::String& path, juce::MemoryBlock& out,
                                       juce::String& mime)
    {
        const auto name = Assets::basename (path);

        // The one substitution. The page asks for JUCE's frontend library;
        // it gets the headless one instead, with the current parameter values
        // written above it -- the interface reads a slider synchronously
        // during its first render, so there is no opportunity to fetch them.
        if (name == "juce-framework-frontend.js")
        {
            const char* js = nullptr;
            int size = 0;
            if (! assets.find ("headless-frontend.js", js, size)) return false;

            // CharPointer_UTF8, not a bare literal: juce::String decodes a
            // const char* as Latin-1, so the separator would be re-encoded
            // and the header would read "v0.9.0 A. main".
            const juce::String prelude =
                juce::String (juce::CharPointer_UTF8 (
                    "window.EPI_VERSION_STR = 'v" EPI_VERSION " \xc2\xb7 " EPI_GIT_BRANCH "';\n"
                    "window.__EPI_INIT__ = "))
                + juce::JSON::toString (host->initialState(), true) + ";\n";

            out.reset();
            out.append (prelude.toRawUTF8(), prelude.getNumBytesAsUTF8());
            out.append (js, (size_t) size);
            mime = "application/javascript";
            return true;
        }

        const char* data = nullptr;
        int size = 0;
        if (! assets.find (name, data, size)) return false;
        out.replaceAll (data, (size_t) size);
        mime = Assets::mimeFor (name);
        return true;
    };

    handlers.setParam = [&host] (const juce::String& kind, const juce::String& id, double v)
    {
        juce::MessageManager::callAsync ([h = host.get(), kind, id, v]
                                         { h->setParamFromBrowser (kind, id, v); });
    };

    handlers.emit = [&bridge] (const juce::String& name, const juce::var& payload)
    {
        if (const auto* fn = bridge.findListener (name))
            juce::MessageManager::callAsync ([f = *fn, payload] { f (payload); });
    };

    handlers.native = [&bridge] (const juce::String& name, const juce::var& args) -> juce::var
    {
        // reloadUI belongs to the plugin's WebView; here the browser has its
        // own reload and does not need to ask us for one.
        if (name == "reloadUI") return {};
        const auto* fn = bridge.findNative (name);
        if (fn == nullptr) return {};

        // Onto the message thread and back, so a workshop read sees the same
        // state the interface's edits are applied to.
        juce::var result;
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            return (*fn) (args);

        juce::WaitableEvent done;
        juce::MessageManager::callAsync ([&, f = *fn] { result = f (args); done.signal(); });
        done.wait (2000);
        return result;
    };

    handlers.streamOpened = [&host]
    {
        juce::MessageManager::callAsync ([h = host.get()] { h->resendToBrowser(); });
    };

    const int port = value ("--port", "8080").getIntValue();
    epi::WebServer server (port, value ("--bind", "0.0.0.0"), std::move (handlers));
    if (! server.start())
    {
        std::fprintf (stderr, "Epi: cannot listen on port %d — is another copy running?\n", port);
        return 1;
    }

    host->attach (&server, midiOut.get(), ! flag ("--no-cc-feedback"));
    if (! noAudio)
        devices.addAudioCallback (host.get());

    if (auto* dev = devices.getCurrentAudioDevice())
        std::printf ("Epi: audio %s, %.0f Hz, %d samples\n",
                     dev->getName().toRawUTF8(), dev->getCurrentSampleRate(),
                     dev->getCurrentBufferSizeSamples());
    std::printf ("Epi: interface on http://localhost:%d/ (and on this machine's address)\n", port);
    std::printf ("Epi: ready. Ctrl-C to stop.\n");
    std::fflush (stdout);

    std::signal (SIGINT,  onSignal);
    std::signal (SIGTERM, onSignal);

    // An unbundled console binary on macOS returns from runDispatchLoop
    // immediately, so the loop is pumped in slices instead. Timers, the async
    // parameter edits posted from the server threads, and the preset manager
    // all need this thread to be running one.
    while (! shouldQuit.load())
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    std::printf ("\nEpi: stopping.\n");

    if (! noAudio)
        devices.removeAudioCallback (host.get());
    for (auto& in : inputs) in->stop();
    server.stop();
    return 0;
}
