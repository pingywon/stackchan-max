"""Voice-controlled Pihole: stats, and a timed disable.

Bridge-native, not a device tool - Pihole lives on the LAN, not on the ESP32, and its
credentials belong here with every other API key, not on the device.

v5 (the `/admin/api.php` query-param API) is the priority: it's simple, stable, and its
`disable=<seconds>` parameter re-enables itself server-side with no help from us, which
is exactly the "disable for N minutes, come back automatically" behaviour asked for - it
survives a bridge restart or crash, since Pihole's own timer does the work, not ours.

v6 (`/api/...`, session-token auth) is supported on a best-effort basis. Its exact
request/response shapes here come from Pihole's published API docs, not a live test
against a running v6 instance - re-verify before trusting this blindly if `PIHOLE_API_VERSION`
is set to "6" or "auto" picks v6.
"""

from __future__ import annotations

import logging
from typing import Optional

import aiohttp

log = logging.getLogger("bridge.pihole")

# Detected version and v6 session id are both stable for as long as the Pihole install
# itself doesn't change -- caching them (keyed by URL, since cfg is effectively singleton
# per bridge process but this costs nothing to make safe against that changing) turns what
# used to be 3 HTTP round trips per voice command (detect + auth + operation) into 1 after
# the first call. Session id is invalidated on a 401 from the actual operation, not on a
# timer -- Pihole v6 sessions normally outlive a single voice interaction by a wide margin.
_version_cache: dict[str, str] = {}
_sid_cache: dict[str, str] = {}


class PiholeError(RuntimeError):
    pass


def _configured(cfg) -> bool:
    return bool(cfg.pihole_url)


async def _detect_version(session: aiohttp.ClientSession, cfg) -> str:
    if cfg.pihole_api_version in ("5", "6"):
        return cfg.pihole_api_version
    cached = _version_cache.get(cfg.pihole_url)
    if cached is not None:
        return cached
    # "auto": v6 answers on /api/auth even with a wrong password (401, not 404);
    # v5 has no such path. A connect failure here just falls through to v5, which
    # is the more common/stable install to guess when we can't tell. Deliberately not
    # cached on the connect-failure path -- a transient network hiccup shouldn't pin the
    # guess to v5 for the rest of the process.
    base = cfg.pihole_url.rstrip("/")
    try:
        async with session.get(f"{base}/api/auth", timeout=aiohttp.ClientTimeout(total=3)) as r:
            if r.status in (200, 400, 401):
                _version_cache[cfg.pihole_url] = "6"
                return "6"
    except Exception:
        return "5"
    _version_cache[cfg.pihole_url] = "5"
    return "5"


async def _v6_session_id(session: aiohttp.ClientSession, cfg, force_refresh: bool = False) -> str:
    if not force_refresh:
        cached = _sid_cache.get(cfg.pihole_url)
        if cached is not None:
            return cached
    base = cfg.pihole_url.rstrip("/")
    async with session.post(f"{base}/api/auth", json={"password": cfg.pihole_password}) as r:
        data = await r.json()
        if r.status != 200:
            raise PiholeError(f"pihole v6 auth {r.status}: {data}")
        sid = (data.get("session") or {}).get("sid")
        if not sid:
            raise PiholeError(f"pihole v6 auth did not return a session id: {data}")
        _sid_cache[cfg.pihole_url] = sid
        return sid


async def _v6_request(session: aiohttp.ClientSession, cfg, method: str, url: str,
                       json: Optional[dict] = None) -> tuple[int, dict]:
    """v6 request with a cached session id, retried once with a fresh one on 401."""
    sid = await _v6_session_id(session, cfg)
    async with session.request(method, url, json=json, headers={"X-FTL-SID": sid}) as r:
        if r.status != 401:
            return r.status, await r.json()
    sid = await _v6_session_id(session, cfg, force_refresh=True)
    async with session.request(method, url, json=json, headers={"X-FTL-SID": sid}) as r:
        return r.status, await r.json()


