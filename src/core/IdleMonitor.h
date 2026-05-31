#pragma once

#include <QObject>
#include <QTimer>

class QEvent;

// Session-wide idle detector. Installs an application event filter so it sees
// every mouse/keyboard/touch event delivered to FocusOS, and flips `idle` true
// after `timeoutMs` without input. QML binds the pitch-black starfield idle
// screen to `idle`; the `activity()` signal also drives the unlock panel's
// inactivity auto-lock (see RoutineManager::notifyActivity).
class IdleMonitor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)
    Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY timeoutMsChanged)

public:
    explicit IdleMonitor(QObject *parent = nullptr);

    bool idle() const { return m_idle; }
    int timeoutMs() const { return m_timeoutMs; }
    void setTimeoutMs(int ms);

    // Force-clear the idle state (e.g. the idle overlay swallowing the first tap).
    Q_INVOKABLE void wake();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void idleChanged();
    void timeoutMsChanged();
    // Emitted on every user input event (cheap; no allocation).
    void activity();

private:
    void noteActivity();
    void goIdle();

    bool m_idle = false;
    int m_timeoutMs = 5 * 60 * 1000; // 5 minutes
    QTimer m_timer;
};
