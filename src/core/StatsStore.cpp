#include "core/StatsStore.h"

#include "core/RoutineManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

// Live-session progress is reported every second; persist it to disk at most
// this often. Final-session / target / reflection writes are not throttled.
constexpr qint64 kProgressSaveIntervalMs = 45 * 1000;


QString statsPath()
{
    return RoutineManager::dataDirectory() + QStringLiteral("/stats.json");
}

QString normalizedResult(const QString &result)
{
    const QString normalized = result.trimmed().toLower();
    if (normalized == QStringLiteral("completed") ||
        normalized == QStringLiteral("interrupted")) {
        return normalized;
    }
    return QStringLiteral("unlocked");
}

QDate localDateForSession(const RoutineSession &session)
{
    const QDateTime basis = session.startedAt.isValid() ? session.startedAt : session.endedAt;
    return basis.toLocalTime().date();
}

} // namespace

StatsStore::StatsStore(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(RoutineManager::dataDirectory());
    load();
    importInterruptedActiveSession();
    save();
}

QVariantList StatsStore::dailyStats() const
{
    QVariantList days;
    const QDate today = QDate::currentDate();
    for (int offset = 13; offset >= 0; --offset) {
        days.append(aggregateForDate(today.addDays(-offset)));
    }
    return days;
}

QVariantList StatsStore::focusHistory() const
{
    QVariantList days;
    const QDate today = QDate::currentDate();
    for (int offset = 89; offset >= 0; --offset) {
        days.append(aggregateForDate(today.addDays(-offset)));
    }
    return days;
}

int StatsStore::todayFocusMinutes() const
{
    return aggregateForDate(QDate::currentDate()).value(QStringLiteral("focusMinutes")).toInt();
}

int StatsStore::totalFocusMinutes() const
{
    int total = 0;
    for (const RoutineSession &session : m_sessions) {
        total += displayMinutesForSeconds(sessionSeconds(session));
    }
    if (m_hasActiveSession) {
        total += displayMinutesForSeconds(sessionSeconds(m_activeSession));
    }
    return total;
}

int StatsStore::completedSessions() const
{
    int count = 0;
    for (const RoutineSession &session : m_sessions) {
        if (session.result == QStringLiteral("completed")) {
            ++count;
        }
    }
    return count;
}

int StatsStore::unlockedSessions() const
{
    int count = 0;
    for (const RoutineSession &session : m_sessions) {
        if (session.result == QStringLiteral("unlocked")) {
            ++count;
        }
    }
    return count;
}

int StatsStore::interruptedSessions() const
{
    int count = 0;
    for (const RoutineSession &session : m_sessions) {
        if (session.result == QStringLiteral("interrupted")) {
            ++count;
        }
    }
    return count;
}

int StatsStore::currentStreakDays() const
{
    int streak = 0;
    QDate cursor = QDate::currentDate();
    while (aggregateForDate(cursor).value(QStringLiteral("focusMinutes")).toInt() > 0) {
        ++streak;
        cursor = cursor.addDays(-1);
    }
    return streak;
}

QString StatsStore::lastSessionSummary() const
{
    if (m_hasActiveSession && sessionSeconds(m_activeSession) > 0) {
        return QStringLiteral("%1  ■  %2  ■  IN PROGRESS")
            .arg(m_activeSession.routineName.toUpper(),
                 formatMinutes(displayMinutesForSeconds(sessionSeconds(m_activeSession))));
    }

    if (m_sessions.isEmpty()) {
        return QStringLiteral("NO SESSIONS RECORDED");
    }

    const RoutineSession &session = m_sessions.last();
    QString result = QStringLiteral("UNLOCKED");
    if (session.result == QStringLiteral("completed")) {
        result = QStringLiteral("COMPLETED");
    } else if (session.result == QStringLiteral("interrupted")) {
        result = QStringLiteral("INTERRUPTED");
    }
    return QStringLiteral("%1  ■  %2  ■  %3")
        .arg(session.routineName.toUpper(),
             formatMinutes(displayMinutesForSeconds(sessionSeconds(session))),
             result);
}

QString StatsStore::lastSessionReflection() const
{
    if (m_sessions.isEmpty()) {
        return {};
    }

    return m_sessions.last().reflection;
}

int StatsStore::dailyTargetMinutes() const
{
    return m_dailyTargetMinutes;
}

void StatsStore::setDailyTargetMinutes(int minutes)
{
    const int clamped = qBound(0, minutes, 24 * 60);
    if (m_dailyTargetMinutes == clamped) {
        return;
    }
    m_dailyTargetMinutes = clamped;
    save();
    emit targetChanged();
    emit statsChanged();
}

double StatsStore::todayTargetProgress() const
{
    if (m_dailyTargetMinutes <= 0) {
        return 0.0;
    }
    return double(todayFocusMinutes()) / double(m_dailyTargetMinutes);
}

