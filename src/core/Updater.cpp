#include "core/Updater.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QString readSnapshotPointer(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString value = QString::fromUtf8(file.readAll()).trimmed();
    return value;
}

} // namespace

Updater::Updater(QObject *parent)
    : QObject(parent)
{
    m_probationTimer.setInterval(1000);
    m_probationTimer.setSingleShot(false);
    connect(&m_probationTimer, &QTimer::timeout, this, &Updater::tickProbation);

    evaluateProbationAtStartup();
}

bool Updater::supported() const
{
#if defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

bool Updater::busy() const
{
    return m_busy;
}

QString Updater::status() const
{
    return m_status;
}

QString Updater::log() const
{
    return m_log;
}

bool Updater::updatePending() const
{
    return m_updatePending;
}

int Updater::probationRemainingSeconds() const
{
    return m_probationRemainingSeconds;
}

bool Updater::revertAvailable() const
{
    return m_updatePending && !m_snapshotPath.isEmpty();
}

void Updater::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void Updater::setStatus(const QString &status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged();
}

void Updater::appendLog(const QString &chunk)
{
    if (chunk.isEmpty()) {
        return;
    }
    m_log.append(chunk);
    // Keep the buffer bounded so a long rebuild doesn't grow without limit.
    const int kMaxLog = 64 * 1024;
    if (m_log.size() > kMaxLog) {
        m_log = m_log.right(kMaxLog);
    }
    emit logChanged();
}

QString Updater::dataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.focusos");
}

QString Updater::pendingPath()
{
    return dataDirectory() + QStringLiteral("/update-pending.json");
}

QString Updater::snapshotPointerPath()
{
    return dataDirectory() + QStringLiteral("/pending-snapshot");
}

QString Updater::binaryPath() const
{
    return QCoreApplication::applicationFilePath();
}

QString Updater::repoDirectory() const
{
    const QString override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("FOCUSOS_REPO"));
    if (!override.isEmpty() && QFileInfo::exists(override)) {
        return override;
    }
    // Dev + home-checkout install both run the binary out of <repo>/build/.
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir.absolutePath();
}

QString Updater::scriptPath(const QString &scriptName) const
{
    const QStringList installed {
        QStringLiteral("/usr/local/lib/focusos/") + scriptName,
        QStringLiteral("/opt/focusos/lib/") + scriptName,
    };
    for (const QString &candidate : installed) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    const QString repoScript = repoDirectory() + QStringLiteral("/packaging/linux/") + scriptName;
    if (QFileInfo::exists(repoScript)) {
        return repoScript;
    }
    return {};
}

void Updater::runUpdate()
{
    if (m_busy || m_process) {
        return;
    }
    if (!supported()) {
        setStatus(QStringLiteral("UPDATES ARE ONLY SUPPORTED ON THE LINUX BUILD"));
        return;
    }

    const QString script = scriptPath(QStringLiteral("focusos-update.sh"));
    if (script.isEmpty()) {
        setStatus(QStringLiteral("UPDATE SCRIPT NOT FOUND"));
        return;
    }

    m_log.clear();
    emit logChanged();
    setStatus(QStringLiteral("UPDATING — PULLING + REBUILDING…"));
    setBusy(true);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        appendLog(QString::fromUtf8(m_process->readAllStandardOutput()));
    });
    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                setBusy(false);
                m_process->deleteLater();
                m_process = nullptr;

                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    m_snapshotPath = readSnapshotPointer(snapshotPointerPath());

                    QJsonObject pending;
                    pending.insert(QStringLiteral("deadline"),
                                   static_cast<qint64>(QDateTime::currentSecsSinceEpoch() + kProbationSeconds));
                    pending.insert(QStringLiteral("launch_count"), 0);
                    pending.insert(QStringLiteral("snapshot"), m_snapshotPath);

                    QSaveFile file(pendingPath());
                    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        file.write(QJsonDocument(pending).toJson(QJsonDocument::Compact));
                        file.commit();
                    }

                    setStatus(QStringLiteral("UPDATE BUILT — RELAUNCHING…"));
                    beginRelaunch();
                } else {
                    setStatus(QStringLiteral("UPDATE FAILED — REVIEW THE LOG"));
                }
            });

    m_process->start(QStringLiteral("bash"), {script, repoDirectory(), binaryPath()});
}

