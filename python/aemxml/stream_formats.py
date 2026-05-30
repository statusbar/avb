# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
"""AAF stream format encode/decode per IEEE 1722-2016."""

from __future__ import annotations

# NSR codes from IEEE 1722-2016 Table 11
_RATE_TO_NSR: dict[int, int] = {
    8000: 0x01,
    16000: 0x02,
    32000: 0x03,
    44100: 0x04,
    48000: 0x05,
    88200: 0x06,
    96000: 0x07,
    176400: 0x08,
    192000: 0x09,
    24000: 0x0A,
}
_NSR_TO_RATE: dict[int, int] = {v: k for k, v in _RATE_TO_NSR.items()}

AAF_SUBTYPE = 0x02


def encode_aaf_stream_format(rate: int, channels: int, depth: int) -> int:
    """Encode AAF parameters into an 8-byte IEEE 1722 stream format value.

    Uses the AVTP-defined format (v=0):
    Byte 0: 0x02 (AAF subtype)
    Byte 1: nsr[7:4] | 00 | channels_per_frame[9:8]
    Byte 2: channels_per_frame[7:0]
    Byte 3: bit_depth
    Bytes 4-7: 0x00
    """
    if rate not in _RATE_TO_NSR:
        raise ValueError(
            f"Unsupported sample rate {rate}, valid: {sorted(_RATE_TO_NSR.keys())}"
        )
    if channels < 1 or channels > 1023:
        raise ValueError(f"Channels must be 1-1023, got {channels}")
    if depth < 1 or depth > 255:
        raise ValueError(f"Bit depth must be 1-255, got {depth}")

    nsr = _RATE_TO_NSR[rate]
    channels_hi = (channels >> 8) & 0x03
    channels_lo = channels & 0xFF

    b0 = AAF_SUBTYPE
    b1 = (nsr << 4) | channels_hi
    b2 = channels_lo
    b3 = depth

    return (b0 << 56) | (b1 << 48) | (b2 << 40) | (b3 << 32)


def decode_aaf_stream_format(fmt: int) -> dict | None:
    """Decode an 8-byte stream format to AAF parameters.

    Returns {"type": "AAF", "rate": int, "channels": int, "depth": int} or None
    if the format is not AAF.
    """
    b0 = (fmt >> 56) & 0xFF
    if b0 != AAF_SUBTYPE:
        return None

    b1 = (fmt >> 48) & 0xFF
    b2 = (fmt >> 40) & 0xFF
    b3 = (fmt >> 32) & 0xFF

    nsr = (b1 >> 4) & 0x0F
    channels_hi = b1 & 0x03
    channels = (channels_hi << 8) | b2
    depth = b3

    rate = _NSR_TO_RATE.get(nsr)
    if rate is None:
        return None

    return {"type": "AAF", "rate": rate, "channels": channels, "depth": depth}


def parse_stream_format(fmt_obj) -> int:
    """Parse a stream format from JSON (structured object or hex string) to int."""
    if isinstance(fmt_obj, str):
        return int(fmt_obj, 16)
    if isinstance(fmt_obj, dict):
        fmt_type = fmt_obj.get("type", "")
        if fmt_type == "AAF":
            return encode_aaf_stream_format(
                fmt_obj["rate"], fmt_obj["channels"], fmt_obj["depth"]
            )
        raise ValueError(f"Unsupported stream format type: {fmt_type!r}")
    raise ValueError(f"Stream format must be object or hex string, got {type(fmt_obj)}")


def format_stream_format(fmt: int) -> dict | str:
    """Convert a stream format int to structured object (AAF) or hex string."""
    decoded = decode_aaf_stream_format(fmt)
    if decoded is not None:
        return decoded
    return f"0x{fmt:016X}"
