#include "core/SystemStatus.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtGlobal>

#if defined(Q_OS_MACOS)
#include <pwd.h>
#include <unistd.h>
#endif

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
    // Fire-and-forget so the UI thread never blocks waiting on osascript.
    QProcess::startDetached(QStringLiteral("/usr/bin/osascript"), {
        QStringLiteral("-e"),
        QStringLiteral("set volume output volume %1").arg(clampedPercent(percent))
    });
}

void writeMuteToggle()
{
    QProcess::startDetached(QStringLiteral("/usr/bin/osascript"), {
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
        // Detached so dragging the volume slider doesn't stall the UI thread on
        // a synchronous pactl round-trip for every step.
        QProcess::startDetached(pactl, {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("0")});
        QProcess::startDetached(pactl, {QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("%1%").arg(clamped)});
    }
}

void writeMuteToggle()
{
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (!pactl.isEmpty()) {
        QProcess::startDetached(pactl, {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("toggle")});
    }
}

void writeBrightness(int percent)
{
    const int clamped = clampedPercent(percent);

    // Prefer brightnessctl: it ships a udev rule (and/or setuid helper) that
    // grants write access to the backlight without root, which is why the bare
    // sysfs write below silently fails for a normal user — /sys/class/backlight/
    // */brightness is root-owned. brightnessctl is detached so dragging the
    // slider never blocks the UI thread.
    const QString brightnessctl = QStandardPaths::findExecutable(QStringLiteral("brightnessctl"));
    if (!brightnessctl.isEmpty()) {
        // "-n" keeps a 1% floor so the screen never goes fully black.
        const int floored = qMax(1, clamped);
        if (QProcess::startDetached(brightnessctl,
                                    {QStringLiteral("set"), QStringLiteral("%1%").arg(floored)})) {
            return;
        }
    }

    // Fallback: direct sysfs write. Only succeeds when the user is in the
    // "video" group and a udev rule has made the file group-writable.
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

    // ~50ms throttle window → at most ~20 volume writes/second while dragging,
    // with the final value always flushed (trailing edge). See flushPendingVolume.
    m_volumeWriteThrottle.setSingleShot(true);
    m_volumeWriteThrottle.setInterval(50);
    connect(&m_volumeWriteThrottle, &QTimer::timeout, this, &SystemStatus::flushPendingVolume);

    refreshElevatedLaunchState();
}

void SystemStatus::flushPendingVolume()
{
    if (m_pendingVolume < 0 || m_pendingVolume == m_lastWrittenVolume) {
        return;  // nothing new since the last write — let the throttle lapse
    }
    writeSystemVolume(m_pendingVolume);
    m_lastWrittenVolume = m_pendingVolume;
    // Re-arm so a value that changed again during this window still flushes.
    m_volumeWriteThrottle.start();
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
    const int clamped = clampedPercent(percent);

    // Optimistically reflect the new value instead of blocking on a read-back —
    // the periodic refresh reconciles any drift. Keeps the slider instant.
    if (!m_volumeAvailable || m_volumePercent != clamped) {
        m_volumePercent = clamped;
        m_volumeAvailable = true;
        emit statusChanged();
    }

    // Throttle the actual (heavy) write. Leading edge fires immediately; further
    // changes inside the window ride on the trailing flush (flushPendingVolume).
    m_pendingVolume = clamped;
    if (!m_volumeWriteThrottle.isActive()) {
        writeSystemVolume(clamped);
        m_lastWrittenVolume = clamped;
        m_volumeWriteThrottle.start();
    }
}

void SystemStatus::toggleMute()
{
    // Detached write; the 30s refresh (or a later volume change) catches up.
    writeMuteToggle();
}

void SystemStatus::adjustBrightness(int deltaPercent)
{
    const int base = m_brightnessAvailable ? m_brightnessPercent : 50;
    setBrightness(base + deltaPercent);
}

void SystemStatus::setBrightness(int percent)
{
    const int clamped = clampedPercent(percent);
    writeBrightness(clamped);
    if (!m_brightnessAvailable || m_brightnessPercent != clamped) {
        m_brightnessPercent = clamped;
        m_brightnessAvailable = true;
        emit statusChanged();
    }
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

QString SystemStatus::startupScriptPath() const
{
    return QDir::homePath() + QStringLiteral("/.focusos/startup.sh");
}

QString SystemStatus::readStartupScript() const
{
    QFile file(startupScriptPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool SystemStatus::writeStartupScript(const QString &contents)
{
    const QString path = startupScriptPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(contents.toUtf8());
    file.close();
    // Make it executable too, so a user who adds a shebang and runs it by hand
    // gets the behavior they expect (the backend runs it through a shell either
    // way, so this is purely a courtesy).
    file.setPermissions(file.permissions()
                        | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    return true;
}

// ──────────────────── Passwordless firewall access (macOS) ─────────────────
//
// Goal: let the pf network lock work when FocusOS is launched from the Dock as a
// normal user — no Terminal, no `sudo` typed at engage time. pf (`pfctl`) is the
// only thing that genuinely needs root, so we install a single NOPASSWD sudoers
// rule scoped to *`/sbin/pfctl`* for *this user* and drive pfctl through `sudo -n`
// (see MacBackendNative::runPfctl). The admin password is only ever needed once,
// here, to install that rule.
//
// We deliberately do NOT re-exec the whole GUI app as root: a sudo-launched root
// process loses the TCC identity that the Accessibility grant (and therefore the
// CGEventTap key blocker) is tied to, which is what made macOS re-pop the
// Accessibility prompt on every launch. Keeping the app unprivileged and elevating
// only pfctl fixes that while still installing the firewall.

#if defined(Q_OS_MACOS)
namespace {

const QString kSudoersFile = QStringLiteral("/etc/sudoers.d/focusos");
// The single command the NOPASSWD rule authorises. pf is the only thing FocusOS
// needs root for; everything else runs fine as the normal user.
const QString kPfctlPath = QStringLiteral("/sbin/pfctl");

QString elevatedRealUser()
{
    // Launched via sudo, getuid() is 0; the human is in SUDO_USER. Otherwise we
    // are that human already.
    const QByteArray sudoUser = qgetenv("SUDO_USER");
    if (!sudoUser.isEmpty() && sudoUser != "root") {
        return QString::fromLocal8Bit(sudoUser);
    }
    if (const struct passwd *pw = getpwuid(getuid())) {
        return QString::fromLocal8Bit(pw->pw_name);
    }
    return {};
}

QString elevatedSelfPath()
{
    // canonicalFilePath resolves symlinks so it matches what sudo compares the
    // exec'd path against (and what the sudoers Cmnd must spell out).
    const QString canonical = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    return canonical.isEmpty() ? QCoreApplication::applicationFilePath() : canonical;
}

// sudoers escapes whitespace and the special chars ", : = \" in a command path
// with a leading backslash. Escape them so a path with spaces still matches.
QString sudoersEscape(const QString &path)
{
    QString out;
    out.reserve(path.size() + 8);
    for (const QChar ch : path) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char(' ') || ch == QLatin1Char(',') ||
            ch == QLatin1Char(':') || ch == QLatin1Char('=')) {
            out.append(QLatin1Char('\\'));
        }
        out.append(ch);
    }
    return out;
}

// Run a /bin/sh script as root. When already root we run it directly; otherwise
// we drive `sudo -S` and feed it the admin password on stdin. Returns empty on
// success, else a human-readable error (trimmed sudo/script stderr).
QString runPrivilegedScript(const QString &script, const QString &adminPassword)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    if (geteuid() == 0) {
        proc.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script});
    } else {
        // -S: read the password from stdin. -k: ignore any cached credential so a
        // wrong password fails here instead of silently succeeding on a stale
        // timestamp. -p "": suppress the prompt (we feed stdin directly).
        proc.start(QStringLiteral("/usr/bin/sudo"),
                   {QStringLiteral("-S"), QStringLiteral("-k"), QStringLiteral("-p"),
                    QString(), QStringLiteral("/bin/sh"), QStringLiteral("-c"), script});
        if (!proc.waitForStarted(5000)) {
            return QStringLiteral("Could not start sudo.");
        }
        proc.write(adminPassword.toUtf8());
        proc.write("\n");
        proc.closeWriteChannel();
    }
    if (!proc.waitForFinished(20000)) {
        proc.kill();
        proc.waitForFinished(500);
        return QStringLiteral("The privileged command timed out.");
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        QString output = QString::fromLocal8Bit(proc.readAll()).trimmed();
        if (output.contains(QStringLiteral("incorrect password"), Qt::CaseInsensitive) ||
            output.contains(QStringLiteral("Sorry, try again"), Qt::CaseInsensitive)) {
            return QStringLiteral("Incorrect admin password.");
        }
        return output.isEmpty()
            ? QStringLiteral("The privileged command failed (exit %1).").arg(proc.exitCode())
            : output.left(300);
    }
    return {};
}

} // namespace
#endif // Q_OS_MACOS

