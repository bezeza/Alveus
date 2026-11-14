// Renders a QML scene offscreen, optionally plays mouse actions, saves a screenshot.
// usage: uiharness <scene.qml> <out.png> [delay-ms] [width] [height] [step]
// ALVEUS_ACTIONS="drag:x1,y1,x2,y2;dblclick:x,y;rclick:x,y;click:x,y;wait:ms"
// Import paths come from QML_IMPORT_PATH. Provides i18n()/i18nc() like KDE.
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQuickItem>
#include <QPalette>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QStringList>
#include <QTest>
#include <QTimer>

class I18n : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE QString i18n(const QString &s, const QVariant &a = {}, const QVariant &b = {}, const QVariant &c = {}) const
    {
        QString out = s;
        int n = 1;
        for (const QVariant &v : {a, b, c}) {
            if (v.isValid()) {
                out.replace(QStringLiteral("%") + QString::number(n), v.toString());
            }
            ++n;
        }
        return out;
    }
    Q_INVOKABLE QString i18nc(const QString &, const QString &s, const QVariant &a = {}, const QVariant &b = {}) const
    {
        return i18n(s, a, b);
    }
    Q_INVOKABLE QString i18np(const QString &one, const QString &many, const QVariant &n, const QVariant &b = {}) const
    {
        return i18n(n.toInt() == 1 ? one : many, n, b);
    }
};

static QList<int> nums(const QString &s)
{
    QList<int> out;
    for (const QString &p : s.split(QLatin1Char(','))) {
        out << p.trimmed().toInt();
    }
    return out;
}

static bool isCriticalQmlWarning(const QQmlError &error)
{
    const QString message = error.description().toLower();
    return message.contains(QStringLiteral("referenceerror"))
        || message.contains(QStringLiteral("typeerror"))
        || message.contains(QStringLiteral("binding loop"))
        || message.contains(QStringLiteral("unable to assign"))
        || message.contains(QStringLiteral("cannot assign"))
        || message.contains(QStringLiteral("is not a type"))
        || message.contains(QStringLiteral("is not installed"))
        || message.contains(QStringLiteral("cannot load library"));
}

