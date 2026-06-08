#include "core/NotesStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

// NotesStore writes under ~/.focusos, and AppPaths resolves that from $HOME on
// Unix/macOS. Each test points HOME at a throwaway directory so the suite never
// touches the real notes archive and every case starts from a clean slate.
class NotesTests final : public QObject
{
    Q_OBJECT

private:
    // Drives one complete archived session through the public API, the same path
    // RoutineManager + main.cpp wire up at runtime.
    static void logSession(NotesStore &store,
                           const QString &routineId,
                           const QString &routineName,
                           const QString &noteText,
                           int minutes,
                           const QString &result,
                           const QDateTime &started,
                           const QDateTime &ended)
    {
        store.onRoutineEngaged(routineId, routineName);
        store.setText(noteText);
        store.onRoutineSessionFinished(routineId, routineName, minutes, result, started, ended);
    }

private slots:
    void searchMatchesBodyAndName()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        NotesStore store;
        const QDateTime base = QDateTime(QDate(2026, 6, 1), QTime(9, 0));
        logSession(store, "r1", "Deep Work", "Found a nasty bug in the JSON parser",
                   45, "complete", base, base.addSecs(45 * 60));
        logSession(store, "r2", "Admin", "Replied to email and filed expenses",
                   20, "complete", base.addSecs(3600), base.addSecs(3600 + 20 * 60));

