#include "core/TOTPEngine.h"

#include <QtTest/QtTest>

class TOTPTests final : public QObject
{
    Q_OBJECT

private slots:
    void rfc6238Sha1Vectors_data()
    {
        QTest::addColumn<quint64>("timestamp");
        QTest::addColumn<QString>("expected");

        QTest::newRow("59") << quint64(59) << QStringLiteral("94287082");
        QTest::newRow("1111111109") << quint64(1111111109) << QStringLiteral("07081804");
        QTest::newRow("1111111111") << quint64(1111111111) << QStringLiteral("14050471");
        QTest::newRow("1234567890") << quint64(1234567890) << QStringLiteral("89005924");
        QTest::newRow("2000000000") << quint64(2000000000) << QStringLiteral("69279037");
        QTest::newRow("20000000000") << quint64(20000000000) << QStringLiteral("65353130");
    }

    void rfc6238Sha1Vectors()
    {
        QFETCH(quint64, timestamp);
        QFETCH(QString, expected);

        const QString secret = QStringLiteral("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
        QCOMPARE(TOTPEngine::generateCode(secret, timestamp / 30, 8), expected);
    }
};

QTEST_GUILESS_MAIN(TOTPTests)

#include "totp_tests.moc"
