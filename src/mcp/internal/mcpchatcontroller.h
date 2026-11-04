/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <QPointer>
#include <QString>

#include "modularity/ioc.h"
#include "actions/actionable.h"
#include "actions/iactionsdispatcher.h"
#include "global/async/asyncable.h"
#include "ui/iuiconfiguration.h"

class FMcpServer;
class QWidget;

namespace au::mcp {
//! The chat window of libforge-mcp (FAgentChatWidget) inside Audacity.
//!
//! The chat talks to an OpenAI-compatible or Anthropic endpoint chosen in its own
//! connection dialog and calls Audacity's commands in-process through the MCP
//! server (FMcpServer::internalToolsCall), the same tools an external client sees.
//!
//! The window is a plain Qt Widgets window that lives as long as the context:
//! closing it only hides it, so a running conversation (and a tool loop in
//! flight) survives. muse's widget dialogs are created anew on every open, which
//! would cut a conversation off.
class McpChatController : public muse::actions::Actionable, public muse::async::Asyncable, public muse::Contextable
{
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };
    muse::GlobalInject<muse::ui::IUiConfiguration> uiConfiguration;

public:
    explicit McpChatController(const muse::modularity::ContextPtr& ctx);
    ~McpChatController();

    //! \a server runs on its own thread; \a instructions become the system prompt
    //! of the built-in assistant profile.
    void init(FMcpServer* server, const QString& instructions);
    void deinit();

private:
    void openChat();
    //! Colours of the current Audacity theme for the widgets of the window.
    void applyTheme();

    FMcpServer* m_server = nullptr;
    QPointer<QWidget> m_window;
};
}
