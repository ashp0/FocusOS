#include "core/RoutineManager.h"

#include "core/AppPaths.h"
#include "core/FilePicker.h"
#include "platform/PlatformBackend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QVariantMap>

#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

// The routine timer ticks every second, but the user doesn't want the SSD
// touched that often. Persist the crash checkpoint at most this often on the
// ordinary tick path; legitimate state transitions force an immediate write.
constexpr qint64 kCheckpointWriteIntervalMs = 30 * 1000;

QString routinesPath()
{
    return AppPaths::filePath(QStringLiteral("routines.json"));
}

QString configPath()
{
    return AppPaths::filePath(QStringLiteral("config.json"));
}

// Live-session checkpoint. Its mere existence means a routine is "armed" —
// the respawn watchdog keeps FocusOS alive while this file is present, and a
// fresh launch resumes the locked routine from it. Deleted only on a
// legitimate end (expiry, or TOTP-unlock past the min-time floor).
QString activeSessionPath()
{
    return AppPaths::filePath(QStringLiteral("active.json"));
}

#if defined(Q_OS_MACOS)
uint macLaunchdUserId()
{
    bool ok = false;
    const uint sudoUid = qEnvironmentVariable("SUDO_UID").toUInt(&ok);
    if (geteuid() == 0 && ok && sudoUid > 0) {
        return sudoUid;
    }
    return static_cast<uint>(getuid());
}

qint64 currentBootTimeSeconds()
{
    timeval bootTime {};
    size_t size = sizeof(bootTime);
    if (sysctlbyname("kern.boottime", &bootTime, &size, nullptr, 0) != 0 || bootTime.tv_sec <= 0) {
        return 0;
    }
    return static_cast<qint64>(bootTime.tv_sec);
}

QString currentBootId()
{
    const qint64 bootTime = currentBootTimeSeconds();
    return bootTime > 0 ? QStringLiteral("macos:%1").arg(bootTime) : QString();
}

bool macRecoveryWatchdogLoaded()
{
    QProcess process;
    process.start(QStringLiteral("/bin/launchctl"),
                  {QStringLiteral("print"),
                   QStringLiteral("gui/%1/com.focusos.watchdog").arg(macLaunchdUserId())});
    if (!process.waitForStarted(1000)) {
        return false;
    }
    if (!process.waitForFinished(1500)) {
        process.kill();
        process.waitForFinished(200);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool checkpointBelongsToCurrentBoot(const QJsonObject &object, const QFileInfo &checkpointInfo)
{
    const QString expectedBootId = currentBootId();
    if (expectedBootId.isEmpty()) {
        // Startup safety on macOS matters more than resurrecting a questionable
        // lock. If we cannot identify the boot, do not auto-engage a routine.
        return false;
    }

    const QString storedBootId = object.value(QStringLiteral("boot_id")).toString();
    if (!storedBootId.isEmpty()) {
        return storedBootId == expectedBootId && macRecoveryWatchdogLoaded();
    }

    // Legacy checkpoints did not carry boot_id. Let same-boot crash recovery keep
    // working, but drop files left behind before a full restart.
    const QDateTime modified = checkpointInfo.lastModified().toUTC();
    return modified.isValid() &&
           modified.toSecsSinceEpoch() >= currentBootTimeSeconds() &&
           macRecoveryWatchdogLoaded();
}
#else
QString currentBootId()
{
    return {};
}

bool checkpointBelongsToCurrentBoot(const QJsonObject &object, const QFileInfo &checkpointInfo)
{
    Q_UNUSED(object);
    Q_UNUSED(checkpointInfo);
    return true;
}
#endif

QStringList jsonArrayToStringList(const QJsonArray &array)
{
    QStringList values;
    for (const QJsonValue &value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

// A routine "requires a web browser" if anything it launches opens in one: an
// in-browser kiosk app entry (kiosk:<url>) or a plain allowed-URL list (those
// are handed to the default Chromium browser via openUrls). Such routines lean
// on the blocker EXTENSION to gate URLs, so they skip the system-wide outbound
// firewall — a full egress block would otherwise strand the allowed sites'
// off-host subresources. Routines with neither get the full system-wide block.
bool routineRequiresBrowser(const Routine &routine)
{
    if (!routine.allowedUrls.isEmpty()) {
        return true;
    }
    for (const QString &app : routine.apps) {
        if (app.trimmed().startsWith(QStringLiteral("kiosk:"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QJsonObject readConfigObject()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject {};
}

QStringList variantToStringList(const QVariant &value)
{
    QStringList values;
    const auto append = [&values](const QString &text) {
        const QString trimmed = text.trimmed();
        if (!trimmed.isEmpty()) {
            values.append(trimmed);
        }
    };

    if (value.metaType().id() == QMetaType::QStringList) {
        for (const QString &text : value.toStringList()) {
            append(text);
        }
        return values;
    }

    if (value.metaType().id() == QMetaType::QString) {
        const QStringList parts = value.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            append(part);
        }
        return values;
    }

    for (const QVariant &entry : value.toList()) {
        append(entry.toString());
    }
    return values;
}

QString routineIdFromName(const QString &name)
{
    QString id = name.trimmed().toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    id.replace(QRegularExpression(QStringLiteral("^_+|_+$")), QString());
    return id;
}

int intFromVariant(const QVariant &value, int fallback)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok ? parsed : fallback;
}

// Per-routine music behavior on engage: "stop" (silence), "low" (duck to a low
// volume) or "same" (keep the configured volume). Unknown/empty falls back to
// "low" — "Continue at low volume" is the default a new routine gets.
QString normalizedMusicBehavior(const QString &behavior)
{
    const QString normalized = behavior.trimmed().toLower();
    if (normalized == QStringLiteral("stop") || normalized == QStringLiteral("same")) {
        return normalized;
    }
    return QStringLiteral("low");
}

// The single on-disk encoding for one routine. Both writers go through here —
// the editor's saveRoutines() (after it validates/dedupes the QML payload into a
// Routine) and the live persistRoutines() (music / display-sleep toggles) — so
// the two paths can never drift on field set or normalization. Keep this the
// only place that names routines.json's keys.
QJsonObject serializeRoutine(const Routine &routine)
{
    QJsonArray appArray;
    for (const QString &app : routine.apps) {
        appArray.append(app);
    }
    QJsonArray urlArray;
    for (const QString &url : routine.allowedUrls) {
        urlArray.append(url);
    }

    QJsonObject object;
    object.insert(QStringLiteral("id"), routine.id);
    object.insert(QStringLiteral("name"), routine.name);
    object.insert(QStringLiteral("description"), routine.description);
    object.insert(QStringLiteral("apps"), appArray);
    object.insert(QStringLiteral("allowed_urls"), urlArray);
    object.insert(QStringLiteral("access_folder"), routine.accessFolder);
    object.insert(QStringLiteral("access_desktop"), routine.accessDesktop);
    object.insert(QStringLiteral("access_documents"), routine.accessDocuments);
    object.insert(QStringLiteral("access_downloads"), routine.accessDownloads);
    object.insert(QStringLiteral("browsable"), routine.browsable);
    object.insert(QStringLiteral("time_limit_minutes"), routine.timeLimitMinutes);
    object.insert(QStringLiteral("min_time_minutes"), routine.minTimeMinutes);
    object.insert(QStringLiteral("network_lock"), routine.networkLock);
    object.insert(QStringLiteral("full_access"), routine.fullAccess);
    object.insert(QStringLiteral("break_frequency_minutes"), routine.breakFrequencyMinutes);
    object.insert(QStringLiteral("break_duration_minutes"), routine.breakDurationMinutes);
    object.insert(QStringLiteral("keep_display_awake"), routine.keepDisplayAwake);
    object.insert(QStringLiteral("music_behavior"), routine.musicBehavior);
    return object;
}

} // namespace

RoutineManager::RoutineManager(PlatformBackend *backend, QObject *parent)
    : QAbstractListModel(parent)
    , m_backend(backend)
{
    QDir().mkpath(AppPaths::dataDirectory());
    loadConfig();
    loadRoutines();

    connect(&m_routineTimer, &FocusTimer::remainingSecondsChanged, this, [this] {
        emit remainingSecondsChanged();
        emitRowsChanged();
        emitActiveSessionProgress();
        // Refresh the checkpoint each tick so a kill/crash resumes from the
        // most recent remaining-time, not the start-of-session value.
        writeActiveSession();
    });
    connect(&m_routineTimer, &FocusTimer::pausedChanged, this, [this] {
        emit pausedChanged();
        emit pauseModeChanged();
    });
    connect(&m_routineTimer, &FocusTimer::expired, this, &RoutineManager::onRoutineExpired);

    m_accessTimer.setInterval(1000);
    connect(&m_accessTimer, &QTimer::timeout, this, &RoutineManager::tickOtherAccess);

    // Unlock-panel inactivity auto-lock: 30 minutes with no input revokes
    // access and re-locks settings (finishOtherAccess re-locks the modal).
    m_inactivityTimer.setSingleShot(true);
    m_inactivityTimer.setInterval(30 * 60 * 1000);
    connect(&m_inactivityTimer, &QTimer::timeout, this, [this] {
        if (accessGranted()) {
            finishOtherAccess();
            setStatusMessage(QStringLiteral("ACCESS LOCKED — 30 MIN INACTIVITY"));
        }
    });

    // Deferred so ShellWindow's signal connections (activeChanged, etc.) are
    // wired before a resumed routine fires them. Resumes a locked routine left
    // behind by a kill/crash via the active.json checkpoint.
    QTimer::singleShot(0, this, &RoutineManager::resumeActiveSessionIfPresent);

    // Persistent kiosk defaults ON: the very first time FocusOS runs on a machine
    // that supports it, arm launch-at-login + un-quittable. A marker records that
    // the default has been applied so we never re-enable it after the user turns it
    // off in Settings → SYSTEM. (Enabling only writes the LaunchAgent plist; it does
    // not spawn a second instance — see MacBackend::setPersistentKiosk.)
    if (m_backend && m_backend->persistentKioskSupported()) {
        const QString marker = QDir(AppPaths::dataDirectory()).absoluteFilePath(
            QStringLiteral("kiosk-default-applied"));
        if (!QFileInfo::exists(marker)) {
            QString kioskError;
            m_backend->setPersistentKiosk(true, &kioskError);
            QFile flag(marker);
            if (flag.open(QIODevice::WriteOnly | QIODevice::Text)) {
                flag.write("1\n");
            }
            emit persistentKioskChanged();
        }
    }
}

int RoutineManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_routines.size();
}

QVariant RoutineManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_routines.size()) {
        return {};
    }

    const Routine &routine = m_routines.at(index.row());
    const bool routineIsActive = routine.id == m_activeRoutineId;

    switch (role) {
    case RoutineIdRole:
        return routine.id;
    case NameRole:
        return routine.name;
    case DescriptionRole:
        return routine.description;
    case AppsRole:
        return routine.apps;
    case AppsDisplayRole: {
        QStringList appNames;
        appNames.reserve(routine.apps.size());
        for (const QString &app : routine.apps) {
            QString name = applicationDisplayName(app);
            appNames.append(name.toUpper());
        }
        return appNames.join(QStringLiteral("  ■  "));
    }
    case AllowedUrlsRole:
        return routine.allowedUrls;
    case TimeLimitMinutesRole:
        return routine.timeLimitMinutes;
    case MinTimeMinutesRole:
        return routine.minTimeMinutes;
    case NetworkLockRole:
        return routine.networkLock;
    case FullAccessRole:
        return routine.fullAccess;
    case BreakFrequencyMinutesRole:
        return routine.breakFrequencyMinutes;
    case BreakDurationMinutesRole:
        return routine.breakDurationMinutes;
    case IsActiveRole:
        return routineIsActive;
    case TimeLabelRole:
        return routineIsActive ? formatDuration(m_routineTimer.remainingSeconds()) : formatDuration(routine.timeLimitMinutes * 60);
    case ButtonLabelRole:
        if (routineIsActive) {
            return QStringLiteral("■ LOCKED");
        }
        return active() ? QStringLiteral("⊠ SEALED") : QStringLiteral("▶ ENGAGE");
    case ButtonEnabledRole:
        return !active() && !accessGranted();
    default:
        return {};
    }
}

QHash<int, QByteArray> RoutineManager::roleNames() const
{
    return {
        {RoutineIdRole, "routineId"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {AppsRole, "apps"},
        {AppsDisplayRole, "appsDisplay"},
        {AllowedUrlsRole, "allowedUrls"},
        {TimeLimitMinutesRole, "timeLimitMinutes"},
        {MinTimeMinutesRole, "minTimeMinutes"},
        {NetworkLockRole, "networkLock"},
        {FullAccessRole, "fullAccess"},
        {BreakFrequencyMinutesRole, "breakFrequencyMinutes"},
        {BreakDurationMinutesRole, "breakDurationMinutes"},
        {IsActiveRole, "isActive"},
        {TimeLabelRole, "timeLabel"},
        {ButtonLabelRole, "buttonLabel"},
        {ButtonEnabledRole, "buttonEnabled"}
    };
}

bool RoutineManager::active() const
{
    return !m_activeRoutineId.isEmpty();
}

QString RoutineManager::activeRoutineId() const
{
    return m_activeRoutineId;
}

QString RoutineManager::activeRoutineName() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).name : QString();
}

int RoutineManager::activeRoutineTotalSeconds() const
{
    // Open-ended continuation has no fixed total — report 0 so any
    // progress/percentage math collapses to zero (the UI hides the countdown).
    if (m_openEnded) {
        return 0;
    }
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).timeLimitMinutes * 60 : 0;
}

