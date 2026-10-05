"""Configuration for the StackyChan bridge.

Everything is an environment variable with a sane default, because the two places this
realistically runs — a terminal on a laptop and a docker-compose file — both do env vars
well and neither enjoys a config file format.

Nothing here reaches the network. Import it, print it, and you know what the bridge will
do before you start it: `python -m bridge.config` dumps the resolved settings with the
secrets masked.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field, asdict


def _env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def _env_int(name: str, default: int) -> int:
    raw = _env(name)
    if not raw:
        return default
    try:
        return int(raw)
    except ValueError:
        return default


def _mask(value: str) -> str:
    if not value:
        return ""
    return value[:4] + "…" + value[-4:] if len(value) > 12 else "…"


def _parse_mcp_servers(raw: str) -> list:
    """Parse BRIDGE_MCP_SERVERS. Empty or malformed input yields an empty list rather
    than crashing startup — a typo in one env var shouldn't take down the whole bridge."""
    if not raw.strip():
        return []
    try:
        servers = json.loads(raw)
    except ValueError as exc:
        print(f"BRIDGE_MCP_SERVERS is not valid JSON, ignoring it: {exc}")
        return []
    if not isinstance(servers, list):
        print("BRIDGE_MCP_SERVERS must be a JSON array, ignoring it")
        return []

    out = []
    for entry in servers:
        if not isinstance(entry, dict) or not entry.get("name") or not entry.get("url"):
            print(f"BRIDGE_MCP_SERVERS entry missing name/url, skipping: {entry!r}")
            continue
        out.append({"name": entry["name"], "url": entry["url"], "token": entry.get("token", "")})
    return out


