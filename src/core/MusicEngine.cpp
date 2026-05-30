#include "core/MusicEngine.h"

#include "core/RoutineManager.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QProcess>
#include <QRandomGenerator>
#include <QResource>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

Q_LOGGING_CATEGORY(lcMusic, "focusos.music")

namespace {

QString configPath()
{
    return RoutineManager::dataDirectory() + QStringLiteral("/config.json");
}

QString musicDirectory()
{
    return RoutineManager::dataDirectory() + QStringLiteral("/music");
}

QString fallbackTrackPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates {
        QDir(appDir).filePath(QStringLiteral("../Resources/assets/music/ambient_default.ogg")),
        QDir(appDir).filePath(QStringLiteral("assets/music/ambient_default.ogg")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("assets/music/ambient_default.ogg"))
    };

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    if (QResource(QStringLiteral(":/qt/qml/FocusOS/assets/music/ambient_default.ogg")).isValid()) {
        return QStringLiteral("qrc:/qt/qml/FocusOS/assets/music/ambient_default.ogg");
    }
    return {};
}

void ensureMusicFolderReadme()
{
    const QString path = QDir(musicDirectory()).filePath(QStringLiteral("README.txt"));
    if (QFileInfo::exists(path)) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    file.write("FocusOS ambient music\n");
    file.write("=====================\n\n");
    file.write("Place .mp3 or .ogg files in this folder.\n");
    file.write("FocusOS shuffles them, loops them, and fades them around routines.\n");
    file.commit();
}

QJsonObject readConfigObject()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject {};
}

QString normalizedBehavior(const QString &behavior)
{
    const QString normalized = behavior.trimmed().toLower();
    if (normalized == QStringLiteral("low") || normalized == QStringLiteral("same")) {
        return normalized;
    }
    return QStringLiteral("stop");
}

} // namespace

MusicEngine::MusicEngine(QObject *parent)
    : QObject(parent)
    , m_fadeAnimation(&m_audioOutput, "volume", this)
{
    QDir().mkpath(musicDirectory());
    ensureMusicFolderReadme();
    m_player.setAudioOutput(&m_audioOutput);
    m_audioOutput.setVolume(0.0);
    m_fadeAnimation.setEasingCurve(QEasingCurve::InOutQuad);

    // Audio diagnostics. On Linux the music engine "silently fails" when the
    // Qt Multimedia backend can't be loaded or has no usable output device —
    // every other part of FocusOS keeps working, so the symptom is just
    // silence with no clue why. Surface the backend, the output device, and any
    // playback error so a broken setup is diagnosable from the log instead of
    // guessable. Set QT_LOGGING_RULES="focusos.music.debug=true" to see the
    // startup lines; warnings are always emitted.
    const QByteArray backend = qgetenv("QT_MEDIA_BACKEND");
    const QAudioDevice outputDevice = m_audioOutput.device();
    qCDebug(lcMusic, "media backend: %s | default output device: %s",
            backend.isEmpty() ? "(platform default)" : backend.constData(),
            outputDevice.isNull() ? "(none — no audio device available!)"
                                  : qPrintable(outputDevice.description()));
    if (outputDevice.isNull() || QMediaDevices::audioOutputs().isEmpty()) {
        qCWarning(lcMusic, "no audio output device detected — ambient music will be silent "
                           "(is PipeWire/PulseAudio running in this session?)");
    }

    connect(&m_player, &QMediaPlayer::errorOccurred, this,
            [](QMediaPlayer::Error error, const QString &errorString) {
        if (error != QMediaPlayer::NoError) {
            // Most common on a Linux box where qt6-multimedia is installed but its
            // decoder/backend plugin (FFmpeg or GStreamer) is missing: the file is
            // found but never decodes, so playback stays silent.
            qCWarning(lcMusic, "playback error %d: %s", static_cast<int>(error),
                      qPrintable(errorString));
        }
    });

    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia) {
            qCWarning(lcMusic, "invalid media (cannot decode '%s') — missing audio codec/backend?",
                      qPrintable(m_player.source().toString()));
        }
        if (status == QMediaPlayer::EndOfMedia && !m_playbackQueue.isEmpty()) {
            advanceSource();
        }
    });

    connect(&m_fadeAnimation, &QPropertyAnimation::finished, this, [this] {
        if (m_stopAfterFade) {
            m_stopAfterFade = false;
            pausePlayback();
        }
    });

    loadConfig();
    refreshMusicFiles();
    QTimer::singleShot(0, this, [this] {
        if (m_enabled && available()) {
            startPlayback(3000);
        }
    });
}

