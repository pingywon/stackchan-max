"""Voice-controlled weather via Open-Meteo — genuinely free, no API key.

Bridge-native, same shape as pihole.py. `cfg.weather_location` is either a place name
(geocoded once via Open-Meteo's free geocoding API and cached for the life of the
process — a location doesn't move) or "lat,lon" directly.
"""

from __future__ import annotations

import logging
from typing import Optional, Tuple

import aiohttp

log = logging.getLogger("bridge.weather")

_GEOCODE_URL = "https://geocoding-api.open-meteo.com/v1/search"
_FORECAST_URL = "https://api.open-meteo.com/v1/forecast"

# Open-Meteo's WMO weather codes, condensed to what's worth saying out loud.
_CODE_TEXT = {
    0: "clear", 1: "mostly clear", 2: "partly cloudy", 3: "overcast",
    45: "foggy", 48: "foggy",
    51: "light drizzle", 53: "drizzle", 55: "heavy drizzle",
    61: "light rain", 63: "rain", 65: "heavy rain",
    71: "light snow", 73: "snow", 75: "heavy snow",
    80: "rain showers", 81: "rain showers", 82: "heavy rain showers",
    95: "thunderstorms", 96: "thunderstorms", 99: "severe thunderstorms",
}

# Resolved once per process — a configured location doesn't move mid-run.
_geocode_cache: dict = {}


class WeatherError(RuntimeError):
    pass


def _configured(cfg, location: Optional[str] = None) -> bool:
    return bool(location or cfg.weather_location)


async def _resolve_location(session: aiohttp.ClientSession, cfg, location: Optional[str] = None) -> Tuple[float, float]:
    loc = (location or cfg.weather_location).strip()
    if loc in _geocode_cache:
        return _geocode_cache[loc]

    if "," in loc:
        parts = loc.split(",", 1)
        try:
            lat, lon = float(parts[0].strip()), float(parts[1].strip())
            _geocode_cache[loc] = (lat, lon)
            return lat, lon
        except ValueError:
            pass  # not "lat,lon" after all — fall through to geocoding as a place name

    async with session.get(_GEOCODE_URL, params={"name": loc, "count": 1}) as r:
        data = await r.json()
        if r.status != 200:
            raise WeatherError(f"open-meteo geocoding {r.status}: {data}")
    results = data.get("results") or []
    if not results:
        raise WeatherError(f"could not find a location matching '{loc}'")
    lat, lon = results[0]["latitude"], results[0]["longitude"]
    _geocode_cache[loc] = (lat, lon)
    return lat, lon


async def get_current(session: aiohttp.ClientSession, cfg, location: Optional[str] = None) -> str:
    if not _configured(cfg, location):
        raise WeatherError("no weather location is configured")
    lat, lon = await _resolve_location(session, cfg, location)

    async with session.get(_FORECAST_URL, params={
        "latitude": lat, "longitude": lon,
        "current": "temperature_2m,weather_code,wind_speed_10m",
        "temperature_unit": "fahrenheit",
    }) as r:
        data = await r.json()
        if r.status != 200:
            raise WeatherError(f"open-meteo {r.status}: {data}")

    current = data.get("current") or {}
    temp = current.get("temperature_2m")
    wind = current.get("wind_speed_10m")
    code = current.get("weather_code")
    if temp is None:
        raise WeatherError(f"unexpected Open-Meteo response shape: {data}")
    condition = _CODE_TEXT.get(code, "unclear skies")
    return f"It's currently {round(temp)} degrees and {condition}, wind around {round(wind or 0)} mph."


async def get_forecast(session: aiohttp.ClientSession, cfg, location: Optional[str] = None) -> str:
    if not _configured(cfg, location):
        raise WeatherError("no weather location is configured")
    lat, lon = await _resolve_location(session, cfg, location)

    async with session.get(_FORECAST_URL, params={
        "latitude": lat, "longitude": lon,
        "daily": "temperature_2m_max,temperature_2m_min,weather_code",
        "temperature_unit": "fahrenheit",
        "forecast_days": 1,
    }) as r:
        data = await r.json()
        if r.status != 200:
            raise WeatherError(f"open-meteo {r.status}: {data}")

    daily = data.get("daily") or {}
    highs = daily.get("temperature_2m_max") or []
    lows = daily.get("temperature_2m_min") or []
    codes = daily.get("weather_code") or []
    if not highs or not lows:
        raise WeatherError(f"unexpected Open-Meteo response shape: {data}")
    condition = _CODE_TEXT.get(codes[0] if codes else None, "unclear skies")
    return f"Today's forecast: a high of {round(highs[0])} and a low of {round(lows[0])}, {condition}."


WEATHER_TOOLS = [
    {
        "name": "weather_get_current",
        "description": "Get the current weather conditions at the configured location.",
        "input_schema": {"type": "object", "properties": {}},
    },
    {
        "name": "weather_get_forecast",
        "description": "Get today's high/low weather forecast at the configured location.",
        "input_schema": {"type": "object", "properties": {}},
    },
]


async def call_tool(session: aiohttp.ClientSession, cfg, name: str, arguments: dict,
                    location: Optional[str] = None) -> str:
    if name == "weather_get_current":
        return await get_current(session, cfg, location)
    if name == "weather_get_forecast":
        return await get_forecast(session, cfg, location)
    raise WeatherError(f"unknown weather tool '{name}'")
