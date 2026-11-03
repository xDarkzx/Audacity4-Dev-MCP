/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Audacity: A Digital Audio Editor
 */
#include "audacitymcphost.h"

#include <sstream>

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTimer>

#include "FMcpSettings.h"
#include "FMcpToolRegistry.h"

#include "global/stringutils.h"
#include "log.h"

using namespace au::mcp;
using namespace muse;
using namespace muse::rcommand;

namespace {
// Settings of the forge-mcp settings file (see McpModuleContext). Each has its
// default here and nowhere else.
const QString ACCESS_TOKEN_KEY = QStringLiteral("mcp/access_token");
const QString ACCESS_SLUG_KEY = QStringLiteral("mcp/access_slug");
const QString ACCESS_SLUG_DEFAULT = QStringLiteral("audacity");
const QString CALL_TIMEOUT_KEY = QStringLiteral("mcp/call_timeout_ms");
constexpr int CALL_TIMEOUT_DEFAULT_MS = 120000;

//! Tool names are the command path with "/" replaced by "_" (mcp/export-wav ->
//! mcp_export-wav) - the names the TCP bridge used, so existing prompts still fit.
QString toolName(const Command& command)
{
    std::string path = command.path();
    strings::replace(path, "/", "_");
    return QString::fromStdString(path);
}

QString jsonType(DataType type)
{
    switch (type) {
    case DataType::String: return QStringLiteral("string");
    case DataType::Integer: return QStringLiteral("integer");
    case DataType::Float: return QStringLiteral("number");
    case DataType::Boolean: return QStringLiteral("boolean");
    case DataType::Object: return QStringLiteral("object");
    case DataType::Array: return QStringLiteral("array");
    case DataType::Null: return QStringLiteral("null");
    case DataType::Undefined: break;
    }
    return QString();
}

QJsonValue valToJson(const Val& val)
{
    switch (val.type()) {
    case Val::Type::Undefined: return QJsonValue(QJsonValue::Undefined);
    case Val::Type::Bool: return val.toBool();
    case Val::Type::Int:
    case Val::Type::Int64: return static_cast<qint64>(val.toInt64());
    case Val::Type::Double: return val.toDouble();
    default: return QString::fromStdString(val.toString());
    }
}

QJsonObject inputSchemaOf(const CommandInfo& info)
{
    QJsonObject properties;
    for (const auto& [argName, arg] : info.inputSchema.args) {
        QJsonObject property{ { QStringLiteral("description"), arg.description.toQString() } };
        const QString type = jsonType(arg.type);
        if (!type.isEmpty()) {
            property.insert(QStringLiteral("type"), type);
        }
        const QJsonValue min = valToJson(arg.min);
        if (!min.isUndefined()) {
            property.insert(QStringLiteral("minimum"), min);
        }
        const QJsonValue max = valToJson(arg.max);
        if (!max.isUndefined()) {
            property.insert(QStringLiteral("maximum"), max);
        }
        properties.insert(QString::fromStdString(argName), property);
    }
    // Every argument of a command is optional for the register, and handlers
    // ignore what they do not know - so nothing is required and nothing extra is
    // refused here; the command itself answers with its own error.
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), properties },
        { QStringLiteral("additionalProperties"), true },
    };
}

//! Command arguments travel as strings (UriQuery). Numbers are written with
//! enough digits, booleans as true/false, objects and arrays as compact JSON.
std::string toArgString(const QJsonValue& value)
{
    switch (value.type()) {
    case QJsonValue::String: return value.toString().toStdString();
    case QJsonValue::Bool: return value.toBool() ? "true" : "false";
    case QJsonValue::Double: {
        std::ostringstream oss;
        oss.precision(15);
        oss << value.toDouble();
        return oss.str();
    }
    case QJsonValue::Array:
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).toStdString();
    case QJsonValue::Object:
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).toStdString();
    case QJsonValue::Null:
    case QJsonValue::Undefined: break;
    }
    return std::string();
}

QJsonValue resultData(const std::any& data)
{
    const std::string* text = std::any_cast<std::string>(&data);
    if (!text) {
        return QJsonValue(QJsonValue::Null);
    }
    // Many commands answer with a JSON document as text; hand it over as JSON.
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(*text), &err);
    if (err.error == QJsonParseError::NoError) {
        return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    }
    return QString::fromStdString(*text);
}

bool constantTimeEquals(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}
}

AudacityMcpHost::AudacityMcpHost(const modularity::ContextPtr& iocCtx)
    : Contextable(iocCtx)
{
}

QJsonObject AudacityMcpHost::serverInfo() const
{
    return QJsonObject{
        { QStringLiteral("name"), QStringLiteral("audacity4-mcp") },
        { QStringLiteral("version"), application()->version().toString().toQString() },
        { QStringLiteral("writes_enabled"), writesUnlocked() },
    };
}

QString AudacityMcpHost::instructions() const
{
    return QStringLiteral(
        "You are connected to a running Audacity 4. The mcp_* tools act on the open "
        "project: tracks, clips, selection, effects, labels, import and export. Read "
        "the state first (mcp_project-get-info, mcp_track-get-info) and work from "
        "what it returns. Every edit can be undone with mcp_edit-undo.");
}

bool AudacityMcpHost::writesUnlocked() const
{
    // Audacity has no hardware or safety surface: every edit is undoable and the
    // project is only written on an explicit save. Data-loss actions can still go
    // through the confirmation FSM below.
    return true;
}

QString AudacityMcpHost::helpNamespace() const
{
    return QStringLiteral("audacity");
}

