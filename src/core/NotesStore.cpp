#include "core/NotesStore.h"

#include "core/AppPaths.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
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

// Collapse runs of whitespace/newlines so a multi-line note reads as a tidy
// one-line snippet in the search results.
QString flattenWhitespace(const QString &text)
{
    return text.simplified();
}

// A short context window around the first term hit, with each matched term
// wrapped in sentinel markers (0x01..0x02) that the html builder turns into
// <b> tags. Marking before escaping guarantees valid markup.
QString markedSnippet(const QString &body, const QStringList &terms, int radius = 70)
{
    const QString flat = flattenWhitespace(body);
    if (flat.isEmpty()) {
        return {};
    }
    const QString lower = flat.toLower();

    int firstHit = -1;
    for (const QString &term : terms) {
        const int idx = lower.indexOf(term);
        if (idx >= 0 && (firstHit < 0 || idx < firstHit)) {
            firstHit = idx;
        }
    }
    // Matched only on routine name / result: show the opening of the note.
    const int anchor = firstHit < 0 ? 0 : firstHit;
    int start = qMax(0, anchor - radius);
    int end = qMin(flat.size(), anchor + radius * 2);
    QString window = flat.mid(start, end - start);

    // Re-mark matches within the window (indices shifted by `start`).
    QString marked;
    marked.reserve(window.size() + 16);
    const QString windowLower = window.toLower();
    for (int i = 0; i < window.size();) {
        int matchLen = 0;
        for (const QString &term : terms) {
            if (!term.isEmpty()
                && windowLower.mid(i, term.size()) == term) {
                matchLen = term.size();
                break;
            }
        }
        if (matchLen > 0) {
            marked += QChar(0x01);
            marked += window.mid(i, matchLen);
            marked += QChar(0x02);
            i += matchLen;
        } else {
            marked += window.at(i);
            ++i;
        }
    }

    if (start > 0) {
        marked.prepend(QStringLiteral("… "));
    }
    if (end < flat.size()) {
        marked.append(QStringLiteral(" …"));
    }
    return marked;
}

QString htmlFromMarked(const QString &marked)
{
    QString escaped = marked.toHtmlEscaped();
    escaped.replace(QChar(0x01), QStringLiteral("<b>"));
    escaped.replace(QChar(0x02), QStringLiteral("</b>"));
    return escaped;
}

QString plainFromMarked(QString marked)
{
    marked.remove(QChar(0x01));
    marked.remove(QChar(0x02));
    return marked;
}

// A human, *unique-per-day* label for grouping the mission log: "TODAY",
// "YESTERDAY", else the weekday + date ("MONDAY · JUN 2", with the year added
// once it differs from the current one). Uniqueness matters because the UI
// keys day-section headers off this string.
QString friendlyDayLabel(const QDate &date)
{
    if (!date.isValid()) {
        return QStringLiteral("UNDATED");
    }
    const QDate today = QDate::currentDate();
    const qint64 delta = date.daysTo(today);
    if (delta == 0) {
        return QStringLiteral("TODAY");
    }
    if (delta == 1) {
        return QStringLiteral("YESTERDAY");
    }
    const QString pattern = date.year() == today.year()
                                ? QStringLiteral("dddd · MMM d")
                                : QStringLiteral("dddd · MMM d, yyyy");
    return date.toString(pattern).toUpper();
}

} // namespace

