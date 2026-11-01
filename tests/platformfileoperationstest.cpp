#include "platform/platformfactory.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using Alveus::FileOps::Control;
using Alveus::FileOps::Result;
using Alveus::Platform::IFileOperations;

class PlatformFileOperationsTest final : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<IFileOperations> m_ops = Alveus::Platform::createFileOperations();
    static bool writeFile(const QString &path, const QByteArray &data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
    static QByteArray readFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private Q_SLOTS:
    void copyRegularFile() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString src=temp.filePath("source.txt"), dst=temp.filePath("copy.txt");
        QVERIFY(writeFile(src,"alveus")); QCOMPARE(m_ops->treeSize(src),qint64(6));
        QVERIFY(m_ops->copyTree(src,dst)==Result::Done);
        QVERIFY(QFileInfo::exists(src)); QCOMPARE(readFile(dst),QByteArray("alveus"));
    }
    void copyDirectoryAndHiddenFiles() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString src=temp.filePath("source"), nested=QDir(src).filePath("nested");
        QVERIFY(QDir().mkpath(nested));
        QVERIFY(writeFile(QDir(src).filePath(".hidden"),"h"));
        QVERIFY(writeFile(QDir(nested).filePath("data.bin"),"payload"));
        const QString dst=temp.filePath("copy");
        QVERIFY(m_ops->copyTree(src,dst)==Result::Done);
        QCOMPARE(readFile(QDir(dst).filePath(".hidden")),QByteArray("h"));
        QCOMPARE(readFile(QDir(dst).filePath("nested/data.bin")),QByteArray("payload"));
    }
    void copyDoesNotOverwrite() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString src=temp.filePath("source.txt"), dst=temp.filePath("destination.txt");
        QVERIFY(writeFile(src,"source")); QVERIFY(writeFile(dst,"keep"));
        QVERIFY(m_ops->copyTree(src,dst)==Result::Failed);
        QCOMPARE(readFile(src),QByteArray("source")); QCOMPARE(readFile(dst),QByteArray("keep"));
    }
    void cancelledCopyLeavesSource() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString src=temp.filePath("source.txt"), dst=temp.filePath("destination.txt");
        QVERIFY(writeFile(src,QByteArray(1024,'x')));
        Control control; control.cancel=true;
        QVERIFY(m_ops->copyTree(src,dst,&control)==Result::Cancelled);
        QVERIFY(QFileInfo::exists(src)); QVERIFY(!QFileInfo::exists(dst));
    }
    void moveRegularFile() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString src=temp.filePath("source.txt"), dst=temp.filePath("moved.txt");
        QVERIFY(writeFile(src,"move me"));
        QVERIFY(m_ops->movePath(src,dst)==Result::Done);
        QVERIFY(!QFileInfo::exists(src)); QCOMPARE(readFile(dst),QByteArray("move me"));
    }
    void removeDirectoryTree() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString dir=temp.filePath("tree");
        QVERIFY(QDir().mkpath(QDir(dir).filePath("child")));
        QVERIFY(writeFile(QDir(dir).filePath("child/file.txt"),"x"));
        QVERIFY(m_ops->removeTree(dir)); QVERIFY(!QFileInfo::exists(dir));
    }
    void siblingPathsShareFilesystem() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString src=temp.filePath("source.txt"); QVERIFY(writeFile(src,"x"));
        QVERIFY(m_ops->sameFilesystem(src,temp.path()));
    }
#if defined(Q_OS_LINUX)
    void copySymlinkWithoutDereferencing() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString src=temp.filePath("source.txt"), link=temp.filePath("source-link");
        const QString dst=temp.filePath("copied-link");
        QVERIFY(writeFile(src,"target")); QVERIFY(QFile::link(src,link)); QVERIFY(QFileInfo(link).isSymLink());
        QVERIFY(m_ops->copyTree(link,dst)==Result::Done);
        QVERIFY(QFileInfo(dst).isSymLink());
        QCOMPARE(QFileInfo(dst).symLinkTarget(),QFileInfo(link).symLinkTarget());
    }
#endif
};
QTEST_GUILESS_MAIN(PlatformFileOperationsTest)
#include "platformfileoperationstest.moc"
