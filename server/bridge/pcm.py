"""The three PCM operations this bridge needs, without `audioop`.

`audioop` was removed from the standard library in Python 3.13. The replacement on PyPI
works, but adding a compiled dependency for a level meter and a resampler is a poor trade
on a box where the install may well be managed and locked down.

Everything here is 16-bit signed mono unless stated otherwise, which is the only format
that crosses this bridge. `array` does the byte-level work, so the per-sample Python is
limited to arithmetic — fast enough for the few hundred milliseconds of speech in a turn.
"""

from __future__ import annotations

import array
import math
import sys


def _samples(pcm: bytes) -> array.array:
    a = array.array("h")
    # A stray odd byte would raise, and a truncated final sample is not worth a crash.
    a.frombytes(pcm[:len(pcm) - (len(pcm) % 2)])
    if sys.byteorder == "big":
        a.byteswap()
    return a


def _bytes(a: array.array) -> bytes:
    if sys.byteorder == "big":
        a = array.array("h", a)
        a.byteswap()
    return a.tobytes()


def rms(pcm: bytes) -> int:
    """Root-mean-square level, on the same 0..32767 scale audioop.rms used."""
    a = _samples(pcm)
    if not a:
        return 0
    total = 0
    for s in a:
        total += s * s
    return int(math.sqrt(total / len(a)))


def to_mono(pcm: bytes, channels: int) -> bytes:
    """Average interleaved channels down to one."""
    if channels <= 1:
        return pcm
    a = _samples(pcm)
    out = array.array("h", bytes(2 * (len(a) // channels)))
    for i in range(len(out)):
        frame = a[i * channels:(i + 1) * channels]
        out[i] = int(sum(frame) / channels)
    return _bytes(out)


def resample(pcm: bytes, src_rate: int, dst_rate: int) -> bytes:
    """Linear-interpolation resample.

    Not a windowed-sinc, and it does not need to be: the material is band-limited speech
    that a neural codec is about to chew on anyway. What matters is that it introduces no
    clicks at the boundaries, which linear interpolation does not.
    """
    if src_rate == dst_rate or not pcm:
        return pcm

    a = _samples(pcm)
    if len(a) < 2:
        return pcm

    ratio = src_rate / dst_rate
    count = max(1, int(len(a) / ratio))
    out = array.array("h", bytes(2 * count))

    for i in range(count):
        pos = i * ratio
        left = int(pos)
        if left >= len(a) - 1:
            out[i] = a[-1]
            continue
        frac = pos - left
        out[i] = int(a[left] * (1.0 - frac) + a[left + 1] * frac)

    return _bytes(out)
