"""StackyChan bridge — a xiaozhi-protocol WebSocket server backed by any LLM.

Why this exists
---------------
The device is a thin audio client. It captures speech, encodes it as Opus, streams it to
a server, and plays back whatever audio comes home. It contains no language model and has
no API key. So "use a different LLM" is not a firmware setting — it is a decision about
which server the device points at. This is that server.

Point the portal's AI backend URL at ws://<this host>:8000/xiaozhi/v1/ and the persona you
selected there becomes the system prompt, and the provider you picked answers.

The turn
--------
    device  ->  hello (JSON)                      capabilities, persona, model preference
    server  ->  hello (JSON)                      session id, the rate it will speak at
    device  ->  listen/start, then Opus frames    continuous while the user talks
    server  ->  stt                               what it heard, shown on the device
    server  ->  llm  (emotion)                    drives the face
    server  ->  tts/start, Opus frames, tts/stop  the spoken reply

In "auto" mode the device keeps streaming and expects the server to decide when a turn
ended, which is what the silence detector below is for. In "manual" mode the device sends
listen/stop itself and the detector is bypassed.

Status
------
Written against the firmware source in this repo (`protocols/protocol.cc`,
`application.cc`) and exercised by `selftest.py`, which runs a fake device through a whole
turn with the model and speech calls stubbed out. The protocol and audio framing are
tested that way; the provider calls are not — they need real keys and real hardware.
"""

from __future__ import annotations

import asyncio
import hmac
import json
import logging
import os
import re
import time
import uuid
from typing import Dict, List, Optional

import aiohttp
from aiohttp import web

from config import Config
import llm as llm_mod
import speech
import pihole
import weather
from accounts import AccountStore, SignInError
from mcp_device import DeviceMcpClient, device_tools_to_anthropic

log = logging.getLogger("bridge")


