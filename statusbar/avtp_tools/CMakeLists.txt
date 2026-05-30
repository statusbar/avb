# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

statusbar_add_module(
  NAME
  avtp_tools
  SOURCES
  avtp_audio_stream_decoder.cpp
  DEPS
  statusbar-avtp
  statusbar::buffer
  statusbar::tsn
  TESTS
  avtp_tools_test.cpp)

add_executable(avtp_to_wav_tool avtp_to_wav_tool.cpp)
target_link_libraries(avtp_to_wav_tool PRIVATE statusbar)
set_target_properties(avtp_to_wav_tool PROPERTIES OUTPUT_NAME
                                                  statusbar-avtp-to-wav)
set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS avtp_to_wav_tool)
