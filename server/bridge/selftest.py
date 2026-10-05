"""Run a fake StackyChan through a whole turn, with no keys, no network and no hardware.

What this actually proves
------------------------
The parts that are easy to get wrong and impossible to check by reading: the handshake,
the shape of every message the device will parse, the silence detector deciding a turn is
over, sentence splitting, and audio arriving as binary frames between tts/start and
tts/stop. Those are the pieces that would otherwise only fail on a desk, one flash cycle
at a time.

What it does not prove
----------------------
That Anthropic, Whisper or a TTS voice return what we expect, and that real Opus from a
real microphone decodes cleanly. Those need real credentials and the actual device. The
providers are stubbed here on purpose — a test that needs an API key is a test nobody runs.

    python selftest.py
"""

from __future__ import annotations

import asyncio
import json
import os
import stat
import sys
import tempfile

import aiohttp
from aiohttp import web

import speech
import llm as llm_mod
import bridge
import pihole
import weather
import xiaozhi_mcp_relay
import accounts
from config import Config, _parse_mcp_servers
from mcp_device import DeviceMcpClient, device_tools_to_anthropic

PORT = 8771
SPOKEN = "Hello there, how are you doing today?"
REPLY = "I am doing well, thank you. What can I do for you? Ask me anything at all."


class FakeCodec:
    """Stands in for Opus so the test needs no native library.

    Frames are passed through as raw PCM, which is all the layers under test care about —
    they count frames and check ordering, not codec output.
    """

    def __init__(self, in_rate=16000, out_rate=16000, frame_ms=60):
        self.frame_ms = frame_ms
        self.out_rate = out_rate

    @property
    def frame_samples(self):
        return int(self.out_rate * self.frame_ms / 1000)

    def decode(self, packet: bytes) -> bytes:
        return packet

    def encode(self, pcm: bytes):
        width = self.frame_samples * 2
        return [pcm[i:i + width] for i in range(0, len(pcm), width)] or [b"\x00" * width]


def install_stubs():
    speech.OpusCodec = FakeCodec

    async def fake_transcribe(http, cfg, pcm, rate):
        assert len(pcm) > 0, "ASR was handed no audio"
        return SPOKEN

    async def fake_synthesize(http, cfg, text, voice, rate, speed=None):
        # 200 ms of silence per sentence — enough to make several frames.
        return b"\x00" * int(rate * 0.2) * 2

    async def fake_complete_with_tools(http, cfg, provider, model, system, turns,
                                       device_tools=None, call_device_tool=None):
        # Mirrors the real contract: read the just-appended user turn, then append
        # exactly one assistant turn holding the reply. A caller that also appended the
        # reply itself would show two assistant turns in a row — this stub would not
        # catch that on its own, which is why bridge.py's finish_turn doesn't do it.
        assert turns and turns[-1]["content"] == SPOKEN, "history did not reach the model"
        assert "StackyChan" in system or system, "no system prompt"
        fake_complete_with_tools.seen = {
            "provider": provider, "model": model, "system": system,
            "device_tools": device_tools, "has_caller": call_device_tool is not None,
        }
        turns.append({"role": "assistant", "content": REPLY})
        return REPLY

    speech.transcribe = fake_transcribe
    speech.synthesize = fake_synthesize
    llm_mod.complete_with_tools = fake_complete_with_tools
    bridge.speech = speech
    bridge.llm_mod = llm_mod
    return fake_complete_with_tools


async def run() -> int:
    seen_complete = install_stubs()

    cfg = Config()
    cfg.host, cfg.port, cfg.path = "127.0.0.1", PORT, "/xiaozhi/v1/"
    cfg.token = ""
    cfg.vad_silence_ms = 200      # keep the test quick
    cfg.tts_sample_rate = 16000
    cfg.llm_provider = "anthropic"
    cfg.llm_model = "claude-sonnet-5"
    # The device-tool relay is exercised on its own below, against a synchronous fake
    # transport. Leaving it on here would have on_hello spawn a real discovery attempt
    # against a fake device that never answers MCP messages — an 8s timeout for nothing.
    cfg.enable_device_tools = False
    cfg.accounts_file = os.path.join(tempfile.mkdtemp(prefix="selftest-accounts-"), "accounts.json")

    app = bridge.build_app(cfg)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", PORT)
    await site.start()

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    try:
        async with aiohttp.ClientSession() as http:
            async with http.ws_connect(
                f"http://127.0.0.1:{PORT}/xiaozhi/v1/",
                headers={"Device-Id": "aa:bb:cc:dd:ee:ff", "Client-Id": "selftest",
                         "Protocol-Version": "1"}) as ws:

                print("\nhandshake")
                await ws.send_str(json.dumps({
                    "type": "hello", "version": 1,
                    "features": {"mcp": True},
                    "transport": "websocket",
                    "audio_params": {"format": "opus", "sample_rate": 16000,
                                     "channels": 1, "frame_duration": 60},
                    "persona_id": "max",
                    "persona": {"id": "max", "name": "MAX",
                                "prompt": "You are MAX, a fast-talking television host."},
                    "llm": {"provider": "anthropic", "model": "claude-sonnet-5"},
                    "memory_notes": "Lives in Chicago.",
                }))
                hello = json.loads((await asyncio.wait_for(ws.receive(), 5)).data)
                check(hello.get("type") == "hello", "server answers hello")
                check(hello.get("transport") == "websocket", "transport is websocket")
                check(bool(hello.get("session_id")), "hello carries a session id")
                check(hello.get("audio_params", {}).get("sample_rate") == 16000,
                      "hello states the sample rate the device should decode at")

                print("\nturn")
                await ws.send_str(json.dumps({"type": "listen", "state": "start",
                                              "mode": "auto"}))

                # 600 ms of loud audio, then silence, which is what the detector watches for.
                loud = (b"\x00\x20" * 480)      # one 60 ms frame at 16 kHz, non-trivial RMS
                for _ in range(10):
                    await ws.send_bytes(loud)
                    await asyncio.sleep(0.01)
                quiet = b"\x00\x00" * 480
                for _ in range(30):
                    await ws.send_bytes(quiet)
                    await asyncio.sleep(0.02)

                got_stt = got_llm = tts_start = tts_stop = None
                sentences, audio_frames = [], 0

                while True:
                    try:
                        msg = await asyncio.wait_for(ws.receive(), 10)
                    except asyncio.TimeoutError:
                        break
                    if msg.type == aiohttp.WSMsgType.BINARY:
                        audio_frames += 1
                        continue
                    if msg.type != aiohttp.WSMsgType.TEXT:
                        break
                    payload = json.loads(msg.data)
                    kind = payload.get("type")
                    if kind == "stt":
                        got_stt = payload
                    elif kind == "llm":
                        got_llm = payload
                    elif kind == "tts":
                        state = payload.get("state")
                        if state == "start":
                            tts_start = payload
                        elif state == "sentence_start":
                            sentences.append(payload.get("text", ""))
                        elif state == "stop":
                            tts_stop = payload
                            break

                check(got_stt is not None and got_stt.get("text") == SPOKEN,
                      "stt reports what was heard")
                check(got_llm is not None, "llm message sent")
                check(got_llm and got_llm.get("emotion") in
                      ("neutral", "happy", "sad", "angry", "doubt", "sleepy"),
                      "emotion is one the skins implement")
                check(tts_start is not None, "tts/start sent")
                check(len(sentences) >= 2, f"reply split into sentences (got {len(sentences)})")
                check(audio_frames > 0, f"audio frames streamed (got {audio_frames})")
                check(tts_stop is not None, "tts/stop sent")
                check(all(payload.get("session_id") for payload in
                          [got_stt, got_llm, tts_start, tts_stop] if payload),
                      "every message carries the session id")

                print("\npersona and model routing")
                seen = getattr(seen_complete, "seen", {})
                check(seen.get("provider") == "anthropic", "provider taken from the device hello")
                check(seen.get("model") == "claude-sonnet-5", "model taken from the device hello")
                check("MAX" in seen.get("system", ""),
                      "the selected persona became the system prompt")
                check("Lives in Chicago" in seen.get("system", ""),
                      "memory notes from hello are appended to the system prompt")
                robot = app["accounts"].robot_pref or {}
                check(robot.get("provider") == "anthropic",
                      "the robot's own provider request is recorded for the sign-in page")

        print("\nhealth endpoint")
        async with aiohttp.ClientSession() as http:
            async with http.get(f"http://127.0.0.1:{PORT}/health") as r:
                body = await r.json()
                check(r.status == 200 and body.get("ok") is True, "health responds")

    finally:
        await runner.cleanup()

    print()
    if failures:
        print(f"{len(failures)} FAILED: " + "; ".join(failures))
        return 1
    print("all checks passed")
    return 0


