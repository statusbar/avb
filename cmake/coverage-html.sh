#!/bin/sh
# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
#
# Generate an HTML coverage report from combined.profdata.
#
# Usage: coverage-html.sh <llvm_cov> <profdata> <output_dir> <cov_ignore_regex> <test_bin>
#
# Arguments:
#   llvm_cov           Path to llvm-cov tool
#   profdata           Path to combined .profdata file
#   output_dir         Directory for HTML output (created if absent)
#   cov_ignore_regex   Regex for filenames to ignore (e.g. test files)
#   test_bin           The test binary that produced the profdata

set -e

if [ $# -ne 5 ]; then
    echo "Usage: $0 <llvm_cov> <profdata> <output_dir> <cov_ignore_regex> <test_bin>" >&2
    exit 1
fi

LLVM_COV="$1"
PROFDATA="$2"
OUTPUT_DIR="$3"
COV_IGNORE="$4"
TEST_BIN="$5"

if [ ! -x "$LLVM_COV" ]; then
    echo "Error: llvm-cov not found at $LLVM_COV" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

echo "Generating HTML coverage report..."
"$LLVM_COV" show "$TEST_BIN" \
    -instr-profile="$PROFDATA" \
    -format=html \
    -output-dir="$OUTPUT_DIR" \
    -ignore-filename-regex="$COV_IGNORE" \
    -show-line-counts-or-regions \
    -show-instantiations=false \
    -Xdemangler c++filt

echo "HTML report generated in $OUTPUT_DIR/index.html"