static void play(QQuickView *view, const QString &script)
{
    for (const QString &raw : script.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString cmd = raw.section(QLatin1Char(':'), 0, 0).trimmed();
        const QList<int> a = nums(raw.section(QLatin1Char(':'), 1));
        if (cmd == QLatin1String("wait")) {
            QTest::qWait(a.value(0));
        } else if (cmd == QLatin1String("drag") && a.size() == 4) {
            const QPoint p1(a[0], a[1]), p2(a[2], a[3]);
            QTest::mousePress(view, Qt::LeftButton, {}, p1);
            for (int i = 1; i <= 12; ++i) {
                QTest::mouseMove(view, p1 + (p2 - p1) * i / 12);
                QTest::qWait(15);
            }
            QTest::mouseRelease(view, Qt::LeftButton, {}, p2);
        } else if (cmd == QLatin1String("press") && a.size() == 4) {
            // drag without releasing (to show the box while drawing)
            const QPoint p1(a[0], a[1]), p2(a[2], a[3]);
            QTest::mousePress(view, Qt::LeftButton, {}, p1);
            for (int i = 1; i <= 12; ++i) {
                QTest::mouseMove(view, p1 + (p2 - p1) * i / 12);
                QTest::qWait(15);
            }
        } else if (cmd == QLatin1String("click") && a.size() == 2) {
            QTest::mouseClick(view, Qt::LeftButton, {}, QPoint(a[0], a[1]));
        } else if (cmd == QLatin1String("rclick") && a.size() == 2) {
            QTest::mouseClick(view, Qt::RightButton, {}, QPoint(a[0], a[1]));
        } else if (cmd == QLatin1String("dblclick") && a.size() == 2) {
            QTest::mouseDClick(view, Qt::LeftButton, {}, QPoint(a[0], a[1]));
        }
        QTest::qWait(120);
    }
}

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, QColor("#202326"));
    palette.setColor(QPalette::WindowText, QColor("#f2f4f8"));
    palette.setColor(QPalette::Base, QColor("#1b1f24"));
    palette.setColor(QPalette::AlternateBase, QColor("#292e34"));
    palette.setColor(QPalette::Text, QColor("#f2f4f8"));
    palette.setColor(QPalette::Button, QColor("#30363d"));
    palette.setColor(QPalette::ButtonText, QColor("#f2f4f8"));
    palette.setColor(QPalette::Highlight, QColor("#3daee9"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipBase, QColor("#202326"));
    palette.setColor(QPalette::ToolTipText, QColor("#f2f4f8"));
    app.setPalette(palette);
    if (argc < 3) {
        qWarning("usage: uiharness scene.qml out.png [delay] [w] [h] [step]");
        return 2;
    }
    const int delay = argc > 3 ? atoi(argv[3]) : 800;
    const int w = argc > 4 ? atoi(argv[4]) : 1600;
    const int h = argc > 5 ? atoi(argv[5]) : 900;

    QQuickView view;
    I18n i18n;
    view.engine()->rootContext()->setContextObject(&i18n);
    QStringList criticalWarnings;
    QObject::connect(view.engine(), &QQmlEngine::warnings, [&criticalWarnings](const QList<QQmlError> &list) {
        for (const QQmlError &e : list) {
            fprintf(stderr, "QML: %s\n", qPrintable(e.toString()));
            if (isCriticalQmlWarning(e)) {
                criticalWarnings << e.toString();
            }
        }
    });
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(w, h);
    view.setSource(QUrl::fromLocalFile(QDir::current().absoluteFilePath(QString::fromLocal8Bit(argv[1]))));
    if (view.status() != QQuickView::Ready) {
        for (const QQmlError &e : view.errors()) {
            fprintf(stderr, "ERROR: %s\n", qPrintable(e.toString()));
        }
        return 1;
    }
    view.show();
    const QString out = QString::fromLocal8Bit(argv[2]);
    const QString actions = qEnvironmentVariable("ALVEUS_ACTIONS");
    QTimer::singleShot(delay, [&]() {
        play(&view, actions);
        QTest::qWait(300);
        const QVariant prepared = view.rootObject()->property("prepared");
        const QString step = argc > 6 ? QString::fromLocal8Bit(argv[6]) : QStringLiteral("welcome");
        if (step == QLatin1String("blanktitle")) {
            const QString id = view.rootObject()->property("blankFenceId").toString();
            const QString objectName = QStringLiteral("fence-title-") + id;
            QObject *loader = view.rootObject()->findChild<QObject *>(QStringLiteral("desktopLoader"));
            QObject *desktop = loader ? loader->property("item").value<QObject *>() : nullptr;
            QQuickItem *title = desktop ? desktop->findChild<QQuickItem *>(objectName) : nullptr;
            if (!title) {
                fprintf(stderr, "ERROR: could not find the blank fence title label: %s\n", qPrintable(objectName));
                app.exit(1);
                return;
            }
            if (title->property("text").toString() != QString()) {
                fprintf(stderr, "ERROR: an empty fence title was replaced with visible text: %s\n",
                        qPrintable(title->property("text").toString()));
                app.exit(1);
                return;
            }
        }
        if (prepared.isValid() && !prepared.toBool()) {
            fprintf(stderr, "ERROR: the QML scene did not finish preparing; check nested Loader errors above.\n");
            app.exit(1);
            return;
        }
        int exitCode = 0;
        const QImage screenshot = view.grabWindow();
        if (screenshot.isNull() || !screenshot.save(out)) {
            fprintf(stderr, "ERROR: could not capture or save screenshot: %s\n", qPrintable(out));
            exitCode = 1;
        }
        if (!criticalWarnings.isEmpty()) {
            for (const QString &warning : criticalWarnings) {
                qWarning("critical QML warning: %s", qPrintable(warning));
            }
            exitCode = 1;
        }
        app.exit(exitCode);
    });
    return app.exec();
}

#include "uiharness.moc"
