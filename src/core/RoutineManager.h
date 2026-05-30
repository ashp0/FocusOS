#pragma once

#include "core/Timer.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QTimer>
#include <QVariantList>

class PlatformBackend;

struct Routine
{
    QString id;
    QString name;
    QString description;
    QStringList apps;
    QStringList allowedUrls;
    int timeLimitMinutes = 0;
    int minTimeMinutes = 0;
    bool networkLock = true;
    int breakFrequencyMinutes = 0;
    int breakDurationMinutes = 0;
    bool keepDisplayAwake = true;
};

class RoutineManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString activeRoutineId READ activeRoutineId NOTIFY activeChanged)
    Q_PROPERTY(QString activeRoutineName READ activeRoutineName NOTIFY activeChanged)
    Q_PROPERTY(int activeRoutineTotalSeconds READ activeRoutineTotalSeconds NOTIFY activeChanged)
    Q_PROPERTY(QString activeRoutineDescription READ activeRoutineDescription NOTIFY activeChanged)
    Q_PROPERTY(int activeRoutineBreakFrequencyMinutes READ activeRoutineBreakFrequencyMinutes NOTIFY activeChanged)
    Q_PROPERTY(int activeRoutineBreakDurationMinutes READ activeRoutineBreakDurationMinutes NOTIFY activeChanged)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY remainingSecondsChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY remainingSecondsChanged)
    Q_PROPERTY(bool accessGranted READ accessGranted NOTIFY accessChanged)
    Q_PROPERTY(int accessRemainingSeconds READ accessRemainingSeconds NOTIFY accessChanged)
    Q_PROPERTY(QString accessStatus READ accessStatus NOTIFY accessChanged)
    Q_PROPERTY(int otherAccessMinutes READ otherAccessMinutes WRITE setOtherAccessMinutes NOTIFY configChanged)
    Q_PROPERTY(bool sessionPromptVisible READ sessionPromptVisible NOTIFY sessionPromptChanged)
    Q_PROPERTY(QString finishedSessionName READ finishedSessionName NOTIFY sessionPromptChanged)
    Q_PROPERTY(int finishedSessionMinutes READ finishedSessionMinutes NOTIFY sessionPromptChanged)
    Q_PROPERTY(QString finishedSessionResult READ finishedSessionResult NOTIFY sessionPromptChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
    Q_PROPERTY(bool desktopShellSupported READ desktopShellSupported CONSTANT)
    Q_PROPERTY(bool desktopShellRunning READ desktopShellRunning NOTIFY desktopShellChanged)
    Q_PROPERTY(int routineCount READ routineCount NOTIFY routineCountChanged)
    Q_PROPERTY(bool activeRoutineHasLaunchTargets READ activeRoutineHasLaunchTargets NOTIFY activeChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool networkLockPromptVisible READ networkLockPromptVisible NOTIFY networkLockPromptChanged)
    Q_PROPERTY(QString networkLockError READ networkLockError NOTIFY networkLockPromptChanged)
    Q_PROPERTY(QString networkLockRoutineName READ networkLockRoutineName NOTIFY networkLockPromptChanged)
    Q_PROPERTY(QStringList alwaysAllowedApps READ alwaysAllowedApps NOTIFY alwaysAllowedAppsChanged)
    Q_PROPERTY(bool overlayProgressEnabled READ overlayProgressEnabled WRITE setOverlayProgressEnabled NOTIFY overlayProgressEnabledChanged)
    Q_PROPERTY(bool displayStaysAwake READ displayStaysAwake WRITE setDisplayStaysAwake NOTIFY displayStaysAwakeChanged)

public:
    enum Role {
        RoutineIdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        AppsRole,
        AppsDisplayRole,
        AllowedUrlsRole,
        TimeLimitMinutesRole,
        MinTimeMinutesRole,
        NetworkLockRole,
        BreakFrequencyMinutesRole,
        BreakDurationMinutesRole,
        IsActiveRole,
        TimeLabelRole,
        ButtonLabelRole,
        ButtonEnabledRole
    };
    Q_ENUM(Role)

    explicit RoutineManager(PlatformBackend *backend, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const;
    QString activeRoutineId() const;
    QString activeRoutineName() const;
    int activeRoutineTotalSeconds() const;
    QString activeRoutineDescription() const;
    int activeRoutineBreakFrequencyMinutes() const;
    int activeRoutineBreakDurationMinutes() const;
    int remainingSeconds() const;
    int elapsedSeconds() const;
    bool accessGranted() const;
    int accessRemainingSeconds() const;
    QString accessStatus() const;
    int otherAccessMinutes() const;
    void setOtherAccessMinutes(int minutes);
    bool sessionPromptVisible() const;
    QString finishedSessionName() const;
    int finishedSessionMinutes() const;
    QString finishedSessionResult() const;
    bool paused() const;
    bool editMode() const;
    void setEditMode(bool enabled);
    bool desktopShellSupported() const;
    bool desktopShellRunning() const;
    int routineCount() const;
    bool activeRoutineHasLaunchTargets() const;
    QString statusMessage() const;
    bool networkLockPromptVisible() const;
    QString networkLockError() const;
    QString networkLockRoutineName() const;
    QStringList alwaysAllowedApps() const;
    bool overlayProgressEnabled() const;
    void setOverlayProgressEnabled(bool enabled);
    bool displayStaysAwake() const;
    void setDisplayStaysAwake(bool stayAwake);

    Q_INVOKABLE void engage(const QString &routineId);
    Q_INVOKABLE void abortPendingRoutineStart();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void endActiveRoutine();
    Q_INVOKABLE void closeOtherAccess();
    Q_INVOKABLE void launchDesktopShell();
    Q_INVOKABLE void relaunchActiveRoutine();
    Q_INVOKABLE void unlockOtherAccess();
    Q_INVOKABLE void continueFinishedSession();
    Q_INVOKABLE void quitFinishedSession();
    Q_INVOKABLE QVariantList routinesForEditing() const;
    Q_INVOKABLE bool saveRoutines(const QVariantList &routines);
    Q_INVOKABLE bool updateRoutineDescription(const QString &routineId, const QString &description);
    Q_INVOKABLE QString pickApplication();
    Q_INVOKABLE QString applicationDisplayName(const QString &path) const;
    Q_INVOKABLE bool addAlwaysAllowedApp(const QString &commandLine);
    Q_INVOKABLE void removeAlwaysAllowedApp(int index);
    Q_INVOKABLE bool sessionRecoverySupported() const;
    Q_INVOKABLE bool restoreLoginSessions();

    static QString dataDirectory();

signals:
    void activeChanged();
    void remainingSecondsChanged();
    void accessChanged();
    void configChanged();
    void sessionPromptChanged();
    void pausedChanged();
    void editModeChanged();
    void desktopShellChanged();
    void routineCountChanged();
    void statusMessageChanged(const QString &message);
    void networkLockPromptChanged();
    void alwaysAllowedAppsChanged();
    void overlayProgressEnabledChanged();
    void displayStaysAwakeChanged();
    void desktopAccessRequested();
    void routineSessionFinished(const QString &routineId,
                                const QString &routineName,
                                int minutes,
                                const QString &result,
                                const QDateTime &startedAt,
                                const QDateTime &endedAt);
    void routineSessionProgress(const QString &routineId,
                                const QString &routineName,
                                int elapsedSeconds,
                                const QDateTime &startedAt,
                                const QDateTime &updatedAt);

private:
    void loadConfig();
    bool saveConfig() const;
    void loadRoutines();
    void writeDefaultRoutines(const QString &path) const;
    void writeActiveSession(bool force = false) const;
    void clearActiveSession() const;
    bool persistRoutines() const;
    void updateDisplaySleepInhibit();
    void resumeActiveSessionIfPresent();
    void onRoutineExpired();
    void tickOtherAccess();
    void finishOtherAccess();
    void emitActiveSessionProgress();
    void emitRowsChanged();
    bool startRoutine(const Routine &routine, bool applyNetworkLock, QString *errorMessage = nullptr);
    bool launchRoutineTargets(const Routine &routine, QString *errorMessage = nullptr);
    void setStatusMessage(const QString &message);
    void setNetworkLockPrompt(const Routine &routine, const QString &error);
    void clearNetworkLockPrompt();
    void setFinishedSessionPrompt(const Routine &routine, int minutes, const QString &result);
    void clearFinishedSessionPrompt();
    QString formatDuration(int seconds) const;
    int indexOfRoutine(const QString &routineId) const;

    PlatformBackend *m_backend = nullptr;
    QVector<Routine> m_routines;
    FocusTimer m_routineTimer;
    QTimer m_accessTimer;
    QString m_activeRoutineId;
    QDateTime m_activeStartedAt;
    int m_accessRemainingSeconds = 0;
    int m_otherAccessMinutes = 30;
    bool m_sessionPromptVisible = false;
    QString m_finishedSessionName;
    int m_finishedSessionMinutes = 0;
    QString m_finishedSessionResult;
    QStringList m_finishedSessionApps;
    QStringList m_finishedSessionUrls;
    QString m_statusMessage;
    QString m_pendingNetworkRoutineId;
    QString m_networkLockError;
    QString m_networkLockRoutineName;
    QStringList m_alwaysAllowedApps;
    bool m_alwaysAllowedLaunched = false;
    bool m_overlayProgressEnabled = true;
    bool m_editMode = false;
    bool m_desktopShellRunning = false;
    // Throttle for the active.json crash checkpoint — the timer ticks every
    // second but we only need to touch the disk occasionally. Forced writes
    // (start, pause/resume, resume-on-launch) bypass it.
    mutable qint64 m_lastCheckpointWriteMs = 0;
};
