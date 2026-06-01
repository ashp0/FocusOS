#include "core/IdleMonitor.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>

IdleMonitor::IdleMonitor(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(m_timeoutMs);
    connect(&m_timer, &QTimer::timeout, this, &IdleMonitor::goIdle);

    m_deepTimer.setSingleShot(true);
    m_deepTimer.setInterval(m_deepTimeoutMs);
    connect(&m_deepTimer, &QTimer::timeout, this, &IdleMonitor::goDeepIdle);

    if (qApp) {
        qApp->installEventFilter(this);
    }

    // Idle is application-wide but scoped to FocusOS having the focus: input to
    // ANY FocusOS window (the shell, the Settings modal, the overlay) counts as
    // activity, but we never go idle while the user is focused in a *different*
    // application's window — they may be working there, not idle. When FocusOS
    // regains focus the idle countdown restarts (and an idle pause resumes).
    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, &IdleMonitor::onApplicationStateChanged);
    if (focusOsIsActive()) {
        m_timer.start();
    }
}

bool IdleMonitor::focusOsIsActive() const
{
    // No QGuiApplication (e.g. headless test harness) → treat as active so the
    // countdown still runs.
    return !qApp || QGuiApplication::applicationState() == Qt::ApplicationActive;
}

void IdleMonitor::onApplicationStateChanged(Qt::ApplicationState state)
{
    if (state == Qt::ApplicationActive) {
        // Focus is back within FocusOS. Clear any idle state, restart the
        // countdown, and resume an idle-paused routine ("focus back in our
        // window" is the resume trigger from Task 4).
        clearIdleState();
        if (!m_suppressed) {
            m_timer.start();
        }
        emit resumeHint();
    } else {
        // The user moved to another application — they are not "idle" in the
        // screensaver sense, so suppress the idle screen and stop the countdown.
        clearIdleState();
        m_timer.stop();
    }
}

void IdleMonitor::setTimeoutMs(int ms)
{
    if (ms <= 0 || ms == m_timeoutMs) {
        return;
    }
    m_timeoutMs = ms;
    m_timer.setInterval(ms);
    m_timer.start();
    emit timeoutMsChanged();
}

void IdleMonitor::setDeepTimeoutMs(int ms)
{
    if (ms <= 0 || ms == m_deepTimeoutMs) {
        return;
    }
    m_deepTimeoutMs = ms;
    m_deepTimer.setInterval(ms);
    emit deepTimeoutMsChanged();
}

void IdleMonitor::wake()
{
    noteActivity();
}

void IdleMonitor::setSuppressed(bool suppressed)
{
    if (m_suppressed == suppressed) {
        return;
    }
    m_suppressed = suppressed;
    if (m_suppressed) {
        // Routine engaged: stop the countdown and clear any idle state so the
        // starfield never appears and display-sleep-on-idle never fires.
        m_timer.stop();
        clearIdleState();
    } else if (focusOsIsActive()) {
        // Routine ended: resume normal idle detection on the home screen.
        m_timer.start();
    }
}

bool IdleMonitor::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::TouchBegin:
        // Keyboard / touch is a "meaningful return" — resume an idle pause.
        emit resumeHint();
        noteActivity();
        break;
    case QEvent::WindowActivate:
    case QEvent::FocusIn:
        // A window-focus change also resumes an idle pause, but isn't the kind
        // of input that should reset the idle-screen countdown on its own.
        emit resumeHint();
        break;
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::Wheel:
    case QEvent::TouchUpdate:
        noteActivity();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void IdleMonitor::noteActivity()
{
    clearIdleState();
    if (!m_suppressed) {
        m_timer.start(); // restart the idle countdown
    }
    emit activity();
}

void IdleMonitor::clearIdleState()
{
    m_deepTimer.stop();
    if (m_deepIdle) {
        m_deepIdle = false;
        emit deepIdleChanged();
    }
    if (m_idle) {
        m_idle = false;
        emit idleChanged();
    }
}

void IdleMonitor::goIdle()
{
    // Never go idle while suppressed (a routine is engaged): a focused, idle
    // user is working, not away. Also only ever show idle when FocusOS itself is
    // the focused application.
    if (m_suppressed || !focusOsIsActive()) {
        return;
    }
    if (!m_idle) {
        m_idle = true;
        emit idleChanged();
    }
    // Start the countdown to deep sleep (panel off + music off + system suspend).
    m_deepTimer.start();
}

void IdleMonitor::goDeepIdle()
{
    if (m_suppressed || !focusOsIsActive() || !m_idle) {
        return;
    }
    if (!m_deepIdle) {
        m_deepIdle = true;
        emit deepIdleChanged();
    }
}
