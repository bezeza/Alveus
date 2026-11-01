/*
 * Alveus — KRunner search (Alt+Space): find fences and desktop files.
 * Implements the org.kde.krunner1 D-Bus interface.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QList>
#include <QObject>
#include <QVariantMap>

class AlveusCore;

struct RemoteMatch {
    QString id;
    QString text;
    QString iconName;
    int categoryRelevance = 0;
    double relevance = 0;
    QVariantMap properties;
};
using RemoteMatches = QList<RemoteMatch>;

struct RemoteAction {
    QString id;
    QString text;
    QString iconName;
};
using RemoteActions = QList<RemoteAction>;

QDBusArgument &operator<<(QDBusArgument &arg, const RemoteMatch &m);
const QDBusArgument &operator>>(const QDBusArgument &arg, RemoteMatch &m);
QDBusArgument &operator<<(QDBusArgument &arg, const RemoteAction &a);
const QDBusArgument &operator>>(const QDBusArgument &arg, RemoteAction &a);

Q_DECLARE_METATYPE(RemoteMatch)
Q_DECLARE_METATYPE(RemoteMatches)
Q_DECLARE_METATYPE(RemoteAction)
Q_DECLARE_METATYPE(RemoteActions)

class RunnerService : public QObject
{
    Q_OBJECT
public:
    static constexpr const char *ServiceName = "ua.bezeza.alveus";
    static constexpr const char *ObjectPath = "/runner";

    explicit RunnerService(AlveusCore *core);
    bool registerOnBus();

    RemoteMatches match(const QString &query) const;
    void run(const QString &id, const QString &action);

private:
    AlveusCore *m_core;
};

class RunnerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")
public:
    explicit RunnerAdaptor(RunnerService *service);

public Q_SLOTS:
    RemoteActions Actions();
    RemoteMatches Match(const QString &query);
    void Run(const QString &matchId, const QString &actionId);
    void Teardown();
    void SetActivationToken(const QString &token);

private:
    RunnerService *m_service;
};
