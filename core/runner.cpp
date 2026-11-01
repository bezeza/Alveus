/*
 * Alveus — KRunner search
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "runner.h"
#include "alveuscore.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>

QDBusArgument &operator<<(QDBusArgument &arg, const RemoteMatch &m)
{
    arg.beginStructure();
    arg << m.id << m.text << m.iconName << m.categoryRelevance << m.relevance << m.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, RemoteMatch &m)
{
    arg.beginStructure();
    arg >> m.id >> m.text >> m.iconName >> m.categoryRelevance >> m.relevance >> m.properties;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const RemoteAction &a)
{
    arg.beginStructure();
    arg << a.id << a.text << a.iconName;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, RemoteAction &a)
{
    arg.beginStructure();
    arg >> a.id >> a.text >> a.iconName;
    arg.endStructure();
    return arg;
}

RunnerService::RunnerService(AlveusCore *core)
    : QObject(core)
    , m_core(core)
{
    qDBusRegisterMetaType<RemoteMatch>();
    qDBusRegisterMetaType<RemoteMatches>();
    qDBusRegisterMetaType<RemoteAction>();
    qDBusRegisterMetaType<RemoteActions>();
    new RunnerAdaptor(this);
}

bool RunnerService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    if (!bus.registerObject(QString::fromLatin1(ObjectPath), this)) {
        return false;
    }
    return bus.registerService(QString::fromLatin1(ServiceName));
}

RemoteMatches RunnerService::match(const QString &query) const
{
    RemoteMatches out;
    for (const QVariant &v : m_core->runnerMatches(query)) {
        const QVariantMap m = v.toMap();
        RemoteMatch r;
        r.id = m.value(QStringLiteral("id")).toString();
        r.text = m.value(QStringLiteral("text")).toString();
        r.iconName = m.value(QStringLiteral("icon")).toString();
        r.categoryRelevance = m.value(QStringLiteral("category")).toInt();
        r.relevance = m.value(QStringLiteral("relevance")).toDouble();
        QVariantMap props;
        props.insert(QStringLiteral("subtext"), m.value(QStringLiteral("subtext")));
        props.insert(QStringLiteral("category"), QStringLiteral("Alveus"));
        if (m.contains(QStringLiteral("url"))) {
            props.insert(QStringLiteral("urls"), QStringList{m.value(QStringLiteral("url")).toString()});
        }
        props.insert(QStringLiteral("actions"), m.value(QStringLiteral("isFile")).toBool()
                                                    ? QStringList{QStringLiteral("show-in-folder")}
                                                    : QStringList{});
        r.properties = props;
        out << r;
    }
    return out;
}

void RunnerService::run(const QString &id, const QString &action)
{
    if (action == QLatin1String("show-in-folder") && id.startsWith(QLatin1String("file:"))) {
        m_core->showInFolder(id.mid(5));
        return;
    }
    m_core->runnerRun(id);
}

RunnerAdaptor::RunnerAdaptor(RunnerService *service)
    : QDBusAbstractAdaptor(service)
    , m_service(service)
{
}

RemoteActions RunnerAdaptor::Actions()
{
    return {RemoteAction{QStringLiteral("show-in-folder"), QStringLiteral("Show in folder"), QStringLiteral("folder-open")}};
}

RemoteMatches RunnerAdaptor::Match(const QString &query)
{
    return m_service->match(query);
}

void RunnerAdaptor::Run(const QString &matchId, const QString &actionId)
{
    m_service->run(matchId, actionId);
}

void RunnerAdaptor::Teardown()
{
}

void RunnerAdaptor::SetActivationToken(const QString &token)
{
    // Lets the opened application take focus on Wayland
    qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
}
