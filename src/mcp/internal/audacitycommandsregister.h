/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef AU_MCP_AUDACITYCOMMANDSREGISTER_H
#define AU_MCP_AUDACITYCOMMANDSREGISTER_H

#include "framework/rcommand/imodulecommandsregister.h"

namespace au::mcp {
class AudacityCommandsRegister : public muse::rcommand::IModuleCommandsRegister
{
public:
    AudacityCommandsRegister() = default;

    std::string moduleName() const override;

    const std::vector<muse::rcommand::Command>& commandList() const override;
    const std::vector<muse::rcommand::CommandInfo>& commandInfoList() const override;
};
}

#endif // AU_MCP_AUDACITYCOMMANDSREGISTER_H
