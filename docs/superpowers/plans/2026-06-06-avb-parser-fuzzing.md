# AVB Protocol Parser Fuzzing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add ~20 libFuzzer harnesses (one per wire-format parse entry point) across the avb submodule so every protocol parser is exercised against arbitrary bytes/lengths under ASan/UBSan, without modifying any parser source.

**Architecture:** Each harness is a tiny `extern "C" int LLVMFuzzerTestOneInput(...)` that wraps the input in `std::span<uint8_t const>` and calls exactly one parse entry point, following the existing `statusbar/avtp/avtp_aaf_fuzzer.cpp`. Fixed-size PDUs use the length-checked `statusbar::load()` (never the asserting `load_unchecked()`); variable-length parsers (`*_parse_header`, `parse_aem`, `parse_descriptor`) and the stateful SRP `receive_pdu()` are called directly. Each harness is registered with the existing `add_avb_fuzzer()` CMake helper and auto-discovered by the `fuzz-smoke`/`fuzz-all` targets.

**Tech Stack:** C++23, Clang + libc++, libFuzzer (`-fsanitize=fuzzer,address,undefined`), CMake/Ninja, the mandatory `cmake/toolchain-clang.cmake`.

---

## Background the implementer must know

- **Build only works with the pinned toolchain.** All configure commands pass `--toolchain cmake/toolchain-clang.cmake` from the umbrella root `/home/jeffk/src/statusbar-umbrella`.
- **Sanitizer coverage of the library matters.** `-DENABLE_FUZZING=ON` instruments only the fuzzer `.cpp`; the parser code lives in `.cpp` files compiled into the `statusbar` library, which is built clean unless a global sanitizer is on. To catch OOB reads *inside the parsers*, the verification build MUST add `-DENABLE_ASAN=ON`. `ENABLE_ASAN`, `ENABLE_UBSAN`, `ENABLE_TSAN` are mutually exclusive (CMake `FATAL_ERROR` otherwise), so UBSan is a separate second build dir.
- **`add_avb_fuzzer(<abs path to .cpp>)`** is defined in `avb/statusbar/CMakeLists.txt` and is in scope inside every module subdirectory (`avtp`, `atdecc`, `srp`). It is a no-op unless `ENABLE_FUZZING` is on, so these additions never affect normal builds. The fuzzer binary's name is the source file stem (e.g. `avtp_crf_fuzzer.cpp` → target/binary `avtp_crf_fuzzer`).
- **`using namespace statusbar;`** brings the generic `load(span, T*) -> StatusValue<size_t>` into scope (the avb tests call it unqualified). `StatusValue` is truthy on success.
- **Do not modify any parser source.** If fuzzing finds a crash, it is reported back, not patched here.
- **Formatting:** run `./reformat.sh` from `avb/` after creating files (clang-format).
- **Reference harness** (the house style to copy), `avb/statusbar/avtp/avtp_aaf_fuzzer.cpp`:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for avtp::aaf_parse_header. The parser consumes raw
/// AVTP/AAF wire bytes from an untrusted source and must not crash or
/// read out of bounds on any input. May legally return std::nullopt;
/// we look for UB via ASan/UBSan.

#include "statusbar/avtp/avtp_aaf.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);
    auto const pdu = statusbar::avtp::aaf_parse_header(packet);
    if (pdu.has_value()) {
        (void)pdu->stream_data_length;
        (void)pdu->sv_version_flags;
    }
    return 0;
}
```

All new harnesses use a type-agnostic success-touch — `auto const sink = *pdu; (void)sink;` — so no per-PDU field names are needed (every PDU is a trivially-copyable wire struct).

---

## Task 0: Configure the fuzzing build directory

**Files:** none (build setup only)

- [ ] **Step 1: Configure an ASan + fuzzing build from the umbrella root**

Run:
```bash
cd /home/jeffk/src/statusbar-umbrella
cmake -G Ninja -B build-fuzz -S . \
  --toolchain cmake/toolchain-clang.cmake \
  -DENABLE_FUZZING=ON -DENABLE_ASAN=ON
