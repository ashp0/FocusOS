#include "core/SystemStatus.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtGlobal>

namespace {

struct BatteryReading
{
    int percent = -1;
    bool charging = false;
};

struct PercentReading
{
    int percent = -1;
    bool available = false;
};

QString readTrimmedFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLocal8Bit(file.readAll()).trimmed();
}

bool runTextCommand(const QString &program, const QStringList &arguments, QString *stdoutText, int timeoutMs = 800)
{
    const QString path = QStandardPaths::findExecutable(program);
    if (path.isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(path, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return false;
    }
    if (stdoutText) {
        *stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput());
    }
    return true;
}

int clampedPercent(int value)
{
    return qBound(0, value, 100);
}

#if defined(Q_OS_MACOS)
BatteryReading readBattery()
{
    BatteryReading reading;

    QString output;
    if (!runTextCommand(QStringLiteral("pmset"), {QStringLiteral("-g"), QStringLiteral("batt")}, &output)) {
        return reading;
    }

    const QRegularExpression percentPattern(QStringLiteral("(\\d+)%"));
    const QRegularExpressionMatch match = percentPattern.match(output);
    if (match.hasMatch()) {
        reading.percent = clampedPercent(match.captured(1).toInt());
    }

    const QString lowered = output.toLower();
    reading.charging = lowered.contains(QStringLiteral("ac power")) ||
                       lowered.contains(QStringLiteral("charging")) ||
                       lowered.contains(QStringLiteral("charged"));
    return reading;
}

PercentReading readSystemVolume()
{
    QString output;
    if (!runTextCommand(QStringLiteral("osascript"),
                        {QStringLiteral("-e"), QStringLiteral("output volume of (get volume settings)")},
                        &output)) {
        return {};
    }
    bool ok = false;
    const int percent = output.trimmed().toInt(&ok);
    return {ok ? clampedPercent(percent) : -1, ok};
}

PercentReading readBrightness()
{
    return {};
}

void writeSystemVolume(int percent)
{
    QProcess::execute(QStringLiteral("/usr/bin/osascript"), {
        QStringLiteral("-e"),
        QStringLiteral("set volume output volume %1").arg(clampedPercent(percent))
    });
}

void writeMuteToggle()
{
    QProcess::execute(QStringLiteral("/usr/bin/osascript"), {
        QStringLiteral("-e"),
        QStringLiteral("set volume output muted (not (output muted of (get volume settings)))")
    });
}

void writeBrightness(int percent)
{
    Q_UNUSED(percent)
}
#elif defined(Q_OS_LINUX)
int readIntFile(const QString &path, bool *ok = nullptr)
{
    bool parsed = false;
    const int value = readTrimmedFile(path).toInt(&parsed);
    if (ok) {
        *ok = parsed;
    }
    return parsed ? value : 0;
}

BatteryReading readBattery()
{
    BatteryReading reading;

    QDir powerSupply(QStringLiteral("/sys/class/power_supply"));
    const QFileInfoList supplies = powerSupply.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &supply : supplies) {
        const QString supplyPath = supply.absoluteFilePath();
        const QString type = readTrimmedFile(supplyPath + QStringLiteral("/type")).toLower();
        const QString name = supply.fileName().toLower();
        if (type != QStringLiteral("battery") &&
            !name.startsWith(QStringLiteral("bat")) &&
            !name.contains(QStringLiteral("battery"))) {
            continue;
        }

        bool ok = false;
        int percent = readIntFile(supplyPath + QStringLiteral("/capacity"), &ok);
        if (!ok) {
            const int now = readIntFile(supplyPath + QStringLiteral("/charge_now"), &ok);
            bool fullOk = false;
            const int full = readIntFile(supplyPath + QStringLiteral("/charge_full"), &fullOk);
            if (!ok || !fullOk || full <= 0) {
                const int energyNow = readIntFile(supplyPath + QStringLiteral("/energy_now"), &ok);
                const int energyFull = readIntFile(supplyPath + QStringLiteral("/energy_full"), &fullOk);
                if (ok && fullOk && energyFull > 0) {
                    percent = (energyNow * 100 + energyFull / 2) / energyFull;
                    ok = true;
                }
            } else {
                percent = (now * 100 + full / 2) / full;
                ok = true;
            }
        }

        if (ok) {
            reading.percent = clampedPercent(percent);
        }

        const QString status = readTrimmedFile(supplyPath + QStringLiteral("/status")).toLower();
        reading.charging = status == QStringLiteral("charging") ||
                           status == QStringLiteral("full") ||
                           status == QStringLiteral("not charging");
        return reading;
    }

    return reading;
}