bool MusicEngine::enabled() const
{
    return m_enabled;
}

bool MusicEngine::available() const
{
    return !m_playbackQueue.isEmpty();
}

int MusicEngine::volume() const
{
    return m_volume;
}

QString MusicEngine::engageBehavior() const
{
    return m_engageBehavior;
}

QStringList MusicEngine::musicFiles() const
{
    return m_musicFileNames;
}

QString MusicEngine::importStatus() const
{
    return m_importStatus;
}

void MusicEngine::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    saveConfig();
    emit enabledChanged();

    if (!m_enabled) {
        // Pause instead of stop so resuming continues from the same offset.
        m_stopAfterFade = false;
        m_fadeAnimation.stop();
        m_fadeAnimation.setDuration(600);
        m_fadeAnimation.setStartValue(m_audioOutput.volume());
        m_fadeAnimation.setEndValue(0.0);
        m_fadeAnimation.start();
        // Pause exactly when fade finishes — schedule directly.
        QTimer::singleShot(620, this, [this] {
            if (!m_enabled) {
                m_player.pause();
            }
        });
        return;
    }

    if (available()) {
        applyEngagedState(3000);
    }
}

void MusicEngine::setVolume(int volume)
{
    const int clamped = qBound(0, volume, 100);
    if (m_volume == clamped) {
        return;
    }

    m_volume = clamped;
    saveConfig();
    emit volumeChanged();

    if (!m_enabled || !available()) {
        return;
    }

    if (!m_routineEngaged || m_engageBehavior == QStringLiteral("same")) {
        fadeTo(configuredVolume(), 120, false);
    } else if (m_engageBehavior == QStringLiteral("low")) {
        fadeTo(lowVolume(), 120, false);
    }
}

void MusicEngine::setEngageBehavior(const QString &behavior)
{
    const QString normalized = normalizedBehavior(behavior);
    if (m_engageBehavior == normalized) {
        return;
    }

    m_engageBehavior = normalized;
    saveConfig();
    emit engageBehaviorChanged();
    applyEngagedState(400);
}

void MusicEngine::refreshMusicFiles()
{
    QDir directory(musicDirectory());
    directory.mkpath(QStringLiteral("."));

    const QStringList filters {
        QStringLiteral("*.mp3"),
        QStringLiteral("*.ogg")
    };
    const QFileInfoList entries = directory.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);

    m_musicFilePaths.clear();
    m_musicFileNames.clear();
    for (const QFileInfo &entry : entries) {
        m_musicFilePaths.append(entry.absoluteFilePath());
        m_musicFileNames.append(entry.fileName());
    }

    rebuildPlaybackQueue();
    emit musicFilesChanged();

    if (m_enabled && available() && m_player.playbackState() != QMediaPlayer::PlayingState) {
        applyEngagedState(1200);
    } else if (!available()) {
        stopPlayback();
    }
}

void MusicEngine::openMusicFolder() const
{
    // Opens the user's ~/.focusos/music in a file manager. On the always-
    // on-top FocusOS shell this is unreliable — the file manager spawns
    // BEHIND the FocusOS fullscreen and the user can't see it, which is
    // why we also expose importMusicFile() as the primary affordance.
    const QString dir = musicDirectory();
    QDir().mkpath(dir);
    ensureMusicFolderReadme();

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        return;
    }

    // QDesktopServices silently fails on some KDE setups where xdg-open isn't
    // wired to a file manager. Try the common managers directly so the user
    // still sees their folder open.
    const QStringList fileManagers {
        QStringLiteral("xdg-open"),
        QStringLiteral("dolphin"),
        QStringLiteral("nautilus"),
        QStringLiteral("nemo"),
        QStringLiteral("thunar"),
        QStringLiteral("pcmanfm"),
        QStringLiteral("caja")
    };
    for (const QString &manager : fileManagers) {
        const QString path = QStandardPaths::findExecutable(manager);
        if (path.isEmpty()) {
            continue;
        }
        if (QProcess::startDetached(path, {dir})) {
            return;
        }
    }
}

