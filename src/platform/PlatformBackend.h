#pragma once

#include <QString>
#include <QStringList>

class PlatformBackend
{
public:
    virtual ~PlatformBackend() = default;

    virtual QString name() const = 0;
    virtual void prepareRoutineSession(const QStringList &appPaths) { Q_UNUSED(appPaths); }
    virtual bool launchApps(const QStringList &appPaths, QString *errorMessage = nullptr) = 0;
    virtual bool openUrls(const QStringList &urls, QString *errorMessage = nullptr) = 0;
    virtual void terminateApps(const QStringList &appPaths) = 0;
    virtual bool applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage = nullptr) = 0;
    virtual void dropNetworkPolicy() = 0;
    virtual bool openSystemTerminal(QString *errorMessage = nullptr) = 0;
    virtual void terminateUnrestrictedApps() = 0;
    virtual bool launchDesktopShell(QString *errorMessage = nullptr) { Q_UNUSED(errorMessage); return false; }
    virtual void terminateDesktopShell() {}
    virtual bool desktopShellSupported() const { return false; }
    // Return the FocusOS shell to its home workspace + clean up any routine
    // window-management state. Called when a routine ends.
    virtual void restoreShellPlacement() {}
    // Keep the display awake (inhibit screen blanking / sleep) for the
    // duration of a routine. Idempotent — call with true to hold the display
    // on, false to release. No-op on platforms without a power-management hook.
    virtual void setDisplaySleepInhibited(bool inhibited) { Q_UNUSED(inhibited); }
    // Forcibly release any lingering display-sleep inhibitors, including ones
    // owned by a predecessor process. Safe to call from a crash/signal handler.
    // Distinct from setDisplaySleepInhibited(false), which only releases an
    // inhibitor this instance still owns. No-op where unsupported.
    virtual void releaseDisplaySleepInhibitors() {}
    // Apps the user has flagged as "always allowed" — calendar, word
    // processor, etc. The backend exempts them from the lockdown watchdog
    // and won't terminate them between routines.
    virtual void setAlwaysAllowedApps(const QStringList &commandLines) { Q_UNUSED(commandLines); }
    // Ensure the respawn watchdog is running for the current process. The
    // watchdog respawns FocusOS while a routine checkpoint is armed
    // (~/.focusos/active.json), making a kill / crash recoverable. No-op on
    // platforms without a strict lockdown story.
    virtual void startWatchdog(const QString &binaryPath) { Q_UNUSED(binaryPath); }
    // TOTP-gated recovery: restore the other login sessions that the
    // permanent install stashed, so the user can leave the FocusOS-only
    // session. Returns false (with errorMessage) when unsupported or it fails.
    virtual bool restoreLoginSessions(QString *errorMessage = nullptr)
    {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Session recovery is not supported on this platform");
        }
        return false;
    }
};
