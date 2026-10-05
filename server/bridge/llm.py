"""Language model providers.

Two wire shapes cover everything in providers.py: Anthropic's Messages API, and OpenAI's
chat completions, which OpenAI, Groq, Google's Gemini (its OpenAI-compatible endpoint),
OpenRouter, Cerebras, Mistral, and a local Ollama (at /v1) all serve. Which base URL and
which key a provider uses is providers.py's business; this module only knows the two shapes.

Each provider gets the same input — a system prompt and a list of turns — and returns
plain text. Streaming is deliberately not used: the reply has to be turned into speech
before the device can do anything with it, and sentence-level chunking happens one layer
up in bridge.py where it can be interleaved with the TTS calls.

Device tools (servo/LED/reminder/Pihole/weather — anything registered on the device's
own MCP server, relayed over the websocket by `mcp_device.DeviceMcpClient`, plus the
bridge-native tools like Pihole/weather) work on both shapes. External MCP servers
(`BRIDGE_MCP_SERVERS`) stay Anthropic-only: they're reached through Anthropic's native MCP
connector (`mcp_servers` + `mcp_toolset`, beta `mcp-client-2025-11-20`), which has no
OpenAI equivalent. External MCP tool calls are resolved entirely server-side on the
Anthropic path — they show up as `mcp_tool_use`/`mcp_tool_result` pairs that need nothing
from us but echoing back. Device (and bridge-native) tool calls show up as ordinary
`tool_use` blocks (Anthropic) or `tool_calls` (OpenAI shape), because nothing outside this
process can reach the device or the bridge-native integrations — those are the ones the
loops below actually execute.

Free models don't all support tools. When one answers a tool-bearing request with a 400,
the same request is retried once without tools rather than failing the turn: the robot
still answers, it just can't move its head that time.
"""

from __future__ import annotations

import json
import logging
from typing import Awaitable, Callable, List, Dict, Optional

import aiohttp

import providers

log = logging.getLogger("bridge.llm")

ANTHROPIC_BETA_MCP = "mcp-client-2025-11-20"
DEVICE_TOOL_PREFIX = "device_"

# How many model→tool→model round trips one utterance may take before giving up. Tool
# calls within a round run one at a time: the device is a single-core embedded MCP
# server on the far end of a websocket, and two calls racing to move the same servo is
# not a scenario worth supporting.
_MAX_TOOL_ROUNDS = 6


class LlmError(RuntimeError):
    pass


NOT_SIGNED_IN = "{} isn't signed in. Sign in on the bridge page, port 8000."
KEY_REJECTED = "{} rejected the key. Sign in again on the bridge page, port 8000."


def _credential(cfg, provider: str) -> str:
    key = providers.credential(cfg, provider)
    if not key:
        if provider in providers.PROVIDERS:
            raise LlmError(NOT_SIGNED_IN.format(providers.label(provider)))
        raise LlmError("OPENAI_API_KEY is not set")
    return key


def _openai_headers(cfg, provider: str) -> dict:
    headers = {"Authorization": f"Bearer {_credential(cfg, provider)}",
               "Content-Type": "application/json"}
    if provider == "openrouter":
        headers["X-Title"] = "StackyChan"
    return headers


def _anthropic_headers(cfg, beta: str = "") -> dict:
    if not cfg.anthropic_key:
        raise LlmError(NOT_SIGNED_IN.format("Claude"))
    headers = {
        "x-api-key": cfg.anthropic_key,
        "anthropic-version": "2023-06-01",
        "content-type": "application/json",
    }
    if cfg.anthropic_workspace:
        headers["anthropic-workspace-id"] = cfg.anthropic_workspace
    if beta:
        headers["anthropic-beta"] = beta
    return headers


def _openai_token_limit(provider: str, limit: int) -> dict:
    # api.openai.com rejects max_tokens for reasoning models (gpt-5, o-series); other
    # OpenAI-compatible servers don't all accept the newer name.
    if provider == "openai":
        return {"max_completion_tokens": limit}
    return {"max_tokens": limit}


def _error_message(data) -> str:
    if isinstance(data, dict):
        err = data.get("error")
        if isinstance(err, dict):
            return str(err.get("message") or err)
        if err:
            return str(err)
    return str(data)


def _raise_openai(provider: str, status: int, data) -> None:
    if status == 401:
        raise LlmError(KEY_REJECTED.format(providers.label(provider)))
    raise LlmError(f"{provider} {status}: {_error_message(data)[:200]}")


def _looks_like_tool_rejection(status: int, data) -> bool:
    if status != 400:
        return False
    text = _error_message(data).lower()
    return "tool" in text or "function" in text


