#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
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
    // deep sleep — the starfield stops, the panel blanks (DPMS off), music
    // pauses, and the machine is asked to suspend. QML binds the pure-black idle
    // screen (no animation) to this; main.cpp wires the power side effects.
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

    // Debounce a manual "Sleep" press. The compositor wakes DPMS on the very next
    // input, so the click-release or a stray cursor twitch right after the press
    // would flick the freshly-blanked panel back on. For a short grace window after
    // this is called, mouse input is swallowed and the panel is re-blanked
    // (reblankRequested) instead of waking; a deliberate keypress/tap ends the
    // window early and wakes normally. Wired to RoutineManager::displaySleepRequested.
    void beginSleepGrace();

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
    // Third stage: emitted once after a full 30 minutes of idleness on the home
    // screen, on top of the deep-sleep blank. main.cpp wires it to the screen
    // lock so a machine left untended long-term ends up locked, not merely dark.
    // Like the other stages it never fires while suppressed (a routine engaged).
    void lockIdle();
    // Emitted during the manual-sleep grace window when input arrives that the
    // compositor will have used to wake DPMS; main.cpp re-issues the panel blank.
    void reblankRequested();

private:
    void noteActivity();
    // Re-blank the panel during the grace window, throttled so a continuously
    // moving cursor doesn't spawn a storm of kscreen-doctor/xset processes.
    void requestReblank();
    void endSleepGrace();
    void goIdle();
    void goDeepIdle();
    void goLockIdle();
    // Clear all idle stages (and stop the deep + lock timers); emits the changes.
    void clearIdleState();

    bool m_idle = false;
    bool m_deepIdle = false;
    bool m_suppressed = false;
    // Last cursor position a MouseMove was *counted* at. Sub-threshold jitter
    // (a Magic Mouse trembling on the desk, a bumped table) is ignored so it
    // can't perpetually re-arm the idle countdown — otherwise the screensaver /
    // display-sleep never fires on a machine that's actually been left alone.
    // Mirrors how desktop screensavers treat tiny pointer noise as "no input".
    QPointF m_lastCountedMousePos;
    bool m_haveLastMousePos = false;
    // Manhattan-distance the cursor must travel before a move counts as activity.
    static constexpr qreal kMouseJitterPx = 12.0;
    int m_timeoutMs = 5 * 60 * 1000;       // 5 minutes to the starfield screensaver
    int m_deepTimeoutMs = 2 * 60 * 1000;   // 2 more minutes to deep sleep (panel off)
    // 23 more minutes after deep sleep → 30 minutes total idle → lock the screen.
    int m_lockTimeoutMs = 23 * 60 * 1000;
    QTimer m_timer;
    QTimer m_deepTimer;
    QTimer m_lockTimer;
    // Manual-sleep debounce: true for kSleepGraceMs after the Sleep button blanks
    // the panel. m_reblankThrottle rate-limits the re-blank to one per ~400ms.
    bool m_inSleepGrace = false;
    QTimer m_sleepGraceTimer;
    QElapsedTimer m_reblankThrottle;
    static constexpr int kSleepGraceMs = 4000;
    static constexpr qint64 kReblankThrottleMs = 400;
};
