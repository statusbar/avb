# AVB Protocol Parser Fuzzing — Design

Date: 2026-06-06
Scope: `avb` submodule (`statusbar/avtp`, `statusbar/atdecc`, `statusbar/srp`)

## Goal

Add libFuzzer harnesses that feed arbitrary bytes and lengths to every
wire-format parse entry point in the avb protocol stack, verifying under
ASan/UBSan that no parser crashes, asserts, or reads out of bounds on
malformed input.

The recently-fixed parser code is **not modified**. This work adds only new
`*_fuzzer.cpp` harness files plus one `add_avb_fuzzer(...)` line per harness in
the existing module `CMakeLists.txt` files. No seed corpus is generated.

## Constraints

- Toolchain: Clang + libc++, C++23, `-Werror` (mandatory `cmake/toolchain-clang.cmake`).
- Fuzzing is gated behind `-DENABLE_FUZZING=ON`; `add_avb_fuzzer()` is a no-op
  otherwise, so normal builds are unaffected.
- Harnesses follow the existing repo convention exactly (see
  `statusbar/avtp/avtp_aaf_fuzzer.cpp`): a single
  `extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)`
  that wraps the input in a `std::span<uint8_t const>`, calls one parse entry
  point, touches a field or two on success, and returns 0.
- Auto-discovery: the existing `fuzz-smoke` / `fuzz-all` make targets glob every
  `*_fuzzer` binary, so no harness needs hand-registration beyond `add_avb_fuzzer`.

## Approach

**One harness per parse entry point** (the established convention). This keeps
crash attribution unambiguous, lets each harness own its corpus, and matches
`avtp_aaf_fuzzer.cpp`, `gptp_message_fuzzer.cpp`, and
`atdecc_descriptor_storage_fuzzer.cpp`.

Rejected alternatives: a single multiplexed harness per module (muddies crash
attribution and corpus sharing) and structure-aware FuzzedDataProvider harnesses
(more code than the minimal house style warrants).

## Critical rule: `load()` not `load_unchecked()`

Fixed-size PDUs (MAAP, ADP, ACMP, AECP) deserialize through the generic core
serialization API in `core/statusbar/buffer/buffer_protocol.hpp`, which has
three entry points:

- `load_unchecked(span, T*)` — **asserts** `buf.size() >= required`; on a short
  fuzz input it would `abort()`, a false-positive crash. **Never call this from
  a harness.**
- `can_load(span, T*) -> StatusValue<size_t>` — length check only.
- `load(span, T*) -> StatusValue<size_t>` — calls `can_load` first, then loads.
  **This is the correct, length-safe fuzz target for fixed structs.**

Every fixed-struct harness calls `load()` and only touches the result when the
returned `StatusValue` is truthy. The variable-length parsers
(`*_parse_header`, `parse_aem`, `parse_descriptor`) already return
`std::optional` / `StatusValue` and are called directly.

## Harnesses

### AVTP — `statusbar/avtp/` (every `*_parse_header` variant + maap)

Each `*_parse_header(std::span<uint8_t const>) -> std::optional<...>` harness
calls the function and touches a couple of fields on `has_value()`:

| Harness file                      | Entry point                                  | Header |
|-----------------------------------|----------------------------------------------|--------|
| `avtp_am824_fuzzer.cpp`           | `am824_parse_header`                          | `avtp_am824.hpp` |
| `avtp_am824_v1_fuzzer.cpp`        | `am824_v1_parse_header`                        | `avtp_am824_v1.hpp` |
| `avtp_crf_fuzzer.cpp`             | `crf_parse_header`                             | `avtp_crf.hpp` |
| `avtp_crf_v1_fuzzer.cpp`          | `crf_v1_parse_header`                          | `avtp_crf_v1.hpp` |
| `avtp_eecf_fuzzer.cpp`            | `eecf_parse_header`                            | `avtp_eecf.hpp` |
| `avtp_escf_fuzzer.cpp`            | `escf_parse_header`                            | `avtp_escf.hpp` |
| `avtp_aef_fuzzer.cpp`             | `aef_continuous_parse_header` + `aef_discrete_parse_header` | `avtp_aef.hpp` |
| `avtp_ntscf_fuzzer.cpp`           | `ntscf_parse_header`                           | `avtp_ntscf.hpp` |
| `avtp_ntscf_v1_fuzzer.cpp`        | `ntscf_v1_parse_header`                        | `avtp_ntscf_v1.hpp` |
| `avtp_tscf_fuzzer.cpp`            | `tscf_parse_header`                            | `avtp_tscf.hpp` |
| `avtp_tscf_v1_fuzzer.cpp`         | `tscf_v1_parse_header`                         | `avtp_tscf_v1.hpp` |
| `avtp_ip_encap_fuzzer.cpp`        | `ip_avtpdu_parse_header`                        | `avtp_ip_encap.hpp` |
| `avtp_maap_fuzzer.cpp`            | `load(span, &MaapDu)` then `is_valid()` / accessors | `avtp_maap.hpp` |