async def complete(session: aiohttp.ClientSession, cfg, provider: str, model: str,
                   system: str, turns: List[Dict[str, str]]) -> str:
    """Run one completion with no tools. `turns` is [{"role":..., "content": str}, …]."""
    provider = (provider or "").lower()
    if not provider:
        raise LlmError("no LLM provider configured")
    if providers.kind(provider) == "anthropic":
        return await _anthropic(session, cfg, model, system, turns)
    return await _openai(session, cfg, model, system, turns, provider)


async def complete_with_tools(
    session: aiohttp.ClientSession, cfg, provider: str, model: str,
    system: str, turns: List[Dict[str, object]],
    device_tools: Optional[List[Dict]] = None,
    call_device_tool: Optional[Callable[[str, dict], Awaitable[str]]] = None,
) -> str:
    """Run a completion that may call tools, looping until the model has a final answer.

    `turns` is the caller's conversation history, mutated in place: on every path
    through this function, by the time it returns, `turns` ends with exactly one new
    assistant turn holding the final reply — plus, when tools were actually called, the
    tool_use/tool_result round trip that produced it. Callers should never separately
    append the reply; the appending is this function's job precisely so a tool call and
    its result live in history the same way a real conversation would replay them.

    Device/bridge-native tools work on both shapes. External MCP servers (`mcp_servers`
    in config) stay Anthropic-only — see the module docstring.
    """
    provider = (provider or "").lower()
    if not provider:
        raise LlmError("no LLM provider configured")

    if providers.kind(provider) != "anthropic":
        if device_tools:
            return await _openai_with_tools(session, cfg, model, system, turns,
                                            device_tools, call_device_tool, provider)
        # This filter is also what makes a mid-conversation provider switch safe: both
        # Anthropic's tool_use content-block turns and _openai_with_tools's tool_calls/
        # role:"tool" turns have non-string `content`, so history built while on one
        # provider is silently skipped rather than crashing a plain-text call on the
        # other provider.
        text = await _openai(session, cfg, model, system,
                             [t for t in turns if isinstance(t.get("content"), str)], provider)
        turns.append({"role": "assistant", "content": text})
        return text

    tools = list(device_tools or [])
    mcp_servers = []
    for server in cfg.mcp_servers:
        mcp_servers.append({
            "type": "url",
            "name": server["name"],
            "url": server["url"],
            **({"authorization_token": server["token"]} if server.get("token") else {}),
        })
        tools.append({"type": "mcp_toolset", "mcp_server_name": server["name"]})

    if not tools:
        # No tools configured — the plain endpoint is one call instead of a beta one.
        text = await _anthropic(session, cfg, model, system,
                                [t for t in turns if isinstance(t.get("content"), str)])
        turns.append({"role": "assistant", "content": text})
        return text

    headers = _anthropic_headers(cfg, beta=ANTHROPIC_BETA_MCP)

    for round_num in range(_MAX_TOOL_ROUNDS):
        body = {
            "model": model,
            "max_tokens": 768,
            "system": system,
            "messages": turns,
            "tools": tools,
        }
        if mcp_servers:
            body["mcp_servers"] = mcp_servers

        async with session.post("https://api.anthropic.com/v1/messages",
                                json=body, headers=headers) as r:
            data = await r.json()
            if r.status != 200:
                raise LlmError(f"anthropic {r.status}: {_error_message(data)[:200]}")

        content = data.get("content", [])
        turns.append({"role": "assistant", "content": content})

        # Every `tool_use` block (as opposed to `mcp_tool_use`) is ours to answer,
        # whether it's a device tool (device_-prefixed) or a bridge-native one like
        # Pihole/weather (no fixed prefix). `mcp_tool_use`/`mcp_tool_result` blocks are
        # the ones Anthropic's own MCP connector already resolved server-side — those
        # aren't `tool_use` blocks at all, so this filter naturally excludes them
        # without needing to know every bridge-native tool's name up front.
        client_calls = [b for b in content if b.get("type") == "tool_use"]

        if data.get("stop_reason") != "tool_use" or not client_calls:
            parts = [b.get("text", "") for b in content if b.get("type") == "text"]
            return "".join(parts).strip()

        if call_device_tool is None:
            raise LlmError("model requested a tool but no tool caller was given")

        results = []
        for block in client_calls:
            name = block["name"]
            bare_name = name[len(DEVICE_TOOL_PREFIX):] if name.startswith(DEVICE_TOOL_PREFIX) else name
            try:
                text = await call_device_tool(bare_name, block.get("input", {}) or {})
                results.append({"type": "tool_result", "tool_use_id": block["id"], "content": text})
            except Exception as exc:
                log.warning("device tool '%s' failed: %s", bare_name, exc)
                results.append({"type": "tool_result", "tool_use_id": block["id"],
                                "content": str(exc), "is_error": True})
        turns.append({"role": "user", "content": results})

    raise LlmError(f"gave up after {_MAX_TOOL_ROUNDS} tool-use rounds without a final answer")


