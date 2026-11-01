/*
 * Alveus — sorting rules
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rules.h"

#include <QFile>

#include <sys/xattr.h>

namespace Alveus
{

QStringList tokenize(const QString &text)
{
    QStringList out;
    QString cur;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (c == QLatin1Char(',') || c == QLatin1Char(';') || c.isSpace())) {
            if (!cur.isEmpty()) {
                out << cur;
                cur.clear();
            }
            continue;
        }
        cur += c;
    }
    if (!cur.isEmpty()) {
        out << cur;
    }
    return out;
}

qint64 parseSize(const QString &text, bool *ok)
{
    QString t = text.trimmed().toUpper();
    if (t.endsWith(QLatin1Char('B'))) {
        t.chop(1);
    }
    qint64 mult = 1;
    if (t.endsWith(QLatin1Char('K'))) {
        mult = 1024;
    } else if (t.endsWith(QLatin1Char('M'))) {
        mult = 1024 * 1024;
    } else if (t.endsWith(QLatin1Char('G'))) {
        mult = 1024LL * 1024 * 1024;
    }
    if (mult != 1) {
        t.chop(1);
    }
    const double v = t.replace(QLatin1Char(','), QLatin1Char('.')).toDouble(ok);
    return (*ok && v >= 0) ? qint64(v * mult) : -1;
}

qint64 parseAge(const QString &text, bool *ok)
{
    QString t = text.trimmed().toLower();
    qint64 mult = 86400; // days by default
    if (t.endsWith(QLatin1Char('h'))) {
        mult = 3600;
        t.chop(1);
    } else if (t.endsWith(QLatin1Char('d'))) {
        t.chop(1);
    } else if (t.endsWith(QLatin1Char('w'))) {
        mult = 7 * 86400;
        t.chop(1);
    }
    const double v = t.replace(QLatin1Char(','), QLatin1Char('.')).toDouble(ok);
    return (*ok && v >= 0) ? qint64(v * mult) : -1;
}

static QRegularExpression patternFor(const QString &value)
{
    QString pattern = value;
    if (!pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?'))) {
        pattern = QLatin1Char('*') + pattern + QLatin1Char('*');
    }
    // file names have no '/', so the default (anchored, path-aware) conversion is fine
    return QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
}

Rules::Rules(const QString &text)
{
    for (const QString &raw : tokenize(text)) {
        const int colon = raw.indexOf(QLatin1Char(':'));
        const QString key = colon > 0 ? raw.left(colon).toLower() : QString();
        const QString value = colon > 0 ? raw.mid(colon + 1) : raw;
        bool ok = true;

        if (key == QLatin1String("name")) {
            if (!value.isEmpty()) {
                m_names << patternFor(value);
            }
        } else if (key == QLatin1String("from")) {
            if (!value.isEmpty()) {
                m_origins << value.toLower();
            }
        } else if (key == QLatin1String("newer")) {
            m_newerSecs = parseAge(value, &ok);
        } else if (key == QLatin1String("older")) {
            m_olderSecs = parseAge(value, &ok);
        } else if (key == QLatin1String("larger")) {
            m_larger = parseSize(value, &ok);
        } else if (key == QLatin1String("smaller")) {
            m_smaller = parseSize(value, &ok);
        } else if (!key.isEmpty()) {
            ok = false;
        } else {
            QString ext = raw.toLower();
            while (ext.startsWith(QLatin1Char('*')) || ext.startsWith(QLatin1Char('.'))) {
                ext.remove(0, 1);
            }
            if (ext == QLatin1String("folder")) {
                m_folders = true;
            } else if (!ext.isEmpty() && !ext.contains(QLatin1Char('/'))) {
                if (!m_extensions.contains(ext)) {
                    m_extensions << ext;
                }
            }
        }
        if (!ok) {
            m_errors << raw;
        }
    }
}

QString Rules::normalize(const QString &text)
{
    QStringList out;
    for (QString t : tokenize(text)) {
        if (t.contains(QLatin1Char(' '))) {
            const int colon = t.indexOf(QLatin1Char(':'));
            t = colon > 0 ? t.left(colon + 1) + QLatin1Char('"') + t.mid(colon + 1) + QLatin1Char('"')
                          : QLatin1Char('"') + t + QLatin1Char('"');
        }
        if (!out.contains(t)) {
            out << t;
        }
    }
    return out.join(QStringLiteral(", "));
}

bool Rules::isEmpty() const
{
    return m_extensions.isEmpty() && !m_folders && m_names.isEmpty() && m_origins.isEmpty()
        && m_newerSecs < 0 && m_olderSecs < 0 && m_larger < 0 && m_smaller < 0;
}

bool Rules::hasTimeCondition() const
{
    return m_newerSecs >= 0 || m_olderSecs >= 0;
}

bool Rules::matches(const FileFacts &f, const QDateTime &now) const
{
    if (isEmpty()) {
        return false;
    }

    // --- conditions: all must hold
    if (m_newerSecs >= 0 || m_olderSecs >= 0) {
        if (!f.modified.isValid()) {
            return false;
        }
        const qint64 age = f.modified.secsTo(now);
        if (m_newerSecs >= 0 && age > m_newerSecs) {
            return false;
        }
        if (m_olderSecs >= 0 && age < m_olderSecs) {
            return false;
        }
    }
    if (m_larger >= 0 && (f.isDir || f.size < m_larger)) {
        return false;
    }
    if (m_smaller >= 0 && (f.isDir || f.size > m_smaller)) {
        return false;
    }

    // --- selectors: any one is enough
    const bool hasSelectors = !m_extensions.isEmpty() || m_folders || !m_names.isEmpty() || !m_origins.isEmpty();
    if (!hasSelectors) {
        return true;
    }
    if (f.isDir && m_folders) {
        return true;
    }
    if (!f.isDir) {
        const QString lower = f.name.toLower();
        for (const QString &ext : m_extensions) {
            if (lower.size() > ext.size() + 1 && lower.endsWith(QLatin1Char('.') + ext)) {
                return true;
            }
        }
    }
    for (const QRegularExpression &re : m_names) {
        if (re.match(f.name).hasMatch()) {
            return true;
        }
    }
    if (!f.origin.isEmpty()) {
        const QString origin = f.origin.toLower();
        for (const QString &o : m_origins) {
            if (origin.contains(o)) {
                return true;
            }
        }
    }
    return false;
}

QString readOrigin(const QString &path)
{
    const QByteArray p = QFile::encodeName(path);
    for (const char *attr : {"user.xdg.origin.url", "user.xdg.referrer.url"}) {
        char buf[2048];
        const ssize_t n = getxattr(p.constData(), attr, buf, sizeof(buf) - 1);
        if (n > 0) {
            return QString::fromUtf8(buf, int(n));
        }
    }
    return QString();
}

} // namespace Alveus
