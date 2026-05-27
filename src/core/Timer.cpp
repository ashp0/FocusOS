#include "core/Timer.h"

FocusTimer::FocusTimer(QObject *parent)
    : QObject(parent)
{
    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, &FocusTimer::tick);
}

int FocusTimer::remainingSeconds() const
{
    return m_remainingSeconds;
}

bool FocusTimer::active() const
{
    return m_active;
}

void FocusTimer::start(int seconds)
{
    m_remainingSeconds = qMax(0, seconds);
    const bool wasActive = m_active;
    const bool wasPaused = m_paused;
    m_active = m_remainingSeconds > 0;
    m_paused = false;
    emit remainingSecondsChanged();
    if (wasActive != m_active) {
        emit activeChanged();
    }
    if (wasPaused) {
        emit pausedChanged();
    }

    if (m_active) {
        m_tickTimer.start();
    } else {
        m_tickTimer.stop();
        emit expired();
    }
}

void FocusTimer::pause()
{
    if (!m_active || m_paused) {
        return;
    }
    m_paused = true;
    m_tickTimer.stop();
    emit pausedChanged();
}

void FocusTimer::resume()
{
    if (!m_active || !m_paused) {
        return;
    }
    m_paused = false;
    m_tickTimer.start();
    emit pausedChanged();
}

bool FocusTimer::paused() const
{
    return m_paused;
}

void FocusTimer::stop()
{
    m_tickTimer.stop();
    const bool wasActive = m_active;
    const bool wasPaused = m_paused;
    m_active = false;
    m_paused = false;
    m_remainingSeconds = 0;
    emit remainingSecondsChanged();
    if (wasActive) {
        emit activeChanged();
    }
    if (wasPaused) {
        emit pausedChanged();
    }
}

void FocusTimer::tick()
{
    if (!m_active) {
        return;
    }

    --m_remainingSeconds;
    emit remainingSecondsChanged();

    if (m_remainingSeconds <= 0) {
        m_tickTimer.stop();
        m_active = false;
        emit activeChanged();
        playAlarm();
        emit expired();
    }
}

void FocusTimer::playAlarm()
{
    // Routine completion is acknowledged by the shell prompt and visual shift.
    // Avoid an attention-grabbing alarm at the exact moment the user returns.
}
