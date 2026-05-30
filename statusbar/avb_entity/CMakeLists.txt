# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

statusbar_add_module(
  NAME
  avb_entity
  SOURCES
  avb_entity_stereo_io.cpp
  avb_entity_am824_io.cpp
  DEPS
  statusbar-atdecc
  statusbar-avtp
  statusbar::buffer
  statusbar::dsp
  statusbar-gptp
  statusbar::ieee
  statusbar-nanoavb
  statusbar::net
  statusbar-ptpclient
  statusbar::realtime
  statusbar::sm
  statusbar::status
  statusbar::tsn
  TESTS
  avb_entity_stereo_io_test.cpp
  avb_entity_am824_io_test.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(avb_entity_stereo_io_tool avb_entity_stereo_io_tool.cpp)
  target_link_libraries(avb_entity_stereo_io_tool PRIVATE statusbar)
  set_target_properties(avb_entity_stereo_io_tool
                        PROPERTIES OUTPUT_NAME statusbar-avb-stereo-io)
  set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS
                                      avb_entity_stereo_io_tool)

  add_executable(avb_entity_am824_io_tool avb_entity_am824_io_tool.cpp)
  target_link_libraries(avb_entity_am824_io_tool PRIVATE statusbar)
  set_target_properties(avb_entity_am824_io_tool
                        PROPERTIES OUTPUT_NAME statusbar-avb-am824-io)
  set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS
                                      avb_entity_am824_io_tool)
endif()
