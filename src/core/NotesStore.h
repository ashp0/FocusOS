#pragma once

#include <QDateTime>
#include <QObject>
#include <QVariantMap>
#include <QTimer>
#include <QVariantList>
#include <QVector>

struct SessionNote
{
    QString sessionId;
    QString routineId;
    QString routineName;
    QDateTime startedAt;
    QDateTime endedAt;
    int minutes = 0;
    QString result;
    QString text;
};

class NotesStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString draftRoutineName READ draftRoutineName NOTIFY draftChanged)
    Q_PROPERTY(QString todayCombinedNotes READ todayCombinedNotes NOTIFY archiveChanged)
    Q_PROPERTY(int todayNoteCount READ todayNoteCount NOTIFY archiveChanged)
    Q_PROPERTY(QVariantList sessionHistory READ sessionHistory NOTIFY archiveChanged)
    Q_PROPERTY(QVariantList availableDates READ availableDates NOTIFY archiveChanged)

public:
    explicit NotesStore(QObject *parent = nullptr);

    QString text() const;
    void setText(const QString &text);
    QString draftRoutineName() const;
    QString todayCombinedNotes() const;
    int todayNoteCount() const;
    QVariantList sessionHistory() const;
    QVariantList availableDates() const;

    Q_INVOKABLE QString sessionNoteText(const QString &sessionId) const;
    Q_INVOKABLE QVariantMap sessionNote(const QString &sessionId) const;
    // Full-text recall across every archived session (and today's live draft).
    // Space-separated terms are ANDed, case-insensitively, against the routine
    // name + result + note body. Each hit carries the same shape as a
    // sessionHistory row plus a `snippet`/`snippetHtml` context window so the
    // user can find "that thing I noted last week" without scrolling the log.
    Q_INVOKABLE QVariantList searchNotes(const QString &query) const;
    Q_INVOKABLE bool updateSessionNote(const QString &sessionId, const QString &text);
    Q_INVOKABLE bool recordSessionReflection(const QString &reflection);
    Q_INVOKABLE QString combinedNotesForDate(const QString &date) const;
    Q_INVOKABLE QVariantMap timelineSummaryForDate(const QString &date) const;
    Q_INVOKABLE QVariantList timelineForDate(const QString &date) const;

public slots:
    void onRoutineEngaged(const QString &routineId, const QString &routineName);
    void onRoutineSessionFinished(const QString &routineId,
                                  const QString &routineName,
                                  int minutes,
                                  const QString &result,
                                  const QDateTime &startedAt,
                                  const QDateTime &endedAt);

signals:
    void textChanged();
    void draftChanged();
    void archiveChanged();

private:
    void loadDraft();
    void saveDraft();
    void loadArchive();
    void archiveSession(const SessionNote &note);
    void writeSessionFile(const SessionNote &note) const;
    // Daily scoping. The live draft belongs to the day it was started/last
    // written; once a new day begins it is no longer "today's note". On launch
    // and at midnight we archive a leftover draft under its own day and clear
    // the live editor so today starts blank. draftIsForToday() is the display
    // guard the "Today's Notes" surfaces use in the meantime.
    bool draftIsForToday() const;
    void rolloverStaleDraft();
    void scheduleMidnightRollover();
    static QString sessionsDirectory();
    static QString draftPath();
    static QString sessionFilePath(const QDate &date, const QString &sessionId);
    static QString formatSession(const SessionNote &note);
    static QDate parseDateOrToday(const QString &date);

    QString m_text;
    QString m_draftRoutineId;
    QString m_draftRoutineName;
    QDateTime m_draftStartedAt;
    QTimer m_saveTimer;
    QTimer m_midnightTimer;
    QVector<SessionNote> m_archive;
};
