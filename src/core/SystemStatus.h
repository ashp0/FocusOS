#pragma once

#include <QObject>
#include <QTimer>

class SystemStatus final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString batteryLabel READ batteryLabel NOTIFY statusChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY statusChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY statusChanged)

public:
    explicit SystemStatus(QObject *parent = nullptr);

    QString batteryLabel() const;
    int batteryPercent() const;
    bool batteryCharging() const;

signals:
    void statusChanged();

private:
    void refreshBattery();

    QTimer m_batteryTimer;
    int m_batteryPercent = -1;
    bool m_batteryCharging = false;
};
