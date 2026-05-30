#!/bin/sh
# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
#
# Generate a text coverage report from combined.profdata.
#
# Usage: coverage-report.sh <llvm_cov> <profdata> <cov_ignore_regex> <test_bin>
#
# Arguments:
#   llvm_cov           Path to llvm-cov tool
#   profdata           Path to combined .profdata file
#   cov_ignore_regex   Regex for filenames to ignore (e.g. test files)
#   test_bin           The test binary that produced the profdata

set -e

if [ $# -ne 4 ]; then
    echo "Usage: $0 <llvm_cov> <profdata> <cov_ignore_regex> <test_bin>" >&2
    exit 1
fi

LLVM_COV="$1"
PROFDATA="$2"
COV_IGNORE="$3"
TEST_BIN="$4"

if [ ! -x "$LLVM_COV" ]; then
    echo "Error: llvm-cov not found at $LLVM_COV" >&2
    exit 1
fi

echo ""
echo "=== Line Coverage Report ==="
"$LLVM_COV" report "$TEST_BIN" \
    -instr-profile="$PROFDATA" \
    -ignore-filename-regex="$COV_IGNORE" \
    -show-region-summary=false
echo ""