QString RoutineManager::activeRoutineDescription() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).description : QString();
}

int RoutineManager::activeRoutineBreakFrequencyMinutes() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).breakFrequencyMinutes : 0;
}

int RoutineManager::activeRoutineBreakDurationMinutes() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).breakDurationMinutes : 0;
}

bool RoutineManager::activeRoutineBrowsable() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).browsable : false;
}

QString RoutineManager::activeRoutineMusicBehavior() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).musicBehavior
                             : QStringLiteral("low");
}

void RoutineManager::setActiveRoutineMusicBehavior(const QString &behavior)
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0) {
        return;
    }

    const QString normalized = normalizedMusicBehavior(behavior);
    if (m_routines[routineIndex].musicBehavior == normalized) {
        return;
    }

    m_routines[routineIndex].musicBehavior = normalized;
    persistRoutines();
    // Re-emitting activeChanged lets the MusicEngine wiring in main.cpp pick up
    // the new behavior live (it re-reads activeRoutineMusicBehavior on engage).
    emit activeChanged();
}

int RoutineManager::remainingSeconds() const
{
    if (m_openEnded) {
        return 0;
    }
    return m_routineTimer.remainingSeconds();
}

int RoutineManager::elapsedSeconds() const
{
    return qMax(0, activeRoutineTotalSeconds() - m_routineTimer.remainingSeconds());
}

bool RoutineManager::openEnded() const
{
    return m_openEnded;
}

bool RoutineManager::screenLocked() const
{
    return m_screenLocked;
}

void RoutineManager::lockScreen()
{
    // Idempotent: bail if we're already locked. Critical because backend
    // lockScreen() runs `loginctl lock-session`, which makes logind emit the
    // session's "Lock" signal — and that signal is wired straight back to this
    // slot (see main.cpp). Without this guard the echo re-enters here, re-issues
    // lock-session + `kscreen-doctor --dpms off`, and the panel cycles on/off
    // forever (it never stays lit long enough to accept the unlock input).
    if (m_screenLocked) {
        return;
    }
    // Flip state first so the re-entrant Lock-signal call short-circuits above
    // before it can touch the backend again.
    m_screenLocked = true;
    emit screenLockedChanged();
    if (m_backend) {
        // Best-effort: physically turn the panel off where the platform can.
        m_backend->lockScreen();
    }
}

void RoutineManager::sleepDisplay()
{
    if (m_backend) {
        m_backend->sleepDisplay();
    }
}

void RoutineManager::handlePrepareForSleep(bool aboutToSleep)
{
    if (aboutToSleep) {
        return; // Going down — the deep-idle path already blanked + muted.
    }
    // Just resumed. The deep-idle state machine only wakes the panel on
    // deepIdleChanged(false), which needs a Qt input event; a lid/power resume
    // is not one, so the display could stay DPMS-off — and any apps frozen for the
    // deep-idle sleep would stay SIGSTOP'd — until the user happens to touch the
    // trackpad. Light the panel back up and thaw the frozen apps here. Both are
    // idempotent, so this is harmless if the input-driven path already ran.
    if (m_backend) {
        m_backend->wakeDisplay();
        m_backend->thawBackgroundProcesses();
    }
}

void RoutineManager::unlockScreen()
{
    if (!m_screenLocked) {
        return;
    }
    m_screenLocked = false;
    if (m_backend) {
        m_backend->unlockScreen();
    }
    emit screenLockedChanged();
}

bool RoutineManager::accessGranted() const
{
    return m_accessRemainingSeconds > 0;
}

int RoutineManager::accessRemainingSeconds() const
{
    return m_accessRemainingSeconds;
}

QString RoutineManager::accessStatus() const
{
    if (!accessGranted()) {
        return {};
    }
    return QStringLiteral("ACCESS GRANTED — SESSION EXPIRES IN %1").arg(formatDuration(m_accessRemainingSeconds).mid(3));
}

int RoutineManager::otherAccessMinutes() const
{
    return m_otherAccessMinutes;
}

bool RoutineManager::sessionPromptVisible() const
{
    return m_sessionPromptVisible;
}

QString RoutineManager::finishedSessionName() const
{
    return m_finishedSessionName;
}

int RoutineManager::finishedSessionMinutes() const
{
    return m_finishedSessionMinutes;
}

QString RoutineManager::finishedSessionResult() const
{
    return m_finishedSessionResult;
}

bool RoutineManager::paused() const
{
    return m_routineTimer.paused() || m_openEndedPaused;
}

bool RoutineManager::editMode() const
{
    return m_editMode;
}

void RoutineManager::setEditMode(bool enabled)
{
    if (m_editMode == enabled) {
        return;
    }
    m_editMode = enabled;
    emit editModeChanged();
}

int RoutineManager::pauseMode() const
{
    // Open-ended momentum tracks its own pause flag (no countdown timer to pause).
    if (m_openEnded) {
        if (!m_openEndedPaused) {
            return 0;
        }
        return m_manualPause ? 2 : 1;
    }
    if (!m_routineTimer.paused()) {
        return 0;
    }
    return m_manualPause ? 2 : 1;
}

void RoutineManager::togglePause()
{
    if (!active()) {
        return;
    }
    if (paused()) {
        // A single click resumes from either pause mode (this is how the user
        // manually unpauses a manual pause).
        resumeRoutine();
        return;
    }
    // Single click on a running timer = idle pause: it auto-resumes the moment
    // the user comes back (keyboard / window focus), so stepping away briefly
    // doesn't require remembering to unpause. Idleness itself never pauses —
    // this is always a deliberate click.
    m_manualPause = false;
    if (m_openEnded) {
        // Open-ended momentum keeps no checkpoint (see continueFinishedSession),
        // so its pause is in-memory only — don't write active.json here.
        m_openEndedPaused = true;
        emit pausedChanged();
        emit pauseModeChanged();
        return;
    }
    m_routineTimer.pause();
    emit pauseModeChanged();
    writeActiveSession(true);
}

