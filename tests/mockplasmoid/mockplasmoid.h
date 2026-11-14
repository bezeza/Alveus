// Stand-in for org.kde.plasma.plasmoid: the attached "Plasmoid" object and
// ContainmentItem, enough to render an Alveus desktop outside Plasma.
#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QQmlListProperty>
#include <QRectF>

class MockApplet : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int screen READ screen CONSTANT)
    Q_PROPERTY(QString activity READ activity CONSTANT)
    Q_PROPERTY(QString activityName READ activityName CONSTANT)
    Q_PROPERTY(QRectF availableScreenRect READ availableScreenRect CONSTANT)
    Q_PROPERTY(QRectF screenGeometry READ screenGeometry CONSTANT)
    Q_PROPERTY(QQmlListProperty<QObject> contextualActions READ contextualActions NOTIFY contextualActionsChanged)
    Q_PROPERTY(bool immutable READ immutable CONSTANT)
    Q_PROPERTY(bool isContainment READ isContainment CONSTANT)
    Q_PROPERTY(QObject *containment READ containment CONSTANT)
    Q_PROPERTY(QObject *corona READ corona CONSTANT)
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
public:
    static MockApplet *instance();
    int screen() const;
    QString activity() const;
    QString activityName() const;
    QRectF availableScreenRect() const;
    QRectF screenGeometry() const;
    bool immutable() const { return false; }
    bool isContainment() const { return true; }
    QObject *containment() { return this; }
    QObject *corona() { return this; }
    bool editMode() const { return m_editMode; }
    void setEditMode(bool value);
    QQmlListProperty<QObject> contextualActions();
    QList<QObject *> m_actions;
Q_SIGNALS:
    void contextualActionsChanged();
    void editModeChanged();

private:
    bool m_editMode = false;
};

class PlasmoidAttachedType : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Plasmoid)
    QML_UNCREATABLE("attached only")
    QML_ATTACHED(MockApplet)
public:
    static MockApplet *qmlAttachedProperties(QObject *) { return MockApplet::instance(); }
};
