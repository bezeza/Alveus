// Unit tests for the Alveus core
#include "../core/alveuscore.h"
#include "../core/drives.h"
#include "../core/integrations.h"
#include "../core/processutils.h"
#include "../core/rules.h"
#include "../core/runner.h"
#include "../core/thumbnails.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeData>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

using namespace Alveus;

static void touch(const QString &path, const QByteArray &data = "x")
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

static void age(const QString &path, int days)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    const QDateTime t = QDateTime::currentDateTime().addDays(-days);
    QVERIFY(f.setFileTime(t, QFileDevice::FileModificationTime));
    QVERIFY(f.setFileTime(t, QFileDevice::FileAccessTime));
}

static QStringList names(const QVariantList &files)
{
    QStringList out;
    for (const QVariant &v : files) {
        out << v.toMap().value(QStringLiteral("name")).toString();
    }
    out.sort();
    return out;
}

static QStringList ids(const QVariantList &fences)
{
    QStringList out;
    for (const QVariant &v : fences) {
        out << v.toMap().value(QStringLiteral("id")).toString();
    }
    out.sort();
    return out;
}


static QString writeExecutable(const QString &directory, const QString &name, const QByteArray &contents)
{
    QDir().mkpath(directory);
    const QString path = directory + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(contents);
    file.close();
    const QFileDevice::Permissions permissions = QFile::permissions(path)
        | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther;
    return QFile::setPermissions(path, permissions) ? path : QString();
}

class ScopedPath
{
public:
    explicit ScopedPath(const QString &prefix)
        : m_original(qgetenv("PATH"))
    {
        qputenv("PATH", prefix.toUtf8() + QLatin1Char(':').toLatin1() + m_original);
    }
    ~ScopedPath()
    {
        if (m_original.isNull()) {
            qunsetenv("PATH");
        } else {
            qputenv("PATH", m_original);
        }
    }

private:
    QByteArray m_original;
};

static bool hasMessageKind(const QSignalSpy &spy, const QString &kind)
{
    for (const QList<QVariant> &arguments : spy) {
        if (!arguments.isEmpty() && arguments.first().toString() == kind) {
            return true;
        }
    }
    return false;
}

static int messageKindCount(const QSignalSpy &spy, const QString &kind)
{
    int count = 0;
    for (const QList<QVariant> &arguments : spy) {
        if (!arguments.isEmpty() && arguments.first().toString() == kind) {
            ++count;
        }
    }
    return count;
}

static int lineCount(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }
    return file.readAll().count('\n');
}

static QtMessageHandler s_testDefaultMessageHandler = nullptr;
static bool s_triggerNestedDiagnostic = false;

static void testPreviousMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (s_triggerNestedDiagnostic) {
        s_triggerNestedDiagnostic = false;
        qWarning("nested diagnostics test warning");
    }
    if (s_testDefaultMessageHandler) {
        s_testDefaultMessageHandler(type, context, message);
    }
}

class CoreTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString desk() const { return m_tmp.path() + QStringLiteral("/Desktop"); }
    QString store() const { return m_tmp.path() + QStringLiteral("/store/layout.json"); }
    QString d(const QString &n) const { return desk() + QLatin1Char('/') + n; }

