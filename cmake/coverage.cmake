# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
#
# Code coverage instrumentation for LLVM profiling.
#
# `-DENABLE_COVERAGE=ON` adds `-fprofile-instr-generate -fcoverage-mapping` to
# every compiled and linked target. Use a separate build directory; the
# instrumentation pulls in libclang_rt and produces .profraw files on test runs.
#
# After the build defines its `statusbar_test` target, it should call
# `statusbar_register_coverage_targets(statusbar_test)` to expose three custom
# make targets: `coverage-collect`, `coverage-report`, `coverage-html`. The
# targets are only registered when ENABLE_COVERAGE is on AND when llvm-profdata
# / llvm-cov are discoverable; otherwise they're skipped.

option(ENABLE_COVERAGE "Enable code coverage" OFF)

if(ENABLE_COVERAGE)
  message(STATUS "Code coverage enabled")
  string(APPEND CMAKE_CXX_FLAGS " -fprofile-instr-generate -fcoverage-mapping")
  string(APPEND CMAKE_EXE_LINKER_FLAGS
         " -fprofile-instr-generate -fcoverage-mapping")
endif()

set(STATUSBAR_COVERAGE_IGNORE
    "test\\.cpp|test\\.hpp|_test\\.cpp"
    CACHE STRING "Filename regex for files to exclude from coverage reports")

function(statusbar_register_coverage_targets test_target)
  if(NOT ENABLE_COVERAGE)
    return()
  endif()
  if(NOT TARGET ${test_target})
    message(
      WARNING
        "statusbar_register_coverage_targets: target '${test_target}' not found"
    )
    return()
  endif()
  find_program(
    LLVM_PROFDATA
    NAMES llvm-profdata
    DOC "llvm-profdata")
  find_program(
    LLVM_COV
    NAMES llvm-cov
    DOC "llvm-cov")
  if(NOT LLVM_PROFDATA OR NOT LLVM_COV)
    message(
      WARNING "statusbar_register_coverage_targets: llvm-profdata or llvm-cov "
              "not found in PATH; coverage targets will not be created.")
    return()
  endif()

  if(TARGET coverage-collect)
    # An outer (aggregate) build already registered these.
    return()
  endif()

  set(_cov_dir "${CMAKE_BINARY_DIR}/coverage")
  set(_scripts_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")

  add_custom_target(
    coverage-collect
    COMMAND ${_scripts_dir}/coverage-collect.sh ${_cov_dir} ${LLVM_PROFDATA}
            $<TARGET_FILE:${test_target}>
    DEPENDS ${test_target}
    COMMENT "Running ${test_target} with LLVM coverage instrumentation"
    VERBATIM USES_TERMINAL)

  add_custom_target(
    coverage-report
    COMMAND
      ${_scripts_dir}/coverage-report.sh ${LLVM_COV}
      ${_cov_dir}/combined.profdata ${STATUSBAR_COVERAGE_IGNORE}
      $<TARGET_FILE:${test_target}>
    DEPENDS coverage-collect
    COMMENT "Generating LLVM text coverage report"
    VERBATIM USES_TERMINAL)

  add_custom_target(
    coverage-html
    COMMAND
      ${_scripts_dir}/coverage-html.sh ${LLVM_COV} ${_cov_dir}/combined.profdata
      ${_cov_dir}/html ${STATUSBAR_COVERAGE_IGNORE}
      $<TARGET_FILE:${test_target}>
    DEPENDS coverage-collect
    COMMENT "Generating LLVM HTML coverage report"
    VERBATIM USES_TERMINAL)
endfunction()
