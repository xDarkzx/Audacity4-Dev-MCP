/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Audacity: A Digital Audio Editor
 */
#include "mcpuiactions.h"

#include "context/shortcutcontext.h"
#include "context/uicontext.h"

using namespace au::mcp;

const muse::ui::UiActionList McpUiActions::m_actions = {
    muse::ui::UiAction("mcp-chat",
                       au::context::UiCtxAny,
                       au::context::CTX_ANY,
                       //: Action title: shown as a menu item or a button label; keep it short
                       muse::TranslatableString("action", "AI &assistant…"),
                       //: Action description: shown as a tooltip; can be a full sentence
                       muse::TranslatableString("action_description",
                                                "Open the chat with an AI assistant that can use Audacity's commands")
                       ),
};

const muse::ui::UiActionList& McpUiActions::actionsList() const
{
    return m_actions;
}

bool McpUiActions::actionEnabled(const muse::ui::UiAction&) const
{
    return true;
}

bool McpUiActions::actionChecked(const muse::ui::UiAction&) const
{
    return false;
}

muse::async::Channel<muse::actions::ActionCodeList> McpUiActions::actionEnabledChanged() const
{
    return m_actionEnabledChanged;
}

muse::async::Channel<muse::actions::ActionCodeList> McpUiActions::actionCheckedChanged() const
{
    return m_actionCheckedChanged;
}
