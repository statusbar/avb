<!-- SPDX-License-Identifier: MIT -->
# Test data

## `simple2.bin`

ATDECC AEM descriptor-storage blob — a binary serialisation of an AVDECC
entity model consumed by `AvbEntityAm824IO::create` via the
`descriptor_storage_blob` configuration field. Loaded as a fixture by
`statusbar/avb_entity/avb_entity_am824_io_test.cpp`.

The blob describes a single 8-channel AM824 entity:

- 1 entity
- 1 configuration
- 1 stream input
- 1 stream output
- 1 AVB interface

These counts are asserted by the test that consumes it.

The blob is **generated externally** from an AEMXML model description; no
in-tree generator currently exists, so this file is a committed fixture.
If regenerated, the expected descriptor counts in
`avb_entity_am824_io_test.cpp` must be updated to match.

To inspect the blob, run `statusbar-descriptor-storage` (see
`statusbar/atdecc/atdecc_descriptor_storage_tool.cpp`).
