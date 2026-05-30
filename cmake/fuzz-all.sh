#!/bin/sh
# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
#
# Extended fuzzing of every *_fuzzer with platform-specific strategies.
#
# Linux (libFuzzer):     mutation-based fuzzing with persistent corpus
#                        directories under <corpus_dir>/<fuzzer_name>/.
# macOS (standalone):    random-input testing with ASAN/UBSAN.
#
# Usage: fuzz-all.sh <fuzz_bin_dir> <corpus_dir> <duration> <max_len> <count>
#
# Arguments:
#   fuzz_bin_dir  Root directory containing fuzzer executables (searched recursively)
#   corpus_dir    Directory to store corpus files (one subdir per fuzzer)
#   duration      Seconds per fuzzer (Linux libFuzzer; 0 = unlimited)
#   max_len       Max input size in bytes
#   count         Number of random inputs per fuzzer (macOS only)

set -e

if [ $# -ne 5 ]; then
    echo "Usage: $0 <fuzz_bin_dir> <corpus_dir> <duration> <max_len> <count>" >&2
    exit 1
fi

FUZZ_BIN_DIR="$1"
CORPUS_DIR="$2"
DURATION="$3"
MAX_LEN="$4"
COUNT="$5"

mkdir -p "$CORPUS_DIR"

fuzzers=$(find "$FUZZ_BIN_DIR" -type f -name '*_fuzzer' 2>/dev/null | sort)
if [ -z "$fuzzers" ]; then
    echo "ERROR: no *_fuzzer executables found under $FUZZ_BIN_DIR" >&2
    echo "       Did you build with -DENABLE_FUZZING=ON?" >&2
    exit 1
fi

uname_s=$(uname -s)
total=0
failed=0
for fuzzer in $fuzzers; do
    total=$((total + 1))
    name=$(basename "$fuzzer")
    corpus="$CORPUS_DIR/$name"
    mkdir -p "$corpus"
    echo ""
    echo "=== $name ==="
    case "$uname_s" in
        Linux)
            # libFuzzer: mutation-based, bounded by duration
            if ! "$fuzzer" -max_total_time="$DURATION" -max_len="$MAX_LEN" \
                          -timeout=10 -close_fd_mask=3 "$corpus"; then
                echo "FAILED: $name" >&2
                failed=$((failed + 1))
            fi
            ;;
        Darwin|*)
            # Standalone driver: feed N random inputs
            i=0
            while [ $i -lt "$COUNT" ]; do
                seed=$(mktemp)
                head -c "$MAX_LEN" /dev/urandom > "$seed"
                if ! "$fuzzer" "$seed" > /dev/null 2>&1; then
                    echo "FAILED at input $i: $name" >&2
                    failed=$((failed + 1))
                    rm -f "$seed"
                    break
                fi
                rm -f "$seed"
                i=$((i + 1))
            done
            ;;
    esac
done

echo ""
echo "Ran $total fuzzer(s); $failed failure(s)."
exit $failed
