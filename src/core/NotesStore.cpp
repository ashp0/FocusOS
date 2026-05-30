#include "core/NotesStore.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariantMap>
#include <algorithm>

namespace {

QString sessionIdFromTimestamp(const QDateTime &when)
{
    return when.toLocalTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

QString safeSlug(const QString &text)
{
    QString slug;
    slug.reserve(text.size());
    for (QChar ch : text) {
        if (ch.isLetterOrNumber()) {
            slug.append(ch.toLower());
        } else if (slug.isEmpty() || slug.endsWith(QLatin1Char('-'))) {
            continue;
        } else {
            slug.append(QLatin1Char('-'));
        }
    }
    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }
    return slug.isEmpty() ? QStringLiteral("routine") : slug;
}

} // namespace

NotesStore::NotesStore(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(dataDirectory());
    QDir().mkpath(sessionsDirectory());
    m_saveTimer.setInterval(500);
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &NotesStore::saveDraft);
    loadDraft();
    loadArchive();
}

QString NotesStore::text() const
{
    return m_text;
}

void NotesStore::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    emit textChanged();
    m_saveTimer.start();
}

QString NotesStore::draftRoutineName() const
{
    return m_draftRoutineName;
}

QString NotesStore::todayCombinedNotes() const
{
    const QDate today = QDate::currentDate();
    QStringList chunks;
    for (const SessionNote &note : m_archive) {
        if (note.endedAt.toLocalTime().date() != today) {
            continue;
        }
        chunks.append(formatSession(note));
    }
    if (!m_text.trimmed().isEmpty()) {
        SessionNote pending;
        pending.routineName = m_draftRoutineName.isEmpty() ? QStringLiteral("CURRENT DRAFT") : m_draftRoutineName;
        pending.startedAt = m_draftStartedAt.isValid() ? m_draftStartedAt : QDateTime::currentDateTime();
        pending.endedAt = QDateTime::currentDateTime();
        pending.result = QStringLiteral("draft");
        pending.text = m_text;
        chunks.append(formatSession(pending));
    }
    return chunks.join(QStringLiteral("\n\n"));
}

int NotesStore::todayNoteCount() const
{
    const QDate today = QDate::currentDate();
    int count = 0;
    for (const SessionNote &note : m_archive) {
        if (note.endedAt.toLocalTime().date() == today) {
            ++count;
        }
    }
    if (!m_text.trimmed().isEmpty()) {
        ++count;
    }
    return count;
}

QVariantList NotesStore::sessionHistory() const
{
    QVariantList list;
    list.reserve(m_archive.size());
    for (int i = m_archive.size() - 1; i >= 0; --i) {
        const SessionNote &note = m_archive.at(i);
        QVariantMap entry;
        entry.insert(QStringLiteral("sessionId"), note.sessionId);
        entry.insert(QStringLiteral("routineId"), note.routineId);
        entry.insert(QStringLiteral("routineName"), note.routineName);
        entry.insert(QStringLiteral("startedAt"), note.startedAt.toLocalTime().toString(Qt::ISODate));
        entry.insert(QStringLiteral("endedAt"), note.endedAt.toLocalTime().toString(Qt::ISODate));
        entry.insert(QStringLiteral("dateLabel"), note.endedAt.toLocalTime().date().toString(QStringLiteral("yyyy-MM-dd")));
        entry.insert(QStringLiteral("timeLabel"), note.endedAt.toLocalTime().toString(QStringLiteral("HH:mm")));
        entry.insert(QStringLiteral("minutes"), note.minutes);
        entry.insert(QStringLiteral("result"), note.result);
        entry.insert(QStringLiteral("hasNote"), !note.text.trimmed().isEmpty());
        const QString trimmed = note.text.trimmed();
        entry.insert(QStringLiteral("preview"), trimmed.left(140));
        list.append(entry);
    }
    return list;
}

QVariantList NotesStore::availableDates() const
{
    QMap<QDate, int> counts;
    for (const SessionNote &note : m_archive) {
        const QDate date = note.endedAt.toLocalTime().date();
        if (date.isValid()) {
            counts[date] += 1;
        }
    }
    if (!m_text.trimmed().isEmpty()) {
        counts[QDate::currentDate()] += 1;
    }

    QVariantList list;
    auto it = counts.constEnd();
    while (it != counts.constBegin()) {
        --it;
        QVariantMap entry;
        entry.insert(QStringLiteral("date"), it.key().toString(Qt::ISODate));
        entry.insert(QStringLiteral("label"), it.key().toString(QStringLiteral("MMM d")));
        entry.insert(QStringLiteral("count"), it.value());
        list.append(entry);
    }
    return list;
}