def test_sentence_splitting() -> int:
    """Splitting is pure and worth checking directly rather than through a socket."""
    print("\nsentence splitting")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    check(bridge.split_sentences("One. Two! Three?") == ["One.", "Two!", "Three?"],
          "splits on terminators")
    check(bridge.split_sentences("No terminator here") == ["No terminator here"],
          "keeps a trailing fragment")
    check(bridge.split_sentences("") == [""], "empty input does not explode")
    long_one = "a, " * 200
    check(all(len(p) <= 240 for p in bridge.split_sentences(long_one)),
          "long run-on is broken into speakable pieces")
    check(bridge.pick_emotion("Sorry, I cannot do that") == "sad", "emotion picked from wording")
    return 1 if failures else 0


def test_mcp_servers_config() -> int:
    """The BRIDGE_MCP_SERVERS parser — this is the whole 'room for more MCPs' knob."""
    print("\nMCP server config parsing")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    check(_parse_mcp_servers("") == [], "empty string yields no servers")
    check(_parse_mcp_servers("not json") == [], "malformed JSON is ignored, not fatal")
    check(_parse_mcp_servers('{"name":"x","url":"y"}') == [],
          "a bare object (not an array) is rejected")

    good = _parse_mcp_servers(
        '[{"name":"weather","url":"https://mcp.example.com/sse"},'
        ' {"name":"home","url":"https://home.example.com/mcp","token":"secret"}]')
    check(len(good) == 2, f"two well-formed entries parse (got {len(good)})")
    check(good[0] == {"name": "weather", "url": "https://mcp.example.com/sse", "token": ""},
          "entry without a token defaults to empty string")
    check(good[1]["token"] == "secret", "entry with a token keeps it")

    partial = _parse_mcp_servers('[{"name":"ok","url":"https://x"}, {"name":"broken"}]')
    check(len(partial) == 1 and partial[0]["name"] == "ok",
          "one bad entry among good ones is skipped, not fatal to the rest")

    return 1 if failures else 0


async def test_device_mcp_relay() -> int:
    """The device-tool relay: discover tools over the mcp websocket shape, call one.

    Uses a synchronous fake transport rather than a real socket — send_text() answers
    itself immediately via on_message(), so this runs in milliseconds and needs no
    hardware. What it proves: DeviceMcpClient speaks the exact JSON-RPC-over-
    {"type":"mcp","payload":...} envelope firmware/xiaozhi-esp32/main/mcp_server.cc
    expects, and that a discovered tool converts into a valid Anthropic tool definition.
    """
    print("\ndevice MCP relay")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    sent = []

    async def fake_send_text(text: str):
        sent.append(text)
        payload = json.loads(text)["payload"]
        method = payload.get("method")
        request_id = payload["id"]

        if method == "initialize":
            response = {"protocolVersion": "2024-11-05", "capabilities": {"tools": {}}}
        elif method == "tools/list":
            response = {"tools": [{
                "name": "set_led_color",
                "description": "Set the RGB LED strip colors",
                "inputSchema": {
                    "type": "object",
                    "properties": {"left": {"type": "string"}, "right": {"type": "string"}},
                    "required": ["left", "right"],
                },
            }]}
        elif method == "tools/call":
            check(payload["params"]["name"] == "set_led_color", "call reaches the right tool")
            response = {"content": [{"type": "text", "text": "ok"}], "isError": False}
        else:
            response = {}

        # Deliver "asynchronously" (next tick) the way a real websocket reply would —
        # if on_message ran synchronously inside send_text, a bug where the client
        # awaited its own send before registering the pending future could hide.
        async def deliver():
            client.on_message({"jsonrpc": "2.0", "id": request_id, "result": response})
        asyncio.get_event_loop().call_soon(lambda: asyncio.ensure_future(deliver()))

    client = DeviceMcpClient(fake_send_text)
    tools = await client.discover()
    check(len(tools) == 1 and tools[0]["name"] == "set_led_color", "discover() returns the tool")
    check(len(sent) == 2, f"discover sends initialize + tools/list (got {len(sent)} messages)")

    anthropic_tools = device_tools_to_anthropic(tools)
    check(anthropic_tools[0]["name"] == "device_set_led_color",
          "converted tool is prefixed to avoid name collisions")
    check(anthropic_tools[0]["input_schema"]["required"] == ["left", "right"],
          "the device's inputSchema carries through as input_schema unchanged")

    result = await client.call_tool("set_led_color", {"left": "#FF0000", "right": "#00FF00"})
    check(result == "ok", f"call_tool returns the device's text result (got {result!r})")

    return 1 if failures else 0