class Session:
    """One connected device, for as long as the socket lives."""

    def __init__(self, ws: web.WebSocketResponse, cfg: Config, device_id: str,
                 http: aiohttp.ClientSession, accounts: Optional[AccountStore] = None):
        self.ws = ws
        self.cfg = cfg
        self.device_id = device_id
        self.http = http
        self.accounts = accounts
        self.id = uuid.uuid4().hex[:16]

        self.codec = speech.OpusCodec(16000, cfg.tts_sample_rate, cfg.frame_duration_ms)
        self.history: List[Dict[str, object]] = []

        # Set from the device's hello.
        self.persona: Dict[str, str] = {}
        self.provider = cfg.llm_provider
        self.model = cfg.resolved_llm_model()
        if not self.model:
            log.warning("[%s] no model resolved for provider '%s' -- set BRIDGE_LLM_MODEL "
                       "explicitly for this provider, requests will fail with an empty model",
                       device_id, self.provider)
        self.kie_model = cfg.kie_model
        self.memory_notes = ""
        self.weather_location = cfg.weather_location
        self.history_turns = cfg.history_turns

        # The device's own MCP tools (servos, LEDs, reminders, ...), reached over this
        # same websocket. Discovered once, right after hello — see on_hello().
        self.device_mcp = DeviceMcpClient(self._send_mcp_text) if cfg.enable_device_tools else None
        self.device_tools: List[Dict] = []

        # Capture state for the turn in progress.
        self.listening = False
        self.mode = "auto"
        self.pcm = bytearray()
        self.last_voice_at = 0.0
        self.started_at = 0.0
        self.saw_voice = False
        self.busy = False
        self.aborted = False

    # ------------------------------------------------------------------ helpers --

    async def send_json(self, payload: dict):
        payload.setdefault("session_id", self.id)
        await self.ws.send_str(json.dumps(payload))

    async def send_audio(self, packets: List[bytes]):
        """Stream Opus frames out at roughly real time.

        The device has a small jitter buffer. Sending a whole reply as fast as the socket
        allows overruns it and the audio arrives chopped, so the frames are paced to their
        own duration. `aborted` is checked between frames because a wake word during
        playback should cut the reply off immediately.
        """
        interval = self.cfg.frame_duration_ms / 1000.0
        next_at = time.monotonic()
        for packet in packets:
            if self.aborted or self.ws.closed:
                return
            await self.ws.send_bytes(packet)
            next_at += interval
            delay = next_at - time.monotonic()
            if delay > 0:
                await asyncio.sleep(delay)

    def system_prompt(self) -> str:
        base = self.persona.get("prompt") or self.cfg.default_prompt
        if self.memory_notes:
            return f"{base}\n\nThings to remember:\n{self.memory_notes}"
        return base

    def remember(self, role: str, content: str):
        self.history.append({"role": role, "content": content})
        self._trim_history()

    def _trim_history(self):
        """Cap history length. Tool round trips add extra turns per exchange, so this
        runs after every exchange rather than once per remember() call — otherwise a
        tool-heavy turn could push history well past the configured limit before the
        next trim had a chance to run."""
        limit = self.history_turns * 2
        if len(self.history) > limit:
            self.history = self.history[-limit:]

    async def _send_mcp_text(self, text: str):
        """The raw websocket text sender DeviceMcpClient calls to reach the device."""
        await self.ws.send_str(text)

    async def _call_any_tool(self, name: str, arguments: dict) -> str:
        """Routes a tool call to wherever it actually lives — the device (over MCP), or
        a bridge-native integration (Pihole, weather) — by name. llm.py strips the
        `device_` prefix before calling this, so a device tool arrives here already bare
        (e.g. "set_led_color"); bridge-native tools arrive with their own full name
        (e.g. "pihole_disable"), since they don't share the device's prefix."""
        if name.startswith("pihole_"):
            return await pihole.call_tool(self.http, self.cfg, name, arguments)
        if name.startswith("weather_"):
            return await weather.call_tool(self.http, self.cfg, name, arguments, self.weather_location)
        if self.device_mcp is None:
            raise RuntimeError(f"no device tool caller available for '{name}'")
        return await self.device_mcp.call_tool(name, arguments)

    # -------------------------------------------------------------------- hello --

    async def on_hello(self, msg: dict):
        """Read the device's capabilities, persona and model preference."""
        persona = msg.get("persona")
        if isinstance(persona, dict):
            self.persona = persona
            log.info("[%s] persona: %s", self.device_id, persona.get("name") or persona.get("id"))

        if msg.get("kie_model"):
            self.kie_model = msg["kie_model"]
        if msg.get("memory_notes"):
            self.memory_notes = msg["memory_notes"]
        if msg.get("weather_location"):
            self.weather_location = msg["weather_location"]
        if isinstance(msg.get("history_turns"), (int, float)) and msg["history_turns"] > 0:
            # Portal-set override for this device; applies for the life of this session
            # only (BRIDGE_HISTORY_TURNS remains the default for anything that doesn't
            # send one -- e.g. a device that's never touched the portal's memory page).
            self.history_turns = int(msg["history_turns"])
            self._trim_history()

        pref = msg.get("llm")
        if self.accounts is not None:
            asked = pref if isinstance(pref, dict) else {}
            self.accounts.note_robot_pref(asked.get("provider", ""), asked.get("model", ""))
        if isinstance(pref, dict) and self.cfg.allow_device_model:
            if pref.get("provider"):
                self.provider = pref["provider"]
                # A provider change invalidates a model name meant for the old one.
                self.model = pref.get("model") or Config(
                    llm_provider=self.provider, llm_model="").resolved_llm_model()
            if pref.get("model"):
                self.model = pref["model"]
            if not self.model:
                log.warning("[%s] no model resolved for provider '%s' after device override -- "
                           "set a model explicitly in the portal's AI page", self.device_id,
                           self.provider)
            log.info("[%s] device asked for %s / %s", self.device_id, self.provider, self.model)

        await self.send_json({
            "type": "hello",
            "transport": "websocket",
            "version": msg.get("version", 1),
            "session_id": self.id,
            "audio_params": {
                "format": "opus",
                "sample_rate": self.cfg.tts_sample_rate,
                "channels": 1,
                "frame_duration": self.cfg.frame_duration_ms,
            },
        })

        if self.device_mcp is not None:
            # Fire-and-forget: discovery does its own error handling and just leaves
            # device_tools empty on failure, so a turn that starts before this finishes
            # simply runs without device tools rather than blocking on it.
            asyncio.ensure_future(self._discover_device_tools())

    async def _discover_device_tools(self):
        mcp_tools = await self.device_mcp.discover()
        self.device_tools = device_tools_to_anthropic(mcp_tools)

    # ------------------------------------------------------------------- listen --

    async def on_listen(self, msg: dict):
        state = msg.get("state")
        if state == "start":
            self.mode = msg.get("mode", "auto")
            self.listening = True
            self.aborted = False
            self.pcm = bytearray()
            self.saw_voice = False
            self.started_at = time.monotonic()
            self.last_voice_at = self.started_at
            log.debug("[%s] listening (%s)", self.device_id, self.mode)

        elif state == "stop":
            self.listening = False
            await self.finish_turn()

        elif state == "detect":
            # The wake word fired. The device is about to open a turn; anything still
            # playing is stale.
            self.aborted = True
            log.info("[%s] wake word: %s", self.device_id, msg.get("text", ""))

    async def on_audio(self, packet: bytes):
        if not self.listening or self.busy:
            return

        pcm = self.codec.decode(packet)
        if not pcm:
            return
        self.pcm.extend(pcm)

        now = time.monotonic()
        if speech.is_speech(pcm, self.cfg.vad_threshold):
            self.saw_voice = True
            self.last_voice_at = now

        if self.mode == "manual":
            return  # the device decides when this ends

        elapsed_ms = (now - self.started_at) * 1000
        silence_ms = (now - self.last_voice_at) * 1000

        if self.saw_voice and silence_ms >= self.cfg.vad_silence_ms:
            self.listening = False
            await self.finish_turn()
        elif elapsed_ms >= self.cfg.max_utterance_ms:
            log.info("[%s] utterance hit the length cap", self.device_id)
            self.listening = False
            await self.finish_turn()

    # --------------------------------------------------------------------- turn --

    async def finish_turn(self):
        """Recognise, answer, speak. Runs once per user utterance."""
        if self.busy:
            return
        audio, self.pcm = bytes(self.pcm), bytearray()

        # Under about a third of a second is a door closing, not a sentence.
        if not self.saw_voice or len(audio) < 16000 * 2 * 0.3:
            log.debug("[%s] ignoring %d bytes of near-silence", self.device_id, len(audio))
            return

        self.busy = True
        try:
            text = await speech.transcribe(self.http, self.cfg, audio, 16000)
            if not text:
                log.info("[%s] nothing recognised", self.device_id)
                return
            log.info("[%s] >> %s", self.device_id, text)
            await self.send_json({"type": "stt", "text": text})

            self.remember("user", text)
            tools = list(self.device_tools)
            if self.cfg.pihole_url:
                tools += pihole.PIHOLE_TOOLS
            if self.weather_location:
                tools += weather.WEATHER_TOOLS
            provider, model = self.provider, self.model
            if self.accounts is not None:
                provider, model = self.accounts.resolve(provider, model)
            reply = await llm_mod.complete_with_tools(
                self.http, self.cfg, provider, model, self.system_prompt(),
                self.history, device_tools=tools, call_device_tool=self._call_any_tool)
            # complete_with_tools appends the assistant turn(s) itself — including any
            # tool_use/tool_result round trip — so history is already up to date here.
            self._trim_history()
            if not reply:
                reply = "Sorry, I did not catch that."
            log.info("[%s] << %s", self.device_id, reply)

            await self.send_json({"type": "llm", "text": reply, "emotion": pick_emotion(reply)})
            await self.speak(reply)

        except Exception as exc:
            log.exception("[%s] turn failed", self.device_id)
            await self.send_json({
                "type": "alert", "status": "error",
                "message": str(exc)[:120], "emotion": "sad",
            })
        finally:
            self.busy = False

    async def speak(self, text: str):
        """Synthesise and stream, one sentence at a time.

        Sentence-by-sentence matters: the first words reach the speaker while the rest is
        still being synthesised, which turns a multi-second wait into a short one.
        """
        voice = self.persona.get("voice") or None
        speed = self.persona.get("speed") or None

        await self.send_json({"type": "tts", "state": "start"})
        try:
            for sentence in split_sentences(text):
                if self.aborted:
                    break
                await self.send_json({"type": "tts", "state": "sentence_start", "text": sentence})
                pcm = await speech.synthesize(self.http, self.cfg, sentence, voice,
                                              self.cfg.tts_sample_rate, speed)
                await self.send_audio(self.codec.encode(pcm))
        finally:
            await self.send_json({"type": "tts", "state": "stop"})