void RoutineManager::manualPause()
{
    if (!active()) {
        return;
    }
    // Double click = manual pause: never auto-resumes. Upgrades an existing idle
    // pause in place. The persistent banner (QML) makes sure the user can't
    // forget it's paused — that's how logged time used to get lost.
    if (m_openEnded) {
        // In-memory only — open-ended momentum has no checkpoint to update.
        if (!m_openEndedPaused) {
            m_openEndedPaused = true;
            emit pausedChanged();
        }
        m_manualPause = true;
        emit pauseModeChanged();
        return;
    }
    if (!m_routineTimer.paused()) {
        m_routineTimer.pause();
    }
    if (!m_manualPause) {
        m_manualPause = true;
    }
    emit pauseModeChanged();
    writeActiveSession(true);
}

void RoutineManager::resumeRoutine()
{
    if (!active() || !paused()) {
        return;
    }
    m_manualPause = false;
    if (m_openEnded) {
        m_openEndedPaused = false;
        emit pausedChanged();
        emit pauseModeChanged();
        return;
    }
    m_routineTimer.resume();
    emit pauseModeChanged();
    writeActiveSession(true);
}

void RoutineManager::onResumeHint()
{
    // Idle pause only: a manual pause is deliberate and stays put.
    if (active() && paused() && !m_manualPause) {
        resumeRoutine();
    }
}

int RoutineManager::routineCount() const
{
    return m_routines.size();
}

bool RoutineManager::desktopShellSupported() const
{
    return m_backend && m_backend->desktopShellSupported();
}

bool RoutineManager::desktopShellRunning() const
{
    return m_desktopShellRunning;
}

bool RoutineManager::activeRoutineHasLaunchTargets() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0) {
        return false;
    }
    const Routine &routine = m_routines.at(routineIndex);
    return !routine.apps.isEmpty() || !routine.allowedUrls.isEmpty();
}

QString RoutineManager::statusMessage() const
{
    return m_statusMessage;
}

bool RoutineManager::networkLockPromptVisible() const
{
    return !m_pendingNetworkRoutineId.isEmpty();
}

QString RoutineManager::networkLockError() const
{
    return m_networkLockError;
}

QString RoutineManager::networkLockRoutineName() const
{
    return m_networkLockRoutineName;
}

QStringList RoutineManager::alwaysAllowedApps() const
{
    return m_alwaysAllowedApps;
}

bool RoutineManager::addAlwaysAllowedApp(const QString &commandLine)
{
    const QString trimmed = commandLine.trimmed();
    if (trimmed.isEmpty() || m_alwaysAllowedApps.contains(trimmed)) {
        return false;
    }
    m_alwaysAllowedApps.append(trimmed);
    saveConfig();
    if (m_backend) {
        m_backend->setAlwaysAllowedApps(m_alwaysAllowedApps);
    }
    emit alwaysAllowedAppsChanged();
    return true;
}

void RoutineManager::removeAlwaysAllowedApp(int index)
{
    if (index < 0 || index >= m_alwaysAllowedApps.size()) {
        return;
    }
    m_alwaysAllowedApps.removeAt(index);
    saveConfig();
    if (m_backend) {
        m_backend->setAlwaysAllowedApps(m_alwaysAllowedApps);
    }
    emit alwaysAllowedAppsChanged();
}

bool RoutineManager::sessionRecoverySupported() const
{
    return m_backend &&
           (QFileInfo::exists(QStringLiteral("/usr/local/lib/focusos/focusos-restore-sessions.sh")) ||
            QFileInfo::exists(QStringLiteral("/opt/focusos/lib/focusos-restore-sessions.sh")));
}

bool RoutineManager::restoreLoginSessions()
{
    // TOTP gate: the SYSTEM tab lives behind the unlock modal, and unlocking
    // grants the admin access window. Refuse recovery unless that window is
    // open so the six-digit code is the sole path out of the FocusOS-only
    // session.
    if (!accessGranted()) {
        setStatusMessage(QStringLiteral("UNLOCK SETTINGS WITH YOUR CODE FIRST"));
        return false;
    }
    if (!m_backend) {
        return false;
    }
    QString error;
    if (!m_backend->restoreLoginSessions(&error)) {
        setStatusMessage(error.isEmpty()
                             ? QStringLiteral("SESSION RECOVERY FAILED")
                             : error.toUpper());
        return false;
    }
    setStatusMessage(QStringLiteral("OTHER SESSIONS RESTORED — LOG OUT TO SWITCH"));
    return true;
}

QStringList RoutineManager::previewBackgroundAppQuit() const
{
    if (!m_backend) {
        return {};
    }
    // Preview the worst case: no routine apps yet (those would additionally be
    // spared), only the always-allowed list. Anything session-critical that
    // shows up here is something to add to the always-allowed list first.
    return m_backend->previewBackgroundAppQuit(m_alwaysAllowedApps);
}

void RoutineManager::endActiveRoutine()
{
    if (!active()) {
        return;
    }

    // Open-ended momentum (Task 5): no min-time floor, no extra record (the
    // session was logged at expiry). Just stand down to the console.
    if (m_openEnded) {
        m_openEnded = false;
        const bool wasPaused = m_openEndedPaused;
        m_openEndedPaused = false;
        m_manualPause = false;
        m_activeRoutineId.clear();
        m_activeStartedAt = {};
        if (m_backend) {
            // Open-ended momentum carried the lockdown sweep over from the
            // original routine; stand it down now so launchers work at home.
            m_backend->endRoutineLockdown();
            m_backend->restoreShellPlacement();
        }
        updateDisplaySleepInhibit();
        if (wasPaused) {
            emit pausedChanged();
            emit pauseModeChanged();
        }
        emit activeChanged();
        emitRowsChanged();
        return;
    }

    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0) {
        return;
    }
    const Routine &routine = m_routines.at(routineIndex);
    const int elapsedSecondsValue = qMax(0, routine.timeLimitMinutes * 60 - m_routineTimer.remainingSeconds());
    const int elapsedMinutes = elapsedSecondsValue <= 0 ? 0 : (elapsedSecondsValue + 59) / 60;

    // Strict mode: the END button cannot release the routine before
    // min_time_minutes has elapsed. The escape hatch is Settings access (TOTP),
    // which bypasses this floor (see unlockOtherAccess).
    const int minSeconds = routine.minTimeMinutes * 60;
    if (minSeconds > 0 && elapsedSecondsValue < minSeconds) {
        const int remaining = minSeconds - elapsedSecondsValue;
        const int remainingMinutes = (remaining + 59) / 60;
        setStatusMessage(QStringLiteral("MIN-TIME LOCK ACTIVE — %1 MIN REMAINING").arg(remainingMinutes));
        return;
    }

    if (m_backend) {
        m_backend->dropNetworkPolicy();
        // Stand down the launcher-killing sweep — the routine is over and the
        // finish prompt is a FocusOS-owned screen.
        m_backend->endRoutineLockdown();
        // Bring the user back to FocusOS's home workspace so the completion
        // prompt isn't sitting on top of the Focus apps the user just left.
        m_backend->restoreShellPlacement();
    }
    emitActiveSessionProgress();
    emit routineSessionFinished(routine.id,
                                routine.name,
                                elapsedMinutes,
                                QStringLiteral("unlocked"),
                                m_activeStartedAt,
                                QDateTime::currentDateTimeUtc());
    setFinishedSessionPrompt(routine, elapsedMinutes, QStringLiteral("unlocked"));

    // Legitimate end past the min-time floor: tear down the checkpoint so the
    // watchdog stops respawning and a fresh launch won't re-lock.
    clearActiveSession();

    // Clear active state BEFORE stopping the timer to avoid the phantom-progress race.
    m_activeRoutineId.clear();
    m_activeStartedAt = {};
    m_routineTimer.stop();
    updateDisplaySleepInhibit();
    emit activeChanged();
    emitRowsChanged();
}

void RoutineManager::closeOtherAccess()
{
    if (!accessGranted()) {
        return;
    }
    finishOtherAccess();
}

void RoutineManager::launchDesktopShell()
{
    if (!m_backend || !m_backend->desktopShellSupported() || !accessGranted()) {
        return;
    }
    // Single-instance guard: if we've already brought the desktop shell (KDE
    // Plasma) up this session, don't launch it again — just get FocusOS out of
    // the way so the existing session comes forward. This prevents a rapid
    // second "Access Desktop" (before plasmashell shows up in pgrep) from
    // racing a duplicate Plasma launch. The backend has its own processRunning
    // guard too, but this avoids even attempting the relaunch.
    if (m_desktopShellRunning) {
        emit desktopAccessRequested();
        return;
    }
    QString error;
    if (m_backend->launchDesktopShell(&error)) {
        m_desktopShellRunning = true;
        emit desktopShellChanged();
    } else if (!error.isEmpty()) {
        setStatusMessage(error);
    }
    emit desktopAccessRequested();
}

