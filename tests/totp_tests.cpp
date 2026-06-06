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

    // The shipping enrollment uses the default 6-digit code; pin those too. RFC
    // 6238's 6-digit value is the low six digits of the 8-digit value (both are a
    // mod-10^n of the same dynamic-truncation integer), which the next test
    // asserts as an invariant — these are the concrete expected strings.
    void rfc6238Sha1Vectors6Digit_data()
    {
        QTest::addColumn<quint64>("timestamp");
        QTest::addColumn<QString>("expected");

        QTest::newRow("59") << quint64(59) << QStringLiteral("287082");
        QTest::newRow("1111111109") << quint64(1111111109) << QStringLiteral("081804");
        QTest::newRow("1111111111") << quint64(1111111111) << QStringLiteral("050471");
        QTest::newRow("1234567890") << quint64(1234567890) << QStringLiteral("005924");
        QTest::newRow("2000000000") << quint64(2000000000) << QStringLiteral("279037");
        QTest::newRow("20000000000") << quint64(20000000000) << QStringLiteral("353130");
    }

    void rfc6238Sha1Vectors6Digit()
    {
        QFETCH(quint64, timestamp);
        QFETCH(QString, expected);

        const QString secret = QStringLiteral("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
        const QString code = TOTPEngine::generateCode(secret, timestamp / 30);  // default 6
        QCOMPARE(code, expected);
        QCOMPARE(code.size(), 6);  // always zero-padded to width
    }

    // Invariant: the 6-digit code is exactly the last six characters of the
    // 8-digit code for the same step, for every step. Guards the digit-width
    // parameter and the zero-padding in one shot, across a wide step range.
    void digitWidthInvariant()
    {
        const QString secret = QStringLiteral("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
        for (quint64 step = 0; step < 5000; step += 137) {
            const QString six = TOTPEngine::generateCode(secret, step, 6);
            const QString eight = TOTPEngine::generateCode(secret, step, 8);
            QCOMPARE(six.size(), 6);
            QCOMPARE(eight.size(), 8);
            QCOMPARE(six, eight.right(6));
        }
    }

    // The secret a user pastes back during recovery may carry the spaces and
    // lowercasing authenticator apps display for readability ("gezd gnbv …").
    // base32Decode trims/upper-cases and skips non-alphabet characters, so all
    // these spellings must yield the same code as the canonical secret — otherwise
    // a faithful re-paste would silently lock the user out.
    void secretNormalization()
    {
        const QString canonical = QStringLiteral("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
        const quint64 step = 1234567890ull / 30;
        const QString reference = TOTPEngine::generateCode(canonical, step);
        QCOMPARE(reference, QStringLiteral("005924"));

        QCOMPARE(TOTPEngine::generateCode(canonical.toLower(), step), reference);
        QCOMPARE(TOTPEngine::generateCode(QStringLiteral("  ") + canonical + QStringLiteral("  "), step), reference);
        QCOMPARE(TOTPEngine::generateCode(
                     QStringLiteral("GEZD GNBV GY3T QOJQ GEZD GNBV GY3T QOJQ"), step),
                 reference);
        // Padding '=' (some encoders emit it) is ignored too.
        QCOMPARE(TOTPEngine::generateCode(canonical + QStringLiteral("======"), step), reference);
    }

    // An empty / junk secret must not crash or throw — it should just produce a
    // well-formed (if meaningless) zero-padded code. Hardens the decode path
    // against the corrupt-secret case the recovery UI exists to handle.
    void degenerateSecretsAreSafe()
    {
        QCOMPARE(TOTPEngine::generateCode(QString(), 42).size(), 6);
        QCOMPARE(TOTPEngine::generateCode(QStringLiteral("!!!!"), 42).size(), 6);
        QCOMPARE(TOTPEngine::generateCode(canonicalSecret(), 0, 8).size(), 8);
    }

private:
    static QString canonicalSecret()
    {
        return QStringLiteral("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
    }
};

QTEST_GUILESS_MAIN(TOTPTests)

#include "totp_tests.moc"