QString MusicEngine::musicFolderPath() const
{
    return musicDirectory();
}

QString MusicEngine::importMusicFile()
{
    // The native KDE portal dialog can appear behind the always-on-top shell.
    // A non-native, application-modal dialog with WindowStaysOnTopHint keeps
    // the importer visible while FocusOS owns the screen.
    QFileDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Add Music File"));
    dialog.setDirectory(QDir::homePath());
    dialog.setNameFilters({QStringLiteral("Audio Files (*.mp3 *.ogg)"), QStringLiteral("All Files (*)")});
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, true);

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    const QString sourcePath = dialog.selectedFiles().value(0);
    if (sourcePath.isEmpty()) {
        return {};
    }

    const QFileInfo info(sourcePath);
    const QString suffix = info.suffix().toLower();
    if (suffix != QStringLiteral("mp3") && suffix != QStringLiteral("ogg")) {
        setImportStatus(QStringLiteral("UNSUPPORTED AUDIO FORMAT"));
        return {};
    }

    QDir().mkpath(musicDirectory());
    QString destination = QDir(musicDirectory()).filePath(info.fileName());
    // Avoid clobbering a same-named existing file.
    if (QFileInfo::exists(destination)) {
        int counter = 1;
        QString stem = info.completeBaseName();
        forever {
            const QString candidate = QDir(musicDirectory()).filePath(
                QStringLiteral("%1 (%2).%3").arg(stem).arg(counter).arg(suffix));
            if (!QFileInfo::exists(candidate)) {
                destination = candidate;
                break;
            }
            ++counter;
        }
    }

    if (!QFile::copy(sourcePath, destination)) {
        setImportStatus(QStringLiteral("IMPORT FAILED"));
        return {};
    }

    refreshMusicFiles();
    setImportStatus(QStringLiteral("IMPORTED %1").arg(QFileInfo(destination).fileName()));
    return destination;
}

void MusicEngine::setImportStatus(const QString &status)
{
    if (m_importStatus == status) {
        return;
    }
    m_importStatus = status;
    emit importStatusChanged();
}

void MusicEngine::setRoutineEngaged(bool engaged)
{
    if (m_routineEngaged == engaged) {
        return;
    }

    m_routineEngaged = engaged;
    applyEngagedState(engaged ? 2000 : 3000);
}

void MusicEngine::loadConfig()
{
    const QJsonObject root = readConfigObject();
    m_enabled = root.value(QStringLiteral("music_enabled")).toBool(true);
    m_volume = qBound(0, root.value(QStringLiteral("music_volume")).toInt(35), 100);
    m_engageBehavior = normalizedBehavior(root.value(QStringLiteral("music_engage_behavior")).toString(QStringLiteral("stop")));
    saveConfig();
}

bool MusicEngine::saveConfig() const
{
    QJsonObject root = readConfigObject();
    root.insert(QStringLiteral("music_enabled"), m_enabled);
    root.insert(QStringLiteral("music_volume"), m_volume);
    root.insert(QStringLiteral("music_engage_behavior"), m_engageBehavior);

    QSaveFile saveFile(configPath());
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    saveFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return saveFile.commit();
}

void MusicEngine::rebuildPlaybackQueue()
{
    const QString currentSource = m_currentSourceIndex >= 0 && m_currentSourceIndex < m_playbackQueue.size()
        ? m_playbackQueue.at(m_currentSourceIndex)
        : QString();

    m_playbackQueue = m_musicFilePaths;
    if (m_playbackQueue.isEmpty()) {
        const QString fallback = fallbackTrackPath();
        if (!fallback.isEmpty()) {
            m_playbackQueue.append(fallback);
        }
    }
    for (int i = m_playbackQueue.size() - 1; i > 0; --i) {
        const int swapIndex = QRandomGenerator::global()->bounded(i + 1);
        m_playbackQueue.swapItemsAt(i, swapIndex);
    }

    m_currentSourceIndex = currentSource.isEmpty() ? -1 : m_playbackQueue.indexOf(currentSource);
    if (m_currentSourceIndex < 0 && !m_playbackQueue.isEmpty()) {
        m_currentSourceIndex = 0;
    }
}

