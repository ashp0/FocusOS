#pragma once

#include <QObject>
#include <QTimer>

class SystemStatus final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString batteryLabel READ batteryLabel NOTIFY statusChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY statusChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY statusChanged)
    Q_PROPERTY(int volumePercent READ volumePercent NOTIFY statusChanged)
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable NOTIFY statusChanged)
    Q_PROPERTY(int brightnessPercent READ brightnessPercent NOTIFY statusChanged)
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY statusChanged)
    // Passwordless elevated launch (macOS): once enabled (admin password entered
    // once in the SYSTEM tab) FocusOS re-launches itself as root via a NOPASSWD
    // sudoers rule, so it can be started from the Dock with no Terminal and no
    // prompt. See main.cpp's maybeReexecElevated and enableElevatedLaunch below.
    Q_PROPERTY(bool elevatedLaunchSupported READ elevatedLaunchSupported CONSTANT)
    Q_PROPERTY(bool elevatedLaunchEnabled READ elevatedLaunchEnabled NOTIFY elevatedLaunchChanged)
    Q_PROPERTY(bool runningAsRoot READ runningAsRoot CONSTANT)

public:
    explicit SystemStatus(QObject *parent = nullptr);

    QString batteryLabel() const;
    int batteryPercent() const;
    bool batteryCharging() const;
    int volumePercent() const;
    bool volumeAvailable() const;
    int brightnessPercent() const;
    bool brightnessAvailable() const;

    Q_INVOKABLE void adjustSystemVolume(int deltaPercent);
    Q_INVOKABLE void setSystemVolume(int percent);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void adjustBrightness(int deltaPercent);
    Q_INVOKABLE void setBrightness(int percent);
    Q_INVOKABLE void refresh();

    // User session startup script (~/.focusos/startup.sh) — edited from the admin
    // Settings pane and run once per login by the platform backend. These let the
    // QML editor read/write the file without its own filesystem access.
    Q_INVOKABLE QString startupScriptPath() const;
    Q_INVOKABLE QString readStartupScript() const;
    Q_INVOKABLE bool writeStartupScript(const QString &contents);

    // Elevated-launch (macOS) — see the Q_PROPERTYs above.
    bool elevatedLaunchSupported() const;
    bool elevatedLaunchEnabled() const;
    bool runningAsRoot() const;
    // Absolute path of the FocusOS binary the sudoers rule authorizes (shown in
    // the UI so the user knows exactly what gets passwordless root).
    Q_INVOKABLE QString elevatedBinaryPath() const;
    // Re-probe whether the NOPASSWD rule is installed (called when the modal opens).
    Q_INVOKABLE void refreshElevatedLaunch();
    // Install the NOPASSWD sudoers rule. When FocusOS is already root the password
    // is ignored (no auth needed); otherwise the admin password is piped to
    // `sudo -S`. Returns an empty string on success, or a human-readable error.
    Q_INVOKABLE QString enableElevatedLaunch(const QString &adminPassword);
    // Remove the rule again. Same password semantics as enableElevatedLaunch.
    Q_INVOKABLE QString disableElevatedLaunch(const QString &adminPassword);

signals:
    void statusChanged();
    void elevatedLaunchChanged();

private:
    void refreshStatus();
    // Coalesce volume writes: dragging the slider fires setSystemVolume() dozens
    // of times a second, and each write spawns a heavy process (osascript on
    // macOS). Without throttling, the flood thrashes the audio system so the
    // volume only seems to "land" on mouse-release. We apply a leading write
    // immediately and a single trailing write of the final value.
    void flushPendingVolume();

    void refreshElevatedLaunchState();

    QTimer m_statusTimer;
    QTimer m_volumeWriteThrottle;
    bool m_elevatedLaunchEnabled = false;
    int m_pendingVolume = -1;
    int m_lastWrittenVolume = -1;
    int m_batteryPercent = -1;
    bool m_batteryCharging = false;
    int m_volumePercent = -1;
    bool m_volumeAvailable = false;
    int m_brightnessPercent = -1;
    bool m_brightnessAvailable = false;
};
