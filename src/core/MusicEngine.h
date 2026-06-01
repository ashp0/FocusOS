#pragma once

#include <QAudioDecoder>
#include <QAudioOutput>
#include <QHash>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QObject>
#include <QPropertyAnimation>
#include <QStringList>
#include <QTimer>

class MusicEngine final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool available READ available NOTIFY musicFilesChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString engageBehavior READ engageBehavior WRITE setEngageBehavior NOTIFY engageBehaviorChanged)
    Q_PROPERTY(QStringList musicFiles READ musicFiles NOTIFY musicFilesChanged)
    Q_PROPERTY(QString importStatus READ importStatus NOTIFY importStatusChanged)

public:
    explicit MusicEngine(QObject *parent = nullptr);

    bool enabled() const;
    bool available() const;
    int volume() const;
    QString engageBehavior() const;
    QStringList musicFiles() const;
    QString importStatus() const;

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void setEngageBehavior(const QString &behavior);
    Q_INVOKABLE void refreshMusicFiles();
    Q_INVOKABLE void openMusicFolder() const;
    Q_INVOKABLE QString importMusicFile();
    Q_INVOKABLE QString musicFolderPath() const;
    Q_INVOKABLE void setRoutineEngaged(bool engaged);
    // Deep-idle sleep (wired to IdleMonitor::deepIdle in main.cpp): fade out and
    // pause while the machine goes to sleep, then resume on wake. Distinct from
    // setEnabled() — this doesn't touch the user's on/off preference.
    void setSleeping(bool sleeping);

signals:
    void enabledChanged();
    void volumeChanged();
    void engageBehaviorChanged();
    void musicFilesChanged();
    void importStatusChanged();

private:
    void loadConfig();
    bool saveConfig() const;
    void rebuildPlaybackQueue();
    void startPlayback(int fadeMs);
    void fadeTo(qreal targetVolume, int durationMs, bool stopAfterFade);
    void stopPlayback();
    void pausePlayback();
    void playCurrentSource();
    void advanceSource();
    void applyEngagedState(int fadeMs);
    void seekToInterestingOffset();
    qreal configuredVolume() const;
    qreal lowVolume() const;
    // Per-track loudness equalization. Each track gets a constant linear gain
    // that brings its overall RMS toward a shared target, so the playlist plays
    // at a steady perceived level without flattening each track's own quiet/loud
    // passages. Gains are measured once (QAudioDecoder → RMS) and cached on disk
    // keyed by path + size + mtime.
    qreal currentTrackGain() const;
    QString currentSourcePath() const;
    void loadGainCache(const QJsonObject &root);
    void writeGainCache(QJsonObject &root) const;
    void scheduleGainAnalysis();
    void analyzeNextTrack();
    void accumulateBuffer(const QAudioBuffer &buffer);
    // Compute the gain from the buffers accumulated so far and store it.
    void finalizeAnalysis();
    void finishAnalysis(qreal gain);
    void storeTrackGain(const QString &path, qreal gain);
    // Re-fade the live output to the current track's equalized target. Used when
    // the playing track changes, or when its gain finishes computing mid-track.
    void applyCurrentVolume(int fadeMs);
    void setImportStatus(const QString &status);
    // Bind m_audioOutput to the current default output device, restarting
    // playback if a device only appeared after startup (the Linux silence bug).
    void rebindDefaultAudioDevice();
    // True when music is enabled, has tracks, and the current engaged mode is
    // not the one that intentionally silences playback ("stop").
    bool shouldBePlaying() const;
    // Periodic self-heal: if music should be playing but the backend has
    // stalled (e.g. PipeWire idle-suspended the sink while the display slept),
    // re-bind the sink and re-kick playback.
    void recoverStalledPlayback();

    struct GainEntry {
        qreal gain = 1.0;
        qint64 mtime = 0;
        qint64 size = 0;
    };

    QMediaDevices m_mediaDevices;
    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    QPropertyAnimation m_fadeAnimation;
    QTimer m_playbackWatchdog;
    QStringList m_musicFilePaths;
    QStringList m_musicFileNames;
    QStringList m_playbackQueue;
    // Loudness-equalization state.
    QAudioDecoder m_gainDecoder;
    QHash<QString, GainEntry> m_gainCache;
    QStringList m_analysisQueue;
    QString m_analysisPath;
    double m_analysisSumSquares = 0.0;
    quint64 m_analysisSampleCount = 0;
    bool m_analyzing = false;
    int m_currentSourceIndex = -1;
    bool m_enabled = true;
    bool m_routineEngaged = false;
    bool m_sleeping = false;
    bool m_stopAfterFade = false;
    int m_volume = 35;
    bool m_seekedThisSource = false;
    QString m_engageBehavior = QStringLiteral("stop");
    QString m_importStatus;
};
