# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
"""Read binary descriptor storage (.aem) blobs back into flattened descriptor lists."""

from __future__ import annotations

from .flatten import FlatDescriptor, FlatSymbol
from .wire import unpack_u16, unpack_u32


MAGIC = 0x41454D31
HEADER_SIZE = 20
TOC_ENTRY_SIZE = 12
SYMBOL_ENTRY_SIZE = 10


def read_blob(data: bytes) -> tuple[list[FlatDescriptor], list[FlatSymbol]]:
    """Read a descriptor storage blob and return (descriptors, symbols)."""
    if len(data) < HEADER_SIZE:
        raise ValueError(f"Blob too short: {len(data)} < {HEADER_SIZE}")

    magic = unpack_u32(data, 0)
    if magic != MAGIC:
        raise ValueError(f"Bad magic: 0x{magic:08X}, expected 0x{MAGIC:08X}")

    toc_count = unpack_u32(data, 4)
    toc_offset = unpack_u32(data, 8)
    symbol_count = unpack_u32(data, 12)
    symbol_offset = unpack_u32(data, 16)

    # Read TOC
    descriptors: list[FlatDescriptor] = []
    for i in range(toc_count):
        pos = toc_offset + i * TOC_ENTRY_SIZE
        desc_type = unpack_u16(data, pos)
        desc_index = unpack_u16(data, pos + 2)
        config_index = unpack_u16(data, pos + 4)
        length = unpack_u16(data, pos + 6)
        offset = unpack_u32(data, pos + 8)
        wire_bytes = data[offset : offset + length]
        descriptors.append(
            FlatDescriptor(config_index, desc_type, desc_index, wire_bytes)
        )

    # Read symbols
    symbols: list[FlatSymbol] = []
    for i in range(symbol_count):
        pos = symbol_offset + i * SYMBOL_ENTRY_SIZE
        desc_type = unpack_u16(data, pos)
        desc_index = unpack_u16(data, pos + 2)
        config_index = unpack_u16(data, pos + 4)
        symbol_code = unpack_u32(data, pos + 6)
        symbols.append(FlatSymbol(config_index, desc_type, desc_index, symbol_code))

    return descriptors, symbols
