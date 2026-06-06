#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class PlatformBackend
{
public:
    virtual ~PlatformBackend() = default;

    virtual QString name() const = 0;
    virtual void prepareRoutineSession(const QStringList &appPaths) { Q_UNUSED(appPaths); }
    virtual bool launchApps(const QStringList &appPaths, QString *errorMessage = nullptr) = 0;
    virtual bool openUrls(const QStringList &urls, QString *errorMessage = nullptr) = 0;
    virtual void terminateApps(const QStringList &appPaths) = 0;
    // Continue-after-expiry focus handoff (window management): whether any of the
    // apps this backend launched for the active routine are still alive. When the
    // user picks "Continue" the shell uses this to decide whether to yield focus
    // back to the routine's app (apps still open) or stay in front (a reading /
    // no-app routine, or the user already closed every window). No relaunch — this
    // only reports liveness; the shell yields by minimizing. False where
    // unsupported.
    virtual bool hasLiveRoutineApps() const { return false; }
    // Stop the routine lockdown watchdog (the launcher / spotlight kill sweep)
    // WITHOUT terminating the routine's own apps. Called when a routine ends or
    // admin (TOTP) access is granted, so launchers and the "Access Desktop"
    // path work again — otherwise the sweep keeps pkill'ing plasmashell the
    // instant it comes up. No-op where unsupported.
    virtual void endRoutineLockdown() {}
    // Strict enforcement (Task 1): quit the user's other running GUI apps when a
    // routine begins, so nothing but the routine/always-allowed surfaces remain.
    // allowedCommandLines = the routine's apps + the always-allowed list; the
    // backend additionally keeps a hardcoded set of session-critical processes
    // (compositor, portal, audio, dbus, focusos). No-op where unsupported.
    virtual void quitBackgroundApps(const QStringList &allowedCommandLines) { Q_UNUSED(allowedCommandLines); }
    // Screen lock (Task 6): turn the panel off / blank it. unlockScreen()
    // restores it. No-op where unsupported (the QML black overlay still shows).
    virtual void lockScreen() {}
    virtual void unlockScreen() {}
    // Put the physical display to sleep (DPMS off) WITHOUT engaging the in-app
    // lock overlay — the monitor wakes again on the next input. No-op where
    // unsupported.
    virtual void sleepDisplay() {}
    // Wake the physical display back up (DPMS on). Pairs with sleepDisplay() for
    // the deep-idle path so the panel is forced back on when the user returns.
    virtual void wakeDisplay() {}
    // Suspend the whole machine for the deep-idle sleep. Prefers the safe
    // suspend-to-idle (s2idle / "freeze") state over S3 suspend-to-RAM, since S3
    // black-screens some hardware on wake. Best-effort: returns false where
    // unsupported or not permitted, in which case the caller falls back to the
    // soft sleep (processes frozen + panel off + music paused).
    virtual bool suspendSystem() { return false; }
    // macOS-style "app sleep" for the deep-idle path: SIGSTOP every graphical app
    // the user left running so the CPU parks at idle, WITHOUT a kernel suspend
    // (which is what risks the black-screen-on-wake). Paired with sleepDisplay()
    // and paused music this is the safe, instantly-reversible deep sleep.
    // thawBackgroundProcesses() resumes exactly what was frozen. No-op where
    // unsupported.
    virtual void freezeBackgroundProcesses() {}
    virtual void thawBackgroundProcesses() {}
    virtual bool applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage = nullptr) = 0;
    // Async variant: the slow DNS resolution runs on a worker thread so the GUI
    // never freezes on engage. `onComplete(success, error)` is invoked on the
    // main (GUI) thread when the policy is fully applied (or has failed). The
    // default implementation (no-op platforms) completes immediately with
    // success. Callers must not launch routine apps until the callback fires
    // with success — the firewall must be up first.
    virtual void applyNetworkPolicyAsync(const QStringList &allowedHosts,
                                         std::function<void(bool, const QString &)> onComplete)
    {
        QString error;
        const bool ok = applyNetworkPolicy(allowedHosts, &error);
        onComplete(ok, error);
    }
    // Browser-routine network policy: write ONLY the signed browser-blocker
    // policy (the in-extension URL allowlist) WITHOUT the system-wide outbound
    // firewall. Used for routines that need a web browser — the blocker
    // extension enforces the allowlist at the navigation layer, so a full pf /
    // nftables egress block would be redundant and would break the allowed
    // sites' off-host subresources (CDNs, video, fonts, APIs). Fast (no DNS), so
    // it runs inline. dropNetworkPolicy() clears it again at routine end.
    virtual void applyBrowserBlockerPolicy(const QStringList &allowedHosts) { Q_UNUSED(allowedHosts); }
    virtual void dropNetworkPolicy() = 0;
    // Dry run for the strict engage-time app sweep (quitBackgroundApps): return
    // the process names that WOULD be SIGTERM'd for the given routine, without
    // killing anything. Lets the user verify the keep-set on their own machine
    // before trusting a routine to it. Empty where unsupported.
    virtual QStringList previewBackgroundAppQuit(const QStringList &allowedCommandLines)
    {
        Q_UNUSED(allowedCommandLines);
        return {};
    }
    virtual bool openSystemTerminal(QString *errorMessage = nullptr) = 0;
    virtual void terminateUnrestrictedApps() = 0;
    virtual bool launchDesktopShell(QString *errorMessage = nullptr) { Q_UNUSED(errorMessage); return false; }
    virtual void terminateDesktopShell() {}
    virtual bool desktopShellSupported() const { return false; }
    // Return the FocusOS shell to its home workspace + clean up any routine
    // window-management state. Called when a routine ends.
    virtual void restoreShellPlacement() {}
    // Put the machine into the locked home-screen posture for a fresh launch:
    // launching FocusOS itself acts like a lock ("you're looking to lock in").
    // On macOS this hides the Dock, starts the system-shortcut/Spotlight blocker
    // and disables Mission Control / Spaces, so the user can't reach the Dock or
    // a launcher the moment the app is up — the lock holds until a routine
    // engages or the 6-digit code lifts it. Called once at startup, before the
    // event loop, so a resumed routine's prepareRoutineSession() still overrides
    // it. No-op on Linux (the bare kwin session is already a locked shell).
    virtual void applyHomeScreenLock() {}
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
    // Invoked (on the main thread) each time the lockdown watchdog *newly*
    // detects a blocked launcher / time-sink the user reached for during a
    // routine — i.e. a distraction attempt. Edge-triggered: one call per
    // reach-for-the-launcher, not once per tick the process lingers. Backends
    // without a lockdown watchdog never fire it.
    virtual void setDistractionAttemptCallback(std::function<void()> callback) { Q_UNUSED(callback); }
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

    // Make sure the global-shortcuts daemon (kglobalacceld) is running so the
    // volume/brightness media keys FocusOS registers via KGlobalAccel actually
    // fire session-wide — not just when the shell has focus. A bare kwin_wayland
    // session starts no Plasma daemons, so without this the keys are inert
    // whenever another app is focused. Idempotent; no-op where unsupported.
    virtual void ensureGlobalShortcutsDaemon() {}
    // Run the user's session autostart items once, at launch: the standard XDG
    // autostart entries (~/.config/autostart/*.desktop, which a bare kwin session
    // would otherwise skip) plus a user-editable ~/.focusos/startup.sh. This is
    // how the user brings up input remappers (Toshy), tray agents, etc. that a
    // normal desktop session would have started for them. No-op where unsupported.
    virtual void runSessionStartupItems() {}

    // --- Persistent kiosk: launch at login + un-quittable ---------------------
    // Whether this platform can install a launch-at-login agent that also respawns
    // FocusOS on any quit/kill (so it can only be left via the 6-digit-gated quit
    // path). macOS uses a KeepAlive+RunAtLoad LaunchAgent. False where unsupported.
    virtual bool persistentKioskSupported() const { return false; }
    // Whether that agent is currently installed.
    virtual bool persistentKioskEnabled() const { return false; }
    // Install (true) or remove (false) the persistent kiosk agent. On macOS this
    // writes/removes ~/Library/LaunchAgents/com.focusos.login.plist; launchd loads
    // it at the next login (RunAtLoad) and KeepAlive respawns FocusOS on exit.
    // Returns false (with errorMessage) on failure / where unsupported.
    virtual bool setPersistentKiosk(bool enabled, QString *errorMessage = nullptr)
    {
        Q_UNUSED(enabled);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Persistent kiosk is not supported on this platform");
        }
        return false;
    }
    // Stand down the respawn machinery (the persistent login agent AND the
    // crash-recovery watchdog) so the imminent quit is NOT relaunched by launchd.
    // Called only from the 6-digit-gated "Quit FocusOS" path. The login agent is
    // reloaded by launchd at the next login, so this lifts the lock for THIS
    // session only. No-op where unsupported.
    virtual void prepareForAuthorizedQuit() {}

    // Whether this platform can log the user out of their account / session.
    virtual bool signOutSupported() const { return false; }
    // Log the user out: end the login session and drop back to the display
    // manager (SDDM). In the permanent kiosk install a plain process quit gets
    // respawned by the watchdog, so this must terminate the whole session.
    // Returns false (with errorMessage) when unsupported or it fails.
    virtual bool signOut(QString *errorMessage = nullptr)
    {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Sign out is not supported on this platform");
        }
        return false;
    }

    // Whether this platform can restart / shut down the whole machine. Same kiosk
    // caveat as signOut(): a plain quit would be respawned, so these must hand off
    // to the OS power machinery (logind / loginwindow) rather than just exiting.
    virtual bool powerControlSupported() const { return false; }
    // Reboot the machine. Returns false (with errorMessage) when unsupported/fails.
    virtual bool restartMachine(QString *errorMessage = nullptr)
    {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Restart is not supported on this platform");
        }
        return false;
    }
    // Power the machine off. Returns false (with errorMessage) when unsupported/fails.
    virtual bool shutdownMachine(QString *errorMessage = nullptr)
    {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Shut down is not supported on this platform");
        }
        return false;
    }
};
