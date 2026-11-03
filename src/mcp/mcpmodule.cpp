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
#include "internal/audacitymcphost.h"

#include <QSettings>
#include <QFile>
#include <QThread>

#include "FMcpServer.h"
#include "FMcpSettings.h"

#include "global/iglobalconfiguration.h"
#include "log.h"

using namespace au::mcp;
using namespace muse::modularity;

static const std::string mname("mcp");

// Settings of the MCP server, kept in <user app data>/mcp.ini through the
// forge-mcp settings (gSetting). The defaults live here and nowhere else.
static const QString MCP_ENABLED_KEY = QStringLiteral("mcp/enabled");
static constexpr bool MCP_ENABLED_DEFAULT = true;
static const QString MCP_PORT_KEY = QStringLiteral("mcp/port");
static constexpr int MCP_PORT_DEFAULT = 7536;   // ForgeIEC editors use 7531-7534

std::string McpModule::moduleName() const
{
    return mname;
}

McpModuleContext::McpModuleContext(const muse::modularity::ContextPtr& ctx)
    : muse::modularity::IContextSetup(ctx)
{
}

McpModuleContext::~McpModuleContext() = default;

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
    if (!commandsRegister) {
        return;
    }

    //! The commands register is global, but a module context is not. Opening a second
    //! context in the same process - File > New does exactly this, via
    //! SingleProcessProvider::openNewWindow and BaseApplication::setupNewContext -
    //! constructs a fresh McpModuleContext whose own m_initialized starts out false, so a
    //! per-instance flag cannot prevent re-registration. Registering a second time trips
    //! the assert in CommandsRegister::reg and installs a duplicate set of action
    //! handlers, so ask the register itself rather than trusting a per-context flag.
    if (commandsRegister->moduleRegister(mname)) {
        return;
    }

    commandsRegister->reg(std::make_shared<AudacityCommandsRegister>());
    m_commandsController->init();

    //! Also records that *this* context owns the registration - see onDeinit().
    m_initialized = true;

    //! The server belongs to the context that registered the commands: its
    //! dispatcher is the one their handlers listen on.
    startMcpServer();
}

void McpModuleContext::startMcpServer()
{
    auto globalConfiguration = globalIoc()->resolve<muse::IGlobalConfiguration>(mname);
    if (!globalConfiguration) {
        return;
    }
    const QString iniPath = (globalConfiguration->userAppDataPath() + "/mcp.ini").toQString();
    setMcpSettings(new QSettings(iniPath, QSettings::IniFormat));
    setMcpBrand(QStringLiteral("Audacity"));

    if (!gSetting(MCP_ENABLED_KEY, MCP_ENABLED_DEFAULT).toBool()) {
        LOGI() << "MCP server disabled (" << MCP_ENABLED_KEY.toStdString() << " in " << iniPath.toStdString() << ")";
        return;
    }
    const int port = gSetting(MCP_PORT_KEY, MCP_PORT_DEFAULT).toInt();

    m_mcpHost = std::make_unique<AudacityMcpHost>(iocContext());
    AudacityMcpHost::accessToken(); // created on first start, so clients can be set up
    //! The file holds the bearer token: whoever can read it can drive Audacity.
    //! QSettings creates it with the umask (usually 0644), so restrict it to the
    //! owner, as the TCP bridge did for its token file.
    mcpSettings()->sync();
    if (!QFile::setPermissions(iniPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        LOGW() << "could not restrict the permissions of " << iniPath.toStdString();
    }

    m_mcpThread = new QThread();
    m_mcpThread->setObjectName("mcp-server");
    m_mcpServer = new FMcpServer(m_mcpHost.get()); // no parent: moved to the worker thread
    QObject::connect(m_mcpServer, &FMcpServer::errorOccurred, [](const QString& why) {
        LOGE() << "MCP server: " << why.toStdString();
    });
    m_mcpServer->moveToThread(m_mcpThread);
    m_mcpThread->start();

    FMcpServer* server = m_mcpServer;
    QMetaObject::invokeMethod(server, [server, port, iniPath] {
        if (server->start(static_cast<quint16>(port))) {
            LOGI() << "MCP server listening on port " << port << " (settings: " << iniPath.toStdString() << ")";
        } else {
            LOGE() << "MCP server failed to start on port " << port;
        }
    }, Qt::QueuedConnection);
}

void McpModuleContext::stopMcpServer()
{
    if (!m_mcpThread) {
        return;
    }
    FMcpServer* server = m_mcpServer;
    QMetaObject::invokeMethod(server, [server] { delete server; }, Qt::BlockingQueuedConnection);
    m_mcpServer = nullptr;
    m_mcpThread->quit();
    m_mcpThread->wait();
    delete m_mcpThread;
    m_mcpThread = nullptr;
    m_mcpHost.reset();
}

void McpModuleContext::onDeinit()
{
    //! Only the context that actually registered may unregister. Tearing down a second
    //! context would otherwise remove the commands the surviving context is still
    //! serving, silently leaving the bridge connected but unable to dispatch anything.
    if (!m_initialized) {
        return;
    }

    stopMcpServer();

    auto commandsRegister = globalIoc()->resolve<muse::rcommand::ICommandsRegister>(mname);
    if (commandsRegister) {
        //! moduleRegister() returns null if it was already removed; unreg() asserts on null.
        if (const auto module = commandsRegister->moduleRegister(mname)) {
            commandsRegister->unreg(module);
        }
    }

    m_initialized = false;
}