NotesStore::NotesStore(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(AppPaths::dataDirectory());
    QDir().mkpath(sessionsDirectory());
    m_saveTimer.setInterval(500);
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &NotesStore::saveDraft);
    loadDraft();
    loadArchive();
    // A draft left over from a previous day shouldn't surface under "today" —
    // file it under the day it was written and start today blank.
    rolloverStaleDraft();
    m_midnightTimer.setSingleShot(true);
    connect(&m_midnightTimer, &QTimer::timeout, this, [this] {
        rolloverStaleDraft();
        scheduleMidnightRollover();
    });
    scheduleMidnightRollover();
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
    // Stamp a free-form draft (one written without engaging a routine) with the
    // day it was first written, so the midnight/launch rollover can scope it.
    if (!m_draftStartedAt.isValid() && !m_text.trimmed().isEmpty()) {
        m_draftStartedAt = QDateTime::currentDateTime();
    }
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
    if (draftIsForToday()) {
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
    if (draftIsForToday()) {
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
        entry.insert(QStringLiteral("dayKey"), note.endedAt.toLocalTime().date().toString(Qt::ISODate));
        entry.insert(QStringLiteral("dateGroup"), friendlyDayLabel(note.endedAt.toLocalTime().date()));
        entry.insert(QStringLiteral("timeLabel"), note.endedAt.toLocalTime().toString(QStringLiteral("HH:mm")));
        entry.insert(QStringLiteral("minutes"), note.minutes);
        entry.insert(QStringLiteral("result"), note.result);
        entry.insert(QStringLiteral("focusRating"), note.focusRating);
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
    if (draftIsForToday()) {
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
    if (target == QDate::currentDate() && draftIsForToday()) {
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
    if (target == QDate::currentDate() && draftIsForToday()) {
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
        entry.insert(QStringLiteral("detail"), note.focusRating > 0
                         ? QStringLiteral("%1M  ■  %2  ■  FOCUS %3/5")
                               .arg(QString::number(qMax(0, note.minutes)),
                                    note.result.toUpper(),
                                    QString::number(note.focusRating))
                         : QStringLiteral("%1M  ■  %2")
                               .arg(QString::number(qMax(0, note.minutes)),
                                    note.result.toUpper()));
        entry.insert(QStringLiteral("minutes"), qMax(0, note.minutes));
        entry.insert(QStringLiteral("result"), note.result);
        entry.insert(QStringLiteral("focusRating"), note.focusRating);
        entry.insert(QStringLiteral("noteText"), note.text.trimmed());
        entry.insert(QStringLiteral("hasNote"), !note.text.trimmed().isEmpty());
        entry.insert(QStringLiteral("sortKey"), started.isValid() ? started.toMSecsSinceEpoch() : ended.toMSecsSinceEpoch());
        rows.append(entry);
    }

    if (target == QDate::currentDate() && draftIsForToday()) {
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
        entry.insert(QStringLiteral("focusRating"), note.focusRating);
        entry.insert(QStringLiteral("text"), note.text);
        return entry;
    }
    return {};
}

QVariantList NotesStore::searchNotes(const QString &query) const
{
    QVariantList results;
    const QStringList terms = query.toLower().split(QRegularExpression(QStringLiteral("\\s+")),
                                                    Qt::SkipEmptyParts);
    if (terms.isEmpty()) {
        return results;
    }

    // Build the candidate set: every archived session plus today's live draft, so
    // an in-progress note is searchable the moment it's typed.
    QVector<SessionNote> candidates = m_archive;
    if (draftIsForToday()) {
        SessionNote draft;
        draft.sessionId = QStringLiteral("draft");
        draft.routineId = m_draftRoutineId;
        draft.routineName = m_draftRoutineName.isEmpty() ? QStringLiteral("CURRENT DRAFT")
                                                         : m_draftRoutineName;
        draft.startedAt = m_draftStartedAt.isValid() ? m_draftStartedAt : QDateTime::currentDateTime();
        draft.endedAt = QDateTime::currentDateTime();
        draft.result = QStringLiteral("draft");
        draft.text = m_text;
        candidates.append(draft);
    }

    // Newest first, like sessionHistory.
    for (int i = candidates.size() - 1; i >= 0; --i) {
        const SessionNote &note = candidates.at(i);
        const QString haystack = (note.routineName + QLatin1Char(' ') + note.result
                                  + QLatin1Char(' ') + note.text).toLower();
        bool all = true;
        for (const QString &term : terms) {
            if (!haystack.contains(term)) {
                all = false;
                break;
            }
        }
        if (!all) {
            continue;
        }

        const QString marked = markedSnippet(note.text, terms);
        QVariantMap entry;
        entry.insert(QStringLiteral("sessionId"), note.sessionId);
        entry.insert(QStringLiteral("routineId"), note.routineId);
        entry.insert(QStringLiteral("routineName"), note.routineName);
        entry.insert(QStringLiteral("startedAt"), note.startedAt.toLocalTime().toString(Qt::ISODate));
        entry.insert(QStringLiteral("endedAt"), note.endedAt.toLocalTime().toString(Qt::ISODate));
        entry.insert(QStringLiteral("dateLabel"), note.endedAt.toLocalTime().date().toString(QStringLiteral("yyyy-MM-dd")));
        entry.insert(QStringLiteral("dayKey"), note.endedAt.toLocalTime().date().toString(Qt::ISODate));
        entry.insert(QStringLiteral("dateGroup"), friendlyDayLabel(note.endedAt.toLocalTime().date()));
        entry.insert(QStringLiteral("timeLabel"), note.endedAt.toLocalTime().toString(QStringLiteral("HH:mm")));
        entry.insert(QStringLiteral("minutes"), note.minutes);
        entry.insert(QStringLiteral("result"), note.result);
        entry.insert(QStringLiteral("focusRating"), note.focusRating);
        entry.insert(QStringLiteral("hasNote"), !note.text.trimmed().isEmpty());
        entry.insert(QStringLiteral("snippet"), plainFromMarked(marked));
        entry.insert(QStringLiteral("snippetHtml"), htmlFromMarked(marked));
        results.append(entry);
    }
    return results;
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

bool NotesStore::recordSessionFocusRating(int rating)
{
    if (m_archive.isEmpty()) {
        return false;
    }
    const int clamped = qBound(0, rating, 5);
    SessionNote &note = m_archive.last();
    if (note.focusRating == clamped) {
        return false;
    }
    note.focusRating = clamped;
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
    const QDateTime startedLocal = startedAt.isValid()
                                       ? startedAt.toLocalTime()
                                       : (m_draftStartedAt.isValid() ? m_draftStartedAt : QDateTime::currentDateTime());
    const QDateTime endedLocal = endedAt.isValid() ? endedAt.toLocalTime() : QDateTime::currentDateTime();

    // Open-ended continuation: the same logical session (same routine + start) is
    // finalized again after a "Continue", with a larger total. Fold it into the
    // note already archived at expiry — extend its time, append any new debrief
    // text typed during the continuation — instead of writing a second note for
    // one unbroken session.
    if (!m_archive.isEmpty()) {
        SessionNote &existing = m_archive.last();
        if (existing.routineId == routineId && existing.startedAt == startedLocal) {
            existing.minutes = qMax(existing.minutes, qMax(0, minutes));
            existing.endedAt = endedLocal;
            existing.result = result;
            const QString addition = m_text.trimmed();
            if (!addition.isEmpty()) {
                const QString prior = existing.text.trimmed();
                existing.text = prior.isEmpty() ? addition
                                                : prior + QStringLiteral("\n\n") + addition;
            }
            writeSessionFile(existing);

            m_text.clear();
            m_draftRoutineId.clear();
            m_draftRoutineName.clear();
            m_draftStartedAt = {};
            saveDraft();

            emit textChanged();
            emit draftChanged();
            emit archiveChanged();
            return;
        }
    }

    SessionNote note;
    note.routineId = routineId;
    note.routineName = routineName;
    note.minutes = qMax(0, minutes);
    note.result = result;
    note.startedAt = startedLocal;
    note.endedAt = endedLocal;
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

bool NotesStore::draftIsForToday() const
{
    if (m_text.trimmed().isEmpty()) {
        return false;
    }
    // An undated draft is treated as today's until it gets a timestamp.
    if (!m_draftStartedAt.isValid()) {
        return true;
    }
    return m_draftStartedAt.toLocalTime().date() == QDate::currentDate();
}

void NotesStore::rolloverStaleDraft()
{
    // Nothing to do unless there's a non-empty draft stamped before today.
    if (m_text.trimmed().isEmpty() || !m_draftStartedAt.isValid()) {
        return;
    }
    const QDate draftDate = m_draftStartedAt.toLocalTime().date();
    if (draftDate >= QDate::currentDate()) {
        return;
    }

    // Preserve the work: archive it under the day it was actually written (so it
    // shows in that day's timeline) rather than silently dropping it. minutes=0
    // and result "carried" mark it as a rolled-over note, not a focus session.
    SessionNote note;
    note.routineId = m_draftRoutineId;
    note.routineName = m_draftRoutineName.isEmpty() ? QStringLiteral("CARRIED NOTE")
                                                    : m_draftRoutineName;
    note.minutes = 0;
    note.result = QStringLiteral("carried");
    note.startedAt = m_draftStartedAt;
    note.endedAt = m_draftStartedAt;
    note.sessionId = sessionIdFromTimestamp(note.endedAt) + QStringLiteral("-") + safeSlug(note.routineName);
    note.text = m_text;
    archiveSession(note);

    // Clear the live editor so today starts blank.
    m_text.clear();
    m_draftRoutineId.clear();
    m_draftRoutineName.clear();
    m_draftStartedAt = {};
    saveDraft();

    emit textChanged();
    emit draftChanged();
    emit archiveChanged();
}

void NotesStore::scheduleMidnightRollover()
{
    const QDateTime now = QDateTime::currentDateTime();
    // A second past midnight avoids landing exactly on the boundary.
    const QDateTime nextMidnight(now.date().addDays(1), QTime(0, 0, 1));
    const qint64 ms = now.msecsTo(nextMidnight);
    m_midnightTimer.start(static_cast<int>(qBound<qint64>(1000, ms, 24LL * 60 * 60 * 1000)));
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
            note.focusRating = qBound(0, object.value(QStringLiteral("focus_rating")).toInt(), 5);
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
    if (note.focusRating > 0) {
        root.insert(QStringLiteral("focus_rating"), note.focusRating);
    }
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
        stream << "- result:  " << note.result << "\n";
        if (note.focusRating > 0) {
            stream << "- focus:   " << note.focusRating << "/5\n";
        }
        stream << "\n";
        stream << note.text << "\n";
        mdFile.commit();
    }
}

QString NotesStore::sessionsDirectory()
{
    return AppPaths::filePath(QStringLiteral("sessions"));
}

QString NotesStore::draftPath()
{
    return AppPaths::filePath(QStringLiteral("notes.json"));
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
