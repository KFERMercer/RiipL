#pragma once

#include <QObject>

// What the tray reports while YOLO mode is on: the state of every request the
// shared pool carries, with the outcome of the last one held until new work or a
// manual stop replaces it.
class TranslationActivity : public QObject
{
    Q_OBJECT

public:
    enum class State {
        // Nothing has run yet, or the last outcome has been dropped.
        Ready,
        Busy,
        Done,
        Failure,
    };
    Q_ENUM(State)

    explicit TranslationActivity(QObject* parent = nullptr);

    State state() const { return m_state; }

public slots:
    // Drops the outcome of the last run; reports Busy while the pool still carries
    // requests.
    void markReady();

signals:
    void stateChanged(State state);

private:
    void setState(State state);
    void onBusyChanged(bool busy);
    void onRequestFailed();

    State m_state = State::Ready;
};