class _FakeAnthropicResponse:
    def __init__(self, body: dict):
        self.status = 200
        self._body = body

    async def json(self):
        return self._body

    async def __aenter__(self):
        return self

    async def __aexit__(self, *exc):
        return False


class _FakeAnthropicSession:
    """Stands in for aiohttp.ClientSession against api.anthropic.com/v1/messages.

    Each call to post() pops the next canned response off `responses` and records the
    request body, so a test can assert on exactly what llm.complete_with_tools sent —
    the tools array, the mcp_servers array, the beta header — without a real key.
    """

    def __init__(self, responses):
        self.responses = list(responses)
        self.requests = []

    def post(self, url, json=None, headers=None):
        self.requests.append({"url": url, "json": json, "headers": headers})
        return _FakeAnthropicResponse(self.responses.pop(0))


async def test_llm_tool_loop() -> int:
    """llm.complete_with_tools against a fake Anthropic endpoint.

    Exercises the part selftest's other checks don't reach: request-body assembly
    (tools + mcp_servers + the mcp-client beta header) and the round-trip where a
    device_-prefixed tool_use block is executed via call_device_tool and its result is
    fed back for a second round.
    """
    print("\nllm.complete_with_tools request loop")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    cfg = Config()
    cfg.anthropic_key = "sk-ant-test-not-real"
    cfg.mcp_servers = [{"name": "weather", "url": "https://mcp.example.com/sse", "token": ""}]

    device_tools = [{
        "name": "device_set_led_color",
        "description": "Set the RGB LED strip colors",
        "input_schema": {"type": "object", "properties": {}},
    }]

    calls = []

    async def fake_call_device_tool(name, arguments):
        calls.append((name, arguments))
        return "led set"

    round_one = {
        "stop_reason": "tool_use",
        "content": [
            {"type": "text", "text": "Let me do that."},
            {"type": "tool_use", "id": "toolu_1", "name": "device_set_led_color",
             "input": {"left": "#FF0000"}},
        ],
    }
    round_two = {
        "stop_reason": "end_turn",
        "content": [{"type": "text", "text": "Done, it's red now."}],
    }
    fake_session = _FakeAnthropicSession([round_one, round_two])

    turns = [{"role": "user", "content": "Turn the light red"}]
    reply = await llm_mod.complete_with_tools(
        fake_session, cfg, "anthropic", "claude-sonnet-5", "You are StackyChan.",
        turns, device_tools=device_tools, call_device_tool=fake_call_device_tool)

    check(reply == "Done, it's red now.", f"final reply text returned (got {reply!r})")
    check(len(fake_session.requests) == 2, f"exactly two HTTP round trips (got {len(fake_session.requests)})")
    check(calls == [("set_led_color", {"left": "#FF0000"})],
          f"device tool called with the bare (unprefixed) name and model's input (got {calls})")

    first_body = fake_session.requests[0]["json"]
    check(any(t.get("name") == "device_set_led_color" for t in first_body.get("tools", [])),
          "device tool is offered to the model")
    check(any(t.get("type") == "mcp_toolset" and t.get("mcp_server_name") == "weather"
              for t in first_body.get("tools", [])),
          "configured MCP server is offered as an mcp_toolset")
    check(first_body.get("mcp_servers") == [
        {"type": "url", "name": "weather", "url": "https://mcp.example.com/sse"}],
        "mcp_servers array matches config, with no token field when none is set")
    check(fake_session.requests[0]["headers"].get("anthropic-beta") == llm_mod.ANTHROPIC_BETA_MCP,
          "MCP connector beta header is sent")

    # turns should now hold: user, assistant(tool_use), user(tool_result), assistant(text) —
    # exactly one assistant append per round, matching the documented contract.
    roles = [t["role"] for t in turns]
    check(roles == ["user", "assistant", "user", "assistant"],
          f"history holds the full tool round trip, no double-appends (got {roles})")
    last_content = turns[-1]["content"]
    check(isinstance(last_content, list) and last_content[0]["text"] == "Done, it's red now.",
          "final assistant turn holds the reply text")

    return 1 if failures else 0


class _FakeOpenAiResponse:
    def __init__(self, body: dict, status: int = 200):
        self.status = status
        self._body = body

    async def json(self):
        return self._body

    async def __aenter__(self):
        return self

    async def __aexit__(self, *exc):
        return False


class _FakeOpenAiSession:
    """Stands in for aiohttp.ClientSession against .../chat/completions.

    Mirrors _FakeAnthropicSession: pops a canned response per post(), records the
    request body so a test can assert on the OpenAI-shaped tools array it sent.
    """

    def __init__(self, responses):
        self.responses = list(responses)
        self.requests = []

    def post(self, url, json=None, headers=None):
        self.requests.append({"url": url, "json": json, "headers": headers})
        nxt = self.responses.pop(0)
        if isinstance(nxt, tuple):          # (status, body) for error responses
            return _FakeOpenAiResponse(nxt[1], nxt[0])
        return _FakeOpenAiResponse(nxt)