# ------------------------------------------------------------------------ helpers --

_SENTENCE = re.compile(r"[^.!?…]+[.!?…]+[\"')\]]*|\S[^.!?…]*$")


def split_sentences(text: str, max_chars: int = 220) -> List[str]:
    """Break a reply into speakable pieces.

    Long sentences are split on a comma rather than sent whole, because a TTS call that
    takes four seconds to come back is four seconds of silence on the device.
    """
    out: List[str] = []
    for raw in _SENTENCE.findall(text.strip()):
        piece = raw.strip()
        while len(piece) > max_chars:
            cut = piece.rfind(",", 0, max_chars)
            if cut < max_chars // 2:
                cut = piece.rfind(" ", 0, max_chars)
            if cut <= 0:
                cut = max_chars
            out.append(piece[:cut + 1].strip())
            piece = piece[cut + 1:].strip()
        if piece:
            out.append(piece)
    return out or [text.strip()]


# The six the firmware's skins actually implement. Anything else is ignored by the device,
# so guessing outside this set just loses the expression.
_EMOTION_HINTS = [
    ("happy", ("glad", "great", "wonderful", "love", "nice", "excellent", "!")),
    ("sad", ("sorry", "sad", "unfortunately", "afraid not", "cannot")),
    ("angry", ("no!", "stop", "wrong")),
    ("doubt", ("maybe", "perhaps", "not sure", "might", "?")),
    ("sleepy", ("tired", "sleep", "goodnight")),
]