```
Expected: configure succeeds; output includes `Fuzzing toolchain enabled` and `AddressSanitizer enabled`. (Ninja auto-reconfigures on later `CMakeLists.txt` edits, so this is the only configure needed until the UBSan pass in Task 7.)

- [ ] **Step 2: Confirm the existing fuzzer still builds (baseline sanity)**

Run:
```bash
cmake --build build-fuzz --target avtp_aaf_fuzzer
```
Expected: builds clean. Confirms toolchain + fuzzing flags work before adding new harnesses.

---

## Task 1: AVTP `*_parse_header` clone harnesses (11 files)

These eleven entry points share one shape: `<fn>(std::span<uint8_t const>) noexcept -> std::optional<Pdu>`. Create one file per row below. Each file is the template with `{HEADER}` and `{FN}` substituted from the table — all in namespace `statusbar::avtp`.

**Template** (replace `{HEADER}` and `{FN}`; the doc comment names the function):

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for avtp::{FN}. Consumes raw AVTP wire bytes from an
/// untrusted source; must not crash or read out of bounds on any input. May
/// legally return std::nullopt. We look for OOB/UB via ASan/UBSan.

#include "statusbar/avtp/{HEADER}"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);
    auto const pdu = statusbar::avtp::{FN}(packet);
    if (pdu.has_value()) {
        auto const sink = *pdu;
        (void)sink;
    }
    return 0;
}
```

**Files:** Create under `avb/statusbar/avtp/`:

| Source file                  | `{HEADER}`            | `{FN}`                  |
|------------------------------|-----------------------|-------------------------|
| `avtp_am824_fuzzer.cpp`      | `avtp_am824.hpp`      | `am824_parse_header`    |
| `avtp_am824_v1_fuzzer.cpp`   | `avtp_am824_v1.hpp`   | `am824_v1_parse_header` |
| `avtp_crf_fuzzer.cpp`        | `avtp_crf.hpp`        | `crf_parse_header`      |
| `avtp_crf_v1_fuzzer.cpp`     | `avtp_crf_v1.hpp`     | `crf_v1_parse_header`   |
| `avtp_eecf_fuzzer.cpp`       | `avtp_eecf.hpp`       | `eecf_parse_header`     |
| `avtp_escf_fuzzer.cpp`       | `avtp_escf.hpp`       | `escf_parse_header`     |
| `avtp_ntscf_fuzzer.cpp`      | `avtp_ntscf.hpp`      | `ntscf_parse_header`    |
| `avtp_ntscf_v1_fuzzer.cpp`   | `avtp_ntscf_v1.hpp`   | `ntscf_v1_parse_header` |
| `avtp_tscf_fuzzer.cpp`       | `avtp_tscf.hpp`       | `tscf_parse_header`     |
| `avtp_tscf_v1_fuzzer.cpp`    | `avtp_tscf_v1.hpp`    | `tscf_v1_parse_header`  |
| `avtp_ip_encap_fuzzer.cpp`   | `avtp_ip_encap.hpp`   | `ip_avtpdu_parse_header`|

- [ ] **Step 1: Create all 11 files** using the template + table above.

- [ ] **Step 2: Register them in CMake**

Modify `avb/statusbar/avtp/CMakeLists.txt`: directly **below** the existing line
`add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_aaf_fuzzer.cpp)`, add:

```cmake
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_am824_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_am824_v1_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_crf_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_crf_v1_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_eecf_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_escf_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_ntscf_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_ntscf_v1_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_tscf_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_tscf_v1_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_ip_encap_fuzzer.cpp)
```

- [ ] **Step 3: Build all 11 targets**

Run:
```bash
cd /home/jeffk/src/statusbar-umbrella
cmake --build build-fuzz --target \
  avtp_am824_fuzzer avtp_am824_v1_fuzzer avtp_crf_fuzzer avtp_crf_v1_fuzzer \
  avtp_eecf_fuzzer avtp_escf_fuzzer avtp_ntscf_fuzzer avtp_ntscf_v1_fuzzer \
  avtp_tscf_fuzzer avtp_tscf_v1_fuzzer avtp_ip_encap_fuzzer
```
Expected: all link clean under `-Werror`.

- [ ] **Step 4: Smoke-run each new binary (bounded, must not crash)**

Run:
```bash
cd /home/jeffk/src/statusbar-umbrella
for b in avtp_am824_fuzzer avtp_am824_v1_fuzzer avtp_crf_fuzzer avtp_crf_v1_fuzzer \
         avtp_eecf_fuzzer avtp_escf_fuzzer avtp_ntscf_fuzzer avtp_ntscf_v1_fuzzer \
         avtp_tscf_fuzzer avtp_tscf_v1_fuzzer avtp_ip_encap_fuzzer; do
  echo "== $b =="; "$(find build-fuzz -name "$b" -type f | head -1)" -runs=20000 -max_len=2048 || break
done
```
Expected: each prints `Done NNNNN runs` and exits 0. A non-zero exit / ASan report is a real parser bug — stop and report it (do not patch parser source).

