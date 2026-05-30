#! /bin/sh
# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

# Run clang-format / cmake-format / ruff on this package.
#   default:  rewrite files in place.
#   --check:  report files needing reformatting and exit non-zero if any
#             would change. No files modified. Used by the pre-commit
#             hook and for manual or CI verification.

set -e

check_mode=0
case "${1:-}" in
    --check|--dry-run) check_mode=1 ;;
    "") ;;
    *) echo "usage: $0 [--check]" >&2; exit 2 ;;
esac

if [ "$check_mode" -eq 1 ]; then
    clang_args="--dry-run --Werror"
    cmake_args="--check"
    ruff_args="--check"
else
    clang_args="-i"
    cmake_args="-i"
    ruff_args=""
fi

# C/C++ sources
find statusbar \
    \( -name '*.cpp' -or -name '*.cppm' -or -name '*.hpp' -or -name '*.c' -or -name '*.h' \) \
    -print0 | xargs -0 clang-format $clang_args

# CMake files
if command -v cmake-format >/dev/null 2>&1; then
    cmake-format $cmake_args CMakeLists.txt
    find statusbar -name CMakeLists.txt -print0 2>/dev/null | xargs -0 cmake-format $cmake_args
    find cmake -name '*.cmake' -print0 2>/dev/null | xargs -0 cmake-format $cmake_args
fi

# Python (ruff format walks the cwd recursively when no paths are given)
if command -v ruff >/dev/null 2>&1; then
    ruff format $ruff_args .
fi