async def test_llm_tool_loop_openai() -> int:
    """llm.complete_with_tools against a fake OpenAI chat-completions endpoint.

    Mirrors test_llm_tool_loop() above: request-body assembly (OpenAI's
    {"type":"function","function":{...}} tool shape) and the round-trip where a
    device_-prefixed tool call is executed via call_device_tool and its result fed
    back for a second round.
    """
    print("\nllm.complete_with_tools (OpenAI) request loop")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    cfg = Config()
    cfg.openai_key = "sk-test-not-real"

    device_tools = [{
        "name": "device_set_led_color",
        "description": "Set the RGB LED strip colors",
        "input_schema": {"type": "object", "properties": {}},
    }]

    calls = []

    async def fake_call_device_tool(name, arguments):
        calls.append((name, arguments))
        return "led set"

    round_one = {"choices": [{"message": {
        "role": "assistant", "content": None,
        "tool_calls": [{"id": "call_1", "type": "function",
                        "function": {"name": "device_set_led_color",
                                     "arguments": '{"left": "#FF0000"}'}}],
    }}]}
    round_two = {"choices": [{"message": {
        "role": "assistant", "content": "Done, it's red now.",
    }}]}
    fake_session = _FakeOpenAiSession([round_one, round_two])

    turns = [{"role": "user", "content": "Turn the light red"}]
    reply = await llm_mod.complete_with_tools(
        fake_session, cfg, "openai", "gpt-4o-mini", "You are StackyChan.",
        turns, device_tools=device_tools, call_device_tool=fake_call_device_tool)

    check(reply == "Done, it's red now.", f"final reply text returned (got {reply!r})")
    check(len(fake_session.requests) == 2, f"exactly two HTTP round trips (got {len(fake_session.requests)})")
    check(calls == [("set_led_color", {"left": "#FF0000"})],
          f"device tool called with the bare (unprefixed) name and parsed arguments (got {calls})")

    first_body = fake_session.requests[0]["json"]
    check(first_body.get("tools") == [{"type": "function", "function": {
        "name": "device_set_led_color", "description": "Set the RGB LED strip colors",
        "parameters": {"type": "object", "properties": {}},
    }}], "tools array is OpenAI's function-tool shape")

    roles = [t["role"] for t in turns]
    check(roles == ["user", "assistant", "tool", "assistant"],
          f"history holds the full tool round trip (got {roles})")

    check("max_completion_tokens" in first_body and "max_tokens" not in first_body,
          "api.openai.com gets max_completion_tokens (reasoning models reject max_tokens)")

    cfg.groq_key = "gsk_test-not-real"
    groq_session = _FakeOpenAiSession([round_two])
    await llm_mod.complete_with_tools(
        groq_session, cfg, "groq", "llama-3.3-70b-versatile", "You are StackyChan.",
        [{"role": "user", "content": "hi"}], device_tools=device_tools,
        call_device_tool=fake_call_device_tool)
    groq_req = groq_session.requests[0]
    check("max_tokens" in groq_req["json"] and "max_completion_tokens" not in groq_req["json"],
          "other OpenAI-compatible providers keep max_tokens")
    check(groq_req["url"] == "https://api.groq.com/openai/v1/chat/completions"
          and groq_req["headers"]["Authorization"] == "Bearer gsk_test-not-real",
          f"Groq goes to Groq's own endpoint with Groq's own key (got {groq_req['url']})")

    # A free model that can't do tools: the 400 is retried once without tools.
    cfg.gemini_key = "AIza-test"
    no_tools = _FakeOpenAiSession([
        (400, {"error": {"message": "Function calling is not enabled for this model"}}),
        round_two])
    history = [{"role": "user", "content": "Turn the light red"}]
    reply = await llm_mod.complete_with_tools(
        no_tools, cfg, "gemini", "gemma-3-27b-it", "x", history,
        device_tools=device_tools, call_device_tool=fake_call_device_tool)
    check(reply == "Done, it's red now." and len(no_tools.requests) == 2
          and "tools" not in no_tools.requests[1]["json"],
          "a model that rejects tools is asked again without them and still answers")
    check(no_tools.requests[0]["url"].startswith("https://generativelanguage.googleapis.com/v1beta/openai/"),
          "Gemini uses Google's OpenAI-compatible endpoint")
    check([t["role"] for t in history] == ["user", "assistant"],
          "the retry leaves a clean history (one assistant turn)")

    cfg.ollama_base = ""
    cfg.openai_key = "sk-test-not-real"
    ollama = _FakeOpenAiSession([round_two])
    await llm_mod.complete(ollama, cfg, "ollama", "llama3.2", "x", [{"role": "user", "content": "hi"}])
    check(ollama.requests[0]["url"] == "http://127.0.0.1:11434/v1/chat/completions",
          f"Ollama with no address set goes to the local default (got {ollama.requests[0]['url']})")

    rejected = _FakeOpenAiSession([(401, {"error": {"message": "Invalid API Key"}})])
    try:
        await llm_mod.complete(rejected, cfg, "groq", "llama-3.1-8b-instant", "x",
                               [{"role": "user", "content": "hi"}])
        check(False, "a 401 raises")
    except llm_mod.LlmError as exc:
        check("Groq rejected the key" in str(exc) and len(str(exc)) <= 120,
              f"a rejected key names the provider, short enough for the device (got {exc})")

    ws_cfg = Config()
    ws_cfg.anthropic_key, ws_cfg.anthropic_workspace = "sk-ant-test", "wrkspc_test"
    check(llm_mod._anthropic_headers(ws_cfg).get("anthropic-workspace-id") == "wrkspc_test",
          "an Anthropic workspace ID goes out as the anthropic-workspace-id header")
    ws_cfg.anthropic_workspace = ""
    check("anthropic-workspace-id" not in llm_mod._anthropic_headers(ws_cfg),
          "no workspace ID, no header")

    cfg.openai_key = ""
    try:
        await llm_mod.complete_with_tools(_FakeOpenAiSession([]), cfg, "openai", "gpt-4o-mini",
                                          "x", [{"role": "user", "content": "hi"}])
        check(False, "a missing ChatGPT key raises")
    except llm_mod.LlmError as exc:
        check("ChatGPT isn't signed in" in str(exc) and len(str(exc)) <= 120,
              f"a missing ChatGPT key says to sign in, short enough for the device (got {exc})")

    return 1 if failures else 0


