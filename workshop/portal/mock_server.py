#!/usr/bin/env python3
"""Serve the device portal on a desktop, with the whole API faked.

The portal lives inside a C++ raw string in `firmware/main/portal/portal_page.h`, so the
only way to look at it is normally to build firmware, flash a robot and open its address.
That is a long loop for a CSS change, and it is impossible while the hardware is elsewhere.

This pulls the page straight out of the header and answers every endpoint with plausible
data, so the layout, the navigation and all the interactions can be exercised in a browser.
State is held in memory, so saving something and reloading behaves the way it will on the
device.

    python3 workshop/portal/mock_server.py --port 8130
    # then open http://<this machine>:8130/

It binds 0.0.0.0 on purpose: the useful case is looking at it from another machine.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE_HEADER = os.path.normpath(
    os.path.join(HERE, "..", "..", "firmware", "main", "portal", "portal_page.h"))

BOOTED = time.time()


def load_page() -> bytes:
    """Extract the HTML from the C++ raw string literal it is embedded in."""
    with open(PAGE_HEADER, encoding="utf-8") as fh:
        src = fh.read()
    start = src.index('R"HTML(') + len('R"HTML(')
    end = src.rindex(')HTML"')
    return src[start:end].encode("utf-8")


# Mutable state, so the page behaves like it is talking to a real device.
STATE = {
    "net": {
        "static_ip": False, "ip": "", "netmask": "255.255.255.0", "gateway": "",
        "dns1": "", "dns2": "", "hostname": "stackchan",
    },
    "time": {
        "ntp1": "pool.ntp.org", "ntp2": "time.google.com",
        "ntp3": "time.cloudflare.com", "tz": "EST5EDT,M3.2.0,M11.1.0", "enabled": True,
    },
    "ai": {
        "url": "ws://192.168.1.20:8000/xiaozhi/v1/", "token_set": True, "version": 1,
        "ota_url": "https://api.tenclass.net/xiaozhi/ota/",
        "provider": "anthropic", "model": "claude-sonnet-5",
    },
    "personas": [
        {"id": "max", "name": "MAX", "voice": "onyx", "skin": 2,
         "prompt": "You are MAX, a fast-talking television host made of pure signal. "
                   "Speak in short bursts. Stutter on the first word sometimes."},
        {"id": "helper", "name": "Desk Helper", "voice": "alloy", "skin": 0,
         "prompt": "You are a calm, concise desk assistant. One or two sentences."},
    ],
    "active_persona": "max",
    "skins": [{"id": 0, "name": "Default"}, {"id": 1, "name": "VOLT"}, {"id": 2, "name": "MAX"}],
    "active_skin": 2,
    "wake": "wn9_histackchan_tts3",
    # The eleven phrases firmware/sdkconfig.defaults packs today.
    "wake_models": [
        {"model": "wn9_histackchan_tts3", "phrase": "Hi Stack Chan"},
        {"model": "wn9_jarvis_tts", "phrase": "Jarvis"},
        {"model": "wn9_computer_tts", "phrase": "Computer"},
        {"model": "wn9_himfive", "phrase": "Hi M Five"},
        {"model": "wn9_hiwalle_tts2", "phrase": "Hi Wall-E"},
        {"model": "wn9_mycroft_tts", "phrase": "Mycroft"},
        {"model": "wn9_heywanda_tts", "phrase": "Hey Wanda"},
        {"model": "wn9_heywillow_tts", "phrase": "Hey Willow"},
        {"model": "wn9_sophia_tts", "phrase": "Sophia"},
        {"model": "wn9_heykira_tts3", "phrase": "Hey Kira"},
        {"model": "wn9_hijason_tts2", "phrase": "Hi Jason"},
    ],
    "screensaver": {"enabled": True, "timeout_ms": 30000, "style": 0},
    "memory": {"notes": "", "history_turns": 0},
    "kie": {"model": "kling-2.6/text-to-video"},
    "weather": {"location": ""},
    "stock": {"symbol": ""},
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    # ------------------------------------------------------------------ plumbing --

    def log_message(self, fmt, *args):
        print(f"  {self.command:5s} {self.path}")

    def _send(self, body: bytes, ctype: str, status: int = 200):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, status: int = 200):
        self._send(json.dumps(obj).encode(), "application/json", status)

    def _body(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            return json.loads(raw or b"{}")
        except ValueError:
            return {}

    # ----------------------------------------------------------------------- GET --

    def do_GET(self):
        path = self.path.split("?")[0]

        if path == "/":
            # Re-read every time, so editing the header and refreshing is the whole loop.
            self._send(load_page(), "text/html; charset=utf-8")

        elif path == "/api/info":
            self._json({
                "version": "9.9.3-stackychan", "compiled": "Aug 11 2026 20:48:00",
                "partition": "ota_0", "ip": "192.168.1.50",
                "uptime_ms": int((time.time() - BOOTED) * 1000) + 4527000,
                "free_heap": 4231168,
                "skin": next(s["name"] for s in STATE["skins"]
                             if s["id"] == STATE["active_skin"]),
            })

        elif path == "/api/net":
            self._json({
                "config": STATE["net"],
                "status": {
                    "ip": STATE["net"]["ip"] or "192.168.1.50",
                    "netmask": "255.255.255.0", "gateway": "192.168.1.1",
                    "dns1": "192.168.1.1", "dns2": "1.1.1.1",
                    "mac": "3C:84:27:AB:CD:EF", "ssid": "HomeWiFi",
                    "rssi": -52, "dhcp": not STATE["net"]["static_ip"], "link_up": True,
                },
                "revert_in": 0,
            })

        elif path == "/api/time":
            cfg = dict(STATE["time"])
            cfg["now"] = time.strftime("%Y-%m-%d %H:%M:%S")
            cfg["synced"] = True
            self._json(cfg)

        elif path == "/api/ai":
            self._json(STATE["ai"])

        elif path == "/api/personas":
            self._json({"personas": STATE["personas"], "active": STATE["active_persona"]})

        elif path == "/api/skins":
            self._json({"skins": STATE["skins"], "active": STATE["active_skin"]})

        elif path == "/api/screensaver":
            self._json(STATE["screensaver"])

        elif path == "/api/wake":
            models = [dict(m, active=(m["model"] == STATE["wake"]))
                      for m in STATE["wake_models"]]
            self._json({"models": models, "selected": STATE["wake"]})

        elif path in ("/api/memory", "/api/kie", "/api/weather", "/api/stock"):
            self._json(STATE[path.rsplit("/", 1)[1]])

        elif path == "/api/music/list":
            # No card: the same answer real hardware gives today (see docs/STATUS.md, SD card).
            self._json({"sd_card_available": False, "songs": []})

        else:
            self._json({"error": "not found"}, 404)

    # ---------------------------------------------------------------------- POST --

    def do_POST(self):
        path = self.path.split("?")[0]
        body = self._body()

        if path == "/api/net":
            STATE["net"].update({k: body.get(k, STATE["net"][k]) for k in STATE["net"]})
            self._json({"ok": True, "staged": bool(body.get("static_ip")),
                        "revert_in": 120 if body.get("static_ip") else 0,
                        "next_ip": body.get("ip", "")})

        elif path == "/api/time":
            for key in STATE["time"]:
                if key in body:
                    STATE["time"][key] = body[key]
            self._json({"ok": True, "now": time.strftime("%Y-%m-%d %H:%M:%S")})

        elif path == "/api/time/sync":
            self._json({"ok": True, "now": time.strftime("%Y-%m-%d %H:%M:%S")})

        elif path == "/api/ai":
            for key in ("url", "ota_url", "provider", "model", "version"):
                if key in body:
                    STATE["ai"][key] = body[key]
            if body.get("token"):
                STATE["ai"]["token_set"] = True
            self._json({"ok": True, "note": "takes effect on the next conversation"})

        elif path == "/api/personas":
            STATE["personas"] = body.get("personas", [])[:8]
            self._json({"ok": True, "saved": len(STATE["personas"])})

        elif path == "/api/persona":
            STATE["active_persona"] = body.get("id", "")
            for p in STATE["personas"]:
                if p["id"] == STATE["active_persona"] and p.get("skin", -1) >= 0:
                    STATE["active_skin"] = p["skin"]
            self._json({"ok": True, "active": STATE["active_persona"],
                        "skin": STATE["active_skin"], "reboot_required": False})

        elif path == "/api/wake":
            STATE["wake"] = body.get("model", STATE["wake"])
            self._json({"ok": True, "reboot_required": True})

        elif path == "/api/skin":
            # The device swaps the face live when the AI screen is up; mirror that.
            STATE["active_skin"] = int(body.get("id", STATE["active_skin"]))
            self._json({"ok": True, "reboot_required": False,
                        "name": STATE["skins"][STATE["active_skin"]]["name"]})

        elif path in ("/api/memory", "/api/kie", "/api/weather", "/api/stock"):
            store = STATE[path.rsplit("/", 1)[1]]
            for key in store:
                if key in body:
                    store[key] = body[key]
            self._json({"ok": True})

        elif path.startswith("/api/music/"):
            self._json({"error": "no SD card"}, 400)

        elif path == "/api/screensaver":
            scr = STATE["screensaver"]
            scr["enabled"] = bool(body.get("enabled", scr["enabled"]))
            scr["timeout_ms"] = int(body.get("timeout_ms", scr["timeout_ms"]))
            scr["style"] = int(body.get("style", scr["style"]))
            self._json({"ok": True})

        elif path in ("/api/emotion", "/api/avatar", "/api/motion", "/api/rgb"):
            self._json({"ok": True})

        elif path == "/api/reboot":
            self._json({"ok": True, "rebooting": True})

        elif path == "/api/echo":
            # The page streams a megabyte here; read it all or the socket desyncs.
            length = int(self.headers.get("Content-Length") or 0)
            read = 0
            while read < length:
                chunk = self.rfile.read(min(65536, length - read))
                if not chunk:
                    break
                read += len(chunk)
            self._json({"ok": read == length, "received": read, "expected": length,
                        "ms": 120, "kbps": 8700, "recv": 0})

        elif path.startswith("/api/ota"):
            self._json({"ok": True, "slot": "ota_1", "erase_ms": 2100,
                        "written": 1, "expected": 1})

        else:
            self._json({"error": "not found"}, 404)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8130)
    ap.add_argument("--host", default="0.0.0.0")
    args = ap.parse_args()

    load_page()  # fail now, loudly, rather than on the first request
    print(f"portal preview on http://{args.host}:{args.port}/  (source: {PAGE_HEADER})")
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