void StatsStore::recordLastSessionReflection(const QString &reflection)
{
    if (m_sessions.isEmpty()) {
        return;
    }

    const QString trimmed = reflection.trimmed();
    if (m_sessions.last().reflection == trimmed) {
        return;
    }

    m_sessions.last().reflection = trimmed;
    save();
    emit statsChanged();
}

void StatsStore::recordRoutineSession(const QString &routineId,
                                      const QString &routineName,
                                      int minutes,
                                      const QString &result,
                                      const QDateTime &startedAt,
                                      const QDateTime &endedAt)
{
    RoutineSession session;
    session.routineId = routineId;
    session.routineName = routineName.trimmed().isEmpty() ? QStringLiteral("ROUTINE") : routineName.trimmed();
    session.result = normalizedResult(result);
    session.startedAt = startedAt.isValid() ? startedAt.toUTC() : QDateTime::currentDateTimeUtc();
    session.endedAt = endedAt.isValid() ? endedAt.toUTC() : QDateTime::currentDateTimeUtc();
    session.seconds = qMax(0, minutes * 60);

    if (m_hasActiveSession &&
        m_activeSession.routineId == session.routineId &&
        m_activeSession.startedAt == session.startedAt) {
        session.routineName = m_activeSession.routineName;
        session.startedAt = m_activeSession.startedAt;
        session.seconds = qMax(session.seconds, sessionSeconds(m_activeSession));
        session.reflection = m_activeSession.reflection;
        m_hasActiveSession = false;
        m_activeSession = {};
    }

    if (session.seconds <= 0) {
        return;
    }

    session.minutes = displayMinutesForSeconds(session.seconds);
    m_sessions.append(session);
    save();
    emit statsChanged();
}

void StatsStore::recordRoutineSessionProgress(const QString &routineId,
                                              const QString &routineName,
                                              int elapsedSeconds,
                                              const QDateTime &startedAt,
                                              const QDateTime &updatedAt)
{
    if (routineId.trimmed().isEmpty() || elapsedSeconds <= 0) {
        return;
    }

    RoutineSession session;
    session.routineId = routineId;
    session.routineName = routineName.trimmed().isEmpty() ? QStringLiteral("ROUTINE") : routineName.trimmed();
    session.result = QStringLiteral("active");
    session.startedAt = startedAt.isValid() ? startedAt.toUTC() : QDateTime::currentDateTimeUtc();
    session.endedAt = updatedAt.isValid() ? updatedAt.toUTC() : QDateTime::currentDateTimeUtc();
    session.seconds = qMax(0, elapsedSeconds);
    session.minutes = displayMinutesForSeconds(session.seconds);

    if (m_hasActiveSession &&
        m_activeSession.routineId == session.routineId &&
        m_activeSession.startedAt == session.startedAt &&
        m_activeSession.seconds == session.seconds) {
        return;
    }

    const int previousMinutes = m_hasActiveSession ? m_activeSession.minutes : -1;
    m_activeSession = session;
    m_hasActiveSession = true;

    // Throttle the disk write — the in-memory active session is always current,
    // but we only flush it occasionally. The UI is refreshed when the displayed
    // minute changes (or on a flush) so the graphs stay live without rebuilding
    // every second.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool persisted = false;
    if (now - m_lastSaveMs >= kProgressSaveIntervalMs) {
        save();
        persisted = true;
    }
    if (persisted || session.minutes != previousMinutes) {
        emit statsChanged();
    }
}