def test_speech_routing() -> int:
    """Who does hearing and the voice: explicit key, then ChatGPT, then Groq for free."""
    print("\nspeech routing (ASR/TTS source)")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    cfg = Config()
    cfg.asr_key = cfg.tts_key = cfg.openai_key = cfg.groq_key = ""
    try:
        speech._speech_route(cfg, "asr")
        check(False, "no speech key raises")
    except RuntimeError as exc:
        check("ChatGPT or Groq" in str(exc) and len(str(exc)) <= 120,
              f"with nobody signed in, the message names both options (got {exc})")

    cfg.groq_key = "gsk_x"
    base, key, model, source = speech._speech_route(cfg, "asr")
    check(source == "groq" and model == "whisper-large-v3-turbo" and "groq.com" in base,
          f"Groq alone gives free hearing via Whisper (got {source}/{model})")
    base, key, model, source = speech._speech_route(cfg, "tts")
    check(source == "groq" and model == "playai-tts", f"Groq alone gives a voice (got {source}/{model})")

    cfg.openai_key = "sk-x"
    check(speech._speech_route(cfg, "asr")[3] == "openai", "ChatGPT wins over Groq for speech")
    cfg.asr_key = "custom"
    cfg.asr_base = "http://whisper.example.com/v1"
    base, key, model, source = speech._speech_route(cfg, "asr")
    check(source == "custom" and base == "http://whisper.example.com/v1", "an explicit BRIDGE_ASR_KEY wins over both")
    check(speech._GROQ_VOICES.get("alloy", "").endswith("-PlayAI"), "OpenAI voice names map onto PlayAI voices")

    return 1 if failures else 0


class _FakeHttpResponse:
    def __init__(self, status: int, body: dict):
        self.status = status
        self._body = body

    async def json(self):
        return self._body

    async def __aenter__(self):
        return self

    async def __aexit__(self, *exc):
        return False


class _FakeHttpSession:
    """A generic get()/post() fake for pihole.py/weather.py — both call plain
    aiohttp methods (no Anthropic/OpenAI-specific request shape to assert on), so this
    just queues responses in call order and records each request for inspection."""

    def __init__(self, responses):
        self.responses = list(responses)
        self.requests = []

    def get(self, url, params=None, headers=None, timeout=None):
        self.requests.append({"method": "GET", "url": url, "params": params, "headers": headers})
        status, body = self.responses.pop(0)
        return _FakeHttpResponse(status, body)

    def post(self, url, json=None, headers=None):
        self.requests.append({"method": "POST", "url": url, "json": json, "headers": headers})
        status, body = self.responses.pop(0)
        return _FakeHttpResponse(status, body)


async def test_pihole() -> int:
    print("\npihole.py against a fake Pihole v5")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    cfg = Config()
    cfg.pihole_url = "http://pi.hole"
    cfg.pihole_api_token = "test-token"
    cfg.pihole_api_version = "5"

    stats_session = _FakeHttpSession([
        (200, {"ads_blocked_today": 42, "dns_queries_today": 500, "ads_percentage_today": 8.4}),
    ])
    text = await pihole.get_stats(stats_session, cfg)
    check("42" in text and "500" in text, f"stats text mentions blocked/total counts (got {text!r})")

    disable_session = _FakeHttpSession([(200, {"status": "disabled"})])
    text = await pihole.disable(disable_session, cfg, 15)
    check("15 minute" in text, f"disable confirms the duration (got {text!r})")
    sent_params = disable_session.requests[0]["params"]
    check(sent_params.get("disable") == "900", f"disable=<seconds> is minutes*60 (got {sent_params})")

    return 1 if failures else 0


async def test_weather() -> int:
    print("\nweather.py against fake Open-Meteo")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    cfg = Config()
    cfg.weather_location = "Testville"
    weather._geocode_cache.clear()

    session = _FakeHttpSession([
        (200, {"results": [{"latitude": 41.88, "longitude": -87.63}]}),
        (200, {"current": {"temperature_2m": 72.3, "weather_code": 1, "wind_speed_10m": 5.2}}),
    ])
    text = await weather.get_current(session, cfg)
    check("72" in text, f"current weather mentions the temperature (got {text!r})")
    check(len(session.requests) == 2,
          f"geocode then forecast, two calls (got {len(session.requests)})")

    # Second call for the same location should hit the cache, not geocode again.
    session2 = _FakeHttpSession([
        (200, {"current": {"temperature_2m": 70.0, "weather_code": 2, "wind_speed_10m": 3.0}}),
    ])
    await weather.get_current(session2, cfg)
    check(len(session2.requests) == 1, "cached location skips a repeat geocode call")

    return 1 if failures else 0


async def test_xiaozhi_mcp_relay() -> int:
    """xiaozhi_mcp_relay's request handling, without a real xiaozhi.me connection.

    Exercises the pure logic (tools/list shape, tools/call dispatch, notification/
    unknown-method handling) directly — the actual WebSocket connection to
    wss://api.xiaozhi.me/mcp/ needs a live, owner-provided token and can't be tested here.
    """
    print("\nxiaozhi_mcp_relay request handling")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    cfg = Config()
    cfg.pihole_url = "http://pi.hole"
    cfg.pihole_api_token = "test-token"
    cfg.pihole_api_version = "5"

    tools = xiaozhi_mcp_relay._tools_list()
    names = [t["name"] for t in tools]
    check("pihole_get_stats" in names and "weather_get_current" in names,
          f"tools/list offers both Pihole and weather tools (got {names})")
    check(all("inputSchema" in t for t in tools),
          "tool entries use MCP's inputSchema key, not Anthropic's input_schema")

    reply = await xiaozhi_mcp_relay._handle_request(
        _FakeHttpSession([(200, {"status": "disabled"})]), cfg,
        {"jsonrpc": "2.0", "id": 7, "method": "tools/call",
         "params": {"name": "pihole_disable", "arguments": {"minutes": 10}}})
    check(reply and reply.get("id") == 7, f"tools/call reply carries the request id (got {reply})")
    check(reply and "10 minute" in reply["result"]["content"][0]["text"],
          f"tools/call dispatches to pihole.call_tool (got {reply})")

    notif_reply = await xiaozhi_mcp_relay._handle_request(None, cfg, {"jsonrpc": "2.0", "method": "notifications/initialized"})
    check(notif_reply is None, "a notification (no id) gets no reply")

    unknown_reply = await xiaozhi_mcp_relay._handle_request(None, cfg, {"jsonrpc": "2.0", "id": 9, "method": "prompts/list"})
    check(unknown_reply and "error" in unknown_reply,
          f"an unrecognised method gets a JSON-RPC error, not a crash (got {unknown_reply})")

    return 1 if failures else 0

