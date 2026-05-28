#include "core/RoutineManager.h"

#include "platform/PlatformBackend.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QVariantMap>

namespace {

QString routinesPath()
{
    return RoutineManager::dataDirectory() + QStringLiteral("/routines.json");
}

QString configPath()
{
    return RoutineManager::dataDirectory() + QStringLiteral("/config.json");
}

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

} // namespace

RoutineManager::RoutineManager(PlatformBackend *backend, QObject *parent)
    : QAbstractListModel(parent)
    , m_backend(backend)
{
    QDir().mkpath(dataDirectory());
    loadConfig();
    loadRoutines();

    connect(&m_routineTimer, &FocusTimer::remainingSecondsChanged, this, [this] {
        emit remainingSecondsChanged();
        emitRowsChanged();
        emitActiveSessionProgress();
    });
    connect(&m_routineTimer, &FocusTimer::pausedChanged, this, &RoutineManager::pausedChanged);
    connect(&m_routineTimer, &FocusTimer::expired, this, &RoutineManager::onRoutineExpired);

    m_accessTimer.setInterval(1000);
    connect(&m_accessTimer, &QTimer::timeout, this, &RoutineManager::tickOtherAccess);
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
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).timeLimitMinutes * 60 : 0;
}

QString RoutineManager::activeRoutineDescription() const
{
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    return routineIndex >= 0 ? m_routines.at(routineIndex).description : QString();
}

int RoutineManager::remainingSeconds() const
{
    return m_routineTimer.remainingSeconds();
}

