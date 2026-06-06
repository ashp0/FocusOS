#pragma once

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>

// Centralised diagnostics for FocusOS.
//
// FocusOS runs as the *session shell* on a bare kwin_wayland seat — when
// something misbehaves there is no terminal to read qDebug from and no Plasma
// journal viewer in front of the user. So we tee every Qt log message
// (qDebug/qWarning/qCritical and the qCDebug categories) to a rotating file at
// ~/.focusos/logs/focusos.log while still printing to stderr. The file survives
// a crash (each line is flushed) and a crash-loop fallback to Plasma, so it is
// the one place to look when a routine "did something weird last night".
//
// Logger is also a QObject exposed to QML as `diagnostics`, so the SYSTEM tab of
// the Settings modal can show the recent log, reveal the folder, and let the
// user jot a manual diagnostic marker without leaving the kiosk.
class Logger final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString logFilePath READ logFilePath CONSTANT)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)

public:
    // Idempotent. Opens the log file, writes a session banner, and installs the
    // Qt message handler (chaining to the previous one so stderr still works).
    // Call once, early in main(), after QCoreApplication::setApplicationName.
    static void install();

    // The singleton created by install(); nullptr before install() runs.
    static Logger *instance();

    QString logFilePath() const;
    QString logDirectory() const;

    // Last `maxLines` lines of the current log, newest last. Cheap enough to call
    // on demand from the UI (reads at most the tail of the file).
    Q_INVOKABLE QString tail(int maxLines = 240) const;

    // Open the logs folder in the platform file manager (xdg-open / open).
    Q_INVOKABLE void revealLogs() const;

    // Let QML record a deliberate diagnostic marker (e.g. an error path the user
    // can later point at). Routed through the same file + stderr pipeline.
    Q_INVOKABLE void note(const QString &category, const QString &message);

    // Drop the on-disk logs (keeps the live file open, just truncated). Returns
    // false if the rotated siblings could not all be removed.
    Q_INVOKABLE bool clear();

    // Called by the installed message handler. Public so the free-function
    // handler can reach it; not meant for general use.
    void record(QtMsgType type, const char *category, const QString &message);

signals:
    // Emitted (coalesced) after new lines land, so a live viewer can refresh.
    void appended();

private:
    explicit Logger(const QString &path, QObject *parent = nullptr);
    void writeLine(const QString &line);
    void rotateIfNeeded();

    QString m_path;
    mutable QMutex m_mutex;
    QFile m_file;
    qint64 m_maxBytes = 1024 * 1024; // 1 MiB before rotation
    int m_keep = 3;                  // focusos.log + .1 .. .3
};