void MusicEngine::startPlayback(int fadeMs)
{
    if (!m_enabled || !available()) {
        return;
    }

    if (m_currentSourceIndex < 0) {
        m_currentSourceIndex = 0;
    }

    if (m_player.source().isEmpty()) {
        playCurrentSource();
    }

    m_player.play();
    // For tracks longer than 9 minutes, jump to a 3-minute multiple offset
    // so each engagement hears a different section.
    if (!m_seekedThisSource) {
        seekToInterestingOffset();
    }
    fadeTo(configuredVolume(), fadeMs, false);
}

void MusicEngine::seekToInterestingOffset()
{
    if (m_player.duration() <= 0) {
        // Duration may not be reported yet — wait for the metadata.
        QMetaObject::Connection *handle = new QMetaObject::Connection;
        *handle = connect(&m_player, &QMediaPlayer::durationChanged, this, [this, handle](qint64) {
            disconnect(*handle);
            delete handle;
            seekToInterestingOffset();
        });
        return;
    }

    const qint64 durationMs = m_player.duration();
    const qint64 nineMinutesMs = 9LL * 60 * 1000;
    if (durationMs < nineMinutesMs) {
        m_seekedThisSource = true;
        return;
    }

    // Build the list of 3-minute multiples that leave at least 30s of runway.
    QList<qint64> offsets;
    const qint64 stride = 3LL * 60 * 1000;
    const qint64 safeMax = durationMs - 30LL * 1000;
    for (qint64 offset = stride; offset <= safeMax; offset += stride) {
        offsets.append(offset);
    }
    if (offsets.isEmpty()) {
        m_seekedThisSource = true;
        return;
    }

    const int pick = QRandomGenerator::global()->bounded(offsets.size());
    m_player.setPosition(offsets.at(pick));
    m_seekedThisSource = true;
}

void MusicEngine::fadeTo(qreal targetVolume, int durationMs, bool stopAfterFade)
{
    m_stopAfterFade = stopAfterFade;
    m_fadeAnimation.stop();
    m_fadeAnimation.setDuration(qMax(0, durationMs));
    m_fadeAnimation.setStartValue(m_audioOutput.volume());
    m_fadeAnimation.setEndValue(qBound(0.0, targetVolume, 1.0));
    m_fadeAnimation.start();
}

void MusicEngine::stopPlayback()
{
    m_fadeAnimation.stop();
    m_audioOutput.setVolume(0.0);
    m_player.stop();
    m_seekedThisSource = false;
}

void MusicEngine::pausePlayback()
{
    m_fadeAnimation.stop();
    m_audioOutput.setVolume(0.0);
    m_player.pause();
}

void MusicEngine::playCurrentSource()
{
    if (m_currentSourceIndex < 0 || m_currentSourceIndex >= m_playbackQueue.size()) {
        return;
    }

    const QString source = m_playbackQueue.at(m_currentSourceIndex);
    m_player.setSource(source.startsWith(QStringLiteral("qrc:/")) ? QUrl(source) : QUrl::fromLocalFile(source));
    m_seekedThisSource = false;
}

void MusicEngine::advanceSource()
{
    if (m_playbackQueue.isEmpty()) {
        stopPlayback();
        return;
    }

    m_currentSourceIndex = (m_currentSourceIndex + 1) % m_playbackQueue.size();
    if (m_currentSourceIndex == 0) {
        rebuildPlaybackQueue();
    }
    playCurrentSource();
    m_player.play();
}

void MusicEngine::applyEngagedState(int fadeMs)
{
    if (!m_enabled || !available()) {
        stopPlayback();
        return;
    }

    if (!m_routineEngaged) {
        startPlayback(fadeMs);
        return;
    }

    if (m_engageBehavior == QStringLiteral("same")) {
        startPlayback(0);
    } else if (m_engageBehavior == QStringLiteral("low")) {
        if (m_player.playbackState() != QMediaPlayer::PlayingState) {
            startPlayback(0);
        }
        fadeTo(lowVolume(), fadeMs, false);
    } else {
        fadeTo(0.0, fadeMs, true);
    }
}

qreal MusicEngine::configuredVolume() const
{
    return static_cast<qreal>(m_volume) / 100.0;
}

qreal MusicEngine::lowVolume() const
{
    return qMin(configuredVolume(), 0.12);
}
