/*
 * Alveus — desktop fences for KDE Plasma 6
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Core: one shared instance for every desktop (screen × activity) of plasmashell.
 *
 * Fences are virtual: files stay in the Desktop folder, Alveus only decides
 * which fence shows them. Only choices made by hand are stored; everything
 * else follows the fences' sorting rules, evaluated live.
 */

#pragma once

#include "fileops.h"
#include "rules.h"

#include <QDateTime>
#include <QDir>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <functional>
#include <memory>

class QThread;

class Thumbnailer;
class DriveWatcher;
class Integrations;
class RunnerService;

class AlveusCore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Core)
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    // Bumped on every change; QML re-reads lists when it changes.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString desktopPath READ desktopPath CONSTANT)
    Q_PROPERTY(int fenceCount READ fenceCount NOTIFY changed)
    Q_PROPERTY(bool showWelcome READ showWelcome NOTIFY changed)
    Q_PROPERTY(bool hidden READ hidden WRITE setHidden NOTIFY hiddenChanged)
    Q_PROPERTY(bool privacy READ privacy NOTIFY privacyChanged)
    Q_PROPERTY(bool privacyManual READ privacyManual WRITE setPrivacyManual NOTIFY privacyChanged)
    Q_PROPERTY(bool screenShared READ screenShared NOTIFY privacyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(QVariantList phones READ phones NOTIFY phonesChanged)
    Q_PROPERTY(int staleCount READ staleCount NOTIFY changed)
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    // Copy/move running in the background: {active, kind, name, index, count,
    // progress (0..1, -1 = unknown), queued, elapsed (ms)}
    Q_PROPERTY(QVariantMap fileJob READ fileJob NOTIFY fileJobChanged)

public:
    explicit AlveusCore(const QString &desktopPath = QString(), const QString &storePath = QString(),
                        QObject *parent = nullptr);
    ~AlveusCore() override;

    // The same instance is handed to every QML engine in the process.
    static AlveusCore *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    QString version() const;
    int revision() const { return m_revision; }
    QString desktopPath() const { return m_desktopPath; }
    int fenceCount() const { return m_fences.size(); }
    bool showWelcome() const { return m_fences.isEmpty() && !m_welcomeDismissed; }
    bool hidden() const { return m_hidden; }
    void setHidden(bool hidden);
    bool privacy() const { return m_privacyManual || (setting(QStringLiteral("autoPrivacy")).toBool() && m_screenShared); }
    bool privacyManual() const { return m_privacyManual; }
    void setPrivacyManual(bool on);
    bool screenShared() const { return m_screenShared; }
    bool canUndo() const { return !m_undo.isEmpty() && !m_undoRunning; }
    QString undoText() const;
    bool canPaste() const;
    QVariantList phones() const { return m_phones; }
    int staleCount() const;
    QVariantList drives() const;

    // ---- desktops (one per screen and activity) --------------------------
    Q_INVOKABLE void registerDesktop(int screen, const QString &activity);
    Q_INVOKABLE void unregisterDesktop(int screen, const QString &activity);
    Q_INVOKABLE QVariantList screens(const QString &activity) const;
    Q_INVOKABLE int primaryScreen(const QString &activity) const;
    // Screen that shows things saved for `screen` (the main one if it's gone)
    Q_INVOKABLE int hostScreen(int screen, const QString &activity) const;
    Q_INVOKABLE int pageCount(int screen, const QString &activity) const;
    Q_INVOKABLE void setPageCount(int screen, const QString &activity, int count);

    // ---- fences ------------------------------------------------------------
    // Geometry is stored as fractions (0..1) of the free desktop area.
    Q_INVOKABLE QVariantList fences(int screen, const QString &activity, int page) const;
    Q_INVOKABLE QVariantMap fence(const QString &id) const;
    Q_INVOKABLE QString createFence(int screen, const QString &activity, int page, qreal x, qreal y, qreal w, qreal h,
                                    const QString &title, const QString &rules);
    Q_INVOKABLE QString createPortal(int screen, const QString &activity, int page, qreal x, qreal y, qreal w, qreal h,
                                     const QString &title, const QString &path);
    Q_INVOKABLE void updateFence(const QString &id, const QVariantMap &patch);
    Q_INVOKABLE void removeFence(const QString &id);
    Q_INVOKABLE void mergeFences(const QString &targetId, const QString &sourceId); // tabs
    Q_INVOKABLE void detachTab(const QString &id);
    Q_INVOKABLE void activateTab(const QString &id);
    Q_INVOKABLE QString presetRules(const QString &preset) const;
    Q_INVOKABLE QStringList ruleErrors(const QString &rules) const;
    Q_INVOKABLE void dismissWelcome();
    Q_INVOKABLE void requestFence(const QString &id); // e.g. from KRunner: show it

    // ---- files on the desktop ---------------------------------------------
    // fenceId "" = loose on the desktop. Visible fences depend on the activity.
    Q_INVOKABLE QVariantList filesInFence(const QString &fenceId, const QString &activity) const;
    Q_INVOKABLE QVariantList looseFiles(int screen, const QString &activity) const;
    Q_INVOKABLE QString fenceOfFile(const QString &name, const QString &activity) const;
    Q_INVOKABLE void assignFiles(const QStringList &names, const QString &fenceId);
    Q_INVOKABLE void setFilePositions(const QVariantList &items); // [{name, screen, page, col, row}]
    Q_INVOKABLE void clearPositions(int screen, int page);
    Q_INVOKABLE bool isDesktopUrl(const QUrl &url) const;
    Q_INVOKABLE QString fileNameFromUrl(const QUrl &url) const;
    Q_INVOKABLE QStringList toUrls(const QStringList &paths) const;

    // ---- any folder (portals) ----------------------------------------------
    Q_INVOKABLE QVariantList listFolder(const QString &path) const;
    Q_INVOKABLE void watchFolder(const QString &path);
    Q_INVOKABLE QString parentFolder(const QString &path) const;
    Q_INVOKABLE QString folderName(const QString &path) const;
    Q_INVOKABLE bool folderExists(const QString &path) const;

    // ---- file operations (full paths) --------------------------------------
    // Into the desktop (and a fence) or into a folder. Returns new paths.
    Q_INVOKABLE QStringList importToDesktop(const QList<QUrl> &urls, bool copy, const QString &fenceId);
    Q_INVOKABLE QStringList importToFolder(const QList<QUrl> &urls, bool copy, const QString &folder);
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void showInFolder(const QString &path);
    Q_INVOKABLE void showProperties(const QString &path);
    Q_INVOKABLE void openTerminal(const QString &folder);
    Q_INVOKABLE bool renamePath(const QString &path, const QString &newName);
    Q_INVOKABLE int trashPaths(const QStringList &paths);
    Q_INVOKABLE QString createFolder(const QString &inFolder, const QString &baseName);
    Q_INVOKABLE QString createTextFile(const QString &inFolder, const QString &baseName);
    Q_INVOKABLE void copyToClipboard(const QStringList &paths, bool cut);
    Q_INVOKABLE QStringList paste(const QString &folder, const QString &fenceId);

    // ---- undo ----------------------------------------------------------------
    Q_INVOKABLE bool undo();

    // ---- background file operations -------------------------------------------
    QVariantMap fileJob() const { return m_fileJob; }
    Q_INVOKABLE void cancelFileJobs();
    bool fileJobsBusy() const { return m_jobThread || !m_jobs.isEmpty(); }
    // For tests: process events until every file job finished.
    bool waitForFileJobs(int timeoutMs = 30000);

    // ---- snapshots of the layout -------------------------------------------
    Q_INVOKABLE QVariantList snapshots() const;
    Q_INVOKABLE QString saveSnapshot(const QString &name);
    Q_INVOKABLE void restoreSnapshot(const QString &id);
    Q_INVOKABLE void deleteSnapshot(const QString &id);

    // ---- tidy-up helper ------------------------------------------------------
    Q_INVOKABLE QVariantList staleFiles(int days) const;
    Q_INVOKABLE QString archiveFiles(const QStringList &names, const QString &archiveFolderName);
    Q_INVOKABLE void snoozeCleanup(int days);
    Q_INVOKABLE bool cleanupDue() const;

    // ---- settings ------------------------------------------------------------
    Q_INVOKABLE QVariant setting(const QString &key) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

    // ---- integrations --------------------------------------------------------
    Q_INVOKABLE QString thumbnail(const QString &path); // "" while not ready
    Q_INVOKABLE void refreshPhones();
    Q_INVOKABLE void sendToPhone(const QString &deviceId, const QStringList &paths);
    Q_INVOKABLE QString phoneFolder() const;       // where KDE Connect saves received files
    Q_INVOKABLE QString screenshotFolder() const;  // where Spectacle saves screenshots
    Q_INVOKABLE QString downloadsFolder() const;
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE void refreshDrives();
    Q_INVOKABLE void mountDrive(const QString &device);
    Q_INVOKABLE void unmountDrive(const QString &device);

    // For tests
    Q_INVOKABLE void rescanNow();
    Q_INVOKABLE void saveNow();
    QVariantList runnerMatches(const QString &query) const;
    void runnerRun(const QString &id);

Q_SIGNALS:
    void changed();
    void fencesChanged();
    // activity + fence IDs whose file lists changed; an empty ID denotes loose desktop files.
    void fileModelsChanged(const QString &activity, const QStringList &fenceIds);
    void desktopPositionsChanged();
    void desktopTopologyChanged();
    void folderContentsChanged(const QString &path);
    void hiddenChanged();
    void privacyChanged();
    void undoChanged();
    void clipboardChanged();
    void phonesChanged();
    void thumbnailReady(const QString &path);
    void fenceRequested(const QString &id);
    void message(const QString &kind, const QString &detail);
    void drivesChanged();
    void fileJobChanged();

private:
    // One step of a background job. Copy/Move come from drops, paste and
    // tidy-up; Trash/Restore only from undo.
    struct FileTask {
        enum Op { Copy, Move, Trash, Restore };
        Op op = Copy;
        QString from;
        QString to;
    };
    using TaskResults = QList<Alveus::FileOps::Result>;
    struct FileJob {
        QString kind; // copy | move | archive | undo
        QList<FileTask> tasks;
        std::function<void(const TaskResults &)> finish; // on the GUI thread
    };
    struct Fence {
        QString id;
        QString title;
        int screen = 0;
        QString activity; // "" = every activity
        int page = 0;
        qreal x = 0, y = 0, w = 0.2, h = 0.25;
        // where it sits while its own monitor is unplugged
        bool hasOrphanGeometry = false;
        qreal ox = 0, oy = 0, ow = 0.2, oh = 0.25;
        bool collapsed = false;
        QString rules;
        QString portal;    // folder shown by a portal fence
        QString group;     // tab group ("" = alone)
        bool activeTab = true;
        QString color;     // "" = theme colour
        qreal opacity = 0.55;
        bool blur = true;
        QString view = QStringLiteral("grid"); // grid | honeycomb | list
        int iconSize = 0;  // 0 = default
        qint64 created = 0;

        Alveus::Rules parsed;
    };
    struct Placement {
        QString fence;      // manual choice; "" = keep on the desktop
        bool manual = false;
        bool hasPos = false;
        int screen = 0, page = 0, col = 0, row = 0;
    };
    struct Entry {
        QString name;
        QString path;
        QString label;
        QString icon;
        QString fallbackIcon;
        QString image;
        QString mime;
        bool isDir = false;
        qint64 size = 0;
        QDateTime modified;
        QDateTime accessed;
        QString origin;
    };
    struct FileModelCache {
        QHash<QString, QVariantList> byFence;
        QHash<QString, QString> fenceByName;
    };

    // state
    void load();
    QJsonObject stateJson() const;
    void applyState(const QJsonObject &root, bool includeSettings);
    void scheduleSave();
    void save();
    void scheduleRescan();
    void rescan();
    void bump();
    void bumpFences(bool fileMembershipChanged = false);
    void activateTabInternal(const QString &id, bool fileMembershipChanged);
    void bumpFileModels(const QSet<QString> &changedNames);
    void bumpAllFileModels();
    void bumpDesktopPositions();
    void bumpDesktopTopology();
    const FileModelCache &fileModelCacheFor(const QString &activity) const;
    void refreshFileModelCaches(bool rebuildAll, const QSet<QString> &changedNames = {});
    void invalidateFolder(const QString &path);
    void invalidateAllFolders();

    // undo
    void pushUndo(const QString &text, const QJsonObject &extra = QJsonObject());

    // helpers
    int indexOfFence(const QString &id) const;
    const Fence *findFence(const QString &id) const;
    bool fenceVisibleIn(const Fence &f, const QString &activity) const;
    QString ruleFenceFor(const Entry &e, const QString &activity) const;
    QString placementFor(const Entry &e, const QString &activity) const;
    Entry makeEntry(const QString &dir, const QString &name) const;
    QVariantMap toMap(const Fence &f, const QString &activity) const;
    QVariantMap toMap(const Entry &e) const;
    QString uniqueName(const QString &dir, const QString &name) const;
    QStringList importInto(const QList<QUrl> &urls, bool copy, const QString &dir, QJsonArray *moved,
                           QList<FileTask> *slow);
    void runFileTasks(const QString &kind, const QList<FileTask> &tasks, const QJsonObject &before,
                      const QJsonArray &doneOps);
    void queueFileJob(FileJob job);
    void startNextFileJob();
    void updateFileJob();
    static Alveus::FileOps::Result runTask(const FileTask &task, Alveus::FileOps::Control *control);
    void reportFailures(const QString &kind, const QStringList &names);
    bool isReserved(const QString &path) const { return m_reserved.contains(QDir::cleanPath(path)); }
    void refreshChangedFiles();
    QString key(int screen, const QString &activity) const;
    void fenceNormalizeGeometry(Fence &f);

    QString m_desktopPath;
    QString m_storePath;
    QList<Fence> m_fences;
    QHash<QString, Placement> m_place;
    QList<Entry> m_files;
    mutable QHash<QString, FileModelCache> m_fileModelCache;
    QHash<QString, QList<Entry>> m_folders; // portals and folders browsed in them
    QHash<QString, int> m_desktops;  // "activity|screen" -> refcount
    QHash<QString, int> m_pages;     // "activity|screen" -> page count
    QList<QJsonObject> m_snapshots;
    QList<QJsonObject> m_undo;
    QJsonObject m_settings;
    bool m_storeWritable = true; // false when the existing store cannot be safely replaced
    bool m_saveBlockedReported = false;
    bool m_undoRunning = false;
    QList<FileJob> m_jobs;         // waiting
    FileJob m_runningJob;
    QThread *m_jobThread = nullptr;
    std::shared_ptr<Alveus::FileOps::Control> m_jobControl;
    std::shared_ptr<TaskResults> m_jobResults;
    qint64 m_jobStarted = 0;
    QTimer m_jobTimer;             // refreshes fileJob while a job runs
    QVariantMap m_fileJob;
    QSet<QString> m_reserved;      // destinations of copies still running
    QSet<QString> m_changedFiles;  // desktop files written to since the last refresh
    QTimer m_fileChangeTimer;
    bool m_welcomeDismissed = false;
    bool m_hidden = false;
    bool m_privacyManual = false;
    bool m_screenShared = false;
    qint64 m_cleanupSnoozedUntil = 0;
    int m_revision = 0;
    QVariantList m_phones;

    QFileSystemWatcher m_watcher;
    QTimer m_scanTimer;
    QTimer m_saveTimer;
    QTimer m_clockTimer; // re-evaluates time rules
    Thumbnailer *m_thumbs = nullptr;
    Integrations *m_integrations = nullptr;
    RunnerService *m_runner = nullptr;
    DriveWatcher *m_drives = nullptr;

    friend class RunnerService;
};
