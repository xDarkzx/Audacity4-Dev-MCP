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

# Needed for au3-realtime-effects/RealtimeEffectState.h (RealtimeEffectState::GetInstance(),
# used to resolve an EffectInstanceId for the parameter-tweaking commands) - matches how
# effects/effects_base links it for the same header.
set(MODULE_LINK au3wrap)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mcpmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mcpmodule.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandsregister.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandsregister.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandscontroller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandscontroller.h
    )

# audacitycommandscontroller.cpp pulls in au3-realtime-effects/RealtimeEffectState.h
# (via au3wrap, for RealtimeEffectState::GetInstance()), which transitively includes
# legacy AU3 headers declaring their own (non-muse) TranslatableString. Unity-merging
# this file with audacitycommandsregister.cpp - which does `using namespace muse;`
# and calls TranslatableString(...) unqualified throughout - makes that name
# ambiguous across the whole merged translation unit. Keep this file out of the
# Unity blob so its AU3 includes stay isolated.
set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandscontroller.cpp
    PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON
    )

setup_module()

if (AU_BUILD_MCP_TESTS)
    add_subdirectory(tests)
endif()