void AudacityMcpHost::registerDomainTools(FMcpToolRegistry& registry)
{
    const std::vector<CommandInfo> infos = commandsRegister()->commandInfoList();
    for (const CommandInfo& info : infos) {
        FMcpTool tool;
        tool.name = toolName(info.command);
        tool.description = info.description.qTranslated();
        if (tool.description.isEmpty()) {
            tool.description = info.title.qTranslatedWithoutMnemonic();
        }
        tool.inputSchema = inputSchemaOf(info);
        tool.tags = { QStringLiteral("core") };
        const std::string path = info.command.path();
        tool.handler = [this, path](const QJsonObject& input) {
            return callCommand(path, input);
        };
        registry.registerTool(std::move(tool));
    }
    LOGI() << "registered " << infos.size() << " Audacity commands as MCP tools";
}

QJsonObject AudacityMcpHost::callCommand(const std::string& path, const QJsonObject& input)
{
    CommandQuery query(Command(std::string(COMMAND_SCHEME), path));
    for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
        query.set(it.key().toStdString(), toArgString(it.value()));
    }

    // The handler runs on the UI thread (forge-mcp marshals it there); the
    // dispatcher answers asynchronously, so wait for it in a local event loop.
    bool done = false;
    Response response;
    QEventLoop loop;
    commandsDispatcher()->dispatch(query).onResolve(this, [&](const Response& r) {
        response = r;
        done = true;
        loop.quit();
    });
    if (!done) {
        const int timeoutMs = gSetting(CALL_TIMEOUT_KEY, CALL_TIMEOUT_DEFAULT_MS).toInt();
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (!done) {
        return QJsonObject{
            { QStringLiteral("ok"), false },
            { QStringLiteral("code"), QStringLiteral("FORGE_ERR_TIMEOUT") },
            { QStringLiteral("message"), QStringLiteral("command %1 did not answer in time")
              .arg(QString::fromStdString(path)) },
        };
    }

    const QString text = QString::fromStdString(response.ret.text());
    if (!response.ret.success()) {
        return QJsonObject{
            { QStringLiteral("ok"), false },
            { QStringLiteral("code"), QStringLiteral("FORGE_ERR_COMMAND_FAILED") },
            { QStringLiteral("message"), text.isEmpty() ? QStringLiteral("command failed") : text },
        };
    }

    QJsonObject data;
    if (!text.isEmpty()) {
        data.insert(QStringLiteral("message"), text);
    }
    const QJsonValue result = resultData(response.data);
    if (!result.isNull()) {
        data.insert(QStringLiteral("result"), result);
    }
    return QJsonObject{ { QStringLiteral("ok"), true }, { QStringLiteral("data"), data } };
}

QString AudacityMcpHost::accessToken()
{
    QString token = gSetting(ACCESS_TOKEN_KEY, QString()).toString();
    if (token.isEmpty()) {
        QByteArray bytes(32, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()),
                                              bytes.size() / static_cast<int>(sizeof(quint32)));
        token = QString::fromLatin1(bytes.toHex());
        gSettingSet(ACCESS_TOKEN_KEY, token);
        LOGI() << "created a new MCP access token (settings key " << ACCESS_TOKEN_KEY.toStdString() << ")";
    }
    return token;
}

IMcpToolHost::AuthIdentity AudacityMcpHost::authenticateBearer(const QString& token) const
{
    const QString expected = accessToken();
    if (expected.isEmpty() || token.isEmpty() || !constantTimeEquals(token.toUtf8(), expected.toUtf8())) {
        return {};
    }
    return AuthIdentity{ gSetting(ACCESS_SLUG_KEY, ACCESS_SLUG_DEFAULT).toString(), QStringLiteral("author"), true };
}

IMcpToolHost::OAuthGrant AudacityMcpHost::oauthGrant(const QString& requestedSlug)
{
    Q_UNUSED(requestedSlug);
    const QString slug = gSetting(ACCESS_SLUG_KEY, ACCESS_SLUG_DEFAULT).toString();
    return OAuthGrant{ accessToken(), slug, QStringLiteral("author"), slug, true };
}

QJsonObject AudacityMcpHost::confirmRequire(const QString& prompt, ConfirmAction action)
{
    const QString id = QStringLiteral("c%1").arg(++m_confirmSeq);
    m_pending.insert(id, { prompt, std::move(action) });
    return QJsonObject{
        { QStringLiteral("ok"), false },
        { QStringLiteral("code"), QStringLiteral("FORGE_ERR_CONFIRMATION_REQUIRED") },
        { QStringLiteral("message"), prompt },
        { QStringLiteral("confirmation"), QJsonObject{ { QStringLiteral("id"), id }, { QStringLiteral("prompt"), prompt } } },
    };
}

QJsonObject AudacityMcpHost::confirmResolve(const QString& id, bool confirm)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return QJsonObject{
            { QStringLiteral("ok"), false },
            { QStringLiteral("code"), QStringLiteral("FORGE_ERR_NOT_FOUND") },
            { QStringLiteral("message"), QStringLiteral("no pending confirmation '%1'").arg(id) },
        };
    }
    const Pending pending = it.value();
    m_pending.erase(it);
    if (!confirm) {
        return QJsonObject{ { QStringLiteral("ok"), true },
            { QStringLiteral("data"), QJsonObject{ { QStringLiteral("cancelled"), id } } } };
    }
    return pending.action();
}

QJsonObject AudacityMcpHost::confirmPending()
{
    QJsonArray list;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        list.append(QJsonObject{ { QStringLiteral("id"), it.key() }, { QStringLiteral("prompt"), it.value().prompt } });
    }
    return QJsonObject{ { QStringLiteral("ok"), true },
        { QStringLiteral("data"), QJsonObject{ { QStringLiteral("pending"), list } } } };
}
