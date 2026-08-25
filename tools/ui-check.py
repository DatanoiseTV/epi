#!/usr/bin/env python3
"""Headless smoke test for the plugin's interface.

The C++ suites never load the page, so a panel can throw on render while
every one of them stays green -- which is exactly what happened when a knob
reached the panel without an entry in the parameter table. The state suite
now catches that specific class (row S7), but only that class: a typo in a
component, a workshop that no longer opens, a panel that renders empty are
all still invisible to it.

This drives the real page in headless Chrome against the browser mock,
visits every instrument, opens every workshop, and reports any JavaScript
exception. It needs Chrome and the `websockets` package; it is a developer
tool, not part of ctest, because CI runners have neither.

    python3 tools/ui-check.py            # from the repo root
    python3 tools/ui-check.py --shot out.png
"""

import argparse
import base64
import http.server
import json
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
DEBUG_PORT = 9233
INSTRUMENTS = ["TINES", "STRINGS", "REEDS", "GRAND", "CLAV"]

DRIVE = r"""
(async function () {
  const out = [];
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const click = (txt) => {
    const b = [...document.querySelectorAll('button')]
      .find((e) => e.textContent.trim() === txt);
    if (b) { b.click(); return true; }
    return false;
  };
  for (const inst of %INSTRUMENTS%) {
    if (!click(inst)) { out.push({ inst, error: 'no selector button' }); continue; }
    await sleep(450);
    const shops = [...document.querySelectorAll('button')]
      .filter((b) => ['WORKSHOP', 'STUDIO', 'CURVE'].includes(b.textContent.trim()));
    let opened = 0;
    for (const s of shops) {
      s.click();
      await sleep(350);
      if (document.querySelector('.wsmodal')) opened++;
      const x = [...document.querySelectorAll('.wsactions button')]
        .find((b) => b.textContent.trim() === '✕');
      if (x) { x.click(); await sleep(200); }
    }
    out.push({
      inst,
      panels: document.querySelectorAll('.panel').length,
      knobs: document.querySelectorAll('.knob').length,
      shops: shops.length,
      opened,
    });
  }
  return JSON.stringify(out);
})()
"""


def serve(root: Path) -> socketserver.TCPServer:
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

    # Port 0: the kernel picks a free one. A fixed port makes a second run
    # fail on a socket the first one has not finished releasing, which is a
    # tool that works once.
    class Server(socketserver.TCPServer):
        allow_reuse_address = True

    handler = lambda *a, **kw: Quiet(*a, directory=str(root), **kw)  # noqa: E731
    httpd = Server(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


async def drive(ws_url: str, port: int, shot: str | None):
    import websockets  # imported late so --help works without it

    errors, mid = [], [0]
    async with websockets.connect(ws_url, max_size=80_000_000) as conn:
        async def cmd(method, params=None):
            mid[0] += 1
            await conn.send(json.dumps({"id": mid[0], "method": method,
                                        "params": params or {}}))
            while True:
                msg = json.loads(await conn.recv())
                if msg.get("method") == "Runtime.exceptionThrown":
                    d = msg["params"]["exceptionDetails"]
                    errors.append(f"{d.get('text', '')} "
                                  f"{d.get('exception', {}).get('description', '')}"[:300])
                if msg.get("id") == mid[0]:
                    return msg.get("result", {})

        await cmd("Page.enable")
        await cmd("Runtime.enable")
        await cmd("Page.navigate", {"url": f"http://127.0.0.1:{port}/index.html"})
        time.sleep(5)
        script = DRIVE.replace("%INSTRUMENTS%", json.dumps(INSTRUMENTS))
        res = await cmd("Runtime.evaluate",
                        {"expression": script, "awaitPromise": True,
                         "returnByValue": True})
        if shot:
            png = await cmd("Page.captureScreenshot", {"format": "png"})
            Path(shot).write_bytes(base64.b64decode(png["data"]))
        return json.loads(res.get("result", {}).get("value", "[]")), errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--shot", help="write a screenshot of the final state here")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parent.parent
    if not (repo / "ui" / "epi" / "index.html").exists():
        print("run this from the repo (ui/epi/index.html not found)")
        return 2
    if not Path(CHROME).exists():
        print(f"Chrome not found at {CHROME}")
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / "ui"
        root.mkdir()
        for src in list((repo / "ui" / "epi").iterdir()) + list((repo / "ui" / "vendor").iterdir()):
            if src.is_file():
                shutil.copy(src, root / src.name)
        httpd = serve(root)
        chrome = subprocess.Popen(
            [CHROME, "--headless=new", f"--remote-debugging-port={DEBUG_PORT}",
             f"--user-data-dir={tmp}/prof", "--window-size=1224,900", "about:blank"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(4)
            tabs = json.load(urllib.request.urlopen(
                f"http://127.0.0.1:{DEBUG_PORT}/json"))
            ws_url = [t for t in tabs if t["type"] == "page"][0]["webSocketDebuggerUrl"]
            import asyncio
            results, errors = asyncio.run(
                drive(ws_url, httpd.server_address[1], args.shot))
        finally:
            chrome.terminate()
            httpd.shutdown()

    bad = False
    for r in results:
        if "error" in r:
            print(f"FAIL {r['inst']}: {r['error']}")
            bad = True
            continue
        ok = r["panels"] >= 4 and r["knobs"] > 0 and r["opened"] == r["shops"]
        bad = bad or not ok
        print(f"{'PASS' if ok else 'FAIL'} {r['inst']:<8} panels={r['panels']} "
              f"knobs={r['knobs']} workshops={r['opened']}/{r['shops']}")
    if len(results) != len(INSTRUMENTS):
        print(f"FAIL only {len(results)} of {len(INSTRUMENTS)} instruments reached")
        bad = True
    print(f"{'FAIL' if errors else 'PASS'} javascript errors: {len(errors)}")
    for e in errors[:8]:
        print("   ", e)
    return 1 if (bad or errors) else 0


if __name__ == "__main__":
    sys.exit(main())