void RoutineManager::relaunchActiveRoutine()
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0 || !m_backend) {
        return;
    }

    const Routine routine = m_routines.at(routineIndex);
    // Terminate the routine's previous instances (browsers, IDEs, etc.) before
    // relaunching them — otherwise the user ends up with duplicate windows.
    m_backend->terminateApps(routine.apps);
    setStatusMessage(QStringLiteral("RELAUNCHING ROUTINE WINDOWS…"));

    // Give the WM a beat to actually reap the windows before we respawn them,
    // otherwise the second launch may race the first's teardown.
    QTimer::singleShot(700, this, [this, routine]() {
        if (m_activeRoutineId != routine.id) {
            return;
        }
        QString error;
        if (!launchRoutineTargets(routine, &error) && !error.isEmpty()) {
            setStatusMessage(error);
        } else {
            setStatusMessage(QStringLiteral("ROUTINE WINDOWS RELAUNCHED"));
        }
    });
}

void RoutineManager::setOtherAccessMinutes(int minutes)
{
    const int clamped = qBound(1, minutes, 24 * 60);
    if (m_otherAccessMinutes == clamped) {
        return;
    }

    m_otherAccessMinutes = clamped;
    saveConfig();
    emit configChanged();
}

bool RoutineManager::overlayProgressEnabled() const
{
    return m_overlayProgressEnabled;
}

void RoutineManager::setOverlayProgressEnabled(bool enabled)
{
    if (m_overlayProgressEnabled == enabled) {
        return;
    }

    m_overlayProgressEnabled = enabled;
    saveConfig();
    emit overlayProgressEnabledChanged();
}

bool RoutineManager::deepSleepSuspend() const
{
    return m_deepSleepSuspend;
}

void RoutineManager::setDeepSleepSuspend(bool enabled)
{
    if (m_deepSleepSuspend == enabled) {
        return;
    }

    m_deepSleepSuspend = enabled;
    saveConfig();
    emit configChanged();
}

void RoutineManager::engage(const QString &routineId)
{
    qInfo() << "[engage] engage() called, routineId=" << routineId
            << "active=" << active() << "accessGranted=" << accessGranted()
            << "m_engaging=" << m_engaging;
    // m_engaging guards the async window between "network lock requested" and the
    // resolver callback firing, so a second click can't start a parallel engage.
    if (active() || accessGranted() || m_engaging) {
        return;
    }
    clearFinishedSessionPrompt();
    clearNetworkLockPrompt();

    const int routineIndex = indexOfRoutine(routineId);
    if (routineIndex < 0) {
        return;
    }

    // Copy the routine — the engage may complete in an async callback, by which
    // point m_routines could have been reloaded (the reference would dangle).
    const Routine routine = m_routines.at(routineIndex);
    // Full-internet-access routines deliberately skip the outbound allowlist.
    // The TOTP gate that authorizes them is enforced in QML before engage() is
    // ever called (see ActivitiesPanel / Main fullAccessPrompt).
    const bool restrictNetwork = routine.networkLock && !routine.fullAccess;
    // Only routines that DON'T need a web browser get the full system-wide
    // (pf / nftables) egress block. A browser routine is gated by the blocker
    // extension's URL allowlist instead, so a system-wide block would be
    // redundant and would break allowed sites' off-host subresources.
    const bool applyNetworkLock = restrictNetwork && !routineRequiresBrowser(routine);

    if (restrictNetwork && !applyNetworkLock && m_backend) {
        // Browser routine: enforce the allowlist in the extension only (fast —
        // no DNS), then engage inline. dropNetworkPolicy() clears it at end.
        m_backend->applyBrowserBlockerPolicy(routine.allowedUrls);
        QString error;
        if (!finishEngage(routine, /*networkApplied=*/true, &error)) {
            setStatusMessage(error.isEmpty() ? QStringLiteral("ROUTINE START FAILED") : error);
        }
        return;
    }

    if (!applyNetworkLock || !m_backend) {
        // No network lock (or no backend): nothing slow to do — engage inline.
        QString error;
        if (!finishEngage(routine, /*networkApplied=*/false, &error)) {
            setStatusMessage(error.isEmpty() ? QStringLiteral("ROUTINE START FAILED") : error);
        }
        return;
    }

    // Resolve DNS + apply the firewall OFF the GUI thread so engage doesn't
    // freeze the shell. Routine apps only launch once the lock is confirmed up.
    m_engaging = true;
    setStatusMessage(QStringLiteral("APPLYING NETWORK LOCK…"));
    m_backend->applyNetworkPolicyAsync(routine.allowedUrls,
        [this, routine](bool ok, const QString &networkError) {
            m_engaging = false;
            if (active() || accessGranted()) {
                // State changed under us while resolving (shouldn't normally
                // happen) — undo the lock we just applied and bail.
                if (ok && m_backend) {
                    m_backend->dropNetworkPolicy();
                }
                return;
            }
            if (!ok) {
                setNetworkLockPrompt(routine, networkError.trimmed());
                return;
            }
            QString error;
            if (!finishEngage(routine, /*networkApplied=*/true, &error)) {
                if (m_backend) {
                    m_backend->dropNetworkPolicy();
                }
                setStatusMessage(error.isEmpty() ? QStringLiteral("ROUTINE LAUNCH FAILED") : error);
            }
        });
}

void RoutineManager::abortPendingRoutineStart()
{
    clearNetworkLockPrompt();
    setStatusMessage(QStringLiteral("ROUTINE START ABORTED"));
}

void RoutineManager::unlockOtherAccess()
{
    clearFinishedSessionPrompt();

    // Settings access (TOTP) is the authenticated escape hatch: a correct code
    // releases the session regardless of min_time_minutes. The min-time floor
    // still applies to the ordinary END button (see endActiveRoutine) — proving
    // you hold the TOTP secret is what buys the early exit.

    if (m_backend) {
        m_backend->dropNetworkPolicy();
        // Admin access is full control: stop the launcher-killing sweep so the
        // "Access Desktop" path (plasmashell/krunner/terminal) isn't pkilled the
        // instant it comes up.
        m_backend->endRoutineLockdown();
    }

    if (active()) {
        // TOTP unlock past the min-time floor counts as a legitimate end —
        // retire the checkpoint so the respawn watchdog releases.
        clearActiveSession();
        // Open-ended momentum was already recorded at expiry; don't write a
        // phantom "unlocked" record for it (its timer reads remaining=0).
        const int routineIndex = m_openEnded ? -1 : indexOfRoutine(m_activeRoutineId);
        m_openEnded = false;
        if (routineIndex >= 0) {
            const Routine &routine = m_routines.at(routineIndex);
            const int elapsedSeconds = qMax(0, routine.timeLimitMinutes * 60 - m_routineTimer.remainingSeconds());
            const int elapsedMinutes = elapsedSeconds <= 0 ? 0 : (elapsedSeconds + 59) / 60;
            emitActiveSessionProgress();
            emit routineSessionFinished(routine.id,
                                        routine.name,
                                        elapsedMinutes,
                                        QStringLiteral("unlocked"),
                                        m_activeStartedAt,
                                        QDateTime::currentDateTimeUtc());
        }
        // Clear active state BEFORE stopping the timer — stop emits
        // remainingSecondsChanged, and our handler would otherwise treat
        // remaining=0 as a full elapsed session and write a phantom record.
        m_activeRoutineId.clear();
        m_activeStartedAt = {};
        m_routineTimer.stop();
        updateDisplaySleepInhibit();
        emit activeChanged();
        emitRowsChanged();
    }

    m_accessRemainingSeconds = qMax(1, m_otherAccessMinutes) * 60;
    // Arm the inactivity auto-lock; notifyActivity() re-starts it on input.
    m_inactivityTimer.start();
    // The terminal used to pop here, but on Linux it stole focus from the
    // admin modal and made the user think the ROUTINES tab disappeared. On
    // Linux the user opens the terminal via the Access Desktop button now.
    // macOS still pops Terminal because it has no separate desktop shell path.
    if (m_backend && !m_backend->desktopShellSupported()) {
        QString error;
        m_backend->openSystemTerminal(&error);
    }
    m_accessTimer.start();
    emit accessChanged();
    emitRowsChanged();
}

void RoutineManager::notifyActivity()
{
    // Only the unlock panel's auto-lock cares about activity; re-arm it on input
    // while access is granted. (Idle when locked is harmless — nothing to lock.)
    if (accessGranted()) {
        m_inactivityTimer.start();
    }
}

bool RoutineManager::signOutSupported() const
{
    return m_backend && m_backend->signOutSupported();
}

void RoutineManager::signOut()
{
    // Tear down any routine state first so the respawn watchdog doesn't fight
    // the logout: drop the network policy and retire the active checkpoint.
    if (m_backend) {
        m_backend->dropNetworkPolicy();
    }
    clearActiveSession();
    m_inactivityTimer.stop();
    m_accessTimer.stop();

    if (!m_backend) {
        return;
    }
    QString error;
    if (!m_backend->signOut(&error) && !error.isEmpty()) {
        setStatusMessage(error);
    }
}

bool RoutineManager::persistentKioskSupported() const
{
    return m_backend && m_backend->persistentKioskSupported();
}

bool RoutineManager::persistentKioskEnabled() const
{
    return m_backend && m_backend->persistentKioskEnabled();
}

QString RoutineManager::setPersistentKiosk(bool enabled)
{
    if (!m_backend) {
        return QStringLiteral("Unsupported on this platform.");
    }
    QString error;
    const bool ok = m_backend->setPersistentKiosk(enabled, &error);
    emit persistentKioskChanged();
    if (!ok && !error.isEmpty()) {
        setStatusMessage(error);
        return error;
    }
    return {};
}