async def get_stats(session: aiohttp.ClientSession, cfg) -> str:
    if not _configured(cfg):
        raise PiholeError("Pihole is not configured")
    base = cfg.pihole_url.rstrip("/")
    version = await _detect_version(session, cfg)

    if version == "5":
        async with session.get(f"{base}/admin/api.php",
                               params={"summaryRaw": "", "auth": cfg.pihole_api_token}) as r:
            data = await r.json()
            if r.status != 200:
                raise PiholeError(f"pihole {r.status}: {data}")
        blocked = data.get("ads_blocked_today")
        total = data.get("dns_queries_today")
        pct = data.get("ads_percentage_today")
    else:
        status, data = await _v6_request(session, cfg, "GET", f"{base}/api/stats/summary")
        if status != 200:
            raise PiholeError(f"pihole {status}: {data}")
        queries = data.get("queries", {})
        blocked = queries.get("blocked")
        total = queries.get("total")
        pct = queries.get("percent_blocked")

    if blocked is None or total is None:
        raise PiholeError(f"unexpected Pihole response shape: {data}")
    pct_text = f"{pct:.1f}%" if isinstance(pct, (int, float)) else "an unknown percentage"
    return f"Pihole has blocked {blocked} of {total} DNS queries today ({pct_text})."


async def disable(session: aiohttp.ClientSession, cfg, minutes: int) -> str:
    if not _configured(cfg):
        raise PiholeError("Pihole is not configured")
    base = cfg.pihole_url.rstrip("/")
    version = await _detect_version(session, cfg)
    seconds = max(1, int(minutes)) * 60

    if version == "5":
        async with session.get(f"{base}/admin/api.php",
                               params={"disable": str(seconds), "auth": cfg.pihole_api_token}) as r:
            data = await r.json()
            if r.status != 200:
                raise PiholeError(f"pihole {r.status}: {data}")
    else:
        status, data = await _v6_request(session, cfg, "POST", f"{base}/api/dns/blocking",
                                          json={"blocking": False, "timer": seconds})
        if status not in (200, 204):
            raise PiholeError(f"pihole {status}: {data}")

    return f"Pihole is disabled for {minutes} minute{'s' if minutes != 1 else ''} and will turn back on by itself."


async def enable(session: aiohttp.ClientSession, cfg) -> str:
    if not _configured(cfg):
        raise PiholeError("Pihole is not configured")
    base = cfg.pihole_url.rstrip("/")
    version = await _detect_version(session, cfg)

    if version == "5":
        async with session.get(f"{base}/admin/api.php",
                               params={"enable": "", "auth": cfg.pihole_api_token}) as r:
            data = await r.json()
            if r.status != 200:
                raise PiholeError(f"pihole {r.status}: {data}")
    else:
        status, data = await _v6_request(session, cfg, "POST", f"{base}/api/dns/blocking",
                                          json={"blocking": True})
        if status not in (200, 204):
            raise PiholeError(f"pihole {status}: {data}")

    return "Pihole is back on."


PIHOLE_TOOLS = [
    {
        "name": "pihole_get_stats",
        "description": "Get today's Pihole blocking stats: queries blocked vs total.",
        "input_schema": {"type": "object", "properties": {}},
    },
    {
        "name": "pihole_disable",
        "description": "Turn Pihole off for a number of minutes; it turns itself back on automatically.",
        "input_schema": {
            "type": "object",
            "properties": {"minutes": {"type": "integer", "minimum": 1, "maximum": 1440}},
            "required": ["minutes"],
        },
    },
    {
        "name": "pihole_enable",
        "description": "Turn Pihole back on immediately.",
        "input_schema": {"type": "object", "properties": {}},
    },
]


async def call_tool(session: aiohttp.ClientSession, cfg, name: str, arguments: dict) -> str:
    if name == "pihole_get_stats":
        return await get_stats(session, cfg)
    if name == "pihole_disable":
        return await disable(session, cfg, int(arguments.get("minutes", 30)))
    if name == "pihole_enable":
        return await enable(session, cfg)
    raise PiholeError(f"unknown pihole tool '{name}'")
