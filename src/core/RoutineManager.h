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
    // Optional per-routine folder the in-app file browser may reach. Always
    // browsable when set. Empty = none.
    QString accessFolder;
    // The standard user folders are NOT browsable by default — each routine opts
    // in to the ones it wants reachable from the in-app file browser.
    bool accessDesktop = false;
    bool accessDocuments = false;
    bool accessDownloads = false;
    // Master switch for mid-session file access style. Off (default): the only
    // way to reach a file during the routine is the standard native "open file"
    // picker. On: the in-app folder browser ("browse computer") is offered too,
    // jailed to the accessDesktop/Documents/Downloads opt-ins + accessFolder.
    bool browsable = false;
    int timeLimitMinutes = 0;
    int minTimeMinutes = 0;
    bool networkLock = true;
    int breakFrequencyMinutes = 0;
    int breakDurationMinutes = 0;
    bool keepDisplayAwake = true;
    // What the music engine does while this routine is engaged: "stop", "low"
    // (default — "Continue at low volume") or "same". Drives MusicEngine's
    // engage behavior; updated live if the user toggles music off mid-session.
    QString musicBehavior = QStringLiteral("low");
    // Researcher escape hatch: when set, the routine runs with NO outbound
    // network restrictions (full internet). Because that is high-risk, engaging
    // such a routine always requires a valid TOTP code first (enforced in QML
    // before engage() is called).
    bool fullAccess = false;
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
    // Whether the active routine offers the in-app folder browser ("browse
    // computer") in addition to the standard native open-file picker.
    Q_PROPERTY(bool activeRoutineBrowsable READ activeRoutineBrowsable NOTIFY activeChanged)
    // Music engage behavior of the currently active routine ("stop"/"low"/"same").
    Q_PROPERTY(QString activeRoutineMusicBehavior READ activeRoutineMusicBehavior NOTIFY activeChanged)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY remainingSecondsChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY remainingSecondsChanged)
    // Open-ended continuation: after a routine's timer expires the user can
    // "Continue" into an indefinite, no-countdown momentum state (Task 5).
    Q_PROPERTY(bool openEnded READ openEnded NOTIFY activeChanged)
    // In-app screen lock (Task 6): a pitch-black overlay + display-off; any
    // input dismisses it.
    Q_PROPERTY(bool screenLocked READ screenLocked NOTIFY screenLockedChanged)
    Q_PROPERTY(bool accessGranted READ accessGranted NOTIFY accessChanged)
    Q_PROPERTY(int accessRemainingSeconds READ accessRemainingSeconds NOTIFY accessChanged)
    Q_PROPERTY(QString accessStatus READ accessStatus NOTIFY accessChanged)
    Q_PROPERTY(int otherAccessMinutes READ otherAccessMinutes WRITE setOtherAccessMinutes NOTIFY configChanged)
    Q_PROPERTY(bool sessionPromptVisible READ sessionPromptVisible NOTIFY sessionPromptChanged)
    Q_PROPERTY(QString finishedSessionName READ finishedSessionName NOTIFY sessionPromptChanged)
    Q_PROPERTY(int finishedSessionMinutes READ finishedSessionMinutes NOTIFY sessionPromptChanged)
    Q_PROPERTY(QString finishedSessionResult READ finishedSessionResult NOTIFY sessionPromptChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    // Smart pause (Task 4). 0 = running, 1 = idle pause (auto-resumes on
    // keyboard input / window-focus change), 2 = manual pause (stays paused
    // until the user resumes; a persistent banner reminds them).
    Q_PROPERTY(int pauseMode READ pauseMode NOTIFY pauseModeChanged)
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
    // Whether the deep-idle stage (after the screensaver) suspends the whole
    // machine (S3) in addition to turning the display off + pausing music.
    // Default OFF: on hardware where Linux can't resume from suspend (common on
    // Mac/iMac), suspending bricks the session — the machine sleeps and nothing
    // wakes it. Off = a safe soft sleep (display off + music off) that any input
    // recovers from. Opt in only when you've confirmed resume works.
    Q_PROPERTY(bool deepSleepSuspend READ deepSleepSuspend WRITE setDeepSleepSuspend NOTIFY configChanged)

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
        FullAccessRole,
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
    bool activeRoutineBrowsable() const;
    QString activeRoutineMusicBehavior() const;
    int remainingSeconds() const;
    int elapsedSeconds() const;
    bool openEnded() const;
    bool screenLocked() const;
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
    int pauseMode() const;
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
    bool deepSleepSuspend() const;
    void setDeepSleepSuspend(bool enabled);

    // Update the active routine's music behavior live (and persist it). Wired to
    // the home/launcher music toggle: switching music off mid-session sets the
    // active routine's behavior to "stop" so it sticks for next time.
    Q_INVOKABLE void setActiveRoutineMusicBehavior(const QString &behavior);

    Q_INVOKABLE void engage(const QString &routineId);
    Q_INVOKABLE void abortPendingRoutineStart();
    // Single press: toggle between running and an "idle pause" that auto-resumes.
    Q_INVOKABLE void togglePause();
    // Double press: a "manual pause" that never auto-resumes (persistent banner).
    Q_INVOKABLE void manualPause();
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
    // Generic file picker for the editor's "+ OPEN FILE" workflow and the
    // mid-session quick-open: returns a chosen file path (any type), or empty on
    // cancel. The selected file later opens in its default application.
    Q_INVOKABLE QString pickFile();
    // Folder picker for the routine editor's optional per-routine "access folder".
    // Returns the chosen directory, or empty on cancel.
    Q_INVOKABLE QString pickFolder();
    // Mid-session quick-open: fire a native file dialog and open the chosen
    // document straight away, no folder navigation needed. Kept alongside the
    // in-app browser because picking one known file directly (no double-click
    // through folders) is the fast path. Refuses executables / .desktop launchers.
    Q_INVOKABLE QString openDocumentInSession();
    // The in-app file browser's allowed roots: the standard folders the active
    // routine has opted into (accessDesktop/Documents/Downloads), plus its access
    // folder if set. Each entry is {name, path}. The browser is jailed to these.
    Q_INVOKABLE QVariantList browseRoots() const;
    // Directory listing for the in-app browser. Returns entries only when path is
    // within an allowed root (see isWithinBrowseRoots); each entry is
    // {name, path, isDir, suffix}, directories first then files, alphabetical,
    // hidden entries skipped. Empty when path escapes the roots.
    Q_INVOKABLE QVariantList listFolder(const QString &path) const;
    // Open a document mid-session from the in-app browser. The file manager is
    // killed during a routine (it can launch arbitrary apps), which would
    // otherwise leave a writer or researcher unable to reach a reference document
    // they didn't pre-load. This is the mindful alternative: it opens the chosen
    // file in its viewer, refusing executables / .desktop launchers and any path
    // outside the allowed roots, so it can't become an escape hatch out of the
    // locked-down environment. Returns the opened path, or empty on rejection.
    Q_INVOKABLE QString openFileInSession(const QString &path);
    // Reset the unlock-panel inactivity countdown. Wired to IdleMonitor's
    // activity() signal: any user input while access is granted re-arms the
    // 30-minute auto-lock (see ctor).
    Q_INVOKABLE void notifyActivity();
    Q_INVOKABLE bool signOutSupported() const;
    // Wired to IdleMonitor's keyboard/window-focus signal: while the timer is in
    // an idle pause, meaningful user activity (a keypress or window-focus change,
    // never mouse movement alone) auto-resumes it. No-op for a manual pause.
    void onResumeHint();
    // Log the user out of their account / session (returns to the login
    // screen). Admin-gated by the caller (settings access).
    Q_INVOKABLE void signOut();
    Q_INVOKABLE QString applicationDisplayName(const QString &path) const;
    Q_INVOKABLE bool addAlwaysAllowedApp(const QString &commandLine);
    Q_INVOKABLE void removeAlwaysAllowedApp(int index);
    Q_INVOKABLE bool sessionRecoverySupported() const;
    Q_INVOKABLE bool restoreLoginSessions();
    // Dry run of the strict engage-time app sweep: the names of GUI apps that
    // would be closed if a routine engaged right now (given the always-allowed
    // list), without killing anything. Lets the user verify the keep-set on
    // their own machine before trusting it. Empty where unsupported.
    Q_INVOKABLE QStringList previewBackgroundAppQuit() const;

    static QString dataDirectory();

public slots:
    // Task 6 — turn the screen off / show a black lock overlay. Any input
    // calls unlockScreen() to restore. Declared as slots (not just Q_INVOKABLE)
    // so the logind power-key "Lock" DBus signal can be wired straight to them
    // (see main.cpp on Linux). Slots are still callable from QML.
    void lockScreen();
    void unlockScreen();
    // Sleep the physical display (DPMS off) without engaging the lock overlay.
    // Exposed for the Settings-authorization panel's "sleep display" button.
    Q_INVOKABLE void sleepDisplay();
    // Wired to logind's PrepareForSleep(bool) signal (see main.cpp on Linux).
    // aboutToSleep=false means the machine just resumed: force the panel back on
    // so a resume triggered by lid/power (a non-Qt event the deep-idle state
    // machine never sees) doesn't leave the display DPMS-off until the first
    // mouse/key. A no-op on the way down (aboutToSleep=true) — we already blanked.
    void handlePrepareForSleep(bool aboutToSleep);

signals:
    void activeChanged();
    void remainingSecondsChanged();
    void accessChanged();
    void configChanged();
    void sessionPromptChanged();
    void pausedChanged();
    void pauseModeChanged();
    void editModeChanged();
    void desktopShellChanged();
    void routineCountChanged();
    void statusMessageChanged(const QString &message);
    void networkLockPromptChanged();
    void alwaysAllowedAppsChanged();
    void overlayProgressEnabledChanged();
    void displayStaysAwakeChanged();
    void screenLockedChanged();
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
    // The in-app browser's allowed roots as {displayName, canonicalPath} pairs.
    QList<QPair<QString, QString>> browseRootsInternal() const;
    // True when path resolves (canonically) to inside one of the allowed roots —
    // canonical comparison defeats ".." and symlink escapes.
    bool isWithinBrowseRoots(const QString &path) const;
    void updateDisplaySleepInhibit();
    void resumeActiveSessionIfPresent();
    void onRoutineExpired();
    void tickOtherAccess();
    void finishOtherAccess();
    void emitActiveSessionProgress();
    void emitRowsChanged();
    void resumeRoutine();
    bool finishEngage(const Routine &routine, bool networkApplied, QString *errorMessage = nullptr);
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
    // Single-shot 30-minute inactivity watchdog for the unlock panel. Re-armed
    // by notifyActivity() on every input while access is granted; on timeout it
    // revokes access (re-locking settings) so a walked-away unlocked panel
    // doesn't stay open.
    QTimer m_inactivityTimer;
    QString m_activeRoutineId;
    QDateTime m_activeStartedAt;
    // Open-ended continuation state (Task 5): active() stays true with no
    // countdown timer running. Holds the id of the routine being continued.
    bool m_openEnded = false;
    // Smart pause (Task 4): true while the current pause is a manual pause (no
    // auto-resume). Meaningless when the timer isn't paused.
    bool m_manualPause = false;
    QString m_finishedRoutineId;
    // In-app screen lock (Task 6).
    bool m_screenLocked = false;
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
    // True between "network lock requested" and the async resolver callback, so
    // a second engage can't race a parallel one (see engage()).
    bool m_engaging = false;
    bool m_overlayProgressEnabled = true;
    bool m_deepSleepSuspend = false;
    bool m_editMode = false;
    bool m_desktopShellRunning = false;
    // Throttle for the active.json crash checkpoint — the timer ticks every
    // second but we only need to touch the disk occasionally. Forced writes
    // (start, pause/resume, resume-on-launch) bypass it.
    mutable qint64 m_lastCheckpointWriteMs = 0;
};
