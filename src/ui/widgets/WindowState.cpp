#include "WindowState.h"

#include <QCoreApplication>
#include <QEvent>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QWidget>

namespace {

const QString kGeometryKey = QStringLiteral("/geometry");
const QString kSplitterKey = QStringLiteral("/splitter");

class StateKeeper : public QObject
{
public:
    StateKeeper(QWidget* window, const QString& key, QSplitter* splitter)
        : QObject(window)
        , m_window(window)
        , m_key(key)
        , m_splitter(splitter)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        // Hide covers accept, reject, minimize, close and quit alike, and
        // saveGeometry() reports the normal rectangle while minimized.
        if (event->type() == QEvent::Hide)
            store();
        return QObject::eventFilter(watched, event);
    }

private:
    void store() const
    {
        // QSettings syncs on destruction, so no explicit sync() is needed.
        QSettings settings(WindowState::filePath(), QSettings::IniFormat);
        settings.setValue(m_key + kGeometryKey, m_window->saveGeometry());
        if (m_splitter)
            settings.setValue(m_key + kSplitterKey, m_splitter->saveState());
    }

    QWidget* m_window;
    QString m_key;
    QSplitter* m_splitter;
};

}

QString WindowState::filePath()
{
    // StateLocation would nest the application name twice; this mirrors how
    // ConfigManager derives its own directory.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    const QString organization = QCoreApplication::organizationName();
    if (!organization.isEmpty())
        dir += QLatin1Char('/') + organization;
    return dir + QStringLiteral("/windowstate.ini");
}

bool WindowState::track(QWidget* window, const QString& id, QSplitter* splitter)
{
    const QString key = QStringLiteral("windows/") + id;
    QSettings settings(WindowState::filePath(), QSettings::IniFormat);

    // restoreGeometry() already pulls an off-screen window back into view.
    const QByteArray geometry = settings.value(key + kGeometryKey).toByteArray();
    const bool restored = !geometry.isEmpty() && window->restoreGeometry(geometry);

    if (splitter) {
        const QByteArray state = settings.value(key + kSplitterKey).toByteArray();
        if (!state.isEmpty())
            splitter->restoreState(state);
    }

    window->installEventFilter(new StateKeeper(window, key, splitter));
    return restored;
}
