#include "core/IdleMonitor.h"

#include <QCoreApplication>
#include <QEvent>

IdleMonitor::IdleMonitor(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(m_timeoutMs);
    connect(&m_timer, &QTimer::timeout, this, &IdleMonitor::goIdle);
    m_timer.start();

    if (qApp) {
        qApp->installEventFilter(this);
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

void IdleMonitor::wake()
{
    noteActivity();
}

bool IdleMonitor::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::TouchBegin:
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
    if (m_idle) {
        m_idle = false;
        emit idleChanged();
    }
    m_timer.start(); // restart the idle countdown
    emit activity();
}

void IdleMonitor::goIdle()
{
    if (!m_idle) {
        m_idle = true;
        emit idleChanged();
    }
}
