#include "core/StatsStore.h"

#include <QDateTime>
#include <QTemporaryDir>
#include <QtTest/QtTest>

// StatsStore persists under ~/.focusos/stats.json, resolved from $HOME via
// AppPaths. Each test points HOME at a throwaway directory so the suite never
// touches the real focus log and every case starts from a clean slate.
class StatsTests final : public QObject
{
    Q_OBJECT

private:
    static QDateTime dayAt(int dayOffset, int hour)
    {
        return QDateTime(QDate::currentDate().addDays(dayOffset), QTime(hour, 0));
    }

    // Drives one completed/ended session through the public recording slot, the
    // same path RoutineManager wires up at runtime.
    static void logSession(StatsStore &store,
                           const QString &id,
                           const QString &name,
                           int minutes,
                           const QString &result,
                           const QDateTime &started)
    {
        store.recordRoutineSession(id, name, minutes, result, started,
                                   started.addSecs(minutes * 60));
    }

private slots:
    void countsAndTotals()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        StatsStore store;
        logSession(store, "r1", "Deep Work", 60, "completed", dayAt(0, 10));
        logSession(store, "r2", "Mail", 30, "unlocked", dayAt(0, 12));
        logSession(store, "r3", "Build", 20, "interrupted", dayAt(0, 14));

        QCOMPARE(store.completedSessions(), 1);
        QCOMPARE(store.unlockedSessions(), 1);
        QCOMPARE(store.interruptedSessions(), 1);
        QCOMPARE(store.totalSessions(), 3);
        QCOMPARE(store.totalFocusMinutes(), 110);
        QCOMPARE(store.todayFocusMinutes(), 110);
        QCOMPARE(store.weekFocusMinutes(), 110);
        QCOMPARE(store.averageSessionMinutes(), 110 / 3);
    }

    void streakAnalytics()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        StatsStore store;
        // A 4-day run well in the past, plus an isolated session today: the
        // current streak is 1, but the all-time longest is 4.
        logSession(store, "r", "Block", 30, "completed", dayAt(-6, 9));
        logSession(store, "r", "Block", 30, "completed", dayAt(-5, 9));
        logSession(store, "r", "Block", 30, "completed", dayAt(-4, 9));
        logSession(store, "r", "Block", 30, "completed", dayAt(-3, 9));
        logSession(store, "r", "Block", 30, "completed", dayAt(0, 9));

        QCOMPARE(store.currentStreakDays(), 1);
        QCOMPARE(store.longestStreakDays(), 4);
    }

    void bestDayPicksHighestTotal()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        StatsStore store;
        logSession(store, "r", "AM", 60, "completed", dayAt(0, 9));
        logSession(store, "r", "PM", 30, "completed", dayAt(0, 15)); // today: 90
        logSession(store, "r", "Old", 45, "completed", dayAt(-2, 9)); // -2 days: 45

        QCOMPARE(store.bestDayMinutes(), 90);
        QCOMPARE(store.bestDayLabel(), QStringLiteral("TODAY"));
    }

    void focusRatingRoundTripsThroughDisk()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        {
            StatsStore store;
            logSession(store, "r1", "Deep Work", 60, "completed", dayAt(0, 10));
            store.recordLastSessionFocusRating(4);
            QCOMPARE(store.ratedSessions(), 1);
            QCOMPARE(store.averageFocusRating(), 4.0);
            // Out-of-range ratings clamp; unrated sessions are excluded.
            store.recordLastSessionFocusRating(9);
            QCOMPARE(store.averageFocusRating(), 5.0);
            store.recordLastSessionFocusRating(4);
        }

        // A fresh store reloads the rating from stats.json.
        StatsStore reopened;
        QCOMPARE(reopened.ratedSessions(), 1);
        QCOMPARE(reopened.averageFocusRating(), 4.0);

        // A second rated session moves the average.
        logSession(reopened, "r2", "Review", 30, "completed", dayAt(0, 13));
        reopened.recordLastSessionFocusRating(2);
        QCOMPARE(reopened.ratedSessions(), 2);
        QCOMPARE(reopened.averageFocusRating(), 3.0);
    }

    void distractionTallyPersists()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        {
            StatsStore store;
            QCOMPARE(store.totalDistractionsBlocked(), 0);
            store.noteDistractionBlocked();
            store.noteDistractionBlocked();
            store.noteDistractionBlocked();
            QCOMPARE(store.totalDistractionsBlocked(), 3);
        }

        // The lifetime tally survives a restart and keeps accumulating.
        StatsStore reopened;
        QCOMPARE(reopened.totalDistractionsBlocked(), 3);
        reopened.noteDistractionBlocked();
        QCOMPARE(reopened.totalDistractionsBlocked(), 4);
    }
};

QTEST_GUILESS_MAIN(StatsTests)

#include "stats_tests.moc"
