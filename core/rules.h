/*
 * Alveus — sorting rules
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A fence's rules are a short text, for example:
 *     pdf, docx, name:*рахунок*, from:telegram, newer:1, larger:100M
 *
 *   pdf  .pdf  *.pdf  tar.gz   file extension
 *   folder                     folders
 *   name:PATTERN               file name; * and ? are wildcards, otherwise "contains"
 *   from:TEXT                  downloaded from a web address containing TEXT
 *   newer:N   (N days, Nh = hours)   changed recently
 *   older:N                    not changed for N days
 *   larger:N / smaller:N       size, N in bytes or with K, M, G
 *
 * A file matches when it fits ANY of the "what" rules (extension, folder, name,
 * from) AND ALL of the "when/size" rules. Only "when/size" rules = any file.
 */

#pragma once

#include <QDateTime>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace Alveus
{

struct FileFacts {
    QString name;
    bool isDir = false;
    qint64 size = 0;
    QDateTime modified;
    QString origin; // where it was downloaded from (xattr), may be empty
};

class Rules
{
public:
    Rules() = default;
    explicit Rules(const QString &text);

    static QString normalize(const QString &text);

    bool isEmpty() const;
    // Rules that depend on time are checked before plain type rules,
    // so "Today" or "Old" fences win over "Documents".
    bool hasTimeCondition() const;
    bool matches(const FileFacts &file, const QDateTime &now = QDateTime::currentDateTime()) const;

    QStringList extensions() const { return m_extensions; }
    QStringList errors() const { return m_errors; }

private:
    QStringList m_extensions;
    bool m_folders = false;
    QList<QRegularExpression> m_names;
    QStringList m_origins;
    qint64 m_newerSecs = -1;
    qint64 m_olderSecs = -1;
    qint64 m_larger = -1;
    qint64 m_smaller = -1;
    QStringList m_errors;
};

QStringList tokenize(const QString &text);
qint64 parseSize(const QString &text, bool *ok);
qint64 parseAge(const QString &text, bool *ok); // seconds

// Reads the "downloaded from" web address that browsers and KDE store on files.
QString readOrigin(const QString &path);

} // namespace Alveus