- [ ] **Step 5: Format and commit**

```bash
cd /home/jeffk/src/statusbar-umbrella/avb && ./reformat.sh
git -C /home/jeffk/src/statusbar-umbrella/avb add \
  statusbar/avtp/avtp_am824_fuzzer.cpp statusbar/avtp/avtp_am824_v1_fuzzer.cpp \
  statusbar/avtp/avtp_crf_fuzzer.cpp statusbar/avtp/avtp_crf_v1_fuzzer.cpp \
  statusbar/avtp/avtp_eecf_fuzzer.cpp statusbar/avtp/avtp_escf_fuzzer.cpp \
  statusbar/avtp/avtp_ntscf_fuzzer.cpp statusbar/avtp/avtp_ntscf_v1_fuzzer.cpp \
  statusbar/avtp/avtp_tscf_fuzzer.cpp statusbar/avtp/avtp_tscf_v1_fuzzer.cpp \
  statusbar/avtp/avtp_ip_encap_fuzzer.cpp statusbar/avtp/CMakeLists.txt
git -C /home/jeffk/src/statusbar-umbrella/avb commit -m "avtp: fuzz harnesses for parse_header family"
```

> Note: commit inside the `avb` submodule (explicit paths only — never `git add -A` in the umbrella).

---

## Task 2: AVTP AEF harness (two parse functions)

**Files:**
- Create: `avb/statusbar/avtp/avtp_aef_fuzzer.cpp`
- Modify: `avb/statusbar/avtp/CMakeLists.txt`

- [ ] **Step 1: Create `avtp_aef_fuzzer.cpp`** (exercises both AEF parse entry points on the same input):

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for avtp::aef_continuous_parse_header and
/// avtp::aef_discrete_parse_header. Both consume raw AVTP/AEF wire bytes from an
/// untrusted source; neither may crash or read out of bounds on any input.

#include "statusbar/avtp/avtp_aef.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);

    auto const cont = statusbar::avtp::aef_continuous_parse_header(packet);
    if (cont.has_value()) {
        auto const sink = *cont;
        (void)sink;
    }

    auto const disc = statusbar::avtp::aef_discrete_parse_header(packet);
    if (disc.has_value()) {
        auto const sink = *disc;
        (void)sink;
    }
    return 0;
}
```

- [ ] **Step 2: Register in CMake** — append to the fuzz-harness block in `avb/statusbar/avtp/CMakeLists.txt`:

```cmake
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_aef_fuzzer.cpp)
```

- [ ] **Step 3: Build**

Run: `cmake --build /home/jeffk/src/statusbar-umbrella/build-fuzz --target avtp_aef_fuzzer`
Expected: links clean.

- [ ] **Step 4: Smoke-run**

Run:
```bash
"$(find /home/jeffk/src/statusbar-umbrella/build-fuzz -name avtp_aef_fuzzer -type f | head -1)" -runs=20000 -max_len=2048
```
Expected: `Done` and exit 0.

- [ ] **Step 5: Format and commit**

```bash
cd /home/jeffk/src/statusbar-umbrella/avb && ./reformat.sh
git -C /home/jeffk/src/statusbar-umbrella/avb add statusbar/avtp/avtp_aef_fuzzer.cpp statusbar/avtp/CMakeLists.txt
git -C /home/jeffk/src/statusbar-umbrella/avb commit -m "avtp: fuzz harness for AEF parse headers"
```

---

## Task 3: AVTP MAAP harness (fixed-struct via `load`)

**Files:**
- Create: `avb/statusbar/avtp/avtp_maap_fuzzer.cpp`
- Modify: `avb/statusbar/avtp/CMakeLists.txt`

MAAP has no `parse_header`; ingress deserializes the fixed 28-byte `MaapDu` via the length-checked `load()`, then validates with `is_valid()`.

- [ ] **Step 1: Create `avtp_maap_fuzzer.cpp`**:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for MaapDu deserialization. Uses the length-checked
/// statusbar::load() (NOT load_unchecked, which asserts on short buffers) so
/// arbitrary input lengths are safe. Exercises is_valid() / accessors on
/// success. Looks for OOB/UB via ASan/UBSan.

#include "statusbar/avtp/avtp_maap.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using namespace statusbar;

    auto const packet = std::span<uint8_t const>(data, size);
    avtp::MaapDu du{};
    if (load(packet, &du)) {
        (void)du.is_valid();
        (void)du.message_type();
        (void)du.maap_data_length();
    }
    return 0;
}
```

