#pragma once

#include <QObject>
#include <QTimer>

class FocusTimer final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY remainingSecondsChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit FocusTimer(QObject *parent = nullptr);

    int remainingSeconds() const;
    bool active() const;

    Q_INVOKABLE void start(int seconds);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    bool paused() const;

signals:
    void remainingSecondsChanged();
    void activeChanged();
    void pausedChanged();
    void expired();

private:
    void tick();
    void playAlarm();

    QTimer m_tickTimer;
    int m_remainingSeconds = 0;
    bool m_active = false;
    bool m_paused = false;
};
