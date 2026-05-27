#pragma once

#include <QDateTime>
#include <QObject>
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
    Q_PROPERTY(QString lastSessionSummary READ lastSessionSummary NOTIFY statsChanged)
    Q_PROPERTY(QString lastSessionReflection READ lastSessionReflection NOTIFY statsChanged)
    Q_PROPERTY(int dailyTargetMinutes READ dailyTargetMinutes WRITE setDailyTargetMinutes NOTIFY targetChanged)
    Q_PROPERTY(double todayTargetProgress READ todayTargetProgress NOTIFY statsChanged)

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
    QString lastSessionSummary() const;
    QString lastSessionReflection() const;
    int dailyTargetMinutes() const;
    void setDailyTargetMinutes(int minutes);
    double todayTargetProgress() const;
    Q_INVOKABLE void recordLastSessionReflection(const QString &reflection);

public slots:
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
    QString formatMinutes(int minutes) const;
    int displayMinutesForSeconds(int seconds) const;
    int sessionSeconds(const RoutineSession &session) const;
    void importInterruptedActiveSession();

    QVector<RoutineSession> m_sessions;
    RoutineSession m_activeSession;
    bool m_hasActiveSession = false;
    int m_dailyTargetMinutes = 180;
};