QString NotesStore::sessionNoteText(const QString &sessionId) const
{
    for (const SessionNote &note : m_archive) {
        if (note.sessionId == sessionId) {
            return note.text;
        }
    }
    return {};
}

QString NotesStore::combinedNotesForDate(const QString &date) const
{
    const QDate target = parseDateOrToday(date);
    QStringList chunks;
    for (const SessionNote &note : m_archive) {
        if (note.endedAt.toLocalTime().date() != target) {
            continue;
        }
        chunks.append(formatSession(note));
    }
    if (target == QDate::currentDate() && !m_text.trimmed().isEmpty()) {
        SessionNote pending;
        pending.routineName = m_draftRoutineName.isEmpty() ? QStringLiteral("CURRENT DRAFT") : m_draftRoutineName;
        pending.startedAt = m_draftStartedAt.isValid() ? m_draftStartedAt : QDateTime::currentDateTime();
        pending.endedAt = QDateTime::currentDateTime();
        pending.result = QStringLiteral("draft");
        pending.text = m_text;
        chunks.append(formatSession(pending));
    }
    return chunks.join(QStringLiteral("\n\n"));
}

QVariantMap NotesStore::timelineSummaryForDate(const QString &date) const
{
    const QDate target = parseDateOrToday(date);
    int sessions = 0;
    int noteCount = 0;
    int focusMinutes = 0;
    for (const SessionNote &note : m_archive) {
        if (note.endedAt.toLocalTime().date() != target) {
            continue;
        }
        ++sessions;
        focusMinutes += qMax(0, note.minutes);
        if (!note.text.trimmed().isEmpty()) {
            ++noteCount;
        }
    }
    if (target == QDate::currentDate() && !m_text.trimmed().isEmpty()) {
        ++noteCount;
    }

    QVariantMap summary;
    summary.insert(QStringLiteral("date"), target.toString(Qt::ISODate));
    summary.insert(QStringLiteral("dateLabel"), target.toString(QStringLiteral("dddd, MMMM d")));
    summary.insert(QStringLiteral("sessions"), sessions);
    summary.insert(QStringLiteral("notes"), noteCount);
    summary.insert(QStringLiteral("focusMinutes"), focusMinutes);
    return summary;
}

QVariantList NotesStore::timelineForDate(const QString &date) const
{
    const QDate target = parseDateOrToday(date);
    QList<QVariantMap> rows;
    for (const SessionNote &note : m_archive) {
        if (note.endedAt.toLocalTime().date() != target) {
            continue;
        }
        const QDateTime started = note.startedAt.toLocalTime();
        const QDateTime ended = note.endedAt.toLocalTime();
        QVariantMap entry;
        entry.insert(QStringLiteral("type"), QStringLiteral("routine"));
        entry.insert(QStringLiteral("sessionId"), note.sessionId);
        entry.insert(QStringLiteral("title"), note.routineName);
        entry.insert(QStringLiteral("timeLabel"), QStringLiteral("%1-%2")
                                                  .arg(started.toString(QStringLiteral("HH:mm")),
                                                       ended.toString(QStringLiteral("HH:mm"))));
        entry.insert(QStringLiteral("detail"), QStringLiteral("%1M  ■  %2")
                                               .arg(QString::number(qMax(0, note.minutes)),
                                                    note.result.toUpper()));
        entry.insert(QStringLiteral("minutes"), qMax(0, note.minutes));
        entry.insert(QStringLiteral("result"), note.result);
        entry.insert(QStringLiteral("noteText"), note.text.trimmed());
        entry.insert(QStringLiteral("hasNote"), !note.text.trimmed().isEmpty());
        entry.insert(QStringLiteral("sortKey"), started.isValid() ? started.toMSecsSinceEpoch() : ended.toMSecsSinceEpoch());
        rows.append(entry);
    }

    if (target == QDate::currentDate() && !m_text.trimmed().isEmpty()) {
        const QDateTime started = m_draftStartedAt.isValid() ? m_draftStartedAt.toLocalTime() : QDateTime::currentDateTime();
        QVariantMap entry;
        entry.insert(QStringLiteral("type"), QStringLiteral("draft"));
        entry.insert(QStringLiteral("sessionId"), QStringLiteral("draft"));
        entry.insert(QStringLiteral("title"), m_draftRoutineName.isEmpty() ? QStringLiteral("CURRENT MISSION DRAFT") : m_draftRoutineName);
        entry.insert(QStringLiteral("timeLabel"), QStringLiteral("%1-NOW").arg(started.toString(QStringLiteral("HH:mm"))));
        entry.insert(QStringLiteral("detail"), QStringLiteral("LIVE NOTE  ■  UNSUBMITTED"));
        entry.insert(QStringLiteral("minutes"), 0);
        entry.insert(QStringLiteral("result"), QStringLiteral("draft"));
        entry.insert(QStringLiteral("noteText"), m_text.trimmed());
        entry.insert(QStringLiteral("hasNote"), true);
        entry.insert(QStringLiteral("sortKey"), started.toMSecsSinceEpoch());
        rows.append(entry);
    }

    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("sortKey")).toLongLong() < b.value(QStringLiteral("sortKey")).toLongLong();
    });

    QVariantList list;
    for (const QVariantMap &row : rows) {
        list.append(row);
    }
    return list;
}