- [ ] **Step 2: Register in CMake** — append to the fuzz-harness block in `avb/statusbar/avtp/CMakeLists.txt`:

```cmake
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/avtp_maap_fuzzer.cpp)
```

- [ ] **Step 3: Build**

Run: `cmake --build /home/jeffk/src/statusbar-umbrella/build-fuzz --target avtp_maap_fuzzer`
Expected: links clean. (If `load` is ambiguous/not found, add `#include "statusbar/buffer/buffer.hpp"` — but `avtp_maap.hpp` already pulls in the buffer protocol.)

- [ ] **Step 4: Smoke-run**

Run: `"$(find /home/jeffk/src/statusbar-umbrella/build-fuzz -name avtp_maap_fuzzer -type f | head -1)" -runs=20000 -max_len=256`
Expected: `Done` and exit 0.

- [ ] **Step 5: Format and commit**

```bash
cd /home/jeffk/src/statusbar-umbrella/avb && ./reformat.sh
git -C /home/jeffk/src/statusbar-umbrella/avb add statusbar/avtp/avtp_maap_fuzzer.cpp statusbar/avtp/CMakeLists.txt
git -C /home/jeffk/src/statusbar-umbrella/avb commit -m "avtp: fuzz harness for MaapDu load"
```

---

## Task 4: ATDECC fixed-struct harnesses (ADP, ACMP, AECP)

**Files:**
- Create: `avb/statusbar/atdecc/atdecc_adp_fuzzer.cpp`, `atdecc_acmp_fuzzer.cpp`, `atdecc_aecp_fuzzer.cpp`
- Modify: `avb/statusbar/atdecc/CMakeLists.txt`

All deserialize fixed-size PDUs via the length-checked `load()`. ADP has only `AdpDu` (no 2021 variant). ACMP has `AcmpDu` and `AcmpDu2021`. AECP has `AecpDuCommon` and the address-access `AecpAaDu`.

- [ ] **Step 1: Create `atdecc_adp_fuzzer.cpp`**:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for AdpDu deserialization via length-checked load().

#include "statusbar/atdecc/atdecc_adp.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using namespace statusbar;

    auto const packet = std::span<uint8_t const>(data, size);
    atdecc::AdpDu du{};
    if (load(packet, &du)) {
        (void)du.is_valid();
        (void)du.message_type();
        (void)du.control_data_length();
    }
    return 0;
}
```

- [ ] **Step 2: Create `atdecc_acmp_fuzzer.cpp`**:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for AcmpDu / AcmpDu2021 deserialization via length-checked
/// load(). Both wire layouts are exercised on the same input.

#include "statusbar/atdecc/atdecc_acmp_pdu.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using namespace statusbar;

    auto const packet = std::span<uint8_t const>(data, size);

    atdecc::AcmpDu du{};
    if (load(packet, &du)) {
        (void)du.message_type();
        (void)du.status();
    }

    atdecc::AcmpDu2021 du2021{};
    if (load(packet, &du2021)) {
        auto const sink = du2021;
        (void)sink;
    }
    return 0;
}
```

- [ ] **Step 3: Create `atdecc_aecp_fuzzer.cpp`**:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for AECP common header and Address-Access PDU
/// deserialization via length-checked load().

#include "statusbar/atdecc/atdecc_aecp.hpp"
#include "statusbar/atdecc/atdecc_aecp_aa.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using namespace statusbar;

    auto const packet = std::span<uint8_t const>(data, size);

    atdecc::AecpDuCommon common{};
    if (load(packet, &common)) {
        (void)common.message_type();
        (void)common.is_response();
    }

    atdecc::AecpAaDu aa{};
    if (load(packet, &aa)) {
        auto const sink = aa;
        (void)sink;
    }
    return 0;
}
```

- [ ] **Step 4: Register all three in CMake** — directly **below** the existing
`add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/atdecc_descriptor_storage_fuzzer.cpp)`
in `avb/statusbar/atdecc/CMakeLists.txt`, add:

```cmake
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/atdecc_adp_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/atdecc_acmp_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/atdecc_aecp_fuzzer.cpp)
```

- [ ] **Step 5: Build all three**

Run:
```bash
cmake --build /home/jeffk/src/statusbar-umbrella/build-fuzz --target \
  atdecc_adp_fuzzer atdecc_acmp_fuzzer atdecc_aecp_fuzzer