void Updater::runRevert()
{
    if (m_busy || m_process) {
        return;
    }

    if (m_snapshotPath.isEmpty()) {
        // Recover the snapshot path from the pending file if we don't have it
        // in memory (e.g. a fresh launch where the user hits Revert directly).
        QFile file(pendingPath());
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            if (document.isObject()) {
                m_snapshotPath = document.object().value(QStringLiteral("snapshot")).toString();
            }
        }
    }
    if (m_snapshotPath.isEmpty() || !QFileInfo::exists(m_snapshotPath)) {
        setStatus(QStringLiteral("NO SNAPSHOT AVAILABLE TO REVERT TO"));
        return;
    }

    const QString script = scriptPath(QStringLiteral("focusos-revert.sh"));
    if (script.isEmpty()) {
        setStatus(QStringLiteral("REVERT SCRIPT NOT FOUND"));
        return;
    }

    m_log.clear();
    emit logChanged();
    setStatus(QStringLiteral("REVERTING TO PREVIOUS BUILD…"));
    setBusy(true);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        appendLog(QString::fromUtf8(m_process->readAllStandardOutput()));
    });
    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                setBusy(false);
                m_process->deleteLater();
                m_process = nullptr;

                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    // The reverted binary is the known-good one — end probation
                    // so it isn't itself put back under watch.
                    QFile::remove(pendingPath());
                    m_updatePending = false;
                    m_probationRemainingSeconds = 0;
                    m_probationTimer.stop();
                    emit probationChanged();
                    setStatus(QStringLiteral("REVERTED — RELAUNCHING PREVIOUS BUILD…"));
                    beginRelaunch();
                } else {
                    setStatus(QStringLiteral("REVERT FAILED — REVIEW THE LOG"));
                }
            });

    m_process->start(QStringLiteral("bash"), {script, binaryPath(), m_snapshotPath});
}

void Updater::confirmHealthy()
{
    if (!m_updatePending) {
        return;
    }
    commitProbation();
}

void Updater::beginRelaunch()
{
    const QString script = scriptPath(QStringLiteral("focusos-relaunch.sh"));
    if (!script.isEmpty()) {
        // The relauncher waits for our single-instance lock to release, then
        // execs the (possibly new) binary.
        QProcess::startDetached(QStringLiteral("bash"), {script, binaryPath()});
    }
    // Quit so the QLockFile releases. On the permanent install the kiosk
    // watchdog would also respawn us; the relauncher covers the dev case.
    QTimer::singleShot(150, qApp, &QCoreApplication::quit);
}

void Updater::evaluateProbationAtStartup()
{
    QFile file(pendingPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        QFile::remove(pendingPath());
        return;
    }

    QJsonObject pending = document.object();
    const qint64 deadline = pending.value(QStringLiteral("deadline")).toVariant().toLongLong();
    const int launchCount = pending.value(QStringLiteral("launch_count")).toInt(0) + 1;
    m_snapshotPath = pending.value(QStringLiteral("snapshot")).toString();

    // Persist the incremented launch counter so a crash loop is detectable
    // across successive respawns.
    pending.insert(QStringLiteral("launch_count"), launchCount);
    {
        QSaveFile out(pendingPath());
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(QJsonDocument(pending).toJson(QJsonDocument::Compact));
            out.commit();
        }
    }

    if (launchCount >= kCrashLoopLaunchThreshold) {
        setStatus(QStringLiteral("NEW BUILD UNSTABLE — AUTO-REVERTING"));
        // Defer so the event loop (and the QLockFile) are fully up first.
        QTimer::singleShot(0, this, &Updater::runRevert);
        return;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (deadline <= 0 || now >= deadline) {
        // Probation window already elapsed — the build proved itself.
        commitProbation();
        return;
    }

    m_updatePending = true;
    m_probationRemainingSeconds = static_cast<int>(deadline - now);
    emit probationChanged();
    setStatus(QStringLiteral("NEW BUILD ON PROBATION — REVERT AVAILABLE"));
    m_probationTimer.start();
}

void Updater::tickProbation()
{
    if (!m_updatePending) {
        m_probationTimer.stop();
        return;
    }
    --m_probationRemainingSeconds;
    if (m_probationRemainingSeconds <= 0) {
        commitProbation();
        return;
    }
    emit probationChanged();
}

void Updater::commitProbation()
{
    m_probationTimer.stop();
    QFile::remove(pendingPath());
    // Reclaim the snapshot now that the build is trusted.
    if (!m_snapshotPath.isEmpty()) {
        const QFileInfo info(m_snapshotPath);
        QDir snapshotDir = info.dir();
        if (snapshotDir.exists() && snapshotDir.dirName().startsWith(QStringLiteral("snap-"))) {
            snapshotDir.removeRecursively();
        } else {
            QFile::remove(m_snapshotPath);
        }
    }
    m_snapshotPath.clear();
    m_updatePending = false;
    m_probationRemainingSeconds = 0;
    emit probationChanged();
    setStatus(QStringLiteral("UPDATE CONFIRMED — RUNNING LATEST BUILD"));
}
