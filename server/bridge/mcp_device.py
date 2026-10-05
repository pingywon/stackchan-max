"""The device's own MCP server, reached as an MCP *client* from the bridge.

The firmware exposes tools — set_head_angles, set_led_color, create_reminder, and
whatever `hal_mcp.cpp` adds later — over the same websocket the audio rides on, wrapped in
`{"type":"mcp","payload": <JSON-RPC 2.0>}`. `mcp_server.cc` on the device is the MCP
*server*; this class is the *client* that talks to it, so an LLM that only knows how to
call tools can reach the robot's own actuators.

The wire shape (from `firmware/xiaozhi-esp32/main/mcp_server.cc`):

    bridge -> device   {"type":"mcp","payload":{"jsonrpc":"2.0","id":N,"method":"tools/list", ...}}
    device -> bridge   {"type":"mcp","payload":{"jsonrpc":"2.0","id":N,"result": {...}}}

Every message the device sends this way is a *response* to something the bridge asked —
the firmware never opens an MCP call in the other direction — so routing incoming
payloads by `id` to a waiting future is the whole client.
"""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Any, Dict, List, Optional

log = logging.getLogger("bridge.mcp_device")

# The device already emits `{tools:[{name,description,inputSchema}]}` — the same shape
# Anthropic's `input_schema` expects, modulo the key name.
_CALL_TIMEOUT_S = 8.0


class DeviceMcpError(RuntimeError):
    pass


class DeviceMcpClient:
    """One of these per session. Not safe to share across devices — request ids reset."""

    def __init__(self, send_text):
        # send_text: async fn(str) -> None, the session's raw websocket text sender.
        self._send_text = send_text
        self._next_id = 1
        self._pending: Dict[int, asyncio.Future] = {}
        self.tools: List[Dict[str, Any]] = []  # cached tools/list, empty until discover()
        self._initialized = False

    async def _call(self, method: str, params: Optional[dict] = None) -> dict:
        request_id = self._next_id
        self._next_id += 1

        future: asyncio.Future = asyncio.get_event_loop().create_future()
        self._pending[request_id] = future

        payload = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            payload["params"] = params

        await self._send_text(json.dumps({"type": "mcp", "payload": payload}))

        try:
            response = await asyncio.wait_for(future, _CALL_TIMEOUT_S)
        except asyncio.TimeoutError:
            self._pending.pop(request_id, None)
            raise DeviceMcpError(f"device did not answer '{method}' in {_CALL_TIMEOUT_S}s")

        if "error" in response:
            raise DeviceMcpError(str(response["error"].get("message", response["error"])))
        return response.get("result", {})

    def on_message(self, payload: dict):
        """Feed an incoming `{"type":"mcp","payload":...}` message here."""
        request_id = payload.get("id")
        if request_id is None or request_id not in self._pending:
            log.debug("unmatched mcp message (no pending request for id=%s)", request_id)
            return
        future = self._pending.pop(request_id)
        if not future.done():
            future.set_result(payload)

    async def discover(self) -> List[Dict[str, Any]]:
        """Initialize the connection and cache the device's tool list.

        Safe to call once per session, right after the device's hello arrives — before
        that the firmware hasn't finished bringing up the MCP server and initialize()
        would just time out.
        """
        if self._initialized:
            return self.tools
        try:
            await self._call("initialize", {"capabilities": {}})
            result = await self._call("tools/list", {})
        except DeviceMcpError as exc:
            log.warning("device MCP discovery failed: %s", exc)
            return []

        self.tools = result.get("tools", [])
        self._initialized = True
        log.info("device MCP tools: %s", ", ".join(t.get("name", "?") for t in self.tools))
        return self.tools

    async def call_tool(self, name: str, arguments: dict) -> str:
        """Call a device tool and return its result as plain text for a tool_result block."""
        result = await self._call("tools/call", {"name": name, "arguments": arguments})
        # MCP tool results are {"content":[{"type":"text","text":"..."}], "isError": bool}.
        # The firmware's tool callbacks return bool/int/string/JSON, which mcp_server.cc
        # wraps the same way — collapse to one string, which is all a tool_result needs.
        pieces = []
        for block in result.get("content", []):
            if block.get("type") == "text":
                pieces.append(block.get("text", ""))
            else:
                pieces.append(json.dumps(block))
        text = "\n".join(pieces) if pieces else json.dumps(result)
        if result.get("isError"):
            raise DeviceMcpError(text)
        return text


def device_tools_to_anthropic(mcp_tools: List[Dict[str, Any]], prefix: str = "device_") -> List[Dict[str, Any]]:
    """Convert the device's MCP tool list into Anthropic Messages API tool definitions.

    Prefixed so a device tool can never collide with a name the bridge or an external MCP
    server also happens to use — `set_led_color` becomes `device_set_led_color`.
    """
    out = []
    for tool in mcp_tools:
        name = tool.get("name")
        if not name:
            continue
        schema = tool.get("inputSchema") or {"type": "object", "properties": {}}
        out.append({
            "name": prefix + name,
            "description": tool.get("description", "") or f"Device tool: {name}",
            "input_schema": schema,
        })
    return out