bool SystemStatus::elevatedLaunchSupported() const
{
#if defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

bool SystemStatus::elevatedLaunchEnabled() const
{
    return m_elevatedLaunchEnabled;
}

bool SystemStatus::runningAsRoot() const
{
#if defined(Q_OS_MACOS)
    return geteuid() == 0;
#else
    return false;
#endif
}

QString SystemStatus::elevatedBinaryPath() const
{
#if defined(Q_OS_MACOS)
    return elevatedSelfPath();
#else
    return {};
#endif
}

void SystemStatus::refreshElevatedLaunchState()
{
#if defined(Q_OS_MACOS)
    bool enabled = false;
    if (geteuid() == 0) {
        // We can read the rule file directly; confirm it grants NOPASSWD pfctl.
        QFile file(kSudoersFile);
        if (file.open(QIODevice::ReadOnly)) {
            const QString contents = QString::fromUtf8(file.readAll());
            enabled = contents.contains(QStringLiteral("NOPASSWD:"))
                && contents.contains(kPfctlPath);
        }
    } else {
        // sudoers.d is root-only; instead ask sudo whether it would let us run
        // pfctl with NOPASSWD. A cached sudo timestamp can make the exit code
        // succeed for password-required entries, so require the explicit tag.
        QProcess probe;
        probe.setProcessChannelMode(QProcess::MergedChannels);
        probe.start(QStringLiteral("/usr/bin/sudo"),
                    {QStringLiteral("-n"), QStringLiteral("-l"), kPfctlPath});
        if (probe.waitForFinished(5000)) {
            const QString listing = QString::fromLocal8Bit(probe.readAll());
            enabled = probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0
                && listing.contains(QStringLiteral("NOPASSWD"));
        }
    }
    if (enabled != m_elevatedLaunchEnabled) {
        m_elevatedLaunchEnabled = enabled;
        emit elevatedLaunchChanged();
    }
#endif
}

void SystemStatus::refreshElevatedLaunch()
{
    refreshElevatedLaunchState();
}

QString SystemStatus::enableElevatedLaunch(const QString &adminPassword)
{
#if defined(Q_OS_MACOS)
    const QString user = elevatedRealUser();
    if (user.isEmpty()) {
        return QStringLiteral("Could not determine the current user.");
    }
    if (geteuid() != 0 && adminPassword.isEmpty()) {
        return QStringLiteral("Enter your macOS admin password to enable this.");
    }

    // The rule: this user may run pfctl as root with no password (FocusOS drives
    // it via `sudo -n /sbin/pfctl …` to install the network lock). Validate it
    // with visudo before installing so a bad rule never breaks sudo entirely.
    const QString rule = QStringLiteral("%1 ALL=(root) NOPASSWD: %2\n")
                             .arg(user, sudoersEscape(kPfctlPath));

    QTemporaryFile temp(QDir::tempPath() + QStringLiteral("/focusos-sudoers.XXXXXX"));
    temp.setAutoRemove(true);
    if (!temp.open()) {
        return QStringLiteral("Could not create a temporary file.");
    }
    temp.write(rule.toUtf8());
    temp.flush();
    const QString tempPath = temp.fileName();

    const QString script =
        QStringLiteral("/bin/mkdir -p /etc/sudoers.d && "
                       "/usr/sbin/visudo -cf '%1' && "
                       "/usr/bin/install -m 0440 -o root -g wheel '%1' '%2'")
            .arg(tempPath, kSudoersFile);

    const QString error = runPrivilegedScript(script, adminPassword);
    refreshElevatedLaunchState();
    return error;
#else
    Q_UNUSED(adminPassword);
    return QStringLiteral("Elevated launch is only available on macOS.");
#endif
}

QString SystemStatus::disableElevatedLaunch(const QString &adminPassword)
{
#if defined(Q_OS_MACOS)
    if (geteuid() != 0 && adminPassword.isEmpty()) {
        return QStringLiteral("Enter your macOS admin password to disable this.");
    }
    const QString script = QStringLiteral("/bin/rm -f '%1'").arg(kSudoersFile);
    const QString error = runPrivilegedScript(script, adminPassword);
    refreshElevatedLaunchState();
    return error;
#else
    Q_UNUSED(adminPassword);
    return QStringLiteral("Elevated launch is only available on macOS.");
#endif
}
