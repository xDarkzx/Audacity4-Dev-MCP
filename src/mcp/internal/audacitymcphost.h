/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <QHash>
#include <QString>

#include "IMcpToolHost.h"

#include "modularity/ioc.h"
#include "global/iapplication.h"
#include "global/async/asyncable.h"
#include "rcommand/icommanddispatcher.h"
#include "rcommand/icommandsregister.h"

namespace au::mcp {
//! The host side of libforge-mcp for Audacity: every command registered with the
//! rcommand register (the "mcp" commands of src/mcp and whatever other modules
//! add) becomes one MCP tool. A call is dispatched through the command dispatcher
//! of the window context the host was made in, exactly like the TCP bridge of
//! muse::rcontrol did, so the command implementations stay unchanged.
class AudacityMcpHost : public IMcpToolHost, public muse::Contextable, public muse::async::Asyncable
{
    muse::GlobalInject<muse::IApplication> application;
    muse::GlobalInject<muse::rcommand::ICommandsRegister> commandsRegister;
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandsDispatcher = { this };

public:
    explicit AudacityMcpHost(const muse::modularity::ContextPtr& iocCtx);
    ~AudacityMcpHost() override = default;

    QJsonObject serverInfo() const override;
    QString instructions() const override;
    bool writesUnlocked() const override;
    QString helpNamespace() const override;
    void registerDomainTools(FMcpToolRegistry& registry) override;

    AuthIdentity authenticateBearer(const QString& token) const override;
    OAuthGrant oauthGrant(const QString& requestedSlug) override;

    QJsonObject confirmRequire(const QString& prompt, ConfirmAction action) override;
    QJsonObject confirmResolve(const QString& id, bool confirm) override;
    QJsonObject confirmPending() override;

    //! The bearer token clients must send; created on first use and kept in the
    //! forge-mcp settings (key mcp/access_token).
    static QString accessToken();

private:
    QJsonObject callCommand(const std::string& path, const QJsonObject& input);

    struct Pending {
        QString prompt;
        ConfirmAction action;
    };
    QHash<QString, Pending> m_pending;
    int m_confirmSeq = 0;
};
}