def _to_openai_tools(anthropic_tools: List[Dict]) -> List[Dict]:
    """Anthropic's {name, description, input_schema} and OpenAI's function-tool shape
    are both plain JSON Schema underneath - this is a field remap, not a translation."""
    return [{"type": "function", "function": {
        "name": t["name"],
        "description": t.get("description", ""),
        "parameters": t.get("input_schema") or {"type": "object", "properties": {}},
    }} for t in anthropic_tools]


async def _openai_with_tools(
    session: aiohttp.ClientSession, cfg, model: str, system: str,
    turns: List[Dict[str, object]], device_tools: List[Dict],
    call_device_tool: Optional[Callable[[str, dict], Awaitable[str]]],
    provider: str = "openai",
) -> str:
    """OpenAI-chat-completions analogue of the Anthropic tool loop above. Same
    contract: `turns` ends with exactly one new assistant turn holding the final reply.
    """
    headers = _openai_headers(cfg, provider)
    url = f"{providers.base_url(cfg, provider)}/chat/completions"
    tools = _to_openai_tools(device_tools)

    for round_num in range(_MAX_TOOL_ROUNDS):
        body = {
            "model": model,
            **_openai_token_limit(provider, 1536),
            "messages": [{"role": "system", "content": system}] + turns,
            "tools": tools,
        }
        async with session.post(url, json=body, headers=headers) as r:
            data = await r.json()
            status = r.status

        if status != 200 and round_num == 0 and _looks_like_tool_rejection(status, data):
            # The model (typically a free one) can't do tools. Answer without them so
            # the robot still talks; the history filter drops any tool-shaped turns.
            log.warning("%s/%s rejected tools (%s); answering without them",
                        provider, model, _error_message(data)[:120])
            text = await _openai(session, cfg, model, system,
                                 [t for t in turns if isinstance(t.get("content"), str)], provider)
            turns.append({"role": "assistant", "content": text})
            return text
        if status != 200:
            _raise_openai(provider, status, data)

        message = data["choices"][0]["message"]
        # No server-resolved "external MCP" concept on this shape (that stays
        # Anthropic-only, see the module docstring), so unlike the Anthropic path
        # above, every tool call here is ours to answer - no type/prefix filter.
        tool_calls = message.get("tool_calls") or []

        if not tool_calls:
            text = (message.get("content") or "").strip()
            turns.append({"role": "assistant", "content": text})
            return text

        if call_device_tool is None:
            raise LlmError("model requested a tool but no tool caller was given")

        turns.append({"role": "assistant", "content": message.get("content"),
                      "tool_calls": tool_calls})

        for call in tool_calls:
            fn = call["function"]
            name = fn["name"]
            bare_name = name[len(DEVICE_TOOL_PREFIX):] if name.startswith(DEVICE_TOOL_PREFIX) else name
            try:
                args = json.loads(fn.get("arguments") or "{}")
                text = await call_device_tool(bare_name, args)
            except Exception as exc:
                log.warning("device tool '%s' failed: %s", bare_name, exc)
                text = str(exc)
            turns.append({"role": "tool", "tool_call_id": call["id"], "content": text})

    raise LlmError(f"gave up after {_MAX_TOOL_ROUNDS} tool-use rounds without a final answer")


async def _anthropic(session, cfg, model, system, turns) -> str:
    headers = _anthropic_headers(cfg)
    body = {
        "model": model,
        "max_tokens": 512,
        "system": system,
        "messages": turns,
    }
    async with session.post("https://api.anthropic.com/v1/messages",
                            json=body, headers=headers) as r:
        data = await r.json()
        if r.status != 200:
            raise LlmError(f"anthropic {r.status}: {_error_message(data)[:200]}")

    # Content is a list of blocks; only the text ones matter here.
    parts = [b.get("text", "") for b in data.get("content", []) if b.get("type") == "text"]
    return "".join(parts).strip()


async def _openai(session, cfg, model, system, turns, provider: str = "openai") -> str:
    headers = _openai_headers(cfg, provider)
    url = f"{providers.base_url(cfg, provider)}/chat/completions"
    body = {
        "model": model,
        **_openai_token_limit(provider, 1024),
        "messages": [{"role": "system", "content": system}] + turns,
    }
    async with session.post(url, json=body, headers=headers) as r:
        data = await r.json()
        if r.status != 200:
            _raise_openai(provider, r.status, data)

    return (data["choices"][0]["message"].get("content") or "").strip()