        // Body term.
        QCOMPARE(store.searchNotes(QStringLiteral("bug")).size(), 1);
        // Routine-name term.
        QCOMPARE(store.searchNotes(QStringLiteral("admin")).size(), 1);
        // Multi-term AND across name + body.
        QCOMPARE(store.searchNotes(QStringLiteral("parser deep")).size(), 1);
        // A term present in neither candidate fails the AND.
        QCOMPARE(store.searchNotes(QStringLiteral("parser admin")).size(), 0);
        // No match at all.
        QVERIFY(store.searchNotes(QStringLiteral("xyzzy")).isEmpty());
        // Empty / whitespace query returns nothing rather than everything.
        QVERIFY(store.searchNotes(QString()).isEmpty());
        QVERIFY(store.searchNotes(QStringLiteral("   ")).isEmpty());
    }

    void searchIsCaseInsensitiveAndNewestFirst()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        NotesStore store;
        const QDateTime base = QDateTime(QDate(2026, 6, 2), QTime(8, 0));
        logSession(store, "r1", "Morning", "Outline of the SHIPPING plan",
                   30, "complete", base, base.addSecs(1800));
        logSession(store, "r2", "Evening", "Refine the shipping checklist",
                   30, "complete", base.addSecs(7200), base.addSecs(9000));

        const QVariantList hits = store.searchNotes(QStringLiteral("Shipping"));
        QCOMPARE(hits.size(), 2);
        // Newest session first (Evening was logged later).
        QCOMPARE(hits.first().toMap().value(QStringLiteral("routineName")).toString(),
                 QStringLiteral("Evening"));
    }

    void searchSnippetHighlightsTerm()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        NotesStore store;
        const QDateTime base = QDateTime(QDate(2026, 6, 3), QTime(10, 0));
        logSession(store, "r1", "Research",
                   "The migration script needs a rollback path before launch",
                   60, "complete", base, base.addSecs(3600));

        const QVariantList hits = store.searchNotes(QStringLiteral("rollback"));
        QCOMPARE(hits.size(), 1);
        const QVariantMap hit = hits.first().toMap();
        // Plain snippet carries the surrounding context...
        QVERIFY(hit.value(QStringLiteral("snippet")).toString().contains(
            QStringLiteral("rollback"), Qt::CaseInsensitive));
        // ...and the html variant bolds the matched term for the UI.
        QVERIFY(hit.value(QStringLiteral("snippetHtml")).toString().contains(
            QStringLiteral("<b>rollback</b>")));
    }

    void staleDraftRollsOverOnLaunch()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        // Seed a leftover draft stamped *yesterday* directly on disk, then bring a
        // NotesStore up: it should file the draft under its own day and clear the
        // live editor so today starts blank (no silent data loss).
        const QString dir = home.path() + QStringLiteral("/.focusos");
        QVERIFY(QDir().mkpath(dir));
        QJsonObject draft;
        draft.insert(QStringLiteral("text"), QStringLiteral("leftover idea from last night"));
        draft.insert(QStringLiteral("routine_name"), QStringLiteral("Late Session"));
        draft.insert(QStringLiteral("started_at"),
                     QDateTime::currentDateTime().addDays(-1).toString(Qt::ISODate));
        QFile file(dir + QStringLiteral("/notes.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(draft).toJson());
        file.close();

        NotesStore store;
        // Live editor cleared for today.
        QVERIFY(store.text().isEmpty());
        // The carried note was preserved in the archive and is searchable.
        const QVariantList hits = store.searchNotes(QStringLiteral("leftover"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().toMap().value(QStringLiteral("result")).toString(),
                 QStringLiteral("carried"));
    }

    // The MISSION LOG groups sessions into dated sections; the backend supplies a
    // unique-per-day `dateGroup` label (TODAY/YESTERDAY/weekday+date) and an ISO
    // `dayKey` on every history and search row. Same-day sessions must share the
    // group; different days must not.
    void sessionRowsCarryDayGrouping()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        NotesStore store;
        const QDateTime now = QDateTime::currentDateTime();
        // Logged in the order they happen (yesterday, then today's two) — the
        // archive preserves insertion order and sessionHistory reverses it.
        logSession(store, "r3", "Late Night", "yesterday's wrap up",
                   25, "completed", now.addDays(-1), now.addDays(-1).addSecs(1500));
        logSession(store, "r1", "Morning Block", "kickoff notes",
                   30, "completed", now.addSecs(-7200), now.addSecs(-5400));
        logSession(store, "r2", "Afternoon Block", "shipping the patch",
                   45, "completed", now.addSecs(-3600), now.addSecs(-900));

        const QVariantList rows = store.sessionHistory();
        QCOMPARE(rows.size(), 3);
        // Newest first; the two of today share one group, yesterday differs.
        const QString g0 = rows.at(0).toMap().value(QStringLiteral("dateGroup")).toString();
        const QString g1 = rows.at(1).toMap().value(QStringLiteral("dateGroup")).toString();
        const QString g2 = rows.at(2).toMap().value(QStringLiteral("dateGroup")).toString();
        QCOMPARE(g0, QStringLiteral("TODAY"));
        QCOMPARE(g1, QStringLiteral("TODAY"));
        QCOMPARE(g2, QStringLiteral("YESTERDAY"));
        // Every row also carries an ISO dayKey.
        QVERIFY(!rows.at(0).toMap().value(QStringLiteral("dayKey")).toString().isEmpty());
        // Search rows expose the same grouping fields.
        const QVariantList hits = store.searchNotes(QStringLiteral("shipping"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().toMap().value(QStringLiteral("dateGroup")).toString(),
                 QStringLiteral("TODAY"));
    }

    // The MISSION COMPLETE focus rating is folded into the most recent note and
    // surfaces in the history rows (and survives a reload), so the mission log can
    // show it next to the debrief.
    void focusRatingFoldsIntoNoteAndPersists()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        const QDateTime base = QDateTime(QDate(2026, 6, 1), QTime(9, 0));
        {
            NotesStore store;
            logSession(store, "r1", "Deep Work", "shipped the parser fix",
                       45, "completed", base, base.addSecs(45 * 60));
            QVERIFY(store.recordSessionFocusRating(4));
            QCOMPARE(store.sessionHistory().first().toMap()
                         .value(QStringLiteral("focusRating")).toInt(), 4);
        }

        NotesStore reopened;
        QCOMPARE(reopened.sessionHistory().first().toMap()
                     .value(QStringLiteral("focusRating")).toInt(), 4);
    }

    // Open-ended "Continue": finishing the continuation re-fires
    // onRoutineSessionFinished with the SAME routine id + start and a larger total.
    // That must extend the original note (more minutes, appended debrief) — one
    // mission-log entry for one unbroken session, not two.
    void openEndedContinuationExtendsNote()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        NotesStore store;
        const QDateTime started = QDateTime(QDate(2026, 6, 2), QTime(8, 0));

        // Original 60-minute session, archived at expiry with its in-session notes.
        store.onRoutineEngaged("deep", "Deep Work");
        store.setText("first hour notes");
        store.onRoutineSessionFinished("deep", "Deep Work", 60, "completed",
                                       started, started.addSecs(60 * 60));
        QCOMPARE(store.sessionHistory().size(), 1);

        // Continuation: fresh draft for the same routine, then finish with the full
        // 90-minute total and the original start.
        store.onRoutineEngaged("deep", "Deep Work");
        store.setText("continuation notes");
        store.onRoutineSessionFinished("deep", "Deep Work", 90, "completed",
                                       started, started.addSecs(90 * 60));

        const QVariantList rows = store.sessionHistory();
        QCOMPARE(rows.size(), 1);
        const QVariantMap row = rows.first().toMap();
        QCOMPARE(row.value(QStringLiteral("minutes")).toInt(), 90);
        const QString text = store.sessionNoteText(row.value(QStringLiteral("sessionId")).toString());
        QVERIFY(text.contains(QStringLiteral("first hour notes")));
        QVERIFY(text.contains(QStringLiteral("continuation notes")));
    }
};

QTEST_GUILESS_MAIN(NotesTests)

#include "notes_tests.moc"