def pick_emotion(text: str) -> str:
    low = text.lower()
    for emotion, hints in _EMOTION_HINTS:
        if any(h in low for h in hints):
            return emotion
    return "neutral"


# ------------------------------------------------------------------------- server --

async def ws_handler(request: web.Request) -> web.WebSocketResponse:
    cfg: Config = request.app["cfg"]

    if cfg.token:
        given = request.headers.get("Authorization", "").replace("Bearer ", "").strip()
        if not hmac.compare_digest(given.encode(), cfg.token.encode()):
            log.warning("rejected a connection with a bad token from %s", request.remote)
            return web.Response(status=401, text="bad token")

    ws = web.WebSocketResponse(heartbeat=30, max_msg_size=4 * 1024 * 1024)
    await ws.prepare(request)
    device_id = request.headers.get("Device-Id", request.remote or "?")
    session = Session(ws, cfg, device_id, request.app["http"], request.app["accounts"])
    log.info("[%s] connected (client %s)", device_id, request.headers.get("Client-Id", "?"))

    try:
        async for msg in ws:
            if msg.type == aiohttp.WSMsgType.BINARY:
                await session.on_audio(msg.data)
                continue
            if msg.type != aiohttp.WSMsgType.TEXT:
                continue

            try:
                payload = json.loads(msg.data)
            except ValueError:
                log.warning("[%s] non-JSON text frame", device_id)
                continue

            kind = payload.get("type")
            if kind == "hello":
                await session.on_hello(payload)
            elif kind == "listen":
                await session.on_listen(payload)
            elif kind == "abort":
                session.aborted = True
                log.info("[%s] abort (%s)", device_id, payload.get("reason", ""))
            elif kind == "mcp":
                # Every "mcp" message the device sends is a JSON-RPC response to a
                # request DeviceMcpClient made (initialize, tools/list, tools/call) —
                # the firmware never opens a call in this direction. Route by id.
                inner = payload.get("payload")
                if session.device_mcp is not None and isinstance(inner, dict):
                    session.device_mcp.on_message(inner)
                else:
                    log.debug("[%s] mcp (no client to route to): %s", device_id, str(payload)[:200])
            else:
                log.debug("[%s] unhandled '%s'", device_id, kind)

    finally:
        log.info("[%s] disconnected", device_id)

    return ws