```
Expected: link clean. If any accessor name (`status()`, `is_response()`, `control_data_length()`) does not exist on its type, replace that line with the type-agnostic `auto const sink = du; (void)sink;` touch — the harness only needs `load()` plus a read.

- [ ] **Step 6: Smoke-run each**

Run:
```bash
for b in atdecc_adp_fuzzer atdecc_acmp_fuzzer atdecc_aecp_fuzzer; do
  echo "== $b =="; "$(find /home/jeffk/src/statusbar-umbrella/build-fuzz -name "$b" -type f | head -1)" -runs=20000 -max_len=512 || break
done
```
Expected: each `Done`, exit 0.

- [ ] **Step 7: Format and commit**

```bash
cd /home/jeffk/src/statusbar-umbrella/avb && ./reformat.sh
git -C /home/jeffk/src/statusbar-umbrella/avb add \
  statusbar/atdecc/atdecc_adp_fuzzer.cpp statusbar/atdecc/atdecc_acmp_fuzzer.cpp \
  statusbar/atdecc/atdecc_aecp_fuzzer.cpp statusbar/atdecc/CMakeLists.txt
git -C /home/jeffk/src/statusbar-umbrella/avb commit -m "atdecc: fuzz harnesses for ADP/ACMP/AECP PDU load"
```

---

## Task 5: ATDECC variable-length parsers (AEM command/response + descriptor)

**Files:**
- Create: `avb/statusbar/atdecc/atdecc_aecp_aem_fuzzer.cpp`, `atdecc_aem_descriptor_fuzzer.cpp`
- Modify: `avb/statusbar/atdecc/CMakeLists.txt`

`parse_aem(uint16_t cmd, bool is_response, span) -> StatusValue<aem::ParsedAemPayload const*>` parses both AEM commands and responses; we derive `cmd`/`is_response` from a loaded `AemDu` header (which validates the 24-byte minimum) and pass the trailing payload. `parse_descriptor(span) -> StatusValue<aem::ParsedDescriptor const*>` parses a raw descriptor.

- [ ] **Step 1: Create `atdecc_aecp_aem_fuzzer.cpp`**:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for AEM command/response payload parsing. Loads the
/// fixed AemDu header (length-checked), then runs parse_aem() over the trailing
/// payload with the header-derived command code and direction. Covers both AEM
/// commands and responses.

#include "statusbar/atdecc/atdecc_aecp_aem.hpp"
#include "statusbar/atdecc/atdecc_aem_format.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using namespace statusbar;

    auto const packet = std::span<uint8_t const>(data, size);
    atdecc::AemDu hdr{};
    if (load(packet, &hdr)) {
        // load() succeeded => packet.size() >= AemDu::LENGTH, so the subspan is valid.
        auto const payload = packet.subspan(atdecc::AemDu::LENGTH);
        auto const parsed = atdecc::parse_aem(hdr.command_code(), hdr.is_response(), payload);
        (void)parsed;
    }
    return 0;
}
```

- [ ] **Step 2: Create `atdecc_aem_descriptor_fuzzer.cpp`**:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for aem::parse_descriptor. Consumes raw descriptor bytes
/// from an untrusted source; must not crash or read out of bounds on any input.

#include "statusbar/atdecc/atdecc_aem_format.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const desc = std::span<uint8_t const>(data, size);
    auto const parsed = statusbar::atdecc::aem::parse_descriptor(desc);
    (void)parsed;
    return 0;
}
```

- [ ] **Step 3: Register both in CMake** — append to the fuzz-harness block in `avb/statusbar/atdecc/CMakeLists.txt`:

```cmake
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/atdecc_aecp_aem_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/atdecc_aem_descriptor_fuzzer.cpp)
```

- [ ] **Step 4: Build both**

Run:
```bash
cmake --build /home/jeffk/src/statusbar-umbrella/build-fuzz --target \
  atdecc_aecp_aem_fuzzer atdecc_aem_descriptor_fuzzer