void StatsStore::load()
{
    QFile file(statsPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject rootObject = document.object();
    m_dailyTargetMinutes = qBound(0, rootObject.value(QStringLiteral("daily_target_minutes")).toInt(180), 24 * 60);
    const QJsonArray sessions = rootObject.value(QStringLiteral("sessions")).toArray();

    m_sessions.clear();
    for (const QJsonValue &value : sessions) {
        const QJsonObject object = value.toObject();
        RoutineSession session;
        session.routineId = object.value(QStringLiteral("routine_id")).toString();
        session.routineName = object.value(QStringLiteral("routine_name")).toString(QStringLiteral("ROUTINE"));
        session.result = normalizedResult(object.value(QStringLiteral("result")).toString());
        session.startedAt = QDateTime::fromString(object.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
        session.endedAt = QDateTime::fromString(object.value(QStringLiteral("ended_at")).toString(), Qt::ISODate);
        session.reflection = object.value(QStringLiteral("reflection")).toString().trimmed();
        session.minutes = qMax(0, object.value(QStringLiteral("minutes")).toInt());
        session.seconds = qMax(0, object.value(QStringLiteral("seconds")).toInt(session.minutes * 60));
        if (!session.routineId.isEmpty() && sessionSeconds(session) > 0) {
            session.minutes = displayMinutesForSeconds(sessionSeconds(session));
            m_sessions.append(session);
        }
    }

    const QJsonObject activeObject = document.object().value(QStringLiteral("active_session")).toObject();
    RoutineSession activeSession;
    activeSession.routineId = activeObject.value(QStringLiteral("routine_id")).toString();
    activeSession.routineName = activeObject.value(QStringLiteral("routine_name")).toString(QStringLiteral("ROUTINE"));
    activeSession.result = QStringLiteral("active");
    activeSession.startedAt = QDateTime::fromString(activeObject.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
    activeSession.endedAt = QDateTime::fromString(activeObject.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
    activeSession.reflection = activeObject.value(QStringLiteral("reflection")).toString().trimmed();
    activeSession.seconds = qMax(0, activeObject.value(QStringLiteral("seconds")).toInt());
    activeSession.minutes = displayMinutesForSeconds(activeSession.seconds);
    if (!activeSession.routineId.isEmpty() && activeSession.seconds > 0) {
        m_activeSession = activeSession;
        m_hasActiveSession = true;
    }
}

bool StatsStore::save() const
{
    QJsonArray sessions;
    for (const RoutineSession &session : m_sessions) {
        QJsonObject object;
        object.insert(QStringLiteral("routine_id"), session.routineId);
        object.insert(QStringLiteral("routine_name"), session.routineName);
        object.insert(QStringLiteral("result"), session.result);
        object.insert(QStringLiteral("started_at"), session.startedAt.toUTC().toString(Qt::ISODate));
        object.insert(QStringLiteral("ended_at"), session.endedAt.toUTC().toString(Qt::ISODate));
        if (!session.reflection.isEmpty()) {
            object.insert(QStringLiteral("reflection"), session.reflection);
        }
        object.insert(QStringLiteral("minutes"), displayMinutesForSeconds(sessionSeconds(session)));
        object.insert(QStringLiteral("seconds"), sessionSeconds(session));
        sessions.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("sessions"), sessions);
    root.insert(QStringLiteral("daily_target_minutes"), m_dailyTargetMinutes);
    if (m_hasActiveSession) {
        QJsonObject active;
        active.insert(QStringLiteral("routine_id"), m_activeSession.routineId);
        active.insert(QStringLiteral("routine_name"), m_activeSession.routineName);
        active.insert(QStringLiteral("started_at"), m_activeSession.startedAt.toUTC().toString(Qt::ISODate));
        active.insert(QStringLiteral("updated_at"), m_activeSession.endedAt.toUTC().toString(Qt::ISODate));
        if (!m_activeSession.reflection.isEmpty()) {
            active.insert(QStringLiteral("reflection"), m_activeSession.reflection);
        }
        active.insert(QStringLiteral("minutes"), displayMinutesForSeconds(sessionSeconds(m_activeSession)));
        active.insert(QStringLiteral("seconds"), sessionSeconds(m_activeSession));
        root.insert(QStringLiteral("active_session"), active);
    }

    QSaveFile file(statsPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    const bool committed = file.commit();
    if (committed) {
        m_lastSaveMs = QDateTime::currentMSecsSinceEpoch();
    }
    return committed;
}

QVariantMap StatsStore::aggregateForDate(const QDate &date) const
{
    int focusMinutes = 0;
    int completedMinutes = 0;
    int unlockedMinutes = 0;
    int sessions = 0;

    const auto accumulate = [&](const RoutineSession &session) {
        if (localDateForSession(session) != date) {
            return;
        }

        const int minutes = displayMinutesForSeconds(sessionSeconds(session));
        focusMinutes += minutes;
        ++sessions;
        if (session.result == QStringLiteral("completed")) {
            completedMinutes += minutes;
        } else {
            unlockedMinutes += minutes;
        }
    };

    for (const RoutineSession &session : m_sessions) {
        accumulate(session);
    }
    if (m_hasActiveSession) {
        accumulate(m_activeSession);
    }

    QVariantMap day;
    day.insert(QStringLiteral("date"), date.toString(Qt::ISODate));
    day.insert(QStringLiteral("label"), date.toString(QStringLiteral("M/d")));
    day.insert(QStringLiteral("focusMinutes"), focusMinutes);
    day.insert(QStringLiteral("completedMinutes"), completedMinutes);
    day.insert(QStringLiteral("unlockedMinutes"), unlockedMinutes);
    day.insert(QStringLiteral("sessions"), sessions);
    return day;
}

QString StatsStore::formatMinutes(int minutes) const
{
    if (minutes >= 60) {
        return QStringLiteral("%1H %2M").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1M").arg(minutes);
}

int StatsStore::displayMinutesForSeconds(int seconds) const
{
    if (seconds <= 0) {
        return 0;
    }
    return (seconds + 59) / 60;
}

int StatsStore::sessionSeconds(const RoutineSession &session) const
{
    return session.seconds > 0 ? session.seconds : qMax(0, session.minutes * 60);
}

void StatsStore::importInterruptedActiveSession()
{
    if (!m_hasActiveSession || sessionSeconds(m_activeSession) <= 0) {
        m_hasActiveSession = false;
        m_activeSession = {};
        return;
    }

    RoutineSession interrupted = m_activeSession;
    interrupted.result = QStringLiteral("interrupted");
    interrupted.minutes = displayMinutesForSeconds(sessionSeconds(interrupted));
    m_sessions.append(interrupted);
    m_hasActiveSession = false;
    m_activeSession = {};
}