int RoutineManager::elapsedSeconds() const
{
    return qMax(0, activeRoutineTotalSeconds() - m_routineTimer.remainingSeconds());
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
    return m_routineTimer.paused();
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

void RoutineManager::togglePause()
{
    if (!active()) {
        return;
    }
    if (m_routineTimer.paused()) {
        m_routineTimer.resume();
    } else {
        m_routineTimer.pause();
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

void RoutineManager::endActiveRoutine()
{
    if (!active()) {
        return;
    }
    const int routineIndex = indexOfRoutine(m_activeRoutineId);
    if (routineIndex < 0) {
        return;
    }
    const Routine &routine = m_routines.at(routineIndex);
    const int elapsedSecondsValue = qMax(0, routine.timeLimitMinutes * 60 - m_routineTimer.remainingSeconds());
    const int elapsedMinutes = elapsedSecondsValue <= 0 ? 0 : (elapsedSecondsValue + 59) / 60;

    if (m_backend) {
        m_backend->dropNetworkPolicy();
    }
    emitActiveSessionProgress();
    emit routineSessionFinished(routine.id,
                                routine.name,
                                elapsedMinutes,
                                QStringLiteral("unlocked"),
                                m_activeStartedAt,
                                QDateTime::currentDateTimeUtc());
    setFinishedSessionPrompt(routine, elapsedMinutes, QStringLiteral("unlocked"));

    // Clear active state BEFORE stopping the timer to avoid the phantom-progress race.
    m_activeRoutineId.clear();
    m_activeStartedAt = {};
    m_routineTimer.stop();
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

void RoutineManager::engage(const QString &routineId)
{
    if (active() || accessGranted()) {
        return;
    }
    clearFinishedSessionPrompt();
    clearNetworkLockPrompt();

    const int routineIndex = indexOfRoutine(routineId);
    if (routineIndex < 0) {
        return;
    }

    const Routine &routine = m_routines.at(routineIndex);
    QString error;
    if (!startRoutine(routine, routine.networkLock, &error)) {
        if (routine.networkLock && error.startsWith(QStringLiteral("network:"), Qt::CaseInsensitive)) {
            setNetworkLockPrompt(routine, error.mid(QStringLiteral("network:").size()).trimmed());
            return;
        }
        setStatusMessage(error.isEmpty()
                             ? QStringLiteral("ROUTINE START FAILED")
                             : error);
    }
}

void RoutineManager::startPendingRoutineWithoutNetworkLock()
{
    if (m_pendingNetworkRoutineId.isEmpty() || active() || accessGranted()) {
        clearNetworkLockPrompt();
        return;
    }

    const int routineIndex = indexOfRoutine(m_pendingNetworkRoutineId);
    if (routineIndex < 0) {
        clearNetworkLockPrompt();
        return;
    }

    const Routine routine = m_routines.at(routineIndex);
    clearNetworkLockPrompt();

    QString error;
    if (!startRoutine(routine, false, &error)) {
        setStatusMessage(error.isEmpty()
                             ? QStringLiteral("ROUTINE START FAILED")
                             : error);
    }
}

void RoutineManager::abortPendingRoutineStart()
{
    clearNetworkLockPrompt();
    setStatusMessage(QStringLiteral("ROUTINE START ABORTED"));
}

void RoutineManager::unlockOtherAccess()
{
    clearFinishedSessionPrompt();

    if (m_backend) {
        m_backend->dropNetworkPolicy();
    }

    if (active()) {
        const int routineIndex = indexOfRoutine(m_activeRoutineId);
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
        emit activeChanged();
        emitRowsChanged();
    }

    m_accessRemainingSeconds = qMax(1, m_otherAccessMinutes) * 60;
    if (m_backend) {
        QString error;
        m_backend->openSystemTerminal(&error);
    }
    m_accessTimer.start();
    emit accessChanged();
    emitRowsChanged();
}

void RoutineManager::continueFinishedSession()
{
    clearFinishedSessionPrompt();
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
        object.insert(QStringLiteral("time_limit_minutes"), routine.timeLimitMinutes);
        object.insert(QStringLiteral("min_time_minutes"), routine.minTimeMinutes);
        object.insert(QStringLiteral("network_lock"), routine.networkLock);
        object.insert(QStringLiteral("break_frequency_minutes"), routine.breakFrequencyMinutes);
        object.insert(QStringLiteral("break_duration_minutes"), routine.breakDurationMinutes);
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

        const QString description = object.value(QStringLiteral("description")).toString().trimmed();
        const QStringList apps = variantToStringList(object.value(QStringLiteral("apps")));
        const QStringList allowedUrls = variantToStringList(object.value(QStringLiteral("allowed_urls")));

        QJsonArray appArray;
        for (const QString &app : apps) {
            appArray.append(app);
        }

        QJsonArray urlArray;
        for (const QString &url : allowedUrls) {
            urlArray.append(url);
        }

        QJsonObject routine;
        routine.insert(QStringLiteral("id"), id);
        routine.insert(QStringLiteral("name"), name);
        routine.insert(QStringLiteral("description"), description);
        routine.insert(QStringLiteral("apps"), appArray);
        routine.insert(QStringLiteral("allowed_urls"), urlArray);
        routine.insert(QStringLiteral("time_limit_minutes"), qMax(1, intFromVariant(object.value(QStringLiteral("time_limit_minutes")), 60)));
        routine.insert(QStringLiteral("min_time_minutes"), qMax(0, intFromVariant(object.value(QStringLiteral("min_time_minutes")), 0)));
        routine.insert(QStringLiteral("network_lock"), object.value(QStringLiteral("network_lock"), true).toBool());
        routine.insert(QStringLiteral("break_frequency_minutes"), qMax(0, intFromVariant(object.value(QStringLiteral("break_frequency_minutes")), 0)));
        routine.insert(QStringLiteral("break_duration_minutes"), qMax(0, intFromVariant(object.value(QStringLiteral("break_duration_minutes")), 0)));
        array.append(routine);
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

    QJsonArray array;
    for (const Routine &routine : m_routines) {
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
        object.insert(QStringLiteral("time_limit_minutes"), routine.timeLimitMinutes);
        object.insert(QStringLiteral("min_time_minutes"), routine.minTimeMinutes);
        object.insert(QStringLiteral("network_lock"), routine.networkLock);
        object.insert(QStringLiteral("break_frequency_minutes"), routine.breakFrequencyMinutes);
        object.insert(QStringLiteral("break_duration_minutes"), routine.breakDurationMinutes);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("routines"), array);

    QSaveFile file(routinesPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }

    emit dataChanged(index(routineIndex, 0), index(routineIndex, 0), { DescriptionRole });
    return true;
}

QString RoutineManager::pickApplication() const
{
#if defined(Q_OS_MACOS)
    return QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Select Allowed Application"),
        QStringLiteral("/Applications"),
        QStringLiteral("Applications (*.app)"));
#elif defined(Q_OS_LINUX)
    const QString applicationsPath = QFileInfo::exists(QStringLiteral("/usr/share/applications"))
        ? QStringLiteral("/usr/share/applications")
        : QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Select Allowed Application"),
        applicationsPath,
        QStringLiteral("Applications (*.desktop);;Executables (*)"));
    const QFileInfo info(path);
    if (!path.isEmpty() &&
        info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) != 0 &&
        !info.isExecutable()) {
        return {};
    }
    return path;
#else
    return {};
#endif
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
    saveConfig();
    emit configChanged();
}

bool RoutineManager::saveConfig() const
{
    QJsonObject root = readConfigObject();
    root.insert(QStringLiteral("other_access_minutes"), m_otherAccessMinutes);
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
        routine.timeLimitMinutes = qMax(1, object.value(QStringLiteral("time_limit_minutes")).toInt(60));
        routine.minTimeMinutes = qMax(0, object.value(QStringLiteral("min_time_minutes")).toInt(0));
        routine.networkLock = object.value(QStringLiteral("network_lock")).toBool(true);
        routine.breakFrequencyMinutes = qMax(0, object.value(QStringLiteral("break_frequency_minutes")).toInt(0));
        routine.breakDurationMinutes = qMax(0, object.value(QStringLiteral("break_duration_minutes")).toInt(0));
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
    routines.append(reflection);

    QJsonObject root;
    root.insert(QStringLiteral("routines"), routines);

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
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
    }
    m_activeRoutineId.clear();
    m_activeStartedAt = {};
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
    m_accessTimer.stop();
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

bool RoutineManager::startRoutine(const Routine &routine, bool applyNetworkLock, QString *errorMessage)
{
    if (m_backend) {
        QString error;
        if (applyNetworkLock && !m_backend->applyNetworkPolicy(routine.allowedUrls, &error)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("network: %1").arg(error.isEmpty()
                                                                       ? QStringLiteral("network restrictions could not be applied")
                                                                       : error);
            }
            return false;
        }

        m_backend->prepareRoutineSession(routine.apps);
        if (!launchRoutineTargets(routine, &error)) {
            if (applyNetworkLock) {
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

    m_activeRoutineId = routine.id;
    m_activeStartedAt = QDateTime::currentDateTimeUtc();
    emit activeChanged();
    emitRowsChanged();
    m_routineTimer.start(routine.timeLimitMinutes * 60);
    emitActiveSessionProgress();
    return true;
}

bool RoutineManager::launchRoutineTargets(const Routine &routine, QString *errorMessage)
{
    if (!m_backend) {
        return true;
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

QString RoutineManager::dataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.focusos");
}