### ATDECC — `statusbar/atdecc/`

| Harness file                      | What it fuzzes |
|-----------------------------------|----------------|
| `atdecc_adp_fuzzer.cpp`           | `load()` into `AdpDu` and `AdpDu2021`, then `is_valid()` / accessors |
| `atdecc_acmp_fuzzer.cpp`          | `load()` into `AcmpDu` and `AcmpDu2021`, then accessors |
| `atdecc_aecp_fuzzer.cpp`          | `load()` into `AecpDuCommon` and `AecpAaDu` (address-access) |
| `atdecc_aecp_aem_fuzzer.cpp`      | `load()` the `AemDu` header; if `is_aem()`, call `parse_aem(command_code(), is_response(), payload_subspan)` — covers AEM **command** and **response** payloads |
| `atdecc_aem_descriptor_fuzzer.cpp`| `parse_descriptor(span)` |

`parse_aem` / `parse_descriptor` return `StatusValue<... const*>`; the harness
checks the status before touching the parsed result.

### SRP — `statusbar/srp/` (stateful `receive_pdu`)

MSRP/MVRP wire parsing is reached through `receive_pdu(span, now)` on a
participant that drives FSMs/timers. Because `receive_pdu` mutates participant
state, each harness constructs a fresh default participant per input (so runs
stay reproducible — libFuzzer replays a single input deterministically) and
feeds it the fuzz bytes:

| Harness file                | What it fuzzes |
|-----------------------------|----------------|
| `srp_msrp_fuzzer.cpp`       | `MsrpParticipant{MsrpConfig{}}.receive_pdu(span, fixed_now)` |
| `srp_mvrp_fuzzer.cpp`       | `MvrpParticipant{MvrpConfig{}}.receive_pdu(span, fixed_now)` |

`fixed_now` is a constant `TimePoint` (no wall-clock dependency).

### Already covered (untouched)

`avtp_aaf`, `gptp_message`, `atdecc_descriptor_storage`, `stun_decode`,
`netdump_format`, `avtp_pdu`, `avtp_keychain` already have fuzzers.

## CMake wiring

For each new harness, add to the owning module's `CMakeLists.txt`:

```cmake
add_avb_fuzzer(${CMAKE_CURRENT_LIST_DIR}/<name>_fuzzer.cpp)
```

`add_avb_fuzzer` (in `statusbar/CMakeLists.txt`) already builds each as a
libFuzzer binary with `-fsanitize=fuzzer,address,undefined` on Linux (and a
standalone ASan/UBSan driver on Apple), linked against the `statusbar` umbrella.

## Verification

1. Configure a fuzzing build:
   `cmake -G Ninja -B build-fuzz -S . --toolchain cmake/toolchain-clang.cmake -DENABLE_FUZZING=ON`
   (from the umbrella root; aggregate build picks up avb's harnesses).
2. Build all targets; confirm every `*_fuzzer` compiles under `-Werror`.
3. `cmake --build build-fuzz --target fuzz-smoke` — runs each fuzzer once with a
   random seed; must exit clean.
4. A bounded `fuzz-all` run (default 30s/fuzzer) — must find no ASan/UBSan
   crashes. Any crash is a real parser bug to report back (not to fix here,
   per the no-source-change constraint).
5. Format new files via the package `reformat.sh`.

## Success criteria

- ~21 new `*_fuzzer.cpp` files, one per parse entry point listed above.
- All compile under the mandatory clang/libc++ `-Werror` toolchain.
- `fuzz-smoke` passes; a short `fuzz-all` run surfaces no crashes (or surfaces
  genuine bugs, reported but not patched here).
- No changes to any parser source file.