ANTHROPIC_MODELS = {"data": [
    {"type": "model", "id": "claude-opus-5", "display_name": "Claude Opus 5"},
    {"type": "model", "id": "claude-sonnet-5", "display_name": "Claude Sonnet 5"},
    {"type": "model", "id": "claude-haiku-4-5-20251001", "display_name": "Claude Haiku 4.5"},
]}
OPENAI_MODELS = {"data": [
    {"id": "whisper-1"}, {"id": "gpt-4o-mini-tts"}, {"id": "text-embedding-3-small"},
    {"id": "gpt-4o"}, {"id": "gpt-5-mini"}, {"id": "gpt-4o-mini"},
    {"id": "gpt-4o-realtime-preview"}, {"id": "dall-e-3"}, {"id": "o3-mini"},
]}


GEMINI_MODELS = {"data": [
    {"id": "models/gemini-2.5-flash", "object": "model"},
    {"id": "models/gemini-2.5-flash-lite", "object": "model"},
    {"id": "models/gemini-embedding-001", "object": "model"},
    {"id": "models/gemini-2.5-flash-preview-tts", "object": "model"},
    {"id": "models/imagen-4.0-generate-001", "object": "model"},
]}
OPENROUTER_MODELS = {"data": [
    {"id": "openai/gpt-4o", "name": "OpenAI: GPT-4o"},
    {"id": "meta-llama/llama-3.3-70b-instruct:free", "name": "Meta: Llama 3.3 70B Instruct (free)"},
    {"id": "google/gemma-3-27b-it:free", "name": "Google: Gemma 3 27B (free)"},
]}
OLLAMA_MODELS = {"data": [{"id": "llama3.2:latest"}, {"id": "qwen2.5:7b"}]}
GROQ_MODELS = {"data": [
    {"id": "whisper-large-v3-turbo"}, {"id": "llama-3.3-70b-versatile"}, {"id": "playai-tts"},
    {"id": "llama-3.1-8b-instant"}, {"id": "meta-llama/llama-guard-4-12b"},
]}
WORKSPACE_ERROR = {"type": "error", "error": {"type": "invalid_request_error", "message":
    "This API key is not scoped to a workspace, so this request must include the "
    "anthropic-workspace-id header with the ID of the workspace to use."}}


def _fresh_cfg(**keys) -> Config:
    cfg = Config()
    cfg.anthropic_key = keys.get("anthropic", "")
    cfg.anthropic_workspace = ""
    cfg.openai_key = keys.get("openai", "")
    cfg.groq_key = cfg.gemini_key = cfg.openrouter_key = cfg.cerebras_key = cfg.mistral_key = ""
    cfg.ollama_base = ""
    cfg.asr_key = cfg.tts_key = ""
    cfg.token = keys.get("token", "")
    cfg.accounts_file = os.path.join(tempfile.mkdtemp(prefix="selftest-accounts-"), "accounts.json")
    return cfg


