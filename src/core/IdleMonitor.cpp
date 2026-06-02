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

    m_lockTimer.setSingleShot(true);
    m_lockTimer.setInterval(m_lockTimeoutMs);
    connect(&m_lockTimer, &QTimer::timeout, this, &IdleMonitor::goLockIdle);

    if (qApp) {
        qApp->installEventFilter(this);
    }

    // Idle is application-wide: input to ANY FocusOS window (the shell, the
    // Settings modal, the overlay) counts as activity via the event filter. We
    // used to additionally gate the whole thing on FocusOS being the focused
    // application (QGuiApplication::applicationState == Active), to avoid blanking
    // while the user worked in another window. But on the bare kwin_wayland
    // session the shell often never registers as "active" (the same unreliability
    // the AmbientLayer wallpaper hit), so that gate left the screensaver dead: the
    // countdown never started and goIdle()/goDeepIdle() always early-returned.
    // On the home screen FocusOS owns the screen anyway, and while a routine is
    // engaged idle detection is suppressed outright — so the focus gate bought us
    // nothing real while breaking idle entirely. Drop it and run purely off input
    // events + the suppression flag.
    m_timer.start();
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
    } else {
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
    m_lockTimer.stop();
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
    // user is working, not away.
    if (m_suppressed) {
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
    if (m_suppressed || !m_idle) {
        return;
    }
    if (!m_deepIdle) {
        m_deepIdle = true;
        emit deepIdleChanged();
    }
    // Start the final countdown to the screen lock (30 minutes total idle).
    m_lockTimer.start();
}

void IdleMonitor::goLockIdle()
{
    // Same guard as the earlier stages: never lock a focused, idle user mid
    // routine. m_deepIdle is the precondition because the lock timer is only ever
    // armed from goDeepIdle().
    if (m_suppressed || !m_deepIdle) {
        return;
    }
    emit lockIdle();
}
