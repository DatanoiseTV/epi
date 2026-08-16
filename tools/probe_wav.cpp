/*
  Look at a rendered file: where the energy is, where it is not, and what the
  bursts in between actually are.

  Build: c++ -std=c++20 -O2 -Itests tools/probe_wav.cpp -o /tmp/probe_wav
*/

#include "EpiAnalysis.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace an = epianalysis;

// Minimal 16-bit PCM WAVE reader: enough for something afconvert wrote.
static bool readWav (const char* path, std::vector<double>& l, std::vector<double>& r,
                     double& fs, int& channels)
{
    FILE* f = std::fopen (path, "rb");
    if (f == nullptr) return false;

    char riff[12];
    if (std::fread (riff, 1, 12, f) != 12) { std::fclose (f); return false; }

    int bits = 16;
    long dataBytes = 0;
    while (! std::feof (f))
    {
        char id[4];
        std::uint32_t size = 0;
        if (std::fread (id, 1, 4, f) != 4) break;
        if (std::fread (&size, 4, 1, f) != 1) break;

        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            std::vector<unsigned char> fmt (size);
            if (std::fread (fmt.data(), 1, size, f) != size) break;
            channels = fmt[2] | (fmt[3] << 8);
            fs = static_cast<double> (fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24));
            bits = fmt[14] | (fmt[15] << 8);
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            dataBytes = static_cast<long> (size);
            const long frames = dataBytes / (channels * (bits / 8));
            l.resize (static_cast<std::size_t> (frames));
            r.resize (static_cast<std::size_t> (frames));
            std::vector<std::int16_t> buf (static_cast<std::size_t> (frames * channels));
            if (std::fread (buf.data(), 2, buf.size(), f) != buf.size()) break;
            for (long i = 0; i < frames; ++i)
            {
                l[static_cast<std::size_t> (i)] = buf[static_cast<std::size_t> (i * channels)] / 32768.0;
                r[static_cast<std::size_t> (i)] = channels > 1
                    ? buf[static_cast<std::size_t> (i * channels + 1)] / 32768.0
                    : l[static_cast<std::size_t> (i)];
            }
            break;
        }
        else
        {
            std::fseek (f, static_cast<long> (size + (size & 1)), SEEK_CUR);
        }
    }
    std::fclose (f);
    return ! l.empty();
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: probe_wav <file.wav>\n"); return 1; }

    std::vector<double> L, R;
    double fs = 48000.0;
    int ch = 2;
    if (! readWav (argv[1], L, R, fs, ch)) { std::printf ("cannot read\n"); return 1; }

    std::printf ("%s\n  %.0f Hz, %d ch, %.1f s\n\n", argv[1], fs, ch,
                 static_cast<double> (L.size()) / fs);

    // Level over time, half a second at a time.
    const int hop = static_cast<int> (fs * 0.5);
    double overallPeak = 0.0;
    for (double v : L) overallPeak = std::max (overallPeak, std::abs (v));
    std::printf ("  overall peak %.5f (%.1f dBFS)\n\n", overallPeak,
                 20.0 * std::log10 (std::max (1e-12, overallPeak)));

    std::printf ("  %-9s %-10s %-10s %s\n", "time", "peak dBFS", "rms dBFS", "");
    int loud = 0, quiet = 0;
    for (std::size_t i = 0; i + hop < L.size(); i += static_cast<std::size_t> (hop))
    {
        double pk = 0.0, ms = 0.0;
        for (int j = 0; j < hop; ++j)
        {
            const double v = L[i + static_cast<std::size_t> (j)];
            pk = std::max (pk, std::abs (v));
            ms += v * v;
        }
        const double pkDb = 20.0 * std::log10 (std::max (1e-12, pk));
        const double rmsDb = 10.0 * std::log10 (std::max (1e-24, ms / hop));
        if (rmsDb > -60.0) ++loud; else ++quiet;
        // Only print the first stretch and anything unusual, or this is 900 rows.
        if (i / static_cast<std::size_t> (hop) < 24)
            std::printf ("  %-9.1f %-10.1f %-10.1f %s\n",
                         static_cast<double> (i) / fs, pkDb, rmsDb,
                         (pkDb > -60.0 && rmsDb < -60.0) ? "  <- spike in silence" : "");
    }
    std::printf ("\n  %d half-second windows above -60 dB rms, %d below\n", loud, quiet);
    return 0;
}
