# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

statusbar_add_module(
  NAME
  atdecc_tools
  SOURCES
  atdecc_controller_tui.cpp
  atdecc_controller_simple.cpp
  DEPS
  statusbar-atdecc
  statusbar-avtp
  statusbar::buffer
  statusbar::config
  statusbar::ieee
  statusbar-nanoavb
  statusbar::net
  statusbar::status
  statusbar::tui
  TESTS
  atdecc_controller_tui_test.cpp)

add_executable(atdecc_monitor_tool atdecc_monitor_tool.cpp)
target_link_libraries(atdecc_monitor_tool PRIVATE statusbar)
set_target_properties(atdecc_monitor_tool PROPERTIES OUTPUT_NAME
                                                     statusbar-atdecc-monitor)
set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS
                                    atdecc_monitor_tool)

add_executable(atdecc_controller_tool atdecc_controller_tool.cpp)
target_link_libraries(atdecc_controller_tool PRIVATE statusbar)
set_target_properties(atdecc_controller_tool
                      PROPERTIES OUTPUT_NAME statusbar-atdecc-controller)
set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS
                                    atdecc_controller_tool)
