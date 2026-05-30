#!/bin/sh
# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
#
# Smoke-run every fuzzer once with a random seed.
#
# Usage: fuzz-smoke.sh <fuzz_bin_dir> <seeds_dir>
#
# Fuzzers are auto-discovered: every executable matching *_fuzzer under
# fuzz_bin_dir is run once with a 256-byte random seed. Stays in sync with
# CMake's add_*_fuzzer() calls without a hand-maintained list.

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <fuzz_bin_dir> <seeds_dir>" >&2
    exit 1
fi

FUZZ_BIN_DIR="$1"
SEEDS_DIR="$2"

mkdir -p "$SEEDS_DIR"

fuzzers=$(find "$FUZZ_BIN_DIR" -type f -name '*_fuzzer' 2>/dev/null | sort)
if [ -z "$fuzzers" ]; then
    echo "ERROR: no *_fuzzer executables found under $FUZZ_BIN_DIR" >&2
    echo "       Did you build with -DENABLE_FUZZING=ON?" >&2
    exit 1
fi

failed=0
count=0
for fuzzer in $fuzzers; do
    count=$((count + 1))
    name=$(basename "$fuzzer")
    seed="$SEEDS_DIR/${name}_seed.bin"
    head -c 256 /dev/urandom > "$seed"
    echo "[$count] Running $name with random seed..."
    if ! "$fuzzer" -runs=1 -timeout=5 -close_fd_mask=3 "$seed" 2>&1 | tail -3; then
        echo "  FAILED: $name" >&2
        failed=$((failed + 1))
    fi
done

echo ""
echo "Smoke ran $count fuzzer(s); $failed failure(s)."
exit $failed
