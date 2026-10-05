"""Bring bridge-native tools (Pihole, weather) to the official xiaozhi.me cloud agent.

Why this exists
----------------
The device's own tools (servo, LEDs, reminders, play_gesture) already reach xiaozhi.me's
cloud agent automatically once the device is connected there — that's stock upstream MCP
support (`mcp_server.cc`, same `{"type":"mcp",...}` envelope this repo's own bridge also
uses), nothing StackChan-specific about it. What the stock cloud agent has no way to reach
is Pihole/weather — those live in *this* process, not on the device. xiaozhi.me's "MCP
Access Point" feature closes that gap: it lets an external process expose tools directly
to the cloud agent over a dedicated WebSocket.

The relationship here is the reverse of mcp_device.py. There, the bridge is an MCP
*client* calling the device's MCP *server*. Here, xiaozhi.me's cloud is the MCP *client* —
it opens this process's outbound connection and sends `tools/list`/`tools/call` requests
*into* it — so this module has to behave as an MCP *server* instead.

Known-unknowns, flagged rather than guessed
--------------------------------------------
The exact JSON-RPC framing over `wss://api.xiaozhi.me/mcp/` beyond "WebSocket carrying MCP
JSON-RPC 2.0" is not documented anywhere publicly available at the time this was written.
The confirmed reference point (a Home Assistant integration using this same mechanism)
states only: "Xiaozhi cloud sends MCP requests to this integration via WebSocket." Run this
with LOG_LEVEL=DEBUG on first connect and read the logged frames before trusting the
dispatch logic below on a real xiaozhi.me account — the request shape is implemented as
standard MCP (matching `firmware/xiaozhi-esp32/docs/mcp-protocol.md` and mcp_device.py's
own wire shape), but has not been verified against a live xiaozhi.me connection.

Run as its own long-lived process (systemd service, not part of the per-device bridge):

    XIAOZHI_MCP_TOKEN=... python xiaozhi_mcp_relay.py

That token is a live credential from the xiaozhi.me app's "MCP Access Point" screen —
treat it exactly like every other credential in this project (a 600-permission dotfile or
an env var set by the service manager, never committed). If a token was ever shared in a
screenshot or chat, rotate it in the app before using it here.
"""

from __future__ import annotations

import asyncio
import json
import logging

import aiohttp

from config import Config
import pihole
import weather

log = logging.getLogger("bridge.xiaozhi_mcp_relay")

_RECONNECT_DELAY_S = 10


def _tools_list() -> list:
    """Merge Pihole/weather tool schemas into native MCP shape.

    PIHOLE_TOOLS/WEATHER_TOOLS use the Anthropic-style key `input_schema` (see
    llm.py's docstring — that's the shape both provider conversions were themselves
    derived from). MCP's own wire format spells it `inputSchema` instead.
    """
    out = []
    for tool in list(pihole.PIHOLE_TOOLS) + list(weather.WEATHER_TOOLS):
        out.append({
            "name": tool["name"],
            "description": tool.get("description", ""),
            "inputSchema": tool.get("input_schema") or {"type": "object", "properties": {}},
        })
    return out


async def _call_tool(session: aiohttp.ClientSession, cfg: Config, name: str, arguments: dict) -> dict:
    try:
        if name.startswith("pihole_"):
            text = await pihole.call_tool(session, cfg, name, arguments)
        elif name.startswith("weather_"):
            text = await weather.call_tool(session, cfg, name, arguments)
        else:
            return {"content": [{"type": "text", "text": f"unknown tool '{name}'"}], "isError": True}
        return {"content": [{"type": "text", "text": text}]}
    except Exception as exc:
        log.warning("tool '%s' failed: %s", name, exc)
        return {"content": [{"type": "text", "text": str(exc)}], "isError": True}


async def _handle_request(session: aiohttp.ClientSession, cfg: Config, req: dict) -> dict | None:
    method = req.get("method")
    request_id = req.get("id")
    # Per JSON-RPC/MCP, a notification is identified by the ABSENCE of "id" -- not by
    # having no "method". Confirmed live against a real xiaozhi.me connection: it sends
    # a named notification ("notifications/initialized") with no id right after connecting.
    # The original check here (`method is None`) never matches a real notification, since
    # notifications do carry a method name -- it would have sent a bogus error reply back
    # for id-less unhandled methods instead of staying silent.
    is_notification = request_id is None

    if method == "initialize":
        result = {"protocolVersion": "2024-11-05", "capabilities": {"tools": {}},
                  "serverInfo": {"name": "stackychan-bridge", "version": "1.0"}}
    elif method == "tools/list":
        result = {"tools": _tools_list()}
    elif method == "tools/call":
        params = req.get("params") or {}
        result = await _call_tool(session, cfg, params.get("name", ""), params.get("arguments") or {})
    elif method == "ping":
        # Standard MCP keepalive -- confirmed live, xiaozhi.me's broker sends this
        # periodically. It expects an empty result, not "method not found"; answering
        # wrong here is exactly the kind of thing that could make the access point look
        # unhealthy/offline even while tool calls themselves work fine.
        result = {}
    else:
        if is_notification:
            log.debug("xiaozhi relay: unhandled notification '%s', no reply needed: %s", method, req)
            return None
        log.info("xiaozhi relay: unhandled method '%s' - logging so the real shape can be added", method)
        return {"jsonrpc": "2.0", "id": request_id,
                "error": {"code": -32601, "message": f"method not found: {method}"}}

    if is_notification:
        return None
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


async def run_once(cfg: Config) -> None:
    url = f"{cfg.xiaozhi_mcp_url.rstrip('/')}/?token={cfg.xiaozhi_mcp_token}"
    async with aiohttp.ClientSession() as session:
        async with session.ws_connect(url, heartbeat=30) as ws:
            log.info("xiaozhi MCP relay connected")
            async for msg in ws:
                if msg.type != aiohttp.WSMsgType.TEXT:
                    continue
                log.debug("xiaozhi relay <- %s", msg.data)
                try:
                    req = json.loads(msg.data)
                except ValueError:
                    log.warning("xiaozhi relay: non-JSON frame, ignoring: %r", msg.data[:200])
                    continue
                reply = await _handle_request(session, cfg, req)
                if reply is not None:
                    log.debug("xiaozhi relay -> %s", reply)
                    await ws.send_str(json.dumps(reply))


async def main() -> None:
    cfg = Config()
    logging.basicConfig(level=cfg.log_level)
    if not cfg.xiaozhi_mcp_token:
        log.error("XIAOZHI_MCP_TOKEN not set - nothing to connect to, exiting")
        return

    while True:
        try:
            await run_once(cfg)
        except Exception as exc:
            log.warning("xiaozhi relay connection lost: %s - reconnecting in %ds",
                       exc, _RECONNECT_DELAY_S)
        await asyncio.sleep(_RECONNECT_DELAY_S)


if __name__ == "__main__":
    asyncio.run(main())
