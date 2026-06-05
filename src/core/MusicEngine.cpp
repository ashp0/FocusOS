#include "core/MusicEngine.h"

#include "core/AppPaths.h"

#include <QAudioBuffer>
#include <QAudioDevice>
#include <QAudioFormat>
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
#include <QtMath>
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
    return AppPaths::filePath(QStringLiteral("config.json"));
}

QString musicDirectory()
{
    return AppPaths::filePath(QStringLiteral("music"));
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

// Loudness target the equalizer matches every track toward. ~-20 dBFS RMS is a
// comfortable level for ambient backing tracks; the per-track gain is the ratio
// that nudges a track's measured RMS to this, clamped so nothing is boosted or
// cut to extremes (which would wreck the sound the user warned about).
constexpr qreal kTargetRms = 0.10;
constexpr qreal kMinGain = 0.35;
constexpr qreal kMaxGain = 2.5;
// Cap analysis at the first few minutes of each track — enough to estimate
// overall loudness, but bounded so a long ambient track doesn't decode forever.
constexpr quint64 kMaxAnalysisSeconds = 180;

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
    // Bind explicitly to the current default sink and follow device hot-plug.
    // This is the Linux "sound stopped working" fix: on a bare kwin_wayland
    // session PipeWire/PulseAudio often comes up *after* FocusOS, and a
    // QAudioOutput constructed before any sink exists stays bound to a null
    // device — silent forever, even once audio is available. Re-binding when a
    // device appears (see rebindDefaultAudioDevice) restores playback.
    m_audioOutput.setDevice(QMediaDevices::defaultAudioOutput());
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged,
            this, &MusicEngine::rebindDefaultAudioDevice);
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

    // Playback watchdog. Sleeping and re-waking the display lets PipeWire
    // idle-suspend the output sink; on a bare kwin session the player can come
    // back from that stalled (not Playing) and never resume on its own, so the
    // music stays dead after the screen wakes. audioOutputsChanged doesn't
    // reliably fire for a suspend/resume of the *same* device, so poll: if music
    // should be playing but isn't, re-bind the sink and re-kick it.
    m_playbackWatchdog.setInterval(4000);
    connect(&m_playbackWatchdog, &QTimer::timeout, this, &MusicEngine::recoverStalledPlayback);
    m_playbackWatchdog.start();

    // Loudness-equalization decoder. Runs off to the side: decodes each track to
    // raw PCM (independent of the output sink, so it works even before audio is
    // up) to measure RMS, then caches the gain. One track at a time.
    connect(&m_gainDecoder, &QAudioDecoder::bufferReady, this, [this] {
        accumulateBuffer(m_gainDecoder.read());
        const QAudioFormat format = m_gainDecoder.audioFormat();
        if (format.isValid() && format.sampleRate() > 0) {
            const quint64 cap = static_cast<quint64>(format.sampleRate())
                * static_cast<quint64>(qMax(1, format.channelCount())) * kMaxAnalysisSeconds;
            if (m_analysisSampleCount >= cap) {
                m_gainDecoder.stop();
                finalizeAnalysis();
            }
        }
    });
    connect(&m_gainDecoder, &QAudioDecoder::finished, this, [this] { finalizeAnalysis(); });
    connect(&m_gainDecoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this,
            [this](QAudioDecoder::Error error) {
        if (error == QAudioDecoder::NoError) {
            return;
        }
        qCDebug(lcMusic, "gain analysis failed for '%s' (%s) — leaving at unity",
                qPrintable(m_analysisPath), qPrintable(m_gainDecoder.errorString()));
        // Cache unity so we don't retry a file we can't decode every launch.
        finishAnalysis(1.0);
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
    scheduleGainAnalysis();
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
    loadGainCache(root);
    saveConfig();
}

bool MusicEngine::saveConfig() const
{
    QJsonObject root = readConfigObject();
    root.insert(QStringLiteral("music_enabled"), m_enabled);
    root.insert(QStringLiteral("music_volume"), m_volume);
    root.insert(QStringLiteral("music_engage_behavior"), m_engageBehavior);
    writeGainCache(root);

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
    const qint64 tenMinutesMs = 10LL * 60 * 1000;
    if (durationMs <= tenMinutesMs) {
        // Short enough to play from the top.
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
    // Each track carries its own equalization gain, so ease the output to the
    // new track's level instead of leaving it at the previous track's volume.
    applyCurrentVolume(800);
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

void MusicEngine::rebindDefaultAudioDevice()
{
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || m_audioOutput.device() == device) {
        return;
    }

    const qreal currentVolume = m_audioOutput.volume();
    m_audioOutput.setDevice(device);
    m_audioOutput.setVolume(currentVolume);
    qCDebug(lcMusic, "rebound audio output to '%s'", qPrintable(device.description()));

    // A sink that only just appeared means startup playback was bound to a null
    // device and produced silence — (re)start it now that there's somewhere to
    // play to.
    if (m_enabled && available() && m_player.playbackState() != QMediaPlayer::PlayingState) {
        applyEngagedState(800);
    }
}

void MusicEngine::setSleeping(bool sleeping)
{
    if (m_sleeping == sleeping) {
        return;
    }
    m_sleeping = sleeping;

    if (m_sleeping) {
        // Going to sleep: ease the sound out and pause. shouldBePlaying() now
        // returns false, so the stall watchdog won't fight this and try to
        // re-kick playback while the machine is suspending.
        fadeTo(0.0, 600, true);
        return;
    }

    // Woke up: resume whatever the engaged state calls for (home idle volume, or
    // the routine's low/same level), unless music is disabled or has no tracks.
    if (m_enabled && available()) {
        applyEngagedState(2500);
    }
}

bool MusicEngine::shouldBePlaying() const
{
    if (!m_enabled || !available() || m_sleeping) {
        return false;
    }
    // "stop" is the only engaged behavior that intentionally pauses playback.
    if (m_routineEngaged && m_engageBehavior == QStringLiteral("stop")) {
        return false;
    }
    return true;
}

void MusicEngine::recoverStalledPlayback()
{
    if (!shouldBePlaying()) {
        return;
    }
    // Don't fight a fade in progress (e.g. a routine engage/disengage transition).
    if (m_fadeAnimation.state() == QAbstractAnimation::Running) {
        return;
    }
    if (m_player.playbackState() == QMediaPlayer::PlayingState) {
        return;
    }

    // Music should be sounding but isn't. Force the sink to re-acquire — after a
    // suspend/resume the QAudioOutput can be left bound to a stale stream — then
    // re-kick playback.
    m_audioOutput.setDevice(QMediaDevices::defaultAudioOutput());
    applyEngagedState(800);
}

qreal MusicEngine::configuredVolume() const
{
    // Fold the current track's loudness-equalization gain into every fade target
    // so quieter tracks come up and louder ones come down to a steady level.
    return qBound(0.0, (static_cast<qreal>(m_volume) / 100.0) * currentTrackGain(), 1.0);
}

qreal MusicEngine::lowVolume() const
{
    // Scale the "low" cap by the same gain so the engaged-low level is equally
    // steady across tracks.
    return qMin(configuredVolume(), 0.12 * currentTrackGain());
}

QString MusicEngine::currentSourcePath() const
{
    if (m_currentSourceIndex < 0 || m_currentSourceIndex >= m_playbackQueue.size()) {
        return {};
    }
    return m_playbackQueue.at(m_currentSourceIndex);
}

qreal MusicEngine::currentTrackGain() const
{
    const auto it = m_gainCache.constFind(currentSourcePath());
    return it != m_gainCache.constEnd() ? it->gain : 1.0;
}

void MusicEngine::applyCurrentVolume(int fadeMs)
{
    // shouldBePlaying() (not the live playback state) is the right gate here: we
    // call this right after m_player.play(), whose state transition is async, so
    // checking PlayingState would skip the fade on a fresh source change.
    if (!shouldBePlaying()) {
        return;
    }
    // Don't stomp a transition fade that's still running (engage/disengage).
    if (m_fadeAnimation.state() == QAbstractAnimation::Running) {
        return;
    }
    const qreal target = (m_routineEngaged && m_engageBehavior == QStringLiteral("low"))
        ? lowVolume()
        : configuredVolume();
    fadeTo(target, fadeMs, false);
}

void MusicEngine::loadGainCache(const QJsonObject &root)
{
    m_gainCache.clear();
    const QJsonObject gains = root.value(QStringLiteral("music_track_gains")).toObject();
    for (auto it = gains.constBegin(); it != gains.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        GainEntry gain;
        gain.gain = qBound(kMinGain, entry.value(QStringLiteral("gain")).toDouble(1.0), kMaxGain);
        gain.mtime = entry.value(QStringLiteral("mtime")).toVariant().toLongLong();
        gain.size = entry.value(QStringLiteral("size")).toVariant().toLongLong();
        m_gainCache.insert(it.key(), gain);
    }
}

void MusicEngine::writeGainCache(QJsonObject &root) const
{
    QJsonObject gains;
    for (auto it = m_gainCache.constBegin(); it != m_gainCache.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("gain"), it->gain);
        entry.insert(QStringLiteral("mtime"), it->mtime);
        entry.insert(QStringLiteral("size"), it->size);
        gains.insert(it.key(), entry);
    }
    root.insert(QStringLiteral("music_track_gains"), gains);
}

void MusicEngine::scheduleGainAnalysis()
{
    // Queue any real track whose gain we haven't measured (or whose file changed
    // since we did). The fallback qrc track is skipped — it's bundled and tiny.
    for (const QString &path : std::as_const(m_musicFilePaths)) {
        const QFileInfo info(path);
        const auto it = m_gainCache.constFind(path);
        if (it != m_gainCache.constEnd()
                && it->mtime == info.lastModified().toMSecsSinceEpoch()
                && it->size == info.size()) {
            continue;
        }
        if (!m_analysisQueue.contains(path) && path != m_analysisPath) {
            m_analysisQueue.append(path);
        }
    }

    // Drop queued entries for tracks that no longer exist.
    m_analysisQueue.erase(
        std::remove_if(m_analysisQueue.begin(), m_analysisQueue.end(),
                       [this](const QString &path) { return !m_musicFilePaths.contains(path); }),
        m_analysisQueue.end());

    analyzeNextTrack();
}

void MusicEngine::analyzeNextTrack()
{
    if (m_analyzing || m_analysisQueue.isEmpty()) {
        return;
    }
    m_analysisPath = m_analysisQueue.takeFirst();
    m_analysisSumSquares = 0.0;
    m_analysisSampleCount = 0;
    m_analyzing = true;
    m_gainDecoder.setSource(QUrl::fromLocalFile(m_analysisPath));
    m_gainDecoder.start();
}

void MusicEngine::accumulateBuffer(const QAudioBuffer &buffer)
{
    if (!buffer.isValid()) {
        return;
    }

    const QAudioFormat format = buffer.format();
    const int count = buffer.sampleCount();
    if (count <= 0) {
        return;
    }

    // Sum the squares of every sample, normalized to [-1, 1], across all
    // channels. RMS over the whole track is our loudness proxy.
    switch (format.sampleFormat()) {
    case QAudioFormat::Int16: {
        const qint16 *data = buffer.constData<qint16>();
        for (int i = 0; i < count; ++i) {
            const qreal s = static_cast<qreal>(data[i]) / 32768.0;
            m_analysisSumSquares += s * s;
        }
        break;
    }
    case QAudioFormat::Int32: {
        const qint32 *data = buffer.constData<qint32>();
        for (int i = 0; i < count; ++i) {
            const qreal s = static_cast<qreal>(data[i]) / 2147483648.0;
            m_analysisSumSquares += s * s;
        }
        break;
    }
    case QAudioFormat::UInt8: {
        const quint8 *data = buffer.constData<quint8>();
        for (int i = 0; i < count; ++i) {
            const qreal s = (static_cast<qreal>(data[i]) - 128.0) / 128.0;
            m_analysisSumSquares += s * s;
        }
        break;
    }
    case QAudioFormat::Float: {
        const float *data = buffer.constData<float>();
        for (int i = 0; i < count; ++i) {
            const qreal s = static_cast<qreal>(data[i]);
            m_analysisSumSquares += s * s;
        }
        break;
    }
    default:
        return;
    }
    m_analysisSampleCount += static_cast<quint64>(count);
}

void MusicEngine::finalizeAnalysis()
{
    if (!m_analyzing) {
        return;
    }
    qreal gain = 1.0;
    if (m_analysisSampleCount > 0) {
        const qreal rms = std::sqrt(m_analysisSumSquares / static_cast<qreal>(m_analysisSampleCount));
        if (rms > 1e-6) {
            gain = qBound(kMinGain, kTargetRms / rms, kMaxGain);
        }
    }
    finishAnalysis(gain);
}

void MusicEngine::finishAnalysis(qreal gain)
{
    if (!m_analyzing) {
        return;
    }
    const QString path = m_analysisPath;
    storeTrackGain(path, gain);

    m_analysisPath.clear();
    m_analysisSumSquares = 0.0;
    m_analysisSampleCount = 0;
    m_analyzing = false;

    // If we just measured the track that's currently playing, ease the live
    // output to its equalized level now.
    if (!path.isEmpty() && path == currentSourcePath()) {
        applyCurrentVolume(800);
    }

    // Start the next track from a clean stack — never re-enter the decoder from
    // inside one of its own signal handlers.
    QTimer::singleShot(0, this, &MusicEngine::analyzeNextTrack);
}

void MusicEngine::storeTrackGain(const QString &path, qreal gain)
{
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
    GainEntry entry;
    entry.gain = qBound(kMinGain, gain, kMaxGain);
    entry.mtime = info.lastModified().toMSecsSinceEpoch();
    entry.size = info.size();
    m_gainCache.insert(path, entry);
    saveConfig();
    qCDebug(lcMusic, "equalized '%s' → gain %.3f", qPrintable(info.fileName()), entry.gain);
}
