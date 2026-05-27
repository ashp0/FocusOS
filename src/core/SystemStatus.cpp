#include "core/SystemStatus.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace {

struct BatteryReading
{
    int percent = -1;
    bool charging = false;
};

#if defined(Q_OS_MACOS)
BatteryReading readBattery()
{
    BatteryReading reading;

    QProcess process;
    process.start(QStringLiteral("/usr/bin/pmset"), {QStringLiteral("-g"), QStringLiteral("batt")});
    if (!process.waitForFinished(750)) {
        process.kill();
        return reading;
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QRegularExpression percentPattern(QStringLiteral("(\\d+)%"));
    const QRegularExpressionMatch match = percentPattern.match(output);
    if (match.hasMatch()) {
        reading.percent = qBound(0, match.captured(1).toInt(), 100);
    }

    const QString lowered = output.toLower();
    reading.charging = lowered.contains(QStringLiteral("ac power")) ||
                       lowered.contains(QStringLiteral("charging")) ||
                       lowered.contains(QStringLiteral("charged"));
    return reading;
}
#elif defined(Q_OS_LINUX)
QString readTrimmedFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLocal8Bit(file.readAll()).trimmed();
}

BatteryReading readBattery()
{
    BatteryReading reading;

    QDir powerSupply(QStringLiteral("/sys/class/power_supply"));
    const QFileInfoList batteries = powerSupply.entryInfoList({QStringLiteral("BAT*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (batteries.isEmpty()) {
        return reading;
    }

    const QString batteryPath = batteries.first().absoluteFilePath();
    bool ok = false;
    const int percent = readTrimmedFile(batteryPath + QStringLiteral("/capacity")).toInt(&ok);
    if (ok) {
        reading.percent = qBound(0, percent, 100);
    }

    const QString status = readTrimmedFile(batteryPath + QStringLiteral("/status")).toLower();
    reading.charging = status == QStringLiteral("charging") ||
                       status == QStringLiteral("full") ||
                       status == QStringLiteral("not charging");
    return reading;
}
#else
BatteryReading readBattery()
{
    return {};
}
#endif

} // namespace

SystemStatus::SystemStatus(QObject *parent)
    : QObject(parent)
{
    refreshBattery();
    m_batteryTimer.setInterval(30000);
    connect(&m_batteryTimer, &QTimer::timeout, this, &SystemStatus::refreshBattery);
    m_batteryTimer.start();
}

QString SystemStatus::batteryLabel() const
{
    if (m_batteryPercent < 0) {
        return QStringLiteral("BAT --");
    }

    QString label = QStringLiteral("BAT %1%").arg(m_batteryPercent);
    if (m_batteryCharging) {
        label += QStringLiteral(" AC");
    }
    return label;
}

int SystemStatus::batteryPercent() const
{
    return m_batteryPercent;
}

bool SystemStatus::batteryCharging() const
{
    return m_batteryCharging;
}

void SystemStatus::refreshBattery()
{
    const BatteryReading reading = readBattery();
    if (m_batteryPercent == reading.percent && m_batteryCharging == reading.charging) {
        return;
    }

    m_batteryPercent = reading.percent;
    m_batteryCharging = reading.charging;
    emit statusChanged();
}
