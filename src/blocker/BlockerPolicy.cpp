#include "blocker/BlockerPolicy.h"

#include "blocker/BlockerSecret.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// On-disk container layout: MAGIC + VERSION + payload + HMAC-SHA256(of all
// preceding bytes). Bump FORMAT_VERSION if the payload schema changes.
const QByteArray kMagic = QByteArrayLiteral("FOSB");
constexpr quint8 kFormatVersion = 1;
constexpr int kMacSize = 32; // SHA-256
// Pin the stream version so a Qt upgrade can't change the serialized layout.
constexpr QDataStream::Version kStreamVersion = QDataStream::Qt_6_5;

QByteArray homeFocusDir()
{
    return (QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QStringLiteral("/.focusos"))
        .toUtf8();
}

QByteArray computeMac(const QByteArray &signedRegion)
{
    return QMessageAuthenticationCode::hash(signedRegion,
                                            BlockerSecret::hmacKey(),
                                            QCryptographicHash::Sha256);
}

} // namespace

namespace BlockerPolicy {

QString blockerDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                        + QStringLiteral("/.focusos/blocker");
    QDir().mkpath(dir);
    return dir;
}

QString policyFilePath()
{
    return blockerDir() + QStringLiteral("/policy.dat");
}

QString heartbeatFilePath()
{
    return blockerDir() + QStringLiteral("/host-alive");
}

void write(bool active, const QStringList &allowlist)
{
    QStringList hosts;
    for (const QString &raw : allowlist) {
        const QString host = normalizeHost(raw);
        if (!host.isEmpty() && !hosts.contains(host)) {
            hosts.append(host);
        }
    }

    QByteArray payload;
    {
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setVersion(kStreamVersion);
        stream << active << hosts << QDateTime::currentMSecsSinceEpoch();
    }

    QByteArray blob = kMagic;
    blob.append(static_cast<char>(kFormatVersion));
    blob.append(payload);
    blob.append(computeMac(blob));

    // QSaveFile gives us the temp-write + atomic-rename the host relies on to
    // never observe a torn file mid-update.
    QSaveFile file(policyFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(blob);
        file.commit();
    }

    // Retire the legacy plaintext file from earlier builds, if present.
    QFile::remove(blockerDir() + QStringLiteral("/policy.json"));
}

State read()
{
    State state;

    QFile file(policyFilePath());
    bool valid = false;
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray blob = file.readAll();
        const int headerSize = kMagic.size() + 1; // magic + version byte
        if (blob.size() >= headerSize + kMacSize
            && blob.startsWith(kMagic)
            && static_cast<quint8>(blob.at(kMagic.size())) == kFormatVersion) {
            const QByteArray signedRegion = blob.left(blob.size() - kMacSize);
            const QByteArray storedMac = blob.right(kMacSize);
            if (computeMac(signedRegion) == storedMac) {
                const QByteArray payload = signedRegion.mid(headerSize);
                QDataStream stream(payload);
                stream.setVersion(kStreamVersion);
                QStringList hosts;
                qint64 issuedAt = 0;
                stream >> state.active >> hosts >> issuedAt;
                if (stream.status() == QDataStream::Ok) {
                    for (const QString &host : hosts) {
                        const QString normalized = normalizeHost(host);
                        if (!normalized.isEmpty()) {
                            state.allowlist.append(normalized);
                        }
                    }
                    valid = true;
                }
            }
        }
    }

    if (valid) {
        return state;
    }

    // Missing / tampered / corrupt. Fail closed while a routine is live so
    // editing or deleting the file can't unlock the session; otherwise allow
    // all (a clean machine that never started a session isn't punished).
    State fallback;
    if (sessionCheckpointActive()) {
        fallback.active = true; // block everything (empty allowlist)
    }
    return fallback;
}

bool sessionCheckpointActive()
{
    QFile file(QString::fromUtf8(homeFocusDir()) + QStringLiteral("/active.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }
    return document.object().value(QStringLiteral("remaining_seconds")).toInt(0) > 0;
}

QString normalizeHost(const QString &raw)
{
    QString host = raw.trimmed();
    if (host.isEmpty() || host.startsWith(QLatin1Char('#'))) {
        return QString();
    }

    // Drop the scheme (everything up to and including "//").
    const int schemeSep = host.indexOf(QStringLiteral("//"));
    if (schemeSep >= 0) {
        host = host.mid(schemeSep + 2);
    }

    if (host.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
        host = host.mid(4);
    }

    while (host.endsWith(QLatin1Char('/'))) {
        host.chop(1);
    }

    return host.toLower();
}

} // namespace BlockerPolicy