async def health(request: web.Request) -> web.Response:
    cfg: Config = request.app["cfg"]
    store: AccountStore = request.app["accounts"]
    provider, model = store.resolve(cfg.llm_provider, cfg.resolved_llm_model())
    who = store.speech_source()
    return web.json_response({
        "ok": True,
        "llm": f"{provider}/{model}",
        "asr": f"{who['asr'] or 'nobody'}/{cfg.asr_model}",
        "tts": f"{who['tts'] or 'nobody'}/{cfg.tts_model}",
        "opus": speech.OPUS_AVAILABLE,
    })


ACCOUNTS_PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "accounts_page.html")


async def accounts_page(request: web.Request) -> web.StreamResponse:
    return web.FileResponse(ACCOUNTS_PAGE, headers={"Cache-Control": "no-store"})


async def accounts_status(request: web.Request) -> web.Response:
    # Readable cross-origin so the robot's own portal can show sign-in status. Holds no keys.
    return web.json_response(request.app["accounts"].status(), headers={
        "Access-Control-Allow-Origin": "*", "Cache-Control": "no-store"})


async def accounts_action(request: web.Request) -> web.Response:
    cfg: Config = request.app["cfg"]
    store: AccountStore = request.app["accounts"]

    # A custom header forces a CORS preflight, which this server never approves, so other
    # websites open in the same browser can't sign the bridge in or out.
    if request.headers.get("X-StackyChan") != "1":
        return web.json_response({"error": "Missing X-StackyChan header."}, status=403)
    if cfg.token:
        given = request.headers.get("Authorization", "").replace("Bearer ", "").strip()
        if not hmac.compare_digest(given.encode(), cfg.token.encode()):
            return web.json_response({"error": "Wrong bridge password."}, status=401)

    try:
        body = await request.json()
    except ValueError:
        body = None
    if not isinstance(body, dict):
        return web.json_response({"error": "Expected a JSON object."}, status=400)

    action = request.match_info["action"]
    provider = str(body.get("provider") or "")
    note = ""
    try:
        if action == "signin":
            note = await store.sign_in(request.app["http"], provider, str(body.get("key") or ""),
                                       str(body.get("workspace") or ""))
        elif action == "workspace":
            note = await store.set_workspace(request.app["http"], str(body.get("workspace") or ""))
        elif action == "signout":
            store.sign_out(provider)
        elif action == "choose":
            store.choose(provider, str(body.get("model") or ""))
        else:
            return web.json_response({"error": "Unknown action."}, status=404)
    except SignInError as exc:
        return web.json_response({"error": str(exc), "needs": exc.needs}, status=400)
    except OSError as exc:
        log.exception("could not save %s", store.path)
        return web.json_response({"error": f"The bridge couldn't save that: {exc}"}, status=500)

    out = store.status()
    out["note"] = note
    return web.json_response(out)


def build_app(cfg: Optional[Config] = None) -> web.Application:
    cfg = cfg or Config()
    app = web.Application()
    app["cfg"] = cfg
    app["accounts"] = AccountStore(cfg)

    async def _startup(a):
        a["http"] = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=60))

    async def _cleanup(a):
        await a["http"].close()

    app.on_startup.append(_startup)
    app.on_cleanup.append(_cleanup)

    app.router.add_get(cfg.path, ws_handler)
    app.router.add_get("/health", health)
    app.router.add_get("/", accounts_page)
    app.router.add_get("/api/accounts", accounts_status)
    app.router.add_post("/api/accounts/{action}", accounts_action)
    return app


def main():
    cfg = Config()
    logging.basicConfig(
        level=getattr(logging, cfg.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)-5s %(name)s  %(message)s",
        datefmt="%H:%M:%S")

    if not speech.OPUS_AVAILABLE:
        log.error("libopus is missing — install it (apt install libopus0) or no audio will flow")

    log.info("llm  : %s / %s", cfg.llm_provider, cfg.resolved_llm_model())
    log.info("asr  : %s / %s", cfg.asr_provider, cfg.asr_model)
    log.info("tts  : %s / %s", cfg.tts_provider, cfg.tts_model)
    log.info("listening on ws://%s:%d%s", cfg.host, cfg.port, cfg.path)
    log.info("sign in (Claude, ChatGPT, or a free provider) at http://%s:%d/", cfg.host, cfg.port)

    web.run_app(build_app(cfg), host=cfg.host, port=cfg.port, print=None)


if __name__ == "__main__":
    main()
