#pragma once

#include "platform/PlatformBackend.h"
#include "platform/linux/NetGate.h"

#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

class LinuxBackend final : public PlatformBackend
{
public:
    LinuxBackend();
    ~LinuxBackend() override;

    QString name() const override;
    void prepareRoutineSession(const QStringList &appPaths) override;
    bool launchApps(const QStringList &appPaths, QString *errorMessage = nullptr) override;
    bool openUrls(const QStringList &urls, QString *errorMessage = nullptr) override;
    void terminateApps(const QStringList &appPaths) override;
    bool hasLiveRoutineApps() const override;
    void endRoutineLockdown() override;
    void quitBackgroundApps(const QStringList &allowedCommandLines) override;
    void lockScreen() override;
    void unlockScreen() override;
    void sleepDisplay() override;
    void wakeDisplay() override;
    bool suspendSystem() override;
    void freezeBackgroundProcesses() override;
    void thawBackgroundProcesses() override;
    bool applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage = nullptr) override;
    void applyNetworkPolicyAsync(const QStringList &allowedHosts,
                                 std::function<void(bool, const QString &)> onComplete) override;
    void applyBrowserBlockerPolicy(const QStringList &allowedHosts) override;
    void dropNetworkPolicy() override;
    QStringList previewBackgroundAppQuit(const QStringList &allowedCommandLines) override;
    bool openSystemTerminal(QString *errorMessage = nullptr) override;
    void terminateUnrestrictedApps() override;
    bool launchDesktopShell(QString *errorMessage = nullptr) override;
    void terminateDesktopShell() override;
    bool desktopShellSupported() const override { return true; }
    void restoreShellPlacement() override;
    void setAlwaysAllowedApps(const QStringList &commandLines) override;
    void startWatchdog(const QString &binaryPath) override;
    bool restoreLoginSessions(QString *errorMessage = nullptr) override;
    bool signOutSupported() const override { return true; }
    bool signOut(QString *errorMessage = nullptr) override;
    bool powerControlSupported() const override { return true; }
    bool restartMachine(QString *errorMessage = nullptr) override;
    bool shutdownMachine(QString *errorMessage = nullptr) override;
    void setDisplaySleepInhibited(bool inhibited) override;
    void releaseDisplaySleepInhibitors() override;
    void ensureGlobalShortcutsDaemon() override;
    void runSessionStartupItems() override;

private:
    // Shared core of quitBackgroundApps / previewBackgroundAppQuit: enumerate the
    // user's GUI processes outside the keep-set and either SIGTERM them (dryRun
    // false) or just collect their names (dryRun true). Returns the names acted
    // on / that would be acted on.
    QStringList sweepBackgroundApps(const QStringList &allowedCommandLines, bool dryRun);
    // The hardcoded keep-set of session-critical processes (compositor, audio,
    // dbus, portals, the global-shortcut daemon, FocusOS itself) that must never
    // be SIGTERM'd or SIGSTOP'd — doing so wedges the session or the wake path.
    // Shared by the engage-time app sweep and the deep-idle process freeze.
    QSet<QString> criticalKeepSet() const;
    void startLockdownWatchdog();
    void stopLockdownWatchdog();
    void tickLockdownWatchdog();
    QStringList alwaysAllowedProcessNames() const;
    QString watchdogScriptPath() const;
    // Drop the ~/.focusos/session-exit marker that tells the kiosk respawn chain
    // (watchdog + focusos-session.sh) this exit is intentional. intent is one of
    // "signout" / "restart" / "shutdown" (informational; the file's presence is
    // what matters).
    void writeSessionExitMarker(const QString &intent);

    // While a network lock is live: if a browser is running but the blocker
    // extension stopped talking to its native host (i.e. it was disabled or
    // removed), clamp the network to a full deny and nag the user to re-enable
    // it; restore the routine allowlist once the extension is back.
    // Main-thread tail shared by applyNetworkPolicy + applyNetworkPolicyAsync:
    // load an already-resolved ruleset and arm the blocker policy / watchdog.
    bool commitNetworkPolicy(const QString &ruleset, const QStringList &allowedHosts,
                             QString *errorMessage);
    void ensureWatchdogTimer();
    void maybeStopWatchdogTimer();
    void enforceBlockerExtension();
    bool blockerExtensionAlive() const;
    bool chromiumBrowserRunning() const;
    void showExtensionDisabledAlert() const;

    NetGate m_netGate;
    QList<qint64> m_sessionPids;
    // PIDs SIGSTOP'd by freezeBackgroundProcesses() for the deep-idle sleep;
    // SIGCONT'd (and cleared) by thawBackgroundProcesses() on wake.
    QList<qint64> m_frozenPids;
    QTimer m_lockdownTimer;
    bool m_lockdownActive = false;
    // Counts lockdown-watchdog ticks. Used to force a full pkill sweep every Nth
    // tick (a safety backstop) while the in-process comm pre-check skips the
    // pkill fork+exec on the other ticks. See tickLockdownWatchdog.
    int m_lockdownSweepCounter = 0;
    // Network-lock state, tracked so the watchdog can restore the routine
    // allowlist after an extension-disabled full-deny clamp.
    bool m_networkLockActive = false;
    bool m_extensionBanActive = false;
    // The presence-ban only arms once the extension has checked in at least
    // once this session. If it never connects (broken host/extension wiring,
    // not user tampering) we stay quiet rather than stranding the user behind a
    // full-deny + nag loop. Set true the first tick the beacon is fresh.
    bool m_extensionSeenAlive = false;
    QStringList m_activeAllowedHosts;
    // The last successfully-resolved+applied ruleset text. Cached so the
    // extension-presence watchdog can RESTORE the allowlist after a full-deny
    // clamp without repeating the (slow, GUI-thread) DNS resolution.
    QString m_activeRuleset;
    // Wall-clock (ms) the "browser up but extension beacon stale" condition has
    // held continuously; 0 when clear. The ban only fires once it has persisted
    // past a debounce, so browser/extension startup lag doesn't false-trigger.
    qint64 m_extensionMissingSinceMs = 0;
    qint64 m_lastExtensionAlertMs = 0;
    QStringList m_alwaysAllowedCommandLines;
    QStringList m_sessionAllowedProcessNames;
    // Holds a systemd-inhibit lock (--what=idle) while a routine wants the
    // screen kept on; terminated to release. Reaped on destruction.
    QProcess m_displayInhibitor;
};