private Q_SLOTS:
    void initTestCase()
    {
        s_testDefaultMessageHandler = qInstallMessageHandler(testPreviousMessageHandler);
        qputenv("ALVEUS_NO_DBUS", "1");
    }

    void init()
    {
        QDir(m_tmp.path()).removeRecursively();
        QDir().mkpath(desk());
        qputenv("XDG_DATA_HOME", (m_tmp.path() + QStringLiteral("/data")).toUtf8());
        qputenv("XDG_CACHE_HOME", (m_tmp.path() + QStringLiteral("/cache")).toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + QStringLiteral("/config")).toUtf8());
        qputenv("ALVEUS_ARCHIVE_DIR", (m_tmp.path() + QStringLiteral("/Documents")).toUtf8());
        touch(d(QStringLiteral("report.pdf")));
        touch(d(QStringLiteral("Scan.JPG")));
        touch(d(QStringLiteral("song.mp3")));
        touch(d(QStringLiteral("backup.tar.gz")));
        touch(d(QStringLiteral("Рахунок_березень.xlsx")));
        touch(d(QStringLiteral("notes")));
        QDir().mkpath(d(QStringLiteral("Projects")));
    }


    // ---- bounded external helpers -------------------------------------------

    void processTimeoutKillsHungHelperAndPreservesExitErrors()
    {
        QTemporaryDir helpers;
        QVERIFY(helpers.isValid());
        const QString sleeper = writeExecutable(helpers.path(), QStringLiteral("sleep-helper"),
                                                  "#!/bin/sh\nexec sleep 30\n");
        QVERIFY(!sleeper.isEmpty());
        QProcess hung;
        QSignalSpy hungFinished(&hung, &QProcess::finished);
        hung.start(sleeper);
        QVERIFY(hung.waitForStarted(1000));
        Alveus::killAfterTimeout(&hung, 100);
        QTRY_COMPARE_WITH_TIMEOUT(hungFinished.count(), 1, 2000);
        QVERIFY(Alveus::processTimedOut(&hung));
        QCOMPARE(hung.exitStatus(), QProcess::CrashExit);

        const QString failing = writeExecutable(helpers.path(), QStringLiteral("error-helper"),
                                                  "#!/bin/sh\nexit 7\n");
        QVERIFY(!failing.isEmpty());
        QProcess error;
        QSignalSpy errorFinished(&error, &QProcess::finished);
        error.start(failing);
        QTRY_COMPARE_WITH_TIMEOUT(errorFinished.count(), 1, 2000);
        QVERIFY(!Alveus::processTimedOut(&error));
        QCOMPARE(error.exitCode(), 7);
    }

    void integrationHelpersTimeoutFailClosedAndRecover()
    {
        QTemporaryDir helpers;
        QVERIFY(helpers.isValid());
        const QString pwDump = writeExecutable(helpers.path(), QStringLiteral("pw-dump"),
                                                "#!/bin/sh\nexec sleep 30\n");
        const QString kdeConnect = writeExecutable(helpers.path(), QStringLiteral("kdeconnect-cli"),
                                                     "#!/bin/sh\nexec sleep 30\n");
        QVERIFY(!pwDump.isEmpty());
        QVERIFY(!kdeConnect.isEmpty());
        ScopedPath path(helpers.path());

        Integrations integrations(nullptr, 100);
        QSignalSpy phones(&integrations, &Integrations::phonesChanged);
        QSignalSpy shared(&integrations, &Integrations::screenSharedChanged);
        QSignalSpy messages(&integrations, &Integrations::message);

        integrations.refreshPhones();
        integrations.setWatchScreenShare(true);
        QTRY_VERIFY_WITH_TIMEOUT(hasMessageKind(messages, QStringLiteral("kdeconnect-timeout")), 2000);
        QVERIFY(phones.isEmpty()); // timeout preserves the last known list instead of publishing a false empty result
        QTRY_VERIFY_WITH_TIMEOUT(hasMessageKind(messages, QStringLiteral("screen-share-timeout")), 2000);
        QVERIFY(!shared.isEmpty());
        QCOMPARE(shared.last().first().toBool(), true);

        writeExecutable(helpers.path(), QStringLiteral("pw-dump"), "#!/bin/sh\nprintf '[]\\n'\n");
        integrations.setWatchScreenShare(true);
        QTRY_VERIFY_WITH_TIMEOUT(shared.count() >= 2, 2000);
        QCOMPARE(shared.last().first().toBool(), false);

        writeExecutable(helpers.path(), QStringLiteral("kdeconnect-cli"), "#!/bin/sh\nexit 7\n");
        integrations.refreshPhones();
        QTRY_VERIFY_WITH_TIMEOUT(hasMessageKind(messages, QStringLiteral("kdeconnect-failed")), 2000);
        QVERIFY(phones.isEmpty());
        writeExecutable(helpers.path(), QStringLiteral("kdeconnect-cli"),
                        "#!/bin/sh\nprintf 'device-1 Pixel 8\\n'\n");
        integrations.refreshPhones();
        QTRY_VERIFY_WITH_TIMEOUT(phones.count() == 1, 2000);
        QCOMPARE(phones.last().first().toList().first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("device-1"));
        integrations.setWatchScreenShare(false);
    }

    void driveDiscoveryFallsBackForOlderLsblk()
    {
        QTemporaryDir helpers;
        QVERIFY(helpers.isValid());
        const QString log = helpers.path() + QStringLiteral("/lsblk-columns.log");
        const QByteArray script = QStringLiteral(
            "#!/bin/sh\n"
            "printf '%s\\n' \"$4\" >> '%1'\n"
            "case \"$4\" in *TRAN*) echo 'unknown column: TRAN' >&2; exit 1;; esac\n"
            "printf '{\"blockdevices\":[{\"name\":\"sdd\",\"path\":\"/dev/sdd\",\"type\":\"disk\",\"size\":1000}]}\\n'\n")
                                      .arg(log).toUtf8();
        QVERIFY(!writeExecutable(helpers.path(), QStringLiteral("lsblk"), script).isEmpty());
        ScopedPath path(helpers.path());

        DriveWatcher watcher(nullptr, 2000, 1000);
        QSignalSpy messages(&watcher, &DriveWatcher::message);
        watcher.devices(); // starts discovery with the extended column set

        QTRY_COMPARE_WITH_TIMEOUT(lineCount(log), 2, 4000);
        const auto hasSdd = [&watcher]() {
            for (const QVariant &value : watcher.devices()) {
                if (value.toMap().value(QStringLiteral("device")).toString() == QStringLiteral("/dev/sdd")) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasSdd(), 4000);
        QVERIFY(!hasMessageKind(messages, QStringLiteral("drive-list-failed")));
    }

    void driveHelpersTimeoutAndCanRetry()
    {
        QTemporaryDir helpers;
        QVERIFY(helpers.isValid());
        const QString lsblkLog = helpers.path() + QStringLiteral("/lsblk.log");
        const QString udisksLog = helpers.path() + QStringLiteral("/udisks.log");
        const QByteArray logLine = QStringLiteral("echo run >> '%1'\n").arg(lsblkLog).toUtf8();
        const QString lsblk = writeExecutable(helpers.path(), QStringLiteral("lsblk"),
                        "#!/bin/sh\n" + logLine + "exec sleep 30\n");
        const QString udisks = writeExecutable(helpers.path(), QStringLiteral("udisksctl"),
                        QStringLiteral("#!/bin/sh\necho \"$1\" >> '%1'\nexec sleep 30\n").arg(udisksLog).toUtf8());
        QVERIFY(!lsblk.isEmpty());
        QVERIFY(!udisks.isEmpty());
        ScopedPath path(helpers.path());

        DriveWatcher watcher(nullptr, 100, 100);
        QSignalSpy messages(&watcher, &DriveWatcher::message);
        watcher.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(hasMessageKind(messages, QStringLiteral("drive-list-timeout")), 2000);

        const QString quickLsblk = writeExecutable(helpers.path(), QStringLiteral("lsblk"),
                        "#!/bin/sh\n" + logLine + "printf '{\"blockdevices\":[]}\\n'\n");
        QVERIFY(!quickLsblk.isEmpty());
        watcher.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(lineCount(lsblkLog) >= 2, 2000);

        watcher.mount(QStringLiteral("/dev/fake"));
        QTRY_VERIFY_WITH_TIMEOUT(hasMessageKind(messages, QStringLiteral("mount-timeout")), 2000);
        watcher.unmount(QStringLiteral("/dev/fake"));
        QTRY_VERIFY_WITH_TIMEOUT(hasMessageKind(messages, QStringLiteral("unmount-timeout")), 2000);
        QVERIFY(lineCount(udisksLog) >= 2);
    }

    void thumbnailTimeoutsReleaseWorkerSlotsAndBackOff()
    {
        QTemporaryDir helpers;
        QVERIFY(helpers.isValid());
        const QString helperLog = helpers.path() + QStringLiteral("/thumbnail.log");
        const QByteArray script = QStringLiteral("#!/bin/sh\necho run >> '%1'\nexec sleep 30\n").arg(helperLog).toUtf8();
        const QString thumbnailHelper = writeExecutable(helpers.path(), QStringLiteral("ffmpegthumbnailer"), script);
        QVERIFY(!thumbnailHelper.isEmpty());
        ScopedPath path(helpers.path());

        Thumbnailer thumbnails(nullptr, 100);
        QSignalSpy messages(&thumbnails, &Thumbnailer::message);
        QStringList files;
        for (int i = 0; i < 3; ++i) {
            const QString file = d(QStringLiteral("clip-%1.mp4").arg(i));
            touch(file);
            files << file;
            QVERIFY(thumbnails.lookup(file, QStringLiteral("video/mp4")).isEmpty());
        }

        QTRY_VERIFY_WITH_TIMEOUT(hasMessageKind(messages, QStringLiteral("thumbnail-timeout")), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(lineCount(helperLog), 3, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(messageKindCount(messages, QStringLiteral("thumbnail-timeout")), 3, 2000);

        for (const QString &file : files) {
            QVERIFY(thumbnails.lookup(file, QStringLiteral("video/mp4")).isEmpty());
        }
        QTest::qWait(200);
        QCOMPARE(lineCount(helperLog), 3); // no immediate retry storm for the same source stamp
    }

    // ---- rules ---------------------------------------------------------------

    void rulesParsing()
    {
        QCOMPARE(tokenize(QStringLiteral("pdf, .DOCX;*.txt  \"name:мій звіт\"")),
                 (QStringList{QStringLiteral("pdf"), QStringLiteral(".DOCX"), QStringLiteral("*.txt"), QStringLiteral("name:мій звіт")}));
        Rules r(QStringLiteral("pdf, .DOCX, *.txt, tar.gz, folder"));
        QCOMPARE(r.extensions(), (QStringList{QStringLiteral("pdf"), QStringLiteral("docx"), QStringLiteral("txt"), QStringLiteral("tar.gz")}));
        QVERIFY(r.errors().isEmpty());
        QVERIFY(!r.hasTimeCondition());
        QCOMPARE(Rules(QStringLiteral("foo:bar, pdf")).errors(), QStringList{QStringLiteral("foo:bar")});
        QCOMPARE(Rules(QStringLiteral("newer:x")).errors(), QStringList{QStringLiteral("newer:x")});
        QVERIFY(Rules(QString()).isEmpty());
        QCOMPARE(Rules::normalize(QStringLiteral("pdf,pdf  name:мій звіт")), QStringLiteral("pdf, name:мій, звіт"));
        QCOMPARE(Rules::normalize(QStringLiteral("pdf, \"name:мій звіт\"")), QStringLiteral("pdf, name:\"мій звіт\""));

        bool ok = false;
        QCOMPARE(parseSize(QStringLiteral("1.5M"), &ok), qint64(1.5 * 1024 * 1024));
        QVERIFY(ok);
        QCOMPARE(parseSize(QStringLiteral("10kb"), &ok), qint64(10 * 1024));
        QCOMPARE(parseAge(QStringLiteral("12h"), &ok), qint64(12 * 3600));
        QCOMPARE(parseAge(QStringLiteral("2w"), &ok), qint64(14 * 86400));
        QCOMPARE(parseAge(QStringLiteral("3"), &ok), qint64(3 * 86400));
    }

    void rulesMatching()
    {
        const QDateTime now = QDateTime::currentDateTime();
        auto file = [&](const QString &name, int daysOld = 0, qint64 size = 10, bool dir = false, const QString &origin = {}) {
            FileFacts f;
            f.name = name;
            f.modified = now.addDays(-daysOld);
            f.size = size;
            f.isDir = dir;
            f.origin = origin;
            return f;
        };
        QVERIFY(Rules(QStringLiteral("pdf")).matches(file(QStringLiteral("A.PDF")), now));
        QVERIFY(!Rules(QStringLiteral("pdf")).matches(file(QStringLiteral(".pdf")), now)); // no name, only a dot
        QVERIFY(!Rules(QStringLiteral("pdf")).matches(file(QStringLiteral("pdf"), 0, 1, true), now));
        QVERIFY(Rules(QStringLiteral("tar.gz")).matches(file(QStringLiteral("a.tar.gz")), now));
        QVERIFY(Rules(QStringLiteral("folder")).matches(file(QStringLiteral("X"), 0, 0, true), now));
        // names: Cyrillic, case-insensitive, contains or wildcard
        QVERIFY(Rules(QStringLiteral("name:рахунок")).matches(file(QStringLiteral("Рахунок_1.pdf")), now));
        QVERIFY(Rules(QStringLiteral("name:Скан_*")).matches(file(QStringLiteral("скан_002.png")), now));
        QVERIFY(!Rules(QStringLiteral("name:Скан_*")).matches(file(QStringLiteral("мій скан_002.png")), now));
        QVERIFY(Rules(QStringLiteral("\"name:мій звіт\"")).matches(file(QStringLiteral("Мій звіт 2026.docx")), now));
        // origin
        QVERIFY(Rules(QStringLiteral("from:telegram")).matches(file(QStringLiteral("x.bin"), 0, 1, false, QStringLiteral("https://web.Telegram.org/a")), now));
        QVERIFY(!Rules(QStringLiteral("from:telegram")).matches(file(QStringLiteral("x.bin")), now));
        // time and size conditions combine with AND
        QVERIFY(Rules(QStringLiteral("newer:1")).matches(file(QStringLiteral("x")), now));
        QVERIFY(!Rules(QStringLiteral("newer:1")).matches(file(QStringLiteral("x"), 3), now));
        QVERIFY(Rules(QStringLiteral("older:30")).matches(file(QStringLiteral("x"), 45), now));
        QVERIFY(!Rules(QStringLiteral("pdf, older:30")).matches(file(QStringLiteral("x.pdf"), 5), now));
        QVERIFY(Rules(QStringLiteral("pdf, older:30")).matches(file(QStringLiteral("x.pdf"), 40), now));
        QVERIFY(!Rules(QStringLiteral("pdf, older:30")).matches(file(QStringLiteral("x.doc"), 40), now));
        QVERIFY(Rules(QStringLiteral("larger:1M")).matches(file(QStringLiteral("big.iso"), 0, 5 * 1024 * 1024), now));
        QVERIFY(!Rules(QStringLiteral("larger:1M")).matches(file(QStringLiteral("small.iso"), 0, 100), now));
        QVERIFY(Rules(QStringLiteral("newer:1")).hasTimeCondition());
    }

    // ---- sorting -------------------------------------------------------------

    void startsLoose()
    {
        AlveusCore core(desk(), store());
        core.registerDesktop(0, QString());
        QCOMPARE(core.looseFiles(0, QString()).size(), 7);
        QVERIFY(core.showWelcome());
        QCOMPARE(core.version(), QStringLiteral("0.03.1 beta"));
    }

    void rulesSortFiles()
    {
        AlveusCore core(desk(), store());
        const QString docs = core.createFence(0, QString(), 0, 0.1, 0.1, 0.2, 0.3, QStringLiteral("Docs"), core.presetRules(QStringLiteral("documents")));
        const QString pics = core.createFence(0, QString(), 0, 0.4, 0.1, 0.2, 0.3, QStringLiteral("Pics"), core.presetRules(QStringLiteral("images")));
        const QString arch = core.createFence(0, QString(), 0, 0.7, 0.1, 0.2, 0.3, QStringLiteral("Arch"), core.presetRules(QStringLiteral("archives")));
        const QString bills = core.createFence(0, QString(), 1, 0.7, 0.5, 0.2, 0.3, QStringLiteral("Bills"), QStringLiteral("name:рахунок"));
        QCOMPARE(names(core.filesInFence(docs, QString())), (QStringList{QStringLiteral("report.pdf"), QStringLiteral("Рахунок_березень.xlsx")}));
        QCOMPARE(names(core.filesInFence(pics, QString())), QStringList{QStringLiteral("Scan.JPG")});
        QCOMPARE(names(core.filesInFence(arch, QString())), QStringList{QStringLiteral("backup.tar.gz")});
        QVERIFY(core.filesInFence(bills, QString()).isEmpty()); // "Docs" came first
        touch(d(QStringLiteral("new.docx")));
        core.rescanNow();
        QCOMPARE(core.fenceOfFile(QStringLiteral("new.docx"), QString()), docs);
    }

    void timeFencesWin()
    {
        age(d(QStringLiteral("report.pdf")), 90);
        AlveusCore core(desk(), store());
        const QString docs = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Docs"), QStringLiteral("pdf, xlsx"));
        const QString today = core.createFence(0, QString(), 0, 0.3, 0, 0.2, 0.2, QStringLiteral("Today"), core.presetRules(QStringLiteral("today")));
        const QString old = core.createFence(0, QString(), 0, 0.6, 0, 0.2, 0.2, QStringLiteral("Old"), core.presetRules(QStringLiteral("old")));
        QCOMPARE(core.fenceOfFile(QStringLiteral("report.pdf"), QString()), old);
        QCOMPARE(core.fenceOfFile(QStringLiteral("Рахунок_березень.xlsx"), QString()), today);
        QVERIFY(core.fence(today).value(QStringLiteral("timeRule")).toBool());
        Q_UNUSED(docs)
    }

    void downloadedFrom()
    {
        const QByteArray p = QFile::encodeName(d(QStringLiteral("song.mp3")));
        const QByteArray url = "https://web.telegram.org/k/";
        if (setxattr(p.constData(), "user.xdg.origin.url", url.constData(), size_t(url.size()), 0) != 0) {
            QSKIP("this filesystem has no user extended attributes");
        }
        QCOMPARE(readOrigin(d(QStringLiteral("song.mp3"))), QString::fromLatin1(url));
        AlveusCore core(desk(), store());
        const QString tg = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Telegram"), core.presetRules(QStringLiteral("telegram")));
        QCOMPARE(names(core.filesInFence(tg, QString())), QStringList{QStringLiteral("song.mp3")});
    }

    void manualChoicesStick()
    {
        AlveusCore core(desk(), store());
        const QString docs = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Docs"), QStringLiteral("pdf"));
        const QString other = core.createFence(0, QString(), 0, 0.5, 0, 0.2, 0.2, QStringLiteral("Other"), QString());
        core.assignFiles({QStringLiteral("report.pdf")}, other);
        core.updateFence(docs, {{QStringLiteral("rules"), QStringLiteral("pdf, mp3")}});
        QCOMPARE(core.fenceOfFile(QStringLiteral("report.pdf"), QString()), other);
        QCOMPARE(core.fenceOfFile(QStringLiteral("song.mp3"), QString()), docs);
        core.assignFiles({QStringLiteral("song.mp3")}, QString()); // taken out by hand
        QCOMPARE(core.fenceOfFile(QStringLiteral("song.mp3"), QString()), QString());
    }

    void removeAndUndo()
    {
        AlveusCore core(desk(), store());
        const QString docs = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Docs"), QStringLiteral("pdf"));
        core.assignFiles({QStringLiteral("notes")}, docs);
        core.removeFence(docs);
        QCOMPARE(core.fenceCount(), 0);
        QVERIFY(QFile::exists(d(QStringLiteral("report.pdf"))));
        QCOMPARE(core.undoText(), QStringLiteral("remove-fence|Docs"));
        QVERIFY(core.undo());
        QCOMPARE(core.fenceCount(), 1);
        QCOMPARE(names(core.filesInFence(docs, QString())), (QStringList{QStringLiteral("notes"), QStringLiteral("report.pdf")}));
    }

    // ---- tabs, pages, activities, screens -------------------------------------

    void tabs()
    {
        AlveusCore core(desk(), store());
        const QString a = core.createFence(0, QString(), 0, 0.1, 0.1, 0.3, 0.3, QStringLiteral("A"), QStringLiteral("pdf"));
        const QString b = core.createFence(0, QString(), 0, 0.6, 0.6, 0.2, 0.2, QStringLiteral("B"), QStringLiteral("mp3"));
        core.mergeFences(a, b);
        QVariantMap fb = core.fence(b);
        QVERIFY(!fb.value(QStringLiteral("group")).toString().isEmpty());
        QCOMPARE(fb.value(QStringLiteral("x")).toReal(), 0.1);        // took A's frame
        QCOMPARE(fb.value(QStringLiteral("activeTab")).toBool(), true);
        QCOMPARE(core.fence(a).value(QStringLiteral("activeTab")).toBool(), false);
        QCOMPARE(fb.value(QStringLiteral("tabs")).toList().size(), 2);
        // shared frame: moving one moves both
        core.updateFence(a, {{QStringLiteral("x"), 0.2}, {QStringLiteral("collapsed"), true}});
        QCOMPARE(core.fence(b).value(QStringLiteral("x")).toReal(), 0.2);
        QCOMPARE(core.fence(b).value(QStringLiteral("collapsed")).toBool(), true);
        // but not the rules
        QCOMPARE(core.fence(b).value(QStringLiteral("rules")).toString(), QStringLiteral("mp3"));
        core.activateTab(a);
        QCOMPARE(core.fence(a).value(QStringLiteral("activeTab")).toBool(), true);
        QCOMPARE(core.fence(b).value(QStringLiteral("activeTab")).toBool(), false);
        core.detachTab(b);
        QVERIFY(core.fence(a).value(QStringLiteral("group")).toString().isEmpty());
        QVERIFY(core.fence(b).value(QStringLiteral("group")).toString().isEmpty());
        QVERIFY(core.fence(b).value(QStringLiteral("activeTab")).toBool());
    }

    void mergeFencesRefreshActivityFileModelCaches()
    {
        AlveusCore core(desk(), store());

        // A global source fence becomes activity-specific when merged into a
        // work-only target. The home cache must stop placing this file in it.
        const QString workTarget = core.createFence(0, QStringLiteral("work"), 0,
                                                    0, 0, 0.2, 0.2,
                                                    QStringLiteral("Documents"), QStringLiteral("pdf"));
        const QString globalSource = core.createFence(0, QString(), 0,
                                                      0.3, 0, 0.2, 0.2,
                                                      QStringLiteral("Images"), QStringLiteral("jpg"));
        touch(d(QStringLiteral("cache-scope.jpg")), "photo");
        core.rescanNow();
        core.looseFiles(0, QStringLiteral("work"));
        core.looseFiles(0, QStringLiteral("home"));
        QCOMPARE(core.fenceOfFile(QStringLiteral("cache-scope.jpg"), QStringLiteral("work")), globalSource);
        QCOMPARE(core.fenceOfFile(QStringLiteral("cache-scope.jpg"), QStringLiteral("home")), globalSource);

        core.mergeFences(workTarget, globalSource);
        QCOMPARE(core.fence(globalSource).value(QStringLiteral("activity")).toString(), QStringLiteral("work"));
        QCOMPARE(core.fenceOfFile(QStringLiteral("cache-scope.jpg"), QStringLiteral("work")), globalSource);
        QCOMPARE(core.fenceOfFile(QStringLiteral("cache-scope.jpg"), QStringLiteral("home")), QString());
        QVERIFY(names(core.filesInFence(globalSource, QStringLiteral("home"))).isEmpty());
        QVERIFY(names(core.looseFiles(0, QStringLiteral("home"))).contains(QStringLiteral("cache-scope.jpg")));

        // The reverse scope change must also invalidate caches: a work-only
        // source fence becomes global when merged into a global target.
        const QString globalTarget = core.createFence(0, QString(), 0,
                                                      0.5, 0, 0.2, 0.2,
                                                      QStringLiteral("Text"), QStringLiteral("txt"));
        const QString workSource = core.createFence(0, QStringLiteral("work"), 0,
                                                    0.7, 0, 0.2, 0.2,
                                                    QStringLiteral("Screenshots"), QStringLiteral("png"));
        touch(d(QStringLiteral("cache-scope.png")), "screenshot");
        core.rescanNow();
        core.looseFiles(0, QStringLiteral("work"));
        core.looseFiles(0, QStringLiteral("home"));
        QCOMPARE(core.fenceOfFile(QStringLiteral("cache-scope.png"), QStringLiteral("work")), workSource);
        QCOMPARE(core.fenceOfFile(QStringLiteral("cache-scope.png"), QStringLiteral("home")), QString());

        core.mergeFences(globalTarget, workSource);
        QCOMPARE(core.fence(workSource).value(QStringLiteral("activity")).toString(), QString());
        QCOMPARE(core.fenceOfFile(QStringLiteral("cache-scope.png"), QStringLiteral("work")), workSource);
        QCOMPARE(core.fenceOfFile(QStringLiteral("cache-scope.png"), QStringLiteral("home")), workSource);
        QVERIFY(names(core.filesInFence(workSource, QStringLiteral("home"))).contains(QStringLiteral("cache-scope.png")));
    }

    void pagesAndActivities()
    {
        AlveusCore core(desk(), store());
        const QString work = core.createFence(0, QStringLiteral("work"), 0, 0, 0, 0.2, 0.2, QStringLiteral("Work"), QStringLiteral("pdf"));
        const QString all = core.createFence(0, QString(), 1, 0, 0, 0.2, 0.2, QStringLiteral("All"), QStringLiteral("mp3"));
        QCOMPARE(ids(core.fences(0, QStringLiteral("work"), 0)), QStringList{work});
        QCOMPARE(ids(core.fences(0, QStringLiteral("work"), 1)), QStringList{all});
        QVERIFY(core.fences(0, QStringLiteral("home"), 0).isEmpty());
        QCOMPARE(ids(core.fences(0, QStringLiteral("home"), 1)), QStringList{all});
        QCOMPARE(core.pageCount(0, QStringLiteral("home")), 2);
        core.setPageCount(0, QStringLiteral("home"), 4);
        QCOMPARE(core.pageCount(0, QStringLiteral("home")), 4);
        // the PDF is in "Work" there, but loose at home
        QCOMPARE(core.fenceOfFile(QStringLiteral("report.pdf"), QStringLiteral("work")), work);
        QCOMPARE(core.fenceOfFile(QStringLiteral("report.pdf"), QStringLiteral("home")), QString());
        core.registerDesktop(0, QStringLiteral("home"));
        QVERIFY(names(core.looseFiles(0, QStringLiteral("home"))).contains(QStringLiteral("report.pdf")));
    }

    void monitors()
    {
        AlveusCore core(desk(), store());
        const QString left = core.createFence(0, QString(), 0, 0.1, 0.1, 0.2, 0.2, QStringLiteral("Left"), QString());
        const QString right = core.createFence(1, QString(), 0, 0.5, 0.5, 0.2, 0.2, QStringLiteral("Right"), QString());
        core.registerDesktop(0, QString());
        core.registerDesktop(1, QString());
        QCOMPARE(ids(core.fences(0, QString(), 0)), QStringList{left});
        core.unregisterDesktop(1, QString()); // unplugged
        QCOMPARE(core.fences(0, QString(), 0).size(), 2);
        QVariantMap r = core.fence(right);
        QVERIFY(r.value(QStringLiteral("orphan")).toBool());
        // moved while away: remembered separately
        core.updateFence(right, {{QStringLiteral("x"), 0.7}, {QStringLiteral("y"), 0.0}, {QStringLiteral("orphan"), true}});
        QCOMPARE(core.fence(right).value(QStringLiteral("x")).toReal(), 0.7);
        core.registerDesktop(1, QString()); // back again
        QCOMPARE(ids(core.fences(1, QString(), 0)), QStringList{right});
        QCOMPARE(core.fence(right).value(QStringLiteral("x")).toReal(), 0.5);
        // move to another monitor for good
        core.updateFence(right, {{QStringLiteral("screen"), 0}});
        QCOMPARE(core.fences(0, QString(), 0).size(), 2);
        QCOMPARE(core.screens(QString()), (QVariantList{0, 1}));
    }

    void iconPositions()
    {
        AlveusCore core(desk(), store());
        core.registerDesktop(0, QString());
        core.registerDesktop(1, QString());
        core.setFilePositions({QVariantMap{{QStringLiteral("name"), QStringLiteral("notes")}, {QStringLiteral("screen"), 1},
                                           {QStringLiteral("page"), 0}, {QStringLiteral("col"), 3}, {QStringLiteral("row"), 2}}});
        QVERIFY(!names(core.looseFiles(0, QString())).contains(QStringLiteral("notes")));
        QVariantMap m;
        for (const QVariant &v : core.looseFiles(1, QString())) {
            if (v.toMap().value(QStringLiteral("name")) == QStringLiteral("notes")) {
                m = v.toMap();
            }
        }
        QCOMPARE(m.value(QStringLiteral("col")).toInt(), 3);
        QVERIFY(m.value(QStringLiteral("hasPos")).toBool());
        core.unregisterDesktop(1, QString()); // its icons come to the main screen, auto-placed
        bool found = false;
        for (const QVariant &v : core.looseFiles(0, QString())) {
            if (v.toMap().value(QStringLiteral("name")) == QStringLiteral("notes")) {
                found = true;
                QVERIFY(!v.toMap().value(QStringLiteral("hasPos")).toBool());
            }
        }
        QVERIFY(found);
        core.registerDesktop(1, QString());
        core.clearPositions(1, 0);
        QVERIFY(!names(core.looseFiles(1, QString())).contains(QStringLiteral("notes")));
    }

    // ---- persistence, snapshots ---------------------------------------------

    void persistence()
    {
        QString id;
        {
            AlveusCore core(desk(), store());
            id = core.createFence(1, QStringLiteral("act"), 2, 0.25, 0.5, 0.3, 0.2, QStringLiteral("Фото"), QStringLiteral("jpg"));
            core.updateFence(id, {{QStringLiteral("collapsed"), true}, {QStringLiteral("color"), QStringLiteral("#ff8800")},
                                  {QStringLiteral("view"), QStringLiteral("honeycomb")}, {QStringLiteral("opacity"), 0.8}});
            core.assignFiles({QStringLiteral("notes")}, id);
            core.setSetting(QStringLiteral("cleanupDays"), 30);
            core.saveSnapshot(QStringLiteral("Робочий"));
            core.saveNow();
        }
        AlveusCore core(desk(), store());
        const QVariantMap f = core.fence(id);
        QCOMPARE(f.value(QStringLiteral("title")).toString(), QStringLiteral("Фото"));
        QCOMPARE(f.value(QStringLiteral("page")).toInt(), 2);
        QCOMPARE(f.value(QStringLiteral("activity")).toString(), QStringLiteral("act"));
        QCOMPARE(f.value(QStringLiteral("color")).toString(), QStringLiteral("#ff8800"));
        QCOMPARE(f.value(QStringLiteral("view")).toString(), QStringLiteral("honeycomb"));
        QCOMPARE(f.value(QStringLiteral("opacity")).toReal(), 0.8);
        QCOMPARE(f.value(QStringLiteral("collapsed")).toBool(), true);
        QCOMPARE(names(core.filesInFence(id, QStringLiteral("act"))), (QStringList{QStringLiteral("Scan.JPG"), QStringLiteral("notes")}));
        QCOMPARE(core.setting(QStringLiteral("cleanupDays")).toInt(), 30);
        QCOMPARE(core.setting(QStringLiteral("iconSize")).toInt(), 48); // default
        QCOMPARE(core.snapshots().size(), 1);
    }

    void corruptSettingsAreBackedUpBeforeReplacement()
    {
        QDir().mkpath(QFileInfo(store()).absolutePath());
        touch(store(), "{ this is not valid json");

        AlveusCore core(desk(), store());
        const QString id = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Recovered"), QString());
        core.saveNow();

        const QStringList backups = QDir(QFileInfo(store()).absolutePath())
            .entryList({QFileInfo(store()).fileName() + QStringLiteral(".corrupt-*")}, QDir::Files);
        QVERIFY2(!backups.isEmpty(), "The damaged layout must be preserved before a new one is saved");
        QFile backup(QFileInfo(store()).absolutePath() + QLatin1Char('/') + backups.first());
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QVERIFY(backup.readAll().contains("not valid json"));

        QFile repaired(store());
        QVERIFY(repaired.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(repaired.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(doc.object().value(QStringLiteral("version")).toInt(), 2);
        QCOMPARE(doc.object().value(QStringLiteral("fences")).toArray().size(), 1);
        QCOMPARE(core.fence(id).value(QStringLiteral("title")).toString(), QStringLiteral("Recovered"));
    }

    void structurallyInvalidSettingsAreBackedUpBeforeReplacement()
    {
        auto verifyRejectedStore = [this](const QString &path, const QByteArray &original) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            touch(path, original);
            AlveusCore core(desk(), path);
            const QString id = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2,
                                                QStringLiteral("Recovered"), QString());
            core.saveNow();

            const QStringList backups = QDir(QFileInfo(path).absolutePath())
                .entryList({QFileInfo(path).fileName() + QStringLiteral(".corrupt-*")}, QDir::Files);
            QVERIFY2(!backups.isEmpty(), "Structurally invalid settings must be preserved before replacement");
            QFile backup(QFileInfo(path).absolutePath() + QLatin1Char('/') + backups.first());
            QVERIFY(backup.open(QIODevice::ReadOnly));
            QCOMPARE(backup.readAll(), original);

            QFile repaired(path);
            QVERIFY(repaired.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(repaired.readAll(), &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QCOMPARE(doc.object().value(QStringLiteral("fences")).toArray().size(), 1);
            QCOMPARE(core.fence(id).value(QStringLiteral("title")).toString(), QStringLiteral("Recovered"));
        };

        verifyRejectedStore(m_tmp.path() + QStringLiteral("/store/missing-fences.json"),
                            R"({"version":2})");
        verifyRejectedStore(m_tmp.path() + QStringLiteral("/store/invalid-fence.json"),
                            R"({"version":2,"fences":[{}]})");
        verifyRejectedStore(m_tmp.path() + QStringLiteral("/store/invalid-snapshot.json"),
                            R"({"version":2,"fences":[],"files":{},"pages":{},"snapshots":[{"id":"x","name":"bad","state":{"fences":[{}]}}]})");
        verifyRejectedStore(m_tmp.path() + QStringLiteral("/store/invalid-drive-options.json"),
                            R"({"version":2,"fences":[{"id":"d","title":"Disks","portal":"drives:","driveOptions":{"partitionMode":"unknown"}}]})");
        verifyRejectedStore(m_tmp.path() + QStringLiteral("/store/invalid-drive-card-size.json"),
                            R"({"version":2,"fences":[{"id":"d","title":"Disks","portal":"drives:","driveOptions":{"displayMode":"tiny"}}]})");
        verifyRejectedStore(m_tmp.path() + QStringLiteral("/store/invalid-drive-defaults.json"),
                            R"({"version":2,"fences":[],"settings":{"drives":{"groups":{"usb":"yes"}}}})");
        verifyRejectedStore(m_tmp.path() + QStringLiteral("/store/invalid-drive-display-mode.json"),
                            R"({"version":2,"fences":[],"settings":{"drives":{"displayMode":"huge"}}})");
    }

    void newerSettingsAreNeverOverwritten()
    {
        QDir().mkpath(QFileInfo(store()).absolutePath());
        const QByteArray original = R"({"version":999,"fences":[{"id":"future"}],"custom":"keep me"})";
        touch(store(), original);

        AlveusCore core(desk(), store());
        core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Local"), QString());
        core.saveNow();

        QFile file(store());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }

    void fenceAppearanceCanBeUndone()
    {
        AlveusCore core(desk(), store());
        const QString id = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Style"), QString());
        core.updateFence(id, {{QStringLiteral("color"), QStringLiteral("#ff8800")},
                              {QStringLiteral("opacity"), 0.8}, {QStringLiteral("blur"), false}});
        QCOMPARE(core.fence(id).value(QStringLiteral("color")).toString(), QStringLiteral("#ff8800"));
        QVERIFY(core.undo());
        QCOMPARE(core.fence(id).value(QStringLiteral("color")).toString(), QString());
        QCOMPARE(core.fence(id).value(QStringLiteral("opacity")).toReal(), 0.55);
        QCOMPARE(core.fence(id).value(QStringLiteral("blur")).toBool(), true);
    }

    void importPreservesSymlinksAndOriginMetadata()
    {
        AlveusCore core(desk(), store());
        const QString source = m_tmp.path() + QStringLiteral("/source-tree");
        QVERIFY(QDir().mkpath(source));
        const QString target = source + QStringLiteral("/target.txt");
        touch(target, "relative link target");
        const QString link = source + QStringLiteral("/relative-link.txt");
        const QByteArray linkTarget = "target.txt";
        const QByteArray linkName = QFile::encodeName(link);
        QVERIFY(::symlink(linkTarget.constData(), linkName.constData()) == 0);
        QVERIFY(QDir().mkpath(source + QStringLiteral("/subdir")));
        touch(source + QStringLiteral("/subdir/inside.txt"), "inside");
        const QByteArray dirLinkTarget = "subdir";
        const QByteArray dirLinkName = QFile::encodeName(source + QStringLiteral("/directory-link"));
        QVERIFY(::symlink(dirLinkTarget.constData(), dirLinkName.constData()) == 0);
        const QByteArray danglingTarget = "missing-target";
        const QByteArray danglingName = QFile::encodeName(source + QStringLiteral("/dangling-link"));
        QVERIFY(::symlink(danglingTarget.constData(), danglingName.constData()) == 0);

        const QByteArray origin = "https://web.telegram.org/k/";
        const QByteArray sourceName = QFile::encodeName(target);
        const bool hasOrigin = ::setxattr(sourceName.constData(), "user.xdg.origin.url", origin.constData(),
                                          size_t(origin.size()), 0) == 0;

        const QStringList copied = core.importToDesktop({QUrl::fromLocalFile(source)}, true, QString());
        QCOMPARE(copied.size(), 1);
        QVERIFY(core.waitForFileJobs());
        const QString copiedFile = copied.first() + QStringLiteral("/target.txt");
        const QString copiedLink = copied.first() + QStringLiteral("/relative-link.txt");
        const QString copiedDirLink = copied.first() + QStringLiteral("/directory-link");
        const QString copiedDangling = copied.first() + QStringLiteral("/dangling-link");
        QVERIFY(QFileInfo(copiedLink).isSymLink());
        QVERIFY(QFileInfo(copiedDirLink).isSymLink());
        QVERIFY(QFileInfo(copiedDangling).isSymLink());
        auto readLinkTarget = [](const QString &path) {
            char targetBuffer[512] = {};
            const QByteArray encoded = QFile::encodeName(path);
            const ssize_t length = ::readlink(encoded.constData(), targetBuffer, sizeof(targetBuffer) - 1);
            return length > 0 ? QByteArray(targetBuffer, int(length)) : QByteArray();
        };
        QCOMPARE(readLinkTarget(copiedLink), linkTarget);
        QCOMPARE(readLinkTarget(copiedDirLink), dirLinkTarget);
        QCOMPARE(readLinkTarget(copiedDangling), danglingTarget);
        QFile throughLink(copiedLink);
        QVERIFY(throughLink.open(QIODevice::ReadOnly));
        QCOMPARE(throughLink.readAll(), QByteArray("relative link target"));
        if (hasOrigin) {
            QCOMPARE(readOrigin(copiedFile), QString::fromLatin1(origin));
        }
        QVERIFY(QFile::exists(target)); // copy must not change the source
    }

    void fileOperationsRejectSpecialFilesWithoutBlocking()
    {
        const QString sourceDir = m_tmp.path() + QStringLiteral("/special-source");
        QVERIFY(QDir().mkpath(sourceDir));
        const QString fifo = sourceDir + QStringLiteral("/pipe");
        const QByteArray encodedFifo = QFile::encodeName(fifo);
        QVERIFY(::mkfifo(encodedFifo.constData(), 0600) == 0);

        const QString copied = m_tmp.path() + QStringLiteral("/special-copy");
        QCOMPARE(Alveus::FileOps::copyTree(sourceDir, copied), Alveus::FileOps::Result::Failed);
        QVERIFY(!QFileInfo::exists(copied));
        struct stat st {};
        QVERIFY(::lstat(encodedFifo.constData(), &st) == 0);
        QVERIFY(S_ISFIFO(st.st_mode));

        const QString moved = m_tmp.path() + QStringLiteral("/special-move");
        QCOMPARE(Alveus::FileOps::movePath(sourceDir, moved), Alveus::FileOps::Result::Failed);
        QVERIFY(QFileInfo(sourceDir).isDir());
        QVERIFY(!QFileInfo::exists(moved));
    }

    void crossFilesystemMoveReportsSourceLeftWhenRemovalFails()
    {
        if (::geteuid() == 0) {
            QSKIP("permission test needs an unprivileged user");
        }
        QTemporaryDir other(QStringLiteral("/dev/shm/alveus-source-left-XXXXXX"));
        if (!other.isValid() || Alveus::FileOps::sameFilesystem(m_tmp.path(), other.path())) {
            QSKIP("needs /dev/shm on a different filesystem");
        }

        const QString lockedDir = m_tmp.path() + QStringLiteral("/locked-source");
        QVERIFY(QDir().mkpath(lockedDir));
        const QString source = lockedDir + QStringLiteral("/payload.txt");
        touch(source, "keep the original");
        QVERIFY(QFile::setPermissions(lockedDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        const QString destination = other.path() + QStringLiteral("/payload.txt");
        QCOMPARE(Alveus::FileOps::movePath(source, destination), Alveus::FileOps::Result::SourceLeft);
        QVERIFY(QFileInfo::exists(source));
        QFile copied(destination);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("keep the original"));

        QVERIFY(QFile::setPermissions(lockedDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner));
        QVERIFY(QFile::remove(source));
        QVERIFY(QFile::remove(destination));
        QVERIFY(QDir().rmdir(lockedDir));
    }

    // Needs a FAT mount (USB stick, or `fusefat -o rw+ img dir`):
    //   ALVEUS_TEST_FAT_DIR=/run/media/me/STICK coretest copyAndMoveToFat
    void copyAndMoveToFat()
    {
        const QString fat = qEnvironmentVariable("ALVEUS_TEST_FAT_DIR");
        if (fat.isEmpty()) {
            QSKIP("set ALVEUS_TEST_FAT_DIR to a FAT mount");
        }
        const QString dir = fat + QStringLiteral("/alveus-test-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        QVERIFY(QDir().mkpath(dir));
        AlveusCore core(desk(), store());

        auto makeTree = [this](const QString &name) {
            const QString tree = d(name);
            QDir().mkpath(tree + QStringLiteral("/sub"));
            touch(tree + QStringLiteral("/a.txt"), "data a");
            touch(tree + QStringLiteral("/sub/b.txt"), "data b");
            QFile::link(QStringLiteral("a.txt"), tree + QStringLiteral("/link.txt"));
            return tree;
        };

        // Regular files can use the data-only fallback on FAT.
        touch(d(QStringLiteral("single.txt")), "single");
        core.importToFolder({QUrl::fromLocalFile(d(QStringLiteral("single.txt")))}, true, dir);
        QVERIFY(core.waitForFileJobs());
        QFile single(dir + QStringLiteral("/single.txt"));
        QVERIFY(single.open(QIODevice::ReadOnly));
        QCOMPARE(single.readAll(), QByteArray("single"));

        // A FAT filesystem cannot represent symlinks. Do not silently turn
        // them into regular files or skip them: fail and keep the source.
        const QString tree = makeTree(QStringLiteral("tree"));
        core.importToFolder({QUrl::fromLocalFile(tree)}, true, dir);
        QVERIFY(core.waitForFileJobs());
        QVERIFY(QFileInfo(tree).isDir());
        QVERIFY(QFileInfo(tree + QStringLiteral("/link.txt")).isSymLink());
        QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/tree")));

        const QString moving = makeTree(QStringLiteral("moving"));
        core.importToFolder({QUrl::fromLocalFile(moving)}, false, dir);
        QVERIFY(core.waitForFileJobs());
        QVERIFY(QFileInfo(moving).isDir());
        QVERIFY(QFileInfo(moving + QStringLiteral("/link.txt")).isSymLink());
        QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/moving")));
        const QStringList hidden = QDir(dir).entryList(QDir::Hidden | QDir::AllEntries | QDir::NoDotAndDotDot)
                                       .filter(QStringLiteral(".alveus-copy-"));
        QVERIFY2(hidden.isEmpty(), qPrintable(hidden.join(u' ')));
        QVERIFY(QDir(dir).removeRecursively());
    }

    // Same-filesystem directory moves still validate every descendant. That
    // work must run in the worker for imports, archives, and undo.
    void sameFilesystemDirectoryMovesRunInBackground()
    {
        AlveusCore core(desk(), store());
        const QString target = m_tmp.path() + QStringLiteral("/same-disk-target");
        QVERIFY(QDir().mkpath(target));
        QVERIFY(Alveus::FileOps::sameFilesystem(desk(), target));

        const QString source = d(QStringLiteral("background-tree"));
        QVERIFY(QDir().mkpath(source + QStringLiteral("/nested")));
        touch(source + QStringLiteral("/nested/item.txt"), "payload");

        const QStringList moved = core.importToFolder({QUrl::fromLocalFile(source)}, false, target);
        QCOMPARE(moved, QStringList{target + QStringLiteral("/background-tree")});
        QVERIFY(core.fileJobsBusy());
        QVERIFY(core.fileJob().value(QStringLiteral("active")).toBool());
        QVERIFY(core.waitForFileJobs());
        QVERIFY(!QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(target + QStringLiteral("/background-tree/nested/item.txt")));
        QCOMPARE(core.undoText(), QStringLiteral("move|1"));

        QVERIFY(core.undo());
        QVERIFY(core.fileJobsBusy());
        QVERIFY(core.waitForFileJobs());
        QVERIFY(QFileInfo::exists(source + QStringLiteral("/nested/item.txt")));
        QVERIFY(!QFileInfo::exists(target + QStringLiteral("/background-tree")));

        const QString archiveSource = d(QStringLiteral("archive-tree"));
        QVERIFY(QDir().mkpath(archiveSource + QStringLiteral("/nested")));
        touch(archiveSource + QStringLiteral("/nested/archive.txt"), "archive");
        const QString archive = core.archiveFiles({QStringLiteral("archive-tree")}, QStringLiteral("Alveus test archive"));
        QVERIFY(!archive.isEmpty());
        QVERIFY(core.fileJobsBusy());
        QVERIFY(core.waitForFileJobs());
        QVERIFY(!QFileInfo::exists(archiveSource));
        QVERIFY(QFileInfo::exists(archive + QStringLiteral("/archive-tree/nested/archive.txt")));
        QCOMPARE(core.undoText(), QStringLiteral("archive|1"));

        QVERIFY(core.undo());
        QVERIFY(core.fileJobsBusy());
        QVERIFY(core.waitForFileJobs());
        QVERIFY(QFileInfo::exists(archiveSource + QStringLiteral("/nested/archive.txt")));
        QVERIFY(!QFileInfo::exists(archive + QStringLiteral("/archive-tree")));
    }

    // Moving to another disk runs in the background, with progress, and can be
    // undone; a cancelled copy leaves nothing behind.
    void backgroundJobsOnAnotherDisk()
    {
        QTemporaryDir other(QStringLiteral("/dev/shm/alveus-test-XXXXXX"));
        if (!other.isValid() || Alveus::FileOps::sameFilesystem(desk(), other.path())) {
            QSKIP("needs /dev/shm on a different filesystem");
        }
        AlveusCore core(desk(), store());
        QSignalSpy jobs(&core, &AlveusCore::fileJobChanged);

        QDir().mkpath(d(QStringLiteral("big/sub")));
        touch(d(QStringLiteral("big/sub/x.txt")), "x");
        QFile::link(QStringLiteral("sub/x.txt"), d(QStringLiteral("big/link")));
        const QStringList moved = core.importToFolder({QUrl::fromLocalFile(d(QStringLiteral("big")))}, false, other.path());
        QCOMPARE(moved, QStringList{other.path() + QStringLiteral("/big")});
        QVERIFY(core.fileJob().value(QStringLiteral("active")).toBool());
        QVERIFY(!core.canUndo() || core.undoText() != QStringLiteral("move|1")); // undo step comes when it is done
        QVERIFY(core.waitForFileJobs());
        QVERIFY(!core.fileJob().value(QStringLiteral("active")).toBool());
        QVERIFY(jobs.count() >= 2);
        QVERIFY(QFile::exists(other.path() + QStringLiteral("/big/sub/x.txt")));
        QVERIFY(QFileInfo(other.path() + QStringLiteral("/big/link")).isSymLink());
        QVERIFY(!QFileInfo::exists(d(QStringLiteral("big"))));
        QCOMPARE(core.undoText(), QStringLiteral("move|1"));

        QVERIFY(core.undo()); // back across disks: in the background too
        QVERIFY(!core.canUndo() || core.undoText() == QStringLiteral("move|1"));
        QVERIFY(core.waitForFileJobs());
        QVERIFY(QFile::exists(d(QStringLiteral("big/sub/x.txt"))));
        QVERIFY(!QFileInfo::exists(other.path() + QStringLiteral("/big")));

        // cancel: no half copy, original untouched, nothing to undo
        QFile big(d(QStringLiteral("large.bin")));
        QVERIFY(big.open(QIODevice::WriteOnly));
        const QByteArray mb(1 << 20, 'z');
        for (int i = 0; i < 64; ++i) {
            big.write(mb);
        }
        big.close();
        core.importToFolder({QUrl::fromLocalFile(d(QStringLiteral("large.bin"))), QUrl::fromLocalFile(d(QStringLiteral("notes")))},
                            true, other.path());
        core.cancelFileJobs();
        QVERIFY(core.waitForFileJobs());
        const QStringList left = QDir(other.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        QVERIFY2(left.isEmpty() || left == QStringList{QStringLiteral("large.bin")}, qPrintable(left.join(u' ')));
        QVERIFY(!left.contains(QStringLiteral("notes"))); // the second one never started
        QVERIFY(QFile::exists(d(QStringLiteral("large.bin"))));
    }

    // The desktop is not re-read on every write of a growing file.
    void growingFileIsNotRescannedOnEveryWrite()
    {
        AlveusCore core(desk(), store());
        QSignalSpy changes(&core, &AlveusCore::changed);
        const QString path = d(QStringLiteral("download.bin"));
        touch(path, "start");
        QTRY_VERIFY_WITH_TIMEOUT(names(core.looseFiles(0, QString())).contains(QStringLiteral("download.bin")), 3000);
        changes.clear();
        QFile f(path);
        QVERIFY(f.open(QIODevice::Append));
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 2000) { // ~200 writes in 2 s
            f.write(QByteArray(1024, 'a'));
            f.flush();
            QTest::qWait(10);
        }
        f.close();
        QTest::qWait(1300);
        QVERIFY2(changes.count() <= 6, qPrintable(QString::number(changes.count())));
        qint64 size = -1;
        for (const QVariant &v : core.looseFiles(0, QString())) {
            if (v.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("download.bin")) {
                size = v.toMap().value(QStringLiteral("size")).toLongLong();
            }
        }
        QCOMPARE(size, QFileInfo(path).size());
    }

    void targetedFileModelUpdatesAndInvalidation()
    {
        AlveusCore core(desk(), store());
        core.looseFiles(0, QString()); // populate the per-activity cache before edits
        QSignalSpy fenceChanges(&core, &AlveusCore::fencesChanged);
        QSignalSpy fileChanges(&core, &AlveusCore::fileModelsChanged);
        QSignalSpy positionChanges(&core, &AlveusCore::desktopPositionsChanged);

        const QString id = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2,
                                            QStringLiteral("PDF"), QStringLiteral("pdf"));
        const QString otherId = core.createFence(0, QString(), 0, 0.3, 0, 0.2, 0.2,
                                                 QStringLiteral("Images"), QStringLiteral("png"));
        QCOMPARE(names(core.filesInFence(id, QString())), QStringList{QStringLiteral("report.pdf")});
        QVERIFY(core.filesInFence(otherId, QString()).isEmpty());
        QVERIFY(fileChanges.count() > 0);

        // Appearance edits update the fence row, not every file model.
        fileChanges.clear();
        fenceChanges.clear();
        core.updateFence(id, {{QStringLiteral("color"), QStringLiteral("#334455")}});
        QCOMPARE(fenceChanges.count(), 1);
        QCOMPARE(fileChanges.count(), 0);

        // Rule edits invalidate placement and notify only the lists that changed.
        fileChanges.clear();
        fenceChanges.clear();
        core.updateFence(id, {{QStringLiteral("rules"), QStringLiteral("mp3")}});
        QCOMPARE(fenceChanges.count(), 1);
        QCOMPARE(fileChanges.count(), 1);
        QStringList affected = qvariant_cast<QStringList>(fileChanges.first().at(1));
        QVERIFY(affected.contains(id));
        QVERIFY(affected.contains(QString()));
        QVERIFY(!affected.contains(otherId));
        QCOMPARE(names(core.filesInFence(id, QString())), QStringList{QStringLiteral("song.mp3")});

        // A new file changes only the destination fence model; removal does likewise.
        fileChanges.clear();
        touch(d(QStringLiteral("fresh.mp3")), "fresh");
        core.rescanNow();
        QCOMPARE(names(core.filesInFence(id, QString())),
                 (QStringList{QStringLiteral("fresh.mp3"), QStringLiteral("song.mp3")}));
        QCOMPARE(fileChanges.count(), 1);
        affected = qvariant_cast<QStringList>(fileChanges.first().at(1));
        QVERIFY(affected.contains(id));
        QVERIFY(!affected.contains(otherId));

        fileChanges.clear();
        QVERIFY(QFile::remove(d(QStringLiteral("fresh.mp3"))));
        core.rescanNow();
        QCOMPARE(names(core.filesInFence(id, QString())), QStringList{QStringLiteral("song.mp3")});
        QCOMPARE(fileChanges.count(), 1);

        // Manual assignment and undo must update the cache in both directions.
        core.updateFence(id, {{QStringLiteral("rules"), QStringLiteral("pdf")}});
        fileChanges.clear();
        core.assignFiles({QStringLiteral("song.mp3")}, id);
        QCOMPARE(core.fenceOfFile(QStringLiteral("song.mp3"), QString()), id);
        QCOMPARE(names(core.filesInFence(id, QString())),
                 (QStringList{QStringLiteral("report.pdf"), QStringLiteral("song.mp3")}));
        QVERIFY(fileChanges.count() > 0);
        affected = qvariant_cast<QStringList>(fileChanges.first().at(1));
        QVERIFY(affected.contains(id));
        QVERIFY(affected.contains(QString()));

        fileChanges.clear();
        QVERIFY(core.undo());
        QCOMPARE(core.fenceOfFile(QStringLiteral("song.mp3"), QString()), QString());
        QCOMPARE(names(core.filesInFence(id, QString())), QStringList{QStringLiteral("report.pdf")});
        QVERIFY(fileChanges.count() > 0);

        // Moving loose icons updates their positions without rebuilding fence contents.
        fileChanges.clear();
        positionChanges.clear();
        core.setFilePositions({QVariantMap{{QStringLiteral("name"), QStringLiteral("notes")},
                                           {QStringLiteral("screen"), 0},
                                           {QStringLiteral("page"), 0},
                                           {QStringLiteral("col"), 1},
                                           {QStringLiteral("row"), 1}}});
        QCOMPARE(positionChanges.count(), 1);
        QCOMPARE(fileChanges.count(), 0);
    }

    void cachedFileModelsScaleAcrossManyFences()
    {
        AlveusCore core(desk(), store());
        const QStringList extensions = {QStringLiteral("pdf"), QStringLiteral("jpg"), QStringLiteral("png"),
                                        QStringLiteral("txt"), QStringLiteral("mp3"), QStringLiteral("docx"),
                                        QStringLiteral("xlsx"), QStringLiteral("zip"), QStringLiteral("mp4"),
                                        QStringLiteral("csv")};
        QStringList fenceIds;
        for (int i = 0; i < extensions.size(); ++i) {
            fenceIds << core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2,
                                         extensions.at(i), extensions.at(i));
        }

        for (int i = 0; i < 600; ++i) {
            const QString path = d(QStringLiteral("bulk-%1.%2").arg(i).arg(extensions.at(i % extensions.size())));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        }
        core.rescanNow();

        const int expected = QDir(desk()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System).size();
        int observed = core.looseFiles(0, QString()).size();
        for (const QString &id : fenceIds) {
            observed += core.filesInFence(id, QString()).size();
        }
        QCOMPARE(observed, expected);

        // Repeated retrieval exercises cache hits rather than re-evaluating every rule for every file.
        QBENCHMARK {
            int count = core.looseFiles(0, QString()).size();
            for (const QString &id : fenceIds) {
                count += core.filesInFence(id, QString()).size();
            }
            observed = count;
        }
        QCOMPARE(observed, expected);
    }

    void settingsBlockedMessageOnce()
    {
        QDir().mkpath(m_tmp.path() + QStringLiteral("/store"));
        touch(store(), R"({"version":999})");
        AlveusCore core(desk(), store());
        QSignalSpy messages(&core, &AlveusCore::message);
        core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("A"), QString());
        core.saveNow();
        core.createFence(0, QString(), 0, 0.3, 0, 0.2, 0.2, QStringLiteral("B"), QString());
        core.saveNow();
        QTest::qWait(50);
        int blocked = 0;
        for (const auto &m : messages) {
            blocked += m.at(0).toString() == QStringLiteral("settings-save-blocked");
        }
        QCOMPARE(blocked, 1);
        QCOMPARE(QFile(store()).size(), qint64(QByteArray(R"({"version":999})").size())); // untouched
    }

    void diagnosticsCaptureWarningsAndCanBeCleared()
    {
        AlveusCore core(desk(), store());
        core.clearDiagnostics();

        qWarning("Alveus diagnostics test warning");
        const QString privatePath = QDir::homePath() + QStringLiteral("/alveus-private-diagnostic.txt");
        qWarning().noquote() << privatePath;
        const QVariantList entries = core.recentDiagnostics(10);
        QVERIFY(entries.size() >= 2);
        const QVariantMap latest = entries.first().toMap();
        QCOMPARE(latest.value(QStringLiteral("level")).toString(), QStringLiteral("warning"));
        const QString latestMessage = latest.value(QStringLiteral("message")).toString();
        QVERIFY(latestMessage.contains(QStringLiteral("~")));
        QVERIFY(!latestMessage.contains(QDir::homePath()));
        QVERIFY(QFileInfo::exists(core.diagnosticsPath()));

        core.clearDiagnostics();
        QVERIFY(core.recentDiagnostics().isEmpty());
    }

    void diagnosticsHandlerIsReentrant()
    {
        AlveusCore core(desk(), store());
        core.clearDiagnostics();
        s_triggerNestedDiagnostic = true;
        qWarning("outer diagnostics reentrancy test");

        bool outerFound = false;
        bool nestedFound = false;
        for (const QVariant &value : core.recentDiagnostics(10)) {
            const QString message = value.toMap().value(QStringLiteral("message")).toString();
            outerFound = outerFound || message.contains(QStringLiteral("outer diagnostics reentrancy test"));
            nestedFound = nestedFound || message.contains(QStringLiteral("nested diagnostics test warning"));
        }
        QVERIFY(outerFound);
        QVERIFY(!nestedFound);
    }

    void diagnosticsRotatesLogsAndKeepsThreeGenerations()
    {
        AlveusCore core(desk(), store());
        core.clearDiagnostics();
        const QString path = core.diagnosticsPath();

        // Force four rotations. The logger must retain only the newest three
        // archives and keep the final warning in the active JSON-lines file.
        for (int i = 0; i < 4; ++i) {
            QFile oversized(path);
            QVERIFY(oversized.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(oversized.resize(1024 * 1024 + 1));
            oversized.close();
            qWarning("diagnostics rotation marker %d", i);
        }

        QVERIFY(QFileInfo::exists(path + QStringLiteral(".1")));
        QVERIFY(QFileInfo::exists(path + QStringLiteral(".2")));
        QVERIFY(QFileInfo::exists(path + QStringLiteral(".3")));
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".4")));

        const QVariantList entries = core.recentDiagnostics(10);
        QVERIFY(!entries.isEmpty());
        QVERIFY(entries.first().toMap().value(QStringLiteral("message")).toString()
                    .contains(QStringLiteral("diagnostics rotation marker 3")));
    }

    void diagnosticsCaptureApplicationErrors()
    {
        QDir().mkpath(QFileInfo(store()).absolutePath());
        touch(store(), R"({"version":999})");

        AlveusCore core(desk(), store());
        QSignalSpy messages(&core, &AlveusCore::message);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() > 0, 1000);

        bool found = false;
        for (const QVariant &value : core.recentDiagnostics(20)) {
            const QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("message")).toString()
                    .contains(QStringLiteral("settings-newer-version"))) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    void desktopFileChangesAreRescanned()
    {
        const QString path = d(QStringLiteral("watch.txt"));
        touch(path, "a");
        AlveusCore core(desk(), store());
        auto fileSize = [&core]() -> qint64 {
            for (const QVariant &entry : core.looseFiles(0, QString())) {
                const QVariantMap item = entry.toMap();
                if (item.value(QStringLiteral("name")).toString() == QStringLiteral("watch.txt")) {
                    return item.value(QStringLiteral("size")).toLongLong();
                }
            }
            return -1;
        };
        QCOMPARE(fileSize(), qint64(1));
        touch(path, "a much longer file body");
        QTRY_COMPARE_WITH_TIMEOUT(fileSize(), qint64(23), 4000);
    }

    void failedUndoKeepsTheUndoEntry()
    {
        AlveusCore core(desk(), store());
        const QString outside = m_tmp.path() + QStringLiteral("/outside.txt");
        touch(outside, "move me");
        const QStringList moved = core.importToDesktop({QUrl::fromLocalFile(outside)}, false, QString());
        QCOMPARE(moved.size(), 1);
        touch(outside, "conflict");

        QVERIFY(!core.undo());
        QVERIFY(core.canUndo());
        QVERIFY(QFile::exists(moved.first()));

        QVERIFY(QFile::remove(outside));
        QVERIFY(core.undo());
        QVERIFY(QFile::exists(outside));
        QVERIFY(!QFile::exists(moved.first()));
    }

    void migrationFromVersion1()
    {
        QDir().mkpath(m_tmp.path() + QStringLiteral("/store"));
        touch(store(), R"({"version":1,"welcomeDismissed":true,
            "fences":[{"id":"abc","title":"Docs","screen":0,"x":0.1,"y":0.1,"w":0.2,"h":0.2,"collapsed":false,"extensions":["pdf","odt"]}],
            "files":{"notes":{"fence":"abc","manual":true},"song.mp3":{"fence":"","manual":false}}})");
        AlveusCore core(desk(), store());
        QCOMPARE(core.fence(QStringLiteral("abc")).value(QStringLiteral("rules")).toString(), QStringLiteral("pdf, odt"));
        QCOMPARE(names(core.filesInFence(QStringLiteral("abc"), QString())), (QStringList{QStringLiteral("notes"), QStringLiteral("report.pdf")}));
        QVERIFY(!core.showWelcome());
    }

    void snapshots()
    {
        AlveusCore core(desk(), store());
        const QString a = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("A"), QStringLiteral("pdf"));
        const QString snap = core.saveSnapshot(QStringLiteral("one"));
        core.removeFence(a);
        core.createFence(0, QString(), 0, 0.5, 0.5, 0.2, 0.2, QStringLiteral("B"), QString());
        QCOMPARE(core.fenceCount(), 1);
        core.restoreSnapshot(snap);
        QCOMPARE(core.fenceCount(), 1);
        QCOMPARE(core.fence(a).value(QStringLiteral("title")).toString(), QStringLiteral("A"));
        QVERIFY(core.undo()); // back to "B"
        QVERIFY(core.fence(a).isEmpty());
        core.deleteSnapshot(snap);
        QVERIFY(core.snapshots().isEmpty());
    }

    // ---- file operations ----------------------------------------------------

    void importAndUndo()
    {
        AlveusCore core(desk(), store());
        const QString id = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("In"), QString());
        const QString outside = m_tmp.path() + QStringLiteral("/outside");
        QDir().mkpath(outside + QStringLiteral("/dir/sub"));
        touch(outside + QStringLiteral("/report.pdf"));
        touch(outside + QStringLiteral("/dir/sub/deep.txt"));

        QStringList got = core.importToDesktop({QUrl::fromLocalFile(outside + QStringLiteral("/report.pdf"))}, true, id);
        QCOMPARE(got, QStringList{d(QStringLiteral("report (1).pdf"))});
        QVERIFY(core.waitForFileJobs());
        QVERIFY(QFile::exists(outside + QStringLiteral("/report.pdf")));
        QCOMPARE(core.fenceOfFile(QStringLiteral("report (1).pdf"), QString()), id);

        got = core.importToDesktop({QUrl::fromLocalFile(outside + QStringLiteral("/dir"))}, false, QString());
        QVERIFY(core.waitForFileJobs()); // same-filesystem directory validation runs in the worker
        QVERIFY(!QFile::exists(outside + QStringLiteral("/dir")));
        QVERIFY(QFile::exists(d(QStringLiteral("dir/sub/deep.txt"))));
        QVERIFY(core.undo()); // the folder goes back where it was
        QVERIFY(core.waitForFileJobs()); // undo of a directory move is asynchronous too
        QVERIFY(QFile::exists(outside + QStringLiteral("/dir/sub/deep.txt")));
        QVERIFY(!QFile::exists(d(QStringLiteral("dir"))));

        // a desktop file dropped on a fence is only reassigned
        core.importToDesktop({QUrl::fromLocalFile(d(QStringLiteral("song.mp3")))}, false, id);
        QCOMPARE(core.fenceOfFile(QStringLiteral("song.mp3"), QString()), id);
        QVERIFY(QFile::exists(d(QStringLiteral("song.mp3"))));
        // never into itself
        QVERIFY(core.importToFolder({QUrl::fromLocalFile(desk())}, true, d(QStringLiteral("Projects"))).isEmpty());
    }

    void renameTrashCreateUndo()
    {
        AlveusCore core(desk(), store());
        const QString id = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("F"), QString());
        core.assignFiles({QStringLiteral("song.mp3")}, id);
        QVERIFY(core.renamePath(d(QStringLiteral("song.mp3")), QStringLiteral("tune.mp3")));
        QCOMPARE(core.fenceOfFile(QStringLiteral("tune.mp3"), QString()), id);
        QVERIFY(!core.renamePath(d(QStringLiteral("tune.mp3")), QStringLiteral("notes")));
        QVERIFY(!core.renamePath(d(QStringLiteral("tune.mp3")), QStringLiteral("a/b")));
        QVERIFY(core.undo());
        QVERIFY(QFile::exists(d(QStringLiteral("song.mp3"))));
        QCOMPARE(core.fenceOfFile(QStringLiteral("song.mp3"), QString()), id);

        QCOMPARE(core.trashPaths({d(QStringLiteral("notes")), d(QStringLiteral("Projects"))}), 2);
        QVERIFY(!QFile::exists(d(QStringLiteral("notes"))));
        QVERIFY(QFile::exists(m_tmp.path() + QStringLiteral("/data/Trash/files/notes")));
        QVERIFY(core.undo());
        QVERIFY(core.waitForFileJobs()); // restoring the trashed directory uses the worker
        QVERIFY(QFile::exists(d(QStringLiteral("notes"))));
        QVERIFY(QFileInfo(d(QStringLiteral("Projects"))).isDir());
        QVERIFY(!QFile::exists(m_tmp.path() + QStringLiteral("/data/Trash/info/notes.trashinfo")));

        const QString folder = core.createFolder(QString(), QStringLiteral("Нова тека"));
        QCOMPARE(folder, d(QStringLiteral("Нова тека")));
        QCOMPARE(core.createFolder(QString(), QStringLiteral("Нова тека")), d(QStringLiteral("Нова тека (1)")));
        const QString txt = core.createTextFile(QString(), QStringLiteral("Нотатка.txt"));
        QVERIFY(QFile::exists(txt));
        QVERIFY(core.undo());
        QVERIFY(!QFile::exists(txt));
    }

    void clipboard()
    {
        AlveusCore core(desk(), store());
        const QString outside = m_tmp.path() + QStringLiteral("/outside");
        QDir().mkpath(outside);
        touch(outside + QStringLiteral("/clip.txt"));
        core.copyToClipboard({outside + QStringLiteral("/clip.txt")}, false);
        QVERIFY(core.canPaste());
        QCOMPARE(core.paste(QString(), QString()), QStringList{d(QStringLiteral("clip.txt"))});
        QVERIFY(core.waitForFileJobs());
        QVERIFY(QFile::exists(outside + QStringLiteral("/clip.txt")));
        core.copyToClipboard({d(QStringLiteral("notes"))}, false); // copy of a desktop file onto the desktop
        QCOMPARE(core.paste(QString(), QString()), QStringList{d(QStringLiteral("notes (1)"))});
        QVERIFY(core.waitForFileJobs());
        QVERIFY(QFileInfo(d(QStringLiteral("notes (1)"))).exists());
        core.copyToClipboard({d(QStringLiteral("clip.txt"))}, true); // cut into a folder
        QCOMPARE(core.paste(d(QStringLiteral("Projects")), QString()), QStringList{d(QStringLiteral("Projects/clip.txt"))});
        QVERIFY(!QFile::exists(d(QStringLiteral("clip.txt"))));
        QVERIFY(!core.canPaste()); // a cut is used up
    }

    void portals()
    {
        AlveusCore core(desk(), store());
        const QString dl = m_tmp.path() + QStringLiteral("/Downloads");
        QDir().mkpath(dl + QStringLiteral("/Telegram Desktop"));
        touch(dl + QStringLiteral("/a.zip"));
        const QString id = core.createPortal(0, QString(), 0, 0, 0, 0.3, 0.3, QStringLiteral("Downloads"), dl);
        QCOMPARE(names(core.filesInFence(id, QString())), (QStringList{QStringLiteral("Telegram Desktop"), QStringLiteral("a.zip")}));
        // portal fences never take desktop files
        core.updateFence(id, {{QStringLiteral("rules"), QStringLiteral("pdf")}});
        QCOMPARE(core.fenceOfFile(QStringLiteral("report.pdf"), QString()), QString());
        core.importToFolder({QUrl::fromLocalFile(d(QStringLiteral("notes")))}, false, dl);
        QVERIFY(QFile::exists(dl + QStringLiteral("/notes")));
        QVERIFY(names(core.listFolder(dl)).contains(QStringLiteral("notes")));
        QCOMPARE(core.parentFolder(dl + QStringLiteral("/Telegram Desktop")), dl);
        QCOMPARE(core.folderName(dl), QStringLiteral("Downloads"));
        // the watcher notices changes made by others
        touch(dl + QStringLiteral("/later.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(names(core.listFolder(dl)).contains(QStringLiteral("later.txt")), 3000);
    }

    void tidyUpHelper()
    {
        age(d(QStringLiteral("song.mp3")), 100);
        age(d(QStringLiteral("notes")), 70);
        AlveusCore core(desk(), store());
        QCOMPARE(names(core.staleFiles(60)), (QStringList{QStringLiteral("notes"), QStringLiteral("song.mp3")}));
        QCOMPARE(core.staleCount(), 2);
        QVERIFY(!core.cleanupDue()); // fewer than 5
        const QString target = core.archiveFiles({QStringLiteral("song.mp3"), QStringLiteral("notes")}, QStringLiteral("Архів стільниці"));
        QVERIFY(target.startsWith(m_tmp.path() + QStringLiteral("/Documents/Архів стільниці/")));
        QVERIFY(QFile::exists(target + QStringLiteral("/song.mp3")));
        QVERIFY(!QFile::exists(d(QStringLiteral("song.mp3"))));
        QVERIFY(core.undo());
        QVERIFY(QFile::exists(d(QStringLiteral("song.mp3"))));
        QVERIFY(QFile::exists(d(QStringLiteral("notes"))));
    }

    void watcherAndRunner()
    {
        AlveusCore core(desk(), store());
        const QString id = core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Рахунки"), QString());
        touch(d(QStringLiteral("fresh.txt")));
        QTRY_VERIFY_WITH_TIMEOUT(core.fenceOfFile(QStringLiteral("fresh.txt"), QString()).isEmpty()
                                     && core.runnerMatches(QStringLiteral("fresh")).size() == 1, 3000);
        const QVariantList m = core.runnerMatches(QStringLiteral("рах"));
        QCOMPARE(m.size(), 2); // the fence and the xlsx
        QCOMPARE(m.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("fence:") + id);
        QSignalSpy spy(&core, &AlveusCore::fenceRequested);
        core.hidden();
        core.setHidden(true);
        core.runnerRun(QStringLiteral("fence:") + id);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!core.hidden());
        QVERIFY(core.runnerMatches(QStringLiteral("x")).isEmpty()); // too short
    }

    void runnerOverDBus()
    {
        if (!QDBusConnection::sessionBus().isConnected()) {
            QSKIP("no session bus");
        }
        AlveusCore core(desk(), store());
        core.createFence(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Звіти"), QString());
        auto *svc = new RunnerService(&core);
        QVERIFY(svc->registerOnBus());
        QProcess gdbus;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
        gdbus.setProcessEnvironment(env);
        gdbus.start(QStringLiteral("gdbus"), {QStringLiteral("call"), QStringLiteral("--session"), QStringLiteral("--dest"),
                                              QStringLiteral("ua.bezeza.alveus"), QStringLiteral("--object-path"), QStringLiteral("/runner"),
                                              QStringLiteral("--method"), QStringLiteral("org.kde.krunner1.Match"), QStringLiteral("звіт")});
        QTRY_VERIFY_WITH_TIMEOUT(gdbus.state() == QProcess::NotRunning, 5000);
        const QString out = QString::fromUtf8(gdbus.readAllStandardOutput());
        QVERIFY2(out.contains(QStringLiteral("Звіти")) && out.contains(QStringLiteral("fence:")), qPrintable(out + gdbus.readAllStandardError()));
        QDBusConnection::sessionBus().unregisterService(QStringLiteral("ua.bezeza.alveus"));
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/runner"));
    }

    // ---- integrations --------------------------------------------------------

    void integrationsParsing()
    {
        QCOMPARE(Integrations::parsePhones(QStringLiteral("a1b2 Pixel 8\n\nc3d4 Samsung Galaxy\n")).size(), 2);
        QCOMPARE(Integrations::parsePhones(QStringLiteral("a1b2 Pixel 8\n")).first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Pixel 8"));

        const QString cfg = m_tmp.path() + QStringLiteral("/cfg");
        QDir().mkpath(cfg + QStringLiteral("/kdeconnect/dev123/kdeconnect_share"));
        touch(cfg + QStringLiteral("/kdeconnect/dev123/kdeconnect_share/config"),
              "[General]\nincoming_path=$HOME/Phone\n");
        QCOMPARE(Integrations::phoneFolder(cfg), QDir::homePath() + QStringLiteral("/Phone"));
        touch(cfg + QStringLiteral("/spectaclerc"), "[ImageSave]\nimageSaveLocation=file:///home/u/Pictures/Знімки/\n");
        QCOMPARE(Integrations::screenshotFolder(cfg), QStringLiteral("/home/u/Pictures/Знімки"));

        const QByteArray camera = R"([{"type":"PipeWire:Interface:Node","info":{"state":"running","props":{"media.class":"Video/Source","device.api":"v4l2"}}}])";
        const QByteArray screen = R"([{"type":"PipeWire:Interface:Node","info":{"state":"running","props":{"media.class":"Video/Source","node.name":"xdg-desktop-portal-kde"}}}])";
        const QByteArray idle = R"([{"type":"PipeWire:Interface:Node","info":{"state":"suspended","props":{"media.class":"Video/Source"}}}])";
        QVERIFY(!Integrations::screencastIn(camera));
        QVERIFY(Integrations::screencastIn(screen));
        QVERIFY(!Integrations::screencastIn(idle));
        QVERIFY(!Integrations::screencastIn("not json"));
    }


    void drives()
    {
        // which mounts count as disks
        QVERIFY(isUserMount(QStringLiteral("/"), QStringLiteral("btrfs"), QStringLiteral("/dev/nvme0n1p3")));
        QVERIFY(isUserMount(QStringLiteral("/mnt/Data"), QStringLiteral("ntfs3"), QStringLiteral("/dev/sdb2")));
        QVERIFY(isUserMount(QStringLiteral("/run/media/user/USB"), QStringLiteral("vfat"), QStringLiteral("/dev/sdc1")));
        QVERIFY(isUserMount(QStringLiteral("/mnt/nas"), QStringLiteral("cifs"), QStringLiteral("//nas/share")));
        QVERIFY(isNetworkMount(QStringLiteral("fuse.sshfs"), QStringLiteral("/dev/fuse")));
        QVERIFY(isNetworkMount(QStringLiteral("nfs4"), QStringLiteral("server:/share")));
        QVERIFY(!isNetworkMount(QStringLiteral("ntfs3"), QStringLiteral("/dev/sdb2")));
        QVERIFY(isUserMount(QStringLiteral("/media/Windows"), QStringLiteral("fuseblk"), QStringLiteral("/dev/sda3"))); // ntfs-3g
        QVERIFY(isUserMount(QStringLiteral("/boot/efi"), QStringLiteral("vfat"), QStringLiteral("/dev/nvme0n1p1")));
        QVERIFY(!isUserMount(QStringLiteral("/home"), QStringLiteral("btrfs"), QStringLiteral("/dev/nvme0n1p3")));
        QVERIFY(!isUserMount(QStringLiteral("/run/user/1000"), QStringLiteral("tmpfs"), QStringLiteral("tmpfs")));
        QVERIFY(!isUserMount(QStringLiteral("/var/lib/flatpak"), QStringLiteral("ext4"), QStringLiteral("/dev/sda1")));
        QVERIFY(!isUserMount(QStringLiteral("/sys/fs/cgroup"), QStringLiteral("cgroup2"), QStringLiteral("cgroup2")));

        Drive msr;
        msr.partType = QStringLiteral("e3c9e316-0b5c-4db8-817d-f92df00215ae");
        QCOMPARE(servicePartitionKind(msr), QStringLiteral("microsoftReserved"));
        Drive biosBoot;
        biosBoot.partType = QStringLiteral("21686148-6449-6e6f-744e-656564454649");
        QCOMPARE(servicePartitionKind(biosBoot), QStringLiteral("biosBoot"));
        Drive labelOnly;
        labelOnly.label = QStringLiteral("Recovery");
        QCOMPARE(servicePartitionKind(labelOnly), QString()); // labels alone never hide user data

        // lsblk: discover all partitions; known service partitions are hidden by the visibility policy
        const QByteArray json = R"({"blockdevices":[
          {"name":"sda","path":"/dev/sda","label":null,"fstype":null,"mountpoints":[null],"size":1000204886016,"rm":false,"hotplug":false,"type":"disk",
           "children":[
             {"name":"sda1","path":"/dev/sda1","label":"Recovery","fstype":"ntfs","parttype":"de94bba4-06d1-4d40-a16a-bfd50179d6ac","mountpoints":[null],"size":629145600,"rm":false,"hotplug":false,"type":"part"},
             {"name":"sda2","path":"/dev/sda2","label":"Фільми","fstype":"ntfs","mountpoints":[null],"size":999000000000,"rm":false,"hotplug":false,"type":"part"}]},
          {"name":"nvme0n1","path":"/dev/nvme0n1","fstype":null,"mountpoints":[null],"size":512110190592,"rm":false,"hotplug":false,"type":"disk",
           "children":[
             {"name":"nvme0n1p1","path":"/dev/nvme0n1p1","label":null,"fstype":"vfat","mountpoints":["/boot/efi"],"size":629145600,"rm":false,"hotplug":false,"type":"part"},
             {"name":"nvme0n1p2","path":"/dev/nvme0n1p2","label":"swap","fstype":"swap","parttype":"0657fd6d-a4ab-43c4-84e5-0933c84b4f4f","mountpoints":["[SWAP]"],"size":8589934592,"rm":false,"hotplug":false,"type":"part"},
             {"name":"nvme0n1p3","path":"/dev/nvme0n1p3","label":"fedora","fstype":"btrfs","mountpoints":["/home","/"],"size":500000000000,"rm":false,"hotplug":false,"type":"part"}]},
          {"name":"sdc","path":"/dev/sdc","fstype":null,"mountpoints":[null],"size":"32010928128","rm":true,"hotplug":true,"tran":"usb","type":"disk",
           "children":[{"name":"sdc1","path":"/dev/sdc1","label":"FLASH","fstype":"exfat","mountpoints":[null],"size":"32008830976","rm":true,"hotplug":true,"type":"part"}]}
        ]})";
        const QList<Drive> un = unmountedFromLsblk(json);
        QCOMPARE(un.size(), 4); // small service partitions remain discoverable for configurable filtering
        QCOMPARE(un[0].device, QStringLiteral("/dev/sda1"));
        QCOMPARE(servicePartitionKind(un[0]), QStringLiteral("recovery"));
        QCOMPARE(un[1].device, QStringLiteral("/dev/sda2"));
        QCOMPARE(un[1].label, QStringLiteral("Фільми"));
        QCOMPARE(un[1].fsType, QStringLiteral("ntfs"));
        QVERIFY(!un[1].removable);
        QCOMPARE(un[2].device, QStringLiteral("/dev/nvme0n1p2"));
        QCOMPARE(servicePartitionKind(un[2]), QStringLiteral("swap"));
        QCOMPARE(un[3].device, QStringLiteral("/dev/sdc1"));
        QVERIFY(un[3].removable);
        QVERIFY(un[3].usb); // transport metadata is inherited from the physical parent
        QCOMPARE(un[3].total, qint64(32008830976));
        QVERIFY(removableFromLsblk(json).value(QStringLiteral("/dev/sdc1")));

        // names and icons
        Drive mounted;
        mounted.device = QStringLiteral("/dev/sdb2");
        mounted.mountPoint = QStringLiteral("/mnt/Data");
        mounted.fsType = QStringLiteral("ntfs3");
        mounted.total = 1000;
        mounted.free = 250;
        QVariantMap m = toMap(mounted);
        QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Data"));
        QCOMPARE(m.value(QStringLiteral("mounted")).toBool(), true);
        QCOMPARE(m.value(QStringLiteral("icon")).toString(), QStringLiteral("drive-harddisk"));
        QCOMPARE(toMap(un[1]).value(QStringLiteral("name")).toString(), QStringLiteral("Фільми"));
        QCOMPARE(toMap(un[3]).value(QStringLiteral("icon")).toString(), QStringLiteral("drive-removable-media-usb"));

        // the real machine: the system disk is always there, first
        const QList<Drive> live = fromMounts(QStorageInfo::mountedVolumes());
        QVERIFY(!live.isEmpty());
        QVERIFY(live.first().system);
        QVERIFY(live.first().total > 0);

        // a drives fence keeps its marker
        AlveusCore core(desk(), store());
        const QString id = core.createPortal(0, QString(), 0, 0, 0, 0.2, 0.2, QStringLiteral("Диски"), QStringLiteral("drives:"));
        QCOMPARE(core.fence(id).value(QStringLiteral("portal")).toString(), QStringLiteral("drives:"));
        QTRY_VERIFY_WITH_TIMEOUT(!core.drives().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!core.driveDevices().isEmpty(), 5000);
        const QVariantList visibleDevices = core.visibleDriveDevices(id);
        QVERIFY(!visibleDevices.isEmpty());
        QVERIFY(visibleDevices.first().toMap().contains(QStringLiteral("children")));
        const QString visibleDeviceId = visibleDevices.first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(!visibleDeviceId.isEmpty());
        QVERIFY(core.setDriveVisibilityException(id, visibleDeviceId, QStringLiteral("hide")));
        const QVariantList savedExceptions = core.driveVisibilityExceptions(id);
        QVERIFY(std::any_of(savedExceptions.cbegin(), savedExceptions.cend(), [&visibleDeviceId](const QVariant &value) {
            const QVariantMap exception = value.toMap();
            return exception.value(QStringLiteral("id")).toString() == visibleDeviceId
                && exception.value(QStringLiteral("connected")).toBool();
        }));
    }

    void driveVisibilitySettingsPersistAndFilter()
    {
        AlveusCore core(desk(), store());
        const QString fenceId = core.createPortal(0, QString(), 0, 0, 0, 0.2, 0.2,
                                                  QStringLiteral("Disks"), QStringLiteral("drives:"));

        const QVariantMap defaults = core.driveDefaults();
        const QVariantMap defaultGroups = defaults.value(QStringLiteral("groups")).toMap();
        QVERIFY(defaultGroups.value(QStringLiteral("internal")).toBool());
        QVERIFY(defaultGroups.value(QStringLiteral("usb")).toBool());
        QVERIFY(!defaultGroups.value(QStringLiteral("virtual")).toBool());
        QCOMPARE(defaults.value(QStringLiteral("partitionMode")).toString(), QStringLiteral("all"));
        QVERIFY(defaults.value(QStringLiteral("hideEfi")).toBool());
        QCOMPARE(defaults.value(QStringLiteral("displayMode")).toString(), QStringLiteral("large"));
        QVERIFY(defaults.value(QStringLiteral("separateUsb")).toBool());
        QCOMPARE(defaults.value(QStringLiteral("informationLevel")).toString(), QStringLiteral("detailed"));

        QVERIFY(core.setDriveDefault(QStringLiteral("groups.usb"), false));
        QVERIFY(core.setDriveDefault(QStringLiteral("separateUsb"), false));
        QVERIFY(core.setDriveDefault(QStringLiteral("informationLevel"), QStringLiteral("basic")));
        QVERIFY(core.setDriveDefault(QStringLiteral("hideEfi"), false));
        QVERIFY(core.setDriveOption(fenceId, QStringLiteral("groups.network"), false));
        QVERIFY(core.setDriveOption(fenceId, QStringLiteral("partitionMode"), QStringLiteral("selected")));
        QVERIFY(core.setDriveDefault(QStringLiteral("displayMode"), QStringLiteral("compact")));
        QVERIFY(core.setDriveOption(fenceId, QStringLiteral("displayMode"), QStringLiteral("large")));
        QVERIFY(core.setDriveOption(fenceId, QStringLiteral("separateUsb"), true));
        QVERIFY(core.setDriveOption(fenceId, QStringLiteral("informationLevel"), QStringLiteral("detailed")));
        QVERIFY(core.setDriveVisibilityException(fenceId, QStringLiteral("partuuid:efi@wwn:test"), QStringLiteral("show")));
        QVERIFY(core.setDriveVisibilityException(fenceId, QStringLiteral("uuid:old-disk"), QStringLiteral("hide")));
        QVERIFY(!core.setDriveDefault(QStringLiteral("partitionMode"), QStringLiteral("unknown")));
        QVERIFY(!core.setDriveDefault(QStringLiteral("displayMode"), QStringLiteral("huge")));
        QVERIFY(!core.setDriveDefault(QStringLiteral("informationLevel"), QStringLiteral("verbose")));
        QVERIFY(!core.setDriveOption(fenceId, QStringLiteral("separateUsb"), QStringLiteral("yes")));
        QVERIFY(!core.setDriveOption(fenceId, QStringLiteral("informationLevel"), QStringLiteral("verbose")));
        QVERIFY(!core.setDriveOption(fenceId, QStringLiteral("groups.unknown"), true));
        QVERIFY(!core.setDriveVisibilityException(fenceId, QStringLiteral("bad-id"), QStringLiteral("maybe")));

        const QVariantMap effective = core.driveOptions(fenceId);
        const QVariantMap effectiveGroups = effective.value(QStringLiteral("groups")).toMap();
        QVERIFY(!effectiveGroups.value(QStringLiteral("usb")).toBool()); // inherited global value
        QVERIFY(!effectiveGroups.value(QStringLiteral("network")).toBool()); // per-fence override
        QCOMPARE(effective.value(QStringLiteral("partitionMode")).toString(), QStringLiteral("selected"));
        QCOMPARE(core.driveDefaults().value(QStringLiteral("displayMode")).toString(), QStringLiteral("compact"));
        QCOMPARE(effective.value(QStringLiteral("displayMode")).toString(), QStringLiteral("large"));
        QVERIFY(!core.driveDefaults().value(QStringLiteral("separateUsb")).toBool());
        QCOMPARE(core.driveDefaults().value(QStringLiteral("informationLevel")).toString(), QStringLiteral("basic"));
        QVERIFY(effective.value(QStringLiteral("separateUsb")).toBool()); // per-fence override
        QCOMPARE(effective.value(QStringLiteral("informationLevel")).toString(), QStringLiteral("detailed"));
        QCOMPARE(core.driveVisibilityExceptions(fenceId).size(), 2);

        core.saveNow();
        AlveusCore reloaded(desk(), store());
        const QVariantMap savedDefaults = reloaded.driveDefaults();
        const QVariantMap savedOptions = reloaded.driveOptions(fenceId);
        QVERIFY(!savedDefaults.value(QStringLiteral("groups")).toMap().value(QStringLiteral("usb")).toBool());
        QVERIFY(!savedOptions.value(QStringLiteral("groups")).toMap().value(QStringLiteral("network")).toBool());
        QCOMPARE(savedOptions.value(QStringLiteral("partitionMode")).toString(), QStringLiteral("selected"));
        QCOMPARE(savedDefaults.value(QStringLiteral("displayMode")).toString(), QStringLiteral("compact"));
        QCOMPARE(savedOptions.value(QStringLiteral("displayMode")).toString(), QStringLiteral("large"));
        QVERIFY(!savedDefaults.value(QStringLiteral("separateUsb")).toBool());
        QCOMPARE(savedDefaults.value(QStringLiteral("informationLevel")).toString(), QStringLiteral("basic"));
        QVERIFY(savedOptions.value(QStringLiteral("separateUsb")).toBool());
        QCOMPARE(savedOptions.value(QStringLiteral("informationLevel")).toString(), QStringLiteral("detailed"));
        QCOMPARE(reloaded.driveVisibilityExceptions(fenceId).size(), 2);

        QVERIFY(reloaded.resetDriveOption(fenceId, QStringLiteral("groups.network")));
        QVERIFY(reloaded.driveOptions(fenceId).value(QStringLiteral("groups")).toMap()
                    .value(QStringLiteral("network")).toBool());
        QVERIFY(reloaded.resetDriveOption(fenceId, QStringLiteral("partitionMode")));
        QCOMPARE(reloaded.driveOptions(fenceId).value(QStringLiteral("partitionMode")).toString(), QStringLiteral("all"));
        QVERIFY(reloaded.resetDriveOption(fenceId, QStringLiteral("displayMode")));
        QCOMPARE(reloaded.driveOptions(fenceId).value(QStringLiteral("displayMode")).toString(), QStringLiteral("compact"));
        QVERIFY(reloaded.resetDriveOption(fenceId, QStringLiteral("separateUsb")));
        QVERIFY(!reloaded.driveOptions(fenceId).value(QStringLiteral("separateUsb")).toBool());
        QVERIFY(reloaded.resetDriveOption(fenceId, QStringLiteral("informationLevel")));
        QCOMPARE(reloaded.driveOptions(fenceId).value(QStringLiteral("informationLevel")).toString(), QStringLiteral("basic"));
        QVERIFY(reloaded.setDriveVisibilityException(fenceId, QStringLiteral("uuid:old-disk"), QString()));
        QCOMPARE(reloaded.driveVisibilityExceptions(fenceId).size(), 1);
        reloaded.resetDriveOptions(fenceId);
        QVERIFY(reloaded.driveOptionOverrides(fenceId).isEmpty());
        reloaded.resetDriveDefaults();
        QVERIFY(reloaded.driveDefaults().value(QStringLiteral("groups")).toMap().value(QStringLiteral("usb")).toBool());
        QCOMPARE(reloaded.driveDefaults().value(QStringLiteral("displayMode")).toString(), QStringLiteral("large"));
        QVERIFY(reloaded.driveDefaults().value(QStringLiteral("separateUsb")).toBool());
        QCOMPARE(reloaded.driveDefaults().value(QStringLiteral("informationLevel")).toString(), QStringLiteral("detailed"));

        const auto item = [](const QString &id, const QString &type, bool mounted, qint64 size,
                             const QString &serviceKind = QString(), bool usb = false,
                             bool removable = false, bool network = false) {
            return QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("type"), type},
                {QStringLiteral("mounted"), mounted},
                {QStringLiteral("total"), size},
                {QStringLiteral("serviceKind"), serviceKind},
                {QStringLiteral("usb"), usb},
                {QStringLiteral("removable"), removable},
                {QStringLiteral("network"), network},
                {QStringLiteral("hotplug"), false},
            };
        };
        const QVariantList sample = {
            item(QStringLiteral("data"), QStringLiteral("part"), true, 512LL * 1024 * 1024),
            item(QStringLiteral("efi"), QStringLiteral("part"), true, 512LL * 1024 * 1024, QStringLiteral("efi")),
            item(QStringLiteral("swap"), QStringLiteral("part"), false, 8LL * 1024 * 1024 * 1024, QStringLiteral("swap")),
            item(QStringLiteral("usb"), QStringLiteral("part"), false, 32LL * 1024 * 1024, QString(), true),
            item(QStringLiteral("external"), QStringLiteral("part"), false, 64LL * 1024 * 1024, QString(), false, true),
            item(QStringLiteral("network"), QStringLiteral("network"), true, 0, QString(), false, false, true),
            item(QStringLiteral("virtual"), QStringLiteral("crypt"), true, 4LL * 1024 * 1024 * 1024),
            item(QStringLiteral("optical"), QStringLiteral("rom"), false, 700LL * 1024 * 1024),
        };

        const QVariantMap defaultOptions = reloaded.driveDefaults();
        const QVariantList visible = Alveus::filterDriveList(sample, defaultOptions);
        QStringList visibleIds;
        for (const QVariant &value : visible) {
            visibleIds << value.toMap().value(QStringLiteral("id")).toString();
        }
        QCOMPARE(visibleIds, (QStringList{QStringLiteral("data"), QStringLiteral("usb"), QStringLiteral("external"),
                                          QStringLiteral("network"), QStringLiteral("optical")}));

        QVariantMap overrides = defaultOptions;
        QVariantMap exceptions = {{QStringLiteral("efi"), QStringLiteral("show")},
                                  {QStringLiteral("data"), QStringLiteral("hide")}};
        overrides.insert(QStringLiteral("visibilityExceptions"), exceptions);
        const QVariantList withExceptions = Alveus::filterDriveList(sample, overrides);
        QStringList exceptionIds;
        for (const QVariant &value : withExceptions) {
            exceptionIds << value.toMap().value(QStringLiteral("id")).toString();
        }
        QVERIFY(exceptionIds.contains(QStringLiteral("efi")));
        QVERIFY(!exceptionIds.contains(QStringLiteral("data")));
        QVERIFY(!exceptionIds.contains(QStringLiteral("swap"))); // automatic service filter

        QVariantMap groups = overrides.value(QStringLiteral("groups")).toMap();
        groups.insert(QStringLiteral("usb"), false);
        overrides.insert(QStringLiteral("groups"), groups);
        bool usbVisible = false;
        for (const QVariant &value : Alveus::filterDriveList(sample, overrides)) {
            usbVisible = usbVisible || value.toMap().value(QStringLiteral("id")).toString() == QLatin1String("usb");
        }
        QVERIFY(!usbVisible); // Always show does not override a disabled group

        const QVariantList temporary = Alveus::filterDriveList(sample, overrides, true);
        QCOMPARE(temporary.size(), sample.size()); // temporary reveal never changes saved rules

        QVariantMap mountedOnly = defaultOptions;
        mountedOnly.insert(QStringLiteral("partitionMode"), QStringLiteral("mounted"));
        const QVariantList mounted = Alveus::filterDriveList(sample, mountedOnly);
        for (const QVariant &value : mounted) {
            const QVariantMap drive = value.toMap();
            if (drive.value(QStringLiteral("type")).toString() == QLatin1String("part")) {
                QVERIFY(drive.value(QStringLiteral("mounted")).toBool());
            }
        }

        QVariantMap selected = defaultOptions;
        selected.insert(QStringLiteral("partitionMode"), QStringLiteral("selected"));
        selected.insert(QStringLiteral("visibilityExceptions"),
                        QVariantMap{{QStringLiteral("data"), QStringLiteral("show")}});
        const QVariantList selectedOnly = Alveus::filterDriveList(sample, selected);
        QVERIFY(std::any_of(selectedOnly.cbegin(), selectedOnly.cend(), [](const QVariant &value) {
            return value.toMap().value(QStringLiteral("id")).toString() == QLatin1String("data");
        }));
        QVERIFY(!std::any_of(selectedOnly.cbegin(), selectedOnly.cend(), [](const QVariant &value) {
            return value.toMap().value(QStringLiteral("id")).toString() == QLatin1String("efi");
        }));

        QVariantMap sizeFiltered = defaultOptions;
        sizeFiltered.insert(QStringLiteral("minimumPartitionSizeMiB"), 64);
        const QVariantList aboveMinimum = Alveus::filterDriveList(sample, sizeFiltered);
        QVERIFY(!std::any_of(aboveMinimum.cbegin(), aboveMinimum.cend(), [](const QVariant &value) {
            return value.toMap().value(QStringLiteral("id")).toString() == QLatin1String("usb");
        }));
    }

    void driveHierarchyFilteringPreservesDeviceTree()
    {
        const auto node = [](const QString &id, const QString &type, bool mounted, const QString &serviceKind,
                             const QVariantList &children = {}, bool usb = false) {
            return QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("type"), type},
                {QStringLiteral("mounted"), mounted},
                {QStringLiteral("serviceKind"), serviceKind},
                {QStringLiteral("usb"), usb},
                {QStringLiteral("removable"), usb},
                {QStringLiteral("hotplug"), usb},
                {QStringLiteral("physical"), type == QLatin1String("disk")},
                {QStringLiteral("children"), children},
                {QStringLiteral("childCount"), children.size()},
            };
        };

        const QVariantMap data = node(QStringLiteral("data"), QStringLiteral("part"), true, QString());
        const QVariantMap efi = node(QStringLiteral("efi"), QStringLiteral("part"), true, QStringLiteral("efi"));
        const QVariantMap unmounted = node(QStringLiteral("unused"), QStringLiteral("part"), false, QString());
        const QVariantMap disk = node(QStringLiteral("disk"), QStringLiteral("disk"), false, QString(),
                                      {data, efi, unmounted});
        const QVariantList hierarchy = {disk};

        QVariantMap options{
            {QStringLiteral("groups"), QVariantMap{{QStringLiteral("internal"), true}}},
            {QStringLiteral("hideEfi"), true},
            {QStringLiteral("hideMicrosoftReserved"), true},
            {QStringLiteral("hideRecovery"), true},
            {QStringLiteral("hideSwap"), true},
            {QStringLiteral("hideBiosBoot"), true},
            {QStringLiteral("showUnmounted"), true},
            {QStringLiteral("partitionMode"), QStringLiteral("all")},
            {QStringLiteral("minimumPartitionSizeMiB"), 0},
        };

        QVariantList visible = filterDriveHierarchy(hierarchy, options);
        QCOMPARE(visible.size(), 1);
        QVariantMap visibleDisk = visible.first().toMap();
        QCOMPARE(visibleDisk.value(QStringLiteral("id")).toString(), QStringLiteral("disk"));
        QVariantList visibleChildren = visibleDisk.value(QStringLiteral("children")).toList();
        QCOMPARE(visibleChildren.size(), 2);
        QCOMPARE(visibleDisk.value(QStringLiteral("childCount")).toInt(), 2);
        QCOMPARE(visibleChildren.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("data"));
        QCOMPARE(visibleChildren.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("unused"));

        options.insert(QStringLiteral("showUnmounted"), false);
        visible = filterDriveHierarchy(hierarchy, options);
        QCOMPARE(visible.size(), 1); // the disk remains because its mounted child is visible
        visibleChildren = visible.first().toMap().value(QStringLiteral("children")).toList();
        QCOMPARE(visibleChildren.size(), 1);
        QCOMPARE(visibleChildren.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("data"));

        options.insert(QStringLiteral("partitionMode"), QStringLiteral("selected"));
        options.insert(QStringLiteral("visibilityExceptions"),
                       QVariantMap{{QStringLiteral("data"), QStringLiteral("show")}});
        visible = filterDriveHierarchy(hierarchy, options);
        QCOMPARE(visible.size(), 1);
        visibleChildren = visible.first().toMap().value(QStringLiteral("children")).toList();
        QCOMPARE(visibleChildren.size(), 1);
        QCOMPARE(visibleChildren.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("data"));

        options.insert(QStringLiteral("groups"),
                       QVariantMap{{QStringLiteral("internal"), false}});
        QVERIFY(filterDriveHierarchy(hierarchy, options).isEmpty());

        options.insert(QStringLiteral("groups"),
                       QVariantMap{{QStringLiteral("internal"), true}});
        const QVariantList temporary = filterDriveHierarchy(hierarchy, options, true);
        QCOMPARE(temporary.size(), 1);
        QCOMPARE(temporary.first().toMap().value(QStringLiteral("children")).toList().size(), 3);

        options.remove(QStringLiteral("visibilityExceptions"));
        options.insert(QStringLiteral("partitionMode"), QStringLiteral("all"));
        options.insert(QStringLiteral("showUnmounted"), true);
        options.insert(QStringLiteral("visibilityExceptions"),
                       QVariantMap{{QStringLiteral("disk"), QStringLiteral("hide")}});
        QVERIFY(filterDriveHierarchy(hierarchy, options).isEmpty());
    }

    void driveHierarchyParserHandlesDevices()
    {
        const QByteArray json = R"JSON(
        {
          "blockdevices": [
            {
              "name": "nvme0n1",
              "path": "/dev/nvme0n1",
              "type": "disk",
              "size": 1000204886016,
              "model": "990 PRO 1TB",
              "vendor": "Samsung",
              "tran": "nvme",
              "wwn": "eui.0025381234567890",
              "rm": false,
              "hotplug": false,
              "ro": false,
              "rota": 0,
              "children": [
                {
                  "name": "nvme0n1p1",
                  "path": "/dev/nvme0n1p1",
                  "type": "part",
                  "size": 536870912,
                  "label": "EFI",
                  "fstype": "vfat",
                  "partuuid": "1111-AAAA",
                  "parttype": "c12a7328-f81f-11d2-ba4b-00a0c93ec93b",
                  "mountpoints": ["/boot/efi"]
                },
                {
                  "name": "nvme0n1p2",
                  "path": "/dev/nvme0n1p2",
                  "type": "part",
                  "size": 499000000000,
                  "label": "System",
                  "fstype": "btrfs",
                  "uuid": "ABCD-1234",
                  "mountpoints": ["/", "/home"]
                },
                {
                  "name": "nvme0n1p3",
                  "path": "/dev/nvme0n1p3",
                  "type": "part",
                  "size": 1000000000,
                  "label": "Data",
                  "fstype": "ntfs3",
                  "mountpoint": "/mnt/data"
                },
                {
                  "name": "nvme0n1p4",
                  "path": "/dev/nvme0n1p4",
                  "type": "part",
                  "size": 500000000000,
                  "fstype": "ext4",
                  "uuid": "DEAD-BEEF",
                  "mountpoints": [null]
                },
                {
                  "name": "nvme0n1p5",
                  "path": "/dev/nvme0n1p5",
                  "type": "part",
                  "size": 104857600,
                  "mountpoints": [null]
                }
              ]
            },
            {
              "name": "sdb",
              "path": "/dev/sdb",
              "type": "disk",
              "size": 32000000000,
              "model": "Flash Disk",
              "vendor": "Generic",
              "tran": "usb",
              "rm": true,
              "hotplug": true,
              "rota": 1,
              "children": [
                {
                  "name": "sdb1",
                  "path": "/dev/sdb1",
                  "type": "part",
                  "size": 31900000000,
                  "label": "BACKUP",
                  "fstype": "exfat",
                  "rm": false,
                  "hotplug": false,
                  "mountpoints": [null]
                }
              ]
            },
            {
              "name": "sdc",
              "path": "/dev/sdc",
              "type": "disk",
              "size": 1000000000,
              "rm": true,
              "tran": null
            }
          ]
        })JSON";

        const QList<Drive> devices = driveHierarchyFromLsblk(json);
        QCOMPARE(devices.size(), 3);

        const Drive &nvme = devices.at(0);
        QCOMPARE(nvme.id, QStringLiteral("wwn:eui.0025381234567890"));
        QCOMPARE(nvme.device, QStringLiteral("/dev/nvme0n1"));
        QCOMPARE(nvme.type, QStringLiteral("disk"));
        QVERIFY(nvme.physical);
        QCOMPARE(nvme.vendor, QStringLiteral("Samsung"));
        QCOMPARE(nvme.model, QStringLiteral("990 PRO 1TB"));
        QCOMPARE(nvme.transport, QStringLiteral("nvme"));
        QVERIFY(nvme.rotationalKnown);
        QVERIFY(!nvme.rotational); // ROTA=0 identifies solid-state media
        QVERIFY(nvme.system);
        QCOMPARE(nvme.children.size(), 5);

        const Drive &efi = nvme.children.at(0);
        QCOMPARE(servicePartitionKind(efi), QStringLiteral("efi"));
        QCOMPARE(efi.total, qint64(536870912)); // small service partitions remain discoverable
        QCOMPARE(efi.partUuid, QStringLiteral("1111-AAAA"));
        QCOMPARE(efi.partType, QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b"));
        QCOMPARE(efi.parentId, nvme.id);
        QCOMPARE(efi.parentDevice, nvme.device);
        QCOMPARE(efi.id, QStringLiteral("partuuid:1111-aaaa@wwn:eui.0025381234567890"));

        const Drive &system = nvme.children.at(1);
        QCOMPARE(system.uuid, QStringLiteral("ABCD-1234"));
        QCOMPARE(system.id, QStringLiteral("uuid:abcd-1234@wwn:eui.0025381234567890"));
        QCOMPARE(system.mountPoints, QStringList({QStringLiteral("/"), QStringLiteral("/home")}));
        QCOMPARE(system.mountPoint, QStringLiteral("/"));
        QVERIFY(!system.mountPoints.isEmpty());
        QVERIFY(system.system);

        const Drive &data = nvme.children.at(2);
        QCOMPARE(data.mountPoints, QStringList({QStringLiteral("/mnt/data")}));
        QCOMPARE(data.mountPoint, QStringLiteral("/mnt/data"));

        const QVariantMap mapped = toMap(nvme);
        QCOMPARE(mapped.value(QStringLiteral("children")).toList().size(), 5);
        QCOMPARE(mapped.value(QStringLiteral("model")).toString(), QStringLiteral("990 PRO 1TB"));
        QCOMPARE(mapped.value(QStringLiteral("physical")).toBool(), true);

        const Drive &usb = devices.at(1);
        QVERIFY(usb.usb);
        QVERIFY(usb.removable);
        QCOMPARE(usb.transport, QStringLiteral("usb"));
        QVERIFY(usb.rotationalKnown);
        QVERIFY(usb.rotational); // ROTA=1 identifies a rotating disk in a USB enclosure
        QVERIFY(usb.children.first().usb);
        QVERIFY(usb.children.first().removable);

        const Drive &removableWithoutTransport = devices.at(2);
        QVERIFY(removableWithoutTransport.removable);
        QVERIFY(!removableWithoutTransport.usb); // RM alone is not proof of USB transport
        QVERIFY(!removableWithoutTransport.rotationalKnown); // do not guess SSD/HDD when ROTA is unavailable

        const QList<Drive> fallback = driveHierarchyFromLsblk(
            R"({"blockdevices":[{"name":"sdd","path":"/dev/sdd","type":"disk","size":"100"}]})");
        QCOMPARE(fallback.size(), 1);
        QCOMPARE(fallback.first().id, QStringLiteral("device:/dev/sdd"));
        QVERIFY(driveHierarchyFromLsblk(QByteArrayLiteral("not json")).isEmpty());
        QVERIFY(driveHierarchyFromLsblk(QByteArray(R"({"devices":[]})")).isEmpty());
    }

    void thumbnails()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
            QSKIP("pdftoppm/ffmpeg not installed");
        }
        // a real one-page PDF and a short video
        const QString pdf = d(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(pdf);
            QPainter p(&writer);
            p.fillRect(QRect(0, 0, 2000, 2000), Qt::darkCyan);
            p.drawText(QPoint(300, 600), QStringLiteral("Alveus"));
        }
        const QString video = d(QStringLiteral("clip.mp4"));
        QProcess::execute(QStringLiteral("ffmpeg"), {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-f"),
                                                     QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=duration=3:size=320x240:rate=10"),
                                                     QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video});
        QVERIFY(QFile::exists(video));

        AlveusCore core(desk(), store());
        QSignalSpy spy(&core, &AlveusCore::thumbnailReady);
        QVERIFY(core.thumbnail(pdf).isEmpty());   // not cached yet: on its way
        QVERIFY(core.thumbnail(video).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 20000);
        const QString pdfThumb = core.thumbnail(pdf);
        QVERIFY(pdfThumb.startsWith(QStringLiteral("file://") + m_tmp.path() + QStringLiteral("/cache/thumbnails/large/")));
        QImage img(QUrl(pdfThumb).toLocalFile());
        QVERIFY(!img.isNull());
        QVERIFY(img.width() <= 256 && img.height() <= 256);
        QCOMPARE(img.text(QStringLiteral("Thumb::URI")), QString::fromLatin1(QUrl::fromLocalFile(pdf).toEncoded()));
        QVERIFY(!core.thumbnail(video).isEmpty());
        // a changed file needs a new thumbnail
        age(pdf, 1);
        QVERIFY(core.thumbnail(pdf).isEmpty());

        // a document Dolphin already made a thumbnail for: we use it as is
        const QString doc = d(QStringLiteral("Лист.docx"));
        touch(doc, "not really a docx");
        QImage page(200, 256, QImage::Format_RGB32);
        page.fill(Qt::white);
        page.setText(QStringLiteral("Thumb::URI"), QString::fromLatin1(QUrl::fromLocalFile(doc).toEncoded()));
        page.setText(QStringLiteral("Thumb::MTime"), QString::number(QFileInfo(doc).lastModified().toSecsSinceEpoch()));
        const QString cached = Thumbnailer::cacheFile(doc, QStringLiteral("normal"));
        QDir().mkpath(QFileInfo(cached).absolutePath());
        QVERIFY(page.save(cached));
        core.rescanNow();
        bool offered = false;
        for (const QVariant &v : core.filesInFence(QString(), QString())) {
            if (v.toMap().value(QStringLiteral("name")) == QStringLiteral("Лист.docx")) {
                offered = v.toMap().value(QStringLiteral("canThumb")).toBool();
            }
        }
        QVERIFY(offered);
        QCOMPARE(core.thumbnail(doc), QUrl::fromLocalFile(cached).toString());
    }
};

QTEST_MAIN(CoreTest)
#include "coretest.moc"
