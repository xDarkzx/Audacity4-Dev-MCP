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
#ifndef AU_MCP_MCPMODULE_H
#define AU_MCP_MCPMODULE_H

#include <memory>

#include "modularity/imodulesetup.h"

class FMcpServer;
class QThread;

namespace au::mcp {
class AudacityCommandsController;
class AudacityMcpHost;

class McpModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override;

    muse::modularity::IContextSetup* newContext(const muse::modularity::ContextPtr& ctx) const override;
};

class McpModuleContext : public muse::modularity::IContextSetup
{
public:
    McpModuleContext(const muse::modularity::ContextPtr& ctx);
    ~McpModuleContext() override;

    void registerExports() override;
    void onInit(const muse::IApplication::RunMode& mode) override;
    void onDeinit() override;

private:
    void startMcpServer();
    void stopMcpServer();

    std::shared_ptr<AudacityCommandsController> m_commandsController;
    bool m_initialized = false;

    //! libforge-mcp: HTTP/SSE MCP server on a worker thread, tools from the host.
    std::unique_ptr<AudacityMcpHost> m_mcpHost;
    FMcpServer* m_mcpServer = nullptr;
    QThread* m_mcpThread = nullptr;
};
}

#endif // AU_MCP_MCPMODULE_H
