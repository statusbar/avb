# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT
#
# Helper function to create a per-module static (or INTERFACE) library.
#
# Usage: statusbar_add_module( NAME            <module-name>            # e.g.
# "buffer" → target statusbar-buffer [SOURCES        <file>...]               #
# .cpp files; omit for INTERFACE [DEPS           <target>...]             #
# public statusbar-* deps [PRIVATE_DEPS   <target>...]             #
# private-only deps [INTERFACE]                              # header-only
# module [TESTS          <file>...])              # test .cpp files
#
# Always registers the created target into the global STATUSBAR_INSTALL_TARGETS
# property and sets its EXPORT_NAME to the module-name (so the exported alias
# becomes statusbar::<module-name>).

function(statusbar_add_module)
  cmake_parse_arguments(ARG "INTERFACE" "NAME"
                        "SOURCES;DEPS;PRIVATE_DEPS;TESTS" ${ARGN})

  if(NOT ARG_NAME)
    message(FATAL_ERROR "statusbar_add_module: NAME is required")
  endif()
  if(ARG_UNPARSED_ARGUMENTS)
    message(
      FATAL_ERROR
        "statusbar_add_module(${ARG_NAME}): unknown keyword(s): "
        "${ARG_UNPARSED_ARGUMENTS}. "
        "Valid keywords are NAME, SOURCES, DEPS, PRIVATE_DEPS, INTERFACE, TESTS."
    )
  endif()

  set(_target "statusbar-${ARG_NAME}")

  if(ARG_INTERFACE)
    add_library(${_target} INTERFACE)
    target_compile_features(${_target} INTERFACE cxx_std_23)
    set_target_properties(${_target} PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(
      ${_target} INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    if(ARG_DEPS)
      target_link_libraries(${_target} INTERFACE ${ARG_DEPS})
    endif()
  else()
    add_library(${_target} STATIC ${ARG_SOURCES})
    target_compile_features(${_target} PUBLIC cxx_std_23)
    set_target_properties(${_target} PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(
      ${_target} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
                        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    if(ARG_DEPS)
      target_link_libraries(${_target} PUBLIC ${ARG_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS)
      target_link_libraries(${_target} PRIVATE ${ARG_PRIVATE_DEPS})
    endif()
  endif()

  # Register the target for install + set the export-side alias.
  set_target_properties(${_target} PROPERTIES EXPORT_NAME ${ARG_NAME})
  set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS ${_target})

  # Local-build alias for namespaced usage
  add_library(statusbar::${ARG_NAME} ALIAS ${_target})

  # Register test files
  if(ARG_TESTS)
    set(_abs_tests "")
    foreach(_t ${ARG_TESTS})
      if(IS_ABSOLUTE "${_t}")
        list(APPEND _abs_tests "${_t}")
      else()
        list(APPEND _abs_tests "${CMAKE_CURRENT_LIST_DIR}/${_t}")
      endif()
    endforeach()
    set_property(GLOBAL APPEND PROPERTY STATUSBAR_TEST_FILES ${_abs_tests})
  endif()
endfunction()
