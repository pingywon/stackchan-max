"""Speech in and speech out, plus the Opus codec the device speaks.

The device only ever sends and receives Opus. Everything in the middle — recognition,
the model, synthesis — works in PCM or in whole audio files, so this module is where the
conversion lives.

Opus is handled through `opuslib`, a thin ctypes binding to the system libopus. That means
libopus itself has to be installed (`apt install libopus0`, `brew install opus`); there is
no pure-Python Opus implementation worth using. If the import fails the bridge still
starts and says so clearly rather than dying on the first audio frame.

WAV is assembled by hand. It is a 44-byte header in front of raw samples, and writing it
directly avoids a dependency for something this small.
"""

from __future__ import annotations

import logging
import struct
from typing import List, Optional

import aiohttp

import pcm as pcmutil

log = logging.getLogger("bridge.speech")

try:
    import opuslib

    OPUS_AVAILABLE = True
except Exception as exc:  # pragma: no cover - depends on the host
    opuslib = None
    OPUS_AVAILABLE = False
    log.warning("opuslib unavailable (%s) — audio will not work until it is installed", exc)


class OpusCodec:
    """One decoder and one encoder, both bound to a single conversation.

    Opus is stateful: frames depend on the ones before them, so a codec instance must not
    be shared between sessions or the audio from one bleeds into the other as artefacts.
    """

    def __init__(self, in_rate: int = 16000, out_rate: int = 16000, frame_ms: int = 60):
        self.in_rate = in_rate
        self.out_rate = out_rate
        self.frame_ms = frame_ms
        self._dec = opuslib.Decoder(in_rate, 1) if OPUS_AVAILABLE else None
        self._enc = opuslib.Encoder(out_rate, 1, opuslib.APPLICATION_VOIP) if OPUS_AVAILABLE else None

    @property
    def frame_samples(self) -> int:
        return int(self.out_rate * self.frame_ms / 1000)

    def decode(self, packet: bytes) -> bytes:
        """One Opus packet to 16-bit mono PCM."""
        if self._dec is None:
            return b""
        try:
            # The frame size argument is a maximum; 120 ms at the input rate is well past
            # anything the device sends.
            return self._dec.decode(packet, int(self.in_rate * 120 / 1000))
        except Exception as exc:
            log.debug("opus decode failed on a %d byte packet: %s", len(packet), exc)
            return b""

    def encode(self, pcm: bytes) -> List[bytes]:
        """16-bit mono PCM to a list of Opus packets, one per frame.

        Opus only encodes whole frames, so a trailing partial frame is padded with
        silence rather than dropped — dropping it clips the last syllable.
        """
        if self._enc is None:
            return []

        out: List[bytes] = []
        width = self.frame_samples * 2
        for start in range(0, len(pcm), width):
            chunk = pcm[start:start + width]
            if len(chunk) < width:
                chunk = chunk + b"\x00" * (width - len(chunk))
            try:
                out.append(self._enc.encode(chunk, self.frame_samples))
            except Exception as exc:
                log.debug("opus encode failed: %s", exc)
        return out


def pcm_to_wav(pcm: bytes, rate: int) -> bytes:
    """Wrap raw 16-bit mono PCM in a WAV container."""
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    header += b"data" + struct.pack("<I", len(pcm))
    return header + pcm


def wav_to_pcm(data: bytes, want_rate: int) -> bytes:
    """Pull mono 16-bit PCM at `want_rate` out of a WAV file.

    Resampling is handled by the local pcm module rather than audioop, which no longer
    ships with Python. Anything that is not a WAV is rejected here rather than fed to the
    encoder as noise — an MP3 played as PCM is unmistakable but takes a while to diagnose.
    """
    if not data.startswith(b"RIFF") or data[8:12] != b"WAVE":
        raise ValueError("expected a WAV file from the TTS provider")

    pos, rate, channels, pcm_data = 12, want_rate, 1, b""
    while pos + 8 <= len(data):
        chunk_id = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if chunk_id == b"fmt ":
            channels = struct.unpack("<H", body[2:4])[0]
            rate = struct.unpack("<I", body[4:8])[0]
        elif chunk_id == b"data":
            pcm_data = body
        pos += 8 + size + (size & 1)

    if not pcm_data:
        raise ValueError("WAV file had no data chunk")
    samples = pcm_data
    if channels > 1:
        samples = pcmutil.to_mono(samples, channels)
    return pcmutil.resample(samples, rate, want_rate)


def is_speech(samples: bytes, threshold: int) -> bool:
    """Energy gate, used to decide when a turn has ended.

    Deliberately crude. A real VAD is better at a whisper in a noisy room, but it is
    another native dependency, and the device already runs Espressif's AFE upstream of
    this — what arrives here has been noise-suppressed and gain-controlled already.
    """
    if not samples:
        return False
    return pcmutil.rms(samples) > threshold


