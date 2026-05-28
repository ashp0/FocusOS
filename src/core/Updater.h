#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class QProcess;

// In-app updater for the no-sudo, home-checkout-rebuild-in-place model.
//
//   runUpdate()  → git pull + rebuild (streamed to the UI), snapshot the old
//                  binary, then relaunch into the new build under a 30-minute
//                  probation.
//   probation    → on the post-update launch we arm a deadline. If the new
//                  build crash-loops (the respawn watchdog keeps relaunching
//                  and the launch counter climbs past a threshold) we auto-
//                  revert. If the deadline passes cleanly we auto-commit.
//   runRevert()  → restore the snapshotted previous binary and relaunch.
//
// All the privileged-free filesystem work lives in shell scripts under
// packaging/linux/; this class orchestrates them and owns the probation state
// machine (~/.focusos/update-pending.json).
class Updater final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(bool updatePending READ updatePending NOTIFY probationChanged)
    Q_PROPERTY(int probationRemainingSeconds READ probationRemainingSeconds NOTIFY probationChanged)
    Q_PROPERTY(bool revertAvailable READ revertAvailable NOTIFY probationChanged)

public:
    explicit Updater(QObject *parent = nullptr);

    bool supported() const;
    bool busy() const;
    QString status() const;
    QString log() const;
    bool updatePending() const;
    int probationRemainingSeconds() const;
    bool revertAvailable() const;

    Q_INVOKABLE void runUpdate();
    Q_INVOKABLE void runRevert();
    // The user pressed "Keep this build" — end probation immediately.
    Q_INVOKABLE void confirmHealthy();

signals:
    void busyChanged();
    void statusChanged();
    void logChanged();
    void probationChanged();

private:
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void appendLog(const QString &chunk);

    QString repoDirectory() const;
    QString scriptPath(const QString &scriptName) const;
    static QString dataDirectory();
    static QString pendingPath();
    static QString snapshotPointerPath();
    QString binaryPath() const;

    // Probation state machine, evaluated once at startup.
    void evaluateProbationAtStartup();
    void tickProbation();
    void commitProbation();   // healthy → drop the pending file + snapshot
    void beginRelaunch();     // detached relauncher + quit so the lock releases

    QProcess *m_process = nullptr;
    QTimer m_probationTimer;
    bool m_busy = false;
    QString m_status;
    QString m_log;
    bool m_updatePending = false;
    int m_probationRemainingSeconds = 0;
    QString m_snapshotPath;

    static constexpr int kProbationSeconds = 30 * 60;
    // If the post-update binary is launched this many times within the
    // probation window, treat it as a crash loop and auto-revert.
    static constexpr int kCrashLoopLaunchThreshold = 3;
};