QVariantMap NotesStore::sessionNote(const QString &sessionId) const
{
    for (const SessionNote &note : m_archive) {
        if (note.sessionId != sessionId) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("sessionId"), note.sessionId);
        entry.insert(QStringLiteral("routineId"), note.routineId);
        entry.insert(QStringLiteral("routineName"), note.routineName);
        entry.insert(QStringLiteral("startedAt"), note.startedAt.toLocalTime().toString(Qt::ISODate));
        entry.insert(QStringLiteral("endedAt"), note.endedAt.toLocalTime().toString(Qt::ISODate));
        entry.insert(QStringLiteral("minutes"), note.minutes);
        entry.insert(QStringLiteral("result"), note.result);
        entry.insert(QStringLiteral("text"), note.text);
        return entry;
    }
    return {};
}

bool NotesStore::updateSessionNote(const QString &sessionId, const QString &text)
{
    for (SessionNote &note : m_archive) {
        if (note.sessionId != sessionId) {
            continue;
        }
        if (note.text == text) {
            return true;
        }
        note.text = text;
        writeSessionFile(note);
        emit archiveChanged();
        return true;
    }
    return false;
}

bool NotesStore::recordSessionReflection(const QString &reflection)
{
    // The "MISSION COMPLETE" popup collects a reflection after a routine ends.
    // By that point onRoutineSessionFinished() has already archived the session
    // (with whatever in-session draft text existed), so fold the reflection into
    // that most recent note — this is what surfaces in the calendar/timeline.
    const QString trimmed = reflection.trimmed();
    if (trimmed.isEmpty() || m_archive.isEmpty()) {
        return false;
    }

    SessionNote &note = m_archive.last();
    const QString existing = note.text.trimmed();
    const QString combined = existing.isEmpty()
                                 ? trimmed
                                 : existing + QStringLiteral("\n\n") + trimmed;
    if (note.text == combined) {
        return false;
    }
    note.text = combined;
    writeSessionFile(note);
    emit archiveChanged();
    return true;
}

void NotesStore::onRoutineEngaged(const QString &routineId, const QString &routineName)
{
    m_draftRoutineId = routineId;
    m_draftRoutineName = routineName;
    m_draftStartedAt = QDateTime::currentDateTime();
    if (!m_text.isEmpty()) {
        m_text.clear();
        emit textChanged();
    }
    saveDraft();
    emit draftChanged();
}

void NotesStore::onRoutineSessionFinished(const QString &routineId,
                                          const QString &routineName,
                                          int minutes,
                                          const QString &result,
                                          const QDateTime &startedAt,
                                          const QDateTime &endedAt)
{
    SessionNote note;
    note.routineId = routineId;
    note.routineName = routineName;
    note.minutes = qMax(0, minutes);
    note.result = result;
    note.startedAt = startedAt.isValid() ? startedAt.toLocalTime() : (m_draftStartedAt.isValid() ? m_draftStartedAt : QDateTime::currentDateTime());
    note.endedAt = endedAt.isValid() ? endedAt.toLocalTime() : QDateTime::currentDateTime();
    note.sessionId = sessionIdFromTimestamp(note.endedAt) + QStringLiteral("-") + safeSlug(routineName);
    note.text = m_text;

    archiveSession(note);

    m_text.clear();
    m_draftRoutineId.clear();
    m_draftRoutineName.clear();
    m_draftStartedAt = {};
    saveDraft();

    emit textChanged();
    emit draftChanged();
    emit archiveChanged();
}