@dataclass
class Config:
    # ------------------------------------------------------------------ server --
    host: str = field(default_factory=lambda: _env("BRIDGE_HOST", "0.0.0.0"))
    port: int = field(default_factory=lambda: _env_int("BRIDGE_PORT", 8000))
    path: str = field(default_factory=lambda: _env("BRIDGE_PATH", "/xiaozhi/v1/"))

    # Shared secret the device sends as `Authorization: Bearer …`. Empty disables the
    # check, which is fine on a trusted LAN and a bad idea anywhere else.
    token: str = field(default_factory=lambda: _env("BRIDGE_TOKEN", ""))

    # ---------------------------------------------------------------- behaviour --
    # The device streams continuously in "auto" mode and expects the server to decide
    # when a turn ended. These two control that.
    vad_silence_ms: int = field(default_factory=lambda: _env_int("BRIDGE_VAD_SILENCE_MS", 900))
    vad_threshold: int = field(default_factory=lambda: _env_int("BRIDGE_VAD_THRESHOLD", 500))
    max_utterance_ms: int = field(default_factory=lambda: _env_int("BRIDGE_MAX_UTTERANCE_MS", 15000))

    # What the device is told to expect back. It resamples if this differs from 16k.
    tts_sample_rate: int = field(default_factory=lambda: _env_int("BRIDGE_TTS_SAMPLE_RATE", 16000))
    frame_duration_ms: int = field(default_factory=lambda: _env_int("BRIDGE_FRAME_MS", 60))

    history_turns: int = field(default_factory=lambda: _env_int("BRIDGE_HISTORY_TURNS", 8))

    default_prompt: str = field(default_factory=lambda: _env(
        "BRIDGE_DEFAULT_PROMPT",
        "You are StackyChan, a small desk robot with a screen for a face. "
        "Keep replies short and spoken-sounding: one or two sentences unless asked for more. "
        "You are being read aloud, so never use markdown, lists, or emoji."))

    # ---------------------------------------------------------------------- llm --
    llm_provider: str = field(default_factory=lambda: _env("BRIDGE_LLM_PROVIDER", "anthropic"))
    llm_model: str = field(default_factory=lambda: _env("BRIDGE_LLM_MODEL", ""))

    # Keys signed in through the bridge's page (GET /) are saved to accounts_file and
    # override every key below. The env vars remain for headless installs.
    anthropic_key: str = field(default_factory=lambda: _env("ANTHROPIC_API_KEY"))
    # Only for organisation-level Anthropic keys, which Anthropic refuses to serve without
    # an `anthropic-workspace-id` header. Keys created inside a workspace don't need it.
    anthropic_workspace: str = field(default_factory=lambda: _env("ANTHROPIC_WORKSPACE_ID"))
    openai_key: str = field(default_factory=lambda: _env("OPENAI_API_KEY"))
    groq_key: str = field(default_factory=lambda: _env("GROQ_API_KEY"))
    gemini_key: str = field(default_factory=lambda: _env("GEMINI_API_KEY"))
    openrouter_key: str = field(default_factory=lambda: _env("OPENROUTER_API_KEY"))
    cerebras_key: str = field(default_factory=lambda: _env("CEREBRAS_API_KEY"))
    mistral_key: str = field(default_factory=lambda: _env("MISTRAL_API_KEY"))
    accounts_file: str = field(default_factory=lambda: _env(
        "BRIDGE_ACCOUNTS_FILE",
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "accounts.json")))
    openai_base: str = field(default_factory=lambda: _env("OPENAI_BASE_URL", "https://api.openai.com/v1"))
    # Empty means "not signed in"; llm.py falls back to http://127.0.0.1:11434 when the
    # env picks the ollama provider without naming an address.
    ollama_base: str = field(default_factory=lambda: _env("OLLAMA_BASE_URL", ""))

    # Whether a device may override the provider/model in its hello. Handy on a bench,
    # worth turning off if the bridge is shared.
    allow_device_model: bool = field(
        default_factory=lambda: _env("BRIDGE_ALLOW_DEVICE_MODEL", "1") not in ("0", "false", "no"))

    # ---------------------------------------------------------------------- mcp --
    # Give the model tools. Two independent sources, both optional:
    #
    #   1. The device's own tools (set_head_angles, set_led_color, reminders, ...),
    #      relayed over the same websocket the audio rides on. On by default — it's the
    #      whole point of the device shipping an MCP server.
    #
    #   2. Any number of external MCP servers, reachable over HTTP, via Anthropic's
    #      native MCP connector. This is the "room for more MCPs" knob: add an entry to
    #      the JSON list below and the model gets a new tool, no code change and no
    #      redeploy of anything but this process.
    #
    # The device-tool relay (and the bridge-native tools below) work on Anthropic and on
    # the OpenAI-chat-completions family (openai/openai-compatible/groq) — see llm.py's
    # complete_with_tools(). The MCP connector for external servers below is still
    # Anthropic-only: it's Anthropic's own native connector, with no OpenAI equivalent.
    enable_device_tools: bool = field(
        default_factory=lambda: _env("BRIDGE_ENABLE_DEVICE_TOOLS", "1") not in ("0", "false", "no"))

    # BRIDGE_MCP_SERVERS='[{"name":"weather","url":"https://mcp.example.com/sse"},
    #                       {"name":"home","url":"https://home.example.com/mcp","token":"..."}]'
    # Anthropic-only (see comment above) — stays that way deliberately, not an oversight.
    mcp_servers: list = field(default_factory=lambda: _parse_mcp_servers(_env("BRIDGE_MCP_SERVERS", "")))

    # ------------------------------------------------------------------- pihole --
    # Bridge-native tool, works on both providers via the same device-tool merge point.
    # v5's API is the simple, stable one (a single ?auth=token query param); v6 moved to
    # session-based auth (POST /api/auth -> session id) and its exact request/response
    # shapes should be re-checked against a live instance or current docs before trusting
    # this blindly - it was implemented from best available knowledge, not a live test.
    pihole_url: str = field(default_factory=lambda: _env("PIHOLE_URL", ""))
    pihole_api_token: str = field(default_factory=lambda: _env("PIHOLE_API_TOKEN", ""))     # v5
    pihole_password: str = field(default_factory=lambda: _env("PIHOLE_PASSWORD", ""))       # v6
    pihole_api_version: str = field(default_factory=lambda: _env("PIHOLE_API_VERSION", "auto"))  # "5"|"6"|"auto"

    # ------------------------------------------------------------------ weather --
    # Open-Meteo: free, no API key. A place name (geocoded once and cached) or "lat,lon".
    weather_location: str = field(default_factory=lambda: _env("WEATHER_LOCATION", ""))

    # ------------------------------------------------------------- xiaozhi.me mcp --
    # For xiaozhi_mcp_relay.py only (a separate long-lived process, not the main bridge).
    # Rotate this token if it's ever been shared/screenshotted - it's a live credential.
    xiaozhi_mcp_token: str = field(default_factory=lambda: _env("XIAOZHI_MCP_TOKEN", ""))
    xiaozhi_mcp_url: str = field(default_factory=lambda: _env("XIAOZHI_MCP_URL", "wss://api.xiaozhi.me/mcp"))

    # -------------------------------------------------------------------- kie.ai --
    # Connection only for now - no generation tool wired up yet (deliberately deferred,
    # see docs/STATUS.md). kie_model is normally supplied per-device via the persona-hello
    # patch, this is just the fallback/default and the API credentials.
    kie_api_key: str = field(default_factory=lambda: _env("KIE_API_KEY", ""))
    kie_base_url: str = field(default_factory=lambda: _env("KIE_BASE_URL", "https://api.kie.ai/api/v1"))
    kie_model: str = field(default_factory=lambda: _env("KIE_MODEL", "kling-2.6/text-to-video"))

    # ---------------------------------------------------------------------- asr --
    asr_provider: str = field(default_factory=lambda: _env("BRIDGE_ASR_PROVIDER", "openai"))
    asr_model: str = field(default_factory=lambda: _env("BRIDGE_ASR_MODEL", "whisper-1"))
    asr_base: str = field(default_factory=lambda: _env("BRIDGE_ASR_BASE_URL", ""))
    asr_key: str = field(default_factory=lambda: _env("BRIDGE_ASR_KEY", ""))
    asr_language: str = field(default_factory=lambda: _env("BRIDGE_ASR_LANGUAGE", "en"))

    # ---------------------------------------------------------------------- tts --
    tts_provider: str = field(default_factory=lambda: _env("BRIDGE_TTS_PROVIDER", "openai"))
    tts_model: str = field(default_factory=lambda: _env("BRIDGE_TTS_MODEL", "gpt-4o-mini-tts"))
    tts_voice: str = field(default_factory=lambda: _env("BRIDGE_TTS_VOICE", "alloy"))
    tts_speed: float = field(default_factory=lambda: float(_env("BRIDGE_TTS_SPEED", "1.0") or "1.0"))
    tts_base: str = field(default_factory=lambda: _env("BRIDGE_TTS_BASE_URL", ""))
    tts_key: str = field(default_factory=lambda: _env("BRIDGE_TTS_KEY", ""))

    log_level: str = field(default_factory=lambda: _env("BRIDGE_LOG_LEVEL", "INFO"))

    def resolved_llm_model(self) -> str:
        if self.llm_model:
            return self.llm_model
        return {
            "anthropic": "claude-sonnet-5",
            "openai": "gpt-4o-mini",
            "ollama": "llama3.2",
            "groq": "llama-3.3-70b-versatile",
            "gemini": "gemini-2.5-flash-lite",
            "cerebras": "llama3.1-8b",
            "mistral": "mistral-small-latest",
            # "openrouter" has no default on purpose: its free models rotate, so the
            # sign-in page picks the first one the live list offers instead.
            # "openai-compatible" deliberately has no default here: it's a generic
            # pointer at whatever OPENAI_BASE_URL is (LM Studio, vLLM, OpenRouter,
            # together.ai, ...), and each of those has its own model-naming scheme --
            # a guessed default would be wrong more often than not. BRIDGE_LLM_MODEL is
            # required for this provider; Session logs a clear warning if it's missing
            # rather than silently sending an empty model string.
        }.get(self.llm_provider, "")

    def describe(self) -> dict:
        out = asdict(self)
        for key in ("token", "anthropic_key", "openai_key", "groq_key", "gemini_key",
                    "openrouter_key", "cerebras_key", "mistral_key", "asr_key", "tts_key",
                    "pihole_api_token", "pihole_password", "kie_api_key", "xiaozhi_mcp_token"):
            out[key] = _mask(out[key])
        out["mcp_servers"] = [
            {"name": s["name"], "url": s["url"], "token": _mask(s["token"])}
            for s in out["mcp_servers"]
        ]
        out["llm_model_resolved"] = self.resolved_llm_model()
        return out


if __name__ == "__main__":
    import json

    print(json.dumps(Config().describe(), indent=2, sort_keys=True))
