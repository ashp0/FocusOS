#pragma once

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QPropertyAnimation>
#include <QStringList>

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
    void setImportStatus(const QString &status);

    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    QPropertyAnimation m_fadeAnimation;
    QStringList m_musicFilePaths;
    QStringList m_musicFileNames;
    QStringList m_playbackQueue;
    int m_currentSourceIndex = -1;
    bool m_enabled = true;
    bool m_routineEngaged = false;
    bool m_stopAfterFade = false;
    int m_volume = 35;
    bool m_seekedThisSource = false;
    QString m_engageBehavior = QStringLiteral("stop");
    QString m_importStatus;
};