void NotesStore::loadDraft()
{
    QFile file(draftPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return;
    }
    const QJsonObject object = document.object();
    m_text = object.value(QStringLiteral("text")).toString();
    m_draftRoutineId = object.value(QStringLiteral("routine_id")).toString();
    m_draftRoutineName = object.value(QStringLiteral("routine_name")).toString();
    m_draftStartedAt = QDateTime::fromString(object.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
    emit textChanged();
    emit draftChanged();
}

void NotesStore::saveDraft()
{
    QSaveFile file(draftPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("text"), m_text);
    root.insert(QStringLiteral("routine_id"), m_draftRoutineId);
    root.insert(QStringLiteral("routine_name"), m_draftRoutineName);
    if (m_draftStartedAt.isValid()) {
        root.insert(QStringLiteral("started_at"), m_draftStartedAt.toString(Qt::ISODate));
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

void NotesStore::loadArchive()
{
    m_archive.clear();
    QDir root(sessionsDirectory());
    const QFileInfoList dayDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &dayInfo : dayDirs) {
        QDir dayDir(dayInfo.absoluteFilePath());
        const QFileInfoList files = dayDir.entryInfoList(QStringList() << QStringLiteral("*.json"),
                                                         QDir::Files,
                                                         QDir::Name);
        for (const QFileInfo &fileInfo : files) {
            QFile file(fileInfo.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            if (!document.isObject()) {
                continue;
            }
            const QJsonObject object = document.object();
            SessionNote note;
            note.sessionId = object.value(QStringLiteral("session_id")).toString();
            note.routineId = object.value(QStringLiteral("routine_id")).toString();
            note.routineName = object.value(QStringLiteral("routine_name")).toString();
            note.startedAt = QDateTime::fromString(object.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
            note.endedAt = QDateTime::fromString(object.value(QStringLiteral("ended_at")).toString(), Qt::ISODate);
            note.minutes = object.value(QStringLiteral("minutes")).toInt();
            note.result = object.value(QStringLiteral("result")).toString();
            note.text = object.value(QStringLiteral("text")).toString();
            if (note.sessionId.isEmpty()) {
                note.sessionId = fileInfo.completeBaseName();
            }
            m_archive.append(note);
        }
    }

    std::sort(m_archive.begin(), m_archive.end(), [](const SessionNote &a, const SessionNote &b) {
        return a.endedAt < b.endedAt;
    });
    emit archiveChanged();
}

void NotesStore::archiveSession(const SessionNote &note)
{
    writeSessionFile(note);
    m_archive.append(note);
}

void NotesStore::writeSessionFile(const SessionNote &note) const
{
    const QDate date = note.endedAt.toLocalTime().date();
    const QString path = sessionFilePath(date, note.sessionId);
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QJsonObject root;
    root.insert(QStringLiteral("session_id"), note.sessionId);
    root.insert(QStringLiteral("routine_id"), note.routineId);
    root.insert(QStringLiteral("routine_name"), note.routineName);
    root.insert(QStringLiteral("started_at"), note.startedAt.toString(Qt::ISODate));
    root.insert(QStringLiteral("ended_at"), note.endedAt.toString(Qt::ISODate));
    root.insert(QStringLiteral("minutes"), note.minutes);
    root.insert(QStringLiteral("result"), note.result);
    root.insert(QStringLiteral("text"), note.text);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();

    // Also dump a human-readable markdown sibling so the sessions folder is browsable.
    const QString markdownPath = path.left(path.size() - 5) + QStringLiteral(".md");
    QSaveFile mdFile(markdownPath);
    if (mdFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream stream(&mdFile);
        stream << "# " << note.routineName << "\n\n";
        stream << "- session: " << note.sessionId << "\n";
        stream << "- started: " << note.startedAt.toString(Qt::ISODate) << "\n";
        stream << "- ended:   " << note.endedAt.toString(Qt::ISODate) << "\n";
        stream << "- minutes: " << note.minutes << "\n";
        stream << "- result:  " << note.result << "\n\n";
        stream << note.text << "\n";
        mdFile.commit();
    }
}

QString NotesStore::dataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.focusos");
}

QString NotesStore::sessionsDirectory()
{
    return dataDirectory() + QStringLiteral("/sessions");
}

QString NotesStore::draftPath()
{
    return dataDirectory() + QStringLiteral("/notes.json");
}

QString NotesStore::sessionFilePath(const QDate &date, const QString &sessionId)
{
    return sessionsDirectory() + QStringLiteral("/") +
           date.toString(QStringLiteral("yyyy-MM-dd")) +
           QStringLiteral("/") + sessionId + QStringLiteral(".json");
}

QString NotesStore::formatSession(const SessionNote &note)
{
    const QString header = QStringLiteral("── %1  ■  %2  ■  %3M  ■  %4 ──")
                               .arg(note.routineName.toUpper(),
                                    note.endedAt.toLocalTime().toString(QStringLiteral("HH:mm")),
                                    QString::number(note.minutes),
                                    note.result.toUpper());
    const QString body = note.text.trimmed().isEmpty()
                             ? QStringLiteral("(no notes captured)")
                             : note.text.trimmed();
    return header + QStringLiteral("\n") + body;
}

QDate NotesStore::parseDateOrToday(const QString &date)
{
    const QDate parsed = QDate::fromString(date.trimmed(), Qt::ISODate);
    return parsed.isValid() ? parsed : QDate::currentDate();
}
