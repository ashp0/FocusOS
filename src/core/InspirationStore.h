#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

class InspirationStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList assets READ assets NOTIFY assetsChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    // Epoch-ms anchor for the ambient fade cycle. Persisted across runs so a
    // force-quit/relaunch resumes the same fade rather than resetting to full.
    Q_PROPERTY(qlonglong fadeStartMs READ fadeStartMs NOTIFY fadeStartMsChanged)

public:
    explicit InspirationStore(QObject *parent = nullptr);

    QVariantList assets() const;
    QString directory() const;

    qlonglong fadeStartMs() const;

    // Restart the fade: media becomes fully visible and the 30-minute cycle
    // begins again. The fresh start time is persisted immediately, which is
    // what makes a clean logout (resets) differ from a crash (does not) on the
    // next launch. Called on task start and on logout / return to login.
    Q_INVOKABLE void resetFadeCycle();

signals:
    void assetsChanged();
    void fadeStartMsChanged();

private:
    void scheduleScan();
    void scan();
    void ensureReadme() const;

    QString m_directory;
    QVariantList m_assets;
    QFileSystemWatcher m_watcher;
    QTimer m_scanTimer;
    qlonglong m_fadeStartMs = 0;
};
