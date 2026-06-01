#include "core/InspirationStore.h"

#include "core/RoutineManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>

namespace {

QString mediaTypeForSuffix(const QString &suffix)
{
    const QString normalized = suffix.toLower();
    if (normalized == QStringLiteral("png") ||
        normalized == QStringLiteral("jpg") ||
        normalized == QStringLiteral("jpeg") ||
        normalized == QStringLiteral("webp") ||
        normalized == QStringLiteral("gif") ||
        normalized == QStringLiteral("bmp") ||
        normalized == QStringLiteral("heic")) {
        return QStringLiteral("image");
    }

    if (normalized == QStringLiteral("mp4") ||
        normalized == QStringLiteral("mov") ||
        normalized == QStringLiteral("m4v") ||
        normalized == QStringLiteral("webm") ||
        normalized == QStringLiteral("mkv")) {
        return QStringLiteral("video");
    }

    return {};
}

} // namespace

InspirationStore::InspirationStore(QObject *parent)
    : QObject(parent)
    , m_directory(RoutineManager::dataDirectory() + QStringLiteral("/inspiration"))
{
    QDir().mkpath(m_directory);
    ensureReadme();

    // Resume the persisted fade cycle. Seed it on the first ever run so launch
    // one starts fully visible; thereafter we only rewrite it on an explicit
    // reset (task start / logout), which is how relaunch-after-crash stays dark.
    QSettings settings;
    m_fadeStartMs = settings.value(QStringLiteral("inspiration/fadeStartMs"), 0).toLongLong();
    if (m_fadeStartMs <= 0) {
        m_fadeStartMs = QDateTime::currentMSecsSinceEpoch();
        settings.setValue(QStringLiteral("inspiration/fadeStartMs"), m_fadeStartMs);
    }

    // A genuine fresh login should always start with the media fully visible,
    // otherwise a stale persisted cycle (last used hours ago, fade complete)
    // leaves the wallpaper at its near-invisible floor on launch. We still want a
    // mid-session watchdog respawn to resume where it left off, so distinguish
    // the two with a per-login marker in $XDG_RUNTIME_DIR (the kernel wipes that
    // dir on logout — same scope main() uses to run startup items once per login).
    const QString runtimeDir =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (!runtimeDir.isEmpty()) {
        const QString marker = runtimeDir + QStringLiteral("/focusos-fade-login");
        if (!QFileInfo::exists(marker)) {
            m_fadeStartMs = QDateTime::currentMSecsSinceEpoch();
            settings.setValue(QStringLiteral("inspiration/fadeStartMs"), m_fadeStartMs);
            QFile markerFile(marker);
            if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                markerFile.close();
            }
        }
    }

    m_scanTimer.setSingleShot(true);
    m_scanTimer.setInterval(250);
    connect(&m_scanTimer, &QTimer::timeout, this, &InspirationStore::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &InspirationStore::scheduleScan);

    m_watcher.addPath(m_directory);
    scan();
}

QVariantList InspirationStore::assets() const
{
    return m_assets;
}

QString InspirationStore::directory() const
{
    return m_directory;
}

qlonglong InspirationStore::fadeStartMs() const
{
    return m_fadeStartMs;
}

void InspirationStore::resetFadeCycle()
{
    m_fadeStartMs = QDateTime::currentMSecsSinceEpoch();
    QSettings settings;
    settings.setValue(QStringLiteral("inspiration/fadeStartMs"), m_fadeStartMs);
    emit fadeStartMsChanged();
}

void InspirationStore::scheduleScan()
{
    if (!m_watcher.directories().contains(m_directory) && QFileInfo::exists(m_directory)) {
        m_watcher.addPath(m_directory);
    }
    m_scanTimer.start();
}

void InspirationStore::scan()
{
    QDir directory(m_directory);
    const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    QVariantList nextAssets;
    for (const QFileInfo &entry : entries) {
        const QString type = mediaTypeForSuffix(entry.suffix());
        if (type.isEmpty()) {
            continue;
        }

        QVariantMap asset;
        asset.insert(QStringLiteral("url"), QUrl::fromLocalFile(entry.absoluteFilePath()).toString());
        asset.insert(QStringLiteral("type"), type);
        asset.insert(QStringLiteral("name"), entry.completeBaseName());
        nextAssets.append(asset);
    }

    if (nextAssets == m_assets) {
        return;
    }

    m_assets = nextAssets;
    emit assetsChanged();
}

void InspirationStore::ensureReadme() const
{
    const QString readmePath = QDir(m_directory).filePath(QStringLiteral("README.txt"));
    if (QFileInfo::exists(readmePath)) {
        return;
    }

    QFile readme(readmePath);
    if (!readme.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    readme.write("Drop images or videos here to let FocusOS fold them into the ambient background.\n");
    readme.write("Media loops continuously. It starts visible, then fades toward a barely noticeable prompt over 30 minutes.\n");
    readme.write("The fade restarts when you begin a task or return to the login screen, and resumes where it left off after a relaunch.\n");
}
