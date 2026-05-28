#pragma once

#include <QObject>
#include <QTimer>

class SystemStatus final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString batteryLabel READ batteryLabel NOTIFY statusChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY statusChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY statusChanged)
    Q_PROPERTY(int volumePercent READ volumePercent NOTIFY statusChanged)
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable NOTIFY statusChanged)
    Q_PROPERTY(int brightnessPercent READ brightnessPercent NOTIFY statusChanged)
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY statusChanged)

public:
    explicit SystemStatus(QObject *parent = nullptr);

    QString batteryLabel() const;
    int batteryPercent() const;
    bool batteryCharging() const;
    int volumePercent() const;
    bool volumeAvailable() const;
    int brightnessPercent() const;
    bool brightnessAvailable() const;

    Q_INVOKABLE void adjustSystemVolume(int deltaPercent);
    Q_INVOKABLE void setSystemVolume(int percent);
    Q_INVOKABLE void adjustBrightness(int deltaPercent);
    Q_INVOKABLE void setBrightness(int percent);
    Q_INVOKABLE void refresh();

signals:
    void statusChanged();

private:
    void refreshStatus();

    QTimer m_statusTimer;
    int m_batteryPercent = -1;
    bool m_batteryCharging = false;
    int m_volumePercent = -1;
    bool m_volumeAvailable = false;
    int m_brightnessPercent = -1;
    bool m_brightnessAvailable = false;
};
