#!/bin/sh
# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
#
# Collect LLVM coverage data from a test binary run.
#
# Runs the unified statusbar test binary with LLVM_PROFILE_FILE pointing into
# COVERAGE_DIR, then merges the resulting .profraw files into combined.profdata.
#
# Usage: coverage-collect.sh <coverage_dir> <llvm_profdata> <test_bin>
#
# Arguments:
#   coverage_dir    Directory to store coverage artifacts (created if absent)
#   llvm_profdata   Path to the llvm-profdata tool
#   test_bin        Path to the statusbar test binary

set -e

if [ $# -ne 3 ]; then
    echo "Usage: $0 <coverage_dir> <llvm_profdata> <test_bin>" >&2
    exit 1
fi

COVERAGE_DIR="$1"
LLVM_PROFDATA="$2"
TEST_BIN="$3"

if [ ! -x "$LLVM_PROFDATA" ]; then
    echo "Error: llvm-profdata not found at $LLVM_PROFDATA" >&2
    exit 1
fi
if [ ! -x "$TEST_BIN" ]; then
    echo "Error: test binary not found at $TEST_BIN" >&2
    exit 1
fi

mkdir -p "$COVERAGE_DIR"

echo "Running tests with coverage instrumentation..."
LLVM_PROFILE_FILE="$COVERAGE_DIR/statusbar_test.profraw" "$TEST_BIN" -A

"$LLVM_PROFDATA" merge -sparse "$COVERAGE_DIR"/*.profraw -o "$COVERAGE_DIR/combined.profdata"

echo "Coverage data merged into $COVERAGE_DIR/combined.profdata"
