# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
#
# Sanitizer options for Clang: ASAN, UBSAN, TSAN.
#
# Use a separate build directory for each sanitizer. The instrumentation changes
# ABI and runtime expectations, so re-running ctest in an ASan build catches a
# different class of bug from a UBSan or TSan build.

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Sanitizer flags must propagate to every link kind — a SHARED library built
# without the sanitizer flag against ASan-instrumented code would crash at load
# time.
if(ENABLE_ASAN)
  message(STATUS "AddressSanitizer enabled")
  string(APPEND CMAKE_CXX_FLAGS
         " -fsanitize=address -fno-omit-frame-pointer -g")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=address")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fsanitize=address")
  string(APPEND CMAKE_MODULE_LINKER_FLAGS " -fsanitize=address")
endif()

if(ENABLE_UBSAN)
  message(STATUS "UndefinedBehaviorSanitizer enabled")
  string(APPEND CMAKE_CXX_FLAGS
         " -fsanitize=undefined -fno-sanitize-recover=undefined -g")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=undefined")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fsanitize=undefined")
  string(APPEND CMAKE_MODULE_LINKER_FLAGS " -fsanitize=undefined")
endif()

if(ENABLE_TSAN)
  message(STATUS "ThreadSanitizer enabled")
  string(APPEND CMAKE_CXX_FLAGS " -fsanitize=thread -fno-omit-frame-pointer -g")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=thread")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fsanitize=thread")
  string(APPEND CMAKE_MODULE_LINKER_FLAGS " -fsanitize=thread")
endif()

set(_enabled_count 0)
if(ENABLE_ASAN)
  math(EXPR _enabled_count "${_enabled_count} + 1")
endif()
if(ENABLE_UBSAN)
  math(EXPR _enabled_count "${_enabled_count} + 1")
endif()
if(ENABLE_TSAN)
  math(EXPR _enabled_count "${_enabled_count} + 1")
endif()
if(_enabled_count GREATER 1)
  message(
    FATAL_ERROR
      "ASAN, UBSAN, and TSAN are mutually exclusive. Use separate build directories."
  )
endif()