async def test_accounts() -> int:
    """Sign-in store: key checks, persistence, choosing, sign-out, env keys."""
    print("\naccounts sign-in store")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    claude_key = "sk-ant-api03-" + "A" * 40 + "wxyz"
    openai_key = "sk-proj-" + "B" * 40 + "1234"

    cfg = _fresh_cfg()
    store = accounts.AccountStore(cfg)
    check(store.active is None and not store.signed_in("anthropic"), "starts signed out")

    session = _FakeHttpSession([(200, ANTHROPIC_MODELS)])
    await store.sign_in(session, "anthropic", "  " + claude_key + "\n")
    req = session.requests[0]
    check(req["url"] == "https://api.anthropic.com/v1/models"
          and req["headers"].get("x-api-key") == claude_key,
          "Claude key is checked against Anthropic's model list, whitespace trimmed")
    check(cfg.anthropic_key == claude_key, "a signed-in key is live in the config, no restart")
    check(store.active == {"provider": "anthropic", "model": "claude-haiku-4-5-20251001"},
          f"first sign-in answers automatically, with the fast model first (got {store.active})")

    mode = stat.S_IMODE(os.stat(cfg.accounts_file).st_mode)
    check(mode == 0o600, f"accounts file is owner-only (got {oct(mode)})")

    status_text = json.dumps(store.status())
    check(claude_key not in status_text and "wxyz" in status_text,
          "status shows only the last four characters of a key")
    check(store.status()["speech_ready"] is False,
          "speech is flagged not ready with only Claude signed in")

    rejected = _FakeHttpSession([(401, {"error": {"message": "Incorrect API key provided"}})])
    try:
        await store.sign_in(rejected, "openai", openai_key)
        check(False, "a rejected key raises")
    except accounts.SignInError as exc:
        check("isn't valid" in str(exc), f"a rejected key gives a plain message (got {exc})")
    check(cfg.openai_key == "" and "openai" not in store.saved, "a rejected key is not saved")

    wrong_box = _FakeHttpSession([])
    try:
        await store.sign_in(wrong_box, "openai", claude_key)
        check(False, "a Claude key in the ChatGPT box raises")
    except accounts.SignInError as exc:
        check("Claude" in str(exc) and not wrong_box.requests,
              f"a Claude key pasted in the ChatGPT box is caught before any request (got {exc})")

    await store.sign_in(_FakeHttpSession([(200, OPENAI_MODELS)]), "openai", openai_key)
    ids = [m["id"] for m in store.models("openai")]
    check(ids[0] == "gpt-4o-mini" and "whisper-1" not in ids and "gpt-4o-mini-tts" not in ids
          and "text-embedding-3-small" not in ids and "gpt-4o-realtime-preview" not in ids
          and "dall-e-3" not in ids,
          f"ChatGPT model list keeps chat models only, fast pick first (got {ids})")
    check(any(m["id"] == "o3-mini" and m["reasoning"] for m in store.models("openai")),
          "reasoning models are marked")
    check(store.active["provider"] == "anthropic", "signing in a second account doesn't switch who answers")
    check(store.status()["speech_ready"] is True, "speech is ready once ChatGPT is signed in")

    store.choose("openai")
    check(store.resolve("anthropic", "claude-sonnet-5") == ("openai", "gpt-4o-mini"),
          "the page's choice overrides the robot's own request")

    reloaded_cfg = _fresh_cfg()
    reloaded_cfg.accounts_file = cfg.accounts_file
    reloaded = accounts.AccountStore(reloaded_cfg)
    check(reloaded_cfg.openai_key == openai_key and reloaded.active["provider"] == "openai",
          "keys and the choice survive a bridge restart")

    store.sign_out("openai")
    check(cfg.openai_key == "" and store.active["provider"] == "anthropic",
          f"signing out of the provider that answers falls back to the other one (got {store.active})")
    store.sign_out("anthropic")
    check(store.active is None and cfg.anthropic_key == "", "signing out of both leaves nobody answering")
    check(store.resolve("anthropic", "claude-sonnet-5") == ("anthropic", "claude-sonnet-5"),
          "with no choice made, the robot's own request applies again")

    env_cfg = _fresh_cfg(openai="sk-env-" + "C" * 30)
    env_store = accounts.AccountStore(env_cfg)
    check(env_store.status()["providers"]["openai"]["source"] == "env",
          "a bridge.env key shows as signed in from the env file")
    try:
        env_store.sign_out("openai")
        check(False, "signing out of an env key raises")
    except accounts.SignInError as exc:
        check("bridge.env" in str(exc), f"signing out of an env key explains where it lives (got {exc})")

    # ---- the free providers
    free_cfg = _fresh_cfg()
    free = accounts.AccountStore(free_cfg)
    groq_key = "gsk_" + "D" * 40 + "9876"
    try:
        await free.sign_in(_FakeHttpSession([]), "openai", groq_key)
        check(False, "a Groq key in the ChatGPT box raises")
    except accounts.SignInError as exc:
        check("Groq" in str(exc), f"a Groq key in the ChatGPT box is named as Groq's (got {exc})")

    groq_session = _FakeHttpSession([(200, GROQ_MODELS)])
    await free.sign_in(groq_session, "groq", groq_key)
    check(groq_session.requests[0]["url"] == "https://api.groq.com/openai/v1/models"
          and groq_session.requests[0]["headers"]["Authorization"] == "Bearer " + groq_key,
          "Groq key is checked against Groq's model list")
    ids = [m["id"] for m in free.models("groq")]
    check(ids == ["llama-3.1-8b-instant", "llama-3.3-70b-versatile"],
          f"Groq list keeps chat models only, small one first (got {ids})")
    check(free_cfg.groq_key == groq_key and free.active == {"provider": "groq", "model": "llama-3.1-8b-instant"},
          "Groq answers automatically as the first sign-in")
    st = free.status()
    check(st["speech_ready"] is True and st["speech"] == {"asr": "groq", "tts": "groq"},
          f"Groq alone makes speech ready, for free (got {st['speech']})")
    check(st["providers"]["groq"]["free"] and not st["providers"]["openai"]["free"]
          and "separate" in st["providers"]["openai"]["note"].lower()
          and "platform.openai.com" in st["providers"]["openai"]["note"],
          "status carries the free-tier line and the ChatGPT 'separate account' note")
    check(st["order"][:2] == ["anthropic", "openai"] and "ollama" in st["order"],
          "status lists every provider in page order")

    await free.sign_in(_FakeHttpSession([(200, GEMINI_MODELS)]), "gemini", "AIza" + "E" * 35)
    ids = [m["id"] for m in free.models("gemini")]
    check(ids == ["gemini-2.5-flash-lite", "gemini-2.5-flash"],
          f"Gemini ids lose the models/ prefix, chat models only, lite first (got {ids})")

    await free.sign_in(_FakeHttpSession([(200, OPENROUTER_MODELS)]), "openrouter", "sk-or-v1-" + "F" * 40)
    ids = [m["id"] for m in free.models("openrouter")]
    check(ids == ["meta-llama/llama-3.3-70b-instruct:free", "google/gemma-3-27b-it:free"]
          and free.models("openrouter")[0]["name"] == "Meta: Llama 3.3 70B Instruct",
          f"OpenRouter lists free models only, without the '(free)' suffix (got {ids})")

    ollama_session = _FakeHttpSession([(200, OLLAMA_MODELS)])
    note = await free.sign_in(ollama_session, "ollama", "192.168.1.60")
    check(ollama_session.requests[0]["url"] == "http://192.168.1.60:11434/v1/models"
          and not (ollama_session.requests[0]["headers"] or {}),
          f"a bare Ollama address gets http:// and :11434, and no auth (got {ollama_session.requests[0]['url']})")
    check(free_cfg.ollama_base == "http://192.168.1.60:11434" and free.signed_in("ollama")
          and note == "", "Ollama's address is live in the config")
    check(free.status()["providers"]["ollama"]["key_hint"] == "http://192.168.1.60:11434",
          "the page can show the Ollama address in full (it isn't a secret)")
    empty = _FakeHttpSession([(200, {"data": []})])
    note = await free.sign_in(empty, "ollama", "http://127.0.0.1:11434")
    check("ollama pull" in note, f"an Ollama with no models says what to run (got {note})")

    free.sign_out("groq")
    check(free.active["provider"] == "gemini" and free_cfg.groq_key == "",
          f"signing out of the answering free provider falls to the next signed-in one (got {free.active})")
    check(free.status()["speech_ready"] is False, "speech goes away with Groq (Gemini has none)")

    # ---- organisation-level Claude keys need a workspace ID
    ws_cfg = _fresh_cfg()
    ws = accounts.AccountStore(ws_cfg)
    try:
        await ws.sign_in(_FakeHttpSession([(400, WORKSPACE_ERROR)]), "anthropic", claude_key)
        check(False, "an org-level key without a workspace raises")
    except accounts.SignInError as exc:
        check(exc.needs == "workspace" and "wrkspc_" in str(exc),
              f"Anthropic's workspace error becomes a 'needs workspace' sign-in error (got {exc})")
    check("anthropic" not in ws.saved and ws_cfg.anthropic_key == "", "nothing saved until the workspace is known")

    session = _FakeHttpSession([(200, ANTHROPIC_MODELS)])
    await ws.sign_in(session, "anthropic", claude_key, "wrkspc_fake")
    check(session.requests[0]["headers"].get("anthropic-workspace-id") == "wrkspc_fake",
          "key + workspace are checked together with the workspace header")
    check(ws_cfg.anthropic_workspace == "wrkspc_fake" and ws.signed_in("anthropic"),
          "the workspace ID is live in the config for llm.py")
    reloaded = accounts.AccountStore(_fresh_cfg(), path=ws_cfg.accounts_file)
    check(reloaded.cfg.anthropic_workspace == "wrkspc_fake", "the workspace ID survives a restart")
    ws.sign_out("anthropic")
    check(ws_cfg.anthropic_workspace == "", "signing out of Claude forgets the workspace too")

    # A key from bridge.env, workspace added on the page afterwards.
    env_ws_cfg = _fresh_cfg(anthropic=claude_key)
    env_ws = accounts.AccountStore(env_ws_cfg)
    session = _FakeHttpSession([(200, ANTHROPIC_MODELS)])
    await env_ws.set_workspace(session, " wrkspc_01XYZ ")
    check(session.requests[0]["headers"].get("x-api-key") == claude_key
          and session.requests[0]["headers"].get("anthropic-workspace-id") == "wrkspc_01XYZ",
          "set_workspace re-checks the bridge.env key with the new workspace")
    check(env_ws.active == {"provider": "anthropic", "model": "claude-haiku-4-5-20251001"}
          and env_ws_cfg.anthropic_workspace == "wrkspc_01XYZ",
          f"after the workspace is saved Claude answers (got {env_ws.active})")
    try:
        await env_ws.set_workspace(_FakeHttpSession([(400, WORKSPACE_ERROR)]), "wrkspc_bad")
        check(False, "a rejected workspace raises")
    except accounts.SignInError as exc:
        check("didn't accept that workspace" in str(exc) and env_ws_cfg.anthropic_workspace == "wrkspc_01XYZ",
              f"a rejected workspace ID is explained and the old one kept (got {exc})")

    return 1 if failures else 0