void RoutineManager::quitFocusOS()
{
    // Mirror signOut()'s teardown so a quit never strands the machine behind the
    // network lock or leaves a checkpoint the watchdog would respawn from.
    if (m_backend) {
        m_backend->dropNetworkPolicy();
    }
    clearActiveSession();
    m_inactivityTimer.stop();
    m_accessTimer.stop();

    if (m_backend) {
        // Stand down the respawn agents (login agent + watchdog) for this session.
        // If FocusOS is itself the login-agent instance this terminates us via
        // launchd (the crash-cleanup signal handler then restores the system UI);
        // otherwise the quit() below does it. The login agent stays installed, so
        // FocusOS still launches at the next login while the kiosk is enabled.
        m_backend->prepareForAuthorizedQuit();
    }
    QCoreApplication::quit();
}

void RoutineManager::continueFinishedSession()
{
    // Task 5 — "Continue" after the timer expires keeps the momentum: re-enter
    // an open-ended active state (no countdown) instead of dropping back to the
    // routine list. The routine's apps are left running; streak/stats already
    // carry over (the completed session was recorded at expiry). END EARLY
    // leaves this state.
    const QString routineId = m_finishedRoutineId;
    clearFinishedSessionPrompt();

    if (routineId.isEmpty() || active() || accessGranted()) {
        return;
    }
    if (indexOfRoutine(routineId) < 0) {
        return;
    }

    m_openEnded = true;
    m_openEndedPaused = false;
    m_manualPause = false;
    m_activeRoutineId = routineId;
    m_activeStartedAt = QDateTime::currentDateTimeUtc();
    // No checkpoint / respawn watchdog: open-ended momentum is not a strict
    // routine, so a crash simply returns to the console rather than re-locking.
    updateDisplaySleepInhibit();
    emit activeChanged();
    emitRowsChanged();
}

bool RoutineManager::shouldFocusAppsOnContinue() const
{
    // Only yield focus when the continued routine actually has a launch target
    // (so it's a "digital app" routine, not a reading/no-app one) AND at least one
    // of those windows is still alive. Otherwise the shell stays in front.
    return activeRoutineHasLaunchTargets() && m_backend && m_backend->hasLiveRoutineApps();
}

void RoutineManager::quitFinishedSession()
{
    if (m_backend && !m_finishedSessionApps.isEmpty()) {
        m_backend->terminateApps(m_finishedSessionApps);
    }
    clearFinishedSessionPrompt();
}

QVariantList RoutineManager::routinesForEditing() const
{
    QVariantList routines;
    routines.reserve(m_routines.size());
    for (const Routine &routine : m_routines) {
        QVariantMap object;
        object.insert(QStringLiteral("id"), routine.id);
        object.insert(QStringLiteral("name"), routine.name);
        object.insert(QStringLiteral("description"), routine.description);
        object.insert(QStringLiteral("apps"), routine.apps);
        object.insert(QStringLiteral("allowed_urls"), routine.allowedUrls);
        object.insert(QStringLiteral("access_folder"), routine.accessFolder);
        object.insert(QStringLiteral("access_desktop"), routine.accessDesktop);
        object.insert(QStringLiteral("access_documents"), routine.accessDocuments);
        object.insert(QStringLiteral("access_downloads"), routine.accessDownloads);
        object.insert(QStringLiteral("browsable"), routine.browsable);
        object.insert(QStringLiteral("time_limit_minutes"), routine.timeLimitMinutes);
        object.insert(QStringLiteral("min_time_minutes"), routine.minTimeMinutes);
        object.insert(QStringLiteral("network_lock"), routine.networkLock);
        object.insert(QStringLiteral("full_access"), routine.fullAccess);
        object.insert(QStringLiteral("break_frequency_minutes"), routine.breakFrequencyMinutes);
        object.insert(QStringLiteral("break_duration_minutes"), routine.breakDurationMinutes);
        object.insert(QStringLiteral("keep_display_awake"), routine.keepDisplayAwake);
        object.insert(QStringLiteral("music_behavior"), routine.musicBehavior);
        routines.append(object);
    }
    return routines;
}

bool RoutineManager::saveRoutines(const QVariantList &routines)
{
    QJsonArray array;
    QSet<QString> usedIds;

    for (const QVariant &value : routines) {
        const QVariantMap object = value.toMap();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            continue;
        }

        QString baseId = routineIdFromName(object.value(QStringLiteral("id")).toString());
        if (baseId.isEmpty()) {
            baseId = routineIdFromName(name);
        }
        if (baseId.isEmpty()) {
            baseId = QStringLiteral("routine");
        }

        QString id = baseId;
        int suffix = 2;
        while (usedIds.contains(id)) {
            id = QStringLiteral("%1_%2").arg(baseId).arg(suffix++);
        }
        usedIds.insert(id);

        // Validate/normalize the editor payload into a Routine, then serialize
        // through the shared encoder so this and persistRoutines() stay identical.
        Routine routine;
        routine.id = id;
        routine.name = name;
        routine.description = object.value(QStringLiteral("description")).toString().trimmed();
        routine.apps = variantToStringList(object.value(QStringLiteral("apps")));
        routine.allowedUrls = variantToStringList(object.value(QStringLiteral("allowed_urls")));
        routine.accessFolder = object.value(QStringLiteral("access_folder")).toString().trimmed();
        routine.accessDesktop = object.value(QStringLiteral("access_desktop")).toBool();
        routine.accessDocuments = object.value(QStringLiteral("access_documents")).toBool();
        routine.accessDownloads = object.value(QStringLiteral("access_downloads")).toBool();
        routine.browsable = object.value(QStringLiteral("browsable")).toBool();
        routine.timeLimitMinutes = qMax(1, intFromVariant(object.value(QStringLiteral("time_limit_minutes")), 60));
        routine.minTimeMinutes = qMax(0, intFromVariant(object.value(QStringLiteral("min_time_minutes")), 0));
        routine.networkLock = object.value(QStringLiteral("network_lock"), true).toBool();
        routine.fullAccess = object.value(QStringLiteral("full_access"), false).toBool();
        routine.breakFrequencyMinutes = qMax(0, intFromVariant(object.value(QStringLiteral("break_frequency_minutes")), 0));
        routine.breakDurationMinutes = qMax(0, intFromVariant(object.value(QStringLiteral("break_duration_minutes")), 0));
        routine.keepDisplayAwake = object.value(QStringLiteral("keep_display_awake"), true).toBool();
        routine.musicBehavior = normalizedMusicBehavior(object.value(QStringLiteral("music_behavior")).toString());
        array.append(serializeRoutine(routine));
    }

    QJsonObject root;
    root.insert(QStringLiteral("routines"), array);

    QSaveFile file(routinesPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatusMessage(QStringLiteral("ROUTINE SAVE FAILED"));
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setStatusMessage(QStringLiteral("ROUTINE SAVE FAILED"));
        return false;
    }

    loadRoutines();
    setStatusMessage(QStringLiteral("ROUTINES SAVED"));
    return true;
}

bool RoutineManager::updateRoutineDescription(const QString &routineId, const QString &description)
{
    const int routineIndex = indexOfRoutine(routineId);
    if (routineIndex < 0) {
        return false;
    }

    const QString trimmed = description.trimmed();
    if (m_routines[routineIndex].description == trimmed) {
        return true;
    }
    m_routines[routineIndex].description = trimmed;

    if (!persistRoutines()) {
        return false;
    }

    emit dataChanged(index(routineIndex, 0), index(routineIndex, 0), { DescriptionRole });
    return true;
}

// Serialize the in-memory routines back to routines.json. Shared by the
// description editor, the per-routine display-sleep toggle and the live
// music-behavior toggle, so all keep the full routine record intact on disk.
// Uses serializeRoutine() — the same encoder saveRoutines() uses — so a live
// toggle and an editor save can never write differently-shaped files. Both
// writers commit through QSaveFile (atomic rename), and everything here runs on
// the GUI thread, so a toggle firing alongside an editor save can't tear the
// file: the two writes are ordered by the event loop and the loser is simply
// overwritten wholesale by the winner, never interleaved.
bool RoutineManager::persistRoutines() const
{
    QJsonArray array;
    for (const Routine &routine : m_routines) {
        array.append(serializeRoutine(routine));
    }

    QJsonObject root;
    root.insert(QStringLiteral("routines"), array);

    QSaveFile file(routinesPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool RoutineManager::displayStaysAwake() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).keepDisplayAwake : true;
}

void RoutineManager::setDisplayStaysAwake(bool stayAwake)
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0 || m_routines[routineIndex].keepDisplayAwake == stayAwake) {
        return;
    }
    m_routines[routineIndex].keepDisplayAwake = stayAwake;
    persistRoutines();
    updateDisplaySleepInhibit();
}

// Hold the display awake while a routine that asks for it is active; release
// otherwise. Emits displayStaysAwakeChanged so the bottom-bar toggle reflects
// the active routine's stored preference.
void RoutineManager::updateDisplaySleepInhibit()
{
    if (m_backend) {
        const int routineIndex = indexOfRoutine(m_activeRoutineId);
        const bool inhibit = routineIndex >= 0 && m_routines.at(routineIndex).keepDisplayAwake;
        m_backend->setDisplaySleepInhibited(inhibit);
    }
    emit displayStaysAwakeChanged();
}

QString RoutineManager::pickApplication()
{
    // UI lives in FilePicker; the manager only relays the validation hint to its
    // status line (the one bit of domain coupling the picker can't own).
    const FilePicker::AppResult result = FilePicker::pickApplication();
    if (!result.error.isEmpty()) {
        setStatusMessage(result.error);
    }
    return result.path;
}

QString RoutineManager::pickFile()
{
    return FilePicker::pickFile();
}

QString RoutineManager::pickFolder()
{
    return FilePicker::pickFolder();
}

