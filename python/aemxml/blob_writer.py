# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
"""Write flattened AEM descriptors to binary descriptor storage (.aem) format."""

from __future__ import annotations

from .flatten import FlatDescriptor, FlatSymbol
from .wire import pack_u16, pack_u32


MAGIC = 0x41454D31  # "AEM1"
HEADER_SIZE = 20
TOC_ENTRY_SIZE = 12
SYMBOL_ENTRY_SIZE = 10


def write_blob(descriptors: list[FlatDescriptor], symbols: list[FlatSymbol]) -> bytes:
    """Write descriptor storage blob from flattened descriptors and symbols."""
    toc_count = len(descriptors)
    symbol_count = len(symbols)
    toc_offset = HEADER_SIZE
    symbol_offset = toc_offset + toc_count * TOC_ENTRY_SIZE
    data_offset = symbol_offset + symbol_count * SYMBOL_ENTRY_SIZE

    # Compute descriptor data offsets
    desc_offsets: list[int] = []
    current_offset = data_offset
    for desc in descriptors:
        desc_offsets.append(current_offset)
        current_offset += len(desc.wire_bytes)

    # Header
    header = b"".join(
        [
            pack_u32(MAGIC),
            pack_u32(toc_count),
            pack_u32(toc_offset),
            pack_u32(symbol_count),
            pack_u32(symbol_offset),
        ]
    )

    # TOC entries
    toc = b""
    for desc, offset in zip(descriptors, desc_offsets):
        toc += b"".join(
            [
                pack_u16(desc.descriptor_type),
                pack_u16(desc.descriptor_index),
                pack_u16(desc.config_index),
                pack_u16(len(desc.wire_bytes)),
                pack_u32(offset),
            ]
        )

    # Symbol entries
    sym_data = b""
    for sym in symbols:
        sym_data += b"".join(
            [
                pack_u16(sym.descriptor_type),
                pack_u16(sym.descriptor_index),
                pack_u16(sym.config_index),
                pack_u32(sym.symbol_code),
            ]
        )

    # Descriptor data
    desc_data = b"".join(d.wire_bytes for d in descriptors)

    return header + toc + sym_data + desc_data