async def test_accounts_http() -> int:
    """The sign-in page and its API, through a real aiohttp server."""
    print("\naccounts page + API over HTTP")
    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    async def fake_verify(http, cfg, provider, key, workspace=""):
        if key.endswith("bad0"):
            raise accounts.SignInError("Anthropic says that key isn't valid.")
        if key.endswith("org0") and not workspace:
            raise accounts.SignInError("This is an organisation-level key.", "workspace")
        return accounts.parse_models(provider, ANTHROPIC_MODELS if provider == "anthropic" else OPENAI_MODELS), ""

    real_verify = accounts.verify_key
    accounts.verify_key = fake_verify
    port = PORT + 1
    cfg = _fresh_cfg()
    cfg.enable_device_tools = False
    app = bridge.build_app(cfg)
    runner = web.AppRunner(app)
    await runner.setup()
    await web.TCPSite(runner, "127.0.0.1", port).start()
    base = f"http://127.0.0.1:{port}"
    good = {"Content-Type": "application/json", "X-StackyChan": "1"}

    try:
        async with aiohttp.ClientSession() as http:
            async with http.get(base + "/") as r:
                html = await r.text()
                check(r.status == 200 and "StackyChan accounts" in html
                      and r.headers.get("Content-Type", "").startswith("text/html"),
                      "GET / serves the sign-in page")

            async with http.get(base + "/api/accounts") as r:
                body = await r.json()
                check(r.headers.get("Access-Control-Allow-Origin") == "*",
                      "status is readable from the robot's portal (CORS)")
                check(body["active"] is None and body["ws_path"] == cfg.path,
                      "status starts signed out and names the robot's websocket path")

            async with http.post(base + "/api/accounts/signin",
                                 json={"provider": "anthropic", "key": "sk-ant-" + "x" * 30}) as r:
                check(r.status == 403, "a write without the X-StackyChan header is refused")

            async with http.post(base + "/api/accounts/choose", headers=good,
                                 json={"provider": "openai"}) as r:
                body = await r.json()
                check(r.status == 400 and "Sign in to ChatGPT first" in body.get("error", ""),
                      "choosing a provider that isn't signed in is refused")

            async with http.post(base + "/api/accounts/signin", headers=good,
                                 json={"provider": "anthropic", "key": "sk-ant-" + "x" * 30 + "bad0"}) as r:
                body = await r.json()
                check(r.status == 400 and "isn't valid" in body.get("error", ""),
                      "a rejected key comes back as a 400 with the message")

            async with http.post(base + "/api/accounts/signin", headers=good,
                                 json={"provider": "anthropic", "key": "sk-ant-" + "x" * 30 + "org0"}) as r:
                body = await r.json()
                check(r.status == 400 and body.get("needs") == "workspace",
                      "an org-level key answers 400 with needs=workspace so the page can ask for it")

            async with http.post(base + "/api/accounts/signin", headers=good,
                                 json={"provider": "anthropic", "key": "sk-ant-" + "x" * 30 + "good"}) as r:
                body = await r.json()
                check(r.status == 200 and body["providers"]["anthropic"]["signed_in"]
                      and body["active"]["provider"] == "anthropic",
                      "sign-in over HTTP saves the key and picks Claude")
                check(len(body["providers"]) == len(accounts.PROVIDERS) and body["providers"]["groq"]["free"],
                      "the HTTP status lists every provider with its free-tier line")

            async with http.post(base + "/api/accounts/workspace", headers=good,
                                 json={"workspace": "wrkspc_01HTTP"}) as r:
                body = await r.json()
                check(r.status == 200 and body["providers"]["anthropic"]["workspace"] == "wrkspc_01HTTP",
                      "the workspace action saves a workspace ID over HTTP")

            async with http.get(base + "/health") as r:
                body = await r.json()
                check(body.get("llm", "").startswith("anthropic/claude-haiku"),
                      f"health reports the page's choice (got {body.get('llm')})")

        cfg.token = "bridge-secret"
        async with aiohttp.ClientSession() as http:
            async with http.post(base + "/api/accounts/signout", headers=good,
                                 json={"provider": "anthropic"}) as r:
                check(r.status == 401, "with a bridge token set, writes need it")
            async with http.post(base + "/api/accounts/signout",
                                 headers={**good, "Authorization": "Bearer bridge-secret"},
                                 json={"provider": "anthropic"}) as r:
                body = await r.json()
                check(r.status == 200 and body["active"] is None,
                      "the right bridge token signs out")
    finally:
        accounts.verify_key = real_verify
        await runner.cleanup()

    return 1 if failures else 0


if __name__ == "__main__":
    rc = test_sentence_splitting()
    rc |= test_mcp_servers_config()
    rc |= asyncio.run(test_device_mcp_relay())
    rc |= asyncio.run(test_llm_tool_loop())
    rc |= asyncio.run(test_llm_tool_loop_openai())
    rc |= test_speech_routing()
    rc |= asyncio.run(test_pihole())
    rc |= asyncio.run(test_weather())
    rc |= asyncio.run(test_xiaozhi_mcp_relay())
    rc |= asyncio.run(test_accounts())
    rc |= asyncio.run(test_accounts_http())
    rc |= asyncio.run(run())
    sys.exit(rc)