QList<QPair<QString, QString>> RoutineManager::browseRootsInternal() const
{
    // The standard folders are opt-in: a routine only exposes the ones it enabled.
    // The custom access folder, when set, is always browsable. Canonical paths so
    // the jail check (isWithinBrowseRoots) compares apples to apples and symlinked
    // roots resolve to their real targets.
    QList<QPair<QString, QString>> roots;
    auto add = [&roots](const QString &name, const QString &dir) {
        if (dir.isEmpty()) {
            return;
        }
        const QString canonical = QFileInfo(dir).canonicalFilePath();
        if (canonical.isEmpty()) {
            return; // doesn't exist / unreadable
        }
        for (const auto &existing : roots) {
            if (existing.second == canonical) {
                return; // de-dupe (e.g. a custom folder that is also Documents)
            }
        }
        roots.append({name, canonical});
    };

    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0) {
        return roots; // no active routine — nothing is browsable
    }
    const Routine &routine = m_routines.at(routineIndex);

    if (routine.accessDesktop) {
        add(QStringLiteral("DESKTOP"),
            QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    }
    if (routine.accessDocuments) {
        add(QStringLiteral("DOCUMENTS"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }
    if (routine.accessDownloads) {
        add(QStringLiteral("DOWNLOADS"),
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    }

    const QString folder = routine.accessFolder.trimmed();
    if (!folder.isEmpty()) {
        add(QFileInfo(folder).fileName().toUpper(), folder);
    }
    return roots;
}

bool RoutineManager::isWithinBrowseRoots(const QString &path) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) {
        return false;
    }
    const auto roots = browseRootsInternal();
    for (const auto &root : roots) {
        if (canonical == root.second
                || canonical.startsWith(root.second + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

QVariantList RoutineManager::browseRoots() const
{
    QVariantList list;
    const auto roots = browseRootsInternal();
    for (const auto &root : roots) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), root.first);
        entry.insert(QStringLiteral("path"), root.second);
        list.append(entry);
    }
    return list;
}

QVariantList RoutineManager::listFolder(const QString &path) const
{
    QVariantList list;
    if (!isWithinBrowseRoots(path)) {
        return list; // refuse anything outside the jail
    }

    QDir dir(path);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name | QDir::DirsFirst | QDir::IgnoreCase);
    for (const QFileInfo &info : entries) {
        if (info.isHidden()) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), info.fileName());
        entry.insert(QStringLiteral("path"), info.absoluteFilePath());
        entry.insert(QStringLiteral("isDir"), info.isDir());
        entry.insert(QStringLiteral("suffix"), info.suffix().toLower());
        list.append(entry);
    }
    return list;
}

QString RoutineManager::openFileInSession(const QString &path)
{
    if (!m_backend) {
        return {};
    }

    // Jail first: the browser only ever hands us paths it listed, but a hostile or
    // stale path must not slip past — only files inside an allowed root open.
    if (!isWithinBrowseRoots(path)) {
        setStatusMessage(QStringLiteral("THAT FILE IS OUTSIDE THE ALLOWED FOLDERS"));
        return {};
    }

    // Guard the distraction-free environment: opening *documents* is the point,
    // but an executable or a .desktop entry would let this become a launcher —
    // exactly what the lockdown watchdog kills file managers to prevent. Refuse
    // those so the door stays only as wide as "open my file".
    const QFileInfo info(path);
    if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0
            || info.isExecutable()) {
        setStatusMessage(QStringLiteral("ONLY DOCUMENTS CAN BE OPENED HERE — NOT APPS OR EXECUTABLES"));
        return {};
    }

    QString error;
    if (!m_backend->launchApps({path}, &error)) {
        setStatusMessage(error.isEmpty() ? QStringLiteral("COULDN'T OPEN THAT FILE") : error);
        return {};
    }

    setStatusMessage(QStringLiteral("OPENED %1").arg(info.fileName()));
    return path;
}

QString RoutineManager::openDocumentInSession()
{
    if (!m_backend) {
        return {};
    }

    const QString path = pickFile();
    if (path.isEmpty()) {
        return {}; // cancelled — no message, the user backed out deliberately.
    }

    // DELIBERATELY NOT jailed to isWithinBrowseRoots(), unlike openFileInSession.
    // That asymmetry is intentional, not an oversight: openFileInSession serves
    // the in-app *browser*, which hands us paths programmatically (a hostile or
    // stale path must be confined to the routine's opt-in roots). This path is
    // the native "OPEN DOC" picker — the human physically navigates and chooses a
    // single file, so the jail would only stop them reaching their own documents,
    // not stop an attacker. The file opens in its default *viewer*, never a
    // launcher, so it is not an escape hatch; the executable/.desktop guard below
    // keeps it that way. (Mid-session doc access is passcode-free by design; only
    // the browser is TOTP-gated.)
    //
    // Same launcher guard as openFileInSession: opening *documents* is the point,
    // but an executable or a .desktop entry would let this become a launcher —
    // exactly what the lockdown watchdog kills file managers to prevent. Refuse
    // those so the door stays only as wide as "open my file".
    const QFileInfo info(path);
    if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0
            || info.isExecutable()) {
        setStatusMessage(QStringLiteral("ONLY DOCUMENTS CAN BE OPENED HERE — NOT APPS OR EXECUTABLES"));
        return {};
    }

    QString error;
    if (!m_backend->launchApps({path}, &error)) {
        setStatusMessage(error.isEmpty() ? QStringLiteral("COULDN'T OPEN THAT FILE") : error);
        return {};
    }

    setStatusMessage(QStringLiteral("OPENED %1").arg(info.fileName()));
    return path;
}

QString RoutineManager::applicationDisplayName(const QString &path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QFileInfo info(trimmed);
    if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0 && info.exists()) {
        QSettings desktopFile(trimmed, QSettings::IniFormat);
        desktopFile.beginGroup(QStringLiteral("Desktop Entry"));
        QString name = desktopFile.value(QStringLiteral("Name")).toString().trimmed();
        if (name.isEmpty()) {
            name = desktopFile.value(QStringLiteral("GenericName")).toString().trimmed();
        }
        desktopFile.endGroup();
        if (!name.isEmpty()) {
            return name;
        }
    }

    if (trimmed.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
        return info.completeBaseName();
    }

    QString name = info.completeBaseName();
    if (name.isEmpty()) {
        name = info.fileName();
    }
    return name.isEmpty() ? trimmed : name;
}

void RoutineManager::loadConfig()
{
    const QJsonObject root = readConfigObject();
    const int minutes = root.value(QStringLiteral("other_access_minutes")).toInt(30);
    m_otherAccessMinutes = qBound(1, minutes, 24 * 60);

    m_alwaysAllowedApps.clear();
    const QJsonArray alwaysAllowed = root.value(QStringLiteral("always_allowed_apps")).toArray();
    for (const QJsonValue &value : alwaysAllowed) {
        const QString entry = value.toString().trimmed();
        if (!entry.isEmpty()) {
            m_alwaysAllowedApps.append(entry);
        }
    }
    if (m_backend) {
        m_backend->setAlwaysAllowedApps(m_alwaysAllowedApps);
    }

    m_overlayProgressEnabled = root.value(QStringLiteral("overlay_progress_enabled")).toBool(true);
    // Default OFF — whole-machine suspend bricks hardware that can't resume.
    m_deepSleepSuspend = root.value(QStringLiteral("deep_sleep_suspend")).toBool(false);

    saveConfig();
    emit configChanged();
    emit alwaysAllowedAppsChanged();
    emit overlayProgressEnabledChanged();
}

bool RoutineManager::saveConfig() const
{
    QJsonObject root = readConfigObject();
    root.insert(QStringLiteral("other_access_minutes"), m_otherAccessMinutes);
    QJsonArray alwaysAllowed;
    for (const QString &entry : m_alwaysAllowedApps) {
        alwaysAllowed.append(entry);
    }
    root.insert(QStringLiteral("always_allowed_apps"), alwaysAllowed);
    root.insert(QStringLiteral("overlay_progress_enabled"), m_overlayProgressEnabled);
    root.insert(QStringLiteral("deep_sleep_suspend"), m_deepSleepSuspend);
    QSaveFile saveFile(configPath());
    if (saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        saveFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return saveFile.commit();
    }
    return false;
}

void RoutineManager::loadRoutines()
{
    const QString path = routinesPath();
    if (!QFileInfo::exists(path)) {
        writeDefaultRoutines(path);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || !document.object().contains(QStringLiteral("routines"))) {
        file.close();
        writeDefaultRoutines(path);
        QFile defaultFile(path);
        if (!defaultFile.open(QIODevice::ReadOnly)) {
            return;
        }
        document = QJsonDocument::fromJson(defaultFile.readAll());
    }

    const QJsonArray array = document.object().value(QStringLiteral("routines")).toArray();

    beginResetModel();
    m_routines.clear();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Routine routine;
        routine.id = object.value(QStringLiteral("id")).toString().trimmed();
        routine.name = object.value(QStringLiteral("name")).toString().trimmed().toUpper();
        routine.description = object.value(QStringLiteral("description")).toString().trimmed();
        routine.apps = jsonArrayToStringList(object.value(QStringLiteral("apps")).toArray());
        routine.allowedUrls = jsonArrayToStringList(object.value(QStringLiteral("allowed_urls")).toArray());
        routine.accessFolder = object.value(QStringLiteral("access_folder")).toString().trimmed();
        routine.accessDesktop = object.value(QStringLiteral("access_desktop")).toBool();
        routine.accessDocuments = object.value(QStringLiteral("access_documents")).toBool();
        routine.accessDownloads = object.value(QStringLiteral("access_downloads")).toBool();
        routine.browsable = object.value(QStringLiteral("browsable")).toBool();
        routine.timeLimitMinutes = qMax(1, object.value(QStringLiteral("time_limit_minutes")).toInt(60));
        routine.minTimeMinutes = qMax(0, object.value(QStringLiteral("min_time_minutes")).toInt(0));
        routine.networkLock = object.value(QStringLiteral("network_lock")).toBool(true);
        routine.fullAccess = object.value(QStringLiteral("full_access")).toBool(false);
        routine.breakFrequencyMinutes = qMax(0, object.value(QStringLiteral("break_frequency_minutes")).toInt(0));
        routine.breakDurationMinutes = qMax(0, object.value(QStringLiteral("break_duration_minutes")).toInt(0));
        routine.keepDisplayAwake = object.value(QStringLiteral("keep_display_awake")).toBool(true);
        routine.musicBehavior = normalizedMusicBehavior(object.value(QStringLiteral("music_behavior")).toString());
        if (!routine.id.isEmpty() && !routine.name.isEmpty()) {
            m_routines.append(routine);
        }
    }
    endResetModel();
    emit routineCountChanged();
}

