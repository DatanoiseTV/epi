/*
  Epi — physically modeled electric pianos
  Copyright (C) 2026 DatanoiseTV

  Offline renderer. Writes demo WAVs straight from the engine, using the same
  signal chain the plugin will, so what you hear is what it does.

  Build:  clang++ -std=c++20 -O2 -Isrc tools/epi_render.cpp src/epi/dsp/EpiEngine.cpp -o epi_render
  Run:    ./epi_render <output-dir>
*/

#include "epi/dsp/EpiEngine.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace epi;

// ---------------------------------------------------------------------------
// A 24-bit WAV writer. 24-bit because the instrument has a very wide dynamic
// range and 16 would put dither noise into the tail of a note that is still
// decaying thirty seconds later.
// ---------------------------------------------------------------------------
static void writeWav (const std::string& path, const std::vector<float>& l,
                      const std::vector<float>& r, int sampleRate)
{
    const int frames = static_cast<int> (std::min (l.size(), r.size()));
    const int channels = 2, bits = 24, bytes = 3;
    const int dataBytes = frames * channels * bytes;

    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) { std::printf ("cannot write %s\n", path.c_str()); return; }

    auto u32 = [f] (unsigned v) { unsigned char b[4] { (unsigned char) v, (unsigned char) (v >> 8),
                                                       (unsigned char) (v >> 16), (unsigned char) (v >> 24) };
                                  std::fwrite (b, 1, 4, f); };
    auto u16 = [f] (unsigned v) { unsigned char b[2] { (unsigned char) v, (unsigned char) (v >> 8) };
                                  std::fwrite (b, 1, 2, f); };

    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (channels);
    u32 (sampleRate); u32 (sampleRate * channels * bytes); u16 (channels * bytes); u16 (bits);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);

    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
        {
            float v = c == 0 ? l[(size_t) i] : r[(size_t) i];
            v = std::clamp (v, -1.0f, 1.0f);
            const int s = static_cast<int> (v * 8388607.0f);
            unsigned char b[3] { (unsigned char) (s & 0xff), (unsigned char) ((s >> 8) & 0xff),
                                 (unsigned char) ((s >> 16) & 0xff) };
            std::fwrite (b, 1, 3, f);
        }
    std::fclose (f);
}

// ---------------------------------------------------------------------------
// A tiny score: notes with a time, a length and a velocity.
// ---------------------------------------------------------------------------
struct Note { double at, dur; int pitch; double vel; };

struct Score
{
    std::vector<Note> notes;
    std::vector<std::pair<double, bool>> pedal;   // time, down
    double length = 4.0;

    void add (double at, double dur, int pitch, double vel)
    { notes.push_back ({ at, dur, pitch, vel }); }

    void chord (double at, double dur, std::initializer_list<int> pitches, double vel)
    { for (int p : pitches) add (at, dur, p, vel); }
};