# ------------------------------------------------------------- who does speech --
#
# Hearing and the voice are OpenAI-shaped calls (`/audio/transcriptions`,
# `/audio/speech`). Anthropic, Gemini and the rest have no drop-in equivalent, so speech
# is decided separately from who answers: an explicit BRIDGE_ASR_KEY/BRIDGE_TTS_KEY wins,
# then the ChatGPT key, then Groq — the one free provider that serves Whisper and a TTS
# model — so a robot signed in to Groq alone still hears and talks.

_GROQ_BASE = "https://api.groq.com/openai/v1"
_GROQ_ASR_MODEL = "whisper-large-v3-turbo"
_GROQ_TTS_MODEL = "playai-tts"
# OpenAI voice names (what the persona editor offers) mapped onto Groq's PlayAI voices,
# nearest in character. Anything unrecognised gets the first one.
_GROQ_VOICES = {"alloy": "Fritz-PlayAI", "echo": "Mason-PlayAI", "fable": "Cillian-PlayAI",
                "onyx": "Atlas-PlayAI", "nova": "Celeste-PlayAI", "shimmer": "Arista-PlayAI",
                "ash": "Briggs-PlayAI", "coral": "Gail-PlayAI", "sage": "Quinn-PlayAI"}


def _speech_route(cfg, want: str):
    """Returns (base, key, model, source) for want='asr' or 'tts', or raises with a
    message short enough for the device's alert box."""
    explicit_key = cfg.asr_key if want == "asr" else cfg.tts_key
    explicit_base = cfg.asr_base if want == "asr" else cfg.tts_base
    model = cfg.asr_model if want == "asr" else cfg.tts_model
    if explicit_key:
        return (explicit_base or cfg.openai_base).rstrip("/"), explicit_key, model, "custom"
    if cfg.openai_key:
        return (explicit_base or cfg.openai_base).rstrip("/"), cfg.openai_key, model, "openai"
    if cfg.groq_key:
        return _GROQ_BASE, cfg.groq_key, (_GROQ_ASR_MODEL if want == "asr" else _GROQ_TTS_MODEL), "groq"
    what = "Can't hear yet: listening" if want == "asr" else "Can't speak yet: the voice"
    raise RuntimeError(f"{what} needs ChatGPT or Groq signed in on the bridge page, port 8000.")


# --------------------------------------------------------------------------- ASR --

async def transcribe(session: aiohttp.ClientSession, cfg, pcm: bytes, rate: int) -> str:
    """PCM to text. Speaks the OpenAI transcription API, which many services copy."""
    provider = (cfg.asr_provider or "").lower()
    if provider not in ("openai", "openai-compatible", "groq"):
        raise RuntimeError(f"unknown ASR provider '{cfg.asr_provider}'")

    base, key, model, _ = _speech_route(cfg, "asr")

    form = aiohttp.FormData()
    form.add_field("file", pcm_to_wav(pcm, rate),
                   filename="speech.wav", content_type="audio/wav")
    form.add_field("model", model)
    if cfg.asr_language:
        form.add_field("language", cfg.asr_language)

    async with session.post(f"{base}/audio/transcriptions", data=form,
                            headers={"Authorization": f"Bearer {key}"}) as r:
        if r.status != 200:
            raise RuntimeError(f"ASR {r.status}: {await r.text()}")
        data = await r.json()

    return (data.get("text") or "").strip()


# --------------------------------------------------------------------------- TTS --

async def synthesize(session: aiohttp.ClientSession, cfg, text: str,
                     voice: Optional[str], rate: int, speed: Optional[float] = None) -> bytes:
    """Text to 16-bit mono PCM at `rate`.

    WAV is requested explicitly. Every provider here can produce it, and it is the one
    format that needs no decoder on this side.

    `speed` mirrors OpenAI's own `/v1/audio/speech` `speed` parameter (0.25-4.0). There's
    no equivalent pitch control in this API — a portal field for pitch would just be a
    no-op, so one was deliberately never added.
    """
    provider = (cfg.tts_provider or "").lower()
    if provider not in ("openai", "openai-compatible", "groq"):
        raise RuntimeError(f"unknown TTS provider '{cfg.tts_provider}'")

    base, key, model, source = _speech_route(cfg, "tts")
    voice = voice or cfg.tts_voice
    if source == "groq" and not voice.endswith("-PlayAI"):
        voice = _GROQ_VOICES.get(voice.lower(), next(iter(_GROQ_VOICES.values())))

    body = {
        "model": model,
        "input": text,
        "voice": voice,
        "response_format": "wav",
    }
    if source != "groq":
        # Groq's speech endpoint doesn't document `speed`; leave it at the model's own pace.
        body["speed"] = speed or cfg.tts_speed
    async with session.post(f"{base}/audio/speech", json=body,
                            headers={"Authorization": f"Bearer {key}"}) as r:
        if r.status != 200:
            raise RuntimeError(f"TTS {r.status}: {await r.text()}")
        audio = await r.read()

    return wav_to_pcm(audio, rate)