void RoutineManager::writeDefaultRoutines(const QString &path) const
{
    QJsonArray routines;

    QJsonObject deepWork;
    deepWork.insert(QStringLiteral("id"), QStringLiteral("deep_work"));
    deepWork.insert(QStringLiteral("name"), QStringLiteral("DEEP WORK"));
    deepWork.insert(QStringLiteral("description"), QStringLiteral("Mission objective. Single problem. No tabs, no inputs, no escape velocity. Output a tangible artifact."));
    deepWork.insert(QStringLiteral("apps"), QJsonArray {});
    deepWork.insert(QStringLiteral("allowed_urls"), QJsonArray {});
    deepWork.insert(QStringLiteral("time_limit_minutes"), 120);
    deepWork.insert(QStringLiteral("min_time_minutes"), 25);
    deepWork.insert(QStringLiteral("network_lock"), true);
    deepWork.insert(QStringLiteral("break_frequency_minutes"), 0);
    deepWork.insert(QStringLiteral("break_duration_minutes"), 0);
    deepWork.insert(QStringLiteral("keep_display_awake"), true);
    routines.append(deepWork);

    QJsonObject research;
    research.insert(QStringLiteral("id"), QStringLiteral("research"));
    research.insert(QStringLiteral("name"), QStringLiteral("RESEARCH"));
    research.insert(QStringLiteral("description"), QStringLiteral("Survey the literature. Build a map of what is known so the next mission lands on solid ground."));
    research.insert(QStringLiteral("apps"), QJsonArray {});
    research.insert(QStringLiteral("allowed_urls"), QJsonArray {
        QStringLiteral("arxiv.org"),
        QStringLiteral("scholar.google.com")
    });
    research.insert(QStringLiteral("time_limit_minutes"), 60);
    research.insert(QStringLiteral("min_time_minutes"), 0);
    research.insert(QStringLiteral("network_lock"), true);
    research.insert(QStringLiteral("break_frequency_minutes"), 0);
    research.insert(QStringLiteral("break_duration_minutes"), 0);
    research.insert(QStringLiteral("keep_display_awake"), true);
    routines.append(research);

    QJsonObject reflection;
    reflection.insert(QStringLiteral("id"), QStringLiteral("reflection"));
    reflection.insert(QStringLiteral("name"), QStringLiteral("REFLECTION + NOTES"));
    reflection.insert(QStringLiteral("description"), QStringLiteral("Decompress. Write down what was learned, what failed, what is still open. The mission log."));
    reflection.insert(QStringLiteral("apps"), QJsonArray {});
    reflection.insert(QStringLiteral("allowed_urls"), QJsonArray {});
    reflection.insert(QStringLiteral("time_limit_minutes"), 30);
    reflection.insert(QStringLiteral("min_time_minutes"), 10);
    reflection.insert(QStringLiteral("network_lock"), true);
    reflection.insert(QStringLiteral("break_frequency_minutes"), 0);
    reflection.insert(QStringLiteral("break_duration_minutes"), 0);
    reflection.insert(QStringLiteral("keep_display_awake"), true);
    routines.append(reflection);

    QJsonObject root;
    root.insert(QStringLiteral("routines"), routines);

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void RoutineManager::writeActiveSession(bool force) const
{
    if (!active()) {
        return;
    }
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0) {
        return;
    }

    // Per-tick writes are throttled to spare the disk; only forced writes (state
    // transitions) and the periodic checkpoint actually hit storage.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && (now - m_lastCheckpointWriteMs) < kCheckpointWriteIntervalMs) {
        return;
    }
    m_lastCheckpointWriteMs = now;

    const Routine &routine = m_routines.at(routineIndex);

    QJsonObject object;
    object.insert(QStringLiteral("routine_id"), routine.id);
    object.insert(QStringLiteral("started_at"), static_cast<qint64>(m_activeStartedAt.toSecsSinceEpoch()));
    object.insert(QStringLiteral("total_seconds"), routine.timeLimitMinutes * 60);
    object.insert(QStringLiteral("remaining_seconds"), m_routineTimer.remainingSeconds());
    object.insert(QStringLiteral("min_time_minutes"), routine.minTimeMinutes);
    object.insert(QStringLiteral("network_lock"), routine.networkLock);
    const QString bootId = currentBootId();
    if (!bootId.isEmpty()) {
        object.insert(QStringLiteral("boot_id"), bootId);
    }
    // Preserve the pause posture so a crash/respawn while paused doesn't silently
    // resume counting time the user didn't intend (Task 4 — don't mislog time).
    object.insert(QStringLiteral("paused"), m_routineTimer.paused());
    object.insert(QStringLiteral("manual_pause"), m_manualPause);

    QSaveFile file(activeSessionPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void RoutineManager::clearActiveSession() const
{
    QFile::remove(activeSessionPath());
}

void RoutineManager::resumeActiveSessionIfPresent()
{
    if (active()) {
        return;
    }

    // Belt-and-suspenders cleanup: if there's no valid session to resume, make
    // sure no firewall/blocker policy is left armed from a crash (the async-safe
    // crash handler tears the nft table down, but the BlockerPolicy file or a
    // partial state could linger). dropNetworkPolicy is idempotent / no-op when
    // nothing is active, so it's safe to run on every clean launch.
    // Genuine fresh launch (nothing to resume): close every other GUI app so
    // FocusOS starts from a clean surface, the same sweep a strict engage does.
    // Only on a fresh launch — the resume path below keeps the routine's own apps
    // open via prepareRoutineSession. macOS-only: the Linux bare session has no
    // user apps to sweep and we don't want to perturb the primary target.
    const auto sweepForFreshLaunch = [this]() {
#if defined(Q_OS_MACOS)
        if (m_backend) {
            m_backend->quitBackgroundApps({});
        }
#endif
    };

    const auto cleanupAndReturn = [this, &sweepForFreshLaunch]() {
        clearActiveSession();
        if (m_backend) {
            m_backend->dropNetworkPolicy();
        }
        sweepForFreshLaunch();
    };

    const QFileInfo checkpointInfo(activeSessionPath());
    QFile file(activeSessionPath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (m_backend) {
            m_backend->dropNetworkPolicy();
        }
        sweepForFreshLaunch();
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        cleanupAndReturn();
        return;
    }

    const QJsonObject object = document.object();
    if (!checkpointBelongsToCurrentBoot(object, checkpointInfo)) {
        // A checkpoint from a previous macOS boot is not a crash recovery signal.
        // Treat it as stale so startup never auto-engages a routine after restart.
        cleanupAndReturn();
        return;
    }

    const int routineIndex = indexOfRoutine(object.value(QStringLiteral("routine_id")).toString());
    if (routineIndex < 0) {
        cleanupAndReturn();
        return;
    }

    const int remaining = object.value(QStringLiteral("remaining_seconds")).toInt(0);
    if (remaining <= 0) {
        // The routine had already run out before the crash/kill — don't
        // resurrect an expired session.
        cleanupAndReturn();
        return;
    }

    const Routine &routine = m_routines.at(routineIndex);

    if (m_backend) {
        // Re-establish the lockdown posture (kills the desktop shell /
        // launchers, starts the lockdown + respawn watchdogs). Routine apps are
        // NOT relaunched — survivors of the crash are still open and the user
        // can use Relaunch if they want fresh windows.
        m_backend->prepareRoutineSession(routine.apps);
        m_backend->startWatchdog(QCoreApplication::applicationFilePath());
        if (routine.networkLock && !routine.fullAccess) {
            if (routineRequiresBrowser(routine)) {
                // Browser routine: re-arm the extension allowlist only — no pf /
                // nftables block (mirrors the engage() decision).
                m_backend->applyBrowserBlockerPolicy(routine.allowedUrls);
            } else {
                // Best-effort, OFF the GUI thread so a slow resolver doesn't
                // freeze the shell coming up after a crash. The timer/commitment
                // resume immediately below regardless; the status line surfaces a
                // failure.
                m_backend->applyNetworkPolicyAsync(routine.allowedUrls,
                    [this](bool ok, const QString &error) {
                        if (!ok && !error.isEmpty()) {
                            setStatusMessage(QStringLiteral("NETWORK LOCK NOT REAPPLIED: %1").arg(error));
                        }
                    });
            }
        }
    }

    m_activeRoutineId = routine.id;
    const qint64 startedAt = object.value(QStringLiteral("started_at")).toVariant().toLongLong();
    m_activeStartedAt = startedAt > 0 ? QDateTime::fromSecsSinceEpoch(startedAt).toUTC()
                                      : QDateTime::currentDateTimeUtc();
    emit activeChanged();
    emitRowsChanged();
    m_routineTimer.start(remaining);

    // Restore a paused posture if the checkpoint was written while paused, so the
    // user comes back to exactly the state they left (and a manual pause keeps
    // its banner and stays put rather than quietly resuming).
    if (object.value(QStringLiteral("paused")).toBool(false)) {
        m_manualPause = object.value(QStringLiteral("manual_pause")).toBool(false);
        m_routineTimer.pause();
        emit pauseModeChanged();
    }

    writeActiveSession(true);
    updateDisplaySleepInhibit();

    const int minutes = (remaining + 59) / 60;
    setStatusMessage(QStringLiteral("ROUTINE RESUMED — %1 MIN REMAINING").arg(minutes));
}

void RoutineManager::onRoutineExpired()
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex >= 0) {
        const Routine &routine = m_routines.at(routineIndex);
        emitActiveSessionProgress();
        emit routineSessionFinished(routine.id,
                                    routine.name,
                                    routine.timeLimitMinutes,
                                    QStringLiteral("completed"),
                                    m_activeStartedAt,
                                    QDateTime::currentDateTimeUtc());
        setFinishedSessionPrompt(routine, routine.timeLimitMinutes, QStringLiteral("completed"));
    }

    if (m_backend) {
        m_backend->dropNetworkPolicy();
        m_backend->endRoutineLockdown();
        m_backend->restoreShellPlacement();
    }
    // Natural expiry is a legitimate end — drop the checkpoint.
    clearActiveSession();
    m_activeRoutineId.clear();
    m_activeStartedAt = {};
    updateDisplaySleepInhibit();
    emit activeChanged();
    emitRowsChanged();
}