```
Expected: link clean.

- [ ] **Step 5: Smoke-run each**

Run:
```bash
for b in atdecc_aecp_aem_fuzzer atdecc_aem_descriptor_fuzzer; do
  echo "== $b =="; "$(find /home/jeffk/src/statusbar-umbrella/build-fuzz -name "$b" -type f | head -1)" -runs=50000 -max_len=600 || break
done
```
Expected: each `Done`, exit 0. (These are the highest-yield targets — let them run a bit longer.)

- [ ] **Step 6: Format and commit**

```bash
cd /home/jeffk/src/statusbar-umbrella/avb && ./reformat.sh
git -C /home/jeffk/src/statusbar-umbrella/avb add \
  statusbar/atdecc/atdecc_aecp_aem_fuzzer.cpp statusbar/atdecc/atdecc_aem_descriptor_fuzzer.cpp \
  statusbar/atdecc/CMakeLists.txt
git -C /home/jeffk/src/statusbar-umbrella/avb commit -m "atdecc: fuzz harnesses for parse_aem and parse_descriptor"
```

---

## Task 6: SRP MSRP/MVRP harnesses (stateful `receive_pdu`)

**Files:**
- Create: `avb/statusbar/srp/srp_msrp_fuzzer.cpp`, `srp_mvrp_fuzzer.cpp`
- Modify: `avb/statusbar/srp/CMakeLists.txt`

MSRP/MVRP wire parsing runs inside `receive_pdu(span, now)` on a participant that drives FSMs. Each input gets a fresh default participant (reproducible single-input replay). `now` is a fixed, non-wall-clock `TimePoint`. The default `MsrpParticipant`/`MvrpParticipant` (`MsrpParticipantT<>` / `MvrpParticipantT<>`) are explicitly instantiated in the library, so linking `statusbar` resolves them.

- [ ] **Step 1: Create `srp_msrp_fuzzer.cpp`**:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for MSRP ingress parsing. Feeds arbitrary bytes to
/// MsrpParticipant::receive_pdu, which walks the MRPDU attribute/vector
/// structure. A fresh participant per input keeps single-input replay
/// reproducible. Looks for OOB/UB via ASan/UBSan.

#include "statusbar/srp/srp_msrp_participant.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using statusbar::srp::msrp::MsrpConfig;
    using statusbar::srp::msrp::MsrpParticipant;

    MsrpParticipant participant{MsrpConfig{}, 0};
    auto const now = statusbar::sm::TimePoint{} + std::chrono::seconds(1);
    participant.receive_pdu(std::span<uint8_t const>(data, size), now);
    return 0;
}
```

- [ ] **Step 2: Create `srp_mvrp_fuzzer.cpp`**:

```cpp
// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for MVRP ingress parsing. Feeds arbitrary bytes to
/// MvrpParticipant::receive_pdu, which walks the MRPDU attribute/vector
/// structure. A fresh participant per input keeps single-input replay
/// reproducible. Looks for OOB/UB via ASan/UBSan.

#include "statusbar/srp/srp_mvrp_participant.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using statusbar::srp::mvrp::MvrpConfig;
    using statusbar::srp::mvrp::MvrpParticipant;

    MvrpParticipant participant{MvrpConfig{}, 0};
    auto const now = statusbar::sm::TimePoint{} + std::chrono::seconds(1);
    participant.receive_pdu(std::span<uint8_t const>(data, size), now);
    return 0;
}
```

> If the exact namespaces of `MsrpConfig`/`MsrpParticipant` differ (verify in `srp_msrp_participant.hpp` / `srp_mvrp_participant.hpp` — they are the `using ... = ...T<>` aliases near the file end), adjust the `using` lines. The participant ctor is `explicit (Config const&, uint64_t rng_seed = 0)`. `statusbar::sm::TimePoint` comes from `statusbar/sm/sm_core.hpp`, pulled in transitively by the participant header (add `#include "statusbar/sm/sm_core.hpp"` if the build complains).

- [ ] **Step 3: Register both in CMake** — append at the end of `avb/statusbar/srp/CMakeLists.txt` (after the `srp_sm_tool` block):

```cmake
# Fuzz harnesses (built only when -DENABLE_FUZZING=ON; defaults ON for Clang).
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/srp_msrp_fuzzer.cpp)
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/srp_mvrp_fuzzer.cpp)
```

