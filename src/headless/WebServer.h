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

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace epi
{

// ---------------------------------------------------------------------------
// The smallest HTTP server that can carry this interface, and no smaller.
//
// It serves the same files the plugin embeds, answers posts, and holds open
// one text/event-stream per browser. That is the whole protocol: there is no
// WebSocket here, and the reason is worth stating rather than discovering.
//
// The traffic is one-directional per channel. The host sends telemetry sixty
// times a second and echoes parameters the player did not move; the browser
// sends edits, which are rare and small. Server-sent events carry the first
// and ordinary posts carry the second, which needs no upgrade handshake, no
// SHA-1, no frame masking and no ping/pong keepalive -- about three hundred
// lines of protocol that would have to be written and then trusted. On a
// board where this is expected to run beside the audio thread, not writing
// them is the design.
//
// Threading: one acceptor thread, one thread per connection. A browser holds
// exactly one stream open, so the thread count is the number of open tabs.
// ---------------------------------------------------------------------------
class WebServer
{
public:
    // Everything the host has to answer. All four are called on server
    // threads, never on the audio thread.
    struct Handlers
    {
        // path -> body. Return false for "no such asset".
        std::function<bool (const juce::String& path, juce::MemoryBlock& out,
                            juce::String& mimeType)> asset;
        // A parameter the browser moved.
        std::function<void (const juce::String& kind, const juce::String& id,
                            double value)> setParam;
        // An interface event, e.g. a workshop edit.
        std::function<void (const juce::String& name, const juce::var& payload)> emit;
        // A native call. Return the result as a var.
        std::function<juce::var (const juce::String& name, const juce::var& args)> native;
        // A browser opened an event stream. It knows nothing yet -- and a
        // reconnecting one knows something STALE, which is worse -- so this is
        // where the host restates every parameter.
        std::function<void()> streamOpened;
    };

    WebServer (int port, juce::String bindAddress, Handlers h)
        : handlers (std::move (h)), listenPort (port), address (std::move (bindAddress)) {}
    ~WebServer() { stop(); }

    bool start()
    {
        if (! listener.createListener (listenPort, address))
            return false;
        running = true;
        acceptor = std::thread ([this] { acceptLoop(); });
        return true;
    }

    void stop()
    {
        if (! running.exchange (false)) return;
        listener.close();
        if (acceptor.joinable()) acceptor.join();
        {
            const juce::ScopedLock sl (streamLock);
            for (auto& s : streams) s->alive = false;
        }
        for (auto& w : workers) if (w->thread.joinable()) w->thread.join();
        workers.clear();
    }

    // Push one line to every open browser. Called from the message thread at
    // the telemetry rate; never from the audio thread.
    void broadcast (const juce::String& jsonLine)
    {
        const juce::ScopedLock sl (streamLock);
        for (auto it = streams.begin(); it != streams.end();)
        {
            auto& s = **it;
            if (! s.alive || s.socket == nullptr || ! s.socket->isConnected())
            { s.alive = false; it = streams.erase (it); continue; }

            // A browser that has stopped reading must not be allowed to block
            // the sender. StreamingSocket::write blocks until the whole buffer
            // is gone, so on a stalled client -- a laptop that slept, a phone
            // that walked out of wifi range -- writing straight into it would
            // hold the message thread and take the audio device down with it.
            // Ask first, and drop the frame if the window is full: telemetry
            // is a snapshot, so the next one supersedes it and nothing has to
            // be queued.
            if (s.socket->waitUntilReady (false, 0) != 1) { ++it; continue; }

            const juce::String frame = "data: " + jsonLine + "\n\n";
            const auto utf8 = frame.toRawUTF8();
            const int len = (int) frame.getNumBytesAsUTF8();
            const int n = s.socket->write (utf8, len);

            // A PARTIAL write cannot be left as it is: half a frame
            // desynchronises the stream and every frame after it arrives as
            // broken JSON. Drop the connection instead -- the page reconnects
            // by itself within a second and is resynchronised on the way in,
            // which is a shorter gap than a corrupt stream would ever recover
            // from. This is why the message thread does not loop here.
            if (n != len) { s.alive = false; it = streams.erase (it); }
            else ++it;
        }
    }

    int boundPort() const { return listenPort; }

private:
    struct Stream
    {
        std::unique_ptr<juce::StreamingSocket> socket;
        std::atomic<bool> alive { true };
    };

    struct Worker
    {
        std::thread thread;
        std::atomic<bool> finished { false };
    };

    void acceptLoop()
    {
        while (running)
        {
            std::unique_ptr<juce::StreamingSocket> c { listener.waitForNextConnection() };
            if (! running) break;
            if (c == nullptr) continue;
            auto w = std::make_unique<Worker>();
            auto* raw = w.get();
            raw->thread = std::thread ([this, s = std::move (c), raw]() mutable
                                       { serve (std::move (s)); raw->finished = true; });
            workers.push_back (std::move (w));

            // Reap finished workers so a long session does not accumulate a
            // thread per page load. A finished std::thread stays joinable
            // until it is joined, so the flag is what says it is done -- and
            // destroying a joinable thread would be std::terminate, not a
            // leak, which makes getting this wrong loud rather than slow.
            workers.erase (std::remove_if (workers.begin(), workers.end(),
                                           [] (std::unique_ptr<Worker>& x)
                                           {
                                               if (! x->finished) return false;
                                               if (x->thread.joinable()) x->thread.join();
                                               return true;
                                           }),
                           workers.end());
        }
    }

    static juce::String header (int code, const juce::String& mime, int length,
                                const char* extra = "")
    {
        return "HTTP/1.1 " + juce::String (code) + (code == 200 ? " OK" : " Not Found") + "\r\n"
             + "Content-Type: " + mime + "\r\n"
             + "Content-Length: " + juce::String (length) + "\r\n"
             + "Cache-Control: no-store\r\n"
             + "Access-Control-Allow-Origin: *\r\n"
             + extra + "Connection: close\r\n\r\n";
    }

    void serve (std::unique_ptr<juce::StreamingSocket> sock)
    {
        // Read the request head. A browser sends it in one segment in
        // practice, but the loop is here because "in practice" is not a
        // guarantee and a truncated head reads as a malformed request.
        juce::String req;
        char buf[4096];
        for (int i = 0; i < 32 && req.indexOf ("\r\n\r\n") < 0; ++i)
        {
            if (! sock->waitUntilReady (true, 2000)) break;
            const int n = sock->read (buf, sizeof buf - 1, false);
            if (n <= 0) break;
            buf[n] = 0;
            req += juce::String::fromUTF8 (buf, n);
        }
        if (req.isEmpty()) return;

        const juce::String head = req.upToFirstOccurrenceOf ("\r\n", false, false);
        const juce::String method = head.upToFirstOccurrenceOf (" ", false, false);
        juce::String path = head.fromFirstOccurrenceOf (" ", false, false)
                                .upToFirstOccurrenceOf (" ", false, false);
        if (path.contains ("?")) path = path.upToFirstOccurrenceOf ("?", false, false);

        const juce::String body = req.fromFirstOccurrenceOf ("\r\n\r\n", false, false);

        if (method == "GET" && path == "/api/events") { serveEvents (std::move (sock)); return; }

        if (method == "POST")
        {
            const juce::var payload = juce::JSON::parse (body);
            if (path == "/api/set")
            {
                if (auto* o = payload.getDynamicObject())
                    if (handlers.setParam)
                        handlers.setParam (o->getProperty ("kind").toString(),
                                           o->getProperty ("id").toString(),
                                           (double) o->getProperty ("value"));
                writeAll (*sock, header (200, "application/json", 2) + "{}");
                return;
            }
            if (path.startsWith ("/api/emit/"))
            {
                if (handlers.emit)
                    handlers.emit (juce::URL::removeEscapeChars (path.fromFirstOccurrenceOf ("/api/emit/", false, false)),
                                   payload);
                writeAll (*sock, header (200, "application/json", 2) + "{}");
                return;
            }
            if (path.startsWith ("/api/native/"))
            {
                juce::var result;
                if (handlers.native)
                {
                    juce::var args;
                    if (auto* o = payload.getDynamicObject()) args = o->getProperty ("args");
                    result = handlers.native (juce::URL::removeEscapeChars (path.fromFirstOccurrenceOf ("/api/native/", false, false)),
                                              args);
                }
                auto* wrap = new juce::DynamicObject();
                wrap->setProperty ("result", result);
                const juce::String json = juce::JSON::toString (juce::var (wrap), true);
                writeAll (*sock, header (200, "application/json", json.getNumBytesAsUTF8()) + json);
                return;
            }
        }

        if (method == "GET")
        {
            juce::MemoryBlock data;
            juce::String mime;
            if (handlers.asset && handlers.asset (path, data, mime))
            {
                if (writeAll (*sock, header (200, mime, (int) data.getSize())))
                    writeFully (*sock, data.getData(), data.getSize());
                return;
            }
        }

        const juce::String notFound = "not found";
        writeAll (*sock, header (404, "text/plain", notFound.length()) + notFound);
    }

    void serveEvents (std::unique_ptr<juce::StreamingSocket> sock)
    {
        const juce::String head =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-store\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: keep-alive\r\n"
            "X-Accel-Buffering: no\r\n\r\n";
        if (! writeAll (*sock, head)) return;

        auto s = std::make_shared<Stream>();
        auto* raw = s.get();
        s->socket = std::move (sock);
        {
            const juce::ScopedLock sl (streamLock);
            streams.push_back (s);
        }
        if (handlers.streamOpened) handlers.streamOpened();

        // Hold the thread until the browser goes away. broadcast() writes
        // through the same socket from the message thread; this side only
        // waits, so there is one writer and no interleaving.
        while (running && raw->alive && raw->socket->isConnected())
            juce::Thread::sleep (200);
        raw->alive = false;
    }

    // juce::StreamingSocket::write is ONE ::send(). It does not loop, and a
    // send() of two megabytes returns as soon as the socket's send buffer is
    // full -- about 146 kB here. So anything larger than that buffer arrives
    // truncated underneath a Content-Length that says otherwise, which is
    // exactly how a browser reports "loading failed" for a file the server
    // believes it sent. Everything written to a socket goes through here.
    //
    // The deadline is on LACK OF PROGRESS, not on total time: a big asset over
    // a slow link is fine, a peer that has stopped reading is not.
    static bool writeFully (juce::StreamingSocket& s, const void* data, size_t total)
    {
        constexpr int kChunk = 1 << 16;
        constexpr uint32_t kStallMs = 15000;

        const auto* p = static_cast<const char*> (data);
        size_t done = 0;
        auto lastProgress = juce::Time::getMillisecondCounter();

        while (done < total)
        {
            if (! s.isConnected()) return false;
            if (s.waitUntilReady (false, 200) < 0) return false;

            const int chunk = (int) std::min (static_cast<size_t> (kChunk), total - done);
            const int n = s.write (p + done, chunk);

            if (n > 0)
            {
                done += static_cast<size_t> (n);
                lastProgress = juce::Time::getMillisecondCounter();
                continue;
            }

            // A socket with no room says so rather than failing; anything else
            // is the connection going away.
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                return false;
            if (juce::Time::getMillisecondCounter() - lastProgress > kStallMs)
                return false;
        }
        return true;
    }

    static bool writeAll (juce::StreamingSocket& s, const juce::String& text)
    {
        return writeFully (s, text.toRawUTF8(), text.getNumBytesAsUTF8());
    }

    Handlers handlers;
    int listenPort = 0;
    juce::String address { "0.0.0.0" };
    juce::StreamingSocket listener;
    std::atomic<bool> running { false };
    std::thread acceptor;
    std::vector<std::unique_ptr<Worker>> workers;
    juce::CriticalSection streamLock;
    std::vector<std::shared_ptr<Stream>> streams;
};

} // namespace epi