void RoutineManager::tickOtherAccess()
{
    if (m_accessRemainingSeconds <= 0) {
        finishOtherAccess();
        return;
    }

    --m_accessRemainingSeconds;
    emit accessChanged();

    if (m_accessRemainingSeconds <= 0) {
        finishOtherAccess();
    }
}

void RoutineManager::finishOtherAccess()
{
    // Guard against re-entrancy: terminateUnrestrictedApps() below spins a nested
    // run loop (the macOS app sweep), during which a stale access-timer timeout
    // already in the event queue can re-deliver tickOtherAccess → finishOtherAccess.
    // Re-entering here recurses into nested run loops and re-runs the native
    // kiosk/lockdown calls, which crashes. Bail on the re-entrant call.
    if (m_finishingOtherAccess) {
        return;
    }
    m_finishingOtherAccess = true;

    m_accessTimer.stop();
    m_inactivityTimer.stop();
    m_accessRemainingSeconds = 0;
    if (m_backend) {
        // terminateUnrestrictedApps also terminates the desktop shell on Linux.
        m_backend->terminateUnrestrictedApps();
    }
    if (m_desktopShellRunning) {
        m_desktopShellRunning = false;
        emit desktopShellChanged();
    }
    if (m_editMode) {
        m_editMode = false;
        emit editModeChanged();
    }
    emit accessChanged();
    emitRowsChanged();
    m_finishingOtherAccess = false;
}

void RoutineManager::emitActiveSessionProgress()
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0) {
        return;
    }
    // Don't write progress for a routine whose timer isn't running.
    // Guards against a stop()/expired emission writing a phantom record.
    if (!m_routineTimer.active() && !m_routineTimer.paused()) {
        return;
    }

    const Routine &routine = m_routines.at(routineIndex);
    const int totalSeconds = qMax(0, routine.timeLimitMinutes * 60);
    const int elapsedSeconds = qBound(0, totalSeconds - m_routineTimer.remainingSeconds(), totalSeconds);
    if (elapsedSeconds <= 0) {
        return;
    }
    emit routineSessionProgress(routine.id,
                                routine.name,
                                elapsedSeconds,
                                m_activeStartedAt,
                                QDateTime::currentDateTimeUtc());
}

void RoutineManager::emitRowsChanged()
{
    if (m_routines.isEmpty()) {
        return;
    }
    emit dataChanged(index(0, 0), index(m_routines.size() - 1, 0), {
        IsActiveRole,
        TimeLabelRole,
        ButtonLabelRole,
        ButtonEnabledRole
    });
}

// Finish engaging a routine once any network lock is already applied (the slow
// DNS+nft work happens in engage() before this, on a worker thread). Launches
// the routine's apps, flips active state, starts the timer + checkpoint +
// respawn watchdog. networkApplied tells us to roll the firewall back if launch
// fails. Returns false (with errorMessage) if the apps can't be launched.
bool RoutineManager::finishEngage(const Routine &routine, bool networkApplied, QString *errorMessage)
{
    qInfo() << "[engage] finishEngage: begin, routine=" << routine.id << "networkApplied=" << networkApplied;
    if (m_backend) {
        QString error;
        qInfo() << "[engage] finishEngage: prepareRoutineSession";
        m_backend->prepareRoutineSession(routine.apps);
        qInfo() << "[engage] finishEngage: launchRoutineTargets";
        if (!launchRoutineTargets(routine, &error)) {
            if (networkApplied) {
                m_backend->dropNetworkPolicy();
            }
            if (errorMessage) {
                *errorMessage = error.isEmpty()
                    ? QStringLiteral("ROUTINE LAUNCH FAILED")
                    : QStringLiteral("ROUTINE LAUNCH FAILED: %1").arg(error);
            }
            return false;
        }
    }

    qInfo() << "[engage] finishEngage: launch done, flipping active state";
    m_activeRoutineId = routine.id;
    m_activeStartedAt = QDateTime::currentDateTimeUtc();
    m_manualPause = false;
    emit activeChanged();
    emitRowsChanged();
    m_routineTimer.start(routine.timeLimitMinutes * 60);
    emitActiveSessionProgress();

    // Arm the checkpoint and respawn watchdog: from here on a kill/crash
    // re-launches FocusOS and resumes this locked routine.
    writeActiveSession(true);
    if (m_backend) {
        m_backend->startWatchdog(QCoreApplication::applicationFilePath());
    }
    updateDisplaySleepInhibit();
    return true;
}

bool RoutineManager::launchRoutineTargets(const Routine &routine, QString *errorMessage)
{
    if (!m_backend) {
        return true;
    }

    // Always-allowed apps come up alongside the routine, but only on the
    // FIRST engage of the FocusOS lifecycle. They're sticky — we don't
    // terminate them at routine end — so re-launching every engage would
    // pile up duplicate windows. The flag resets naturally on FocusOS quit.
    if (!m_alwaysAllowedLaunched && !m_alwaysAllowedApps.isEmpty()) {
        QString alwaysError;
        m_backend->launchApps(m_alwaysAllowedApps, &alwaysError);
        m_alwaysAllowedLaunched = true;
        // Failures here are non-fatal — a missing/uninstalled allowlist
        // entry shouldn't block the routine.
    }

    if (!routine.apps.isEmpty() && !m_backend->launchApps(routine.apps, errorMessage)) {
        return false;
    }

    if (!routine.allowedUrls.isEmpty() && !m_backend->openUrls(routine.allowedUrls, errorMessage)) {
        return false;
    }

    return true;
}

void RoutineManager::setStatusMessage(const QString &message)
{
    m_statusMessage = message;
    emit statusMessageChanged(m_statusMessage);
}

void RoutineManager::setNetworkLockPrompt(const Routine &routine, const QString &error)
{
    m_pendingNetworkRoutineId = routine.id;
    m_networkLockRoutineName = routine.name;
    m_networkLockError = error.isEmpty()
        ? QStringLiteral("Network restrictions could not be applied.")
        : error;
    setStatusMessage(QStringLiteral("NETWORK LOCK FAILED"));
    emit networkLockPromptChanged();
}

void RoutineManager::clearNetworkLockPrompt()
{
    if (m_pendingNetworkRoutineId.isEmpty() && m_networkLockError.isEmpty() && m_networkLockRoutineName.isEmpty()) {
        return;
    }

    m_pendingNetworkRoutineId.clear();
    m_networkLockError.clear();
    m_networkLockRoutineName.clear();
    emit networkLockPromptChanged();
}

void RoutineManager::setFinishedSessionPrompt(const Routine &routine, int minutes, const QString &result)
{
    m_sessionPromptVisible = true;
    m_finishedRoutineId = routine.id;
    m_finishedSessionName = routine.name;
    m_finishedSessionMinutes = qMax(0, minutes);
    m_finishedSessionResult = result;
    m_finishedSessionApps = routine.apps;
    m_finishedSessionUrls = routine.allowedUrls;
    emit sessionPromptChanged();
}

void RoutineManager::clearFinishedSessionPrompt()
{
    if (!m_sessionPromptVisible &&
        m_finishedSessionName.isEmpty() &&
        m_finishedSessionMinutes == 0 &&
        m_finishedSessionResult.isEmpty() &&
        m_finishedSessionApps.isEmpty() &&
        m_finishedSessionUrls.isEmpty()) {
        return;
    }

    m_sessionPromptVisible = false;
    m_finishedSessionName.clear();
    m_finishedSessionMinutes = 0;
    m_finishedSessionResult.clear();
    m_finishedSessionApps.clear();
    m_finishedSessionUrls.clear();
    emit sessionPromptChanged();
}

QString RoutineManager::formatDuration(int seconds) const
{
    const int clamped = qMax(0, seconds);
    const int hours = clamped / 3600;
    const int minutes = (clamped % 3600) / 60;
    const int secs = clamped % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

int RoutineManager::indexOfRoutine(const QString &routineId) const
{
    for (int i = 0; i < m_routines.size(); ++i) {
        if (m_routines.at(i).id == routineId) {
            return i;
        }
    }
    return -1;
}