PercentReading readSystemVolume()
{
    QString output;
    if (runTextCommand(QStringLiteral("pactl"), {QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")}, &output)) {
        const QRegularExpression pattern(QStringLiteral("(\\d+)%"));
        const QRegularExpressionMatch match = pattern.match(output);
        if (match.hasMatch()) {
            return {clampedPercent(match.captured(1).toInt()), true};
        }
    }

    return {};
}

struct BacklightDevice
{
    QString path;
    int value = 0;
    int max = 0;
    bool valid = false;
};

BacklightDevice readBacklightDevice()
{
    QDir backlightRoot(QStringLiteral("/sys/class/backlight"));
    const QFileInfoList devices = backlightRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &device : devices) {
        const QString path = device.absoluteFilePath();
        bool maxOk = false;
        const int max = readIntFile(path + QStringLiteral("/max_brightness"), &maxOk);
        if (!maxOk || max <= 0) {
            continue;
        }

        bool valueOk = false;
        int value = readIntFile(path + QStringLiteral("/actual_brightness"), &valueOk);
        if (!valueOk) {
            value = readIntFile(path + QStringLiteral("/brightness"), &valueOk);
        }
        if (!valueOk) {
            continue;
        }

        return {path, value, max, true};
    }
    return {};
}

PercentReading readBrightness()
{
    const BacklightDevice device = readBacklightDevice();
    if (!device.valid) {
        return {};
    }

    return {clampedPercent((device.value * 100 + device.max / 2) / device.max), true};
}

void writeSystemVolume(int percent)
{
    const int clamped = clampedPercent(percent);
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (!pactl.isEmpty()) {
        QProcess::execute(pactl, {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("0")});
        QProcess::execute(pactl, {QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("%1%").arg(clamped)});
    }
}

void writeMuteToggle()
{
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (!pactl.isEmpty()) {
        QProcess::execute(pactl, {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("toggle")});
    }
}

void writeBrightness(int percent)
{
    const int clamped = clampedPercent(percent);
    const BacklightDevice device = readBacklightDevice();
    if (device.valid) {
        QFile brightnessFile(device.path + QStringLiteral("/brightness"));
        if (brightnessFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const int raw = qBound(1, (device.max * clamped + 50) / 100, device.max);
            brightnessFile.write(QByteArray::number(raw));
            brightnessFile.write("\n");
            return;
        }
    }
}
#else
BatteryReading readBattery()
{
    return {};
}

PercentReading readSystemVolume()
{
    return {};
}

PercentReading readBrightness()
{
    return {};
}

void writeSystemVolume(int percent)
{
    Q_UNUSED(percent)
}

void writeMuteToggle()
{
}

void writeBrightness(int percent)
{
    Q_UNUSED(percent)
}
#endif

} // namespace

SystemStatus::SystemStatus(QObject *parent)
    : QObject(parent)
{
    refreshStatus();
    m_statusTimer.setInterval(30000);
    connect(&m_statusTimer, &QTimer::timeout, this, &SystemStatus::refreshStatus);
    m_statusTimer.start();
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

int SystemStatus::volumePercent() const
{
    return m_volumePercent;
}

bool SystemStatus::volumeAvailable() const
{
    return m_volumeAvailable;
}

int SystemStatus::brightnessPercent() const
{
    return m_brightnessPercent;
}

bool SystemStatus::brightnessAvailable() const
{
    return m_brightnessAvailable;
}

void SystemStatus::adjustSystemVolume(int deltaPercent)
{
    const int base = m_volumeAvailable ? m_volumePercent : 50;
    setSystemVolume(base + deltaPercent);
}

void SystemStatus::setSystemVolume(int percent)
{
    writeSystemVolume(percent);
    refreshStatus();
}

void SystemStatus::toggleMute()
{
    writeMuteToggle();
    refreshStatus();
}

void SystemStatus::adjustBrightness(int deltaPercent)
{
    const int base = m_brightnessAvailable ? m_brightnessPercent : 50;
    setBrightness(base + deltaPercent);
}

void SystemStatus::setBrightness(int percent)
{
    writeBrightness(percent);
    refreshStatus();
}

void SystemStatus::refresh()
{
    refreshStatus();
}

void SystemStatus::refreshStatus()
{
    const BatteryReading battery = readBattery();
    const PercentReading volume = readSystemVolume();
    const PercentReading brightness = readBrightness();

    if (m_batteryPercent == battery.percent &&
        m_batteryCharging == battery.charging &&
        m_volumePercent == volume.percent &&
        m_volumeAvailable == volume.available &&
        m_brightnessPercent == brightness.percent &&
        m_brightnessAvailable == brightness.available) {
        return;
    }

    m_batteryPercent = battery.percent;
    m_batteryCharging = battery.charging;
    m_volumePercent = volume.percent;
    m_volumeAvailable = volume.available;
    m_brightnessPercent = brightness.percent;
    m_brightnessAvailable = brightness.available;
    emit statusChanged();
}
