#pragma once

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QVariantList>

struct RoutineSession
{
    QString routineId;
    QString routineName;
    QString result;
    QDateTime startedAt;
    QDateTime endedAt;
    QString reflection;
    int minutes = 0;
    int seconds = 0;
    // Self-assessed focus quality for the session, 1–5. 0 means unrated.
    int focusRating = 0;
};

class StatsStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList dailyStats READ dailyStats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList focusHistory READ focusHistory NOTIFY statsChanged)
    Q_PROPERTY(int todayFocusMinutes READ todayFocusMinutes NOTIFY statsChanged)
    Q_PROPERTY(int totalFocusMinutes READ totalFocusMinutes NOTIFY statsChanged)
    Q_PROPERTY(int completedSessions READ completedSessions NOTIFY statsChanged)
    Q_PROPERTY(int unlockedSessions READ unlockedSessions NOTIFY statsChanged)
    Q_PROPERTY(int interruptedSessions READ interruptedSessions NOTIFY statsChanged)
    Q_PROPERTY(int currentStreakDays READ currentStreakDays NOTIFY statsChanged)
    Q_PROPERTY(int longestStreakDays READ longestStreakDays NOTIFY statsChanged)
    Q_PROPERTY(int weekFocusMinutes READ weekFocusMinutes NOTIFY statsChanged)
    Q_PROPERTY(int bestDayMinutes READ bestDayMinutes NOTIFY statsChanged)
    Q_PROPERTY(QString bestDayLabel READ bestDayLabel NOTIFY statsChanged)
    Q_PROPERTY(int totalSessions READ totalSessions NOTIFY statsChanged)
    Q_PROPERTY(int averageSessionMinutes READ averageSessionMinutes NOTIFY statsChanged)
    Q_PROPERTY(QString lastSessionSummary READ lastSessionSummary NOTIFY statsChanged)
    Q_PROPERTY(QString lastSessionReflection READ lastSessionReflection NOTIFY statsChanged)
    Q_PROPERTY(int dailyTargetMinutes READ dailyTargetMinutes WRITE setDailyTargetMinutes NOTIFY targetChanged)
    Q_PROPERTY(double todayTargetProgress READ todayTargetProgress NOTIFY statsChanged)
    Q_PROPERTY(double averageFocusRating READ averageFocusRating NOTIFY statsChanged)
    Q_PROPERTY(int ratedSessions READ ratedSessions NOTIFY statsChanged)
    // Lifetime count of launcher/time-sink reaches the lockdown watchdog blocked
    // across all routines. A running tally of resisted distractions.
    Q_PROPERTY(int totalDistractionsBlocked READ totalDistractionsBlocked NOTIFY statsChanged)

public:
    explicit StatsStore(QObject *parent = nullptr);

    QVariantList dailyStats() const;
    QVariantList focusHistory() const;
    int todayFocusMinutes() const;
    int totalFocusMinutes() const;
    int completedSessions() const;
    int unlockedSessions() const;
    int interruptedSessions() const;
    int currentStreakDays() const;
    int longestStreakDays() const;
    int weekFocusMinutes() const;
    int bestDayMinutes() const;
    QString bestDayLabel() const;
    int totalSessions() const;
    int averageSessionMinutes() const;
    QString lastSessionSummary() const;
    QString lastSessionReflection() const;
    int dailyTargetMinutes() const;
    void setDailyTargetMinutes(int minutes);
    double todayTargetProgress() const;
    double averageFocusRating() const;
    int ratedSessions() const;
    int totalDistractionsBlocked() const;
    Q_INVOKABLE void recordLastSessionReflection(const QString &reflection);
    // Records a 1–5 focus-quality rating against the most recently logged
    // session (the one the MISSION COMPLETE prompt is reflecting on). 0 clears it.
    Q_INVOKABLE void recordLastSessionFocusRating(int rating);

public slots:
    // Wired to RoutineManager::distractionAttemptBlocked — increments the
    // persisted lifetime tally.
    void noteDistractionBlocked();
    void recordRoutineSession(const QString &routineId,
                              const QString &routineName,
                              int minutes,
                              const QString &result,
                              const QDateTime &startedAt,
                              const QDateTime &endedAt);
    void recordRoutineSessionProgress(const QString &routineId,
                                      const QString &routineName,
                                      int elapsedSeconds,
                                      const QDateTime &startedAt,
                                      const QDateTime &updatedAt);

signals:
    void statsChanged();
    void targetChanged();

private:
    void load();
    bool save() const;
    QVariantMap aggregateForDate(const QDate &date) const;
    // Display-minutes summed per local calendar date across every recorded
    // session (plus the live one). The single source for the streak/best-day
    // analytics so they don't each re-scan history.
    QMap<QDate, int> minutesByDate() const;
    QString formatMinutes(int minutes) const;
    int displayMinutesForSeconds(int seconds) const;
    int sessionSeconds(const RoutineSession &session) const;
    void importInterruptedActiveSession();
    // "Today" figures are computed against QDate::currentDate(), so they silently
    // go stale if FocusOS is left running across midnight. A timer that fires at
    // the day boundary re-emits statsChanged() so the UI rolls over on its own.
    void scheduleMidnightRefresh();

    QVector<RoutineSession> m_sessions;
    RoutineSession m_activeSession;
    bool m_hasActiveSession = false;
    int m_dailyTargetMinutes = 180;
    int m_totalDistractionsBlocked = 0;
    QTimer m_midnightTimer;
    // Last time save() actually wrote to disk. Live progress ticks every second
    // but only persist occasionally so the SSD isn't hammered; final-state and
    // settings writes still go through immediately.
    mutable qint64 m_lastSaveMs = 0;
};
