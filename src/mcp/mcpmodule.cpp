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
#include "mcpmodule.h"

#include "modularity/ioc.h"
#include "framework/rcommand/icommandsregister.h"

#include "internal/audacitycommandsregister.h"
#include "internal/audacitycommandscontroller.h"

using namespace au::mcp;
using namespace muse::modularity;

static const std::string mname("mcp");

std::string McpModule::moduleName() const
{
    return mname;
}

IContextSetup* McpModule::newContext(const muse::modularity::ContextPtr& ctx) const
{
    return new McpModuleContext(ctx);
}

void McpModuleContext::registerExports()
{
    m_commandsController = std::make_shared<AudacityCommandsController>(iocContext());
}

void McpModuleContext::onInit(const muse::IApplication::RunMode& mode)
{
    if (mode != muse::IApplication::RunMode::GuiApp) {
        return;
    }

    //! NOTE onInit() has been observed to run a second time within the same process
    //! lifetime (live-reproduced via a Tools-menu plugin/extension reload action -
    //! log evidence: "CommandsRegister::reg | ASSERT FAILED: m_modules.find(moduleName)
    //! == m_modules.end()" followed immediately by "ActionsDispatcher::doDispatch | More
    //! than one client can handle the action" and a crash a few seconds later).
    //! ICommandsRegister::reg() itself safely no-ops on the second call (it's guarded by
    //! m_modules), but m_commandsController->init() was being called unconditionally
    //! below regardless of whether reg() actually succeeded - so all 42 commands were
    //! being registered a second time with no guard at all, producing duplicate action
    //! handlers. Make the whole method idempotent instead of relying on the sub-call's
    //! partial guard.
    if (m_initialized) {
        return;
    }

    auto commandsRegister = globalIoc()->resolve<muse::rcommand::ICommandsRegister>(mname);
    if (commandsRegister) {
        commandsRegister->reg(std::make_shared<AudacityCommandsRegister>());
    }

    m_commandsController->init();
    m_initialized = true;
}

void McpModuleContext::onDeinit()
{
    auto commandsRegister = globalIoc()->resolve<muse::rcommand::ICommandsRegister>(mname);
    if (commandsRegister) {
        commandsRegister->unreg(commandsRegister->moduleRegister(mname));
    }
}
