#pragma once

#include <QObject>
#include <Qt>
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
    // Second stage: after `deepTimeoutMs` more without input the session enters a
    // deep sleep — the starfield stops, the panel blanks, music pauses, and the
    // machine is asked to suspend. QML binds the pure-black idle screen (no
    // animation) to this; main.cpp wires the power side effects.
    Q_PROPERTY(bool deepIdle READ deepIdle NOTIFY deepIdleChanged)
    Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY timeoutMsChanged)
    Q_PROPERTY(int deepTimeoutMs READ deepTimeoutMs WRITE setDeepTimeoutMs NOTIFY deepTimeoutMsChanged)

public:
    explicit IdleMonitor(QObject *parent = nullptr);

    bool idle() const { return m_idle; }
    bool deepIdle() const { return m_deepIdle; }
    int timeoutMs() const { return m_timeoutMs; }
    int deepTimeoutMs() const { return m_deepTimeoutMs; }
    void setTimeoutMs(int ms);
    void setDeepTimeoutMs(int ms);

    // Force-clear the idle state (e.g. the idle overlay swallowing the first tap).
    Q_INVOKABLE void wake();

    bool suppressed() const { return m_suppressed; }
    // Suppress idle DETECTION (the starfield screensaver + display-sleep-on-idle)
    // while a routine is engaged: a focused user sitting idle is working, not
    // away, so we never blank on them. Wired to RoutineManager::active in
    // main.cpp. Auto-resume of an idle-paused timer is unaffected — that runs off
    // resumeHint (keyboard/focus events), not this idle timer.
    void setSuppressed(bool suppressed);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void idleChanged();
    void deepIdleChanged();
    void timeoutMsChanged();
    void deepTimeoutMsChanged();
    // Emitted on every user input event (cheap; no allocation).
    void activity();
    // Emitted only on a keyboard press/release or a window-focus change —
    // deliberately NOT on mouse movement. Drives the idle-pause auto-resume
    // (Task 4): coming back to the keyboard / refocusing resumes the timer, but
    // a passing mouse jitter does not.
    void resumeHint();

private:
    void noteActivity();
    void goIdle();
    void goDeepIdle();
    // Clear both idle stages (and stop the deep timer); emits the changes.
    void clearIdleState();

    bool m_idle = false;
    bool m_deepIdle = false;
    bool m_suppressed = false;
    int m_timeoutMs = 5 * 60 * 1000;     // 5 minutes to the starfield screensaver
    int m_deepTimeoutMs = 2 * 60 * 1000; // 2 more minutes to deep sleep
    QTimer m_timer;
    QTimer m_deepTimer;
};
