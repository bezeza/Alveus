#include "mockplasmoid.h"

MockApplet *MockApplet::instance()
{
    static MockApplet *a = new MockApplet;
    return a;
}
void MockApplet::setEditMode(bool value)
{
    if (m_editMode == value) {
        return;
    }
    m_editMode = value;
    Q_EMIT editModeChanged();
}

int MockApplet::screen() const { return qEnvironmentVariableIntValue("ALVEUS_MOCK_SCREEN"); }
QString MockApplet::activity() const { return qEnvironmentVariable("ALVEUS_MOCK_ACTIVITY"); }
QString MockApplet::activityName() const { return qEnvironmentVariable("ALVEUS_MOCK_ACTIVITY_NAME"); }
// a 44 px panel at the bottom of a 1600x900 screen, like a default Plasma setup
QRectF MockApplet::availableScreenRect() const { return QRectF(0, 0, 1600, 856); }
QRectF MockApplet::screenGeometry() const { return QRectF(0, 0, 1600, 900); }

QQmlListProperty<QObject> MockApplet::contextualActions()
{
    return QQmlListProperty<QObject>(
        this, &m_actions,
        [](QQmlListProperty<QObject> *l, QObject *o) {
            static_cast<QList<QObject *> *>(l->data)->append(o);
            Q_EMIT static_cast<MockApplet *>(l->object)->contextualActionsChanged();
        },
        [](QQmlListProperty<QObject> *l) { return static_cast<QList<QObject *> *>(l->data)->size(); },
        [](QQmlListProperty<QObject> *l, qsizetype i) { return static_cast<QList<QObject *> *>(l->data)->at(i); },
        [](QQmlListProperty<QObject> *l) { static_cast<QList<QObject *> *>(l->data)->clear(); });
}