static void render (const Score& score, const EngineParams& params,
                    const std::string& path, int sampleRate)
{
    EpiEngine engine;
    engine.prepare (sampleRate, 512);

    const int total = static_cast<int> (score.length * sampleRate);
    std::vector<float> L (static_cast<size_t> (total), 0.0f);
    std::vector<float> R (static_cast<size_t> (total), 0.0f);

    // Flatten the score into sample-accurate events.
    std::vector<NoteEvent> all;
    for (const auto& n : score.notes)
    {
        all.push_back ({ static_cast<int> (n.at * sampleRate), NoteEvent::noteOn,
                         n.pitch, static_cast<float> (n.vel) });
        all.push_back ({ static_cast<int> ((n.at + n.dur) * sampleRate), NoteEvent::noteOff,
                         n.pitch, 0.0f });
    }
    for (const auto& [t, down] : score.pedal)
        all.push_back ({ static_cast<int> (t * sampleRate),
                         down ? NoteEvent::sustainOn : NoteEvent::sustainOff, 0, 0.0f });
    std::sort (all.begin(), all.end(),
               [] (const NoteEvent& a, const NoteEvent& b) { return a.offset < b.offset; });

    constexpr int kBlock = 128;
    size_t next = 0;
    std::vector<NoteEvent> block;

    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = std::min (kBlock, total - pos);
        block.clear();
        while (next < all.size() && all[next].offset < pos + n)
        {
            NoteEvent e = all[next++];
            e.offset = std::max (0, e.offset - pos);
            block.push_back (e);
        }
        engine.process (L.data() + pos, R.data() + pos, n, params,
                        block.data(), static_cast<int> (block.size()));
    }

    // Normalise to -3 dBFS. The model's absolute level is in volts at a coil,
    // not in dBFS, so a fixed output gain would be meaningless.
    float peak = 0.0f;
    for (int i = 0; i < total; ++i) peak = std::max (peak, std::max (std::abs (L[(size_t) i]),
                                                                    std::abs (R[(size_t) i])));
    const float g = peak > 1.0e-9f ? 0.708f / peak : 1.0f;
    for (int i = 0; i < total; ++i) { L[(size_t) i] *= g; R[(size_t) i] *= g; }

    // Short fade out so a note still ringing at the end does not click.
    const int fade = std::min (total, sampleRate / 4);
    for (int i = 0; i < fade; ++i)
    {
        const float k = static_cast<float> (i) / fade;
        L[(size_t) (total - 1 - i)] *= k;
        R[(size_t) (total - 1 - i)] *= k;
    }

    writeWav (path, L, R, sampleRate);
    std::printf ("  %-34s %5.1f s  peak before normalising %.4f\n",
                 path.substr (path.find_last_of ('/') + 1).c_str(), score.length, peak);
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    constexpr int fs = 48000;

    std::printf ("Rendering Epi demos at %d Hz\n\n", fs);

    // ---- 1. The keyboard, one note per octave, so the compass is audible ----
    {
        Score s; s.length = 15.0;
        double t = 0.3;
        for (int n : { 28, 40, 52, 64, 76, 88 }) { s.add (t, 1.4, n, 0.8); t += 1.6; }
        // Then the same notes soft, to hear how far the timbre moves with touch.
        for (int n : { 28, 40, 52, 64, 76, 88 }) { s.add (t, 1.0, n, 0.22); t += 0.7; }
        EngineParams p; p.tremDepth = 0.0f;
        render (s, p, dir + "/01-compass.wav", fs);
    }

    // ---- 2. Velocity: the bark appearing as you dig in ----------------------
    {
        Score s; s.length = 11.0;
        double t = 0.3;
        for (double v : { 0.15, 0.3, 0.45, 0.6, 0.75, 0.9, 1.0 })
        { s.add (t, 1.1, 40, v); t += 1.4; }
        EngineParams p;
        render (s, p, dir + "/02-velocity-bark.wav", fs);
    }

    // ---- 3. The voicing screw: the same phrase at five pickup heights -------
    // Nothing changes but where the tine sits in the magnet's field.
    {
        const double heights[] = { -0.85, -0.5, -0.25, -0.05, 0.15 };
        int i = 0;
        for (double h : heights)
        {
            Score s; s.length = 5.0;
            s.chord (0.2, 3.2, { 52, 56, 59, 64 }, 0.75);
            s.add (2.0, 2.4, 40, 0.8);
            EngineParams p; p.pickupPos = static_cast<float> (h);
            char name[128];
            std::snprintf (name, sizeof name, "%s/03-voicing-%d-height%+.2f.wav",
                           dir.c_str(), ++i, h);
            render (s, p, name, fs);
        }
    }

    // ---- 4. A chord with the pedal down, to hear the sustain and the beating -
    {
        Score s; s.length = 22.0;
        s.pedal.push_back ({ 0.05, true });
        s.chord (0.2,  0.5, { 40, 52, 56, 59 }, 0.7);
        s.chord (2.2,  0.5, { 45, 57, 61, 64 }, 0.65);
        s.chord (4.2,  0.5, { 38, 50, 54, 57 }, 0.7);
        s.chord (6.2,  0.5, { 43, 55, 59, 62 }, 0.6);
        s.pedal.push_back ({ 9.0, false });
        s.pedal.push_back ({ 9.2, true });
        s.chord (9.3, 8.0, { 28, 40, 47, 52, 56, 59 }, 0.8);
        EngineParams p; p.resDamp = 0.2f;
        render (s, p, dir + "/04-sustain-pedal.wav", fs);
    }

    // ---- 5. The vibrato, which is a stereo panner ---------------------------
    {
        Score s; s.length = 16.0;
        s.pedal.push_back ({ 0.05, true });
        s.chord (0.2, 7.0, { 40, 52, 56, 59, 64 }, 0.7);
        s.chord (8.2, 7.0, { 38, 50, 57, 62, 65 }, 0.7);
        EngineParams p;
        p.tremDepth = 0.85f; p.tremRate = 4.2f;
        render (s, p, dir + "/05-vibrato-slow.wav", fs);

        // The same at speed. The photocell's slow decay means the depth
        // collapses -- fast settings on a real Suitcase are much shallower, and
        // that falls out of the model rather than being dialled in.
        p.tremRate = 11.0f;
        render (s, p, dir + "/06-vibrato-fast.wav", fs);
    }

    // ---- 6. A short piece, so it can be judged as an instrument -------------
    {
        Score s; s.length = 26.0;
        s.pedal.push_back ({ 0.05, true });

        struct Bar { double t; std::initializer_list<int> ch; int bass; };
        const double q = 0.62;
        double t = 0.3;

        auto phrase = [&] (std::initializer_list<int> ch, int bass, double vel)
        {
            s.add (t, q * 3.6, bass, vel + 0.05);
            for (int p : ch) s.add (t + 0.02, q * 3.4, p, vel);
            // A little melody on top, slightly behind the beat.
            s.add (t + q * 1.05, q * 0.9, *(ch.end() - 1) + 5, vel * 0.85);
            s.add (t + q * 2.05, q * 1.4, *(ch.end() - 1) + 7, vel * 0.78);
            t += q * 4.0;
        };

        phrase ({ 52, 55, 59, 62 }, 40, 0.62);   // Cm9
        phrase ({ 50, 53, 57, 60 }, 38, 0.58);   // Bbm9
        phrase ({ 48, 52, 55, 59 }, 36, 0.66);   // Abmaj9
        phrase ({ 47, 50, 54, 57 }, 35, 0.60);   // Gm7
        phrase ({ 52, 55, 59, 62 }, 40, 0.70);
        phrase ({ 50, 53, 57, 60 }, 38, 0.64);
        phrase ({ 45, 48, 52, 55 }, 33, 0.72);
        s.chord (t, 6.0, { 40, 47, 52, 55, 59, 62 }, 0.55);

        EngineParams p;
        p.tremDepth = 0.45f; p.tremRate = 3.6f;
        p.resDamp = 0.28f; p.bassDb = 3.0f; p.trebleDb = 1.5f;
        render (s, p, dir + "/07-piece.wav", fs);
    }

    // ---- 7. Two pickups that never existed ---------------------------------
    // Same tine, same hammer; only the pole geometry differs. These are not
    // presets on a filter -- they are different magnets.
    {
        Score s; s.length = 7.0;
        s.pedal.push_back ({ 0.05, true });
        s.chord (0.2, 5.0, { 40, 52, 56, 59, 64 }, 0.75);

        EngineParams p;
        p.coilFreq = 0.75f; p.coilQ = 0.8f; p.pickupPos = -0.15f; p.pickupDist = 0.12f;
        render (s, p, dir + "/08-close-narrow-pole.wav", fs);

        p.coilFreq = 0.25f; p.coilQ = 0.3f; p.pickupPos = -0.7f; p.pickupDist = 0.8f;
        render (s, p, dir + "/09-far-wide-pole.wav", fs);
    }

    std::printf ("\nDone.\n");
    return 0;
}
