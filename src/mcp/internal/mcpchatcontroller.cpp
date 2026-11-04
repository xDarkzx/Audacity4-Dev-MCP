/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Audacity: A Digital Audio Editor
 */
#include "mcpchatcontroller.h"

#include <QColor>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

#include "FAgentChatWidget.h"
#include "FAgentProfile.h"

#include "translation.h"
#include "log.h"

using namespace au::mcp;

// The one role the assistant has in Audacity. Users can add more profiles in the
// chat's connection settings; this one is the default.
static const QString ASSISTANT_ROLE = QStringLiteral("assistant");

McpChatController::McpChatController(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
{
}

McpChatController::~McpChatController()
{
    deinit();
}

void McpChatController::init(FMcpServer* server, const QString& instructions)
{
    m_server = server;

    FAgentRoleTemplate assistant;
    assistant.role = ASSISTANT_ROLE;
    assistant.defaultName = muse::qtrc("mcp", "Audacity assistant");
    assistant.systemPrompt = instructions;
    FAgentProfile::setRoleTemplates({ assistant });

    dispatcher()->reg(this, "mcp-chat", this, &McpChatController::openChat);

    if (uiConfiguration()) {
        uiConfiguration()->currentThemeChanged().onNotify(this, [this]() { applyTheme(); });
    }
}

void McpChatController::deinit()
{
    if (dispatcher()) {
        dispatcher()->unReg(this);
    }
    delete m_window.data();
    m_server = nullptr;
}

void McpChatController::openChat()
{
    if (!m_server) {
        LOGW() << "MCP server not running (mcp/enabled), the chat has no tools";
    }

    if (!m_window) {
        const QList<FAgentProfile> profiles = FAgentProfile::loadAll();
        FAgentProfile profile = profiles.isEmpty() ? FAgentProfile::defaults().value(0) : profiles.first();

        auto* window = new QWidget(nullptr, Qt::Window);
        window->setObjectName("mcpChatWindow");
        window->setWindowTitle(muse::qtrc("mcp", "AI assistant"));
        // Wide enough for the input's placeholder on one line: the chat sizes
        // its input to the lines of the text, not of the placeholder.
        window->resize(760, 760);

        auto* chat = new FAgentChatWidget(profile, window);
        chat->setMcpServer(m_server);

        auto* layout = new QVBoxLayout(window);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(chat);

        m_window = window;
        applyTheme();
    }

    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void McpChatController::applyTheme()
{
    if (!m_window || !uiConfiguration()) {
        return;
    }
    //! The same roles as muse's WidgetStyle gives the application's widgets.
    const auto& values = uiConfiguration()->currentTheme().values;
    auto color = [&values](muse::ui::ThemeStyleKey key) { return values.value(key).value<QColor>(); };
    const QColor background = color(muse::ui::BACKGROUND_PRIMARY_COLOR);
    const QColor field = color(muse::ui::TEXT_FIELD_COLOR);
    const QColor text = color(muse::ui::FONT_PRIMARY_COLOR);
    const QColor button = color(muse::ui::BUTTON_COLOR);
    const QColor accent = color(muse::ui::ACCENT_COLOR);
    QColor secondaryText = color(muse::ui::FONT_SECONDARY_COLOR);
    if (!background.isValid() || !text.isValid()) {
        return;
    }
    if (!secondaryText.isValid()) {
        secondaryText = text;
    }

    QPalette palette = m_window->palette();
    palette.setColor(QPalette::Window, background);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, field.isValid() ? field : background);
    palette.setColor(QPalette::AlternateBase, color(muse::ui::BACKGROUND_SECONDARY_COLOR));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, secondaryText);
    palette.setColor(QPalette::Button, button.isValid() ? button : background);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::ToolTipBase, color(muse::ui::POPUP_BACKGROUND_COLOR));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, text);
    palette.setColor(QPalette::Link, color(muse::ui::LINK_COLOR));
    m_window->setPalette(palette);
    m_window->setAutoFillBackground(true);
}
