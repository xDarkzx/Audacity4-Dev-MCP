# SPDX-License-Identifier: GPL-3.0-only
# Audacity-CLA-applies
#
# Audacity
# A Digital Audio Editor
#
# Copyright (C) 2026 Audacity BVBA and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

declare_module(mcp)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mcpmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mcpmodule.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandsregister.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandsregister.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandscontroller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandscontroller.h
    )

setup_module()

if (AU_BUILD_MCP_TESTS)
    add_subdirectory(tests)
endif()