- [ ] **Step 4: Build both**

Run:
```bash
cmake --build /home/jeffk/src/statusbar-umbrella/build-fuzz --target \
  srp_msrp_fuzzer srp_mvrp_fuzzer
```
Expected: link clean. (A linker error for `MsrpParticipantT<...>` means the alias used is not the explicitly-instantiated default; use the `MsrpParticipant` / `MvrpParticipant` aliases exactly as above.)

- [ ] **Step 5: Smoke-run each**

Run:
```bash
for b in srp_msrp_fuzzer srp_mvrp_fuzzer; do
  echo "== $b =="; "$(find /home/jeffk/src/statusbar-umbrella/build-fuzz -name "$b" -type f | head -1)" -runs=50000 -max_len=600 || break
done
```
Expected: each `Done`, exit 0.

- [ ] **Step 6: Format and commit**

```bash
cd /home/jeffk/src/statusbar-umbrella/avb && ./reformat.sh
git -C /home/jeffk/src/statusbar-umbrella/avb add \
  statusbar/srp/srp_msrp_fuzzer.cpp statusbar/srp/srp_mvrp_fuzzer.cpp statusbar/srp/CMakeLists.txt
git -C /home/jeffk/src/statusbar-umbrella/avb commit -m "srp: fuzz harnesses for MSRP/MVRP receive_pdu"
```

---

## Task 7: Full-suite verification (ASan + UBSan)

**Files:** none (verification only)

- [ ] **Step 1: Build every fuzzer in the ASan build**

Run:
```bash
cd /home/jeffk/src/statusbar-umbrella
cmake --build build-fuzz
```
Expected: whole aggregate builds clean under `-Werror` with all 20 new `*_fuzzer` targets present.

- [ ] **Step 2: Run the project's `fuzz-smoke` target (every fuzzer once)**

Run:
```bash
cmake --build build-fuzz --target fuzz-smoke
```
Expected: each `*_fuzzer` (existing + new) runs once and reports success; overall exit 0. Any crash names the offending binary — report it.

- [ ] **Step 3: Bounded `fuzz-all` campaign under ASan**

Run:
```bash
cmake --build build-fuzz --target fuzz-all
```
Expected: 30s per fuzzer, no ASan crashes. Crash inputs (if any) land under `build-fuzz/fuzz/corpus`; collect and report them.

- [ ] **Step 4: Second pass under UBSan (separate build dir, mutually exclusive with ASan)**

Run:
```bash
cd /home/jeffk/src/statusbar-umbrella
cmake -G Ninja -B build-fuzz-ubsan -S . \
  --toolchain cmake/toolchain-clang.cmake \
  -DENABLE_FUZZING=ON -DENABLE_UBSAN=ON
cmake --build build-fuzz-ubsan --target fuzz-smoke
cmake --build build-fuzz-ubsan --target fuzz-all
```
Expected: clean. UBSan now instruments the parser library code, catching signed-overflow / misaligned-load / OOB-index UB the ASan pass may miss.

- [ ] **Step 5: Confirm normal (non-fuzzing) build + tests are unaffected**

Run:
```bash
cd /home/jeffk/src/statusbar-umbrella && ./local-build.sh
```
Expected: configures without fuzzing, builds, and `ctest` passes — proving the `add_avb_fuzzer` additions are inert in normal builds.

- [ ] **Step 6: Report results**

Summarize: number of harnesses added (20 files / 21 entry points), `fuzz-smoke` + `fuzz-all` clean under both ASan and UBSan, normal build/tests green. If any crash was found, report the binary name + minimized crash input and the parser involved — **do not modify parser source** (per the project owner's instruction).

---

## Self-review notes (coverage map)

- avtp every `*_parse_header` variant → Tasks 1 (11) + 2 (aef, 2 fns) + 3 (maap) ✓
- atdecc adp / acmp / aecp (fixed structs) → Task 4 ✓
- atdecc descriptor / command / response (variable) → Task 5 (`parse_descriptor`, `parse_aem`) ✓
- msrp / mvrp (stateful receive_pdu) → Task 6 ✓
- Lib parser code actually instrumented (ASan + UBSan, not just the fuzzer TU) → Task 0 + Task 7 ✓
- No parser source modified; crashes reported not patched → stated in Tasks 1–7 ✓
- Naming consistent: binary stem == source stem == `add_avb_fuzzer` arg throughout ✓
